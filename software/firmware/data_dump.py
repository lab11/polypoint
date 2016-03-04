#!/usr/bin/env python3

import argparse
import binascii
import os
import struct
import sys

import serial

import numpy as np
import scipy.io as sio

parser = argparse.ArgumentParser()
parser.add_argument('-s', '--serial',   default='/dev/tty.usbserial-AL00EZAS')
parser.add_argument('-b', '--baudrate', default=3000000, type=int)
parser.add_argument('-o', '--outfile',  default='out')

args = parser.parse_args()

dev = serial.Serial(args.serial, args.baudrate)
if dev.isOpen():
	print("Connected to device at " + dev.portstr)
else:
	raise NotImplementedError("Failed to connect to serial device " + args.serial)


def useful_read(length):
	b = dev.read(length)
	while len(b) < length:
		b += dev.read(length - len(b))
	assert len(b) == length
	return b


HEADER      = (0x80018001).to_bytes(4, 'big')
DATA_HEADER = (0x8080).to_bytes(2, 'big')
FOOTER      = (0x80FE).to_bytes(2, 'big')


def find_header():
	b = useful_read(len(HEADER))
	while b != HEADER:
		b = b[1:len(HEADER)] + useful_read(1)


tsfile  = open(args.outfile + '.timestamps', 'w')
datfile = open(args.outfile + '.data', 'w')

allts   = []
alldata = []

good = 0
bad = 0
while True:
	sys.stdout.write("\rGood {}    Bad {}\t\t".format(good, bad))

	line = ''

	try:
		find_header()

		timestamp, = struct.unpack("<Q", useful_read(8))

		line += '['

		inner = []

		# for(int ii = 0; ii < 4096; ii += 512)
		# uart_write(512, acc_data+1);
		for x in range(8):
			b = useful_read(len(DATA_HEADER))
			if b != DATA_HEADER:
				raise AssertionError
			data = useful_read(512)
			for i in range(0, 512, 4):
				real,imag = struct.unpack("<hh", data[i:i+4])
				line += '{}, {}; '.format(real, imag)
				inner.append(np.complex(real, imag))

		line += ']'

		good += 1

		tsfile.write(str(timestamp) + '\n')
		datfile.write(line + '\n')

		allts.append(timestamp)
		alldata.append(inner)

	except AssertionError:
		bad += 1

	except KeyboardInterrupt:
		break

print("\nGood {}\nBad  {}".format(good, bad))
sio.savemat(args.outfile+'.mat', {
	'timestamps': allts,
	'data': alldata,
	})
print('Wrote Matlab-friendly file to ' + args.outfile + '.mat')

