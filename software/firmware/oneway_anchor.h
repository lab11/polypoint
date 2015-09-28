#ifndef __ONEWAY_ANCHOR_H
#define __ONEWAY_ANCHOR_H

#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000.h"

// Set at some arbitrary length for what the longest packet we will receive
// is.
#define ONEWAY_ANCHOR_MAX_RX_PKT_LEN 64

typedef enum {
	ASTATE_IDLE,
	ASTATE_RANGING,
	ASTATE_RESPONDING
} oneway_anchor_state_e;

// Configuration data for the ANCHOR provided by the TAG
typedef struct {
	uint8_t  reply_after_subsequence;
	uint16_t anchor_reply_window_in_us;
	uint16_t anchor_reply_slot_time_in_us;
} oneway_anchor_tag_config_t;

void oneway_anchor_init ();
dw1000_err_e oneway_anchor_start ();
void oneway_anchor_stop ();

#endif
