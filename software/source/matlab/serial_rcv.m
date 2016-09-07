function ret = serial_rcv(file_number)

more off;
pkg load instrument-control
%h_s = serial('/dev/ttyUSB3', 3000000);
h_s = fopen('~/out.raw','r');

fid = fopen(['serial_rcv_log',num2str(file_number),'.out'],'w');

NUM_RANGING_BROADCASTS = 30;
global NUM_RANGING_CHANNELS = 3;
HEADER = [0x80, 0x01, 0x80, 0x01];
DATA_HEADER = [0x80, 0x80];

%Flush serial port
%srl_flush(h_s);

for kk=1:200
	%First, wait for the header
	cur_header = srl_tee(h_s, fid, length(HEADER));
	while !isequal(cur_header, HEADER)
		cur_header = [cur_header(2:end), srl_tee(h_s, fid, 1)];
	end

	% Read the number of anchors that have been heard from
	num_anchors = srl_tee(h_s, fid, 1);

	disp(['Got ranging event with ', num2str(num_anchors), ' anchors'])

	% Read in the broadcast TX times
	ranging_broadcast_ss_send_times = zeros(NUM_RANGING_BROADCASTS, 1);
	for ii=1:NUM_RANGING_BROADCASTS
		ranging_broadcast_ss_send_times(ii) = srl_to_double(srl_tee(h_s, fid, 8));
	end
	
	tag_poll_TOAs = zeros(NUM_RANGING_BROADCASTS,1);
	for ii=1:num_anchors
		% Make sure the data header is correct.  Otherwise the packet is corrupt
		cur_data_header = srl_tee(h_s, fid, 2);
		if !isequal(cur_data_header, DATA_HEADER)
			disp('ERROR: CORRUPT DATA HEADER')
			break;
		end

		% Read in this anchor's data
		anchor_eui = srl_tee(h_s, fid, 8);
		
		anchor_final_antenna_index = srl_tee(h_s, fid, 1);
		window_packet_recv = srl_tee(h_s, fid, 1);
		anc_final_tx_timestamp = srl_to_double(srl_tee(h_s, fid, 8));
		anc_final_rx_timestamp = srl_to_double(srl_tee(h_s, fid, 8));
		tag_poll_first_idx = srl_tee(h_s, fid, 1)+1;
		tag_poll_first_TOA = srl_to_double(srl_tee(h_s, fid, 8));
		tag_poll_last_idx = srl_tee(h_s, fid, 1)+1;
		tag_poll_last_TOA = srl_to_double(srl_tee(h_s, fid, 8));
		if tag_poll_first_idx > 30 || tag_poll_first_idx < 1 || tag_poll_last_idx > 30 || tag_poll_last_idx < 1
			continue;
		end
		for jj=1:NUM_RANGING_BROADCASTS
			tag_poll_TOAs(jj) = srl_to_double(srl_tee(h_s, fid, 2));
		end

		% Perform ranging operations with the received timestamp data
		tag_poll_TOAs(tag_poll_first_idx) = tag_poll_first_TOA;
		tag_poll_TOAs(tag_poll_last_idx) = tag_poll_last_TOA;

		approx_clock_offset = (tag_poll_last_TOA - tag_poll_first_TOA)/(ranging_broadcast_ss_send_times(tag_poll_last_idx) - ranging_broadcast_ss_send_times(tag_poll_first_idx));

		% Interpolate betseen the first and last to find the high 48 bits which fit best
		for jj=tag_poll_first_idx+1:tag_poll_last_idx-1
			estimated_toa = tag_poll_first_TOA + (approx_clock_offset*(ranging_broadcast_ss_send_times(jj) - ranging_broadcast_ss_send_times(tag_poll_first_idx)));
			actual_toa = bitand(estimated_toa, 0xFFFFFFFFFFF0000) + tag_poll_TOAs(jj);

			if(actual_toa < estimated_toa - 0x7FFF)
				actual_toa = actual_toa + 0x10000;
			elseif(actual_toa > estimated_toa + 0x7FFF)
				actual_toa = actual_toa - 0x10000;
			end

			tag_poll_TOAs(jj) = actual_toa;
		end

		% Get the actual clock offset calculation
		num_valid_offsets = 0;
		offset_cumsum = 0;
		for jj=1:NUM_RANGING_CHANNELS
			if(bitand(tag_poll_TOAs(jj), 0xFFFF) > 0 && bitand(tag_poll_TOAs(27+jj), 0xFFFF) > 0)
				offset_cumsum = offset_cumsum + (tag_poll_TOAs(27+jj) - tag_poll_TOAs(jj))/(ranging_broadcast_ss_send_times(27+jj) - ranging_broadcast_ss_send_times(jj));
				num_valid_offsets = num_valid_offsets + 1;
			end
		end
		offset_anchor_over_tag = offset_cumsum/num_valid_offsets;

		% Figure out what broadcast the received response belongs to
		ss_index_matching = oneway_get_ss_index_from_settings(anchor_final_antenna_index, window_packet_recv) + 1;
		if(ss_index_matching < 1 || ss_index_matching > 30)
			continue;
		end
		if(bitand(tag_poll_TOAs(ss_index_matching), 0xFFFF) == 0)
			continue;
		end

		matching_broadcast_send_time = ranging_broadcast_ss_send_times(ss_index_matching);
		matching_broadcast_recv_time = tag_poll_TOAs(ss_index_matching);
		response_send_time = anc_final_tx_timestamp;
		response_recv_time = anc_final_rx_timestamp;

		two_way_TOF = ((response_recv_time - matching_broadcast_send_time)*offset_anchor_over_tag) - (response_send_time - matching_broadcast_recv_time);
		one_way_TOF = two_way_TOF/2;

		% Declare an array for sorting ranges
		distances_millimeters = ones(NUM_RANGING_BROADCASTS, 1)*NaN;
		for jj=1:NUM_RANGING_BROADCASTS
			broadcast_send_time = ranging_broadcast_ss_send_times(jj);
			broadcast_recv_time = tag_poll_TOAs(jj);
			if(bitand(broadcast_recv_time, 0xFFFF) == 0)
				continue;
			end

			broadcast_anchor_offset = broadcast_recv_time - matching_broadcast_recv_time;
			broadcast_tag_offset = broadcast_send_time - matching_broadcast_send_time;
			TOF = broadcast_anchor_offset - broadcast_tag_offset*offset_anchor_over_tag + one_way_TOF;

			distance_millimeters(jj) = dwtime_to_millimeters(TOF);
		end

		% Get rid of all invalid distances
		distance_millimeters = distance_millimeters(!isnan(distance_millimeters));
		
		anchor_eui_txt = dec2hex(anchor_eui);
		range = prctile(distance_millimeters.',10);
		disp(['eui = ',anchor_eui_txt(1,:),' range = ', num2str(floor(range))])
	end
end

fclose(h_s)
fclose(fid)
