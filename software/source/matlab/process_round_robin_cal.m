A_to_B_fname = '~/temp/polypoint/cal_test/1e_to_3c.csv';
A_to_C_fname = '~/temp/polypoint/cal_test/1e_to_3a.csv';
B_to_C_fname = '~/temp/polypoint/cal_test/3c_to_3a.csv';

DWT_TIME_UNITS = 1.0/499.2e6/128;

cable_length_in = (17+12.5)*1.5; %17" * 3/2 (speed of light through cable)
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

A_cal = round(M2/2);

disp(sprintf('  %5d    %5d      %5d',[A_cal(1), A_cal(5), A_cal(9)]))
