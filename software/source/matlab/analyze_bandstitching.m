clear all

fid = fopen('~/temp/polypoint/bandstitching3/direct_newarrangement.csv','r');

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
keyboard;

%Figure out what the sequence numbers for each CIR are
seq_num_idxs = transaction_idxs(find(spi_mosi(transaction_idxs) == 17)) + 17;
seq_num_idxs = seq_num_idxs(2:2:end-2); %First read is always just used to determine packet length, also remove the last one as it's likely incomplete
seq_nums = spi_miso(seq_num_idxs) + 256*spi_miso(seq_num_idxs+1) + 256*256*spi_miso(seq_num_idxs+2) + 256*256*256*spi_miso(seq_num_idxs+3);

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
start_sequences = find(mod(seq_nums,3) == 0);
start_sequences = start_sequences(1:end-1);
which_complete = start_sequences((mod(seq_nums(start_sequences+1),3) == 1) & (mod(seq_nums(start_sequences+2),3) == 2));

%Pare down the data to only include valid 3-channel sequences
agg_complete = sort([which_complete,which_complete+1,which_complete+2]);
cir_data = cir_data(:,agg_complete);
seq_nums = seq_nums(agg_complete);
rx_stamps = rx_stamps(agg_complete);
fp_indexs = fp_indexs(agg_complete);
tx_times = tx_times(agg_complete);

%Calculate clock offset for each timepoint by comparing successive ToAs on the same channel
%NOTE: THIS ASSUMES THE CLOCK IS STABLE ACROSS ALL TIMEPOINTS!!! MAKE SURE BOTH NODES HAVE BEEN ON FOR A WHILE BEFORE TAKING DATA!!!
clock_offset = mean((rx_stamps(4:3:end)-rx_stamps(1:3:end-3))./(tx_times(4:3:end)-tx_times(1:3:end-3)));

%For each sequence, re-zero the ToAs and rotate the second and third based on the expected clock offset between sequences
first_toas = repmat(rx_stamps(1:3:end),[3,1]);
first_toas = first_toas(:);
first_txs = repmat(tx_times(1:3:end),[3,1]);
first_txs = first_txs(:);
offset_toas = (rx_stamps-first_toas.')-(tx_times-first_txs.').*clock_offset;

%Rotate CIRs to put ToA at zero
cir_fft = fft(cir_data);
freq_mult = fftshift(-CIR_LEN/2:CIR_LEN/2-1).';
cir_fft = cir_fft.*exp(1i*2*pi*repmat(freq_mult,[1,size(cir_fft,2)]).*repmat(fp_indexs,[size(cir_fft,1),1])./CIR_LEN);
cir_fft = cir_fft.*exp(-1i*2*pi*repmat(freq_mult,[1,size(cir_fft,2)]).*repmat(offset_toas,[size(cir_fft,1),1])./CIR_LEN./64);
cir_data = ifft(cir_fft);

%Do deconvolution with calibration data
win = hamming(CIR_LEN/2);
load cir_data_cal
cir_fft = cir_fft./repmat(fft(cir_data_cal),[1,size(cir_fft,2)/size(cir_data_cal,2)]);
cir_fft(CIR_LEN/4+1:3*CIR_LEN/4,:) = 0;
cir_data = ifft(cir_fft);

%Perform a search of differing amplitude ratios and phase differences to find the likely correlation between bandstitching pairs
amplitude_ratios = 0.5:0.01:2.0;
phase_offsets = 0:0.1:2*pi;
first_cir = cir_data(:,1);
second_cir = cir_data(:,2);
first_cir_fft = fft(first_cir);
first_cir_fft = [first_cir_fft(1:CIR_LEN/4);first_cir_fft(3*CIR_LEN/4+1:end)];
second_cir_fft = fft(second_cir);
second_cir_fft = [second_cir_fft(1:CIR_LEN/4);second_cir_fft(3*CIR_LEN/4+1:end)];
corr_differences = zeros(length(amplitude_ratios),length(phase_offsets));
orig_cir = ifft([first_cir_fft(1:CIR_LEN/4).*win(CIR_LEN/4+1:end);zeros(CIR_LEN/2,1);first_cir_fft(CIR_LEN/4+1:end).*win(1:CIR_LEN/4)]);
%orig_cir = ifft([second_cir_fft(1:CIR_LEN/4).*win(CIR_LEN/4+1:end);zeros(CIR_LEN/2,1);second_cir_fft(CIR_LEN/4+1:end).*win(1:CIR_LEN/4)]);
for ii=43%1:length(amplitude_ratios)
	for jj=55%1:length(phase_offsets)
		first_cand_cir_fft = fftshift(first_cir_fft).*amplitude_ratios(ii);
		second_cand_cir_fft = fftshift(second_cir_fft)./amplitude_ratios(ii).*exp(1i*phase_offsets(jj));
		cand_cir = ifft(ifftshift([first_cand_cir_fft;second_cand_cir_fft]).*fftshift(hamming(CIR_LEN)));
		corr_differences(ii,jj) = sum((abs(cand_cir)-abs(orig_cir)).^2);
	end
end
	
%Window each CIR FFT to include only useful frequency content
windowed_ffts = fft(cir_data);
windowed_ffts = windowed_ffts([1:254,1016-254:1016],:);
windowed_ffts = windowed_ffts.*repmat(fftshift(hamming(509)),[1,size(windowed_ffts,2)]);

%Assemble large FFT based on composition of smaller FFTs
large_fft = [fftshift(windowed_ffts(:,1:3:end),1);fftshift(windowed_ffts(:,2:3:end),1);fftshift(windowed_ffts(:,3:3:end),1)];
highres_cir = ifft(large_fft,[],1);
