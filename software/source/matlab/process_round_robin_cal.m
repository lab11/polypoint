A_to_B_fname = '~/temp/polypoint/cal_test/anchor_20_tag_1e.csv';
A_to_C_fname = '~/temp/polypoint/cal_test/anchor_20_tag_21.csv';
B_to_C_fname = '~/temp/polypoint/cal_test/anchor_1e_tag_21.csv';

DWT_TIME_UNITS = 1.0/499.2e6/128;

cable_length_in = 17*1.5; %17" * 3/2 (speed of light through cable)
cable_length_m = cable_length_in * 0.0254;
cable_length_dw_time_units = cable_length_m/3e8/DWT_TIME_UNITS;

%Process two-way ToFs for the three uncalibrated tags
A_to_B_ToFs = analyze_cal_data(A_to_B_fname);
A_to_C_ToFs = analyze_cal_data(A_to_C_fname);
B_to_C_ToFs = analyze_cal_data(B_to_C_fname);

%We're interested in the median of the data
A_to_B_ToFs = median(A_to_B_ToFs);
A_to_C_ToFs = median(A_to_C_ToFs);
B_to_C_ToFs = median(B_to_C_ToFs);

%Remove twice the cable length due to two-way ranging calculation
A_to_B_ToFs = A_to_B_ToFs - cable_length_dw_time_units*2;
A_to_C_ToFs = A_to_C_ToFs - cable_length_dw_time_units*2;
B_to_C_ToFs = B_to_C_ToFs - cable_length_dw_time_units*2;

M1 = A_to_C_ToFs - B_to_C_ToFs;
M2 = A_to_B_ToFs + M1;

A_cal = M2/2;

A_rx_cal = round(mean(reshape(A_cal,[3,3]),1));
A_tx_cal = round(mean(reshape(A_cal,[3,3])-repmat(A_rx_cal,[3,1]),2)).';

% Make sure all the numbers are positive
A_rx_cal = A_rx_cal - min(A_tx_cal);
A_tx_cal = A_tx_cal - min(A_tx_cal);

disp(sprintf('  %5d    %5d      %5d    %5d      %5d    %5d',[A_rx_cal(1), A_tx_cal(1), A_rx_cal(2), A_tx_cal(2), A_rx_cal(3), A_tx_cal(3)]))
