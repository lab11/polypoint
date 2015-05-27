#include "contiki.h"
#include "sys/rtimer.h"
#include "dev/leds.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "dw1000.h"
#include "dev/ssi.h"
#include "cpu/cc2538/lpm.h"
#include "dev/watchdog.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "pp_oneway_common.h"

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

#ifndef ANCHOR_EUI
#define ANCHOR_EUI 1
#endif

_Static_assert(ANCHOR_EUI <= 10, "Invalid ANCHOR_EUI");

/**************
 * GLOBAL STATE
 */

static bool global_round_active = false;
static uint8_t global_round_num = 0xAA;
static uint8_t global_subseq_num = 0xAA;

static struct pp_anc_final pp_anc_final_pkt;

// Triggered after a TX
void app_dw1000_txcallback (const dwt_callback_data_t *txd) {
	//NOTE: No need for tx timestamping after-the-fact (everything's done beforehand)
}


#define RX_PKT_BUF_LEN	64
_Static_assert(RX_PKT_BUF_LEN >= sizeof(struct pp_tag_poll), "RX_PKT_BUF_LEN too small");

// Triggered when we receive a packet
void app_dw1000_rxcallback (const dwt_callback_data_t *rxd) {
	// First grab a copy of local time when this arrived
	rtimer_clock_t rt_timestamp = RTIMER_NOW();
	DEBUG_B6_HIGH;

	if (rxd->event == DWT_SIG_RX_OKAY) {
		leds_toggle(LEDS_BLUE);

		uint8_t packet_type;
		uint64_t dw_timestamp;
		uint8_t recv_pkt_buf[RX_PKT_BUF_LEN];

		// Get the dw_timestamp first
		uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};
		dwt_readrxtimestamp(txTimeStamp);
		dw_timestamp = ((uint64_t) (*((uint32_t*) txTimeStamp))) | (((uint64_t) txTimeStamp[4]) << 32);

		// Get the packet
		dwt_readrxdata(recv_pkt_buf, MIN(RX_PKT_BUF_LEN, rxd->datalength), 0);
		packet_type = recv_pkt_buf[offsetof(struct pp_tag_poll, message_type)];
		global_round_num = recv_pkt_buf[offsetof(struct pp_tag_poll, roundNum)];

		if (packet_type == MSG_TYPE_PP_ONEWAY_TAG_POLL) {
			struct pp_tag_poll* pkt = (struct pp_tag_poll*) recv_pkt_buf;
			DEBUG_P("TAG_POLL rx: %u\r\n", pkt->subsequence);

			if (global_subseq_num == pkt->subsequence) {
				// Configurations matched, record arrival time
				pp_anc_final_pkt.TOAs[global_subseq_num] = dw_timestamp;
			} else {
				// Tag/anchor weren't on same settings, so we
				// drop this sample and catch the anchor up
				global_subseq_num = pkt->subsequence;
			}

			if (!global_round_active) {
				DEBUG_B4_LOW;
				DEBUG_B5_LOW;
				global_round_active = true;
				start_of_new_subseq = true;
				substate_timer_fired = true;
				/*
				subseq_start_time = rt_timestamp - US_TO_RT(TAG_SQ_START_TO_POLL_SFD_HIGH_US);
				rtimer_clock_t set_to = subseq_start_time + US_TO_RT(POLL_TO_SS_US+SS_TO_SQ_US);
				*/
				rtimer_clock_t set_to = rt_timestamp + US_TO_RT(
						POLL_TO_SS_US + SS_TO_SQ_US
						- TAG_SQ_START_TO_POLL_SFD_HIGH_US
						- ANC_MYSTERY_STARTUP_DELAY_US);
				rtimer_set(&subsequence_timer,
						set_to,
						1,
						(rtimer_callback_t)subsequence_task,
						NULL);
				process_poll(&polypoint_anchor);
			}
		} else if (packet_type == MSG_TYPE_PP_ONEWAY_TAG_FINAL) {
			if (!global_round_active) {
				// The first packet we happened to receive was
				// an ANC_FINAL. We have nothing interesting to
				// reply with, so don't. But we *do* need to set
				// receive mode again so that a new poll will be
				// caught
				dwt_rxenable(0);
				return;
			}

			//We're likely in RX mode, so we need to exit before transmission
			dwt_forcetrxoff();

			pp_anc_final_pkt.TOAs[NUM_MEASUREMENTS] = dw_timestamp;

			pp_anc_final_pkt.header.seqNum++;
			const uint16 frame_len = sizeof(struct pp_anc_final);
			dwt_writetxfctrl(frame_len, 0);

			//Schedule this transmission for our scheduled time slot
			DEBUG_B6_LOW;
			uint32_t temp = dwt_readsystimestamphi32();
			uint32_t delay_time = temp +
				DW_DELAY_FROM_US(
					ANC_FINAL_INITIAL_DELAY_HACK_VALUE +
					(ANC_FINAL_RX_TIME_ON_TAG*(ANCHOR_EUI-1))
					);
				/* I don't understand what exactly is going on
				 * here. The chip seems to want way longer for
				 * this preamble than others -- maybe something
				 * to do with the large payload? From empirical
				 * measurements, the 300 base delay is about the
				 * minimum (250 next tested as not working)
				DW_DELAY_FROM_US(
					REVISED_DELAY_FROM_PKT_LEN_US(frame_len) +
					(2*ANC_FINAL_RX_TIME_ON_TAG*(ANCHOR_EUI-1))
					);
				*/
			delay_time &= 0xFFFFFFFE;
			pp_anc_final_pkt.dw_time_sent = delay_time;
			dwt_setdelayedtrxtime(delay_time);

			int err = dwt_starttx(DWT_START_TX_DELAYED);
			dwt_settxantennadelay(TX_ANTENNA_DELAY);
			dwt_writetxdata(frame_len, (uint8_t*) &pp_anc_final_pkt, 0);
			DEBUG_B6_HIGH;

#ifdef DW_DEBUG
			// No printing until after all dwt timing op's
			struct pp_tag_poll* pkt = (struct pp_tag_poll*) recv_pkt_buf;
			DEBUG_P("TAG_FINAL rx: %u\r\n", pkt->subsequence);
#endif

			if (err) {
#ifdef DW_DEBUG
				uint32_t now = dwt_readsystimestamphi32();
				DEBUG_P("Could not send anchor response\r\n");
				DEBUG_P(" -- sched time %lu, now %lu (diff %lu)\r\n", delay_time, now, now-delay_time);
				leds_on(LEDS_RED);
#endif
			} else {
				DEBUG_P("Send ANC_FINAL\r\n");
				leds_off(LEDS_RED);
			}
		} else {
			DEBUG_P("*** ERR: RX Unknown packet type: 0x%X\r\n", packet_type);
		}
	} else {
		DEBUG_P("*** ERR: rxd->event unknown: 0x%X\r\n", rxd->event);
	}
	DEBUG_B6_LOW;
}

void app_init() {
	DEBUG_P("\r\n### APP INIT\r\n");

	int err = app_dw1000_init(ANCHOR, ANCHOR_EUI, app_dw1000_txcallback, app_dw1000_rxcallback);
	if (err == -1) {
		leds_on(LEDS_RED);
		DEBUG_P("--- Err init'ing DW. Trying again\r\n");
		clock_delay_usec(1e3); // sleep for a millisecond before trying again
		return app_init();
	} else {
		leds_off(LEDS_RED);
	}

	// Setup the constants in the outgoing packet
	pp_anc_final_pkt.header.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
	pp_anc_final_pkt.header.frameCtrl[1] = 0xCC; // ext addr
	pp_anc_final_pkt.header.seqNum = 0;
	pp_anc_final_pkt.header.panID[0] = DW1000_PANID & 0xff;
	pp_anc_final_pkt.header.panID[1] = DW1000_PANID >> 8;
	dw1000_populate_eui(pp_anc_final_pkt.header.destAddr, TAG_EUI);
	dw1000_populate_eui(pp_anc_final_pkt.header.sourceAddr, ANCHOR_EUI);

	pp_anc_final_pkt.message_type = MSG_TYPE_PP_ONEWAY_ANC_FINAL;
	pp_anc_final_pkt.anchor_id = ANCHOR_EUI;
}


static char subsequence_task(struct rtimer *rt, void* ptr){
	// (fwd decl'd) static bool start_of_new_subseq;
	rtimer_clock_t now = RTIMER_NOW();
	rtimer_clock_t set_to;
	bool set_timer = true;

	if (start_of_new_subseq) {
		start_of_new_subseq = false;
		subseq_start_time = now;
		subsequence_timer_fired = true;

		if (global_subseq_num < NUM_MEASUREMENTS) {
			set_to = subseq_start_time + US_TO_RT(POLL_TO_SS_US);
		} else {
			set_to = subseq_start_time + US_TO_RT(ALL_ANC_FINAL_US);
		}
	} else {
		start_of_new_subseq = true;
		substate_timer_fired = true;

		if (global_subseq_num < NUM_MEASUREMENTS) {
			set_to = subseq_start_time + US_TO_RT(POLL_TO_SS_US+SS_TO_SQ_US);
		} else {
			set_timer = false;
		}
		// Don't set a timer after the round is done, wait for tag
	}

	if (set_timer) {
		rtimer_set(rt, set_to, 1, (rtimer_callback_t)subsequence_task, NULL);
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
	dwt_forcetrxoff();
	printf("Inited the DW1000 driver (setup SPI)\r\n");

	leds_off(LEDS_ALL);

	printf("Setting up DW1000 as an anchor with ID %d.\r\n", ANCHOR_EUI);

	/* PROGRAM BEHAVIOR
	 *
	 * Default state is listening for a TAG_POLL with subseq 0 parameters.
	 *
	 * When one is received, it kicks off a full sequence. While the anchor
	 * is expecting subsequent TAG_POLL messages, it advances through the
	 * subsequence using a timer instead of packets so that one dropped
	 * packet mid-sequence doesn't ruin the whole range event.
	 *
	 * After NUM_MEASUREMENTS*POLL_PERIOD since the initial TAG_POLL, each
	 * anchor sends an unsolicited ANC_FINAL message.  Each anchor is
	 * assigned a time slot relative to a TAG_POLL based on ANCHOR_EUI to
	 * send ANC_FINAL.
	 *
	 * State execution is split, partially in the timer callback (interrupt
	 * context) and mostly in the main loop context. Item's with '+' are in
	 * the interrupt handler and '-' are in the main loop.
	 *
	 * states 0 .. NUM_MEASUREMENTS-1:
	 *   + Start t0: POLL_TO_SS timer
	 *   - On TAG_POLL rx
	 *     -- Record timing information
	 *
	 *   # Sub-state triggered by t1 expiration
	 *   + Start t1: SS_TO_SQ timer
	 *   - advance radio parameter state
	 *
	 * state NUM_MEASUREMENTS:
	 *   + Start t0: ALL_ANC_FINAL timer
	 *   - On TAG_FINAL rx
	 *     -- Schedule ANC_FINAL message (uses radio delay feature for slotting)
	 *
	 *   # Sub-state triggered by t1 expiration
	 *   + No timers set, anchor is done after this
	 *   - Reset the world
	 *     -- sets radio parameters to subseq 0 values
	 *     -- sets state to 0
	 *
	 */

	app_init();
	rtimer_init();
	watchdog_init();	// Contiki default sets watchdog to 1 s
	watchdog_start();

	//Reset measurements
	memset(pp_anc_final_pkt.TOAs, 0, sizeof(pp_anc_final_pkt.TOAs));

	// Kickstart things at the beginning of the loop
	global_subseq_num = 0;
	set_subsequence_settings(0, ANCHOR);
		clock_delay_usec(1e3); // sleep for a millisecond before trying again
	set_subsequence_settings(0, ANCHOR);
	dwt_rxenable(0);

	DEBUG_B4_INIT;
	DEBUG_B5_INIT;
	DEBUG_B6_INIT;
	DEBUG_B4_LOW;
	DEBUG_B5_LOW;
	DEBUG_B6_LOW;

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
				} else {
					set_subsequence_settings(0, ANCHOR);
				}

				// Go for receiving
				dwt_rxenable(0);
			}
		} else {
			if (subsequence_timer_fired) {
				subsequence_timer_fired = false;
				DEBUG_B4_LOW;
				DEBUG_B5_HIGH;

				// no-op; will respond to TAG_FINAL
			} else {
				substate_timer_fired = false;
				DEBUG_B4_HIGH;
				DEBUG_B5_HIGH;

				DEBUG_P("reset for next round\r\n");
				leds_off(LEDS_BLUE);

				global_round_active = false;
				global_subseq_num = 0;

				//Reset measurements
				memset(pp_anc_final_pkt.TOAs, 0, sizeof(pp_anc_final_pkt.TOAs));

				// Shouldn't be needed, but doesn't hurt
				set_subsequence_settings(0, ANCHOR);

				// Tickle the watchdog
				watchdog_periodic();

				// Go for receiving
				dwt_rxenable(0);
			}
		}
	}

	PROCESS_END();
}

