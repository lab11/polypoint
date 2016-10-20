BLE Apps
========

This folder contains code for connecting to the BLE services provided
by the hardware.


tritag-summon
-------------

App for summon on mobile phones. See the readme in the folder.


tritag.js
---------

This is a script that runs with Node.js to get range estimates
from the TriTag device.

To use:

1. Install [Node.js](https://nodejs.org/en/download/)
2. Install noble:

        npm install noble

3. Run the script

        sudo node ./tritag.js

--------

The app can also be configured to post locations to a RESTful endpoint by providing a URL.
The `generate_test_data.js` provides simulation data for testing.

The packet format is:
```json
{
    "_meta": {
        "received_time":"2016-10-13T00:21:28.647Z",
        "device_id":"c0:98:e5:45:00:01",
        "device_type":"surepoint_tag"
        },
    "x":6.825641556121154,
    "y":7.267745259210209,
    "z":1.818158319382718
}
```


