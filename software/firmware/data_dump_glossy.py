#!/usr/bin/env python3

import argparse
import binascii
import os
import struct
import sys
import code

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
DWT_TIME_UNITS = 1/499.2e6/128;
SPEED_OF_LIGHT = 2.99792458e8;
AIR_N = 1.0003;
good = 0
bad = 0
NUM_RANGING_CHANNELS = 3
NUM_RANGING_BROADCASTS = 30
EUI_LEN = 8
data_section_length = 8*NUM_RANGING_CHANNELS + 8+1+1+8+8+30*8

def antenna_and_channel_to_subsequence_number(tag_antenna_index, anchor_antenna_index, channel_index):
	anc_offset = anchor_antenna_index * NUM_RANGING_CHANNELS
	tag_offset = tag_antenna_index * NUM_RANGING_CHANNELS * NUM_RANGING_CHANNELS
	base_offset = anc_offset + tag_offset + channel_index
	
	ret = base_offset
	return ret

def oneway_get_ss_index_from_settings(anchor_antenna_index, window_num):
	tag_antenna_index = 0
	channel_index = window_num % NUM_RANGING_CHANNELS
	ret = antenna_and_channel_to_subsequence_number(tag_antenna_index, anchor_antenna_index, channel_index)
	return ret

def find_header():
	b = useful_read(len(HEADER))
	while b != HEADER:
		b = b[1:len(HEADER)] + useful_read(1)

def dwtime_to_millimeters(dwtime):
	ret = dwtime*DWT_TIME_UNITS*SPEED_OF_LIGHT/AIR_N
	ret = ret * 1000;
	return ret

if args.textfiles:
	tsfile  = open(args.outfile + '.timestamps', 'w')
	datfile = open(args.outfile + '.data', 'w')
if args.matfile:
	allts   = []
	alldata = []
if args.binfile:
	binfile = open(args.outfile + '.bin', 'wb')

try:
	while True:
		sys.stdout.write("\rGood {}    Bad {}\t\t".format(good, bad))

		try:
			find_header()

			num_anchors, = struct.unpack("<B", useful_read(1))

			ranging_broadcast_ss_send_times = np.array(struct.unpack("<30Q", useful_read(8*NUM_RANGING_BROADCASTS)))
			
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
				tag_poll_first_TOA, = struct.unpack("<Q", useful_read(8))
				tag_poll_last_idx, = struct.unpack("<B", useful_read(1))
				tag_poll_last_TOA, = struct.unpack("<Q", useful_read(8))
				tag_poll_TOAs = np.array(struct.unpack("<"+str(NUM_RANGING_BROADCASTS)+"H", useful_read(2*NUM_RANGING_BROADCASTS)))

				if tag_poll_first_idx >= NUM_RANGING_BROADCASTS or tag_poll_last_idx >= NUM_RANGING_BROADCASTS:
					continue
			
				#print(tag_poll_TOAs[tag_poll_first_idx] & 0xFFFF)
				#print(tag_poll_first_TOA & 0xFFFF)
				#print(tag_poll_TOAs[tag_poll_last_idx] & 0xFFFF)
				#print(tag_poll_last_TOA & 0xFFFF)

				# Perform ranging operations with the received timestamp data
				tag_poll_TOAs[tag_poll_first_idx] = tag_poll_first_TOA
				tag_poll_TOAs[tag_poll_last_idx] = tag_poll_last_TOA

				approx_clock_offset = (tag_poll_last_TOA - tag_poll_first_TOA)/(ranging_broadcast_ss_send_times[tag_poll_last_idx] - ranging_broadcast_ss_send_times[tag_poll_first_idx])

				# Interpolate betseen the first and last to find the high 48 bits which fit best
				for jj in range(tag_poll_first_idx+1,tag_poll_last_idx):
					estimated_toa = tag_poll_first_TOA + (approx_clock_offset*(ranging_broadcast_ss_send_times[jj] - ranging_broadcast_ss_send_times[tag_poll_first_idx]))
					actual_toa = (int(estimated_toa) & 0xFFFFFFFFFFF0000) + tag_poll_TOAs[jj]

					if(actual_toa < estimated_toa - 0x7FFF):
						actual_toa = actual_toa + 0x10000
					elif(actual_toa > estimated_toa + 0x7FFF):
						actual_toa = actual_toa - 0x10000

					tag_poll_TOAs[jj] = actual_toa

				# Get the actual clock offset calculation
				num_valid_offsets = 0
				offset_cumsum = 0
				for jj in range(NUM_RANGING_CHANNELS):
					if(tag_poll_TOAs[jj] & 0xFFFF > 0 and tag_poll_TOAs[27+jj] & 0xFFFF > 0):
						offset_cumsum = offset_cumsum + (tag_poll_TOAs[27+jj] - tag_poll_TOAs[jj])/(ranging_broadcast_ss_send_times[27+jj] - ranging_broadcast_ss_send_times[jj])
						num_valid_offsets = num_valid_offsets + 1

				if num_valid_offsets == 0:
					continue
				offset_anchor_over_tag = offset_cumsum/num_valid_offsets;

				# Figure out what broadcast the received response belongs to
				ss_index_matching = oneway_get_ss_index_from_settings(anchor_final_antenna_index, window_packet_recv)
				if int(tag_poll_TOAs[ss_index_matching]) & 0xFFFF == 0:
					continue
		
				matching_broadcast_send_time = ranging_broadcast_ss_send_times[ss_index_matching]
				matching_broadcast_recv_time = tag_poll_TOAs[ss_index_matching]
				response_send_time = anc_final_tx_timestamp
				response_recv_time = anc_final_rx_timestamp
		
				two_way_TOF = ((response_recv_time - matching_broadcast_send_time)*offset_anchor_over_tag) - (response_send_time - matching_broadcast_recv_time)
				one_way_TOF = two_way_TOF/2
		
				# Declare an array for sorting ranges
				distance_millimeters = []
				for jj in range(NUM_RANGING_BROADCASTS):
					broadcast_send_time = ranging_broadcast_ss_send_times[jj]
					broadcast_recv_time = tag_poll_TOAs[jj]
					if int(broadcast_recv_time) & 0xFFFF == 0:
						continue
		
					broadcast_anchor_offset = broadcast_recv_time - matching_broadcast_recv_time
					broadcast_tag_offset = broadcast_send_time - matching_broadcast_send_time
					TOF = broadcast_anchor_offset - broadcast_tag_offset*offset_anchor_over_tag + one_way_TOF
		
					distance_millimeters.append(TOF)#dwtime_to_millimeters(TOF))
		
				#anchor_eui_txt = dec2hex(anchor_eui)
				range_mm = np.percentile(distance_millimeters,10)
				if(range_mm < 00):
					code.interact(local=dict(globals(), **locals()))
					#print(offset_anchor_over_tag)
					#print(matching_broadcast_recv_time)
					#print(distance_millimeters)
				print(range_mm)
				
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

