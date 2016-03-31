#ifndef __GLOSSY_H
#define __GLOSSY_H

#include "firmware.h"
#include "deca_device_api.h"

#ifdef GLOSSY_PER_TEST
#define GLOSSY_UPDATE_INTERVAL_US 1e4
#else
#define GLOSSY_UPDATE_INTERVAL_US 1e6
#endif
#define GLOSSY_FLOOD_TIMESLOT_US  1e3

#define GLOSSY_UPDATE_INTERVAL_DW (DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US) & 0xFFFFFFFE)

typedef enum {
	GLOSSY_SLAVE = 0,
	GLOSSY_MASTER = 1
} glossy_role_e;

struct pp_glossy_sync {
	struct ieee154_header_broadcast header;
	uint8_t message_type;
	uint8_t depth;
	struct ieee154_footer footer;
} __attribute__ ((__packed__));

void glossy_init(glossy_role_e role);
void glossy_sync_task();
void glossy_sync_process(uint64_t dw_timestamp, uint8_t *buf);
void glossy_process_txcallback();

#endif

