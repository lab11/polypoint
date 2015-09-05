#include <string.h>

#include "deca_device_api.h"
#include "deca_regs.h"

#include "timer.h"
#include "dw1000.h"
#include "dw1000_tag.h"
#include "firmware.h"
#include "operation_api.h"

// Our timer object that we use for timing packet transmissions
timer_t* _ranging_broadcast_timer;

tag_state_e _tag_state = TSTATE_IDLE;

// Which subsequence slot we are on when transmitting broadcast packets
// for ranging.
static uint8_t _ranging_broadcast_ss_num = 0;

// Which slot we are in when receiving packets from the anchor.
static uint8_t _ranging_listening_window_num = 0;

// Array of when we sent each of the broadcast ranging packets
static uint64_t _ranging_broadcast_ss_send_times[NUM_RANGING_BROADCASTS] = {0};

// How many anchor responses we have gotten
static uint8_t _anchor_response_count = 0;

// Array of when we received ANC_FINAL packets and from whom
static anchor_responses_t _anchor_responses[MAX_NUM_ANCHOR_RESPONSES];

// These are the ranges we have calculated to a series of anchors.
// They use the same index as the _anchor_responses array.
// Invalid ranges are marked with INT32_MAX.
int32_t _ranges_millimeters[MAX_NUM_ANCHOR_RESPONSES] = {0};

// Prepopulated struct of the outgoing broadcast poll packet.
static struct pp_tag_poll pp_tag_poll_pkt = {
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

// Functions
static void send_poll ();
static void ranging_broadcast_subsequence_task ();
static void ranging_listening_window_task ();
static void calculate_ranges ();
static void report_range ();

void dw1000_tag_init () {

	uint8_t eui_array[8];

	// Allow data and ack frames
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	// Set this node's ID and the PAN ID for our DW1000 ranging system
	dw1000_read_eui(eui_array);
	dwt_seteui(eui_array);
	dwt_setpanid(POLYPOINT_PANID);

	// Setup parameters of how the radio should work
	dwt_setautorxreenable(TRUE);
	dwt_setdblrxbuffmode(TRUE);
	dwt_enableautoack(DW1000_ACK_RESPONSE_TIME);

	// Configure sleep
	{
		int mode = DWT_LOADUCODE    |
		           DWT_PRESRV_SLEEP |
		           DWT_CONFIG       |
		           DWT_TANDV;
		if (dwt_getldotune() != 0) {
			// If we need to use LDO tune value from OTP kick it after sleep
			mode |= DWT_LOADLDO;
		}

		// NOTE: on the EVK1000 the DEEPSLEEP is not actually putting the
		// DW1000 into full DEEPSLEEP mode as XTAL is kept on
		dwt_configuresleep(mode, DWT_WAKE_CS | DWT_SLP_EN);
	}

	// Put source EUI in the pp_tag_poll packet
	dw1000_read_eui(pp_tag_poll_pkt.header.sourceAddr);

	// Create a timer for use when sending ranging broadcast packets
	_ranging_broadcast_timer = timer_init();

	// Make SPI fast now that everything has been setup
	dw1000_spi_fast();
}

// This starts a ranging event by causing the tag to send a series of
// ranging broadcasts.
dw1000_err_e dw1000_tag_start_ranging_event () {
	if (_tag_state != TSTATE_IDLE) {
		// Cannot start a ranging event if we are currently busy with one.
		return DW1000_BUSY;
	}

	// Move to the broadcast state
	_tag_state = TSTATE_BROADCASTS;

	// Clear state that we keep for each ranging event
	memset(_ranging_broadcast_ss_send_times, 0, sizeof(_ranging_broadcast_ss_send_times));
	_ranging_broadcast_ss_num = 0;

	// Start a timer that will kick off the broadcast ranging events
	timer_start(_ranging_broadcast_timer, RANGING_BROADCASTS_PERIOD_US, ranging_broadcast_subsequence_task);

	return DW1000_NO_ERR;
}


void dw1000_tag_txcallback (const dwt_callback_data_t *data) {

	if (data->event == DWT_SIG_TX_DONE) {
		// Packet was sent successfully

		// Check which state we are in to decide what to do.
		// We use TX_callback because it will get called after we have sent
		// all of the broadcast packets. (Now of course we will get this
		// callback multiple times, but that is ok.)
		if (_tag_state == TSTATE_TRANSITION_TO_ANC_FINAL) {
			// At this point we have sent all of our ranging broadcasts.
			// Now we move to listening for responses from anchors.
			_tag_state = TSTATE_LISTENING;

			// Init some state
			_ranging_listening_window_num = 0;
			_anchor_response_count = 0;

			// Start a timer to switch between the windows
			timer_start(_ranging_broadcast_timer, RANGING_LISTENING_WINDOW_US, ranging_listening_window_task);

		} else {
			// We don't need to do anything on TX done for any other states
		}


	} else {
		timer_stop(_ranging_broadcast_timer);
	}

}

// Called when the tag receives a packet.
void dw1000_tag_rxcallback (const dwt_callback_data_t* rxd) {
	if (rxd->event == DWT_SIG_RX_OKAY) {
		// Everything went right when receiving this packet.
		// We have to process it to ensure that it is a packet we are expecting
		// to get.

		uint64_t dw_rx_timestamp;
		uint8_t  buf[DW1000_TAG_MAX_RX_PKT_LEN];
		uint8_t  message_type;

		// Get the received time of this packet first
		dwt_readrxtimestamp(buf);
		dw_rx_timestamp = DW_TIMESTAMP_TO_UINT64(buf);

		// Get the actual packet bytes
		dwt_readrxdata(buf, MIN(DW1000_TAG_MAX_RX_PKT_LEN, rxd->datalength), 0);
		message_type = buf[offsetof(struct pp_anc_final, message_type)];

		if (message_type == MSG_TYPE_PP_NOSLOTS_ANC_FINAL) {
			// This is what we were looking for, an ANC_FINAL packet
			struct pp_anc_final* anc_final;

			if (_anchor_response_count >= MAX_NUM_ANCHOR_RESPONSES) {
				// Nowhere to store this, so we have to ignore this
				return;
			}

			// Continue parsing the received packet
			anc_final = (struct pp_anc_final*) buf;

			// Check that we haven't already received a packet from this anchor.
			// The anchors should check for an ACK and not retransmit, but that
			// could still fail.
			bool anc_already_found = FALSE;
			for (uint8_t i=0; i<_anchor_response_count; i++) {
				if (memcmp(_anchor_responses[i].anchor_addr, anc_final->ieee154_header_unicast.sourceAddr, EUI_LEN) == 0) {
					anc_already_found = TRUE;
					break;
				}
			}

			// Only save this response if we haven't already seen this anchor
			if (!anc_already_found) {

				// Save the anchor address
				memcpy(_anchor_responses[_anchor_response_count].anchor_addr, anc_final->ieee154_header_unicast.sourceAddr, EUI_LEN);

				// Save the anchor's list of when it received the tag broadcasts
				memcpy(_anchor_responses[_anchor_response_count].tag_poll_TOAs, anc_final->TOAs, sizeof(anc_final->TOAs));

				// Save the antenna the anchor chose to use when responding to us
				_anchor_responses[_anchor_response_count].anchor_final_antenna_index = anc_final->final_antenna;

				// Save when the anchor sent the packet we just received
				_anchor_responses[_anchor_response_count].anc_final_tx_timestamp = ((uint64_t)anc_final->dw_time_sent) << 8;
				// Save when we received the packet
				_anchor_responses[_anchor_response_count].anc_final_rx_timestamp = dw_rx_timestamp;

				// Also need to save what window we are in when we received
				// this packet. This is used so we know all of the settings
				// that were used when this packet was sent to us.
				_anchor_responses[_anchor_response_count].window_packet_recv = _ranging_listening_window_num - 1;

				// Increment the number of anchors heard from
				_anchor_response_count++;
			}

		} else {
			// TAGs don't expect to receive any other types of packets.
		}

	} else {
		// Packet was NOT received correctly. Need to do some re-configuring
		// as things get blown out when this happens. (Because dwt_rxreset
		// within dwt_isr smashes everything without regard.)
		if (rxd->event == DWT_SIG_RX_PHR_ERROR ||
		    rxd->event == DWT_SIG_RX_ERROR ||
		    rxd->event == DWT_SIG_RX_SYNCLOSS ||
		    rxd->event == DWT_SIG_RX_SFDTIMEOUT ||
		    rxd->event == DWT_SIG_RX_PTOTIMEOUT) {
			dw1000_set_ranging_listening_window_settings(TAG, _ranging_listening_window_num, 0, FALSE);
		}
	}

}

// Send one of the ranging broadcast packets.
// After it sends the last of the subsequence this function automatically
// puts the DW1000 in RX mode.
static void send_poll () {
	int err;

	// Record the packet length to send to DW1000
	uint16_t tx_len = sizeof(struct pp_tag_poll);

	// Setup what needs to change in the outgoing packet
	pp_tag_poll_pkt.header.seqNum++;
	pp_tag_poll_pkt.subsequence = _ranging_broadcast_ss_num;

	// Make sure we're out of RX mode before attempting to transmit
	dwt_forcetrxoff();

	// Tell the DW1000 about the packet
	dwt_writetxfctrl(tx_len, 0);

	// Setup the time the packet will go out at, and save that timestamp
	uint32_t delay_time = dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(tx_len);
	delay_time &= 0xFFFFFFFE; //Make sure last bit is zero
	dwt_setdelayedtrxtime(delay_time);
	_ranging_broadcast_ss_send_times[_ranging_broadcast_ss_num] = ((uint64_t) delay_time) << 8;

	// Write the data
	dwt_writetxdata(tx_len, (uint8_t*) &pp_tag_poll_pkt, 0);

	// Start the transmission
	if (_ranging_broadcast_ss_num == NUM_RANGING_BROADCASTS-1) {
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

// This is called for each broadcast ranging subsequence interval where
// the tag sends broadcast packets.
static void ranging_broadcast_subsequence_task () {

	if (_ranging_broadcast_ss_num == NUM_RANGING_BROADCASTS-1) {
		// This is our last packet to send. Stop the timer so we don't generate
		// more packets.
		timer_stop(_ranging_broadcast_timer);

		// Also update the state to say that we are moving to RX mode
		// to listen for responses from the anchor
		_tag_state = TSTATE_TRANSITION_TO_ANC_FINAL;
	}

	// Go ahead and setup and send a ranging broadcast
	dw1000_set_ranging_broadcast_subsequence_settings(TAG, _ranging_broadcast_ss_num, FALSE);

	// Actually send the packet
	send_poll();
	_ranging_broadcast_ss_num += 1;
}

// This is called after the broadcasts have been sent in order to receive
// the responses from the anchors.
static void ranging_listening_window_task () {

	// Stop after the last of the receive windows
	if (_ranging_listening_window_num == NUM_RANGING_LISTENING_WINDOWS) {
		timer_stop(_ranging_broadcast_timer);

		// Stop the radio
		dwt_forcetrxoff();

		// This function finishes up this ranging event.
		report_range();

	} else {

		// Set the correct listening settings
		dw1000_set_ranging_listening_window_settings(TAG, _ranging_listening_window_num, 0, FALSE);

		// Increment and wait
		_ranging_listening_window_num++;

	}
}

// Once we have heard from all of the anchors, calculate range.
static void report_range () {
	// New state
	_tag_state = TSTATE_CALCULATE_RANGE;

	// Calculate ranges
	calculate_ranges();

	// Decide what we should do with these ranges. We can either report
	// these right back to the host, or we can try to get the anchors
	// to calculate location.
	dw1000_report_mode_e report_mode = main_get_report_mode();
	if (report_mode == REPORT_MODE_RANGES) {
		// Just need to send the ranges back to the host. Send the array
		// of ranges to the main application and let it deal with it.
		main_set_ranges(_ranges_millimeters, _anchor_responses);

		// After we do that go back to idle. The state is now the main application's
		// problem.
		_tag_state = TSTATE_IDLE;

	} else if (report_mode == REPORT_MODE_LOCATION) {
		// TODO: implement this
	}
}


// After getting responses from anchors calculate the range to each anchor.
// These values are stored in _ranges_millimeters.
static void calculate_ranges () {
	// Clear array, don't use memset
	for (uint8_t i=0; i<MAX_NUM_ANCHOR_RESPONSES; i++) {
		_ranges_millimeters[i] = INT32_MAX;
	}

	// Iterate through all anchors to calculate the range from the tag
	// to each anchor
	for (uint8_t anchor_index=0; anchor_index<_anchor_response_count; anchor_index++) {
		anchor_responses_t* aresp = &_anchor_responses[anchor_index];

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
			uint64_t first_broadcast_send_time = _ranging_broadcast_ss_send_times[first_broadcast_index];
			uint64_t first_broadcast_recv_time = aresp->tag_poll_TOAs[first_broadcast_index];
			uint64_t last_broadcast_send_time  = _ranging_broadcast_ss_send_times[last_broadcast_index];
			uint64_t last_broadcast_recv_time  = aresp->tag_poll_TOAs[last_broadcast_index];

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
			// continue;
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
		uint8_t ss_index_matching = dw1000_get_ss_index_from_settings(aresp->anchor_final_antenna_index,
		                                                              aresp->window_packet_recv);
		uint64_t matching_broadcast_send_time = _ranging_broadcast_ss_send_times[ss_index_matching];
		uint64_t matching_broadcast_recv_time = aresp->tag_poll_TOAs[ss_index_matching];
		uint64_t response_send_time  = aresp->anc_final_tx_timestamp;
		uint64_t response_recv_time  = aresp->anc_final_rx_timestamp;

		double two_way_TOF = (((double) response_recv_time - (double) matching_broadcast_send_time)*offset_anchor_over_tag) -
			((double) response_send_time - (double) matching_broadcast_recv_time);
		double one_way_TOF = two_way_TOF / 2.0;


		// Declare an array for sorting the ranges.
		int distances_millimeters[NUM_RANGING_BROADCASTS] = {0};
		uint8_t num_valid_distances = 0;

int dm = 0;

		// Next we calculate the TOFs for each of the poll messages the tag sent.
		for (uint8_t broadcast_index=0; broadcast_index<NUM_RANGING_BROADCASTS; broadcast_index++) {
			uint64_t broadcast_send_time = _ranging_broadcast_ss_send_times[broadcast_index];
			uint64_t broadcast_recv_time = aresp->tag_poll_TOAs[broadcast_index];

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
if (dm == 0) dm = distance_millimeters;

			// Check that the distance we have at this point is at all reasonable
			// if (distance_millimeters >= MIN_VALID_RANGE_MM && distance_millimeters <= MAX_VALID_RANGE_MM) {
				// Add this to our sorted array of distances
				insert_sorted(distances_millimeters, distance_millimeters, num_valid_distances);
				num_valid_distances++;
			// }
		}

		// Check to make sure that we got enough ranges from this anchor.
		// If not, we just skip it.
		if (num_valid_distances < MIN_VALID_RANGES_PER_ANCHOR) {
			// continue;
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
		_ranges_millimeters[anchor_index] = result;
		// _ranges_millimeters[anchor_index] = (int32_t) one_way_TOF;
		// _ranges_millimeters[anchor_index] = dm;
		// _ranges_millimeters[anchor_index] = distances_millimeters[bot];
		// _ranges_millimeters[anchor_index] = _ranging_broadcast_ss_send_times[0];
		// _ranges_millimeters[anchor_index] = ss_index_matching;
		// _ranges_millimeters[anchor_index] = num_valid_distances;
		if (_ranges_millimeters[anchor_index] == INT32_MAX) {
			_ranges_millimeters[anchor_index] = 0xBB;
		}
	}
}
