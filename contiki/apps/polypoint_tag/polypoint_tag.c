
#include "contiki.h"
#include "sys/rtimer.h"
#include "dev/leds.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "dw1000.h"
#include "dev/ssi.h"
#include "cpu/cc2538/lpm.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "polypoint_common.h"

/*---------------------------------------------------------------------------*/
PROCESS(polypoint_tag, "Polypoint-tag");
AUTOSTART_PROCESSES(&polypoint_tag);
static char subsequence_task(struct rtimer *rt, void* ptr);
static struct rtimer subsequence_timer;
static bool subsequence_timer_fired = false;
/* virtualized in app timer instead
static char substate_task(struct rtimer *rt, void* ptr);
static struct rtimer substate_timer;
*/
static bool substate_timer_fired = false;
/*---------------------------------------------------------------------------*/

#define TAG_EUI 0

/**************
 * GLOBAL STATE
 */

static uint8_t global_round_num = 0xFF;
static uint8_t global_subseq_num = 0xFF;

static float global_distances[NUM_ANCHORS*NUM_MEASUREMENTS];

static struct ieee154_bcast_msg bcast_msg;

static void send_poll(){
	int err;

	//Reset all the tRRs at the beginning of each poll event
	memset(bcast_msg.tRR, 0, sizeof(bcast_msg.tRR));

	// Through tSP (tSF is field after) then +2 for FCS
	uint16_t tx_frame_length = offsetof(struct ieee154_bcast_msg, tSF) + 2;
	memset(bcast_msg.destAddr, 0xFF, 2);

	bcast_msg.seqNum++;
	bcast_msg.subSeqNum = global_subseq_num;

	// First byte identifies this as a POLL
	bcast_msg.messageType = MSG_TYPE_TAG_POLL;

	// Tell the DW1000 about the packet
	dwt_writetxfctrl(tx_frame_length, 0);

	// We'll get multiple responses, so let them all come in
	//dwt_setrxtimeout(APP_US_TO_DEVICETIMEU32(NODE_DELAY_US*(NUM_ANCHORS+1)));

	// Delay RX?
	dwt_setrxaftertxdelay(1); // us

	uint32_t temp = dwt_readsystimestamphi32();
	//uint32_t delay_time = temp + GLOBAL_PKT_DELAY_UPPER32;
	//(APP_US_TO_DEVICETIMEU32(NODE_DELAY_US) & DELAY_MASK) >> 8
	uint32_t delay_time = temp + (APP_US_TO_DEVICETIMEU32(TAG_SEND_POLL_DELAY_US) >> 8);
	//uint32_t delay_time = dwt_readsystimestamphi32() + GLOBAL_PKT_DELAY_UPPER32;
	delay_time &= 0xFFFFFFFE; //Make sure last bit is zero
	dwt_setdelayedtrxtime(delay_time);
	bcast_msg.tSP = delay_time;

	// Write the data
	dwt_writetxdata(tx_frame_length, (uint8_t*) &bcast_msg, 0);

	// Start the transmission
	err = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);

	// MP bug - TX antenna delay needs reprogramming as it is
	// not preserved
	dwt_settxantennadelay(TX_ANTENNA_DELAY);

	DEBUG_P("now %lu + delay %lli\r\n", temp, GLOBAL_PKT_DELAY_UPPER32);
	if (err == DWT_SUCCESS) {
		DEBUG_P("Sent TAG_POLL  (%u)\r\n", bcast_msg.subSeqNum);
	} else {
		DEBUG_P("Error sending TAG_POLL: %d\r\n", err);
	}
}

// Triggered after a TX
void app_dw1000_txcallback (const dwt_callback_data_t *txd) {
	//NOTE: No need for tx timestamping after-the-fact (everything's done beforehand)
}

// Triggered when we receive a packet
void app_dw1000_rxcallback (const dwt_callback_data_t *rxd) {
	if (rxd->event == DWT_SIG_RX_OKAY) {
		leds_toggle(LEDS_BLUE);
		uint8_t packet_type;
		uint64_t timestamp;
		uint8_t recv_pkt_buf[512];

		// Get the timestamp first
		uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};
		dwt_readrxtimestamp(txTimeStamp);
		timestamp = ((uint64_t) (*((uint32_t*) txTimeStamp))) | (((uint64_t) txTimeStamp[4]) << 32);

		// Get the packet
		dwt_readrxdata(recv_pkt_buf, MIN(512, rxd->datalength), 0);
		packet_type = recv_pkt_buf[offsetof(struct ieee154_anchor_poll_resp, messageType)];

		if (rxd->datalength < offsetof(struct ieee154_anchor_poll_resp, messageType)) {
			DEBUG_P("WARN: Packet too short (%u bytes)\r\n", rxd->datalength);
			return;
		}

		if (packet_type == MSG_TYPE_ANC_RESP) {
			struct ieee154_anchor_poll_resp* msg_ptr;
			msg_ptr = (struct ieee154_anchor_poll_resp*) recv_pkt_buf;

			uint8_t anchor_id = msg_ptr->anchorID;

			DEBUG_P("ANC_RESP from %u\r\n", anchor_id);
			if(anchor_id >= NUM_ANCHORS) anchor_id = NUM_ANCHORS;

			// Need to actually fill out the packet
			bcast_msg.tRR[anchor_id-1] = timestamp;
		} else if (packet_type == MSG_TYPE_ANC_FINAL) {
			struct ieee154_anchor_final_msg* final_msg_ptr;
			final_msg_ptr = (struct ieee154_anchor_final_msg*) recv_pkt_buf;

			DEBUG_P("ANC_FINAL from %u\r\n", final_msg_ptr->anchorID);

			int offset_idx = (final_msg_ptr->anchorID-1)*NUM_MEASUREMENTS;
#ifdef ANC_FINAL_PERCENTILE_ONLY
			global_distances[offset_idx] = final_msg_ptr->distanceHist[0];
#else
			memcpy(
					&global_distances[offset_idx],
					final_msg_ptr->distanceHist,
					sizeof(final_msg_ptr->distanceHist)
					);
#endif
		} else {
			DEBUG_P("*** ERR: RX Unknown packet type: 0x%X\r\n", packet_type);
		}
	} else {
		DEBUG_P("*** ERR: rxd->event unknown: 0x%X\r\n", rxd->event);
	}
}


void app_init(void) {
	DEBUG_P("\r\n### APP INIT\r\n");
	int err = app_dw1000_init(TAG, TAG_EUI, app_dw1000_txcallback, app_dw1000_rxcallback);
	if (err == -1)
		leds_on(LEDS_RED);
	else
		leds_off(LEDS_RED);

	// Setup the constants in the outgoing packet
	bcast_msg.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
	bcast_msg.frameCtrl[1] = 0xC8; // ext addr
	bcast_msg.panID[0] = DW1000_PANID & 0xff;
	bcast_msg.panID[1] = DW1000_PANID >> 8;
	bcast_msg.seqNum = 0;
	bcast_msg.subSeqNum = 0;

	// Set more packet constants
	dw1000_populate_eui(bcast_msg.sourceAddr, TAG_EUI);
	memset(bcast_msg.destAddr, 0xFF, 2);
}



/*
 * NOTE: Something is funny in the rtimer implementation (maybe a 2538 platform
 * issue?) Can't have two running rtimers, so we do the virtualization on the
 * app side.

static char subsequence_task(struct rtimer *rt, void* ptr){
	rtimer_clock_t next_start_time;
	rtimer_clock_t now = RTIMER_NOW();

	if (global_subseq_num == NUM_MEASUREMENTS)
		next_start_time = now + RT_SEQUENCE_PERIOD;
	else
		next_start_time = now + RT_SUBSEQUENCE_PERIOD;

	// In both cases, the substate timer is the same for anchor responses
	rtimer_set(&substate_timer, now + RT_ANCHOR_RESPONSE_WINDOW,
			1, (rtimer_callback_t)substate_task, NULL);

	rtimer_set(&subsequence_timer, next_start_time, 1,
			(rtimer_callback_t)subsequence_task, ptr);

	DEBUG_P("in subsequence_task\r\n");
	subsequence_timer_fired = true;
	process_poll(&polypoint_tag);
	return 1;
}

static char substate_task(struct rtimer *rt, void* ptr) {
	DEBUG_P("in substate_task\r\n");
	substate_timer_fired = true;
	process_poll(&polypoint_tag);
	return 1;
}
*/

static char subsequence_task(struct rtimer *rt, void* ptr){
	static bool start_of_new_subseq = true;
	static rtimer_clock_t subseq_start;
	rtimer_clock_t now = RTIMER_NOW();

	if (start_of_new_subseq) {
		start_of_new_subseq = false;
		subseq_start = now;
		subsequence_timer_fired = true;
		//DEBUG_P("subseq_start; subseq fire\r\n"); too fast to print

		// Need to set substate timer, same in all cases
		rtimer_set(rt, subseq_start + RTIMER_SECOND*(TAG_FINAL_DELAY_US/1e6),
				1, (rtimer_callback_t)subsequence_task, NULL);
	} else {
		start_of_new_subseq = true;
		substate_timer_fired = true;
		//DEBUG_P("substate fire\r\n"); too fast to print here

		if (global_subseq_num < NUM_MEASUREMENTS) {
			rtimer_set(rt, subseq_start + RTIMER_SECOND*(SUBSEQUENCE_PERIOD_US/1e6),
				1, (rtimer_callback_t)subsequence_task, NULL);
		} else {
			rtimer_set(rt, subseq_start + RT_FINAL_PRINTF_DURATION,
				1, (rtimer_callback_t)subsequence_task, NULL);
		}
	}

	process_poll(&polypoint_tag);
	return 1;
}

PROCESS_THREAD(polypoint_tag, ev, data) {
	PROCESS_BEGIN();

	leds_on(LEDS_ALL);

	//Keep things from going to sleep
	lpm_set_max_pm(0);


	dw1000_init();
#ifdef DW_DEBUG
	printf("Inited the DW1000 driver (setup SPI)\r\n");
#endif

	leds_off(LEDS_ALL);

#ifdef DW_DEBUG
	printf("Setting up DW1000 as a Tag.\r\n");
#endif
	app_init();
	bcast_msg.roundNum = ++global_round_num;

	/* PROGRAM BEHAVIOR
	 * Loop through states based on global_subseq_num. State execution is
	 * split, partially in the timer callback (interrupt context) and mostly
	 * in the main loop context. Item's with '+' are in the interrupt handler
	 * and '-' are in the main loop.
	 *
	 * states 0 .. NUM_MEASUREMENTS-1:
	 *   + Start t0: RT_SUBSEQUENCE_PERIOD timer
	 *   + Start t1: RT_ANCHOR_RESPONSE_WINDOW
	 *   - Choose antenna and channel
	 *   - Send broadcast TAG_POLL
	 *     -- Expect ANC_RESP's from each anchor
	 *
	 *   # Sub-state triggered by t1 expiration
	 *   - Send broadcast TAG_FINAL
	 *     -- Expect no response
	 *
	 * state NUM_MEASUREMENTS:
	 *   + Start t0: RT_SEQUENCE_PERIOD timer
	 *   + Start t1: RT_ANCHOR_RESPONSE_WINDOW
	 *   - Keep last ant/ch selection
	 *     -- Expect ANC_FINAL's from each anchor
	 *
	 *   # Sub-state triggered by t1 expiration
	 *   - Print result of all measurements
	 *   - Reset the world
	 *     -- sets state to 0
	 *
	 */

	// Kickstart things at the beginning of the loop
	global_subseq_num = 0;

	rtimer_init();
	rtimer_set(&subsequence_timer, RTIMER_NOW() + RTIMER_SECOND, 1,
			(rtimer_callback_t)subsequence_task, NULL);

	DEBUG_B4_INIT;
	DEBUG_B5_INIT;
	DEBUG_B4_LOW;
	DEBUG_B5_LOW;

	while(1) {
		PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL);

		if (! (subsequence_timer_fired ^ substate_timer_fired) ) {
			DEBUG_P("Timer mismatch. Everything's probably fucked.\r\n");
			DEBUG_P("subsequence_timer: %d\r\n", subsequence_timer_fired);
			DEBUG_P("   substate_timer: %d\r\n", substate_timer_fired);
		}

		if(global_subseq_num < NUM_MEASUREMENTS) {
			if (subsequence_timer_fired) {
				subsequence_timer_fired = false;
				DEBUG_B4_LOW;
				DEBUG_B5_LOW;

				set_subsequence_settings(global_subseq_num, TAG);

				//Make sure we're out of rx mode before attempting to transmit
				dwt_forcetrxoff();

				send_poll(); // sets RX mode
			} else {
				substate_timer_fired = false;
				DEBUG_B4_HIGH;
				DEBUG_B5_LOW;

				//Make sure we're out of rx mode before attempting to transmit
				dwt_forcetrxoff();

				uint32_t temp = dwt_readsystimestamphi32();
				//uint32_t delay_time = temp + GLOBAL_PKT_DELAY_UPPER32;
				uint32_t delay_time = temp + (APP_US_TO_DEVICETIMEU32(TAG_SEND_FINAL_DELAY_US) >> 8);
				//uint32_t delay_time = dwt_readsystimestamphi32() + GLOBAL_PKT_DELAY_UPPER32;
				delay_time &= 0xFFFFFFFE;
				dwt_setdelayedtrxtime(delay_time);

				uint16_t tx_frame_length = sizeof(bcast_msg);
				dwt_writetxfctrl(tx_frame_length, 0);

				bcast_msg.seqNum++;
				bcast_msg.messageType = MSG_TYPE_TAG_FINAL;
				bcast_msg.tSF = delay_time;

				dwt_writetxdata(tx_frame_length, (uint8_t*) &bcast_msg, 0);
				int err = dwt_starttx(DWT_START_TX_DELAYED);
				dwt_settxantennadelay(TX_ANTENNA_DELAY);

				DEBUG_P("now %lu + delay %lli\r\n", temp, GLOBAL_PKT_DELAY_UPPER32);
				if (err) {
					DEBUG_P("Error sending final message\r\n");
				} else {
					DEBUG_P("Sent TAG_FINAL (%u)\n", bcast_msg.subSeqNum);
				}

				int i;
				for (i=0; i<NUM_ANCHORS; i++) {
					DEBUG_P("\tbcast_msg.tRR[%02d] = %llu\r\n", i, bcast_msg.tRR[i]);
				}
	
				global_subseq_num++;
			}
		} else {
			if (subsequence_timer_fired) {
				subsequence_timer_fired = false;
				DEBUG_B4_LOW;
				DEBUG_B5_HIGH;

				dwt_rxenable(0);

				DEBUG_P("In wait period expecting ANC_FINALs\r\n");

				// no-op, anchors should begin sending ANC_FINAL
				// messages during this slot; the rx handler will
				// fill out the results array
			} else {
				substate_timer_fired = false;
				DEBUG_B4_HIGH;
				DEBUG_B5_HIGH;

				// n.b. This loop of printfs takes 140-180 ms to execute
				int ii;
				for(ii=0; ii < NUM_ANCHORS; ii++){
					int offset_idx = ii*NUM_MEASUREMENTS;
#ifdef ANC_FINAL_PERCENTILE_ONLY
					int dist_times_1000 = (int)(global_distances[offset_idx]*1000);
					printf("%d %d.%d,\t", ii+1, dist_times_1000/1000,dist_times_1000%1000);
#else
					int jj;
					printf("tagstart %d\r\n",ii+1);
					for(jj=0; jj < NUM_MEASUREMENTS; jj++){
						int dist_times_1000 = (int)(global_distances[offset_idx+jj]*1000);
						printf("%d.%d\t",dist_times_1000/1000,dist_times_1000%1000);
					}
					printf("\r\n");
#endif
				}
#ifdef ANC_FINAL_PERCENTILE_ONLY
				printf("\r\n");
#else
				printf("done\r\n");
#endif
				memset(global_distances,0,sizeof(global_distances));
				bcast_msg.roundNum = ++global_round_num;

				global_subseq_num = 0;

				DEBUG_B5_LOW; //measure printf duration
			}
		}
	}

	PROCESS_END();
}
