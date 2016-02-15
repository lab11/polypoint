clear all

fid = fopen('~/temp/polypoint/cal_test/anchor_20_tag_1e.csv','r');

%First line is useless.  Discard.
fgetl(fid);

%Parse the Saleae csv dump
spi_data = fscanf(fid,'%f,%d,0x%02X,0x%02X');
num_fields = 4;
num_rows = floor(length(spi_data)/num_fields);
spi_data = reshape(spi_data(1:num_rows*num_fields),[num_fields,num_rows]);

fclose(fid);

%Indices of all new SPI transactions based on ID
spi_time = spi_data(1,:);
spi_id   = spi_data(2,:);
spi_mosi = spi_data(3,:);
spi_miso = spi_data(4,:);
transaction_idxs = [1,find(diff(spi_id) > 0)+1];

%Stitch together all of the CIR data
tx_chunks = transaction_idxs(find(spi_mosi(transaction_idxs) == 137));
rx_chunks = transaction_idxs(find(spi_mosi(transaction_idxs) == 17));

diff_tx_chunks = diff(tx_chunks);
start_tx_chunk_idxs = [1,find(diff_tx_chunks > 192) + 1];
start_tx_chunks = tx_chunks(start_tx_chunk_idxs);
tx_chunks = tx_chunks(repmat(start_tx_chunk_idxs.',[1,30])+repmat(0:29,[length(start_tx_chunk_idxs),1]));
tx_timestamps = spi_mosi(tx_chunks-4) * 256 + spi_mosi(tx_chunks-3) * 256 * 256 + spi_mosi(tx_chunks-2) * 256 * 256 * 256 + spi_mosi(tx_chunks-1) * 256 * 256 * 256 * 256 + 33000;
num_ranges = size(tx_timestamps,1);
tx_antennas = zeros(num_ranges-1,1);
ranges = zeros(num_ranges,30);
distances_millimeters = zeros(num_ranges,1);

for ii=1:num_ranges
	if ii==num_ranges
		anchor_responses = rx_chunks((rx_chunks > start_tx_chunks(ii)));
	else
		anchor_responses = rx_chunks((rx_chunks > start_tx_chunks(ii)) & (rx_chunks < start_tx_chunks(ii+1)));
	end
	anchor_responses = anchor_responses(2:2:end);
	num_anchor_responses = length(anchor_responses);
	if num_anchor_responses >= 1
		rti = repmat(anchor_responses(1).',[1,30]) + 32 + (0:29) * 8;
		anchor_rx_timestamps = spi_miso(rti) + spi_miso(rti+1) * 256 + spi_miso(rti+2) * 256 * 256 + spi_miso(rti+3) * 256 * 256 * 256 + spi_miso(rti+4) * 256 * 256 * 256 * 256;
		tti = anchor_responses - 5;
		tag_rx_timestamps = spi_miso(tti) + spi_miso(tti+1) * 256 + spi_miso(tti+2) * 256 * 256 + spi_miso(tti+3) * 256 * 256 * 256 + spi_miso(tti+4) * 256 * 256 * 256 * 256;
		resp_ti = anchor_responses + 24;
		tx_resp_timestamp = spi_miso(resp_ti) + spi_miso(resp_ti+1) * 256 + spi_miso(resp_ti+2) * 256 * 256 + spi_miso(resp_ti+3) * 256 * 256 * 256 + spi_miso(resp_ti+4) * 256 * 256 * 256 * 256;
		tx_antennas(ii) = spi_miso(anchor_responses(1)+24);

		%TODO: Need to correlate receive antenna, transmit antenna, and channel correctly
		anchor_over_tag = (anchor_rx_timestamps(1,28:30)-anchor_rx_timestamps(1,1:3))./(tx_timestamps(ii,28:30)-tx_timestamps(ii,1:3));
		anchor_over_tag = anchor_over_tag((anchor_rx_timestamps(1,28:30) > 0) & (anchor_rx_timestamps(1,1:3) > 0));
		anchor_over_tag = mean(anchor_over_tag);
		twToF = (tag_rx_timestamps(1)-tx_timestamps(ii,1))*anchor_over_tag - (tx_resp_timestamp(1)-anchor_rx_timestamps(1));
		ranges(ii,:) =  (anchor_rx_timestamps-anchor_rx_timestamps(1)) - (tx_timestamps(ii,:)-tx_timestamps(ii,1))*anchor_over_tag + twToF;

		matching_broadcast_send_time = tx_timestamps(ii,1);
		matching_broadcast_recv_time = anchor_rx_timestamps(1);
		response_send_time = tx_resp_timestamp(1);
		response_recv_time = tag_rx_timestamps(1);
		two_way_TOF = ((response_recv_time - matching_broadcast_send_time)*anchor_over_tag) - (response_send_time - matching_broadcast_recv_time);
		one_way_TOF = two_way_TOF/2;

		broadcast_send_time = tx_timestamps;
		broadcast_recv_time = anchor_rx_timestamps;
		broadcast_anchor_offset = broadcast_recv_time - matching_broadcast_recv_time;
		broadcast_tag_offset = broadcast_send_time - matching_broadcast_send_time;
		TOF = broadcast_anchor_offset - (broadcast_tag_offset(ii,:) * anchor_over_tag) + one_way_TOF;
		distance_millimeters = TOF*15.65e-12*3e8*1e3;
		distance_millimeters = distance_millimeters(broadcast_recv_time > 0);
		distances_millimeters(ii) = prctile(distance_millimeters,10);
		

		%Nullify those ranges which use anchor rx timestamps at zero
		ranges(ii,anchor_rx_timestamps == 0) = -10000;
		%if ii==6
			keyboard;
		%end
	end

end
keyboard;

return

chunk_agg_idxs = repmat(chunk_idxs.',[1,512]) + repmat(0:511,[num_chunks,1]);
chunk_data = spi_miso(chunk_agg_idxs);
chunk_data = chunk_data(:,1:2:end) + chunk_data(:,2:2:end)*256;
cir_data = reshape(chunk_data.',[2048,num_cirs]);
cir_data(cir_data >= 2^15) = cir_data(cir_data >= 2^15) - 2^16;
cir_data = cir_data(1:2:end,:) + 1i*cir_data(2:2:end,:);

%CIRs are only 4064 octets long
cir_data = cir_data(1:CIR_LEN,:);

%Figure out what the sequence numbers for each CIR are
seq_num_idxs = transaction_idxs(find(spi_mosi(transaction_idxs) == 17)) + 17;
seq_num_idxs = seq_num_idxs(2:2:end-2); %First read is always just used to determine packet length, also remove the last one as it's likely incomplete
seq_nums = spi_miso(seq_num_idxs) + 256*spi_miso(seq_num_idxs+1) + 256*256*spi_miso(seq_num_idxs+2) + 256*256*256*spi_miso(seq_num_idxs+3);
	
ones_mean = zeros(CIR_LEN*INTERP_MULT,1);
zeros_mean = zeros(CIR_LEN*INTERP_MULT,1);
ones_mean_chunks = zeros(CIR_LEN*INTERP_MULT,ceil(num_cirs/CHUNK_LEN));
zeros_mean_chunks = zeros(CIR_LEN*INTERP_MULT,ceil(num_cirs/CHUNK_LEN));
toas = zeros(num_cirs,1);
loop_num = 1;

num_cirs_ones = 0;
num_cirs_zeros = 0;

for cir_num = 1:CHUNK_LEN:num_cirs

	first_cir = cir_num;
	last_cir = min(cir_num+CHUNK_LEN-1,num_cirs);
	cur_num_cirs = last_cir-first_cir+1;
	cur_seq_nums = seq_nums(first_cir:last_cir);
	
	%Interpolate the CIRs
	cir_data_fft = fft(cir_data(:,first_cir:last_cir),[],1).*repmat(fftshift(hamming(CIR_LEN)),[1,cur_num_cirs]);
	cir_data_interp_fft = [cir_data_fft(1:CIR_LEN/2,:);zeros((INTERP_MULT-1)*CIR_LEN,cur_num_cirs);cir_data_fft(CIR_LEN/2+1:end,:)];
	cir_data_interp = ifft(cir_data_interp_fft,[],1);
	
	%Come up with a rough estimate of ToA from each CIR
	cur_toas = zeros(cur_num_cirs,1);
	for ii=1:cur_num_cirs
		above_thresh = find(abs(cir_data_interp(:,ii)) > max(abs(cir_data_interp(:,ii)))*IMP_THRESH);
		cur_toas(ii) = above_thresh(1);
	end
	toas(first_cir:last_cir) = cur_toas;
	
	%%Rotate CIRs to place ToA at zero
	%for ii=1:cur_num_cirs
	%	cir_data_interp(:,ii) = circshift(cir_data_interp(:,ii),-cur_toas(ii))./sqrt(sum(abs(cir_data_interp(:,ii).^2)));
	%end
	

	%%Separate CIRs into two bins depending on where in the sequence they came from
	%pn_sequence = [ ...
	%        0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, ... 
	%        0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, ... 
	%        0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, ... 
	%        0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, ... 
	%        0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, ... 
	%        1, 1, 1];
	%
	%seq_nums_mod = mod(cur_seq_nums, length(pn_sequence));
	%pn_idxs = seq_nums_mod+1;
	pn_zeros = cir_data_interp(:,find(mod(cur_seq_nums,2) == 0));
	pn_ones = cir_data_interp(:,find(mod(cur_seq_nums,2) == 1));
	
	%%Fine-tune the shift of each CIR to minimize magnitude difference
	%pn_zeros = shift_corr(pn_zeros, INTERP_MULT, 10);
	%pn_zeros = shift_corr(pn_zeros, INTERP_MULT, 0.1);
	%pn_ones = shift_corr(pn_ones, INTERP_MULT, 10);
	%pn_ones = shift_corr(pn_ones, INTERP_MULT, 0.1);
	
	%for ii=1:size(pn_zeros,2)
	%	%[~,max_peak] = max(abs(pn_zeros(:,ii)));
	%	%pn_zeros(:,ii) = circshift(pn_zeros(:,ii),-max_peak);
	%	pn_zeros(:,ii) = pn_zeros(:,ii).*exp(-1i*angle(pn_zeros(1,ii)));
	%end
	%for ii=1:size(pn_ones,2)
	%	%[~,max_peak] = max(abs(pn_ones(:,ii)));
	%	%pn_ones(:,ii) = circshift(pn_ones(:,ii),-max_peak);
	%	pn_ones(:,ii) = pn_ones(:,ii).*exp(-1i*angle(pn_ones(1,ii)));
	%end

	num_cirs_ones = num_cirs_ones + size(pn_ones,2);
	num_cirs_zeros = num_cirs_zeros + size(pn_zeros,2);
	
	%ones_mean = mean(abs(pn_ones),2);
	%zeros_mean = mean(abs(pn_zeros),2);
	ones_mean = ones_mean + sum(pn_ones,2);
	zeros_mean = zeros_mean + sum(pn_zeros,2);
	ones_mean_chunks(:,loop_num) = sum(pn_ones,2)/size(pn_ones,2);
	zeros_mean_chunks(:,loop_num) = sum(pn_zeros,2)/size(pn_zeros,2);

	%%Scale (and shift) each CIR so that the 20% to 80% leading edge matches the first the closest
	%last_int_index = find(ones_mean > max(ones_mean)*IMP_THRESH2);
	%last_int_index = last_int_index(1);
	%best_shift = 0;
	%best_scale = 0;
	%best_fit = Inf;
	%for shift = -INTERP_MULT:INTERP_MULT
	%	shifted_cir = circshift(ones_mean,shift);
	%	shifted_cir = shifted_cir(1:last_int_index);
	%	for scale_idx = 1:last_int_index
	%		scale = shifted_cir(scale_idx)/zeros_mean(scale_idx);
	%		cand_fit = sum((shifted_cir/scale-zeros_mean(1:last_int_index)).^2); %Base fit metric off sum of squares difference
	%		if cand_fit < best_fit
	%			best_fit = cand_fit;
	%			best_shift = shift;
	%			best_scale = scale;
	%		end
	%	end
	%end
	%ones_mean_new = circshift(ones_mean/best_scale,best_shift);
	
	good_ones = max(abs(pn_ones-repmat(ones_mean,[1,size(pn_ones,2)])),[],1) < 7e-3;
	ones_mean_new = sum(pn_ones(:,find(good_ones)),2)/sum(good_ones);
	good_zeros = max(abs(pn_zeros-repmat(zeros_mean,[1,size(pn_zeros,2)])),[],1) < 7e-3;
	zeros_mean_new = sum(pn_zeros(:,find(good_zeros)),2)/sum(good_zeros);

	loop_num = loop_num + 1;
end

ones_mean = ones_mean/num_cirs_ones;
zeros_mean = zeros_mean/num_cirs_zeros;
