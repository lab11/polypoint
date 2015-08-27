#ifndef __DW1000_ANCHOR_H
#define __DW1000_ANCHOR_H

#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000.h"

// Set at some arbitrary length for what the longest packet we will receive
// is.
#define DW1000_ANCHOR_MAX_RX_PKT_LEN 64

typedef enum {
	ASTATE_IDLE,
	ASTATE_RANGING,
	ASTATE_RESPONDING
} dw1000_anchor_state_e;

typedef struct {
	uint8_t  reply_after_subsequence;
	uint16_t anchor_reply_window_in_us;
	uint16_t anchor_reply_slot_time_in_us;
} dw1000_anchor_tag_config_t;

dw1000_err_e dw1000_anchor_init ();
void dw1000_anchor_start ();

void dw1000_anchor_txcallback (const dwt_callback_data_t *data);
void dw1000_anchor_rxcallback (const dwt_callback_data_t *data);

#endif