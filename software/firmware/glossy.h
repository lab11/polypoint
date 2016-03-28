#ifndef __GLOSSY_H
#define __GLOSSY_H

#include "firmware.h"
#include "deca_device_api.h"

#define GLOSSY_UPDATE_INTERVAL_US 10e6

typedef enum {
	GLOSSY_SLAVE = 0,
	GLOSSY_MASTER = 1
} glossy_role_e;

struct pp_glossy_sync {
	struct ieee154_header_broadcast header;
	uint8_t depth;
	uint32_t dw_time_sent;
} __attribute__ ((__packed__));

void glossy_init(glossy_role_e role);
void glossy_sync_task();

#endif

