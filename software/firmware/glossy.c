#include "dw1000.h"
#include "deca_regs.h"
#include "glossy.h"
#include "oneway_common.h"
#include "timer.h"
#include "prng.h"
#include <string.h>

void send_sync(uint32_t delay_time);

static stm_timer_t* _glossy_timer;
static struct pp_sched_flood _sync_pkt;
static struct pp_sched_req_flood _sched_req_pkt;
static glossy_role_e _role;

static uint8_t _last_sync_depth;
static uint64_t _last_sync_timestamp;
static uint64_t _last_overall_timestamp;
static uint64_t _time_overflow;
static uint64_t _last_time_sent;
static uint64_t _glossy_flood_timeslot_corrected_us;
static uint32_t _last_delay_time;
static uint8_t _currently_syncd;
static uint8_t _xtal_trim;
static uint8_t _last_xtal_trim;
static bool _sending_sync;
static uint32_t _lwb_counter;
static bool _lwb_valid;
static uint8_t _cur_glossy_depth;
static bool _glossy_currently_flooding;

static bool _lwb_sched_en;
static bool _lwb_scheduled;
static uint32_t _lwb_num_timeslots;
static uint32_t _lwb_timeslot;
static void (*_lwb_schedule_callback)(void);
static double _clock_offset;

static uint8_t _sched_euis[MAX_SCHED_TAGS][EUI_LEN];
static int _cur_sched_tags;

static ranctx _prng_state;

#ifdef GLOSSY_PER_TEST
static uint32_t _total_syncs_sent;
static uint32_t _total_syncs_received;
#endif

uint8_t uint64_count_ones(uint64_t number){
	int ii;
	uint8_t ret = 0;
	for(ii = 0; ii < 64; ii++){
		if(number & (1 << ii)) ret++;
	}
	return ret;
}

void glossy_init(glossy_role_e role){
	_sync_pkt = (struct pp_sched_flood) {
		.header = {
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
		},
		.message_type = MSG_TYPE_PP_GLOSSY_SYNC,
		.tag_ranging_mask = 0,
		.tag_sched_idx = 0,
		.tag_sched_eui = { 0 }
	};

	_sched_req_pkt.header = _sync_pkt.header;
	_sched_req_pkt.message_type = MSG_TYPE_PP_GLOSSY_SCHED_REQ;
	dw1000_read_eui(_sched_req_pkt.tag_sched_eui);

	// TODO: We're currently using the same EUI throughout...
	// What happens to glossy when the EUIs are different??

	// Seed our random number generator with our EUI
	raninit(&_prng_state, _sched_req_pkt.tag_sched_eui[0]<<8|_sched_req_pkt.tag_sched_eui[1]);

	_currently_syncd = 0;
	_last_overall_timestamp = 0;
	_last_delay_time = 0;
	_role = role;
	_sending_sync = FALSE;
	_lwb_counter = 0;
	_cur_sched_tags = 0;
	_glossy_flood_timeslot_corrected_us = (uint64_t)(DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE) << 8;

	_lwb_valid = FALSE;
	_lwb_sched_en = FALSE;
	_lwb_scheduled = FALSE;
	_lwb_schedule_callback = NULL;
	_glossy_currently_flooding = FALSE;

#ifdef GLOSSY_PER_TEST
	_total_syncs_sent = 0;
#endif

	// Set crystal trim to mid-range
	_xtal_trim = 15;
	dwt_xtaltrim(_xtal_trim);

	// If the anchor, let's kick off a task which unconditionally kicks off sync messages with depth = 0
	if(role == GLOSSY_MASTER){
		_lwb_valid = TRUE;
		uint8 ldok = OTP_SF_OPS_KICK | OTP_SF_OPS_SEL_TIGHT;
		dwt_writetodevice(OTP_IF_ID, OTP_SF, 1, &ldok); // set load LDE kick bit
		_last_time_sent = dwt_readsystimestamphi32() & 0xFFFFFFFE;
	}

	// The glossy timer acts to synchronize everyone to a common timebase
	_glossy_timer = timer_init();
	timer_start(_glossy_timer, LWB_SLOT_US, glossy_sync_task);
}

void glossy_sync_task(){
	_lwb_counter++;

	if(_role == GLOSSY_MASTER){
		// During the first timeslot, put ourselves back into RX mode
		if(_lwb_counter == 1){
			dwt_rxenable(0);
#ifdef GLOSSY_ANCHOR_SYNC_TEST
			dw1000_choose_antenna(1);
#endif

		// Last timeslot is used by the master to schedule the next glossy sync packet
		} else if(_lwb_counter == (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)-1){
			dwt_forcetrxoff();
		
		#ifdef GLOSSY_PER_TEST
			_total_syncs_sent++;
			if(_total_syncs_sent >= 10000){
				while(1){
					dwt_write32bitreg(3, _total_syncs_received);
				}
			}
		#endif
		
			dw1000_update_channel(1);
			dw1000_choose_antenna(0);
		
			_last_time_sent += GLOSSY_UPDATE_INTERVAL_DW;
			send_sync(_last_time_sent);
			_sending_sync = TRUE;
		}
	} else {
		// Force ourselves into RX mode if we still haven't received any sync floods...
		// TODO: This is a hack... :(
		if((!_lwb_valid || (_lwb_counter > (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US))) && ((_lwb_counter % 5) == 0)) {
			dwt_forcetrxoff();
			dw1000_update_channel(1);
			dw1000_choose_antenna(0);
			dwt_rxenable(0);
		}

		else {
			// Check to see if it's our turn to do a ranging event!
			// LWB Slot 1: Contention slot
			if(_lwb_counter == 1){
				dw1000_update_channel(1);
				dw1000_choose_antenna(0);
				if(!_lwb_scheduled && _lwb_sched_en){
					dwt_forcetrxoff();

					uint16_t frame_len = sizeof(struct pp_sched_req_flood);
					dwt_writetxfctrl(frame_len, 0);

					// Send out a schedule request during this contention slot
					// Pick a random time offset to avoid colliding with others
#ifdef GLOSSY_ANCHOR_SYNC_TEST
					uint32_t sched_req_time = (uint32_t)(_sched_req_pkt.tag_sched_eui[0] - 0x31) * GLOSSY_FLOOD_TIMESLOT_US;
					uint32_t delay_time = (dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(sizeof(struct pp_sched_req_flood)) + DW_DELAY_FROM_US(sched_req_time)) & 0xFFFFFFFE;
					double turnaround_time = (double)((((uint64_t)(delay_time) << 8) - _last_sync_timestamp) & 0xFFFFFFFFFFUL);// + DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US)*_last_sync_depth;
					turnaround_time /= _clock_offset;
					_sched_req_pkt.turnaround_time = (uint64_t)(turnaround_time);
					dw1000_choose_antenna(1);
#else
					uint32_t sched_req_time = (ranval(&_prng_state) % (uint32_t)(LWB_SLOT_US-2*GLOSSY_FLOOD_TIMESLOT_US)) + GLOSSY_FLOOD_TIMESLOT_US;
					uint32_t delay_time = (dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(sizeof(struct pp_sched_req_flood)) + DW_DELAY_FROM_US(sched_req_time)) & 0xFFFFFFFE;
#endif

					dwt_setdelayedtrxtime(delay_time);
					dwt_setrxaftertxdelay(LWB_SLOT_US);
					dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
					dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
					dwt_writetxdata(sizeof(struct pp_sched_req_flood), (uint8_t*) &_sched_req_pkt, 0);
				} else {
					dwt_rxenable(0);
				}

			// LWB Slot N-1: Get ready for next glossy flood
			} else if(_lwb_counter == (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)-2){
				// Make sure we're in RX mode, ready for next glossy sync flood!
				//dwt_setdblrxbuffmode(FALSE);
				dwt_forcetrxoff();
				dw1000_update_channel(1);
				dw1000_choose_antenna(0);
				dwt_rxenable(0);
			}
		}
	}
}

void lwb_set_sched_request(bool sched_en){
	_lwb_sched_en = sched_en;
}

void lwb_set_sched_callback(void (*callback)(void)){
	_lwb_schedule_callback = callback;
}

void glossy_process_txcallback(){
	if(_role == GLOSSY_MASTER && _sending_sync){
		// Sync has sent, set the timer to send the next one at a later time
		timer_reset(_glossy_timer, 0);
		_lwb_counter = 0;
		_sending_sync = FALSE;
	} else if(_role == GLOSSY_SLAVE){
		if(_glossy_currently_flooding){
			// We're flooding, keep doing it until the max depth!
			uint32_t delay_time = _last_delay_time + (DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE);
			delay_time &= 0xFFFFFFFE;
			_last_delay_time = delay_time;

			_cur_glossy_depth++;
			if (_cur_glossy_depth < GLOSSY_MAX_DEPTH){
				dwt_forcetrxoff();
				dwt_setrxaftertxdelay(LWB_SLOT_US);
				dwt_setdelayedtrxtime(delay_time);
				dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
				dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
    				dwt_writetodevice( TX_BUFFER_ID, offsetof(struct ieee154_header_broadcast, seqNum), 1, &_cur_glossy_depth) ;
			} else {
				dwt_rxenable(0);
				_glossy_currently_flooding = FALSE;
			}
		}
	}
}

void send_sync(uint32_t delay_time){
	uint16_t frame_len = sizeof(struct pp_sched_flood);
	dwt_writetxfctrl(frame_len, 0);

	_last_delay_time = delay_time;

	dwt_setdelayedtrxtime(delay_time);
	dwt_setrxaftertxdelay(1);

	dwt_starttx(DWT_START_TX_DELAYED);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
	dwt_writetxdata(sizeof(_sync_pkt), (uint8_t*) &_sync_pkt, 0);
}

#define CW_CAL_12PF ((3.494350-3.494173)/3.4944*1e6/30)
#define CW_CAL_22PF ((3.494078-3.493998)/3.4944*1e6/30)
#define CW_CAL_33PF ((3.493941-3.493891)/3.4944*1e6/30)
int8_t clock_offset_to_trim_diff(double ppm_offset){
       return (int8_t) (ppm_offset/CW_CAL_12PF + 0.5);
}

void glossy_sync_process(uint64_t dw_timestamp, uint8_t *buf){
	struct pp_sched_flood *in_glossy_sync = (struct pp_sched_flood *) buf;
	struct pp_sched_req_flood *in_glossy_sched_req = (struct pp_sched_req_flood *) buf;

	// Due to frequent overflow in the decawave system time counter, we must keep a running total
	// of the number of times it's overflown
	if(dw_timestamp < _last_overall_timestamp)
		_time_overflow += 0x10000000000ULL;
	_last_overall_timestamp = dw_timestamp;
	dw_timestamp += _time_overflow;

	if(_role == GLOSSY_MASTER){
		// If this is a schedule request, try to fit the requesting tag into the schedule
		if(in_glossy_sync->message_type == MSG_TYPE_PP_GLOSSY_SCHED_REQ){
#ifdef GLOSSY_ANCHOR_SYNC_TEST
			uint64_t actual_turnaround = (dw_timestamp - ((uint64_t)(_last_delay_time) << 8)) & 0xFFFFFFFFFFUL;//in_glossy_sched_req->turnaround_time;
			const uint8_t header[] = {0x80, 0x01, 0x80, 0x01};
			uart_write(4, header);
	
			actual_turnaround = in_glossy_sched_req->turnaround_time - actual_turnaround;
	
			uart_write(1, &(in_glossy_sched_req->tag_sched_eui[0]));
			uart_write(1, &(in_glossy_sched_req->sync_depth));
			uart_write(sizeof(uint32_t), &actual_turnaround);

			dwt_forcetrxoff();
			dw1000_update_channel(1);
			dw1000_choose_antenna(1);
			dwt_rxenable(0);
#else
			int ii;
			for(ii = 0; ii < _cur_sched_tags; ii++){
				if(memcmp(_sched_euis[ii], in_glossy_sched_req->tag_sched_eui, EUI_LEN) == 0){
					_sync_pkt.tag_sched_idx = ii;
					break;
				}
			}
			// If we didn't find the EUI (not scheduled), schedule it!
			if(ii == _cur_sched_tags){
				memcpy(_sched_euis[ii], in_glossy_sched_req->tag_sched_eui, EUI_LEN);
				_cur_sched_tags++;
			}
			memcpy(_sync_pkt.tag_sched_eui, _sched_euis[ii], EUI_LEN);
			_sync_pkt.tag_ranging_mask |= (uint64_t)(1) << ii;
			_sync_pkt.tag_sched_idx = ii;
#endif
		}

#ifdef GLOSSY_PER_TEST
		_total_syncs_received++;
#endif
		return;
	}

	else if(_role == GLOSSY_SLAVE){
		if(in_glossy_sync->message_type == MSG_TYPE_PP_GLOSSY_SCHED_REQ){
#ifndef GLOSSY_ANCHOR_SYNC_TEST
			// Increment depth counter
			_cur_glossy_depth = ++in_glossy_sched_req->header.seqNum;
			_glossy_currently_flooding = TRUE;

			uint16_t frame_len = sizeof(struct pp_sched_req_flood);
			dwt_writetxfctrl(frame_len, 0);

			// Flood out as soon as possible
			uint32_t delay_time = (dw_timestamp >> 8) + (DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE);
			delay_time &= 0xFFFFFFFE;
			_last_delay_time = delay_time;
			dwt_forcetrxoff();
			dwt_setrxaftertxdelay(LWB_SLOT_US);
			dwt_setdelayedtrxtime(delay_time);
			dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
			dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
			dwt_writetxdata(sizeof(struct pp_sched_req_flood), (uint8_t*) in_glossy_sched_req, 0);
#endif
		} else {
			// First check to see if this sync packet contains a schedule update for this node
			if(memcmp(in_glossy_sync->tag_sched_eui, _sched_req_pkt.tag_sched_eui, EUI_LEN) == 0){
				_lwb_timeslot = in_glossy_sync->tag_sched_idx;
				_lwb_scheduled = TRUE;
			}
			_lwb_num_timeslots = uint64_count_ones(in_glossy_sync->tag_ranging_mask);

#ifdef GLOSSY_ANCHOR_SYNC_TEST
			_sched_req_pkt.sync_depth = in_glossy_sync->header.seqNum;
#endif

			if(_last_sync_timestamp + ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US * 0.5)) << 8) < dw_timestamp){
				if(_last_sync_timestamp + ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US * 1.5)) << 8) > dw_timestamp){
					// If we're between 0.5 to 1.0 times the update interval, we are now able to update our clock and perpetuate the flood!
			
					// Calculate the ppm offset from the last two received sync messages
					double clock_offset_ppm = (((double)(dw_timestamp - 
					                                     ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE) << 8)*(in_glossy_sync->header.seqNum) - 
					                                     _last_sync_timestamp) / ((uint64_t)(GLOSSY_UPDATE_INTERVAL_DW) << 8)) - 1.0) * 1e6;
					
					_clock_offset = (clock_offset_ppm/1e6)+1.0;
					_glossy_flood_timeslot_corrected_us = (uint64_t)((double)((uint64_t)(DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE) << 8)*_clock_offset);

					// Great, we're still sync'd!
					_last_sync_depth = in_glossy_sync->header.seqNum;
					_currently_syncd = 1;

					// Since we're sync'd, we should make sure to reset our LWB window timer
					_lwb_counter = 0;
					_lwb_valid = TRUE;
					timer_reset(_glossy_timer, ((uint32_t)(in_glossy_sync->header.seqNum))*GLOSSY_FLOOD_TIMESLOT_US);

					// Update DW1000's crystal trim to account for observed PPM offset
					_last_xtal_trim = _xtal_trim;
					int8_t trim_diff = clock_offset_to_trim_diff(clock_offset_ppm);
					if(trim_diff < -(int8_t)(_xtal_trim))
						_xtal_trim = 0;
					else if(trim_diff + _xtal_trim > 31)
						_xtal_trim = 31;
					else
						_xtal_trim += trim_diff;
					dwt_xtaltrim(_xtal_trim);
#ifdef GLOSSY_ANCHOR_SYNC_TEST
					// Sync is invalidated if the xtal trim has changed (this won't happen often)
					if(_last_xtal_trim != _xtal_trim)
						_sched_req_pkt.sync_depth = 0xFF;
#endif

					// Perpetuate the flood!
					memcpy(&_sync_pkt, in_glossy_sync, sizeof(struct pp_sched_flood));
					_cur_glossy_depth = ++_sync_pkt.header.seqNum;

					uint32_t delay_time = (dw_timestamp >> 8) + (DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE);
					delay_time &= 0xFFFFFFFE;
					dwt_forcetrxoff();
					send_sync(delay_time);

					_glossy_currently_flooding = TRUE;
				} else {
					// We lost sync :(
					_currently_syncd = 0;
				}
			} else {
				// We've just received a following packet in the flood
				// This really shouldn't happen, but for now let's ignore it
			}
			_last_sync_timestamp = dw_timestamp - (_glossy_flood_timeslot_corrected_us * in_glossy_sync->header.seqNum);
		}
	}
}
