var noble = require('noble');
var loc = require('./localization');
var buf = require('buffer');
var fs = require('fs');
var strftime = require('strftime');

var TRITAG_SERVICE_UUID          = 'd68c3152a23fee900c455231395e5d2e';
var TRITAG_CHAR_UUID_RAW         = 'd68c3153a23fee900c455231395e5d2e';
var TRITAG_CHAR_UUID_CALIB_INDEX = 'd68c3159a23fee900c455231395e5d2e';

var TRIPOINT_READ_INT_RANGES = 1

var assignment_index = 2;

var filename_start = strftime('tripoint_calibration_%Y-%m-%d_%H-%M-%S_', new Date());

function buf_to_eui (b, offset) {
	var eui = '';
	for (var i=0; i<8; i++) {
		var val = b.readUInt8(offset+i);
		var val = val.toString(16);
		if (val.length == 1) {
			val = '0' + val;
		}
		eui = val+eui;
		if (i<7) {
			eui = ':' + eui;
		}
	}
	return eui;
}

function encoded_mm_to_meters (b, offset) {
	var mm = b.readInt32LE(offset);
	return mm / 1000.0;
}

function record (b, fd) {
	var round = b.readUInt16LE(1);
	var t1 = (b.readUInt8(7) << 32) + b.readUInt32LE(3);
	var offset1 = b.readUInt32LE(8);
	var offset2 = b.readUInt32LE(12);
	var offset3 = b.readUInt32LE(16);
	var t2 = t1+offset1;
	var t3 = t2+offset2;
	var t4 = t3+offset3;

	fs.write(fd, round+'\t'+t1+'\t'+t2+'\t'+t3+'\t'+t4+'\n');

	return round;
}


function receive (peripheral, index, filename) {
	var filename = filename_start + peripheral.uuid.replace(':', '') + '_' + index + '.data';
	fs.open(filename, 'w', function (err, fd) {

		if (index == 0) {
			fs.writeSync(fd, 'Round\tATX0\tBRX0\tCRX0\tDRX0\n');
		} else if (index == 1) {
			fs.writeSync(fd, 'Round\tARX1\tBTX1\tCTX1\tDRX1\n');
		} else if (index == 2) {
			fs.writeSync(fd, 'Round\tARX2\tBRX2\tCRX2\tDTX2\n');
		}

		peripheral.connect(function (connect_err) {

			if (connect_err) {
				console.log('Error connecting to ' + peripheral.uuid);
				console.log(connect_err);
			} else {

				console.log('Connected to TriTag ' + peripheral.uuid);

				peripheral.discoverServices([TRITAG_SERVICE_UUID], function (service_err, services) {
					if (service_err) {
						console.log('Error finding services on ' + peripheral.uuid);
						console.log(service_err);
					} else {
						if (services.length == 1) {
							console.log('Found the TriTag service on ' + peripheral.uuid);

							services[0].discoverCharacteristics([], function (char_err, characteristics) {
								console.log('Found ' + characteristics.length + ' characteristics on ' + peripheral.uuid);

								characteristics.forEach(function (el, idx, arr) {
									var characteristic = el;


									if (characteristic.uuid == TRITAG_CHAR_UUID_RAW) {
										// Setup subscribe

										characteristic.on('data', function (dat) {

											if (dat.length == 20) {
												// console.log('got noitfy: ' + dat.length + ' from ' + peripheral.uuid);
												// console.log(dat);
												var round = record(dat, fd);
												console.log('Round ' + round + ' on ' + peripheral.uuid);
											}

										});

										characteristic.notify(true, function (notify_err) {
											if (notify_err) {
												console.log('error on notify setup ' + peripheral.uuid);
												console.log(notify_err);
											}
										});

									} else if (characteristic.uuid == TRITAG_CHAR_UUID_CALIB_INDEX) {
										// Setup the index number
										console.log('Setting ' + peripheral.uuid + ' to index ' + index);
										characteristic.write(new Buffer([index]), false, function (write_err) {
											if (write_err) {
												console.log('err on write index ' + peripheral.uuid);
												console.log(write_err);
											} else {
												console.log('Actually wrote index ' + index + ' to ' + peripheral.uuid);
											}
										});
									}
								});

							});

						} else {
							console.log('Somehow got two services back? That shouldn\'t happen.');
						}
					}
				});

			}


		});
	});
}


noble.on('stateChange', function (state) {
	if (state === 'poweredOn') {
		console.log('Scanning...');
		noble.startScanning();
	} else {
		console.log('Tried to start scanning, got: ' + state);
	}
});

noble.on('discover', function (peripheral) {
	if (peripheral.advertisement.localName == 'tritag') {
		console.log('Found TriTag: ' + peripheral.uuid);

		receive(peripheral, assignment_index--);
	}
});
