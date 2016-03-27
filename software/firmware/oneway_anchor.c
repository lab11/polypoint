#include <stddef.h>
#include <string.h>

#include "deca_device_api.h"
#include "deca_regs.h"

#include "oneway_common.h"
#include "oneway_anchor.h"
#include "dw1000.h"
#include "timer.h"
#include "delay.h"
#include "firmware.h"

static void ranging_listening_window_setup();
static void anchor_txcallback (const dwt_callback_data_t *txd);
static void anchor_rxcallback (const dwt_callback_data_t *rxd);


void oneway_anchor_init (void *app_scratchspace) {
	
	oa_scratch = (oneway_anchor_scratchspace_struct*) app_scratchspace;
	
	// Initialize this app's scratchspace
	oa_scratch->pp_anc_final_pkt = (struct pp_anc_final) {
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

	// Make sure the SPI speed is slow for this function
	dw1000_spi_slow();

	// Setup callbacks to this ANCHOR
	dwt_setcallbacks(anchor_txcallback, anchor_rxcallback);

	// Make sure the radio starts off
	dwt_forcetrxoff();

	// Set the anchor so it only receives data and ack packets
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	// // Set the ID and PAN ID for this anchor
	uint8_t eui_array[8];
	dw1000_read_eui(eui_array);
	// dwt_seteui(eui_array);
	// dwt_setpanid(POLYPOINT_PANID);

	// Automatically go back to receive
	dwt_setautorxreenable(TRUE);

	// Don't use these
	dwt_setdblrxbuffmode(FALSE);
	dwt_setrxtimeout(FALSE);

	// Load our EUI into the outgoing packet
	dw1000_read_eui(oa_scratch->pp_anc_final_pkt.ieee154_header_unicast.sourceAddr);

	// Need a timer
	if (oa_scratch->anchor_timer == NULL) {
		oa_scratch->anchor_timer = timer_init();
	}

	// Init the PRNG for determining when to respond to the tag
	raninit(&(oa_scratch->prng_state), eui_array[0]<<8|eui_array[1]);

	// Make SPI fast now that everything has been setup
	dw1000_spi_fast();

	// Reset our state because nothing should be in progress if we call init()
	oa_scratch->state = ASTATE_IDLE;
}

// Tell the anchor to start its job of being an anchor
dw1000_err_e oneway_anchor_start () {
	dw1000_err_e err;

	// Make sure the DW1000 is awake.
	err = dw1000_wakeup();
	if (err == DW1000_WAKEUP_SUCCESS) {
		// We did wake the chip, so reconfigure it properly
		// Put back the ANCHOR settings.
		oneway_anchor_init((void*)oa_scratch);
	} else if (err) {
		// Chip did not seem to wakeup. This is not good, so we have
		// to reset the application.
		return err;
	}

	// Also we start over in case the anchor was doing anything before
	oa_scratch->state = ASTATE_IDLE;

	// Choose to wait in the first default position.
	// This could change to wait in any of the first NUM_CHANNEL-1 positions.
	oneway_set_ranging_broadcast_subsequence_settings(ANCHOR, 0);

	// Obviously we want to be able to receive packets
	dwt_rxenable(0);

	return DW1000_NO_ERR;
}

// Tell the anchor to stop ranging with TAGs.
// This cancels whatever the anchor was doing.
void oneway_anchor_stop () {
	// Put the anchor in SLEEP state. This is useful in case we need to
	// re-init some stuff after the anchor comes back alive.
	oa_scratch->state = ASTATE_IDLE;

	// Stop the timer in case it was in use
	timer_stop(oa_scratch->anchor_timer);

	// Put the DW1000 in SLEEP mode.
	dw1000_sleep();
}

// This is called by the periodic timer that tracks the tag's periodic
// broadcast ranging poll messages. This is responsible for setting the
// antenna and channel properties for the anchor.
static void ranging_broadcast_subsequence_task () {
	// When this timer is called it is time to start a new subsequence
	// slot, so we must increment our counter
	oa_scratch->ranging_broadcast_ss_num++;

	// Check if we are done listening for packets from the TAG. If we get
	// a packet on the last subsequence we won't get here, but if we
	// don't get that packet we need this check.
	if (oa_scratch->ranging_broadcast_ss_num > oa_scratch->ranging_operation_config.reply_after_subsequence) {
		ranging_listening_window_setup();

	} else {
		// Update the anchor listening settings
		oneway_set_ranging_broadcast_subsequence_settings(ANCHOR, oa_scratch->ranging_broadcast_ss_num);

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
	if (oa_scratch->ranging_listening_window_num == NUM_RANGING_CHANNELS) {
		// Go back to IDLE
		oa_scratch->state = ASTATE_IDLE;
		// Stop the timer for the window
		timer_stop(oa_scratch->anchor_timer);

		// Restart being an anchor
		oneway_anchor_start();

	} else {

		// Setup the channel and antenna settings
		oneway_set_ranging_listening_window_settings(ANCHOR,
		                                             oa_scratch->ranging_listening_window_num,
		                                             oa_scratch->pp_anc_final_pkt.final_antenna);

		// Prepare the outgoing packet to send back to the
		// tag with our TOAs.
		oa_scratch->pp_anc_final_pkt.ieee154_header_unicast.seqNum++;
		const uint16_t frame_len = sizeof(struct pp_anc_final);
		// const uint16_t frame_len = sizeof(struct pp_anc_final) - (sizeof(uint64_t)*NUM_RANGING_BROADCASTS);
		dwt_writetxfctrl(frame_len, 0);

		// Pick a slot to respond in. Generate a random number and mod it
		// by the number of slots
		uint32_t slot_time = ranval(&(oa_scratch->prng_state)) % (oa_scratch->ranging_operation_config.anchor_reply_window_in_us -
		                                                           dw1000_packet_data_time_in_us(frame_len) -
		                                                           dw1000_preamble_time_in_us());

		// Come up with the time to send this packet back to the
		// tag based on the slot we picked.
		uint32_t delay_time = dwt_readsystimestamphi32() +
			DW_DELAY_FROM_US(RANGING_LISTENING_WINDOW_PADDING_US + dw1000_preamble_time_in_us() + slot_time);

		delay_time &= 0xFFFFFFFE;

		// Record the outgoing time in the packet. Do not take calibration into
		// account here, as that is done on all of the RX timestamps.
		oa_scratch->pp_anc_final_pkt.dw_time_sent = (((uint64_t) delay_time) << 8) + oneway_get_txdelay_from_ranging_listening_window(oa_scratch->ranging_listening_window_num);

		// Set the packet to be transmitted later.
		dwt_setdelayedtrxtime(delay_time);

		// Send the response packet
		// TODO: handle if starttx errors. I'm not sure what to do about it,
		//       other than just wait for the next slot.
		dwt_starttx(DWT_START_TX_DELAYED);
		dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
		dwt_writetxdata(frame_len, (uint8_t*) &(oa_scratch->pp_anc_final_pkt), 0);

		oa_scratch->ranging_listening_window_num++;
	}
}

// Prepare to transmit a response to the TAG.
// TODO: check to see if we should even bother. Did we get enough packets?
static void ranging_listening_window_setup () {
	// Stop iterating through timing channels
	timer_stop(oa_scratch->anchor_timer);

	// We no longer need to receive and need to instead
	// start transmitting.
	dwt_forcetrxoff();

	// Update our state to the TX response state
	oa_scratch->state = ASTATE_RESPONDING;
	// Set the listening window index
	oa_scratch->ranging_listening_window_num = 0;

	// Determine which antenna we are going to use for
	// the response.
	uint8_t max_packets = 0;
	uint8_t max_index = 0;
	for (uint8_t i=0; i<NUM_ANTENNAS; i++) {
		if (oa_scratch->anchor_antenna_recv_num[i] > max_packets) {
			max_packets = oa_scratch->anchor_antenna_recv_num[i];
			max_index = i;
		}
	}
	oa_scratch->pp_anc_final_pkt.final_antenna = max_index;

	// Now we need to setup a timer to iterate through
	// the response windows so we can send a packet
	// back to the tag
	timer_start(oa_scratch->anchor_timer,
	            oa_scratch->ranging_operation_config.anchor_reply_window_in_us + RANGING_LISTENING_WINDOW_PADDING_US*2,
	            ranging_listening_window_task);
}


// Called after a packet is transmitted. We don't need this so it is
// just empty.
static void anchor_txcallback (const dwt_callback_data_t *txd) {

}

// Called when the radio has received a packet.
static void anchor_rxcallback (const dwt_callback_data_t *rxd) {

	if (rxd->event == DWT_SIG_RX_OKAY) {

		// Read in parameters of this packet reception
		uint64_t dw_rx_timestamp;
		uint8_t  buf[ONEWAY_ANCHOR_MAX_RX_PKT_LEN];
		uint8_t  message_type;

		// Get the received time of this packet first
		dwt_readrxtimestamp(buf);
		dw_rx_timestamp = DW_TIMESTAMP_TO_UINT64(buf);

		// Get the actual packet bytes
		dwt_readrxdata(buf, MIN(ONEWAY_ANCHOR_MAX_RX_PKT_LEN, rxd->datalength), 0);

		// We process based on the first byte in the packet. How very active
		// message like...
		message_type = buf[offsetof(struct pp_tag_poll, message_type)];

		if (message_type == MSG_TYPE_PP_NOSLOTS_TAG_POLL) {
			// This is one of the broadcast ranging packets from the tag
			struct pp_tag_poll* rx_poll_pkt = (struct pp_tag_poll*) buf;

			// Decide what to do with this packet
			if (oa_scratch->state == ASTATE_IDLE) {
				// We are currently not ranging with any tags.

				if (rx_poll_pkt->subsequence < NUM_RANGING_CHANNELS) {
					// We are idle and this is one of the first packets
					// that the tag sent. Start listening for this tag's
					// ranging broadcast packets.
					oa_scratch->state = ASTATE_RANGING;

					// Clear memory for this new tag ranging event
					memset(oa_scratch->pp_anc_final_pkt.TOAs, 0, sizeof(oa_scratch->pp_anc_final_pkt.TOAs));
					memset(oa_scratch->anchor_antenna_recv_num, 0, sizeof(oa_scratch->anchor_antenna_recv_num));

					// Record the EUI of the tag so that we don't get mixed up
					memcpy(oa_scratch->pp_anc_final_pkt.ieee154_header_unicast.destAddr, rx_poll_pkt->header.sourceAddr, 8);
					// Record which ranging subsequence the tag is on
					oa_scratch->ranging_broadcast_ss_num = rx_poll_pkt->subsequence;
					// Record the timestamp. Need to subtract off the TX+RX delay from each recorded
					// timestamp.
					oa_scratch->pp_anc_final_pkt.TOAs[oa_scratch->ranging_broadcast_ss_num] =
						dw_rx_timestamp - oneway_get_rxdelay_from_subsequence(ANCHOR, oa_scratch->ranging_broadcast_ss_num);
					// Also record parameters the tag has sent us about how to respond
					// (or other operational parameters).
					oa_scratch->ranging_operation_config.reply_after_subsequence = rx_poll_pkt->reply_after_subsequence;
					oa_scratch->ranging_operation_config.anchor_reply_window_in_us = rx_poll_pkt->anchor_reply_window_in_us;
					oa_scratch->ranging_operation_config.anchor_reply_slot_time_in_us = rx_poll_pkt->anchor_reply_slot_time_in_us;

					// Update the statistics we keep about which antenna
					// receives the most packets from the tag
					uint8_t recv_antenna_index = oneway_subsequence_number_to_antenna(ANCHOR, rx_poll_pkt->subsequence);
					oa_scratch->anchor_antenna_recv_num[recv_antenna_index]++;

					// Now we need to start our own state machine to iterate
					// through the antenna / channel combinations while listening
					// for packets from the same tag.
					timer_start(oa_scratch->anchor_timer, RANGING_BROADCASTS_PERIOD_US, ranging_broadcast_subsequence_task);

				} else {
					// We found this tag ranging sequence late. We don't want
					// to use this because we won't get enough range estimates.
					// Just stay idle, but we do need to re-enable RX to
					// keep receiving packets.
					dwt_rxenable(0);
				}

			} else if (oa_scratch->state == ASTATE_RANGING) {
				// We are currently ranging with a tag, waiting for the various
				// ranging broadcast packets.

				// First check if this is from the same tag
				if (memcmp(oa_scratch->pp_anc_final_pkt.ieee154_header_unicast.destAddr, rx_poll_pkt->header.sourceAddr, 8) == 0) {
					// Same tag

					if (rx_poll_pkt->subsequence == oa_scratch->ranging_broadcast_ss_num) {
						// This is the packet we were expecting from the tag.
						// Record the TOA, and adjust it with the calibration value.
						oa_scratch->pp_anc_final_pkt.TOAs[oa_scratch->ranging_broadcast_ss_num] =
							dw_rx_timestamp - oneway_get_rxdelay_from_subsequence(ANCHOR, oa_scratch->ranging_broadcast_ss_num);

						// Update the statistics we keep about which antenna
						// receives the most packets from the tag
						uint8_t recv_antenna_index = oneway_subsequence_number_to_antenna(ANCHOR, oa_scratch->ranging_broadcast_ss_num);
						oa_scratch->anchor_antenna_recv_num[recv_antenna_index]++;

					} else {
						// Some how we got out of sync with the tag. Ignore the
						// range and catch up.
						oa_scratch->ranging_broadcast_ss_num = rx_poll_pkt->subsequence;
					}

					// Check to see if we got the last of the ranging broadcasts
					if (oa_scratch->ranging_broadcast_ss_num == oa_scratch->ranging_operation_config.reply_after_subsequence) {
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
			// We do want to enter RX mode again, however
			dwt_rxenable(0);
		}

	} else {
		// If an RX error has occurred, we're gonna need to setup the receiver again
		// (because dwt_rxreset within dwt_isr smashes everything without regard)
		if (rxd->event == DWT_SIG_RX_PHR_ERROR ||
			rxd->event == DWT_SIG_RX_ERROR ||
			rxd->event == DWT_SIG_RX_SYNCLOSS ||
			rxd->event == DWT_SIG_RX_SFDTIMEOUT ||
			rxd->event == DWT_SIG_RX_PTOTIMEOUT) {
			oneway_set_ranging_broadcast_subsequence_settings(ANCHOR, oa_scratch->ranging_broadcast_ss_num);
		} else {
			// Some other unknown error, not sure what to do
		}
	}
}
