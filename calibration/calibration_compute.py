#!/usr/bin/env python3

import glob
import json
import numpy as np
import os
import sys

import dataprint

if len(sys.argv) != 2:
	print('Need to pass condensed calibration file as first argument.')
	sys.exit(1)

condensed = sys.argv[1]

meta = os.path.splitext(condensed)[0] + '.meta'

SPEED_OF_LIGHT = 299792458
DWT_TIME_UNITS = (1.0/499.2e6/128.0) #!< = 15.65e-12 s

def dwtime_to_dist(dwtime):
	dist = dwtime * DWT_TIME_UNITS * SPEED_OF_LIGHT;
	#dist += ANCHOR_CAL_LEN;
	#dist -= txDelayCal[anchor_id*NUM_CHANNELS + subseq_num_to_chan(subseq, true)];
	return dist;

def dist_to_dwtime(dist):
	dwtime = dist / (DWT_TIME_UNITS * SPEED_OF_LIGHT)
	return dwtime

# Distance in dwtime between tags
l = dist_to_dwtime(0.15)

def sub_dw_ts(a,b):
	if b > a:
		print(b)
		print(a)
		raise
	return a-b

out = [
		('round', 'calibration', 'deltaB', 'epsilonC', 'converted'),
		]

for line in open(condensed):
	try:
		round_num, node, letters = line.split(maxsplit=2)
		L, M, N, O, P, Q = map(int, letters.split())
	except ValueError:
		continue

	k = sub_dw_ts(Q,L) / sub_dw_ts(P,M)
	deltaB = sub_dw_ts(O,L)
	epsilonC = sub_dw_ts(N,M)

	cal = deltaB - epsilonC * k - l

	out.append((round_num, cal, deltaB, epsilonC, epsilonC*k))

	if cal < 0:
		print(dataprint.to_string(out))
		raise

print(dataprint.to_string(out))

#
#outdata = []
#
#outdata.append(['RoundNum',
#                'ATX0', 'ARX1', 'ARX2',
#                'BRX0', 'BTX1', 'BRX2',
#                'CRX0', 'CTX1', 'CRX2',
#                'DRX0', 'DRX1', 'DTX2'])
#
#for key in sorted(data):
#	if -1 in data[key]:
#		continue
#	outdata.append([key] + data[key])
#
#
#outfilename_base = 'tripoint_calibration_' + timestamp
#
#
#with open(outfilename_base + '.meta', 'w') as f:
#	f.write(json.dumps(meta))
#
#
#dataprint.to_newfile(outfilename_base+'.condensed', outdata, overwrite=True)
