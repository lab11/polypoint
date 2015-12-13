Calibration
===========

We need to calibrate each TriPoint module to account for unique TX and RX
delays with DW1000 radios.


Process
-------

Calibration requires three TriPoint nodes. The nodes need to placed at
well-known distances from each other. The calibration scripts currently
assume that nodes are placed in a triangle each 1 m apart.

The TriPoint software includes the code needed to run calibration. To do
calibration with TriTag or TriTag compatible hardware, flash the `tritag`
nRF51822 BLE application on the TriTag. Then, putting
the node in calibration mode requires writing the calibration node index
to the `0x3159` characteristic. There are three "roles" (0,1,2) that nodes
can be assigned during calibration. Calibration roles are set by writing
one of 0, 1, or 2 to the `0x3159` characteristic. Role 0 must be written
last, as calibration will start once this role is assigned.

In practice, do this:

1. Install [node](https://nodejs.org/en/download/) first. Version 2.0+ should work.
Then install the dependencies:

        # In the calibration folder
        npm install

1. Make noble run without sudo:

        sudo setcap cap_net_raw+eip $(eval readlink -f `which node`)

2. Collect the data from each node:

        node ./calibration_log.js

    Make sure there are only 3 nodes on during calibration.

3. Process the data into a single file

        ./calibration_condense.py YYYY-MM-DD_HH-MM-SS

4. Compute the calibration values for each node involved and add the
results to the main calibration file.

        ./calibration_compute.py tripoint_calibration_YYYY-MM-DD_HH-MM-SS.condensed

5. When flashing the TriPoint firmware, the build system will check
`tripoint_calibration.data` for calibration constants and use those
if they exist.

