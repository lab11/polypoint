
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

#include "pp_oneway_common.h"

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

static uint64_t global_poll_send_times[NUM_MEASUREMENTS+1] = {0};
static uint64_t global_anchor_TOAs[NUM_ANCHORS][NUM_MEASUREMENTS+1] = {{0}};
static uint64_t global_anchor_final_send_times[NUM_ANCHORS] = {0};
static uint64_t global_final_TOAs[NUM_ANCHORS] = {0};

static struct pp_tag_poll pp_tag_poll_pkt;



static void insert_sorted(int arr[], int new, unsigned end) {
	unsigned insert_at = 0;
	while ((insert_at < end) && (new >= arr[insert_at])) {
		insert_at++;
	}
	if (insert_at == end) {
		arr[insert_at] = new;
	} else {
		while (insert_at <= end) {
			int temp = arr[insert_at];
			arr[insert_at] = new;
			new = temp;
			insert_at++;
		}
	}
}

static double dwtime_to_dist(double dwtime, unsigned anchor_id, unsigned subseq) {
	double dist = dwtime * DWT_TIME_UNITS * SPEED_OF_LIGHT;
	dist += ANCHOR_CAL_LEN;
	dist -= txDelayCal[anchor_id*NUM_CHANNELS + subseq_num_to_chan(subseq, true)];
	return dist;
}

static void compute_results() {
	unsigned i;
	for(i=0; i < NUM_ANCHORS; i++){
		unsigned j;
#ifndef REPORT_PERCENTILE_ONLY
		printf("\r\ntagstart %d\r\n",i+1);
#endif
		/*
		for (j=0; j < NUM_MEASUREMENTS+1; j++) {
			printf("%02d: ts %llu aa %llu af %llu tf %llu\r\n",
					j,
					global_poll_send_times[j],
					global_anchor_TOAs[i][j],
					global_anchor_final_send_times[i],
					global_final_TOAs[i]
			      );
		}
		printf("\r\n");
		*/

		double tRF = (double)(global_anchor_TOAs[i][NUM_MEASUREMENTS]);
		double tSP = (double)(global_poll_send_times[0]);
		double tSF = (double)(global_poll_send_times[NUM_MEASUREMENTS]);
		double tRP = (double)(global_anchor_TOAs[i][0]);

		// Find a multiplier for the crystal offset between tag and anchor
		double anchor_over_tag = (tRF-tRP)/(tSF-tSP);

		// 2-way ToF from last round
		double tSR = (double)(global_anchor_final_send_times[i]);
		double tRR = (double)(global_final_TOAs[i]);
		double tTOF = (tRF-tSR)-(tSF-tRR)*anchor_over_tag;
		// Want one-way TOF
		tTOF /= 2;

		/*
		double dist = dwtime_to_dist(tTOF, i+1, 0);
		int64_t dist_times_1000000 = (int64_t)(dist*1000000);
		printf("**[%02d %02d] %lld.%lld\r\n", i+1, 0, dist_times_1000000/1000000,dist_times_1000000%1000000);
		*/

		// ancTOA = tagSent + twToF + offset
		double anc_tag_dw_offset = tRF - tSF*anchor_over_tag - tTOF;

#ifdef SORT_MEASUREMENTS
		int dists_times_100[NUM_MEASUREMENTS] = {0};
#endif
		for (j=0; j < NUM_MEASUREMENTS; j++) {
			double AA = (double)(global_anchor_TOAs[i][j]);
			double PS = (double)(global_poll_send_times[j]);
			double ToF = AA - PS*anchor_over_tag - anc_tag_dw_offset;
			double dist = dwtime_to_dist(ToF, i+1, j);
#ifdef SORT_MEASUREMENTS
			insert_sorted(dists_times_100, (int)(dist*100), j);
#else // SORT_MEASUREMENTS
#ifdef DW_DEBUG
			int64_t dist_times_1000000 = (int64_t)(dist*1000000);
			printf("[%02d %02d] %lld.%lld\r\n", i+1, j, dist_times_1000000/1000000,dist_times_1000000%1000000);
#else
			int dist_times_100 = (int)(dist*100);
			printf("%d.%d ", dist_times_100/100, dist_times_100%100);
#endif // DW_DEBUG
#endif // !SORT_MEASUREMENTS
		}

#ifdef SORT_MEASUREMENTS
#ifdef REPORT_PERCENTILE_ONLY
		{
			DEBUG_B6_LOW;
			// Need to skip bad ranges (usu ~-154) and interpolate %ile
			const unsigned bot = NUM_MEASUREMENTS*TARGET_PERCENTILE;
			const unsigned top = NUM_MEASUREMENTS*TARGET_PERCENTILE+1;
			unsigned idx = 0;
			while (dists_times_100[idx] < -50) {
				idx++;
				if ((idx+top) == NUM_MEASUREMENTS) break;
			}
			if ((idx+top) == NUM_MEASUREMENTS) {
				printf("--- ");
			} else {
				int perc =
					dists_times_100[idx+bot] +
					dists_times_100[idx+top] * (NUM_MEASUREMENTS*TARGET_PERCENTILE - (float) bot);
				printf("%d.%d ", perc/100, perc%100);
			}
			DEBUG_B6_HIGH;
		}
#else  // REPORT_PERCENTILE_ONLY
		for (j=0; j < NUM_MEASUREMENTS; j++) {
			int dist_times_100 = dists_times_100[j];
			printf("%d.%d ", dist_times_100/100, dist_times_100%100);
		}
#endif // !REPORT_PERCENTILE_ONLY
#endif // SORT_MEASUREMENTS
	}
#ifdef REPORT_PERCENTILE_ONLY
	printf("\r\n");
#else
	printf("\r\ndone\r\n");
#endif
}

static void send_pkt(bool is_final){
	DEBUG_B6_HIGH;
	const uint16_t tx_frame_length = sizeof(struct pp_tag_poll);
	pp_tag_poll_pkt.header.seqNum++;
	pp_tag_poll_pkt.message_type = (is_final) ? MSG_TYPE_PP_ONEWAY_TAG_FINAL : MSG_TYPE_PP_ONEWAY_TAG_POLL;
	pp_tag_poll_pkt.subsequence = global_subseq_num;

	//Make sure we're out of rx mode before attempting to transmit
	dwt_forcetrxoff();

	// Tell the DW1000 about the packet
	dwt_writetxfctrl(tx_frame_length, 0);

	uint32_t temp = dwt_readsystimestamphi32();
	uint32_t delay_time = temp + DW_DELAY_FROM_PKT_LEN(tx_frame_length);
	delay_time &= 0xFFFFFFFE; //Make sure last bit is zero
	dwt_setdelayedtrxtime(delay_time);
	global_poll_send_times[global_subseq_num] = ((uint64_t)delay_time) << 8;

	// Write the data
	dwt_writetxdata(tx_frame_length, (uint8_t*) &pp_tag_poll_pkt, 0);

	// Start the transmission
	int err;
	if (is_final) {
		dwt_setrxaftertxdelay(1); // us
		err = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
	} else {
		err = dwt_starttx(DWT_START_TX_DELAYED);
	}

	// MP bug - TX antenna delay needs reprogramming as it is not preserved
	dwt_settxantennadelay(TX_ANTENNA_DELAY);

	if (err == DWT_SUCCESS) {
		DEBUG_P("Sent TAG_POLL  (%u)\r\n", global_subseq_num);
	} else {
		DEBUG_P("Error sending TAG_POLL: %d\r\n", err);
		DEBUG_P("Start %lu, goal %lu, now %lu\r\n",
				temp, delay_time, dwt_readsystimestamphi32());
	}
	DEBUG_B6_LOW;
}

static void send_poll() {
	send_pkt(false);
}

static void send_final() {
	send_pkt(true);
}

// Triggered after a TX
void app_dw1000_txcallback (const dwt_callback_data_t *txd) {
	//NOTE: No need for tx timestamping after-the-fact (everything's done beforehand)
}

#define RX_PKT_BUF_LEN	256
_Static_assert(RX_PKT_BUF_LEN >= sizeof(struct pp_anc_final), "RX_PKT_BUF_LEN too small");

// Triggered when we receive a packet
void app_dw1000_rxcallback (const dwt_callback_data_t *rxd) {
	DEBUG_B6_HIGH;
	if (rxd->event == DWT_SIG_RX_OKAY) {
		leds_toggle(LEDS_BLUE);
		uint8_t packet_type;
		uint64_t timestamp;
		uint8_t recv_pkt_buf[RX_PKT_BUF_LEN];

		// Get the timestamp first
		uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};
		dwt_readrxtimestamp(txTimeStamp);
		timestamp = ((uint64_t) (*((uint32_t*) txTimeStamp))) | (((uint64_t) txTimeStamp[4]) << 32);

		// Get the packet
		dwt_readrxdata(recv_pkt_buf, MIN(RX_PKT_BUF_LEN, rxd->datalength), 0);
		packet_type = recv_pkt_buf[offsetof(struct pp_anc_final, message_type)];

		if (packet_type == MSG_TYPE_PP_ONEWAY_ANC_FINAL) {
			DEBUG_B6_LOW;
			struct pp_anc_final* anc_final;
			anc_final = (struct pp_anc_final*) recv_pkt_buf;

			uint8_t anchor_id = anc_final->anchor_id;

			DEBUG_P("ANC_FINAL from %u\r\n", anchor_id);
			if((anchor_id-1) >= NUM_ANCHORS) return;

			DEBUG_B6_HIGH;
			memcpy(
					&global_anchor_TOAs[anchor_id-1],
					anc_final->TOAs,
					sizeof(anc_final->TOAs)
					);
			global_anchor_final_send_times[anchor_id-1] = ((uint64_t)anc_final->dw_time_sent) << 8;
			global_final_TOAs[anchor_id-1] = timestamp;
		} else {
			DEBUG_P("*** ERR: RX Unknown packet type: 0x%X\r\n", packet_type);
		}
	} else {
		DEBUG_P("*** ERR: rxd->event unknown: 0x%X\r\n", rxd->event);
	}
	DEBUG_B6_LOW;
}


void app_init(void) {
	DEBUG_P("\r\n### APP INIT\r\n");
	int err = app_dw1000_init(TAG, TAG_EUI, app_dw1000_txcallback, app_dw1000_rxcallback);
	if (err == -1) {
		leds_on(LEDS_RED);
		DEBUG_P("--- Err init'ing DW. Trying again\r\n");
		clock_delay_usec(1e3); // sleep for a millisecond before trying again
		return app_init();
	} else {
		leds_off(LEDS_RED);
	}

	// Setup the constants in the outgoing packet
	pp_tag_poll_pkt.header.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
	pp_tag_poll_pkt.header.frameCtrl[1] = 0xC8; // ext addr
	pp_tag_poll_pkt.header.seqNum = 0;
	pp_tag_poll_pkt.header.panID[0] = DW1000_PANID & 0xff;
	pp_tag_poll_pkt.header.panID[1] = DW1000_PANID >> 8;
	pp_tag_poll_pkt.header.destAddr[0] = 0xff;
	pp_tag_poll_pkt.header.destAddr[1] = 0xff;
	dw1000_populate_eui(pp_tag_poll_pkt.header.sourceAddr, TAG_EUI);

	pp_tag_poll_pkt.message_type = MSG_TYPE_PP_ONEWAY_TAG_POLL;

	pp_tag_poll_pkt.roundNum = 0;
	pp_tag_poll_pkt.subsequence = 0;
}



static char subsequence_task(struct rtimer *rt, void* ptr){
	static bool start_of_new_subseq = true;
	static rtimer_clock_t subseq_start;
	rtimer_clock_t now = RTIMER_NOW();
	rtimer_clock_t set_to;

	bool set_timer = true;

	if (start_of_new_subseq) {
		start_of_new_subseq = false;
		subseq_start = now;
		subsequence_timer_fired = true;

		if (global_subseq_num < NUM_MEASUREMENTS) {
			set_to = subseq_start + US_TO_RT(POLL_TO_SS_US);
		} else {
			set_to = subseq_start + US_TO_RT(ALL_ANC_FINAL_US);
		}
	} else {
		start_of_new_subseq = true;
		substate_timer_fired = true;

		if (global_subseq_num < NUM_MEASUREMENTS) {
			set_to = subseq_start + US_TO_RT(POLL_TO_SS_US+SS_TO_SQ_US);
		} else {
			set_timer = false;
			//set_to = subseq_start + US_TO_RT(ALL_ANC_FINAL_US+SS_PRINTF_US);
		}
	}

	if (set_timer) {
		rtimer_set(rt, set_to, 1, (rtimer_callback_t)subsequence_task, NULL);
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
	/* PROGRAM BEHAVIOR
	 * Loop through states based on global_subseq_num. State execution is
	 * split, partially in the timer callback (interrupt context) and mostly
	 * in the main loop context. Item's with '+' are in the interrupt handler
	 * and '-' are in the main loop.
	 *
	 * states 0 .. NUM_MEASUREMENTS-1:
	 *   + Start t0: POLL_TO_SS timer
	 *   - Send broadcast TAG_POLL
	 *
	 *   # Sub-state triggered by t0 expiration
	 *   + Start t1: SS_TO_SQ timer
	 *   - Choose antenna and channel
	 *
	 * state NUM_MEASUREMENTS+1:
	 *   + Start t0: ALL_ANC_FINAL timer
	 *   - Set state to 0
	 *   - Send broadcast TAG_FINAL
	 *     -- Expect ANC_FINAL's from each anchor
	 *
	 *   # Sub-state triggered by t0 expiration
	 *   + Start t1: SS_PRINTF timer
	 *   - Print result of all measurements
	 *   - Reset the world
	 *     -- sets state to 0
	 *
	 */

	app_init();
	global_round_num = 0;
	global_subseq_num = 0;
	set_subsequence_settings(0, TAG);

	// Kickstart things at the beginning of the loop
	rtimer_init();
	rtimer_set(&subsequence_timer, RTIMER_NOW() + RTIMER_SECOND, 1,
			(rtimer_callback_t)subsequence_task, NULL);

	DEBUG_B4_INIT;
	DEBUG_B5_INIT;
	DEBUG_B6_INIT;
	DEBUG_B4_HIGH;
	DEBUG_B5_HIGH;
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

				if (global_subseq_num < NUM_MEASUREMENTS) {
					set_subsequence_settings(global_subseq_num, TAG);
					send_poll();
				} else {
				}
			} else {
				substate_timer_fired = false;
				DEBUG_B4_HIGH;
				DEBUG_B5_LOW;

				global_subseq_num++;

				if (global_subseq_num < NUM_MEASUREMENTS) {
					set_subsequence_settings(global_subseq_num, TAG);
				} else {
					set_subsequence_settings(0, TAG);
				}
			}
		} else {
			if (subsequence_timer_fired) {
				subsequence_timer_fired = false;
				DEBUG_B4_LOW;
				DEBUG_B5_HIGH;

				send_final(); // enables rx

				DEBUG_P("In wait period expecting ANC_FINALs\r\n");

				// anchors should begin sending ANC_FINAL
				// messages during this slot; the rx handler
				// will fill out the results array
			} else {
				substate_timer_fired = false;
				DEBUG_B4_HIGH;
				DEBUG_B5_HIGH;

				// measure timing
				DEBUG_B6_HIGH;

				compute_results();

				pp_tag_poll_pkt.roundNum = ++global_round_num;

				memset(global_poll_send_times, 0, sizeof(global_poll_send_times));
				memset(global_anchor_TOAs, 0, sizeof(global_anchor_TOAs));
				memset(global_anchor_final_send_times, 0, sizeof(global_anchor_final_send_times));
				memset(global_final_TOAs, 0, sizeof(global_final_TOAs));

				global_subseq_num = 0;

#ifdef INTERVAL_DELAY_US
				rtimer_set(
						&subsequence_timer,
						RTIMER_NOW() + US_TO_RT(INTERVAL_DELAY_US),
						1,
						(rtimer_callback_t)subsequence_task,
						NULL
						);
#else
				// Call the timer function so the next round kickstarts
				subsequence_task(&subsequence_timer, NULL);
#endif

				DEBUG_B6_LOW;
			}
		}
	}

	PROCESS_END();
}