#!/usr/bin/env python

import sys, getopt
import os
import glob
import numpy as np
import traceback

from scipy.optimize import fmin_bfgs

NUM_ANCHORS = 10
NUM_ANTENNAS = 3
NUM_CHANNELS = 3
#ANCHOR_POSITIONS = np.matrix([
#    [0.055, 0.582, 1.808],
#    [4.255, 0.567, 1.756],
#    [0.826, 7.179, 1.457],
#    [4.263, 6.190, 1.620],
#    [0.0, 0.0, 0.0],
#    [0.0, 0.0, 0.0],
#    [0.0, 0.0, 0.0],
#    [0.0, 0.0, 0.0],
#    [0.0, 0.0, 0.0],
#    [0.0, 0.0, 0.0]
#]);
#ANCHOR_POSITIONS = np.matrix([
#    [11.066+1.778, 0.055, 1.666],
#    [6.403+1.778, 0.055, 1.776],
#    [0.28+1.778, 0.055, 1.908],
#    [15.713-7.582, 19.53, 1.839],
#    [0.444+0.055, 3.901, 2.180],
#    [12.603-0.055, 3.836, 2.094],
#    [0.444+0.055, 9.762, 2.224],
#    [4.148+1.778, 0.055, 1.689],
#    [0.444-0.051, 15.065, 2.145],
#    [15.713-0.055, 25.233-12.338, 1.394]
#]);
ANCHOR_POSITIONS = np.matrix([
    [28.981, 15.296, 2.517],
    [35.766, 42.256, 1.905],
    [35.417, 0.054, 2.203],
    [15.072, 15.213, 2.091],
    [16.531, 0.054, 2.227],
    [29.123, 34.708, 2.597],
    [0.999, 4.027, 2.238],
    [38.982, 33.845, 2.105],
    [39.372, 8.606, 1.970],
    [53.010, -0.076, 2.249]
]);

def location_optimize(x,anchor_ranges,anchor_locations):
    x = np.expand_dims(x, axis=0)
    x_rep = np.tile(x, [anchor_ranges.size,1])
    r_hat = np.sqrt(np.sum(np.power((x_rep-anchor_locations),2),axis=1))
    r_hat = np.reshape(r_hat,(anchor_ranges.size,))
    ret = np.sum(np.power(r_hat-anchor_ranges,2))
    return ret

try:
    import serial
except ImportError:
    print('{} requires the Python serial library'.format(sys.argv[0]))
    print('Please install it with one of the following:')
    print('')
    if PY3:
        print('   Ubuntu:  sudo apt-get install python3-serial')
        print('   Mac:     sudo port install py34-serial')
    else:
        print('   Ubuntu:  sudo apt-get install python-serial')
        print('   Mac:     sudo port install py-serial')
    sys.exit(1)

if __name__ == "__main__":
    #anchor_ranges = np.array([ 2.5106,  3.128,   4.2106])
    #anchor_locations = np.matrix([[ 4.255,  0.567,  1.756],
    #    [ 0.055,  0.582,  1.808],
    #    [ 4.263,  6.19,   1.62 ]])
    #tag_position = fmin_bfgs(
    #    f=location_optimize, 
    #    x0=np.array([0, 0, 0]),
    #    args=(anchor_ranges, anchor_locations)
    #)
    #print("Tag position: {}".format(tag_position))
    #blah = location_optimize(np.array([2.58, 2.374, 1.824]),anchor_ranges,anchor_locations)
    #print(blah)

    # Try and find the port automatically
    ports = []

    # Get a list of all USB-like names in /dev
    for name in ['tty.usbserial', 'ttyUSB', 'tty.usbmodem']:
        ports.extend(glob.glob('/dev/%s*' % name))

    ports = sorted(ports)

    if ports:
        # Found something - take it
        port_name = ports[0]
    else:
        raise Exception('No serial port found.')

    # Open the serial connection
    sp = serial.Serial(
        port=port_name,
        baudrate=115200,
        bytesize=8,
        parity=serial.PARITY_NONE,
        stopbits=1,
        xonxoff=0,
        rtscts=0,
        timeout=None
    )

    #Wait for comment indicating restart condition
    cur_tag_num = 0
    cur_tag_ranges = np.zeros(NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS)
    cur_tag_range_idx = 0
    ranges = np.zeros(NUM_ANCHORS)
    tag_position=np.array([0, 0, 0])
    while True:
        cur_line = sp.readline()
        cur_line = cur_line.decode("utf-8")
        cur_line = cur_line.strip()
        if(cur_line[0:8] == 'tagstart'):
            cur_tag_num = int(cur_line[9:])
            cur_tag_ranges = np.zeros(NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS)
            cur_tag_range_idx = 0
        elif(cur_line[0:6] == 'tagend'):
            #Actual tag range is based of a percentile of all range estimates
            temp_array = np.array([value for value in cur_tag_ranges if value != 0])
            if(temp_array.size > 0):
                p = np.percentile(temp_array, 10)
                ranges[cur_tag_num-1] = p
            else:
                ranges[cur_tag_num-1] = 0
        elif(cur_line[0:4] == 'done'):
            #Perform trilateration processing on all received data
            sorted_ranges = np.sort(ranges)
            sorted_range_idxs = np.argsort(ranges)
            ranges = np.zeros(NUM_ANCHORS)
            range_start_idx = 0
            first_valid_idx = np.where(sorted_ranges > 0)
            first_valid_idx = first_valid_idx[0]
            if(first_valid_idx.size > 0):
                first_valid_idx = first_valid_idx[0]
            else:
                first_valid_idx = NUM_ANCHORS
            last_valid_idx = first_valid_idx + 3
            
            #Make sure we have enough valid ranges to get a good fix on position (3)
            num_valid_anchors = sorted_ranges.size - first_valid_idx
            print("Seeing {} anchors",NUM_ANCHORS-first_valid_idx)
            if(num_valid_anchors == 0):
                print("ERROR: Zero anchors this time... ")
                print("Guessing last location: {}".format(tag_position))
            elif(num_valid_anchors == 1):
                print("WARNING: ONLY ONE ANCHOR...")
                print("Guessing last location: {}".format(tag_position))
            else:
                print("SUCCESS: Enough valid ranges to perform localization...")
                loc_anchor_positions = ANCHOR_POSITIONS[sorted_range_idxs[first_valid_idx:last_valid_idx]]
                loc_anchor_ranges = sorted_ranges[first_valid_idx:last_valid_idx]
                print("loc_anchor_ranges = {}".format(loc_anchor_ranges))
                tag_position = fmin_bfgs(
                    f=location_optimize, 
                    x0=tag_position,
                    args=(loc_anchor_ranges, loc_anchor_positions)
                )
                print("Tag position: {}".format(tag_position))
        else:
            try:
                cur_tag_ranges[cur_tag_range_idx] = float(cur_line)
                cur_tag_range_idx = cur_tag_range_idx + 1
            except:
                pass
                #traceback.print_exc()
    sp.close()
