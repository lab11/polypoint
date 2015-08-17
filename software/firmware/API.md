Tripoint API
============


| Opcode           | Byte | Type | Description                        |
| ------           | ---- | ---- | -----------                        |
| `INFO`           | 0x01 | W/R  | Get information about the module. |
| `CONFIG`         | 0x02 | W    | Configure options. Set tag/anchor. |
| `SET_LOCATION`   | 0x   | W    | Set location of this device. Useful only for anchors. |
| `DO_RANGE`       | 0x   | W    | If not doing periodic ranging, initiate a range now. |
| `START_LOCATION` | 0x   | W    | Rather than just getting ranges, return actual positions. |
| `READ_INTERRUPT` | 0x   | W/R  | Ask the chip why it asserted the interrupt pin. |
| `SLEEP`          | 0x   | W    | Stop all ranging and put the device in sleep mode. |



### `INFO`

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


### `CONFIG`

```
Byte 0: 0x02  Opcode
Byte 1:       Config 1
   Bit 7:    Anchor/Tag select.
             0 = tag
             1 = anchor
   Bits 5-6: Update mode.
             Configure if the module should periodically get new locations
             or if it should get locations on demand.
             0 = update periodically
             1 = update only on demand
             2 = reserved
             3 = reserved
   Bit 4:    Report locations or ranges.
             Configure if the module should report raw ranges or a computed
             location. NOTE: The module may need to offload the location
             computation.
             0 = return ranges
             1 = return location
   Bits 0-3: Reserved.
Byte 2:      Location update rate.
             Specify the rate at which the module should get location updates.
             Specified in multiples of 0.1 Hz. 0 indicates as fast as possible.
```

| Index | Value | Description |
| ----- | ----- | ----------- |
| 0     | 0x01  | Opcode      |
| 1     |       | Bit 7: Anchor/Tag select.




