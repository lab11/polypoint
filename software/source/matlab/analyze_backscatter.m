fid = fopen('~/temp/polypoint/backscatter/backscatter_trace_with_tag.csv','r');

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

IMP_THRESH = 0.2;
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

%Interpolate the CIRs
cir_data_fft = fft(cir_data,[],1);
cir_data_interp_fft = [cir_data_fft(1:CIR_LEN/2,:);zeros((INTERP_MULT-1)*CIR_LEN,num_cirs);cir_data_fft(CIR_LEN/2+1:end,:)];
cir_data_interp = ifft(cir_data_interp_fft,[],1);

%Find ToAs from each CIR
toas = zeros(num_cirs,1);
for ii=1:num_cirs
	above_thresh = find(abs(cir_data_interp(:,ii)) > max(abs(cir_data_interp(:,ii)))*IMP_THRESH);
	toas(ii) = above_thresh(1);
end

%Rotate CIRs back to place ToA at zero
for ii=1:num_cirs
	cir_data_interp(:,ii) = circshift(cir_data_interp(:,ii),-toas(ii))./sqrt(sum(abs(cir_data_interp(:,ii).^2)));
end

%Figure out what the sequence numbers for each CIR are
seq_num_idxs = transaction_idxs(find(spi_mosi(transaction_idxs) == 17)) + 17;
seq_num_idxs = seq_num_idxs(2:2:end); %First read is always just used to determine packet length
seq_nums = spi_miso(seq_num_idxs) + 256*spi_miso(seq_num_idxs+1) + 256*256*spi_miso(seq_num_idxs+2) + 256*256*256*spi_miso(seq_num_idxs+3);

%Separate CIRs into two bins depending on where in the sequence they came from
pn_sequence = [ ...
        0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, ... 
        0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, ... 
        0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, ... 
        0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, ... 
        0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, ... 
        1, 1, 1];

ones_mean = zeros(length(pn_sequence),size(cir_data_interp,1));
zeros_mean = zeros(length(pn_sequence),size(cir_data_interp,1));
for ii=1:length(pn_sequence)
seq_nums_mod = mod(seq_nums+ii, length(pn_sequence));
pn_idxs = seq_nums_mod+1;
pn_zeros = cir_data_interp(:,find(pn_sequence(pn_idxs) == 0));
pn_ones = cir_data_interp(:,find(pn_sequence(pn_idxs) == 1));
ones_mean(ii,:) = median(abs(pn_ones),2);
zeros_mean(ii,:) = median(abs(pn_zeros),2);%
end
