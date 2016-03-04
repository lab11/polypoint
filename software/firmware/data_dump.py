#!/usr/bin/env python3

import argparse
import binascii
import os
import struct
import sys

import serial

parser = argparse.ArgumentParser()
parser.add_argument('-s', '--serial',   default='/dev/tty.usbserial-AL00EZAS')
parser.add_argument('-b', '--baudrate', default=3000000, type=int)
parser.add_argument('-o', '--outfile',  default='out.data')

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


ofile = open(args.outfile, 'w')

good = 0
bad = 0
while True:
	sys.stdout.write("\rGood {}    Bad {}\t\t".format(good, bad))

	line = ''

	try:
		find_header()

		timestamp, = struct.unpack("<Q", useful_read(8))
		line += str(timestamp) + ' '

		# for(int ii = 0; ii < 4096; ii += 512)
		# uart_write(512, acc_data+1);
		for x in range(8):
			b = useful_read(len(DATA_HEADER))
			if b != DATA_HEADER:
				raise AssertionError
			data = useful_read(512)
			for i in range(0, 512, 4):
				real,imag = struct.unpack("<hh", data[i:i+4])
				line += str(real) + ' ' + str(imag) + ' '

		good += 1
		ofile.write(line + '\n')

	except AssertionError:
		bad += 1

	except KeyboardInterrupt:
		break

print("\nGood {}\nBad  {}".format(good, bad))

