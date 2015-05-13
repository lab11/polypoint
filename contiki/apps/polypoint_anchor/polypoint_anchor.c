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
PROCESS(polypoint_anchor, "Polypoint-anchor");
AUTOSTART_PROCESSES(&polypoint_anchor);
static char subsequence_task(struct rtimer *rt, void* ptr);
static struct rtimer subsequence_timer;
static bool subsequence_timer_fired = false;
/* virtualized in app timer instead
static char substate_task(struct rtimer *rt, void* ptr);
static struct rtimer substate_timer;
*/
static bool substate_timer_fired = false;
/*---------------------------------------------------------------------------*/
// (fwd decl)
static rtimer_clock_t subseq_start_time;
static bool start_of_new_subseq;

// temp
#define TAG_EUI 0

#define ANCHOR_EUI 1

/**************
 * GLOBAL STATE
 */

static uint8_t global_round_num = 0xAA;
static uint8_t global_subseq_num = 0xAA;

static struct ieee154_anchor_poll_resp poll_resp_msg;
static struct ieee154_anchor_final_msg fin_msg;

static uint64_t global_tRP = 0;
static uint32_t global_tSR = 0;

// Triggered after a TX
void app_dw1000_txcallback (const dwt_callback_data_t *txd) {
	//NOTE: No need for tx timestamping after-the-fact (everything's done beforehand)
}

static void insert_sorted(float arr[], float new, unsigned end) {
	unsigned insert_at = 0;
	while ((insert_at < end) && (new > arr[insert_at])) {
		insert_at++;
	}
	if (insert_at == end) {
		arr[insert_at] = new;
	} else {
		while (insert_at <= end) {
			float temp = arr[insert_at];
			arr[insert_at] = new;
			new = temp;
			insert_at++;
		}
	}
}

// Triggered when we receive a packet
void app_dw1000_rxcallback (const dwt_callback_data_t *rxd) {
	int err;

	// The anchor should receive two packets: a POLL from a tag and
	// a FINAL from a tag.

	if (rxd->event == DWT_SIG_RX_OKAY) {
		uint8_t packet_type_byte;
		uint64_t dw_timestamp;
		rtimer_clock_t rt_timestamp;
		uint8_t subseq_num;

		// Get the dw_timestamp first
		uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};
		dwt_readrxtimestamp(txTimeStamp);
		dw_timestamp = ((uint64_t) (*((uint32_t*) txTimeStamp))) | (((uint64_t) txTimeStamp[4]) << 32);

		// Then the contiki RT timestamp
		rt_timestamp = RTIMER_NOW();

		leds_toggle(LEDS_BLUE);

		// Get the packet
		dwt_readrxdata(
				&packet_type_byte,
				1,
				offsetof(struct ieee154_bcast_msg, messageType)
				);
		dwt_readrxdata(
				&global_round_num,
				1,
				offsetof(struct ieee154_bcast_msg, roundNum)
				);
		dwt_readrxdata(
				&subseq_num,
				1,
				offsetof(struct ieee154_bcast_msg, subSeqNum)
				);


		if (packet_type_byte == MSG_TYPE_TAG_POLL) {
			DEBUG_P("TAG_POLL received (remote subseq %u)\r\n", subseq_num);

			// Got POLL
			global_tRP = dw_timestamp;

			// Send response

			// Calculate the delay
			const uint32_t pkt_delay_upper32 = (APP_US_TO_DEVICETIMEU32(ANC_RESP_SEND_TIME_US) & DELAY_MASK) >> 8;
			uint32_t delay_time =
				((uint32_t) (global_tRP >> 8)) +
				pkt_delay_upper32*(ANCHOR_EUI-1) +
				(APP_US_TO_DEVICETIMEU32(ANC_RESP_DELAY_US) >> 8);
			delay_time &= 0xFFFFFFFE;
			global_tSR = delay_time;
			dwt_setdelayedtrxtime(delay_time);

			poll_resp_msg.seqNum++;

			uint16_t tx_frame_length = sizeof(poll_resp_msg);
			// Put at beginning of TX fifo
			dwt_writetxfctrl(tx_frame_length, 0);

			dwt_writetxdata(tx_frame_length, (uint8_t*) &poll_resp_msg, 0);

			// Start delayed TX
			// Hopefully we will receive the FINAL message after this...
			dwt_setrxaftertxdelay(ANC_RESP_SEND_TIME_US*(NUM_ANCHORS - ANCHOR_EUI));
			err = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
			dwt_settxantennadelay(TX_ANTENNA_DELAY);
			if (err) {
#ifdef DW_DEBUG
				uint32_t now = dwt_readsystimestamphi32();
				DEBUG_P("Could not send anchor response\r\n");
				DEBUG_P(" -- sched time %lu, now %lu (diff %lu)\r\n", delay_time, now, now-delay_time);
#endif
			} else {
				DEBUG_P("Send ANC_RESP\r\n");
			}

			if(subseq_num == 0){
				if (global_subseq_num != 0) {
					DEBUG_P("WARN: rx'd a TAG_POLL subseq 0 when local subseq was %u\r\n", global_subseq_num);
					global_subseq_num = 0;
				}

				//Reset all the distance measurements
				memset(fin_msg.distanceHist, 0, sizeof(fin_msg.distanceHist));

				/*
				rtimer_set(&subsequence_timer, RTIMER_NOW() + SUBSEQUENCE_PERIOD - RTIMER_SECOND*0.011805, 1,  //magic number from saleae to approximately line up config times
						(rtimer_callback_t)periodic_task, NULL);
				global_seq_count++;
				*/

				// Kick off the timers that drive the rest of the round
				/* VTIMER HACK
				rtimer_set(&subsequence_timer,
						rt_timestamp + RT_SUBSEQUENCE_PERIOD,
						1,
						(rtimer_callback_t)subsequence_task,
						NULL);
				rtimer_set(&substate_timer,
						rt_timestamp + RT_ANCHOR_RESPONSE_WINDOW+RT_TAG_FINAL_WINDOW,
						1,
						(rtimer_callback_t)substate_task,
						NULL);
				*/
				start_of_new_subseq = false;
				subseq_start_time = rt_timestamp - (RTIMER_SECOND*((TAG_SETTINGS_SETUP_US+TAG_SEND_POLL_DELAY_US)/1e6));;
				rtimer_set(&subsequence_timer,
						subseq_start_time + RTIMER_SECOND*((TAG_FINAL_DELAY_US+ANC_RX_AND_PROC_TAG_FINAL_US)/1e6),
						1,
						(rtimer_callback_t)subsequence_task,
						NULL);
			}
		} else if (packet_type_byte == MSG_TYPE_TAG_FINAL) {
			DEBUG_P("TAG_FINAL received (remote subseq %u)\r\n", subseq_num);

			// Got FINAL
			struct ieee154_bcast_msg bcast_msg;

			// Read the whole packet
			dwt_readrxdata((uint8_t*)&bcast_msg, sizeof(bcast_msg), 0);
			global_round_num = bcast_msg.roundNum;

			//TODO: might need to normalize all times to tSP and tRP
			double tRF = (double)dw_timestamp;
			double tSR = (double)(((uint64_t)global_tSR) << 8);
			double tRR = (double)bcast_msg.tRR[ANCHOR_EUI-1];
			double tSP = (double)(((uint64_t)bcast_msg.tSP) << 8);
			double tSF = (double)(((uint64_t)bcast_msg.tSF) << 8);
			double tRP = (double)global_tRP;

#ifdef DW_DEBUG
			printf("tRF = %llu\r\n", (uint64_t)tRF);
			printf("tSR = %llu\r\n", (uint64_t)tSR);
			printf("tRR = %llu\r\n", (uint64_t)tRR);
			printf("tSP = %llu\r\n", (uint64_t)tSP);
			printf("tSF = %llu\r\n", (uint64_t)tSF);
			printf("tRP = %llu\r\n", (uint64_t)tRP);
#endif

			if(tRF != 0.0 && tSR != 0.0 && tRR != 0.0 && tSP != 0.0 && tSF != 0.0 && tRP != 0.0){
				double aot = (tRF-tRP)/(tSF-tSP);
				double tTOF = (tRF-tSR)-(tSF-tRR)*aot;
				double dist = (tTOF*DWT_TIME_UNITS)/2;

				dist *= SPEED_OF_LIGHT;
				dist += ANCHOR_CAL_LEN;
				dist -= txDelayCal[ANCHOR_EUI*NUM_CHANNELS + subseq_num_to_chan(global_subseq_num, true)];
				DEBUG_P("dist*100 = %d\r\n", (int)(dist*100));
#ifdef SORT_MEASUREMENTS
				insert_sorted(fin_msg.distanceHist, (float)dist, global_subseq_num);
#else
				fin_msg.distanceHist[global_subseq_num] = (float)dist;
#endif
			}

			// Get ready to receive next POLL
			dwt_rxenable(0);
		} else {
#ifdef DW_DEBUG
			printf("*** ERR: RX Unknown packet type: 0x%X\r\n", packet_type_byte);
#endif
		}
	} else {
#ifdef DW_DEBUG
		printf("*** ERR: rxd->event unknown: 0x%X\r\n", rxd->event);
#endif
	}
}

void app_init() {
	DEBUG_P("\r\n### APP INIT\r\n");

	int err = app_dw1000_init(ANCHOR, ANCHOR_EUI, app_dw1000_txcallback, app_dw1000_rxcallback);
	if (err == -1)
		leds_on(LEDS_RED);
	else
		leds_off(LEDS_RED);

	// Setup the constants in the outgoing packet
	poll_resp_msg.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
	poll_resp_msg.frameCtrl[1] = 0xCC; // ext addr
	poll_resp_msg.panID[0] = DW1000_PANID & 0xff;
	poll_resp_msg.panID[1] = DW1000_PANID >> 8;
	poll_resp_msg.seqNum = 0;
	poll_resp_msg.messageType = MSG_TYPE_ANC_RESP;
	poll_resp_msg.anchorID = ANCHOR_EUI;

	// Setup the constants in the outgoing packet
	fin_msg.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
	fin_msg.frameCtrl[1] = 0xCC; // ext addr
	fin_msg.panID[0] = DW1000_PANID & 0xff;
	fin_msg.panID[1] = DW1000_PANID >> 8;
	fin_msg.seqNum = 0;
	fin_msg.messageType = MSG_TYPE_ANC_FINAL;
	fin_msg.anchorID = ANCHOR_EUI;

	// Set more packet constants
	dw1000_populate_eui(poll_resp_msg.sourceAddr, ANCHOR_EUI);
	dw1000_populate_eui(fin_msg.sourceAddr, ANCHOR_EUI);

	// hard code destination for now....
	dw1000_populate_eui(poll_resp_msg.destAddr, TAG_EUI);
	dw1000_populate_eui(fin_msg.destAddr, TAG_EUI);
}


/*
 * NOTE: Something is funny in the rtimer implementation (maybe a 2538 platform
 * issue?) Can't have two running rtimers, so we do the virtualization on the
 * app side.

static char subsequence_task(struct rtimer *rt, void* ptr){
	if (global_subseq_num == NUM_MEASUREMENTS) {
		// no-op
	} else {
		rtimer_set(rt,
				RTIMER_TIME(rt)+RT_SUBSEQUENCE_PERIOD,
				1, (rtimer_callback_t)subsequence_task, ptr);
	}

	// In both cases, the substate timer is the same
	rtimer_set(&substate_timer,
			RTIMER_TIME(rt) + RT_ANCHOR_RESPONSE_WINDOW + RT_TAG_FINAL_WINDOW,
			1, (rtimer_callback_t)substate_task, NULL);

	DEBUG_P("in subsequence_task\r\n");
	subsequence_timer_fired = true;
	process_poll(&polypoint_anchor);
	return 1;
}

static char substate_task(struct rtimer *rt, void* ptr) {
	DEBUG_P("in substate_timer\r\n");
	substate_timer_fired = true;
	process_poll(&polypoint_anchor);
	return 1;
}
*/

static char subsequence_task(struct rtimer *rt, void* ptr){
	// (fwd decl'd) static bool start_of_new_subseq;
	rtimer_clock_t now = RTIMER_NOW();

	if (start_of_new_subseq) {
		start_of_new_subseq = false;
		subseq_start_time = now;
		subsequence_timer_fired = true;
		//DEBUG_P("subseq_start_time; subseq fire\r\n"); too fast to print

		// Need to set substate timer, same in all cases
		rtimer_set(rt, subseq_start_time + RTIMER_SECOND*((TAG_FINAL_DELAY_US+ANC_RX_AND_PROC_TAG_FINAL_US)/1e6),
				1, (rtimer_callback_t)subsequence_task, NULL);
	} else {
		start_of_new_subseq = true;
		substate_timer_fired = true;
		//DEBUG_P("substate fire\r\n"); too fast to print here

		if (global_subseq_num < NUM_MEASUREMENTS) {
			rtimer_set(rt, subseq_start_time + RTIMER_SECOND*(SUBSEQUENCE_PERIOD_US/1e6),
				1, (rtimer_callback_t)subsequence_task, NULL);
		}
		// Don't set a timer after the round is done, wait for tag
	}

	process_poll(&polypoint_anchor);
	return 1;
}

PROCESS_THREAD(polypoint_anchor, ev, data) {
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
	printf("Setting up DW1000 as an anchor with ID %d.\r\n", ANCHOR_EUI);
#endif
	app_init();

	/* PROGRAM BEHAVIOR
	 *
	 * Default state is listening for a TAG_POLL with subseq 0 parameters.
	 *
	 * When one is received, it kicks off a full sequence. While the anchor
	 * is expecting subsequent TAG_POLL messages, it advances through the
	 * subsequence using a timer instead of packets so that one dropped
	 * packet mid-sequence doesn't ruin the whole range event.
	 *
	 * Each anchor is assigned a time slot relative to a TAG_POLL based on
	 * ANCHOR_EUI to send ANC_RESP.
	 *
	 * After NUM_MEASUREMENTS*RT_SUBSEQUENCE_PERIOD since the initial
	 * TAG_POLL, each anchor sends an unsolicited ANC_FINAL message.
	 * ANCHOR_EUIs are again used to assign time slots.
	 *
	 * State execution is split, partially in the timer callback (interrupt
	 * context) and mostly in the main loop context. Item's with '+' are in
	 * the interrupt handler and '-' are in the main loop.
	 *
	 * states 0 .. NUM_MEASUREMENTS-1:
	 *   + Start t0: RT_SUBSEQUENCE_PERIOD timer
	 *   + Start t1: RT_ANCHOR_RESPONSE_WINDOW+RT_TAG_FINAL_WINDOW
	 *   - On TAG_POLL rx
	 *     -- Schedule ANC_RESP message (uses radio delay feature for slotting)
	 *   - On TAG_FINAL rx
	 *     -- Record infromation from tag
	 *
	 *   # Sub-state triggered by t1 expiration
	 *   - advance radio parameter state
	 *
	 * state NUM_MEASUREMENTS:
	 *   + No t0 timer.
	 *   + Start t1: RT_ANCHOR_RESPONSE_WINDOW+RT_TAG_FINAL_WINDOW
	 *   - Schedule ANC_FINAL message (uses radio delay feature for slotting)
	 *
	 *   # Sub-state triggered by t1 expiration
	 *   - Reset the world
	 *     -- sets radio parameters to subseq 0 values
	 *     -- sets state to 0
	 *
	 */

	// Kickstart things at the beginning of the loop
	global_subseq_num = 0;
	set_subsequence_settings(global_subseq_num, ANCHOR);

	rtimer_init();

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

				// no-op; will respond to TAG_POLL
			} else {
				substate_timer_fired = false;
				DEBUG_B4_HIGH;
				DEBUG_B5_LOW;

				global_subseq_num++;
				if(global_subseq_num < NUM_MEASUREMENTS) {
					set_subsequence_settings(global_subseq_num, ANCHOR);
				}
			}
		} else {
			if (subsequence_timer_fired) {
				subsequence_timer_fired = false;
				DEBUG_B4_LOW;
				DEBUG_B5_HIGH;

				//We're likely in RX mode, so we need to exit before transmission
				dwt_forcetrxoff();

#ifdef ANC_FINAL_PERCENTILE_ONLY
				{
					// Need to skip 0's (bad ranges) and interpolate %ile
					const unsigned bot = NUM_MEASUREMENTS*TARGET_PERCENTILE;
					const unsigned top = NUM_MEASUREMENTS*TARGET_PERCENTILE+1;
					unsigned idx = 0;
					while (fin_msg.distanceHist[idx] == 0) {
						idx++;
						if ((idx+top) == NUM_MEASUREMENTS) break;
					}
					if ((idx+top) == NUM_MEASUREMENTS) {
						// Didn't get enough valid measurements
						fin_msg.distanceHist[0] = 0; /* actually, this should already be true */
					} else {
						fin_msg.distanceHist[0] =
							fin_msg.distanceHist[idx+bot] +
							fin_msg.distanceHist[idx+top] * (NUM_MEASUREMENTS*TARGET_PERCENTILE - (float) bot);
					}
				}
#endif

				//Schedule this transmission for our scheduled time slot
				const uint32_t pkt_delay_upper32 = (APP_US_TO_DEVICETIMEU32(ANC_FINAL_SEND_TIME_US) & DELAY_MASK) >> 8;
				uint32_t delay_time =
					dwt_readsystimestamphi32() +
					((APP_US_TO_DEVICETIMEU32(ANC_FINAL_BUFFER_FILL_TIME_US) & DELAY_MASK) >> 8) +
					pkt_delay_upper32*(ANCHOR_EUI-1);
				delay_time &= 0xFFFFFFFE;
				dwt_setdelayedtrxtime(delay_time);
				fin_msg.seqNum++;
#ifdef ANC_FINAL_PERCENTILE_ONLY
				uint16 frame_len = offsetof(struct ieee154_anchor_final_msg, distanceHist);
				frame_len += sizeof(float) + 2*sizeof(uint8_t);
#else
				uint16 frame_len = sizeof(fin_msg);
#endif
				dwt_writetxfctrl(frame_len, 0);
				dwt_writetxdata(frame_len, (uint8_t*) &fin_msg, 0);
				int err = dwt_starttx(DWT_START_TX_DELAYED);
				dwt_settxantennadelay(TX_ANTENNA_DELAY);

				if (err) {
#ifdef DW_DEBUG
					uint32_t now = dwt_readsystimestamphi32();
					DEBUG_P("Could not send anchor response\r\n");
					DEBUG_P(" -- sched time %lu, now %lu (diff %lu)\r\n", delay_time, now, now-delay_time);
					leds_on(LEDS_RED);
#endif
				} else {
					DEBUG_P("Send ANC_FINAL\r\n");
				}
			} else {
				substate_timer_fired = false;
				DEBUG_B4_HIGH;
				DEBUG_B5_HIGH;

				DEBUG_P("reset for next round\r\n");
				leds_off(LEDS_BLUE);

				global_subseq_num = 0;
				set_subsequence_settings(global_subseq_num, ANCHOR);

				// Go for receiving
				dwt_rxenable(0);
			}
		}
	}

	PROCESS_END();
}

