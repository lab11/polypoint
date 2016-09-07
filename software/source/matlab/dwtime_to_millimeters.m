function ret = dwtime_to_millimeters(dwtime)

DWT_TIME_UNITS = 1/499.2e6/128;
SPEED_OF_LIGHT = 2.99792458e8;
AIR_N = 1.0003;

ret = dwtime*DWT_TIME_UNITS*SPEED_OF_LIGHT/AIR_N;

ret = ret * 1000;
