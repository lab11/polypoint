#!/usr/bin/env python3

efile = open('efile', 'w')

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
import datetime
import pprint
import random
import requests
import struct
import sys
import time

import serial

import numpy as np
import scipy.io as sio
from scipy.optimize import fmin_bfgs, least_squares

import dataprint


parser = argparse.ArgumentParser()
parser.add_argument('-a', '--anchor-history', action='store_true')
parser.add_argument('-s', '--serial',   default='/dev/tty.usbserial-AL00EZAS')
parser.add_argument('-f', '--file',     default=None)
parser.add_argument('-b', '--baudrate', default=3000000, type=int)
parser.add_argument('-o', '--outfile',  default='out')
parser.add_argument('-t', '--trilaterate', action='store_true')
parser.add_argument('-g', '--ground-truth', default=None)
parser.add_argument('-m', '--subsample', action='store_true')
parser.add_argument('-n', '--no-iter', action='store_true')
parser.add_argument('-e', '--exact', default=None, type=int)
parser.add_argument('-d', '--dump-full', action='store_true')
parser.add_argument('-v', '--diversity', default=None)
parser.add_argument('--gdp', action='store_true')
parser.add_argument('--gdp-rest', action='store_true')
parser.add_argument('--gdp-rest-url', default='http://localhost:8080')
parser.add_argument('--gdp-log', default='edu.umich.eecs.lab11.polypoint-test')
parser.add_argument('-j', '--anchors_from_json', action="store_true")
parser.add_argument('--anchor-url', default="http://j2x.us/ppts16")
#parser.add_argument('-t', '--textfiles',action='store_true',
#		help="Generate ASCII text files with the data")
#parser.add_argument('-m', '--matfile',  action='store_true',
#		help="Generate Matlab-compatible .mat file of the data")
#parser.add_argument('-n', '--binfile',  action='store_true',
#		help="Generate binary file of the data")

args = parser.parse_args()

if args.subsample and (args.exact is not None):
	raise NotImplementedError("Illegal flags -m + -e")

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
## GDP INTEGRATION

if args.gdp:
	import gdp
	gcl_name = gdp.GDP_NAME(args.gdp_log)
	gcl_handle = gdp.GDP_GCL(gcl_name, gdp.GDP_MODE_RA)

def post_to_gdp(x, y, z):
	print("POSTING TO GDP")
	payload = {
			"_meta": {
				"received_time": datetime.datetime.now().isoformat(),
				"device_id": "c0:98:e5:45:00:37", # TODO Make automatic
				"device_type": "surepoint_tag",
				},
			"x": x,
			"y": y,
			"z": z,
			}

	just_loc = {
			"x": x,
			"y": y,
			"z": z,
			}


	if args.gdp:
		gdp_datum = {"data": payload}
		gcl_handle.append(gdp_datum)
	if args.gdp_rest:
		#r = requests.post(args.gdp_rest_url, json=payload)
		r = requests.put(args.gdp_rest_url, params=just_loc)
		print("POSTED TO GDP")


##########################################################################
## DYNAMIC ANCHOR SUPPORT

if args.anchors_from_json:
	r = requests.get(args.anchor_url)
	_ANCHORS = r.json()
	ANCHORS = {}
	for anc in _ANCHORS:
		ANCHORS[anc.split(':')[-1]] = _ANCHORS[anc]

##########################################################################

# ; 4.756-2.608+.164
# 	2.312
# ; 14.125-2.640+.164
# 	11.649
# ; -4.685+.164+15.819-2.640+0.164
# 	8.822
# ; 3.193-2.64+.164
# 	0.717
_ANCHORS = {
		'22': (  .212,  8.661, 4.047),
		'3f': ( 7.050,  0.064, 3.295),
		'28': (12.704,  9.745, 3.695),
		'2c': ( 2.312,  0.052, 1.369),
		'24': (11.649,  0.058, 0.333),
		'23': (12.704,  3.873, 2.398),
		'2e': ( 8.822, 15.640, 3.910),
		'2b': ( 0.717,  3.870, 2.522),
		'26': (12.704, 15.277, 1.494),
		'30': (12.610,  9.768, 0.064),
		'27': ( 0.719,  3.864, 0.068),
		}

if not args.anchors_from_json:
	ANCHORS = _ANCHORS

if args.ground_truth:
	GT = np.array(list(map(float, args.ground_truth.split(','))))
	GT_RANGE = {}
	for a in ANCHORS:
		GT_RANGE[a] = np.sqrt(sum( (np.array(ANCHORS[a]) - GT)**2 ))




def location_optimize(x,anchor_ranges,anchor_locations):
	if(x.size == 2):
		x = np.append(x,2)
	x = np.expand_dims(x, axis=0)
	x_rep = np.tile(x, [anchor_ranges.size,1])
	r_hat = np.sqrt(np.sum(np.power((x_rep-anchor_locations),2),axis=1))
	r_hat = np.reshape(r_hat,(anchor_ranges.size,))
	ret = np.sum(np.power(r_hat-anchor_ranges,2))
	return ret



def _trilaterate2(lr, lp, last_pos):
	log.debug('------------------- tri2')
	lsq = least_squares(location_optimize, last_pos, args=(lr, lp))
	pos = lsq['x']
	if args.ground_truth:
		error = np.sqrt(sum( (pos - GT)**2 ))
		log.debug('lsq: {} err {:.4f} opt {:.8f}'.format(pos, error, lsq['optimality']))

	if lsq['optimality'] > 1e-5:
		best = 1
		nextp = None
		dropping = None
		for i in range(len(lr)):
			lr2 = np.delete(np.copy(lr), i)
			lp2 = np.delete(np.copy(lp), i, axis=0)
			lsq2 = least_squares(location_optimize, last_pos, args=(lr2, lp2))
			pos2 = lsq2['x']
			opt2 = lsq2['optimality']
			if args.ground_truth:
				error2 = np.sqrt(sum( (pos2 - GT)**2 ))
				log.debug("lsq w/out {:>8.4f}: {} err {:.4f} opt {:.8f}".format(
					lr[i], pos2, error2, opt2))
			if opt2 < best:
				best = opt2
				nextp = (lr2, lp2, last_pos)
				dropping = lr[i]
		log.debug('dropping {:.4f}'.format(dropping))
		return _trilaterate2(*nextp)

	if args.ground_truth:
		efile.write(str(error)+'\t'+str(lsq['optimality'])+'\t'+str(lsq['status'])+'\n')
	return pos, lr, lp, lsq['optimality']


def trilaterate(ranges, last_position):
	efile.write('\n')
	loc_anchor_positions = []
	loc_anchor_ranges = []
	for eui,arange in ranges.items():
		try:
			loc_anchor_positions.append(ANCHORS[eui])
			loc_anchor_ranges.append(arange)
		except KeyError:
			log.warn("Skipping anchor {} with unknown location".format(eui))
	loc_anchor_positions = np.array(loc_anchor_positions)
	loc_anchor_ranges = np.array(loc_anchor_ranges)

	pos, lr, lp = _trilaterate(loc_anchor_ranges, loc_anchor_positions, last_position)
	return pos
	pos2, lr2, lp2, opt2 = _trilaterate2(loc_anchor_ranges, loc_anchor_positions, last_position)

	if args.ground_truth:
		error  = np.sqrt(sum( (pos  - GT)**2 ))
		error2 = np.sqrt(sum( (pos2 - GT)**2 ))
	if len(lr) == len(lr2):
		if args.ground_truth:
			efile.flush()
			if (error - error2) > .05: raise
		return pos

	log.debug("")
	log.debug("Aggregate")
	# np.intersect1d sorts and requires flattening lp; roll our own
	j = 0
	lr3 = []
	lp3 = []
	for i in range(len(lr)):
		if lr[i] in lr2:
			lr3.append(lr[i])
			lp3.append(lp[i])
	lr3 = np.array(lr3)
	lp3 = np.array(lp3)
	pos3, _, _ = _trilaterate(lr3, lp3, pos)
	pos4, _, _, opt4 = _trilaterate2(lr3, lp3, pos)
	#pos5 = (pos3+pos4)/2
	#if args.ground_truth:
	#	error5 = np.sqrt(sum( (pos5 - GT)**2 ))
	#	efile.write(str(error5)+'\n')
	#	log.debug('pos5 {} err {}'.format(pos5, error5))

	if args.ground_truth:
		error3 = np.sqrt(sum( (pos3 - GT)**2 ))
		error4 = np.sqrt(sum( (pos4 - GT)**2 ))
		efile.flush()
		if (error3 - error4) > .10: raise
		if (error3 - error2) > .10: raise

	if opt2 < opt4:
		return pos2
	else:
		return pos3
	return pos5

nodiversity_drop_count = 0

t1_errs = []
t1_iter_drop_count = 0

total_ranges_count = 0

def _trilaterate(loc_anchor_ranges, loc_anchor_positions, last_position, last_error=None):
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
	#				x0=last_position[0:2],
	#				args=(loc_anchor_ranges, loc_anchor_positions)
	#				)
	#		tag_position = np.append(tag_position,2)
	#	else:
	#		return None
	else:
		disp = False #True if log.isEnabledFor(logging.DEBUG) else False
		tag_position, root_fopt, gopt, Bopt, func_calls, grad_calls, warnflag = fmin_bfgs(
				disp=disp,
				f=location_optimize, 
				x0=last_position,
				args=(loc_anchor_ranges, loc_anchor_positions),
				full_output=True,
				)

	if args.no_iter:
		return tag_position, loc_anchor_ranges, loc_anchor_positions

	#print("{} {} {}".format(tag_position[0], tag_position[1], tag_position[2]))

	log.debug("root_fopt: {}".format(root_fopt))
	if root_fopt > 0.1 and loc_anchor_ranges.size > 3:
		fopts = {}
		fopts_dbg_r = {}
		for i in range(len(loc_anchor_ranges)):
			lr = np.delete(np.copy(loc_anchor_ranges), i)
			lp = np.copy(loc_anchor_positions)
			lp = np.delete(lp, i, axis=0)
			tag_position, fopt, gopt, Bopt, func_calls, grad_calls, warnflag = fmin_bfgs(
					disp=disp,
					f=location_optimize, 
					x0=last_position,
					args=(lr, lp),
					full_output=True,
					)
			fopts[fopt] = (lr, lp)
			fopts_dbg_r[fopt] = i

		s = list(sorted(fopts.keys()))

		global t1_iter_drop_count
		t1_iter_drop_count += 1
		log.warn("--------------------------iter drop (count {})".format(t1_iter_drop_count))
		log.debug(fopts.keys())
		log.debug("dropping range {:.4f}".format(loc_anchor_ranges[fopts_dbg_r[min(fopts)]]))
		lr, lp = fopts[min(fopts)]
		error = None
		if args.ground_truth:
			error = np.sqrt(sum( (tag_position - GT)**2 ))
			log.debug("pos before drop {} Err {:.4f}".format(tag_position, error))
		else:
			log.debug("pos before drop {}".format(tag_position))
		return _trilaterate(lr, lp, last_position, last_error=error)


	if args.ground_truth:
		error = np.sqrt(sum( (tag_position - GT)**2 ))
		efile.write(str(error)+'\t'+str(root_fopt)+'\n')
		t1_errs.append(error)
		log.debug("tag_position {} Err {}".format(tag_position, error))
	else:
		log.debug("tag_position {}".format(tag_position))
	return tag_position, loc_anchor_ranges, loc_anchor_positions

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


if args.dump_full:
	dffiles = {}
	for a in ANCHORS:
		dffiles[a] = open(args.outfile + '.df.' + a, 'w')
		dffiles[a].write('# ts\t\t' + '\t'.join(list(map(str,
			range(NUM_RANGING_BROADCASTS)))) + '\n')


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

data_array = []

anc_seen_hist = [5]
anc_seen_hist_ids = []

anc_seen_counts = {}
for a in ANCHORS:
	anc_seen_counts[a] = 0

windows = [0,0,0]

start = 1459998187.496
first_time = None

last_position = np.array((0,0,0))

try:
	while True:
		#if good >= 38:
		#	break
		if args.anchor_history:
			for _aid in sorted(ANCHORS):
				cnt = 0
				for l in anc_seen_hist_ids:
					for k in l:
						if k == _aid:
							cnt += 1
				print('{} {}'.format(_aid, cnt))

		#sys.stdout.write("\rGood {}    Bad {}\t\t".format(good, bad))
		log.info("Good {}    Bad {}    Avg {:.1f}    Last {}\t\t".format(
				good, bad, np.mean(anc_seen_hist), anc_seen_hist[-1]))

		try:
			log.debug("")
			log.debug("")
			log.debug("")
			find_header()

			num_anchors, = struct.unpack("<B", useful_read(1))

			ranging_broadcast_ss_send_times = np.array(struct.unpack("<30Q", useful_read(8*NUM_RANGING_BROADCASTS)))

			if first_time is None:
				first_time = ranging_broadcast_ss_send_times[15]
			DWT_TIME_UNITS = (1.0/499.2e6/128.0)
			ts = start +  (ranging_broadcast_ss_send_times[15] - first_time) * DWT_TIME_UNITS

			ranges = {}
			
			for x in range(num_anchors):
				b = useful_read(len(DATA_HEADER))
				if b != DATA_HEADER:
					log.warn("missed DATA_HEADER")
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
					if(tag_poll_TOAs[jj] & 0xFFFF > 0 and tag_poll_TOAs[27+jj] & 0xFFFF > 0):
						offset_cumsum = offset_cumsum + (tag_poll_TOAs[27+jj] - tag_poll_TOAs[jj])/(ranging_broadcast_ss_send_times[27+jj] - ranging_broadcast_ss_send_times[jj])
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

				if args.dump_full:
					dffiles[anchor_eui[-2:]].write('{:.2f}\t'.format(ts))
		
				# Declare an array for sorting ranges
				distance_millimeters = []
				d_mm_all = []
				for jj in range(NUM_RANGING_BROADCASTS):
					broadcast_send_time = ranging_broadcast_ss_send_times[jj]
					broadcast_recv_time = tag_poll_TOAs[jj]
					if int(broadcast_recv_time) & 0xFFFF == 0:
						if args.dump_full:
							dffiles[anchor_eui[-2:]].write('nan\t')
						d_mm_all.append(-111)
						continue
		
					broadcast_anchor_offset = broadcast_recv_time - matching_broadcast_recv_time
					broadcast_tag_offset = broadcast_send_time - matching_broadcast_send_time
					TOF = broadcast_anchor_offset - broadcast_tag_offset*offset_anchor_over_tag + one_way_TOF
		
					mm = dwtime_to_millimeters(TOF)
					mm -= 121.591
					distance_millimeters.append(mm)
					d_mm_all.append(mm)

					if args.dump_full:
						dffiles[anchor_eui[-2:]].write('{:.2f}\t'.format(mm/1000))

				if args.dump_full:
					dffiles[anchor_eui[-2:]].write('\n')


				# Allow for experiments that ignore diversity
				if args.diversity is not None:
					if args.diversity[0] == 'r':
						d = []
						for i in range(int(args.diversity[1:])):
							cand = d_mm_all.pop(random.randrange(len(d_mm_all)))
							if cand != -111:
								d.append(cand)
						if len(d) == 0:
							nodiversity_drop_count += 1
							print("Dropping, cnt", nodiversity_drop_count)
							continue
						range_mm = np.percentile(d,10)
					else:
						range_mm = d_mm_all[int(args.diversity)]
				else:
					range_mm = np.percentile(distance_millimeters,10)


				if range_mm < 0 or range_mm > (1000*30):
					log.warn('Dropping impossible range %d', range_mm)
					continue

				if args.ground_truth:
					log.debug('Anchor {} Range {:.4f} Error {:.4f}'.format(anchor_eui,
						range_mm/1000, range_mm/1000 - GT_RANGE[anchor_eui[-2:]]))
				else:
					log.debug('Anchor {} Range {:.4f}'.format(anchor_eui, range_mm/1000))

				ranges[anchor_eui[-2:]] = range_mm / 1000
				windows[window_packet_recv] += 1

			footer = useful_read(len(FOOTER))
			if footer != FOOTER:
				raise AssertionError

			if len(anc_seen_hist) > 20:
				anc_seen_hist.pop(0)
			anc_seen_hist.append(len(ranges))

			if len(anc_seen_hist_ids) > 30:
				anc_seen_hist_ids.pop(0)
			anc_seen_hist_ids.append(list(ranges.keys()))

			if args.subsample:
				while len(ranges) > 3:
					del ranges[list(ranges.keys())[random.randrange(0, len(ranges))]]

			if args.exact is not None:
				if len(ranges) < args.exact:
					continue
				while len(ranges) > args.exact:
					del ranges[list(ranges.keys())[random.randrange(0, len(ranges))]]

			total_ranges_count += len(ranges)

			for r in ranges:
				anc_seen_counts[r] += 1

			if args.trilaterate:# and good > 35:
				position = trilaterate(ranges, last_position)
				last_position = position

				if args.ground_truth:
					error = np.sqrt(sum( (position - GT)**2 ))
					efile.write(str(error)+'\n')
					s = "{:.3f} {:1.4f} {:1.4f} {:1.4f} Err {:1.4f}".format(ts, *position, error)
				else:
					s = "{:.3f} {:1.4f} {:1.4f} {:1.4f}".format(ts, *position)
				print(s)

				aa = []
				for a in sorted(ANCHORS.keys()):
					if a in ranges:
						aa.append(ranges[a])
					else:
						aa.append(0)

				data_array.append([ts, *position, *aa])

				if args.gdp or args.gdp_rest:
					post_to_gdp(*position)

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
if sum(windows):
	print("Windows {} ({:.1f}) {} ({:.1f}) {} ({:.1f})".format(
		windows[0], 100* windows[0] / sum(windows),
		windows[1], 100* windows[1] / sum(windows),
		windows[2], 100* windows[2] / sum(windows),
		))
print("Iteration dropped {} times".format(t1_iter_drop_count))
print("Total ranges {}".format(total_ranges_count))
print("Anchors Seen:")
pprint.pprint(anc_seen_counts)
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

#dataprint.to_newfile(args.outfile, data_array,
#		overwrite=True,
#		#comments=("Time", "X", "Y", "Z"),
#		)

if args.trilaterate:
	ofile = open(args.outfile, 'w')
	aa = []
	for a in sorted(ANCHORS.keys()):
		aa.append(':'.join((a, *map(str, ANCHORS[a]))))
	ofile.write("#" + '\t'.join(("Time", "X", "Y", "Z", *aa)) + '\n')
	dataprint.to_file(ofile, data_array)
	ofile.write('#windows {} {} {}\n'.format(*windows))
	print("Saved to {}".format(args.outfile))


if args.ground_truth:
	t1_errs = np.array(t1_errs)
	print("t1:  min {:.4f} max {:.4f} mean {:.4f} med {:.4f}".format(
		np.min   (t1_errs),
		np.max   (t1_errs),
		np.mean  (t1_errs),
		np.median(t1_errs),
		))

print(nodiversity_drop_count)

