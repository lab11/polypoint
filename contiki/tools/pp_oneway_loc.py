#!/usr/bin/env python

import sys
import argparse
import os
import glob
import numpy as np
import traceback

from scipy.optimize import fmin_bfgs

NUM_ANCHORS = 10
NUM_ANTENNAS = 3
NUM_CHANNELS = 3
NUM_MEASUREMENTS = NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS
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
    [15.236,    0.502,  2], # pos1
    [9.470,     0.502,  2], # pos2
    [3.856,     0.502,  2], # pos3
    [-100,      -100,   -100], #dummy
    [-100,      -100,   -100], #dummy
    [0.055,     12.955, 2], # pos6
    [3.863,     12.526, 2], # pos7
    [-100,      -100,   -100], #dummy
    [0.055,     4.063,  2], # pos4
    [0.055,     8.228,  2], # pos5
]);

# Positions not currently occupied by an anchor
#    [9.700,     12.526, 2],   # pos8
#    [19.488,    10.957, 2.5], # pos9
#    [19.488,    8.227,  2.5], # pos10

def location_optimize(x,anchor_ranges,anchor_locations):
    if(x.size == 2):
        x = np.append(x,2)
    x = np.expand_dims(x, axis=0)
    x_rep = np.tile(x, [anchor_ranges.size,1])
    r_hat = np.sqrt(np.sum(np.power((x_rep-anchor_locations),2),axis=1))
    r_hat = np.reshape(r_hat,(anchor_ranges.size,))
    ret = np.sum(np.power(r_hat-anchor_ranges,2))
    return ret

def get_line(port):
    if args.port != '-':
        # Open the serial connection
        sp = serial.Serial(
            port=args.port,
            baudrate=115200,
            bytesize=8,
            parity=serial.PARITY_NONE,
            stopbits=1,
            xonxoff=0,
            rtscts=0,
            timeout=None
        )

    while True:
        if args.port != '-':
            cur_line = sp.readline()
            cur_line = cur_line.decode("utf-8")
            cur_line = cur_line.strip()
        else:
            # Note: This won't handler EOF correctly:
            #cur_line = sys.stdin.readline().strip()
            for line in iter(sys.stdin.readline, ''):
                if '#' in line:
                    # Ignore lines with '# Corrupted packet.'
                    continue
                if ':' in line:
                    # Handle timestamp if present
                    line = line.split(':')[1].strip()
                yield line.strip()
            break
        if len(cur_line) == 0:
            continue
        yield cur_line

def get_measurements(port):
    def parse_measurement(meas):
        n,f = map(float, meas.split('.'))
        m = abs(n) + abs(f)/100
        if n < 0 or f < 0:
            m = -m
        return m

    lines = get_line(port)

    # Throw away the first line as it's probably partial
    lines.next()

    while True:
        line = lines.next()

        if line[-1] == '!':
            # If the line ends in ! assume we are in the %ile only case
            meas = []
            for m in line.split():
                if m == '!':
                    break
                elif '.' in m:
                    meas.append(parse_measurement(m))
                else:
                    meas.append(0.0)
            yield meas
        elif line[0] == '[':
            raise NotImplementedError("Don't currently parse DW_DEBUG output")
        else:
            # In print all measurements case, may or may not be sorted

            # First, hunt for a 'tagstart 1' to get to the start of a round
            while line[0:8] != 'tagstart 1':
                line = lines.next()
            i = 1
            ranges = {}
            while True:
                line = lines.next()
                if line == 'done':
                    print("WARN: Premature 'done' message, skipping measurement")
                    break

                ranges[i] = map(parse_measurement, line.split())
                if len(ranges) != NUM_MEASUREMENTS:
                    print("WARN: Measurement with too few ranges, skipping.  Had", len(ranges))
                    break

                i += 1

                line = lines.next()
                if i < 10:
                    expect = 'tagstart {}'.format(i)
                    if line != expect:
                        print("WARN: expected {} got {}. Skipping measurement".\
                                        format(expect, line))
                        break
                else:
                    if line != 'done':
                        print("WARN: expected {} got {}. Skipping measurement".\
                                        format('done', line))
                        break

                    meas = []
                    for j in range(1, 11):
                        temp_array = np.array([val for val in ranges[j] if (val > -50 and val > 50*100)])
                        if temp_array.size > 0:
                            meas.append(np.percentile(temp_array, 10))
                        else:
                            meas.append(0.0)

                    yield meas
                    break

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
        port_name = '-'

    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--port', default=port_name,
            help="Specify a port (use '-' for stdin)")
    args = parser.parse_args()

    if args.port == '-':
        print("Reading from stdin (probably want ./PolyPointReceive | ./" + sys.argv[0])
    else:
        print("Reading from serial port at " + args.port)

    measurements = get_measurements(args.port)

    #Wait for comment indicating restart condition
    tag_position=np.array([0, 0, 0])
    while True:
            meas = measurements.next()
            ranges = np.array(meas)

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
            last_valid_idx = NUM_ANCHORS

            #Make sure we have enough valid ranges to get a good fix on position (3)
            num_valid_anchors = sorted_ranges.size - first_valid_idx
            print("Seeing {} anchors (from {}-{})".format(num_valid_anchors, first_valid_idx, last_valid_idx))
            if(num_valid_anchors == 0):
                print("ERROR: Zero anchors this time... ")
                print("Guessing last location: {}".format(tag_position))
            elif(num_valid_anchors == 1):
                print("WARNING: ONLY ONE ANCHOR...")
                print("Guessing last location: {}".format(tag_position))
            elif(num_valid_anchors == 2):
                print("WARNING: ONLY TWO ANCHORS...")
                loc_anchor_positions = ANCHOR_POSITIONS[sorted_range_idxs[first_valid_idx:last_valid_idx]]
                loc_anchor_ranges = sorted_ranges[first_valid_idx:last_valid_idx]
                print("loc_anchor_ranges = {}".format(loc_anchor_ranges))
                tag_position = fmin_bfgs(
                    f=location_optimize, 
                    x0=tag_position[0:2],
                    args=(loc_anchor_ranges, loc_anchor_positions)
                )
                tag_position = np.append(tag_position,2)
                #print("Tag position: {}".format(tag_position))
            else:
                print("SUCCESS: Enough valid ranges to perform localization...")
                loc_anchor_positions = ANCHOR_POSITIONS[sorted_range_idxs[first_valid_idx:last_valid_idx]]
                loc_anchor_ranges = sorted_ranges[first_valid_idx:last_valid_idx]
                print(loc_anchor_positions)
                print("loc_anchor_ranges = {}".format(loc_anchor_ranges))
                tag_position = fmin_bfgs(
                    f=location_optimize, 
                    x0=tag_position,
                    args=(loc_anchor_ranges, loc_anchor_positions)
                )
                print("Tag position: {} {} {}".format(tag_position[0], tag_position[1], tag_position[2]))
                #print("Tag position: {}".format(tag_position))

