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

typedef struct oneway_anchor_scratchspace_struct {
	// Our timer object that we use for timing packet transmissions
	stm_timer_t* anchor_timer;
	
	// State for the PRNG
	ranctx prng_state;
	
	/******************************************************************************/
	// Keep track of state for the given ranging event this anchor is handling.
	/******************************************************************************/
	// What the anchor is currently doing
	oneway_anchor_state_e state;
	// Which spot in the ranging broadcast sequence we are currently at
	uint8_t ranging_broadcast_ss_num;
	// What config parameters the tag sent us
	oneway_anchor_tag_config_t ranging_operation_config;
	// Which spot in the listening window sequence we are in.
	// The listening window refers to the time after the ranging broadcasts
	// when the tag listens for anchor responses on each channel
	uint8_t ranging_listening_window_num;
	
	// Keep track of, in each ranging session with a tag, how many packets we
	// receive on each antenna. This lets us pick the best antenna to use
	// when responding to a tag.
	uint8_t anchor_antenna_recv_num[NUM_ANTENNAS];
	
	// Packet that the anchor unicasts to the tag
	struct pp_anc_final pp_anc_final_pkt;
};

oneway_anchor_scratchspace_struct *oa_scratch;

void oneway_anchor_init (void *app_scratchspace);
dw1000_err_e oneway_anchor_start ();
void oneway_anchor_stop ();

#endif
