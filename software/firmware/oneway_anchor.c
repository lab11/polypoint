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

static void send_poll ();
static void ranging_broadcast_subsequence_task ();
static void report_range ();
static void calculate_ranges ();
static void ranging_listening_window_setup();
static void anchor_txcallback (const dwt_callback_data_t *txd);
static void anchor_rxcallback (const dwt_callback_data_t *rxd);


void oneway_anchor_init (void *app_scratchspace) {
	
	oa_scratch = (oneway_anchor_scratchspace_struct*) app_scratchspace;
	
	// Initialize this app's scratchspace
	oa_scratch->pp_anc_final_pkt = (struct pp_anc_final) {
		.ieee154_header_unicast = {
			.frameCtrl = {
				0x61, // FCF[0]: data frame, ack request, panid compression
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

	// Initialize important variables inside scratchspace
	oa_scratch->pp_tag_poll_pkt = (struct pp_tag_poll) {
		{ // 802.15.4 HEADER
			{
				0x41, // FCF[0]: data frame, panid compression
				0xC8  // FCF[1]: ext source address, compressed destination
			},
			0,        // Sequence number
			{
				POLYPOINT_PANID & 0xFF, // PAN ID
				POLYPOINT_PANID >> 8
			},
			{
				0xFF, // Destination address: broadcast
				0xFF
			},
			{ 0 }     // Source (blank for now)
		},
		// PACKET BODY
		MSG_TYPE_PP_NOSLOTS_TAG_POLL,  // Message type
		0,                             // Sub Sequence number
		NUM_RANGING_BROADCASTS-1,
		RANGING_LISTENING_WINDOW_US,
		RANGING_LISTENING_SLOT_US
	};

	// Put source EUI in the pp_tag_poll packet
	dw1000_read_eui(oa_scratch->pp_tag_poll_pkt.header.sourceAddr);

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

	// Don't use these
	dwt_setrxtimeout(FALSE);

	// Setup parameters of how the radio should work
	dwt_setautorxreenable(TRUE);
	dwt_setdblrxbuffmode(TRUE);//FALSE);
	dwt_enableautoack(DW1000_ACK_RESPONSE_TIME);

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

	// Reset our state because nothing should be in progress if we call init()
	oa_scratch->ranging_state = RSTATE_IDLE;
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

	oa_scratch->final_ack_received = FALSE;

	// LPM now schedules all of our ranging events!
	if(glossy_get_role() != GLOSSY_MASTER){
		lwb_set_sched_request(TRUE);
		lwb_set_sched_callback(oneway_anchor_start_ranging_event);
	}

	return DW1000_NO_ERR;
}

void oneway_reset_anchor_flags(){
	oa_scratch->state = ASTATE_IDLE;

	oa_scratch->final_ack_received = FALSE;

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
	if(oa_scratch->ranging_state != RSTATE_IDLE){
		if (oa_scratch->ranging_broadcast_ss_num == NUM_RANGING_BROADCASTS-1) {
			// This is our last packet to send. Stop the timer so we don't generate
			// more packets.
			timer_stop(oa_scratch->anchor_timer);
	
			// Also update the state to say that we are moving to RX mode
			// to listen for responses from the anchor
			oa_scratch->ranging_state = RSTATE_TRANSITION_TO_ANC_FINAL;
		}
	
		// Go ahead and setup and send a ranging broadcast
		oneway_set_ranging_broadcast_subsequence_settings(TAG, oa_scratch->ranging_broadcast_ss_num);
	
		// Actually send the packet
		send_poll();
		oa_scratch->ranging_broadcast_ss_num += 1;

	} else {
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
}

// Called at the beginning of each listening window for transmitting to
// the tag.
static void ranging_listening_window_task () {
	if (oa_scratch->ranging_state != RSTATE_IDLE){
		
		// Stop after the last of the receive windows
		if (oa_scratch->ranging_listening_window_num == NUM_RANGING_LISTENING_WINDOWS) {
			timer_stop(oa_scratch->anchor_timer);
	
			// Stop the radio
			dwt_forcetrxoff();
	
			// This function finishes up this ranging event.
			report_range();
	
		} else {
	
			// Set the correct listening settings
			oneway_set_ranging_listening_window_settings(TAG, oa_scratch->ranging_listening_window_num, 0);
	
			// Make SURE we're in RX mode!
			dwt_rxenable(0);
	
			// Increment and wait
			oa_scratch->ranging_listening_window_num++;
	
		}

	} else {
		// Check if we are done transmitting to the tag.
		// Ideally we never get here, as an ack from the tag will cause us to stop
		// cycling through listening windows and put us back into a ready state.
		if (oa_scratch->ranging_listening_window_num == NUM_RANGING_LISTENING_WINDOWS+1) {
			// Go back to IDLE
			oa_scratch->state = ASTATE_IDLE;
			// Stop the timer for the window
			timer_stop(oa_scratch->anchor_timer);

			oneway_reset_anchor_flags();

		// Add in a slot to flood packet data back to master
		} else if(oa_scratch->ranging_listening_window_num == NUM_RANGING_LISTENING_WINDOWS) {
			// Set things back up to perpetuate floods on the same channel
			oneway_set_ranging_listening_window_settings(ANCHOR, 0, 0);
			dwt_rxenable(0);

			// Next timestep we will reset
			oa_scratch->ranging_listening_window_num++;
		} else {

			if(!oa_scratch->final_ack_received){

				dwt_forcetrxoff();
		
				// Setup the channel and antenna settings
				oneway_set_ranging_listening_window_settings(ANCHOR,
				                                             oa_scratch->ranging_listening_window_num,
				                                             oa_scratch->pp_anc_final_pkt.final_antenna);
		
				// Prepare the outgoing packet to send back to the
				// tag with our TOAs.
				oa_scratch->pp_anc_final_pkt.ieee154_header_unicast.seqNum = ranval(&(oa_scratch->prng_state)) & 0xFF;
				const uint16_t frame_len = sizeof(struct pp_anc_final);
				// const uint16_t frame_len = sizeof(struct pp_anc_final) - (sizeof(uint64_t)*NUM_RANGING_BROADCASTS);
				dwt_writetxfctrl(frame_len, 0);
		
				// Pick a slot to respond in. Generate a random number and mod it
				// by the number of slots
				uint32_t slot_time = ranval(&(oa_scratch->prng_state)) % (oa_scratch->ranging_operation_config.anchor_reply_window_in_us -
				                                                           dw1000_packet_data_time_in_us(frame_len) -
				                                                           dw1000_preamble_time_in_us());
		
				dwt_setrxaftertxdelay(1);
		
				// Come up with the time to send this packet back to the
				// tag based on the slot we picked.
				uint32_t delay_time = dwt_readsystimestamphi32() +
					DW_DELAY_FROM_US(RANGING_LISTENING_WINDOW_PADDING_US + dw1000_preamble_time_in_us() + slot_time);
		
				delay_time &= 0xFFFFFFFE;
		
				// Set the packet to be transmitted later.
				dw1000_setdelayedtrxtime(delay_time);
		
				// Record the outgoing time in the packet. Do not take calibration into
				// account here, as that is done on all of the RX timestamps.
				oa_scratch->pp_anc_final_pkt.dw_time_sent = (((uint64_t) delay_time) << 8) + dw1000_gettimestampoverflow() + oneway_get_txdelay_from_ranging_listening_window(oa_scratch->ranging_listening_window_num);
		
				// Send the response packet
				// TODO: handle if starttx errors. I'm not sure what to do about it,
				//       other than just wait for the next slot.
				dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
				dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
				dwt_writetxdata(frame_len, (uint8_t*) &(oa_scratch->pp_anc_final_pkt), 0);
			}

			oa_scratch->ranging_listening_window_num++;
		}
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
	glossy_process_txcallback();

	if (txd->event == DWT_SIG_TX_DONE) {
		// Packet was sent successfully

		// Check which state we are in to decide what to do.
		// We use TX_callback because it will get called after we have sent
		// all of the broadcast packets. (Now of course we will get this
		// callback multiple times, but that is ok.)
		if (oa_scratch->ranging_state == RSTATE_TRANSITION_TO_ANC_FINAL) {
			// At this point we have sent all of our ranging broadcasts.
			// Now we move to listening for responses from anchors.
			oa_scratch->ranging_state = RSTATE_LISTENING;

			// Init some state
			oa_scratch->ranging_listening_window_num = 0;
			oa_scratch->anchor_response_count = 0;

			// Start a timer to switch between the windows
			timer_start(oa_scratch->anchor_timer, RANGING_LISTENING_WINDOW_US + RANGING_LISTENING_WINDOW_PADDING_US*2, ranging_listening_window_task);

		} else {
			// We don't need to do anything on TX done for any other states
		}


	} else {
		// Some error occurred, don't just keep trying to send packets.
		timer_stop(oa_scratch->anchor_timer);
	}
}

// Called when the radio has received a packet.
static void anchor_rxcallback (const dwt_callback_data_t *rxd) {

	timer_disable_interrupt(oa_scratch->anchor_timer);

	if (rxd->event == DWT_SIG_RX_OKAY) {

		// First check to see if this is an acknowledgement...
		// If so, we can stop sending ranging responses
		if((rxd->fctrl[0] & 0x03) == 0x02){  //This bit says whether this was an ack or not
			uint8_t cur_seq_num;
			dwt_readrxdata(&cur_seq_num, 1, 2);

			// Check to see if the sequence number matches the outgoing packet
			if(cur_seq_num == oa_scratch->pp_anc_final_pkt.ieee154_header_unicast.seqNum)
				oa_scratch->final_ack_received = TRUE;
		} else {

			// Read in parameters of this packet reception
			uint8_t  buf[ONEWAY_ANCHOR_MAX_RX_PKT_LEN];
			uint64_t dw_rx_timestamp;
			uint8_t  broadcast_message_type, unicast_message_type;

			// Get the received time of this packet first
			dw_rx_timestamp = dw1000_readrxtimestamp();

			// Get the actual packet bytes
			dwt_readrxdata(buf, MIN(ONEWAY_ANCHOR_MAX_RX_PKT_LEN, rxd->datalength), 0);

			// We process based on the first byte in the packet. How very active
			// message like...
			broadcast_message_type = buf[offsetof(struct pp_tag_poll, message_type)];
			unicast_message_type = buf[offsetof(struct pp_anc_final, message_type)];

			if (broadcast_message_type == MSG_TYPE_PP_NOSLOTS_TAG_POLL) {
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
						oa_scratch->pp_anc_final_pkt.first_rxd_toa = dw_rx_timestamp - oneway_get_rxdelay_from_subsequence(ANCHOR, oa_scratch->ranging_broadcast_ss_num);
						oa_scratch->pp_anc_final_pkt.first_rxd_idx = oa_scratch->ranging_broadcast_ss_num;
						oa_scratch->pp_anc_final_pkt.TOAs[oa_scratch->ranging_broadcast_ss_num] =
							(dw_rx_timestamp - oneway_get_rxdelay_from_subsequence(ANCHOR, oa_scratch->ranging_broadcast_ss_num)) & 0xFFFF;
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
						//dwt_rxenable(0);
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
								(dw_rx_timestamp - oneway_get_rxdelay_from_subsequence(ANCHOR, oa_scratch->ranging_broadcast_ss_num)) & 0xFFFF;
							oa_scratch->pp_anc_final_pkt.last_rxd_toa = dw_rx_timestamp - oneway_get_rxdelay_from_subsequence(ANCHOR, oa_scratch->ranging_broadcast_ss_num);
							oa_scratch->pp_anc_final_pkt.last_rxd_idx = oa_scratch->ranging_broadcast_ss_num;

							// Update the statistics we keep about which antenna
							// receives the most packets from the tag
							uint8_t recv_antenna_index = oneway_subsequence_number_to_antenna(ANCHOR, oa_scratch->ranging_broadcast_ss_num);
							oa_scratch->anchor_antenna_recv_num[recv_antenna_index]++;

						} else {
							// Some how we got out of sync with the tag. Ignore the
							// range and catch up.
							oa_scratch->ranging_broadcast_ss_num = rx_poll_pkt->subsequence;
						}

						// Regardless, it's a good idea to immediately call the subsequence task and restart the timer
						timer_reset(oa_scratch->anchor_timer, RANGING_BROADCASTS_PERIOD_US-120); // Magic number calculated from timing
						//ranging_broadcast_subsequence_task();
						//timer_reset(oa_scratch->anchor_timer, 0);

						//// Check to see if we got the last of the ranging broadcasts
						//if (oa_scratch->ranging_broadcast_ss_num == oa_scratch->ranging_operation_config.reply_after_subsequence) {
						//	// We did!
						//	ranging_listening_window_setup();
						//}

					} else {
						// Not the same tag, ignore
					}
				} else {
					// We are in some other state, not sure what that means
				}
			} else if(unicast_message_type == MSG_TYPE_PP_NOSLOTS_ANC_FINAL) {
				// This is what we were looking for, an ANC_FINAL packet
				struct pp_anc_final* anc_final;

				if (oa_scratch->anchor_response_count >= MAX_NUM_ANCHOR_RESPONSES) {
					// Nowhere to store this, so we have to ignore this
					return;
				}

				// Continue parsing the received packet
				anc_final = (struct pp_anc_final*) buf;

				// Check that we haven't already received a packet from this anchor.
				// The anchors should check for an ACK and not retransmit, but that
				// could still fail.
				bool anc_already_found = FALSE;
				for (uint8_t i=0; i<oa_scratch->anchor_response_count; i++) {
					if (memcmp(oa_scratch->anchor_responses[i].anchor_addr, anc_final->ieee154_header_unicast.sourceAddr, EUI_LEN) == 0) {
						anc_already_found = TRUE;
						break;
					}
				}

				// Only save this response if we haven't already seen this anchor
				if (!anc_already_found) {

					// Save the anchor address
					memcpy(oa_scratch->anchor_responses[oa_scratch->anchor_response_count].anchor_addr, anc_final->ieee154_header_unicast.sourceAddr, EUI_LEN);

					// Save the anchor's list of when it received the tag broadcasts
					oa_scratch->anchor_responses[oa_scratch->anchor_response_count].tag_poll_first_TOA = anc_final->first_rxd_toa;
					oa_scratch->anchor_responses[oa_scratch->anchor_response_count].tag_poll_first_idx = anc_final->first_rxd_idx;
					oa_scratch->anchor_responses[oa_scratch->anchor_response_count].tag_poll_last_TOA = anc_final->last_rxd_toa;
					oa_scratch->anchor_responses[oa_scratch->anchor_response_count].tag_poll_last_idx = anc_final->last_rxd_idx;
					memcpy(oa_scratch->anchor_responses[oa_scratch->anchor_response_count].tag_poll_TOAs, anc_final->TOAs, sizeof(anc_final->TOAs));

					// Save the antenna the anchor chose to use when responding to us
					oa_scratch->anchor_responses[oa_scratch->anchor_response_count].anchor_final_antenna_index = anc_final->final_antenna;

					// Save when the anchor sent the packet we just received
					oa_scratch->anchor_responses[oa_scratch->anchor_response_count].anc_final_tx_timestamp = anc_final->dw_time_sent;

					// Save when we received the packet.
					// We have already handled the calibration values so
					// we don't need to here.
					oa_scratch->anchor_responses[oa_scratch->anchor_response_count].anc_final_rx_timestamp = dw_rx_timestamp - oneway_get_rxdelay_from_ranging_listening_window(oa_scratch->ranging_listening_window_num - 1);

					// Also need to save what window we are in when we received
					// this packet. This is used so we know all of the settings
					// that were used when this packet was sent to us.
					oa_scratch->anchor_responses[oa_scratch->anchor_response_count].window_packet_recv = oa_scratch->ranging_listening_window_num - 1;

					// Increment the number of anchors heard from
					oa_scratch->anchor_response_count++;
				}

			} else {
				// We do want to enter RX mode again, however
				//dwt_rxenable(0);
				// Other message types go here, if they get added
				if(broadcast_message_type == MSG_TYPE_PP_GLOSSY_SYNC || broadcast_message_type == MSG_TYPE_PP_GLOSSY_SCHED_REQ || broadcast_message_type == MSG_TYPE_PP_RANGING_FLOOD)
					glossy_sync_process(dw_rx_timestamp-oneway_get_rxdelay_from_subsequence(ANCHOR, 0), buf);
			}
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

	timer_enable_interrupt(oa_scratch->anchor_timer);
}

// This starts a ranging event by causing the tag to send a series of
// ranging broadcasts.
dw1000_err_e oneway_anchor_start_ranging_event () {
	dw1000_err_e err;

	if (oa_scratch->ranging_state != RSTATE_IDLE) {
		// Cannot start a ranging event if we are currently busy with one.
		return DW1000_BUSY;
	}

	// Move to the broadcast state
	oa_scratch->ranging_state = RSTATE_BROADCASTS;

	// Clear state that we keep for each ranging event
	memset(oa_scratch->pp_anc_final_pkt.TOAs, 0, sizeof(oa_scratch->pp_anc_final_pkt.TOAs));
	oa_scratch->ranging_broadcast_ss_num = 0;

	// Start a timer that will kick off the broadcast ranging events
	timer_start(oa_scratch->anchor_timer, RANGING_BROADCASTS_PERIOD_US, ranging_broadcast_subsequence_task);

	return DW1000_NO_ERR;
}

// Send one of the ranging broadcast packets.
// After it sends the last of the subsequence this function automatically
// puts the DW1000 in RX mode.
static void send_poll () {
	int err;

	// Record the packet length to send to DW1000
	uint16_t tx_len = sizeof(struct pp_tag_poll);

	// Vary reply window length depending on baudrate and preamble length
	//ot_scratch->pp_tag_poll_pkt.anchor_reply_window_in_us = RANGING_LISTENING_WINDOW_US + dw1000_preamble_time_in_us() + dw1000_packet_data_time_in_us(sizeof(struct pp_anc_final));

	// Setup what needs to change in the outgoing packet
	oa_scratch->pp_tag_poll_pkt.header.seqNum++;
	oa_scratch->pp_tag_poll_pkt.subsequence = oa_scratch->ranging_broadcast_ss_num;

	// Make sure we're out of RX mode before attempting to transmit
	dwt_forcetrxoff();

	// Tell the DW1000 about the packet
	dwt_writetxfctrl(tx_len, 0);

	// Setup the time the packet will go out at, and save that timestamp
	uint32_t delay_time = dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(tx_len);
	delay_time &= 0xFFFFFFFE; //Make sure last bit is zero
	dw1000_setdelayedtrxtime(delay_time);

	// Take the TX+RX delay into account here by adding it to the time stamp
	// of each outgoing packet.
	oa_scratch->ranging_broadcast_ss_send_times[oa_scratch->ranging_broadcast_ss_num] =
		(((uint64_t) delay_time) << 8) + dw1000_gettimestampoverflow() + oneway_get_txdelay_from_subsequence(TAG, oa_scratch->ranging_broadcast_ss_num);

	// Write the data
	dwt_writetxdata(tx_len, (uint8_t*) &(oa_scratch->pp_tag_poll_pkt), 0);

	// Start the transmission
	if (oa_scratch->ranging_broadcast_ss_num == NUM_RANGING_BROADCASTS-1) {
		// This is the last broadcast ranging packet, so we want to transition
		// to RX mode after this packet to receive the responses from the anchors.
		dwt_setrxaftertxdelay(1); // us
		err = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
	} else {
		err = dwt_starttx(DWT_START_TX_DELAYED);
	}

	// MP bug - TX antenna delay needs reprogramming as it is not preserved
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);

	if (err != DWT_SUCCESS) {
		// This likely means our delay was too short when sending this packet.
		// TODO: do something here...
	}
}

// Once we have heard from all of the anchors, calculate range.
static void report_range () {
	// New state
	oa_scratch->ranging_state = RSTATE_CALCULATE_RANGE;

	// Start the ranging flood back to master
	oa_scratch->ranging_state = RSTATE_IDLE;
	oa_scratch->pp_range_flood_pkt.header = (struct ieee154_header_broadcast) {
			.frameCtrl = {
				0x41,
				0xC8
			},
			.seqNum = 0,
			.panID = {
				POLYPOINT_PANID & 0xFF,
				POLYPOINT_PANID >> 8
			},
			.destAddr = {
				0xFF,
				0xFF
			},
			.sourceAddr = { 0 },
		};
	oa_scratch->pp_range_flood_pkt.message_type = MSG_TYPE_PP_RANGING_FLOOD;
	oa_scratch->pp_range_flood_pkt.xtal_trim = glossy_xtaltrim();
	dw1000_read_eui(oa_scratch->pp_range_flood_pkt.anchor_eui);

	// Re-intialize radio settings to first channel
	oneway_set_ranging_listening_window_settings(TAG, 0, 0);

	// Calculate ranges
	calculate_ranges();

}

// After getting responses from anchors calculate the range to each anchor.
// These values are stored in ot_scratch->ranges_millimeters.
static void calculate_ranges () {
	// Clear array, don't use memset
	for (uint8_t i=0; i<MAX_NUM_ANCHOR_RESPONSES; i++) {
		oa_scratch->ranges_millimeters[i] = INT32_MAX;
	}

	// Iterate through all anchors to calculate the range from the tag
	// to each anchor
	for (uint8_t anchor_index=0; anchor_index<oa_scratch->anchor_response_count; anchor_index++) {
		anchor_responses_t* aresp = &(oa_scratch->anchor_responses[anchor_index]);

		// Prepare the ranging flood for this anchor
		oa_scratch->pp_range_flood_pkt.ranging_eui = aresp->anchor_addr[0];
		memset(oa_scratch->pp_range_flood_pkt.ranges_millimeters, 0xFF, sizeof(oa_scratch->pp_range_flood_pkt.ranges_millimeters));

		// Since the rxd TOAs are compressed to 16 bits, we first need to decompress them back to 64-bit quantities
		uint64_t tag_poll_TOAs[NUM_RANGING_BROADCASTS];
		memset(tag_poll_TOAs, 0, sizeof(tag_poll_TOAs));

		// Get an estimate of clock offset
		double approx_clock_offset = (double)(aresp->tag_poll_last_TOA - aresp->tag_poll_first_TOA) / (double)(oa_scratch->ranging_broadcast_ss_send_times[aresp->tag_poll_last_idx] - oa_scratch->ranging_broadcast_ss_send_times[aresp->tag_poll_first_idx]);

		// First put in the TOA values that are known
		tag_poll_TOAs[aresp->tag_poll_first_idx] = aresp->tag_poll_first_TOA;
		tag_poll_TOAs[aresp->tag_poll_last_idx] = aresp->tag_poll_last_TOA;

		// Then interpolate between the two to find the high 48 bits which fit best
		uint8_t ii;
		for(ii=aresp->tag_poll_first_idx+1; ii < aresp->tag_poll_last_idx; ii++){
			uint64_t estimated_TOA = aresp->tag_poll_first_TOA + (approx_clock_offset*(oa_scratch->ranging_broadcast_ss_send_times[ii] - oa_scratch->ranging_broadcast_ss_send_times[aresp->tag_poll_first_idx]));

			uint64_t actual_TOA = (estimated_TOA & 0xFFFFFFFFFFFF0000ULL) + aresp->tag_poll_TOAs[ii];

			// Make corrections if we're off by more than 0x7FFF
			if(actual_TOA < estimated_TOA - 0x7FFF)
				actual_TOA += 0x10000;
			else if(actual_TOA > estimated_TOA + 0x7FFF)
				actual_TOA -= 0x10000;

			// We're done -- store it...
			tag_poll_TOAs[ii] = actual_TOA;
		}

		// First need to calculate the crystal offset between the anchor and tag.
		// To do this, we need to get the timestamps at the anchor and tag
		// for packets that are repeated. In the current scheme, the first
		// three packets are repeated, where three is the number of channels.
		// If we get multiple matches, we take the average of the clock offsets.
		uint8_t valid_offset_calculations = 0;
		double offset_ratios_sum = 0.0;
		for (uint8_t j=0; j<NUM_RANGING_CHANNELS; j++) {
			uint8_t first_broadcast_index = j;
			uint8_t last_broadcast_index = NUM_RANGING_BROADCASTS - NUM_RANGING_CHANNELS + j;
			uint64_t first_broadcast_send_time = oa_scratch->ranging_broadcast_ss_send_times[first_broadcast_index];
			uint64_t first_broadcast_recv_time = tag_poll_TOAs[first_broadcast_index];
			uint64_t last_broadcast_send_time  = oa_scratch->ranging_broadcast_ss_send_times[last_broadcast_index];
			uint64_t last_broadcast_recv_time  = tag_poll_TOAs[last_broadcast_index];

			// Now lets check that the anchor actually received both of these
			// packets. If it didn't then this isn't valid.
			if (first_broadcast_recv_time == 0 || last_broadcast_recv_time == 0) {
				// A packet was dropped (or the anchor wasn't listening on the
				// first channel). This isn't useful so we skip it.
				continue;
			}

			// Calculate the "multiplier for the crystal offset between tag
			// and anchor".
			// (last_recv-first_recv) / (last_send-first_send)
			double offset_anchor_over_tag_item = ((double) last_broadcast_recv_time - (double) first_broadcast_recv_time) /
				((double) last_broadcast_send_time - (double) first_broadcast_send_time);

			// Add this to the running sum for the average
			offset_ratios_sum += offset_anchor_over_tag_item;
			valid_offset_calculations++;
		}

		// If we didn't get any matching pairs in the first and last rounds
		// then we have to skip this anchor.
		if (valid_offset_calculations == 0) {
			oa_scratch->ranges_millimeters[anchor_index] = ONEWAY_TAG_RANGE_ERROR_NO_OFFSET;
			continue;
		}

		// Calculate the average clock offset multiplier
		double offset_anchor_over_tag = offset_ratios_sum / (double) valid_offset_calculations;

		// Now we need to use the one packet we have from the anchor
		// to calculate a one-way time of flight measurement so that we can
		// account for the time offset between the anchor and tag (i.e. the
		// tag and anchors are not synchronized). We will use this TOF
		// to calculate ranges from all of the other polls the tag sent.
		// To do this, we need to match the anchor_antenna, tag_antenna, and
		// channel between the anchor response and the correct tag poll.
		uint8_t ss_index_matching = oneway_get_ss_index_from_settings(aresp->anchor_final_antenna_index,
		                                                              aresp->window_packet_recv);

		// Exit early if the corresponding broadcast wasn't received
		if(tag_poll_TOAs[ss_index_matching] == 0){
			oa_scratch->ranges_millimeters[anchor_index] = ONEWAY_TAG_RANGE_ERROR_NO_OFFSET;
			continue;
		}

		uint64_t matching_broadcast_send_time = oa_scratch->ranging_broadcast_ss_send_times[ss_index_matching];
		uint64_t matching_broadcast_recv_time = tag_poll_TOAs[ss_index_matching];
		uint64_t response_send_time  = aresp->anc_final_tx_timestamp;
		uint64_t response_recv_time  = aresp->anc_final_rx_timestamp;

		double two_way_TOF = (((double) response_recv_time - (double) matching_broadcast_send_time)*offset_anchor_over_tag) -
			((double) response_send_time - (double) matching_broadcast_recv_time);
		double one_way_TOF = two_way_TOF / 2.0;


		// Declare an array for sorting the ranges.
		int distances_millimeters[NUM_RANGING_BROADCASTS] = {0};
		uint8_t num_valid_distances = 0;

		// Next we calculate the TOFs for each of the poll messages the tag sent.
		for (uint8_t broadcast_index=0; broadcast_index<NUM_RANGING_BROADCASTS; broadcast_index++) {
			uint64_t broadcast_send_time = oa_scratch->ranging_broadcast_ss_send_times[broadcast_index];
			uint64_t broadcast_recv_time = tag_poll_TOAs[broadcast_index];

			// Check that the anchor actually received the tag broadcast.
			// We use 0 as a sentinel for the anchor not receiving the packet.
			if (broadcast_recv_time == 0) {
				continue;
			}

			// We use the reference packet (that we used to calculate one_way_TOF)
			// to compensate for the unsynchronized clock.
			int64_t broadcast_anchor_offset = (int64_t) broadcast_recv_time - (int64_t) matching_broadcast_recv_time;
			int64_t broadcast_tag_offset = (int64_t) broadcast_send_time - (int64_t) matching_broadcast_send_time;
			double TOF = (double) broadcast_anchor_offset - (((double) broadcast_tag_offset) * offset_anchor_over_tag) + one_way_TOF;

			int distance_millimeters = dwtime_to_millimeters(TOF);

			// Check that the distance we have at this point is at all reasonable
			if (distance_millimeters >= MIN_VALID_RANGE_MM && distance_millimeters <= MAX_VALID_RANGE_MM) {
				// Add to the ranging flood 
				oa_scratch->pp_range_flood_pkt.ranges_millimeters[broadcast_index] = distance_millimeters;

				// Add this to our sorted array of distances
				insert_sorted(distances_millimeters, distance_millimeters, num_valid_distances);
				num_valid_distances++;
			}
		}

		// Check to make sure that we got enough ranges from this anchor.
		// If not, we just skip it.
		if (num_valid_distances < MIN_VALID_RANGES_PER_ANCHOR) {
			oa_scratch->ranges_millimeters[anchor_index] = ONEWAY_TAG_RANGE_ERROR_TOO_FEW_RANGES;
			continue;
		}


		// Now that we have all of the calculated ranges from all of the tag
		// broadcasts we can calculate some percentile range.
		uint8_t bot = (num_valid_distances*RANGE_PERCENTILE_NUMERATOR)/RANGE_PERCENTILE_DENOMENATOR;
		uint8_t top = bot+1;
		// bot represents the whole index of the item at the percentile.
		// Then we are going to use the remainder decimal portion to get
		// a scaled value to add to that base. And we are going to do this
		// without floating point, so buckle up.
		// EXAMPLE: if the 90th percentile would be index 3.4, we do:
		//                  distances[3] + 0.4*(distances[4]-distances[3])
		int32_t result = distances_millimeters[bot] +
			(((distances_millimeters[top]-distances_millimeters[bot]) * ((RANGE_PERCENTILE_NUMERATOR*num_valid_distances)
			 - (bot*RANGE_PERCENTILE_DENOMENATOR))) / RANGE_PERCENTILE_DENOMENATOR);

		// Save the result
		oa_scratch->ranges_millimeters[anchor_index] = result;
		// ot_scratch->ranges_millimeters[anchor_index] = (int32_t) one_way_TOF;
		// ot_scratch->ranges_millimeters[anchor_index] = dm;
		// ot_scratch->ranges_millimeters[anchor_index] = distances_millimeters[bot];
		// ot_scratch->ranges_millimeters[anchor_index] = ot_scratch->ranging_broadcast_ss_send_times[0];
		// ot_scratch->ranges_millimeters[anchor_index] = ss_index_matching;
		// ot_scratch->ranges_millimeters[anchor_index] = num_valid_distances;
		if (oa_scratch->ranges_millimeters[anchor_index] == INT32_MAX) {
			oa_scratch->ranges_millimeters[anchor_index] = ONEWAY_TAG_RANGE_ERROR_MISC;
		}

		//Cancel out calibration bias
		oa_scratch->ranges_millimeters[anchor_index] -= CAL_BIAS;

		// Initiate the flood
		uint16_t frame_len = sizeof(struct pp_range_flood);
		dwt_writetxfctrl(frame_len, 0);
		dwt_setrxaftertxdelay(1);
		dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
		dwt_writetxdata(sizeof(struct pp_range_flood), (uint8_t*) &(oa_scratch->pp_range_flood_pkt), 0);
		dwt_starttx(DWT_START_TX_IMMEDIATE);
		uDelay(415);

	}
}
