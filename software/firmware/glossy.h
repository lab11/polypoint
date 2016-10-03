#ifndef __GLOSSY_H
#define __GLOSSY_H

#include "firmware.h"
#include "deca_device_api.h"

#define LWB_SLOT_US               2e4

#define LWB_SLOTS_PER_RANGE       5

#define MAX_SCHED_TAGS            10
#define GLOSSY_MAX_DEPTH          10
#define TAG_SCHED_TIMEOUT         60

#ifdef GLOSSY_PER_TEST
#define GLOSSY_UPDATE_INTERVAL_US 1e4
#else
#define GLOSSY_UPDATE_INTERVAL_US 1e6
#endif

#define GLOSSY_FLOOD_TIMESLOT_US  8e2

#define GLOSSY_UPDATE_INTERVAL_DW (DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US) & 0xFFFFFFFE)

typedef enum {
	GLOSSY_SLAVE = 0,
	GLOSSY_MASTER = 1
} glossy_role_e;

struct pp_sched_flood {
	struct ieee154_header_broadcast header;
	uint8_t message_type;
	uint64_t tag_ranging_mask;
	uint8_t tag_sched_idx;
	uint8_t tag_sched_eui[EUI_LEN];
	struct ieee154_footer footer;
} __attribute__ ((__packed__));

struct pp_sched_req_flood {
	struct ieee154_header_broadcast header;
	uint8_t message_type;
	uint8_t deschedule_flag;
	uint8_t tag_sched_eui[EUI_LEN];
#ifdef GLOSSY_ANCHOR_SYNC_TEST
	uint64_t turnaround_time;
	double clock_offset_ppm;
	uint8_t sync_depth;
	int8_t xtal_trim;
#endif
	struct ieee154_footer footer;
} __attribute__ ((__packed__));


glossy_role_e glossy_get_role();
void glossy_init(glossy_role_e role);
void glossy_deschedule();
void glossy_sync_task();
uint8_t glossy_xtaltrim();
void lwb_set_sched_request(bool sched_en);
void lwb_set_sched_callback(void (*callback)(void));
void glossy_sync_process(uint64_t dw_timestamp, uint8_t *buf);
void glossy_process_txcallback();

#endif

