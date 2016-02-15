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
tw_ToFs = tw_ToFs - cable_length_dw_time_units*2;

ret = round(tw_ToFs);

disp(sprintf('%5d    %5d   %5d      %5d   %5d      %5d     %5d   %5d   %5d',[ret(1),ret(1),ret(1),ret(2),ret(2),ret(2),ret(3),ret(3),ret(3)]))

