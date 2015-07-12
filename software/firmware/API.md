Tripoint API
============


| Opcode           | Byte | Description                        |
| ------           | ---- | -----------                        |
| `CONFIG`         | 0x   | Configure options. Set tag/anchor. |
| `SET_LOCATION`   | 0x   | Set location of this device. Useful only for anchors. |
| `START_RANGING`  | 0x   | Tags. Start ranging with available anchors. Options for how often. |
| `DO_RANGE`       | 0x   | If not doing periodic ranging, initiate a range now. |
| `START_LOCATION` | 0x   | Rather than just getting ranges, return actual positions. |
| `READ_INTERRUPT` | 0x   | Ask the chip why it asserted the interrupt pin. |
| `SLEEP`          | 0x   | Stop all ranging and put the device in sleep mode. |




### `CONFIG`

| Index | Value | Description |
| ----- | ----- | ----------- |
| 0     | 0x    | Opcode      |




