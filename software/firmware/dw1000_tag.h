#ifndef __DW1000_TAG_H
#define __DW1000_TAG_H

#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000.h"

typedef enum {
	TSTATE_SLEEP,
	TSTATE_IDLE,
	TSTATE_BROADCASTS,
	TSTATE_TRANSITION_TO_ANC_FINAL,
	TSTATE_LISTENING,
	TSTATE_CALCULATE_RANGE
} tag_state_e;

// ERRORS for reporting to the TAG host what happened with ranges from different
// anchors. If an anchor didn't respond or the packet didn't go through then
// it will not be included. If the anchor did respond, then it will be included,
// and if something went wrong with the range an invalid range from below
// will be returned.

// The ANCHOR did not receive matching packets from the first three cycle.
// This prevents us from calculating clock skew, and we have to skip this
// anchor range.
#define DW1000_TAG_RANGE_ERROR_NO_OFFSET 0x80000001
// The anchor did not receive enough packets from the tag, so we don't have
// enough observations (ranges) to actually calculate a range to this
// anchor.
#define DW1000_TAG_RANGE_ERROR_TOO_FEW_RANGES 0x80000002
// Something else went wrong that we don't have pinned down.
#define DW1000_TAG_RANGE_ERROR_MISC 0x8000000F


void dw1000_tag_init ();
dw1000_err_e dw1000_tag_start_ranging_event ();
void dw1000_tag_stop ();

void dw1000_tag_txcallback (const dwt_callback_data_t *data);
void dw1000_tag_rxcallback (const dwt_callback_data_t *data);

#endif