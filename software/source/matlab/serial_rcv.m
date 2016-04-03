more off;
pkg load instrument-control
h_s = serial('/dev/ttyUSB0', 3000000);

NUM_RANGING_BROADCASTS = 30;
HEADER = [0x80, 0x01, 0x80, 0x01];
DATA_HEADER = [0x80, 0x80];

%Flush serial port
srl_flush(h_s);

while(1)
	%First, wait for the header
	cur_header = srl_read(h_s, length(HEADER));
	while !isequal(cur_header, HEADER)
		cur_header = [cur_header(2:end), srl_read(h_s, 1)];
	end

	% Read the number of anchors that have been heard from
	num_anchors = srl_read(h_s, 1);

	disp(['Got ranging event with ', num2str(num_anchors), ' anchors'])

	% Read in the broadcast TX times
	ranging_broadcast_ss_send_times = zeros(NUM_RANGING_BROADCASTS, 1);
	for ii=1:NUM_RANGING_BROADCASTS
		ranging_broadcast_ss_send_times(ii) = srl_to_double(srl_read(h_s, 8));
	end
	
	tag_poll_TOAs = zeros(NUM_RANGING_BROADCASTS,1);
	for ii=1:num_anchors
		% Make sure the data header is correct.  Otherwise the packet is corrupt
		cur_data_header = srl_read(h_s, 2);
		if !isequal(cur_data_header, DATA_HEADER)
			disp('ERROR: CORRUPT DATA HEADER')
			break;
		end

		% Read in this anchor's data
		anchor_eui = srl_read(h_s, 8);
		anchor_final_antenna_index = srl_read(h_s, 1);
		window_packet_recv = srl_read(h_s, 1);
		anc_final_tx_timestamp = srl_to_double(srl_read(h_s, 8));
		anc_final_rx_timestamp = srl_to_double(srl_read(h_s, 8));
		tag_poll_first_idx = srl_read(h_s, 1);
		tag_poll_first_TOA = srl_to_double(srl_read(h_s, 8));
		tag_poll_last_idx = srl_read(h_s, 1);
		tag_poll_last_TOA = srl_to_double(srl_read(h_s, 8));
		for jj=1:NUM_RANGING_BROADCASTS
			tag_poll_TOAs(jj) = srl_to_double(srl_read(h_s, 2));
		end
		disp('GOT THROUGH ONE DATA HEADER')
	end
end

fclose(h_s)
