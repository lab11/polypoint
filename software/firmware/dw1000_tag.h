#ifndef __DW1000_TAG_H
#define __DW1000_TAG_H

#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000.h"

typedef enum {
	TSTATE_IDLE,
	TSTATE_BROADCASTS,
	TSTATE_TRANSITION_TO_ANC_FINAL,
	TSTATE_LISTENING
} tag_state_e;

void dw1000_tag_init ();
void dw1000_tag_start_ranging_event ();

void dw1000_tag_txcallback (const dwt_callback_data_t *data);
void dw1000_tag_rxcallback (const dwt_callback_data_t *data);

#endif