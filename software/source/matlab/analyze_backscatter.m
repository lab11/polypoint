fid = fopen('~/temp/polypoint/backscatter/backscatter_trace.csv','r');

%First line is useless.  Discard.
fgetl(fid);

%Parse the Saleae csv dump
spi_data = fscanf(fid,'%f,%d,0x%02X,0x%02X');
num_fields = 4;
num_rows = floor(length(spi_data)/num_fields);
spi_data = reshape(spi_data(1:num_rows*num_fields),[num_fields,num_rows]);

fclose(fid);

%Window over which to analyze data
start_time = 4.96607581;
end_time = 5.28113439;

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

%CIRs are only 4064 octects long
cir_data = cir_data(1:1016,:);

