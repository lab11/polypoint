#!/usr/bin/env node

var request = require('request');
POST_URL = null
//POST_URL = "http://requestb.in/sxw295sx"

if (POST_URL == null) {
	console.log("ERROR: Edit file to supply POST_URL\n");
	process.exit(-1);
}

function get_meta (id) {
	return {
		received_time: new Date().toISOString(),
		device_id: id,
		device_type: "surepoint_tag",
	}
}

function get_value (beg, end) {
	return Math.random()*(end-beg+1)+beg
}

var x = 5;
var y = 5;

function generate_surepoint_packet (id) {
	x = x + get_value(-.5, .5);
	y = y + get_value(-.5, .5);
	z = z + get_value(-.2, .2);

	// Reasonable bounds
	if (x>20) x = 19.5;
	if (y>20) y = 19.5;
	if (x<0) x = .5;
	if (y<0) y = .5;
	if (z<0) z = .2;
	if (z>3) z = 2.8;

	return {
		_meta: get_meta(id),
		x: x,
		y: y,
		z: z,
	};
}

function publish (f, id) {
	var rand = get_value(0, 100)

	// Drop 10% of packets
	if (rand >= 10) {
		var pkt = f(id)

		console.log(pkt)
		var options = {
			uri: POST_URL,
			method: 'POST',
			json: pkt,
		};
		request(options);
	}
}

// Generate each packet at varying rates.
setInterval(function () {
	publish(generate_surepoint_packet, 'c0:98:e5:45:00:01')
}, 1000)
setInterval(function () {
	publish(generate_surepoint_packet, 'c0:98:e5:45:00:02')
}, 1000)
setInterval(function () {
	publish(generate_surepoint_packet, 'c0:98:e5:45:00:03')
}, 1000)
setInterval(function () {
	publish(generate_surepoint_packet, 'c0:98:e5:45:00:04')
}, 1000)

