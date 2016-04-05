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
parser.add_argument('-t', '--textfiles',action='store_true',
		help="Generate ASCII text files with the data")
parser.add_argument('-m', '--matfile',  action='store_true',
		help="Generate Matlab-compatible .mat file of the data")
parser.add_argument('-n', '--binfile',  action='store_true',
		help="Generate binary file of the data")

args = parser.parse_args()

if not (args.textfiles or args.matfile or args.binfile):
	print("Error: Must specify at least one of -t, -m, or -n")
	print("")
	parser.print_help()
	sys.exit(1)

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


if args.textfiles:
	tsfile  = open(args.outfile + '.timestamps', 'w')
	datfile = open(args.outfile + '.data', 'w')
if args.matfile:
	allts   = []
	alldata = []
if args.binfile:
	binfile = open(args.outfile + '.bin', 'wb')

good = 0
bad = 0
NUM_RANGING_CHANNELS = 3
NUM_RANGING_BROADCASTS = 30
EUI_LEN = 8
data_section_length = 8*NUM_RANGING_CHANNELS + 8+1+1+8+8+30*8
try:
	while True:
		sys.stdout.write("\rGood {}    Bad {}\t\t".format(good, bad))

		try:
			find_header()

			num_anchors, = struct.unpack("<B", useful_read(1))

			ranging_broadcast_ss_send_times = struct.unpack("30d", useful_read(8*NUM_RANGING_BROADCASTS))
			
			for x in range(num_anchors):
				b = useful_read(len(DATA_HEADER))
				if b != DATA_HEADER:
					raise AssertionError
				anchor_eui = useful_read(EUI_LEN)
				anchor_final_antenna_index, = struct.unpack("<B", useful_read(1))
				window_packet_recv, = struct.unpack("<B", useful_read(1))
				anc_final_tx_timestamp, = struct.unpack("<Q", useful_read(8))
				anc_final_rx_timestamp, = struct.unpack("<Q", useful_read(8))
				tag_poll_first_idx, = struct.unpack("<B", useful_read(1))
				tag_poll_first_TOA, = struct.unpack("<d", useful_read(8))
				tag_poll_last_idx, = struct.unpack("<B", useful_read(1))
				tag_poll_last_TOA, = struct.unpack("<d", useful_read(8))
				tag_poll_TOAs = struct.unpack("<"+str(NUM_RANGING_BROADCASTS)+"H", useful_read(2*NUM_RANGING_BROADCASTS))
				
			footer = useful_read(len(FOOTER))
			if footer != FOOTER:
				raise AssertionError

			good += 1

			#if args.binfile:
			#	binfile.write(bline)

		except AssertionError:
			bad += 1

except KeyboardInterrupt:
	pass

print("\nGood {}\nBad  {}".format(good, bad))
if args.textfiles:
	print("Wrote ASCII outputs to " + args.outfile + ".{timestamps,data}")
if args.matfile:
	sio.savemat(args.outfile+'.mat', {
		'timestamps': allts,
		'data': alldata,
		})
	print('Wrote Matlab-friendly file to ' + args.outfile + '.mat')
if args.binfile:
	print('Wrote binary output to ' + args.outfile + '.bin')
	print('\tBinary data is formatted as:')
	print('\t<uint64_t><int16_t><int16_t><int16_t><int16_t>... all little endian')
	print('\ttimestmap real0    imag0    real1    imag1    ...')
	print('\tFor 1024 total complex numbers')

