clear all

fid = fopen('~/temp/polypoint/bandstitching4/overair_walking_bigbox.csv','r');

%First line is useless.  Discard.
fgetl(fid);

%Parse the Saleae csv dump
spi_data = fscanf(fid,'%f,%d,0x%02X,0x%02X');
num_fields = 4;
num_rows = floor(length(spi_data)/num_fields);
spi_data = reshape(spi_data(1:num_rows*num_fields),[num_fields,num_rows]);

fclose(fid);

%Window over which to analyze data
%%backscatter_trace.csv
%start_time = 4.96607581;
%end_time = 5.28113439;
%%backscatter_trace_with_tag.csv
%start_time = 1.7165836; %first time
%end_time = 2.03164221;
start_time = 2.03164221; %second time
end_time = 2.34670082;
%%backscatter_trace_with_tag_bigger_separation.csv
%start_time = 5.11321996;
%end_time = 5.42827871;
start_time = 0;
end_time = 20;

IMP_THRESH = 0.2;
CHUNK_LEN = 100;
CIR_LEN = 1016;
INTERP_MULT = 100;
NUM_BS_STEPS = 3;

%bandstitching_indices = [
%	830, 1016, 1; ...
%	1, 254, 1;    ...
%	763, 850, 2;  ...
%	851, 1016, 3; ...
%	1, 254, 3;    ...
%	763, 1016, 5; ...
%	1, 200, 5];
	

%Get rid of all data that doesn't fit inside the window
spi_data = spi_data(:,((spi_data(1,:) > start_time) & (spi_data(1,:) < end_time)));

%Indices of all new SPI transactions based on ID
spi_time = spi_data(1,:);
spi_id   = spi_data(2,:);
spi_mosi = spi_data(3,:);
spi_miso = spi_data(4,:);
transaction_idxs = [1,find(diff(spi_id) > 0)+1];

%Stitch together all of the CIR data
start_cir_chunks = transaction_idxs(find(spi_mosi(transaction_idxs) == 37)) + 2;
mid_cir_chunks = transaction_idxs(find(spi_mosi(transaction_idxs) == 101)) + 4;
last_cir_chunk = max(start_cir_chunks);
start_cir_chunks = start_cir_chunks(start_cir_chunks < last_cir_chunk);
mid_cir_chunks = mid_cir_chunks(mid_cir_chunks > min(start_cir_chunks));
mid_cir_chunks = mid_cir_chunks(mid_cir_chunks < last_cir_chunk);
chunk_idxs = sort([start_cir_chunks, mid_cir_chunks]);
num_chunks = length(chunk_idxs);
num_cirs = length(start_cir_chunks);

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

%Read the accumulation time (number of symbols)
rxpacc_idxs = transaction_idxs(find(spi_mosi(transaction_idxs) == 16)) + 3;
rxpacc_idxs = rxpacc_idxs(2:2:end-2);
rxpaccs = bitand(spi_miso(rxpacc_idxs),240)/16 + spi_miso(rxpacc_idxs+1) * 16; 

%Read the timestamps for each CIR
rx_stamp_idxs = transaction_idxs(find(spi_mosi(transaction_idxs) == 21)) + 1;
rx_stamp_idxs = rx_stamp_idxs(1:end-1);
rx_stamps = spi_miso(rx_stamp_idxs) + 256*spi_miso(rx_stamp_idxs+1) + 256*256*spi_miso(rx_stamp_idxs+2) + 256*256*256*spi_miso(rx_stamp_idxs+3) + 256*256*256*256*spi_miso(rx_stamp_idxs+4);

%Figure out what index the timestamp was calculated for
fp_index_idxs = transaction_idxs(find(spi_mosi(transaction_idxs) == 85)) + 2;
fp_index_idxs = fp_index_idxs(1:end-1);
fp_indexs = spi_miso(fp_index_idxs) + 256*spi_miso(fp_index_idxs+1);
fp_indexs = fp_indexs/64; %index is fractional

%Extract TX time from packet data
tx_time_idxs = transaction_idxs(find(spi_mosi(transaction_idxs) == 17)) + 22;
tx_time_idxs = tx_time_idxs(2:2:end-2);
tx_times = spi_miso(tx_time_idxs) + 256*spi_miso(tx_time_idxs+1) + 256*256*spi_miso(tx_time_idxs+2) + 256*256*256*spi_miso(tx_time_idxs+3);
tx_times = tx_times*256; %TX times are always /256 since we can only align to 512 boundaries

%Find complete 1-3 sequence pairs
start_sequences = find(mod(seq_nums,NUM_BS_STEPS) == 0);
start_sequences = start_sequences(1:end-1);
which_complete = start_sequences((mod(seq_nums(start_sequences+1),3) == 1) & (mod(seq_nums(start_sequences+2),3) == 2));% & (mod(seq_nums(start_sequences+3),5) == 3) & (mod(seq_nums(start_sequences+4),5) == 4));

%Pare down the data to only include valid 3-channel sequences
agg_complete = sort([which_complete,which_complete+1,which_complete+2]);%,which_complete+3,which_complete+4]);
cir_data = cir_data(:,agg_complete);
seq_nums = seq_nums(agg_complete);
rx_stamps = rx_stamps(agg_complete);
fp_indexs = fp_indexs(agg_complete);
tx_times = tx_times(agg_complete);
rxpaccs = rxpaccs(agg_complete);

%Divide CIRs by accumulation time to get real power
cir_data = cir_data./repmat(rxpaccs,[size(cir_data,1),1]);

%Calculate clock offset for each timepoint by comparing successive ToAs on the same channel
%NOTE: THIS ASSUMES THE CLOCK IS STABLE ACROSS ALL TIMEPOINTS!!! MAKE SURE BOTH NODES HAVE BEEN ON FOR A WHILE BEFORE TAKING DATA!!!
clock_offset = median((rx_stamps(NUM_BS_STEPS+1:NUM_BS_STEPS:end)-rx_stamps(1:NUM_BS_STEPS:end-NUM_BS_STEPS))./(tx_times(NUM_BS_STEPS+1:NUM_BS_STEPS:end)-tx_times(1:NUM_BS_STEPS:end-NUM_BS_STEPS)));

%For each sequence, re-zero the ToAs and rotate the second and third based on the expected clock offset between sequences
first_toas = repmat(rx_stamps(1:NUM_BS_STEPS:end),[NUM_BS_STEPS,1]);
first_toas = first_toas(:);
first_txs = repmat(tx_times(1:NUM_BS_STEPS:end),[NUM_BS_STEPS,1]);
first_txs = first_txs(:);
offset_toas = (rx_stamps-first_toas.')-(tx_times-first_txs.').*clock_offset;

%Rotate CIRs to put ToA at zero
cir_fft = fft(cir_data);
freq_mult = fftshift(-CIR_LEN/2:CIR_LEN/2-1).';
cir_fft = cir_fft.*exp(1i*2*pi*repmat(freq_mult,[1,size(cir_fft,2)]).*repmat(fp_indexs,[size(cir_fft,1),1])./CIR_LEN);
cir_fft = cir_fft.*exp(-1i*2*pi*repmat(freq_mult,[1,size(cir_fft,2)]).*repmat(offset_toas,[size(cir_fft,1),1])./CIR_LEN./64);
cir_data = ifft(cir_fft);
keyboard;

%%Remove residual phase offset attributed to sampling across time
%freq_mult = fftshift(repmat([0:NUM_BS_STEPS-1]*CIR_LEN/2,[CIR_LEN,1]),1);
%phase_offsets = (tx_times-first_txs.').*(clock_offset-1);
%cir_fft = cir_fft.*exp(-1i*2*pi*repmat(freq_mult,[1,size(cir_fft,2)/NUM_BS_STEPS]).*repmat(phase_offsets,[size(cir_fft,1),1])./CIR_LEN./64);
%cir_data = ifft(cir_fft);

%Do deconvolution with calibration data
load cir_data_cal
cir_fft = cir_fft./repmat(fft(cir_data_cal),[1,size(cir_fft,2)/size(cir_data_cal,2)]);
cir_fft(CIR_LEN/4+1:3*CIR_LEN/4,:) = 0;
cir_fft = [cir_fft(1:CIR_LEN/4,:);cir_fft(3*CIR_LEN/4+1:end,:)];
%cir_fft = cir_fft.*repmat(fftshift(hamming(508)),[1,size(cir_fft,2)]);
cir_data = ifft(cir_fft);

%The most reliable way of determining the phase offsets between CIRs is to find the minimum energy before the leading edge
% over all possible phase offsets
%TODO: Potentially move everything up in time if one of the CIRs yields a negative ToA
full_bw_cirs = zeros(CIR_LEN/2*NUM_BS_STEPS,size(cir_fft,2)/NUM_BS_STEPS);
full_bw_cirs_idx = 1;
for sequence_idx = 1:3:size(cir_fft,2)
	FIRST_INT_IDX = 20;
	LAST_INT_IDX = 10;
	phase_offsets = 0:0.1:2*pi;
	full_bw_fft = fftshift(cir_fft(:,sequence_idx));
	for cur_bs_idx = 2:3
		cand_cirs = zeros(length(full_bw_fft)+size(cir_fft,1),length(phase_offsets));
		for ii=1:length(phase_offsets)
			cand_fft = [full_bw_fft;fftshift(cir_fft(:,sequence_idx+cur_bs_idx-1)).*exp(1i*phase_offsets(ii))];
			cand_cirs(:,ii) = ifft(ifftshift(cand_fft.*hamming(length(cand_fft))));
		end
		integrated_power = sum(abs(cand_cirs(end-FIRST_INT_IDX+1:end-LAST_INT_IDX+1,:)),1);
		[~,best_power_idx] = min(integrated_power);
		best_phase_offset = phase_offsets(best_power_idx);
		full_bw_fft = [full_bw_fft;fftshift(cir_fft(:,sequence_idx+cur_bs_idx-1)).*exp(1i*best_phase_offset)];
	end
	full_bw_cirs(:,full_bw_cirs_idx) = abs(ifft(ifftshift(full_bw_fft.*hamming(length(full_bw_fft)))));
	full_bw_cirs_idx = full_bw_cirs_idx + 1
end
keyboard;

%Last step is to determine the phase offset between successive bands
%TODO: Figure out how this is going to work...

%Perform a search of differing amplitude ratios and phase differences to find the likely correlation between bandstitching pairs
amplitude_ratios = 1;%0.5:0.01:2.0;
phase_offsets = 0:0.1:2*pi;
first_cir = cir_data(:,1);
first_cir_fft = fft(first_cir);
first_cir_fft = [first_cir_fft(1:CIR_LEN/4);first_cir_fft(3*CIR_LEN/4+1:end)];
for second_cir_idx = 2:3
	first_len = length(first_cir_fft);
	win = hamming(first_len);
	second_cir = cir_data(:,second_cir_idx);
	second_cir_fft = fft(second_cir);
	second_cir_fft = [second_cir_fft(1:CIR_LEN/4);second_cir_fft(3*CIR_LEN/4+1:end)];
	second_len = length(second_cir_fft);
	corr_differences = zeros(length(amplitude_ratios),length(phase_offsets));
	orig_cir = ifft([first_cir_fft(1:first_len/2).*win(first_len/2+1:end);zeros(second_len,1);first_cir_fft(first_len/2+1:end).*win(1:first_len/2)]);
	for ii=1:length(amplitude_ratios)
		for jj=1:length(phase_offsets)
			first_cand_cir_fft = fftshift(first_cir_fft).*amplitude_ratios(ii);
			second_cand_cir_fft = fftshift(second_cir_fft)./amplitude_ratios(ii).*exp(1i*phase_offsets(jj));
			cand_cir_fft = [second_cand_cir_fft;first_cand_cir_fft];
			cand_cir_fft = cand_cir_fft.*fftshift(hamming(length(cand_cir_fft)));
			cand_cir = ifft(cand_cir_fft);
			corr_differences(ii,jj) = sum((abs(cand_cir)-abs(orig_cir)).^2);
		end
	end
	[min_val,idx] = min(corr_differences(:));
	[amp_idx,phase_idx] = ind2sub(size(corr_differences),idx);
	amp_ratio = amplitude_ratios(amp_idx);
	phase_offset = phase_offsets(phase_idx);
	keyboard;
	
	first_cir_fft = [first_cir_fft.*amp_ratio; fftshift(second_cir_fft)./amp_ratio.*exp(1i*phase_offset)];
	first_cir = ifft(first_cir_fft.*fftshift(hamming(length(first_cir_fft))));
end
keyboard;
	
%Window each CIR FFT to include only useful frequency content
windowed_ffts = fft(cir_data);
windowed_ffts = windowed_ffts([1:254,1016-254:1016],:);
windowed_ffts = windowed_ffts.*repmat(fftshift(hamming(509)),[1,size(windowed_ffts,2)]);

%Assemble large FFT based on composition of smaller FFTs
large_fft = [fftshift(windowed_ffts(:,1:3:end),1);fftshift(windowed_ffts(:,2:3:end),1);fftshift(windowed_ffts(:,3:3:end),1)];
highres_cir = ifft(large_fft,[],1);
