// Saved properties of the device that beaconed this application.
var device_id = '';
var device_name = '';

// Known constants for TriTag
var uuid_service_tritag = 'd68c3152-a23f-ee90-0c45-5231395e5d2e';
var uuid_tritag_char_raw = 'd68c3153-a23f-ee90-0c45-5231395e5d2e';
var uuid_tritag_char_startstop = 'd68c3154-a23f-ee90-0c45-5231395e5d2e';
var uuid_tritag_char_calibration = 'd68c3159-a23f-ee90-0c45-5231395e5d2e';

// Interrupt reasons
var TRIPOINT_READ_INT_RANGES = 1
var TRIPOINT_READ_INT_CALIBRATION = 2

// Application state
var _anchor_locations = {
    'c0:98:e5:50:50:44:50:1d': [0, 2.752, 2.874],
    'c0:98:e5:50:50:44:50:0f': [0.952, 0, 2.621],
    'c0:98:e5:50:50:44:50:19': [3.249, 0, 2.586],
    'c0:98:e5:50:50:44:50:18': [5.142, 0.158, 2.596],
    'c0:98:e5:50:50:44:50:0a': [5.174, 2.177, 2.557],
    'c0:98:e5:50:50:44:50:1a': [5.174, 4.073, 2.510],
    'c0:98:e5:50:50:44:50:17': [5.174, 7.625, 2.461],
    'c0:98:e5:50:50:44:50:13': [5.174, 10.399, 2.632],
    'c0:98:e5:50:50:44:50:10': [4.926, 14.506, 2.628],
    'c0:98:e5:50:50:44:50:0d': [2.849, 14.506, 2.558],
    'c0:98:e5:50:50:44:50:05': [0.564, 14.506, 2.542],
    'c0:98:e5:50:50:44:50:0b': [0, 13.484, 2.793],
    'c0:98:e5:50:50:44:50:06': [0, 11.671, 2.879],
    'c0:98:e5:50:50:44:50:09': [0, 8.970, 2.779],
    'c0:98:e5:50:50:44:50:14': [0, 5.424, 2.703]
};

var switch_visibility_console_check = "visible";
var switch_visibility_steadyscan_check = "visible";
var steadyscan_on = true;

/******************************************************************************/
// GDP INTEGRATION
/******************************************************************************/

var GDP_REST_ENDPOINT = "http://requestb.in/p0k5jup0"

function get_meta (id) {
    return {
        received_time: new Date().toISOString(),
        device_id: id,
        device_type: "surepoint_tag",
    }
}

function generate_surepoint_packet (id, x, y, z) {
    return {
        _meta: get_meta(id),
        x: x,
        y: y,
        z: z,
    };
}

function post_to_gdp (x, y, z) {
    pkt = generate_surepoint_packet(device_id, x, y, z);
    $.ajax(GDP_REST_ENDPOINT, {
        data: JSON.stringify(pkt),
        contentType: 'application/json',
        type: 'POST',
        success: function () {
            //console.log("GDP POST success");
        },
        error: function (ajaxContext) {
            //console.log("GDP POST fail: " + ajaxContext.responseText);
        },
    })
}

/******************************************************************************/
// LOCATION CALCULATION FUNCTIONS
/******************************************************************************/

// Convert a map of {anchor_id: range_in_meters} to a 3D location.
// Anchor locations is a map of {anchor_id: [X, Y, Z]}.
function calculate_location (anchor_ranges, anchor_locations) {

    // First need two arrays, one of the ranges to the anchors,
    // and one of the anchor locations.
    ranges = [];
    locations = [];
    for (var anchor_id in anchor_ranges) {
        if (anchor_ranges.hasOwnProperty(anchor_id)) {
            ranges.push(anchor_ranges[anchor_id]);
            locations.push(anchor_locations[anchor_id]);
        }
    }

    // Now that we have those data structures setup, we can run
    // the optimization function that calculates the most likely
    // position.
    var location_optimize_with_args = function (start) {
        return location_optimize(start, ranges, locations);
    };

    var best = numeric.uncmin(location_optimize_with_args, [0,0,0]);
    return best.solution;
}

function location_optimize (tag_position, anchor_ranges, anchor_locations) {
    // First create an array that is the same length as the number of
    // ranges that contains the initial tag_position duplicated.
    var start = [];
    for (var i=0; i<anchor_ranges.length; i++) {
        start.push(tag_position);
    }

    // Do a distance calculate between tag and anchor known locations
    var next = [];
    var out = [];
    for (var i=0; i<anchor_ranges.length; i++) {
        next.push([]);
        // Subtract
        next[i][0] = start[i][0] - anchor_locations[i][0];
        next[i][1] = start[i][1] - anchor_locations[i][1];
        next[i][2] = start[i][2] - anchor_locations[i][2];

        // ^2
        next[i][0] = next[i][0] * next[i][0];
        next[i][1] = next[i][1] * next[i][1];
        next[i][2] = next[i][2] * next[i][2];

        // sum and sqrt
        var dist = Math.sqrt(next[i][0] + next[i][1] + next[i][2]);

        // Now subtract from that the range to that anchor, and square that
        var sub = dist - anchor_ranges[i];
        out.push(sub*sub);
    }

    // Sum the range comparisons and return that value
    var end = out.reduce(function(a, b){return a+b;});

    // console.log(end);
    return end;
}


/******************************************************************************/
// DATA PARSING
/******************************************************************************/

function buf_to_eui (dv, offset) {
    var eui = '';
    for (var i=0; i<8; i++) {
        var val = dv.getUint8(offset+i);
        val = val.toString(16);
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

function encoded_mm_to_meters (dv, offset) {
    // Read the value as little-endian
    var mm = dv.getInt32(offset, true);
    return mm / 1000.0;
}


/******************************************************************************/
// HANDLE BUFFER FROM TRIPOINT/PHONE
/******************************************************************************/

function process_raw_buffer (buf) {
    var dv = new DataView(buf, 0);

    // The first byte is the reason byte. This tells us what the TriPoint
    // is sending back to us.
    var reason_byte = dv.getUint8(0);

    // Process the buffer correctly
    if (reason_byte == TRIPOINT_READ_INT_RANGES) {
        // Got range data from TriPoint
        // How many?
        var num_ranges = dv.getUint8(1);
        if (num_ranges == 0) {
            app.log('Got range, 0 anchors');
            app.update_location('Didn\'t get any ranges.');
        } else {
            app.log('Got ' + num_ranges + ' ranges.');
            var offset_start = 2;
            var instance_length = 12;
            var ranges = {};
            var num_valid_ranges = 0;
            for (var i=0; i<num_ranges; i++) {
                var start = offset_start + (i*instance_length);
                var eui = buf_to_eui(dv, start);
                var range = encoded_mm_to_meters(dv, start+8);

                // Strip out ranges that are error codes
                if (range > -1000) {
                    ranges[eui] = range;
                    num_valid_ranges++;
                }

                app.log('  ' + eui + ': ' + range);
            }

            // Got ranges
            // app.log(JSON.stringify(ranges));

            // Calculate a location
            if (num_valid_ranges >= 3) {
                var loc = calculate_location(ranges, _anchor_locations);
                var update =  'X: ' + loc[0].toFixed(2) + ';';
                    update += ' Y: ' + loc[1].toFixed(2) + ';';
                    update += ' Z: ' + loc[2].toFixed(2);
                app.update_location(update);
            }

            // Post location update to GDP
            post_to_gdp(loc[0], loc[1], loc[2]);
        }
    } else {
        app.log('Got different reason byte: ' + reason_byte);
    }

}



var app = {
    // Application Constructor
    initialize: function () {
        app.log('Initializing application');

        document.addEventListener("deviceready", app.onAppReady, false);
        document.addEventListener("resume", app.onAppReady, false);
        document.addEventListener("pause", app.onAppPause, false);
    },

    // App Ready Event Handler
    onAppReady: function () {
        // Check if this is being opened in Summon
        if (typeof window.gateway != "undefined") {
            app.log("Opened via Summon..");
            // If so, we can query for some parameters specific to this
            // instance.
            device_id = window.gateway.getDeviceId();
            device_name = window.gateway.getDeviceName();

            // Save it to the UI
            document.getElementById('device-id-span').innerHTML = String(device_id);

            ble.isEnabled(app.bleEnabled, app.bleDisabled);

        } else {
            // Not opened in Summon. Don't know what to do with this.
            app.log('Not in summon.');
        }
    },
    // App Paused Event Handler
    onAppPause: function () {
        console.log('DISCONNNNENECCCT');
        ble.disconnect(device_id, app.bleDisconnect, app.bleDisconnectError);
    },

    // Callbacks to make sure that the phone has BLE ENABLED.
    bleEnabled: function () {
        app.log('BLE is enabled.');

        // Need to scan in order to ensure that megster BLE library
        // has the peripheral in its cache
        ble.startScan([], app.bleDeviceFound, app.bleScanError);
    },
    bleDisabled: function () {
        app.log('BLE disabled. Boo.');
    },

    // Callbacks for SCANNING
    bleDeviceFound: function (dev) {
        // Scan found a device, check if its the one we are looking for
        if (dev.id == device_id) {
            // Found our device, stop the scan, and try to connect
            ble.stopScan();

            app.log('Trying to connect to the proper TriTag.');
            ble.connect(device_id, app.bleDeviceConnected, app.bleDeviceConnectionError);
        }
    },
    bleScanError: function (err) {
        app.log('Scanning error.');
    },

    // Callbacks for CONNECT
    bleDeviceConnected: function (device) {
        app.log('Successfully connected to TriTag');
        ble.startNotification(device_id, uuid_service_tritag, uuid_tritag_char_raw,
          app.bleRawBufferNotify, app.bleRawBufferNotifyError);
    },
    bleDeviceConnectionError: function (err) {
        app.log('Error connecting to TriTag: ' + err);

        // Check the error to determine if we should try to connect again,
        // or we need to re-scan for the device.
        if (err == "Peripheral " + device_id + " not found.") {
            // Cannot just reconnect, must rescan
            app.bleEnabled();
        } else {
            app.log('TriTag reconnecting try');
            ble.connect(device_id, app.bleDeviceConnected, app.bleDeviceConnectionError);
        }
    },

    // Callbacks for NOTIFY
    bleRawBufferNotify: function (data) {
        // Read to get the rest of the buffer
        ble.read(device_id, uuid_service_tritag, uuid_tritag_char_raw,
          app.bleRawBufferRead, app.bleRawBufferReadError);
    },
    bleRawBufferNotifyError: function (err) {
        app.log('Notify raw buffer error.');
    },

    // Callbacks for READ
    bleRawBufferRead: function (data) {
        process_raw_buffer(data);
    },
    bleRawBufferReadError: function (err) {
        app.log('Read raw buf error');
    },

    // Callbacks for DISCONNECT
    bleDisconnect: function () {
        console.log('Successfully disconnected');
        app.log('Disconnected from TriTag.');
    },
    bleDisconnectError: function (err) {
        console.log('Error disconnecting.');
        console.log(err);
        app.log('Error disconnecting from TriTag');
    },

    update_location: function (str) {
        document.querySelector("#location").innerHTML = str;

    },
    // Function to Log Text to Screen
    log: function(string) {
    	document.querySelector("#console").innerHTML += (new Date()).toLocaleTimeString() + " : " + string + "<br />";
        document.querySelector("#console").scrollTop = document.querySelector("#console").scrollHeight;
    }
};

app.initialize();
