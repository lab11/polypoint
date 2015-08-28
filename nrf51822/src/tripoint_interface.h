#ifndef __TRIPOINT_INTERFACE_H
#define __TRIPOINT_INTERFACE_H

#include "sdk_errors.h"

#define TRIPOINT_ID 0xB01A

#define TRIPOINT_ADDRESS 0x65


#define TRIPOINT_CMD_INFO             0x01
#define TRIPOINT_CMD_CONFIG           0x02
#define TRIPOINT_CMD_READ_INTERRUPT   0x03
#define TRIPOINT_CMD_DO_RANGE         0x04
#define TRIPOINT_CMD_SLEEP            0x05
#define TRIPOINT_CMD_RESUME           0x06
#define TRIPOINT_CMD_SET_LOCATION     0x07


ret_code_t tripoint_init ();
ret_code_t tripoint_hw_init ();
ret_code_t tripoint_get_info (uint16_t* id, uint8_t* version);
ret_code_t tripoint_start_ranging (bool periodic, uint8_t rate);
ret_code_t tripoint_start_anchor ();

#endif
