#!/usr/bin/env python3

import glob
import json
import os
import sys

import dataprint


if len(sys.argv) != 2:
	print('Need to pass the time string of the files to compress as the first command line argument.')
	sys.exit(1)

timestamp = sys.argv[1]
glob_filename = 'tripoint_calibration_' + timestamp + '*.data'

meta = {}
data = {}


for filename in glob.glob(glob_filename):
	fname, fext = os.path.splitext(filename)

	splits = fname.split('_')

	index = int(splits[-1])
	macid = splits[-2]

	meta[index] = macid

	with open(filename) as f:
		for l in f:
			times = l.split()
			try:
				rnd = int(times[0])
				t1 = int(times[1])
				t2 = int(times[2])
				t3 = int(times[3])
				t4 = int(times[4])

				if rnd not in data:
					data[rnd] = [-1]*12

				data[rnd][index+0] = t1
				data[rnd][index+3] = t2
				data[rnd][index+6] = t3
				data[rnd][index+9] = t4

			except:
				pass



outdata = []

outdata.append(['RoundNum',
                'ATX0', 'ARX1', 'ARX2',
                'BRX0', 'BTX1', 'BRX2',
                'CRX0', 'CTX1', 'CRX2',
                'DRX0', 'DRX1', 'DTX2'])

for key in sorted(data):
	if -1 in data[key]:
		continue
	outdata.append([key] + data[key])


outfilename_base = 'tripoint_calibration_' + timestamp


with open(outfilename_base + '.meta', 'w') as f:
	f.write(json.dumps(meta))


dataprint.to_newfile(outfilename_base+'.condensed', outdata, overwrite=True)
