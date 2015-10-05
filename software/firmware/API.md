Tripoint API
============

This defines the I2C interface for the TriPoint module. TriPoint is an
I2C slave.

```
I2C Address: 0x65
```


Commands
--------

These commands are set as a WRITE I2C command from the host to the TriPoint. Each
write command starts with the opcode.

| Opcode             | Byte | Type | Description                                            |
| ------             | ---- | ---- | -----------                                            |
| `INFO`             | 0x01 | W/R  | Get information about the module.                      |
| `CONFIG`           | 0x02 | W    | Configure options. Set tag/anchor.                     |
| `READ_INTERRUPT`   | 0x03 | W/R  | Ask the chip why it asserted the interrupt pin.        |
| `DO_RANGE`         | 0x04 | W    | If not doing periodic ranging, initiate a range now.   |
| `SLEEP`            | 0x05 | W    | Stop all ranging and put the device in sleep mode.     |
| `RESUME`           | 0x06 | W    | Restart ranging.                                       |
| `SET_LOCATION`     | 0x07 | W    | Set location of this device. Useful only for anchors.  |
| `READ_CALIBRATION` | 0x08 | W/R  | Read the stored calibration values from this TriPoint. |





#### `INFO`

Write:
```
Byte 0: 0x01  Opcode
```


Read:
```
Byte 0: 0xB0
Byte 1: 0x1A
Byte 2: version
```


#### `CONFIG`

```
Byte 0: 0x02  Opcode

Byte 1:      Config 1
   Bits 4-7: Reserved
   Bits 2-4: Application select.
             Choose which ranging application to execute on the TriPoint.
               0 = Default
               1 = Calibration
               2-7 = reserved
   Bits 0-1: Anchor/Tag select.
               0 = tag
               1 = anchor
               2 = reserved
               3 = reserved

IF TAG:
Byte 2:
   Bits 4-7: Reserved.
   Bit 3:    Sleep settings.
             Configure if TriPoint should sleep the DW1000 between ranging
             events.
               0 = Do not sleep.
               1 = Enter sleep between ranging events.
   Bits 1-2: Update mode.
             Configure if the module should periodically get new locations
             or if it should get locations on demand.
               0 = update periodically
               1 = update only on demand
               2 = reserved
               3 = reserved
   Bit 0:    Report locations or ranges.
             Configure if the module should report raw ranges or a computed
             location. NOTE: The module may need to offload the location
             computation.
               0 = return ranges
               1 = return location

Byte 3:      Location update rate.
             Specify the rate at which the module should get location updates.
             Specified in multiples of 0.1 Hz. 0 indicates as fast as possible.

IF ANCHOR:
   TODO

IF CALIBRATION:
Byte 2:      Calibration node index.
             The index of the node in the calibration session. Valid values
             are 0,1,2. When a node is assigned index 0, it automatically
             starts the calibration round.

```


### Both TAG and ANCHOR Commands


#### `READ_INTERRUPT`

Write:
```
Byte 0: 0x03  Opcode
````

Read:
```
Byte 0: Length of the following message.

Byte 1: Interrupt reason
  1 = Ranges to anchors are available
  2 = Calibration data


IF byte1 == 0x1:
Byte 2: Number of ranges.
Bytes 3-n: 8 bytes of anchor EUI then 4 bytes of range in millimeters.

IF byte1 == 0x2:
Bytes 2-3:   Round number
Bytes 4-8:   Round A timestamp. TX/RX depends on which node index this node is.
Bytes 9-12:  Diff between Round A timestamp and Round B timestamp.
Bytes 13-16: Diff between Round B timestamp and Round C timestamp.
Bytes 17-20: Diff between Round C timestamp and Round D timestamp.

TODO
```


#### `SLEEP`

Stop all ranging and put the module into sleep mode.
```
Byte 0: 0x05  Opcode
```

#### `RESUME`

If the module is in SLEEP mode after a `SLEEP` command, this will resume the
previous settings.
```
Byte 0: 0x06  Opcode
````

#### `READ_CALBRATION`

Read the stored calibration values off of the device.

Write:
```
Byte 0: 0x08  Opcode
````

Read:
```
Bytes 0-1:   Channel 0, Antenna 0 TX+RX delay
Bytes 2-3:   Channel 0, Antenna 1 TX+RX delay
Bytes 4-5:   Channel 0, Antenna 2 TX+RX delay
Bytes 6-7:   Channel 1, Antenna 0 TX+RX delay
Bytes 8-9:   Channel 1, Antenna 1 TX+RX delay
Bytes 10-11: Channel 1, Antenna 2 TX+RX delay
Bytes 12-13: Channel 2, Antenna 0 TX+RX delay
Bytes 14-15: Channel 2, Antenna 1 TX+RX delay
Bytes 16-17: Channel 2, Antenna 2 TX+RX delay
```

### TAG Commands


#### `DO_RANGE`

Initiate a ranging event. Only valid if tag is in update on demand mode.

```
Byte 0: 0x04  Opcode
```


