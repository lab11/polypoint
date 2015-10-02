/* JavaScript for ESS Summon UI */

var deviceId = "C0:98:E5:30:DA:E4";                                                 // while testing, replace with address of a BLE peripheral
var deviceName = "BLE Device";                                                      // while testing, replace with desired name
var serviceUuid = "181A";                                                           // ESS UUID
var writeValue = "Written Name";                                                    // value to write to characteristic
var essdescriptorUuid = "290D";

var pressureUuid = "2A6D";                                                          // pressure characteristic UUID to read or write
var humidityUuid = "2A6F";                                                          // humidity characteristic UUID to read or write
var temperatureUuid = "2A6E";                                                       // temperature characteristic UUID to read or write
var luxUuid = "C512";                                                               // lux characteristic UUID to read or write
var accelerationUuid = "F801";                                                      // acceleration characteristic UUID to read or write

var ess_service = "181A";

var device_connected = false;
var timer;
var touchduration = 3000; //length of time we want the user to touch before we do something
var connection_toggle = false;
var is_init = false;

var app = {
    // Application Constructor
    initialize: function() {

        document.addEventListener("deviceready", app.onAppReady, false);
        document.addEventListener("resume", app.onAppReady, false);
        document.addEventListener("pause", app.onPause, false);

        bleesimg.addEventListener('touchend', app.onTouch, false);                // if bulb image touched, goto: onToggle
        bleesimg.addEventListener('touchstart', app.onStartTimer, false);         // if bulb image touched, goto: onToggle


        presimg.addEventListener('touchend', app.onTouchPres, false);             // if bulb image touched, goto: onToggle
        humimg.addEventListener('touchend', app.onTouchHum, false);             // if bulb image touched, goto: onToggle
        tempimg.addEventListener('touchend', app.onTouchTemp, false);             // if bulb image touched, goto: onToggle
        lightimg.addEventListener('touchend', app.onTouchLight, false);           // if bulb image touched, goto: onToggle
        accimg.addEventListener('touchend', app.onTouchAcc, false);             // if bulb image touched, goto: onToggle

        app.onAppReady();
    },
    onStartTimer: function(device){
       	connection_toggle = false;
        timer = setTimeout(app.onLongPress, touchduration); 
    },
    onLongPress: function(){
        app.log("timer expired");
        connection_toggle = true;
    },
    // App Ready Event Handler
    onAppReady: function() {
        //if (typeof window.gateway != "undefined") {                               // if UI opened through Summon,
            deviceId = window.gateway.getDeviceId();                                // get device ID from Summon
            deviceName = window.gateway.getDeviceName();                            // get device name from Summon
            app.log("Opened via Summon..");
        //}
        app.log("Opened via Summon...");
        document.getElementById("title").innerHTML = String(deviceId);
        app.log("Checking if ble is enabled...");
        ble.isEnabled(app.onEnable);                                                // if BLE enabled, goto: onEnable
        app.onEnable();
    },
    // App Paused Event Handler
    onPause: function() {
    	app.log("on Pause");                                                           // if user leaves app, stop BLE
        //ble.disconnect(deviceId);
        ble.stopScan();
        if (device_connected) {
	        app.log("Disconnecting from BLEES device!");
	        bluetoothle.disconnect(app.ondisconnectsuccess, app.onError, { "address": deviceId});                
	        ble.disconnect(deviceId);
	        bluetoothle.close(app.ondisconnectsuccess, app.onError, { "address": deviceId});                
	        device_connected = false;
    	}
    },
    // Bluetooth Enabled Callback
    onEnable: function() {
    	app.log("onEnable");
        app.onPause();                                                              // halt any previously running BLE processes
        ble.startScan([], app.onDiscover, app.onAppReady);                          // start BLE scan; if device discovered, goto: onDiscover
        app.log("Searching for " + deviceName + " (" + deviceId + ").");
    },
    // BLE Device Discovered Callback
    onDiscover: function(device) {
    	app.log("onDiscover");
        if (device.id == deviceId) {
            app.log("Found " + deviceName + " (" + deviceId + ")!");
            app.onParseAdvData(device);
            //app.onStartConnection(device);
        }
    },
    onStopScan: function(device) {
        app.log("stopped scanning");
    },
    onStartConnection: function(device) {
    	if (!is_init){
        	bluetoothle.initialize(app.onInitialized, app.onError, {"request": false, "statusReceiver": false});    //initialize plugin
        	is_init = true;
        }
        bluetoothle.connect(app.onConnectOther, app.onError, { "address": deviceId });                          //connect to peripheral- need this for descriptors
        ble.connect(deviceId, app.onConnect, app.onAppReady);                                                   // if device matches, connect; if connected, goto: onConnect
    },
    onInitialized: function(device) {
        app.log("Initialized..");
    },
    onConnectOther: function(device) {
        app.log("Connecting..");
    },
    // BLE Device Connected Callback
    onConnect: function(device) {
        app.log("Connected to " + deviceName + " (" + deviceId + ")!");
        device_connected = true;
        app.onReadAllSensors(device);
    },
    onTouch: function(device) {
        //ble.startScan([], app.onDiscover, app.onAppReady);                          // start BLE scan; if device discovered, goto: onDiscover
        
        clearTimeout(timer);
        if (device_connected){
            if (connection_toggle){
            	app.onPause();
            	app.onAppReady();
            }
            else { 
                app.onReadAllSensors(device);
            }
        }
        else {
            if (connection_toggle) {
                app.log("Connecting to BLEES device!");
                app.onStartConnection(device);
            }
            else {
                app.log("Scanning...");
                ble.startScan([], app.onDiscover, app.onAppReady);                          // start BLE scan; if device discovered, goto: onDiscover
            }
        }
        connection_toggle = false;

    },
    onTouchEyePres: function() {
    	if(device_connected){
    		bluetoothle.discover(app.onDiscoverDescriptors(pressureUuid), app.onError, {"address": deviceId} );
    	}
    	else{
    		app.onPleaseConnect();
    	}
    },
    onTouchEyeHum: function() {    	
    	if(device_connected){
    		bluetoothle.discover(app.onDiscoverDescriptors(humidityUuid), app.onError, {"address": deviceId} );
    	}
    	else{
    		app.onPleaseConnect();
    	}
    },
    onTouchEyeTemp: function() {
    	if(device_connected){
			bluetoothle.discover(app.onDiscoverDescriptors(temperatureUuid), app.onError, {"address": deviceId} );
		}
    	else{
    		app.onPleaseConnect();
    	}
    },
    onTouchEyeLux: function() {    	
    	if(device_connected){
			bluetoothle.discover(app.onDiscoverDescriptors(luxUuid), app.onError, {"address": deviceId} );
		}
    	else{
    		app.onPleaseConnect();
    	}
    },
    onTouchEyeAcc: function() {
    	if(device_connected){
    		bluetoothle.discover(app.onDiscoverDescriptors(accelerationUuid), app.onError, {"address": deviceId} );
    	}
    	else{
    		app.onPleaseConnect();
    	}
    },
    onDiscoverDescriptors: function(char_Uuid) {

        bluetoothle.readDescriptor(app.readSuccess, app.onError, {
              "address": deviceId,
              "serviceUuid": serviceUuid,
              "characteristicUuid": char_Uuid,
              "descriptorUuid": essdescriptorUuid
        });

    },
    readSuccess: function(device){
        app.log("Reading descriptor...");
        var bytesArray = new Uint8Array(5);
        bytesArray[0] = (bluetoothle.encodedStringToBytes(device.value))[0] ;
        bytesArray[1] = (bluetoothle.encodedStringToBytes(device.value))[1] ;
        bytesArray[2] = (bluetoothle.encodedStringToBytes(device.value))[2] ;
        bytesArray[3] = (bluetoothle.encodedStringToBytes(device.value))[3] ;
        bytesArray[4] = (bluetoothle.encodedStringToBytes(device.value))[4] ;

        var output_string;
        //Trigger condition types
        if (bytesArray[0] == 0x00) output_string = "Trigger Inactive";
        else if (bytesArray[0] == 0x03) output_string = "Trigger Value change";

        else{
	        if (bytesArray[0] == 0x01) output_string = "Trigger Fixed interval: ";
	        else if (bytesArray[0] == 0x02) output_string = "Trigger No less than: ";
	        else if (bytesArray[0] == 0x04) output_string = "Trigger While less than: ";
	        else if (bytesArray[0] == 0x05) output_string = "Trigger While less than or equal to: ";
	        else if (bytesArray[0] == 0x06) output_string = "Trigger While greater than: ";
	        else if (bytesArray[0] == 0x07) output_string = "Trigger While greater than or equal to: ";
	        else if (bytesArray[0] == 0x08) output_string = "Trigger While equal to: ";
	        else if (bytesArray[0] == 0x09) output_string = "Trigger While not equal to: ";
	        else output_string = "error";

	        var operand = 0;
	        for(var i = 1; (i < 5) && bytesArray[i]; i++){
	        	operand = operand | (bytesArray[i] << ( (i-1) * 8));
	        }

	        //Uuids need to be lowercase for some reason
	        if(device.characteristicUuid == "2a6d"){ // pressureUuid
	        	if(bytesArray[0] < 0x04){
	        		output_string += operand + " ms";
	        	}
				else {
					output_string += (operand/10) + " Pa";
				}
			}
			
			else if(device.characteristicUuid == "2a6f"){ //humidity Uuid
				if(bytesArray[0] < 0x04){
	        		output_string += operand + " ms";
	        	}
				else {
					output_string += (operand/100) + String.fromCharCode(37);
				}
			}

			else if(device.characteristicUuid == "2a6e"){ //temperature Uuid
				if(bytesArray[0] < 0x04){
	        		output_string += operand + " ms";
	        	}
				else {
					output_string += (operand/100) + " " + String.fromCharCode(176) + "C";
				}
			}

			else if(device.characteristicUuid == "c512"){ //luxUuid
				if(bytesArray[0] < 0x04){
	        		output_string += operand + " ms";
	        	}
				else {
					output_string += (operand) + " " + String.fromCharCode(176) + " lux";
				}
			}

			else if(device.characteristicUuid == "f801"){ //accelerationUuid
				if(bytesArray[0] < 0x04){
	        		output_string += operand + " ms";
	        	}
				else {
					output_string += "Need to implement this!";
				}
			}
			
    	}

    	app.log(output_string);

    },
    ondisconnectsuccess: function() {
        app.log("disconnected success!");
    },
    onPleaseConnect: function(){
		app.log("Please connect to use this feature");
    },
    onTouchPres: function(device) {
        if(device_connected){
            app.log("Getting pressure...");
            ble.read(deviceId, serviceUuid, pressureUuid, app.onReadPres, app.onError);
        } 
        else{
            app.onPleaseConnect();
        }
    },
    onTouchHum: function(device) {
        if(device_connected){
            app.log("Getting humidity...");
            ble.read(deviceId, serviceUuid, humidityUuid, app.onReadHum, app.onError);
        }
        else{
            app.onPleaseConnect;
        }
    },
    onTouchTemp: function(device) {
        if(device_connected){
            app.log("Getting temperature...");
            ble.read(deviceId, serviceUuid, temperatureUuid, app.onReadTemp, app.onError);
        }
        else{
            app.onPleaseConnect();
        }
    },
    onTouchLight: function(device) {
        if(device_connected){
            app.log("Getting lux...");
            ble.read(deviceId, serviceUuid, luxUuid, app.onReadLux, app.onError);  
        }
        else{
            app.onPleaseConnect();
        }
    },
    onTouchAcc: function(device) {
        if(device_connected){
            app.log("Getting acceleration...");
            ble.read(deviceId, serviceUuid, accelerationUuid, app.onReadAcc, app.onError);  
        }
        else{
            app.onPleaseConnect;
        }
    },
    onTouchPencilPres: function() {
    	if(device_connected){
    		document.querySelector("#popuppres").style.display = "block";
    	}
    	else{
    		app.onPleaseConnect();
    	}
    },
    onTouchPencilHum: function() {
    	if(device_connected){
    		document.querySelector("#popuphum").style.display = "block";
    	}
    	else{
    		app.onPleaseConnect();
    	}
    },
    onTouchPencilTemp: function() {
    	if(device_connected){
    		document.querySelector("#popuptemp").style.display = "block";
    	}
    	else{
    		app.onPleaseConnect();
    	}
    },
    onTouchPencilLux: function() {
    	if(device_connected){
       		document.querySelector("#popup").style.display = "block";
        }
    	else{
    		app.onPleaseConnect();
    	}
    },
    onTouchPencilAcc: function() {
    	if(device_connected){
        	document.querySelector("#popupacc").style.display = "block";
        }
    	else{
    		app.onPleaseConnect();
    	}
    },
    accelerationcallback: function(buttonIndex) {
		document.querySelector("#popupacc").style.display = "none";
        app.log('button index clicked: ' + buttonIndex);
        if (buttonIndex == 1){ // buttonIndex is 1-based
            app.log("got one");
			var bytes = new Uint8Array(1);
		    bytes[0] = 0;
		    var value = bluetoothle.bytesToEncodedString(bytes);
			bluetoothle.writeDescriptor(app.writeSuccess, app.onError, {
			    "address": deviceId,
			    "serviceUuid": serviceUuid,
			    "characteristicUuid": accelerationUuid,
			    "descriptorUuid": essdescriptorUuid,
			    "value": value
			});
        }
        else{
            var input_val = window.prompt("What value?", "0x0000");
            var output_val = parseInt(input_val, 10);
            if (input_val) {
                app.log("gotit");
            }
        }
    },
    prescallback: function(buttonIndex) {
		document.querySelector("#popuppres").style.display = "none";
        app.log('button index clicked: ' + buttonIndex);
        if (buttonIndex == 1){ // buttonIndex is 1-based
            app.log("got one");
			var bytes = new Uint8Array(1);
		    bytes[0] = 0;
		    var value = bluetoothle.bytesToEncodedString(bytes);
			bluetoothle.writeDescriptor(app.writeSuccess, app.onError, {
			    "address": deviceId,
			    "serviceUuid": serviceUuid,
			    "characteristicUuid": pressureUuid,
			    "descriptorUuid": essdescriptorUuid,
			    "value": value
			});
        }
        else{
            var input_val = window.prompt("What value?", 0x00);
            var output_val = parseInt( input_val * 100 , 10);
            output_val = output_val / 10;
            var bytes = new Uint8Array(5);
		    bytes[0] = buttonIndex-1;
            if (input_val) {
                //app.log( output_val.toString(16) );
               	bytes[1] = (output_val & 0x000000ff);
               	bytes[2] = (output_val & 0x0000ff00) >> 8;
               	bytes[3] = (output_val & 0x00ff0000) >> 16;
               	bytes[4] = (output_val & 0xff000000) >> 24;
               	var value = bluetoothle.bytesToEncodedString(bytes);
               	bluetoothle.writeDescriptor(app.writeSuccess, app.onError, {
				    "address": deviceId,
				    "serviceUuid": serviceUuid,
				    "characteristicUuid": pressureUuid,
				    "descriptorUuid": essdescriptorUuid,
				    "value": value
				});
            }
        }
    },
    tempcallback: function(buttonIndex) {
		document.querySelector("#popuptemp").style.display = "none";
        app.log('button index clicked: ' + buttonIndex);
        if (buttonIndex == 1){ // buttonIndex is 1-based
            app.log("got one");
			var bytes = new Uint8Array(1);
		    bytes[0] = 0;
		    var value = bluetoothle.bytesToEncodedString(bytes);
			bluetoothle.writeDescriptor(app.writeSuccess, app.onError, {
			    "address": deviceId,
			    "serviceUuid": serviceUuid,
			    "characteristicUuid": temperatureUuid,
			    "descriptorUuid": essdescriptorUuid,
			    "value": value
			});
        }
        else{
            var input_val = window.prompt("What value?", 0x00);
            var output_val = parseInt( input_val * 1000 , 10);
            output_val = output_val / 10;
            var bytes = new Uint8Array(3);
		    bytes[0] = buttonIndex-1;
            if (input_val) {
                //app.log( output_val.toString(16) );
               	bytes[1] = (output_val & 0x000000ff);
               	bytes[2] = (output_val & 0x0000ff00) >> 8;
               	var value = bluetoothle.bytesToEncodedString(bytes);
               	bluetoothle.writeDescriptor(app.writeSuccess, app.onError, {
				    "address": deviceId,
				    "serviceUuid": serviceUuid,
				    "characteristicUuid": temperatureUuid,
				    "descriptorUuid": essdescriptorUuid,
				    "value": value
				});
            }
        }
    },
    humcallback: function(buttonIndex) {
		document.querySelector("#popuphum").style.display = "none";
        app.log('button index clicked: ' + buttonIndex);
        if (buttonIndex == 1){ // buttonIndex is 1-based
            app.log("got one");
			var bytes = new Uint8Array(1);
		    bytes[0] = 0;
		    var value = bluetoothle.bytesToEncodedString(bytes);
			bluetoothle.writeDescriptor(app.writeSuccess, app.onError, {
			    "address": deviceId,
			    "serviceUuid": serviceUuid,
			    "characteristicUuid": humidityUuid,
			    "descriptorUuid": essdescriptorUuid,
			    "value": value
			});
        }
        else{
            var input_val = window.prompt("What value?", 0x00);
            var output_val = parseInt( input_val * 1000 , 10);
            output_val = output_val / 10;
            var bytes = new Uint8Array(3);
		    bytes[0] = buttonIndex-1;
            if (input_val) {
                app.log( output_val.toString(16) );
               	bytes[1] = (output_val & 0x000000ff);
               	bytes[2] = (output_val & 0x0000ff00) >> 8;
               	var value = bluetoothle.bytesToEncodedString(bytes);
               	bluetoothle.writeDescriptor(app.writeSuccess, app.onError, {
				    "address": deviceId,
				    "serviceUuid": serviceUuid,
				    "characteristicUuid": humidityUuid,
				    "descriptorUuid": essdescriptorUuid,
				    "value": value
				});
            }
        }
    },
    luxcallback: function(buttonIndex) {
    	document.querySelector("#popup").style.display = "none";
        app.log('button index clicked: ' + buttonIndex);
        if (buttonIndex == 1){ // buttonIndex is 1-based
            app.log("got one");
			var bytes = new Uint8Array(1);
		    bytes[0] = 0;
		    var value = bluetoothle.bytesToEncodedString(bytes);
			app.log("about to write to descriptor");
			bluetoothle.writeDescriptor(app.writeSuccess, app.onError, {
			    "address": deviceId,
			    "serviceUuid": serviceUuid,
			    "characteristicUuid": luxUuid,
			    "descriptorUuid": essdescriptorUuid,
			    "value": value
			});
        }
        else{
            var input_val = window.prompt("What value?", 0x00);
            var output_val = parseInt( input_val , 10);
            var bytes = new Uint8Array(3);
		    bytes[0] = buttonIndex-1;
            if (input_val) {
                app.log( output_val.toString(16) );
               	bytes[1] = (output_val & 0x000000ff);
               	bytes[2] = (output_val & 0x0000ff00) >> 8;
               	var value = bluetoothle.bytesToEncodedString(bytes);
               	bluetoothle.writeDescriptor(app.writeSuccess, app.onError, {
				    "address": deviceId,
				    "serviceUuid": serviceUuid,
				    "characteristicUuid": luxUuid,
				    "descriptorUuid": essdescriptorUuid,
				    "value": value
				});
            }
        }
    },
    writeSuccess: function() {
    	app.log("device written");
    },
    onParseAdvData: function(device){
        //Parse Advertised Data
        var adData = new Uint8Array(device.advertising);

		if ((adData[12] != 0x1A) || (adData[13] != 0x18)){
			app.log("not right");
			app.onEnable();
			return;
		}

		else{
			ble.stopScan(app.onStopScan, app.onError);
		}

        app.log("Parsing advertised data...");

       	var pressureOut = (( (adData[17] * 16777216) + (adData[16] * 65536 ) + (adData[15] * 256) + adData[14] )/10) + " Pa";
        app.log( "Pressure: " + pressureOut);
        document.getElementById("presVal").innerHTML = String(pressureOut);

        var humidityOut = (( (adData[19] * 256) + adData[18] )/100) + String.fromCharCode(37);
        app.log( "Humidity: " + humidityOut);
        document.getElementById("humVal").innerHTML = String(humidityOut);

        var temperatureOut = (( (adData[21] * 256) + adData[20])/100) + " " + String.fromCharCode(176) + "C";
        app.log( "Temperature: " + temperatureOut);
        document.getElementById("tempVal").innerHTML = String(temperatureOut);

        var luxOut = ( (adData[23] * 256) + adData[22]) + " lux" ;
        app.log( "Lux: " + luxOut);
        document.getElementById("luxVal").innerHTML = String(luxOut);

        var accdata = adData[24];
        app.log("Immediate Acceleration: " + ((accdata & 17) >> 4) );
        app.log("Interval Acceleration: " + (accdata & 1) );

    },
    onReadAllSensors: function(device) {
        app.log("Getting sensor data...");
        ble.read(deviceId, serviceUuid, pressureUuid, app.onReadPres, app.onError);
        ble.read(deviceId, serviceUuid, humidityUuid, app.onReadHum, app.onError);  
        ble.read(deviceId, serviceUuid, temperatureUuid, app.onReadTemp, app.onError); 
        ble.read(deviceId, serviceUuid, luxUuid, app.onReadLux, app.onError);  
        ble.read(deviceId, serviceUuid, accelerationUuid, app.onReadAcc, app.onError);
    },
    // BLE Characteristic Read Callback
    onReadPres: function(data) {
        //app.log("where is pressure");
        var pressureOut = (app.buffToUInt32Decimal(data))/10 + " Pa";                 // display read value as string
        app.log("Pressure: " + pressureOut);
        document.getElementById("presVal").innerHTML = String(pressureOut);
    },
    // BLE Characteristic Read Callback
    onReadHum: function(data) {
        var humidityOut = (app.buffToUInt16Decimal(data))/100 + String.fromCharCode(37);                 // display read value as string
        app.log("Humidity: " + humidityOut);
		document.getElementById("humVal").innerHTML = String(humidityOut);
    },
    // BLE Characteristic Read Callback
    onReadTemp: function(data) {
        var temperatureOut = (app.buffToInt16Decimal(data))/100 + " " + String.fromCharCode(176) + "C";                 // display read value as string
        app.log("Temperature: " + temperatureOut);
		document.getElementById("tempVal").innerHTML = String(temperatureOut);
    },
    // BLE Characteristic Read Callback
    onReadLux: function(data) {
        var luxOut = app.buffToInt16Decimal(data) + " lux";                 // display read value as string
        app.log("Lux: " + luxOut);
		document.getElementById("luxVal").innerHTML = String(luxOut);
    },
    // BLE Characteristic Read Callback
    onReadAcc: function(data) {
        var acc = app.buffToUInt8Decimal(data);
        app.log("Immediate Acceleration: " + ((acc & 17) >> 4) );                 // display read value as string
        app.log("Interval Acceleration: " + (acc & 1) );                 // display read value as string

    },
    // BLE Characteristic Write Callback
    onWrite : function() {
        app.log("Characeristic Written: " + writeValue);                            // display write success
    },
    // BLE Characteristic Read/Write Error Callback
    onError: function() {                                                           // on error, try restarting BLE
        app.log("Read/Write Error.")
        ble.isEnabled(deviceId,function(){},app.onAppReady);
        ble.isConnected(deviceId,function(){},app.onAppReady);
    },
    // Function to Convert String to Bytes (to Write Characteristics)
    stringToBytes: function(string) {
        array = new Uint8Array(string.length);
        for (i = 0, l = string.length; i < l; i++) array[i] = string.charCodeAt(i);
        return array.buffer;
    },
    buffToUInt32Decimal: function(buffer) {
        //app.log("to32");
        var uint32View = new Uint32Array(buffer);
        return uint32View[0];
    },
    buffToUInt16Decimal: function(buffer) {
        var uint16View = new Uint16Array(buffer);
        return uint16View[0];
    },
    buffToInt16Decimal: function(buffer) {
        var int16View = new Int16Array(buffer);
        return int16View[0];
    },
    buffToUInt8Decimal: function(buffer) {
        var uint8View = new Uint8Array(buffer);
        return uint8View[0];
    },
    // Function to Convert Bytes to String (to Read Characteristics)
    bytesToString: function(buffer) {
        return String.fromCharCode.apply(null, new Uint8Array(buffer));
    },
    /*onTouchEyePres: function() {
    	var console = document.getElementById("console");
    	if (console.style.visibility == "hidden"){
    		console.style.visibility = "visible";
    	} 
    	else {
    		console.style.visibility = "hidden";
    	}
    },
    */
    // Function to Log Text to Screen
    log: function(string) {
        document.querySelector("#console").innerHTML += (new Date()).toLocaleTimeString() + " : " + string + "<br />"; 
        document.querySelector("#console").scrollTop = document.querySelector("#console").scrollHeight;
    }
};

app.initialize();