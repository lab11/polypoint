#ifndef __DW1000_ANCHOR_H
#define __DW1000_ANCHOR_H

#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000.h"

// Set at some arbritrary length for what the longest packet we will receive
// is.
#define DW1000_ANCHOR_MAX_RX_PKT_LEN 64

typedef enum {
	ASTATE_IDLE,
	ASTATE_RANGING
} dw1000_anchor_state_e;

dw1000_err_e dw1000_anchor_init ();

void dw1000_anchor_txcallback (const dwt_callback_data_t *data);
void dw1000_anchor_rxcallback (const dwt_callback_data_t *data);

#endif