PolyPoint
=========

NOTE: This branch is used solely for calibration purposes.  Follow these steps to perform calibration on a newly-constructed TriPoint module.

* First, make sure there are no calibration values set in calibration/tripoint_calibration.data for the nodes being calibrated (include the reference node)
* Next, place the reference node (currently c0:98:e5:50:50:44:50:1e) inside a faraday cage to eliminate over-the-air transmission of data.  Supply power via USB to the TriDev board inside the faraday cage
* To TriDev RF Port 2 (top), connect:
  * D/C block
  * 50 dB attenuation
  * 12-inch coax cable
  * Faraday cage RF port
* On the outside, connect:
  * 12-inch coax cable
  * D/C block
  * Port 2 on TriDev board holding unit-under-test
* Configure unit-under-test nRF51822 with application 'tritag'
* Configure reference node nRF51822 with application 'tritag-anchor'
* Connect saleae to monitor the following lines:
  * 0: MOSI
  * 1: MISO
  * 2: SCLK
  * 3: CSn
* Configure Saleae to maximum rate (50 MHz)
* Turn on both nodes
* Record Saleae trace for 100s
* Export Saleae trace to csv
* Run process_single_cal on newly-generated csv dump
* Add resulting calibration data to calibration/tripoint_calibration.data

