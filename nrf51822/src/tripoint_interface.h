#ifndef __TRIPOINT_INTERFACE_H
#define __TRIPOINT_INTERFACE_H

#include "sdk_errors.h"


#define TRIPOINT_ADDRESS 0x74


#define TRIPOINT_CMD_START_RANGING  0x02


ret_code_t tripoint_init ();
ret_code_t tripoint_start_ranging ();

#endif
