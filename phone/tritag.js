var noble = require('noble');
var loc = require('./localization');
var buf = require('buffer');

var TRITAG_SERVICE_UUID = 'd68c3152a23fee900c455231395e5d2e';

var TRIPOINT_READ_INT_RANGES = 1

var anchors = {
	one: [0,0,0],
	two: [4,0,0],
	thr: [3,7,0],
	fou: [1,3,0]
};

var test1 = {
	one: 4,
	two: 2.35,
	thr: 4.85,
	fou: 2.5
};

var actual = {
	test1: [3.32, 2.16, 0],
};

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

noble.on('stateChange', function (state) {
	if (state === 'poweredOn') {
		noble.startScanning([TRITAG_SERVICE_UUID], false);
	}
});


noble.on('discover', function (peripheral) {
	noble.stopScanning();

	console.log('Found TriTag: ' + peripheral.uuid);

	peripheral.connect(function (err) {
		console.log('Connected to TriTag');

		peripheral.discoverServices([TRITAG_SERVICE_UUID], function (err, services) {
			if (services.length == 1) {
				console.log('Found the TriTag service.');

				services[0].discoverCharacteristics([], function (err, characteristics) {
					console.log('Found ' + characteristics.length + ' characteristics');

					characteristics.forEach(function (el, idx, arr) {
						var characteristic = el;

						if (characteristic.uuid == 'd68c3153a23fee900c455231395e5d2e') {

							function get_range () {
								characteristic.read(function (err, data) {
									console.log(data);


									var reason = data.readUInt8(0);
									if (reason == TRIPOINT_READ_INT_RANGES) {
										// Got ranges from the TAG.
										var num_ranges = data.readUInt8(1);
										if (num_ranges == 0) {
											console.log('No anchors in range.');
										} else {
											// Iterate the array to get the
											// anchor IDs and ranges.
											var offset_start = 2;
											var instance_length = 12;
											for (var i=0; i<num_ranges; i++) {
												var start = offset_start + (i*instance_length);
												var eui = buf_to_eui(data, start);
												var range = encoded_mm_to_meters(data, start+8);
												console.log(eui);
												console.log(range);
											}
										}
									}
								});
							}

							get_range();
							setInterval(get_range, 1000);
						}
					});

				});

			}
		});


	});
});




function error (actual, estimate) {
	var x = actual[0] - estimate[0];
	var y = actual[1] - estimate[1];
	var z = actual[2] - estimate[2];
	var sum = x*x + y*y + z*z;
	return Math.sqrt(sum);
}






// var loc = calculate_location(test1, anchors);

// console.log(error(actual.test1, loc))



