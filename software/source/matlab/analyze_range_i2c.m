clear all

fid = fopen('~/temp/polypoint/cal_test/ranges_2p34m_aftercal.csv','r');

%First line is useless.  Discard.
fgetl(fid);

%Parse the Saleae csv dump
i2c_data = fscanf(fid,'%f,%d,0x%02X,0x%02X,%c%*s\n',Inf);
num_fields = 5;
num_rows = floor(length(i2c_data)/num_fields);
i2c_data = reshape(i2c_data(1:num_rows*num_fields),[num_fields,num_rows]);

%Indices of all new I2C transactions based on ID
i2c_time = i2c_data(1,:);
i2c_id = i2c_data(2,:);
i2c_addr = i2c_data(3,:);
i2c_rw = i2c_data(5,:);
i2c_data = i2c_data(4,:);
transaction_idxs = [1,find(diff(i2c_id) > 0)+1];

fclose(fid);

%Find the indices of interrupt events (read events should follow)
interrupt_idxs = transaction_idxs(find((i2c_addr(transaction_idxs) == 202) & (i2c_data(transaction_idxs) == 3)));
range_idxs = interrupt_idxs + 12;
ranges = i2c_data(range_idxs) + i2c_data(range_idxs+1) * 256 + i2c_data(range_idxs+2) * 256 * 256 + i2c_data(range_idxs+3) * 256 * 256 * 256;

%32-bit 2's complement
ranges = mod(ranges+2^31,2^32)-2^31;

%Get rid of 'bad' ranges (ones which aren't error codes
ranges = ranges(find(abs(ranges) < 1e8));
