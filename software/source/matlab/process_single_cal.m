function ret = process_single_cal(filename)

DWT_TIME_UNITS = 1.0/499.2e6/128;

cable_length_in = 17*1.5; %17" * 3/2 (speed of light through cable)
cable_length_m = cable_length_in * 0.0254;
cable_length_dw_time_units = cable_length_m/3e8/DWT_TIME_UNITS;

%Process two-way ToFs for the calibrating tag
tw_ToFs = analyze_cal_data(filename);

%We're interested in the median of the data
tw_ToFs = median(tw_ToFs);

%Remove twice the cable length due to two-way ranging calculation
A_cal = tw_ToFs - cable_length_dw_time_units*2;

A_rx_cal = round(mean(reshape(A_cal,[3,3]),1));
A_tx_cal = round(mean(reshape(A_cal,[3,3])-repmat(A_rx_cal,[3,1]),2)).';

% Make sure all the numbers are positive
A_rx_cal = A_rx_cal - min(A_tx_cal);
A_tx_cal = A_tx_cal - min(A_tx_cal);

ret = [A_rx_cal(1), A_tx_cal(1), A_rx_cal(2), A_tx_cal(2), A_rx_cal(3), A_tx_cal(3)];
disp(sprintf('  %5d    %5d      %5d    %5d      %5d    %5d',ret))
