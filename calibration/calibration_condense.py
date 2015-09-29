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

				if rnd not in data:
					data[rnd] = [-1]*6

				if ((rnd % 3) + 2) % 3 == index:
					data[rnd][1] = t1
					data[rnd][2] = t2
					data[rnd][4] = t3
				else:
					data[rnd][0] = t1
					data[rnd][3] = t2
					data[rnd][5] = t3


			except:
				pass



outdata = []

outdata.append(['RoundNum', 'NodeBeingCalibrated',
                'L', 'M', 'N', 'O', 'P', 'Q'])

for key in sorted(data):
	if -1 in data[key]:
		continue
	node = ['A', 'B', 'C'][(((key % 3) + 2) % 3)]
	outdata.append([key, node] + data[key])


outfilename_base = 'tripoint_calibration_' + timestamp


with open(outfilename_base + '.meta', 'w') as f:
	f.write(json.dumps(meta))


dataprint.to_newfile(outfilename_base+'.condensed', outdata, overwrite=True)
