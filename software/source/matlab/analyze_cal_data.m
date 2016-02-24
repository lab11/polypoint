function ret = analyze_cal_data(filename)

fid = fopen(filename,'r');

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
tx_timestamps = spi_mosi(tx_chunks-4) * 256 + spi_mosi(tx_chunks-3) * 256 * 256 + spi_mosi(tx_chunks-2) * 256 * 256 * 256 + spi_mosi(tx_chunks-1) * 256 * 256 * 256 * 256;
num_ranges = size(tx_timestamps,1);
tx_antennas = zeros(num_ranges-1,1);
ranges = zeros(num_ranges,30);
distances_millimeters = zeros(num_ranges,1);
two_way_TOFs = zeros(num_ranges,9);
one_way_TOFs = zeros(num_ranges,9);

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

		try
		matching_broadcast_send_time = tx_timestamps(ii,[1,2,3,1,2,3,1,2,3]);
		matching_broadcast_recv_time = anchor_rx_timestamps([1,2,3,1,2,3,1,2,3]);
		response_send_time = tx_resp_timestamp([1,1,1,2,2,2,3,3,3]);
		response_recv_time = tag_rx_timestamps([1,1,1,2,2,2,3,3,3]);
		two_way_TOF = ((response_recv_time - matching_broadcast_send_time)*anchor_over_tag) - (response_send_time - matching_broadcast_recv_time);
		one_way_TOF = two_way_TOF/2;
		two_way_TOFs(ii,:) = two_way_TOF;
		one_way_TOFs(ii,:) = one_way_TOF;
		catch
		end
	end

end

ret = two_way_TOFs(find(sum(two_way_TOFs == 0,2) == 0),:);
return
