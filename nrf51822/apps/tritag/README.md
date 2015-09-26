TriTag BLE App
==============

This apps provides the BLE interface for the TriTag localization tag.

Programming
-----------

    SERGGER_SERIAL=xxxxxxxxx make flash ID=c0:98:e5:45:00:01

Advertisement
-------------

TriTag advertises according to the [Eddystone](https://github.com/google/eddystone)
protocol. It works with our [Summon](https://github.com/lab11/summon) project
that provides a browser-based UI for BLE devices.


Services
--------

TriTag provides a service for all ranging operations. Characteristics in that service
provide control and data for the interface with TriPoint.

- **Ranging Service**: UUID: `2e5d5e39-3152-450c-90ee-3fa29c868cd6`
  - **Raw Data Characteristic**: Short UUID: `3153`. Provides direct access to the data published from the
  TriPoint when the TriPoint interrupts the host. See `API.md` for a description of
  the possible data returned.
  - **Ranging Start/Stop Characteristic**: Short UUID: `3154`. Write a 0 to this to stop the ranging
  operation. Write a 1 to start ranging.
  - **Calibration Config Characteristic**: Short UUID: `3159`. Writing to this characteristic
  puts the node in calibration mode. The value written to this characteristic assigns
  the calibration index to the TriPoint. Valid indices are 0,1,2. The node with index
  0 will immediately start the calibration procedure and should be assigned last.
  
  
