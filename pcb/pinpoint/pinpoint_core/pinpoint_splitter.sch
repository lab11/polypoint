EESchema Schematic File Version 2
LIBS:power
LIBS:device
LIBS:transistors
LIBS:conn
LIBS:linear
LIBS:regul
LIBS:74xx
LIBS:cmos4000
LIBS:adc-dac
LIBS:memory
LIBS:xilinx
LIBS:microcontrollers
LIBS:dsp
LIBS:microchip
LIBS:analog_switches
LIBS:motorola
LIBS:texas
LIBS:intel
LIBS:audio
LIBS:interface
LIBS:digital-audio
LIBS:philips
LIBS:display
LIBS:cypress
LIBS:siliconi
LIBS:opto
LIBS:atmel
LIBS:contrib
LIBS:valves
LIBS:polypoint
EELAYER 25 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 5 7
Title ""
Date ""
Rev ""
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
Text HLabel 4850 3950 0    60   Input ~ 0
REF_IN
Text HLabel 4850 4700 0    60   Input ~ 0
SYNC_IN
Text HLabel 6450 3750 2    60   Output ~ 0
REF_OUT0
Text HLabel 6450 3850 2    60   Output ~ 0
REF_OUT1
Text HLabel 6450 3950 2    60   Output ~ 0
REF_OUT2
Text HLabel 6450 4700 2    60   Output ~ 0
SYNC_OUT0
Text HLabel 6450 4800 2    60   Output ~ 0
SYNC_OUT1
Text HLabel 6450 4900 2    60   Output ~ 0
SYNC_OUT2
$Comp
L +3.3V #PWR?
U 1 1 55D9F66A
P 5100 3350
F 0 "#PWR?" H 5100 3200 50  0001 C CNN
F 1 "+3.3V" H 5100 3490 50  0000 C CNN
F 2 "" H 5100 3350 60  0000 C CNN
F 3 "" H 5100 3350 60  0000 C CNN
	1    5100 3350
	1    0    0    -1  
$EndComp
$Comp
L GND #PWR?
U 1 1 55D9F67E
P 5100 4250
F 0 "#PWR?" H 5100 4000 50  0001 C CNN
F 1 "GND" H 5100 4100 50  0000 C CNN
F 2 "" H 5100 4250 60  0000 C CNN
F 3 "" H 5100 4250 60  0000 C CNN
	1    5100 4250
	1    0    0    -1  
$EndComp
$Comp
L ICS574 U?
U 1 1 55D9F692
P 5750 3900
F 0 "U?" H 5450 4250 60  0000 C CNN
F 1 "ICS574" H 5750 3550 60  0000 C CNN
F 2 "" H 5800 3900 60  0000 C CNN
F 3 "" H 5800 3900 60  0000 C CNN
	1    5750 3900
	1    0    0    -1  
$EndComp
Wire Wire Line
	6250 3950 6450 3950
Wire Wire Line
	6450 3850 6250 3850
Wire Wire Line
	6250 3750 6450 3750
Wire Wire Line
	5100 3650 5200 3650
Wire Wire Line
	5100 3350 5100 3650
Wire Wire Line
	5200 4150 5100 4150
Wire Wire Line
	5100 4150 5100 4250
Wire Wire Line
	6300 3100 6300 3750
Wire Wire Line
	4950 3100 4950 3850
Wire Wire Line
	4950 3850 5200 3850
Connection ~ 6300 3750
Wire Wire Line
	5200 3950 4850 3950
$Comp
L R_Small R?
U 1 1 55DA00E3
P 6350 4250
F 0 "R?" H 6380 4270 50  0000 L CNN
F 1 "R_Small" H 6380 4210 50  0000 L CNN
F 2 "" H 6350 4250 60  0000 C CNN
F 3 "" H 6350 4250 60  0000 C CNN
	1    6350 4250
	1    0    0    -1  
$EndComp
$Comp
L GND #PWR?
U 1 1 55DA0108
P 6350 4350
F 0 "#PWR?" H 6350 4100 50  0001 C CNN
F 1 "GND" H 6350 4200 50  0000 C CNN
F 2 "" H 6350 4350 60  0000 C CNN
F 3 "" H 6350 4350 60  0000 C CNN
	1    6350 4350
	1    0    0    -1  
$EndComp
Wire Wire Line
	6350 4150 6350 4050
Wire Wire Line
	6350 4050 6250 4050
Wire Wire Line
	4850 4700 6450 4700
Wire Wire Line
	6450 4900 6200 4900
Wire Wire Line
	6200 4900 6200 4700
Connection ~ 6200 4700
Wire Wire Line
	6450 4800 6200 4800
Connection ~ 6200 4800
$Comp
L C_Small C?
U 1 1 55DA316C
P 5300 3400
F 0 "C?" H 5310 3470 50  0000 L CNN
F 1 "0.1uF" H 5310 3320 50  0000 L CNN
F 2 "" H 5300 3400 60  0000 C CNN
F 3 "" H 5300 3400 60  0000 C CNN
	1    5300 3400
	0    1    1    0   
$EndComp
Wire Wire Line
	5200 3400 5100 3400
Connection ~ 5100 3400
$Comp
L GND #PWR?
U 1 1 55DA31E0
P 5400 3400
F 0 "#PWR?" H 5400 3150 50  0001 C CNN
F 1 "GND" H 5400 3250 50  0000 C CNN
F 2 "" H 5400 3400 60  0000 C CNN
F 3 "" H 5400 3400 60  0000 C CNN
	1    5400 3400
	0    -1   -1   0   
$EndComp
Wire Wire Line
	4950 3100 6300 3100
$EndSCHEMATC
