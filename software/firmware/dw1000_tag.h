#ifndef __DW1000_TAG_H
#define __DW1000_TAG_H

#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000.h"

typedef enum {
	TSTATE_IDLE,
	TSTATE_BROADCASTS,
	TSTATE_TRANSITION_TO_ANC_FINAL,
	TSTATE_LISTENING,
	TSTATE_CALCULATE_RANGE
} tag_state_e;

typedef struct {
	uint8_t  anchor_addr[EUI_LEN];
	uint8_t  anchor_final_antenna_index; // The antenna the anchor used when it responded.
	uint8_t  window_packet_recv;         // The window the tag was in when it received the packet from the anchor.
	uint64_t anc_final_tx_timestamp; // When the anchor node sent the ANC_FINAL
	uint64_t anc_final_rx_timestamp; // When the tag received the ANC_FINAL
	uint64_t tag_poll_TOAs[NUM_RANGING_BROADCASTS];
} anchor_responses_t;

void dw1000_tag_init ();
dw1000_err_e dw1000_tag_start_ranging_event ();
void dw1000_tag_stop ();

void dw1000_tag_txcallback (const dwt_callback_data_t *data);
void dw1000_tag_rxcallback (const dwt_callback_data_t *data);

#endif