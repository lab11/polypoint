var noble = require('noble');
var loc = require('./localization');

TRITAG_SERVICE_UUID = 'd68c3152a23fee900c455231395e5d2e';

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



