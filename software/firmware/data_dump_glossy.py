#!/usr/bin/env python3

import logging
log = logging.getLogger(__name__)

import os
if 'DEBUG' in os.environ:
	logging.basicConfig(level=logging.DEBUG)

try:
	import coloredlogs
	#coloredlogs.DEFAULT_LOG_FORMAT = '%(asctime)s %(hostname)s %(name)s[%(process)d] %(levelname)s %(message)s'
	coloredlogs.DEFAULT_LOG_FORMAT = '%(message)s'
	coloredlogs.DEFAULT_LEVEL_STYLES['debug'] = {'color': 'cyan'}
	if 'DEBUG' in os.environ:
		coloredlogs.install(level=logging.DEBUG)
	else:
		coloredlogs.install()

except ImportError:
	pass


import argparse
import binascii
import struct
import sys
import time

import serial

import numpy as np
import scipy.io as sio
from scipy.optimize import fmin_bfgs




parser = argparse.ArgumentParser()
parser.add_argument('-s', '--serial',   default='/dev/tty.usbserial-AL00EZAS')
parser.add_argument('-f', '--file',     default=None)
parser.add_argument('-b', '--baudrate', default=3000000, type=int)
parser.add_argument('-o', '--outfile',  default='out')
#parser.add_argument('-t', '--textfiles',action='store_true',
#		help="Generate ASCII text files with the data")
#parser.add_argument('-m', '--matfile',  action='store_true',
#		help="Generate Matlab-compatible .mat file of the data")
#parser.add_argument('-n', '--binfile',  action='store_true',
#		help="Generate binary file of the data")

args = parser.parse_args()

#if not (args.textfiles or args.matfile or args.binfile):
#	print("Error: Must specify at least one of -t, -m, or -n")
#	print("")
#	parser.print_help()
#	sys.exit(1)

if args.file is not None:
	dev = open(args.file, 'rb')
	print("Reading data back from file:", args.file)
else:
	dev = serial.Serial(args.serial, args.baudrate)
	if dev.isOpen():
		print("Connected to device at " + dev.portstr)
	else:
		raise NotImplementedError("Failed to connect to serial device " + args.serial)


##########################################################################

# ; 4.756-2.608+.164
# 	2.312
# ; 14.125-2.640+.164
# 	11.649
# ; -4.685+.164+15.819-2.640+0.164
# 	8.822
# ; 3.193-2.64+.164
# 	0.717
ANCHORS = {
		'22': (  .212,  8.661, 4.047),
		'3f': ( 7.050,  0.064, 3.295),
		'28': (12.704,  9.745, 3.695),
		'2c': ( 2.312,  0.052, 1.369),
		'24': (11.649,  0.058, 0.333),
		'23': (12.704,  3.873, 2.398),
		'2e': ( 8.822, 15.640, 3.910),
		'2b': ( 0.717,  3.870, 2.522),
		'26': (12.704, 15.277, 1.494),
		}

def location_optimize(x,anchor_ranges,anchor_locations):
	if(x.size == 2):
		x = np.append(x,2)
	x = np.expand_dims(x, axis=0)
	x_rep = np.tile(x, [anchor_ranges.size,1])
	r_hat = np.sqrt(np.sum(np.power((x_rep-anchor_locations),2),axis=1))
	r_hat = np.reshape(r_hat,(anchor_ranges.size,))
	ret = np.sum(np.power(r_hat-anchor_ranges,2))
	return ret


def trilaterate(ranges, tag_position=np.array([0,0,0])):
	#loc_anchor_positions = ANCHOR_POSITIONS[sorted_range_idxs[first_valid_idx:last_valid_idx]]
	#loc_anchor_ranges = sorted_ranges[first_valid_idx:last_valid_idx]
	loc_anchor_positions = []
	loc_anchor_ranges = []
	for eui,range in ranges.items():
		try:
			loc_anchor_positions.append(ANCHORS[eui])
			loc_anchor_ranges.append(range)
		except KeyError:
			log.warn("Skipping anchor {} with unknown location".format(eui))
	loc_anchor_positions = np.array(loc_anchor_positions)
	loc_anchor_ranges = np.array(loc_anchor_ranges)
	log.debug(loc_anchor_positions)
	log.debug("loc_anchor_ranges = {}".format(loc_anchor_ranges))

	if loc_anchor_ranges.size < 3:
		raise NotImplementedError("Not enough ranges")
	#elif(num_valid_anchors == 2):
	#	if args.always_report_range:
	#		log.debug("WARNING: ONLY TWO ANCHORS...")
	#		loc_anchor_positions = ANCHOR_POSITIONS[sorted_range_idxs[first_valid_idx:last_valid_idx]]
	#		loc_anchor_ranges = sorted_ranges[first_valid_idx:last_valid_idx]
	#		log.debug("loc_anchor_ranges = {}".format(loc_anchor_ranges))
	#		tag_position = fmin_bfgs(
	#				f=location_optimize,
	#				x0=tag_position[0:2],
	#				args=(loc_anchor_ranges, loc_anchor_positions)
	#				)
	#		tag_position = np.append(tag_position,2)
	#	else:
	#		return None
	else:
		disp = False #True if log.isEnabledFor(logging.DEBUG) else False
		tag_position = fmin_bfgs(
				disp=disp,
				f=location_optimize, 
				x0=tag_position,
				args=(loc_anchor_ranges, loc_anchor_positions)
				)

	#print("{} {} {}".format(tag_position[0], tag_position[1], tag_position[2]))

	return tag_position

##########################################################################

def useful_read(length):
	b = dev.read(length)
	while len(b) < length:
		r = dev.read(length - len(b))
		if len(r) == 0:
			raise EOFError
		b += r
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

#if args.textfiles:
#	tsfile  = open(args.outfile + '.timestamps', 'w')
#	datfile = open(args.outfile + '.data', 'w')
#if args.matfile:
#	allts   = []
#	alldata = []
#if args.binfile:
#	binfile = open(args.outfile + '.bin', 'wb')

ofile = open(args.outfile, 'w')

try:
	while True:
		#sys.stdout.write("\rGood {}    Bad {}\t\t".format(good, bad))

		try:
			find_header()
			ts = time.time()

			num_anchors, = struct.unpack("<B", useful_read(1))

			ranging_broadcast_ss_send_times = np.array(struct.unpack("<30Q", useful_read(8*NUM_RANGING_BROADCASTS)))

			ranges = {}
			
			for x in range(num_anchors):
				b = useful_read(len(DATA_HEADER))
				if b != DATA_HEADER:
					raise AssertionError
				anchor_eui = useful_read(EUI_LEN)
				anchor_eui = anchor_eui[::-1] # reverse bytes
				anchor_eui = binascii.hexlify(anchor_eui).decode('utf-8')
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
					log.warn("tag_poll outside of range; skpping")
					continue
			
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
					if(tag_poll_TOAs[jj] & 0xFFFF > 0 and tag_poll_TOAs[26+jj] & 0xFFFF > 0):
						offset_cumsum = offset_cumsum + (tag_poll_TOAs[26+jj] - tag_poll_TOAs[jj])/(ranging_broadcast_ss_send_times[26+jj] - ranging_broadcast_ss_send_times[jj])
						num_valid_offsets = num_valid_offsets + 1

				if num_valid_offsets == 0:
					continue
				offset_anchor_over_tag = offset_cumsum/num_valid_offsets;

				# Figure out what broadcast the received response belongs to
				ss_index_matching = oneway_get_ss_index_from_settings(anchor_final_antenna_index, window_packet_recv)
				if int(tag_poll_TOAs[ss_index_matching]) & 0xFFFF == 0:
					log.warn("no bcast ss match, ss_index_matching {}, TOAs[{}] = {}".format(
						ss_index_matching, ss_index_matching, tag_poll_TOAs[ss_index_matching]))
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
		
					distance_millimeters.append(dwtime_to_millimeters(TOF))
		
				#anchor_eui_txt = dec2hex(anchor_eui)
				range_mm = np.percentile(distance_millimeters,10)


				if range_mm < 0 or range_mm > (1000*30):
					log.warn('Dropping impossible range %d', range_mm)
					continue

				log.debug('Anchor {} Range {}'.format(anchor_eui, range_mm))

				ranges[anchor_eui[-2:]] = range_mm / 1000

			footer = useful_read(len(FOOTER))
			if footer != FOOTER:
				raise AssertionError

			position = trilaterate(ranges)

			s = "{:.3f} {:1.4f} {:1.4f} {:1.4f}".format(ts, *position)
			print(s)
			ofile.write(s + '\n')

			good += 1

			#if args.binfile:
			#	binfile.write(bline)

		except (AssertionError, NotImplementedError):
			bad += 1

except KeyboardInterrupt:
	pass
except EOFError:
	pass

print("\nGood {}\nBad  {}".format(good, bad))
#if args.textfiles:
#	print("Wrote ASCII outputs to " + args.outfile + ".{timestamps,data}")
#if args.matfile:
#	sio.savemat(args.outfile+'.mat', {
#		'timestamps': allts,
#		'data': alldata,
#		})
#	print('Wrote Matlab-friendly file to ' + args.outfile + '.mat')
#if args.binfile:
#	print('Wrote binary output to ' + args.outfile + '.bin')
#	print('\tBinary data is formatted as:')
#	print('\t<uint64_t><int16_t><int16_t><int16_t><int16_t>... all little endian')
#	print('\ttimestmap real0    imag0    real1    imag1    ...')
#	print('\tFor 1024 total complex numbers')

print("Saved to {}".format(args.outfile))

