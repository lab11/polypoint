#include <stddef.h>
#include <string.h>

#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000_anchor.h"
#include "dw1000.h"
#include "timer.h"
#include "prng.h"
#include "firmware.h"

// Our timer object that we use for timing packet transmissions
timer_t* _ranging_broadcast_timer;

// State for the PRNG
ranctx _prng_state;

//
// Keep track of state for the given ranging event this anchor is handling.
//

// What the anchor is currently doing
static dw1000_anchor_state_e _state = ASTATE_IDLE;
// Which spot in the ranging broadcast sequence we are currently at
static uint8_t _ranging_broadcast_ss_num = 0;
// What config parameters the tag sent us
static dw1000_anchor_tag_config_t _ranging_operation_config;
// Which spot in the listening window sequence we are in.
// The listening window refers to the time after the ranging broadcasts
// when the tag listens for anchor responses on each channel
static uint8_t _ranging_listening_window_num = 0;

// Keep track of, in each ranging session with a tag, how many packets we
// receive on each antenna. This lets us pick the best antenna to use
// when responding to a tag.
static uint8_t _anchor_antenna_recv_num[NUM_ANTENNAS] = {0};

// Packet that the anchor unicasts to the tag
static struct pp_anc_final pp_anc_final_pkt = {
	.ieee154_header_unicast = {
		.frameCtrl = {
			0x41, // FCF[0]: data frame, panid compression
			0xCC  // FCF[1]: ext source, ext destination
		},
		.seqNum = 0,
		.panID = {
			POLYPOINT_PANID & 0xFF,
			POLYPOINT_PANID >> 8,
		},
		.destAddr = { 0 },    // (blank for now)
		.sourceAddr = { 0 },  // (blank for now)
	},
	.message_type  = MSG_TYPE_PP_NOSLOTS_ANC_FINAL,
	.final_antenna = 0,
	.dw_time_sent  = 0,
	.TOAs          = { 0 },
};

static void ranging_listening_window_setup();


dw1000_err_e dw1000_anchor_init () {
	uint8_t eui_array[8];

	// Make sure the radio starts off
	dwt_forcetrxoff();

	// Set the anchor so it only receives data and ack packets
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	// Set the ID and PAN ID for this anchor
	dw1000_read_eui(eui_array);
	dwt_seteui(eui_array);
	dwt_setpanid(POLYPOINT_PANID);

	// Automatically go back to receive
	dwt_setautorxreenable(TRUE);

	// Don't use these
	dwt_setdblrxbuffmode(FALSE);
	dwt_setrxtimeout(FALSE);

	// Don't receive at first
	dwt_rxenable(FALSE);

	// Load our EUI into the outgoing packet
	dw1000_read_eui(pp_anc_final_pkt.ieee154_header_unicast.sourceAddr);

	// Need a timer
	_ranging_broadcast_timer = timer_init();

	// Init the PRNG for determining when to respond to the tag
	raninit(&_prng_state, eui_array[0]<<8|eui_array[1]);

	// Make SPI fast now that everything has been setup
	dw1000_spi_fast();

	return DW1000_NO_ERR;
}

// Tell the anchor to start its job of being an anchor
void dw1000_anchor_start () {
	// Choose to wait in the first default position.
	// This could change to wait in any of the first NUM_CHANNEL-1 positions.
	dw1000_set_ranging_broadcast_subsequence_settings(ANCHOR, 0, TRUE);

	// Also we start over in case the anchor was doing anything before
	_state = ASTATE_IDLE;

	// Obviously we want to be able to receive packets
	dwt_rxenable(0);
}

// This is called by the periodic timer that tracks the tag's periodic
// broadcast ranging poll messages. This is responsible for setting the
// antenna and channel properties for the anchor.
static void ranging_broadcast_subsequence_task () {
	// When this timer is called it is time to start a new subsequence
	// slot, so we must increment our counter
	_ranging_broadcast_ss_num++;

	// Check if we are done listening for packets from the TAG. If we get
	// a packet on the last subsequence we won't get here, but if we
	// don't get that packet we need this check.
	if (_ranging_broadcast_ss_num > _ranging_operation_config.reply_after_subsequence) {
		ranging_listening_window_setup();

	} else {
		// Update the anchor listening settings
		dw1000_set_ranging_broadcast_subsequence_settings(ANCHOR, _ranging_broadcast_ss_num, FALSE);

		// And re-enable RX. The set_broadcast_settings function disables tx and rx.
		dwt_rxenable(0);
	}
}

// Called at the beginning of each listening window for transmitting to
// the tag.
static void ranging_listening_window_task () {
	// Check if we are done transmitting to the tag.
	// Ideally we never get here, as an ack from the tag will cause us to stop
	// cycling through listening windows and put us back into a ready state.
	if (_ranging_listening_window_num == NUM_RANGING_CHANNELS) {
		// Go back to IDLE
		_state = ASTATE_IDLE;
		// Stop the timer for the window
		timer_stop(_ranging_broadcast_timer);

		// Restart being an anchor
		dw1000_anchor_start();

	} else {

		// Setup the channel and antenna settings
		dw1000_set_ranging_listening_window_settings(ANCHOR,
		                                             _ranging_listening_window_num,
		                                             pp_anc_final_pkt.final_antenna,
		                                             FALSE);

		// Prepare the outgoing packet to send back to the
		// tag with our TOAs.
		pp_anc_final_pkt.ieee154_header_unicast.seqNum++;
		const uint16_t frame_len = sizeof(struct pp_anc_final);
		dwt_writetxfctrl(frame_len, 0);

		// Pick a slot to respond in. Generate a random number and mod it
		// by the number of slots
		uint8_t slot_num = ranval(&_prng_state) % (_ranging_operation_config.anchor_reply_window_in_us /
		                                          _ranging_operation_config.anchor_reply_slot_time_in_us);

		// Come up with the time to send this packet back to the
		// tag based on the slot we picked.
		uint32_t delay_time = dwt_readsystimestamphi32() +
			DW_DELAY_FROM_US(ANC_FINAL_INITIAL_DELAY_HACK_VALUE +
				(slot_num*_ranging_operation_config.anchor_reply_slot_time_in_us));

		delay_time &= 0xFFFFFFFE;
		pp_anc_final_pkt.dw_time_sent = delay_time;
		dwt_setdelayedtrxtime(delay_time);

		// Send the response packet
		int err = dwt_starttx(DWT_START_TX_DELAYED);
		dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
		dwt_writetxdata(frame_len, (uint8_t*) &pp_anc_final_pkt, 0);

		_ranging_listening_window_num++;
	}
}

// Prepare to transmit a response to the TAG.
// TODO: check to see if we should even bother. Did we get enough packets?
static void ranging_listening_window_setup () {
	// Stop iterating through timing channels
	timer_stop(_ranging_broadcast_timer);

	// We no longer need to receive and need to instead
	// start transmitting.
	dwt_forcetrxoff();

	// Update our state to the TX response state
	_state = ASTATE_RESPONDING;
	// Set the listening window index
	_ranging_listening_window_num = 0;

	// Determine which antenna we are going to use for
	// the response.
	uint8_t max_packets = 0;
	uint8_t max_index = 0;
	for (uint8_t i=0; i<NUM_ANTENNAS; i++) {
		if (_anchor_antenna_recv_num[i] > max_packets) {
			max_packets = _anchor_antenna_recv_num[i];
			max_index = i;
		}
	}
	pp_anc_final_pkt.final_antenna = max_index;

	// Now we need to setup a timer to iterate through
	// the response windows so we can send a packet
	// back to the tag
	timer_start(_ranging_broadcast_timer,
	            _ranging_operation_config.anchor_reply_window_in_us,
	            ranging_listening_window_task);
}


// Called after a packet is transmitted. We don't need this so it is
// just empty.
void dw1000_anchor_txcallback (const dwt_callback_data_t *txd) {

}

// Called when the radio has received a packet.
void dw1000_anchor_rxcallback (const dwt_callback_data_t *rxd) {

	if (rxd->event == DWT_SIG_RX_OKAY) {

		// Read in parameters of this packet reception
		uint64_t           dw_rx_timestamp;
		uint8_t            buf[DW1000_ANCHOR_MAX_RX_PKT_LEN];
		uint8_t            message_type;

		// Get the received time of this packet first
		dwt_readrxtimestamp(buf);
		dw_rx_timestamp = DW_TIMESTAMP_TO_UINT64(buf);

		// Get the actual packet bytes
		dwt_readrxdata(buf, MIN(DW1000_ANCHOR_MAX_RX_PKT_LEN, rxd->datalength), 0);

		// We process based on the first byte in the packet. How very active
		// message like...
		message_type = buf[offsetof(struct pp_tag_poll, message_type)];

		if (message_type == MSG_TYPE_PP_NOSLOTS_TAG_POLL) {
			// This is one of the broadcast ranging packets from the tag
			struct pp_tag_poll* rx_poll_pkt = (struct pp_tag_poll*) buf;

			// Decide what to do with this packet
			if (_state == ASTATE_IDLE) {
				// We are currently not ranging with any tags.

				if (rx_poll_pkt->subsequence < NUM_RANGING_CHANNELS) {
					// We are idle and this is one of the first packets
					// that the tag sent. Start listening for this tag's
					// ranging broadcast packets.
					_state = ASTATE_RANGING;

					// Clear memory for this new tag ranging event
					memset(pp_anc_final_pkt.TOAs, 0, sizeof(pp_anc_final_pkt.TOAs));
					memset(_anchor_antenna_recv_num, 0, sizeof(_anchor_antenna_recv_num));

					// Record the EUI of the tag so that we don't get mixed up
					memcpy(pp_anc_final_pkt.ieee154_header_unicast.destAddr, rx_poll_pkt->header.sourceAddr, 8);
					// Record which ranging subsequence the tag is on
					_ranging_broadcast_ss_num = rx_poll_pkt->subsequence;
					// Record the timestamp
					pp_anc_final_pkt.TOAs[_ranging_broadcast_ss_num] = dw_rx_timestamp;
					// Also record parameters the tag has sent us about how to respond
					// (or other operational parameters).
					_ranging_operation_config.reply_after_subsequence = rx_poll_pkt->reply_after_subsequence;
					_ranging_operation_config.anchor_reply_window_in_us = rx_poll_pkt->anchor_reply_window_in_us;
					_ranging_operation_config.anchor_reply_slot_time_in_us = rx_poll_pkt->anchor_reply_slot_time_in_us;

					// Update the statistics we keep about which antenna
					// receives the most packets from the tag
					uint8_t recv_antenna_index = subsequence_number_to_antenna(ANCHOR, rx_poll_pkt->subsequence);
					_anchor_antenna_recv_num[recv_antenna_index]++;

					// Now we need to start our own state machine to iterate
					// through the antenna / channel combinations while listening
					// for packets from the same tag.
	// _ranging_broadcast_ss_num++;
	// dw1000_set_ranging_broadcast_subsequence_settings(ANCHOR, _ranging_broadcast_ss_num, FALSE);
					timer_start(_ranging_broadcast_timer, RANGING_BROADCASTS_PERIOD_US, ranging_broadcast_subsequence_task);

				} else {
					// We found this tag ranging sequence late. We don't want
					// to use this because we won't get enough range estimates.
					// Just stay idle.
				}

			} else if (_state == ASTATE_RANGING) {
				// We are currently ranging with a tag, waiting for the various
				// ranging broadcast packets.

				// First check if this is from the same tag
				if (memcmp(pp_anc_final_pkt.ieee154_header_unicast.destAddr, rx_poll_pkt->header.sourceAddr, 8) == 0) {
					// Same tag

					if (rx_poll_pkt->subsequence == _ranging_broadcast_ss_num) {
						// This is the packet we were expecting from the tag.
						// Record the TOA.
						pp_anc_final_pkt.TOAs[_ranging_broadcast_ss_num] = dw_rx_timestamp;

						// Update the statistics we keep about which antenna
						// receives the most packets from the tag
						uint8_t recv_antenna_index = subsequence_number_to_antenna(ANCHOR, _ranging_broadcast_ss_num);
						_anchor_antenna_recv_num[recv_antenna_index]++;

					} else {
						// Some how we got out of sync with the tag. Ignore the
						// range and catch up.
						_ranging_broadcast_ss_num = rx_poll_pkt->subsequence;
					}

					// Check to see if we got the last of the ranging broadcasts
					if (_ranging_broadcast_ss_num == _ranging_operation_config.reply_after_subsequence) {
						// We did!
						ranging_listening_window_setup();
					}

				} else {
					// Not the same tag, ignore
				}
			} else {
				// We are in some other state, not sure what that means
			}

		} else {
			// Other message types go here, if they get added
		}

	} else {
		// If an RX error has occurred, we're gonna need to setup the receiver again
		// (because dwt_rxreset within dwt_isr smashes everything without regard)
		if (rxd->event == DWT_SIG_RX_PHR_ERROR ||
			rxd->event == DWT_SIG_RX_ERROR ||
			rxd->event == DWT_SIG_RX_SYNCLOSS ||
			rxd->event == DWT_SIG_RX_SFDTIMEOUT ||
			rxd->event == DWT_SIG_RX_PTOTIMEOUT) {
			dw1000_set_ranging_broadcast_subsequence_settings(ANCHOR, _ranging_broadcast_ss_num, TRUE);
		} else {
			// Some other unknown error, not sure what to do
		}
	}
}
