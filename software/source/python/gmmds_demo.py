#!/usr/bin/env python3

import matplotlib.pyplot as plt
import matplotlib.image as mpimg
import numpy as np
import json
import math
import struct
import serial
import sys
import os
import binascii
import argparse
from oct2py import octave
import scipy.io as sio

def tic():
    #Homemade version of matlab tic and toc functions
    import time
    global startTime_for_tictoc
    startTime_for_tictoc = time.time()

def toc():
    import time
    if 'startTime_for_tictoc' in globals():
        return time.time() - startTime_for_tictoc
    else:
        print("Toc: start time not set")

#Set up paths to point to python code directory
octave.addpath('../matlab')
octave.addpath('../matlab/gmmds_mwe')
octave.addpath('../matlab/gmmds_mwe/EM')
octave.addpath('../matlab/gmmds_mwe/MDS')
octave.addpath('../matlab/gmmds_mwe/sim')
octave.addpath('../matlab/gmmds_mwe/util')
octave.eval('more off')
octave.eval('pkg load statistics')

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

def find_header():
	b = useful_read(len(HEADER))
	while b != HEADER:
		b = b[1:len(HEADER)] + useful_read(1)

anchor_coords = {
	'31': (  2.421, -1.032, 1.224),
	'2e': (  4.598, -2.309, 0.740),
	'26': (  4.598, -4.285, 1.983),
	'22': (  2.476, -7.179, 1.882),
	'27': (  0.055, -4.903, 1.482),
	'3f': (  0.055, -3.904, 1.779),
	'23': (  0.055, -2.657, 2.202),
	'2b': (  0.055, -0.871, 2.263),
	'2c': ( -0.188, -4.124, 2.433),
	'37': ( -2.099, -4.557, 2.241),
	'1f': ( -2.099, -2.594, 0.483),
	'38': ( -0.133, -0.631, 1.549),
	'3e': (  0.410,  0.206, 2.283),
}

good = 0
bad = 0
D_hat = np.empty([0, 0, 0])
D_hat_len = np.empty([0, 0])
D_hat_ids = {}
tic()
try:
	while True:
		sys.stdout.write("\rGood {}    Bad {}\t\t".format(good, bad))

		try:
			find_header()

			reporting_id = binascii.hexlify(useful_read(1)).decode('utf-8')
			ranging_id = binascii.hexlify(useful_read(1)).decode('utf-8')

			#Keep track of mapping from ID to index in array
			if not reporting_id in D_hat_ids:
				D_hat_ids[reporting_id] = len(D_hat_ids.keys())
			if not ranging_id in D_hat_ids:
				D_hat_ids[ranging_id] = len(D_hat_ids.keys())
			reporting_idx = D_hat_ids[reporting_id]
			ranging_idx = D_hat_ids[ranging_id]

			#In an effort to keep the matrix symmetric and not lose any data, always make sure reporting_idx < ranging_idx
			if ranging_idx < reporting_idx:
				reporting_idx, ranging_idx = ranging_idx, reporting_idx

			for x in range(27):
				cur_range = float(struct.unpack("<h", useful_read(2))[0])/1000
				max_len = int(max(D_hat_len.flatten(), default=0) + 1)
				D_hat_new = np.zeros([len(D_hat_ids), len(D_hat_ids), max_len])
				D_hat_len_new = np.zeros([len(D_hat_ids), len(D_hat_ids)])
				D_hat_new[:D_hat.shape[0],:D_hat.shape[1],:D_hat.shape[2]] = D_hat
				D_hat_len_new[:D_hat_len.shape[0],:D_hat_len.shape[1]] = D_hat_len
				D_hat = D_hat_new
				D_hat_len = D_hat_len_new
				D_hat[reporting_idx,ranging_idx,int(D_hat_len[reporting_idx,ranging_idx])] = cur_range
				D_hat[ranging_idx,reporting_idx,int(D_hat_len[reporting_idx,ranging_idx])] = cur_range
				D_hat_len[reporting_idx,ranging_idx] = D_hat_len[reporting_idx,ranging_idx] + 1

			good += 1

		except AssertionError:
			bad += 1

		#Re-perform network localization every second
		if toc() > 10:
	
			#Construct array of intial guesses for XY location
			xy = np.zeros([D_hat.shape[0], 3])
			for anchor_id in D_hat_ids:
				xy[D_hat_ids[anchor_id],:] = np.asarray(anchor_coords[anchor_id])
			print(xy)
			print(D_hat)

			sio.savemat('passed_data.mat', {'D_hat': D_hat, 'xy': xy})
			octave.TerraSwarmDemo(D_hat, xy, 3, xy)
			D_hat = np.empty([0, 0, 0])
			D_hat_len = np.empty([0, 0])
			D_hat_ids = {}
			tic()

except KeyboardInterrupt:
	pass
   

