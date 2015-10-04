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


var switch_visibility_console_check = "visible";
var switch_visibility_steadyscan_check = "visible";
var steadyscan_on = true;


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
            var offset_start = 2;
            var instance_length = 12;
            var ranges = {};
            for (var i=0; i<num_ranges; i++) {
                var start = offset_start + (i*instance_length);
                var eui = buf_to_eui(dv, start);
                var range = encoded_mm_to_meters(dv, start+8);
                ranges[eui] = range;
            }

            // Got ranges
            app.log(JSON.stringify(ranges));
        }
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
        ble.disconnect(device_id);
    },

    // Callbacks to make sure that the phone has BLE enabled.
    bleEnabled: function () {
        app.log('BLE is enabled.');

        app.log('Trying to connect to the proper TriTag.');
        ble.connect(device_id, app.bleDeviceConnected, app.bleDeviceConnectionError);
    },
    bleDisabled: function () {
        app.log('BLE disabled. Boo.');
    },

    bleDeviceConnected: function (device) {
        app.log('Successfully connected to TriTag');

        console.log(JSON.stringify(device));

        ble.startNotification(device_id, uuid_service_tritag, uuid_tritag_char_raw,
          app.bleRawBufferNotify, app.bleRawBufferNotifyError);
    },

    bleDeviceConnectionError: function (err) {
        app.log('Error connecting to TriTag');
        app.log('TriTag reconnecting try');

        ble.connect(device_id, app.bleDeviceConnected, app.bleDeviceConnectionError);
    },

    bleRawBufferNotify: function (data) {
        app.log('got notify data');
        process_raw_buffer(data);
    },

    bleRawBufferNotifyError: function (err) {
        app.log('Notify raw buffer error.');
    },

    update_location: function (str) {
      // $('#location').text(str);
    document.querySelector("#location").innerHTML = str;

    },
    // Function to Log Text to Screen
    log: function(string) {
    	document.querySelector("#console").innerHTML += (new Date()).toLocaleTimeString() + " : " + string + "<br />";
        document.querySelector("#console").scrollTop = document.querySelector("#console").scrollHeight;
    }
};

app.initialize();