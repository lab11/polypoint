#!/usr/bin/env python

import sys, getopt
import os
import glob
import numpy as np

from scipy.optimize import fmin_bfgs

NUM_ANCHORS = 10
NUM_ANTENNAS = 3
NUM_CHANNELS = 3
ANCHOR_POSITIONS = np.matrix([
    [0.0, 0.0, 0.0],
    [1.0, 0.0, 0.0],
    [0.0, 1.0, 0.0],
    [1.0, 1.0, 0.0],
    [0.0, 0.0, 0.0],
    [0.0, 0.0, 0.0],
    [0.0, 0.0, 0.0],
    [0.0, 0.0, 0.0],
    [0.0, 0.0, 0.0],
    [0.0, 0.0, 0.0]
]);

def location_optimize(x,anchor_ranges,anchor_locations):
    x = np.expand_dims(x, axis=0)
    x_rep = np.tile(x, [anchor_ranges.size,1])
    return np.sum(np.sqrt(np.sum(np.power((x_rep-anchor_locations),2)))-anchor_ranges)

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
    cur_tag_ranges = []
    cur_tag_range_idx = 0
    ranges = np.zeros(NUM_ANCHORS)
    while True:
        cur_line = sp.readline()
        cur_line = cur_line.decode("utf-8")
        if(cur_line[0:8] == 'tagstart'):
            cur_tag_num = int(cur_line[9:])
            cur_tag_ranges = []
            cur_tag_range_idx = 0
        elif(cur_line[0:7] == 'tagdone'):
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
            ranges = np.zeros(NUM_ANCHORS)
            sorted_range_idxs = np.argsort(ranges)
            range_start_idx = 0
            first_valid_idx = np.where(sorted_ranges > 0)
            first_valid_idx = first_valid_idx[0]
            if(first_valid_idx.size > 0):
                first_valid_idx = first_valid_idx[0]
            else:
                first_valid_idx = NUM_ANCHORS
            last_valid_idx = first_valid_idx + 3
            
            #Make sure we have enough valid ranges to get a good fix on position (3)
            num_valid_anchors = sorted_ranges.size - first_valid_idx - 1
            if(num_valid_anchors < 3):
                print("ERROR: Not enough anchors this time... ")
            else:
                print("SUCCESS: Enough valid ranges to perform localization...")
                loc_anchor_positions = ANCHOR_POSITIONS[sorted_range_idxs[first_valid_idx:last_valid_idx]]
                loc_anchor_ranges = sorted_ranges[first_valid_idx:last_valid_idx]
                tag_position = fmin_bfgs(
                    f=location_optimize, 
                    x0=np.array([0, 0, 0]),
                    args=(loc_anchor_ranges, loc_anchor_positions)
                )
                print("Tag position: {}".format(tag_position))
        else:
            try:
                cur_tag_ranges[cur_tag_range_idx] = float(cur_line)
                cur_tag_range_idx = cur_tag_range_idx + 1
            except:
                pass
    sp.close()
