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
static uint64_t _time_sent_overflow;
static uint32_t _last_delay_time;
static uint8_t _currently_syncd;
static uint8_t _xtal_trim;
static bool _sending_sync;
static uint32_t _lpm_counter;
static bool _lpm_valid;

static bool _lpm_sched_en;
static bool _lpm_scheduled;
static uint32_t _lpm_num_timeslots;
static uint32_t _lpm_timeslot;
static void (*_lpm_schedule_callback)(void);

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
	_time_sent_overflow = 0;
	_last_delay_time = 0;
	_role = role;
	_sending_sync = FALSE;
	_lpm_counter = 0;
	_cur_sched_tags = 0;

	_lpm_valid = FALSE;
	_lpm_sched_en = FALSE;
	_lpm_scheduled = FALSE;

#ifdef GLOSSY_PER_TEST
	_total_syncs_sent = 0;
#endif

	// Set crystal trim to mid-range
	_xtal_trim = 15;
	dwt_xtaltrim(_xtal_trim);

	// If the anchor, let's kick off a task which unconditionally kicks off sync messages with depth = 0
	if(role == GLOSSY_MASTER){
		_lpm_valid = TRUE;
		uint8 ldok = OTP_SF_OPS_KICK | OTP_SF_OPS_SEL_TIGHT;
		dwt_writetodevice(OTP_IF_ID, OTP_SF, 1, &ldok); // set load LDE kick bit
		_last_time_sent = dwt_readsystimestamphi32() & 0xFFFFFFFE;
	}

	// The glossy timer acts to synchronize everyone to a common timebase
	_glossy_timer = timer_init();
	timer_start(_glossy_timer, LPM_SLOT_US, glossy_sync_task);
}

void glossy_sync_task(){
	_lpm_counter++;

	if(_role == GLOSSY_MASTER){
		// Last timeslot is used by the master to schedule the next glossy sync packet
		if(_lpm_counter == (GLOSSY_UPDATE_INTERVAL_US/LPM_SLOT_US)-1){
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
		if(!_lpm_valid) dwt_rxenable(0);

		else {
			// Check to see if it's our turn to do a ranging event!
			// LPM Slot 1: Contention slot
			if(_lpm_counter == 1){
				if(!_lpm_scheduled && _lpm_sched_en){
					dwt_forcetrxoff();

					// Send out a schedule request during this contention slot
					// Pick a random time offset to avoid colliding with others
					uint32_t sched_req_time = ranval(&_prng_state) % DW_DELAY_FROM_US(LPM_SLOT_US);
					uint32_t delay_time = (dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(sizeof(struct pp_sched_req_flood)) + DW_DELAY_FROM_US(sched_req_time)) & 0xFFFFFFFE;
					dwt_setdelayedtrxtime(delay_time);
					dwt_setrxaftertxdelay(LPM_SLOT_US);
					dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
					dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
					dwt_writetxdata(sizeof(struct pp_sched_req_flood), (uint8_t*) &_sched_req_pkt, 0);
				}

			// LPM Slots 2-N: Ranging slots
			} else if(_lpm_counter < (GLOSSY_UPDATE_INTERVAL_US/LPM_SLOT_US)) {
				if(_lpm_scheduled && (((_lpm_counter - 2)/LPM_SLOTS_PER_RANGE) % _lpm_num_timeslots == _lpm_timeslot) && ((_lpm_counter - 2) % LPM_SLOTS_PER_RANGE == 0)){
					// Our scheduled timeslot!  Call the timeslot callback which will likely kick off a ranging event
					dwt_setdblrxbuffmode(TRUE);
					_lpm_schedule_callback();
					dwt_setdblrxbuffmode(FALSE);
				}
			}
		}
	}
}

void lpm_set_sched_request(bool sched_en){
	_lpm_sched_en = sched_en;
}

void lpm_set_sched_callback(void (*callback)(void)){
	_lpm_schedule_callback = callback;
}

void glossy_process_txcallback(){
	if(_role == GLOSSY_MASTER && _sending_sync){
		// Sync has sent, set the timer to send the next one at a later time
		timer_reset(_glossy_timer, 0);
		_lpm_counter = 0;
		_sending_sync = FALSE;
	}
}

void send_sync(uint32_t delay_time){
	uint16_t frame_len = sizeof(struct pp_sched_flood);
	dwt_writetxfctrl(frame_len, 0);

	if(delay_time < _last_delay_time)
		_time_sent_overflow += 0x100000000ULL;
	_last_delay_time = delay_time;

	dwt_setdelayedtrxtime(delay_time);
	dwt_setrxaftertxdelay(1);

	dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
	dwt_writetxdata(sizeof(_sync_pkt), (uint8_t*) &_sync_pkt, 0);
}

#define CW_CAL_12PF ((3.494350-3.494173)/3.4944*1e6/30)
#define CW_CAL_22PF ((3.494078-3.493998)/3.4944*1e6/30)
#define CW_CAL_33PF ((3.493941-3.493891)/3.4944*1e6/30)
int8_t clock_offset_to_trim_diff(double ppm_offset){
       return (int8_t) (ppm_offset/CW_CAL_22PF + 0.5);
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
			int ii;
			for(ii = 0; ii < _cur_sched_tags; ii++){
				if(memcmp(_sched_euis[ii], in_glossy_sched_req->tag_sched_eui, EUI_LEN) == 0){
					_sync_pkt.tag_sched_idx = ii;
					break;
				}
			}
			// If we didn't find the EUI (not scheduled), schedule it!
			if(ii == _cur_sched_tags){
				_cur_sched_tags++;
			}
			memcpy(_sync_pkt.tag_sched_eui, _sched_euis[ii], EUI_LEN);
			_sync_pkt.tag_ranging_mask |= (uint64_t)(1) << ii;
			_sync_pkt.tag_sched_idx = ii;
		}

#ifdef GLOSSY_PER_TEST
		_total_syncs_received++;
#endif
		return;
	}

	else if(_role == GLOSSY_SLAVE){
		if(in_glossy_sync->message_type == MSG_TYPE_PP_GLOSSY_SCHED_REQ){
			// Increment depth counter
			in_glossy_sched_req->header.seqNum++;

			// Flood out as soon as possible
			uint32_t delay_time = (dw_timestamp >> 8) + DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US);
			delay_time &= 0xFFFFFFFE;
			dwt_forcetrxoff();
			dwt_setrxaftertxdelay(LPM_SLOT_US);
			dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
			dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
			dwt_writetxdata(sizeof(struct pp_sched_req_flood), (uint8_t*) in_glossy_sched_req, 0);
		} else {
			// First check to see if this sync packet contains a schedule update for this node
			if(memcmp(in_glossy_sync->tag_sched_eui, _sched_req_pkt.tag_sched_eui, EUI_LEN) == 0){
				_lpm_timeslot = in_glossy_sync->tag_sched_idx;
				_lpm_scheduled = TRUE;
			}
			_lpm_num_timeslots = uint64_count_ones(in_glossy_sync->tag_ranging_mask);

			if(in_glossy_sync->header.seqNum == _last_sync_depth){
				// Check to see if this is the next sync message from the depth previously seen
				if(_last_sync_timestamp + ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US * 1.5)) << 8) > dw_timestamp){
					// Calculate the ppm offset from the last two received sync messages
					double clock_offset_ppm = (((double)(dw_timestamp - _last_sync_timestamp) / ((uint64_t)(GLOSSY_UPDATE_INTERVAL_DW) << 8)) - 1.0) * 1e6;

					// Great, we're still sync'd!
					_last_sync_timestamp = dw_timestamp;
					_last_sync_depth = in_glossy_sync->header.seqNum;
					_currently_syncd = 1;

					// Since we're sync'd, we should make sure to reset our LPM window timer
					_lpm_counter = 0;
					_lpm_valid = TRUE;
					timer_reset(_glossy_timer, in_glossy_sync->header.seqNum*GLOSSY_FLOOD_TIMESLOT_US);

					// Update DW1000's crystal trim to account for observed PPM offset
					int8_t trim_diff = clock_offset_to_trim_diff(clock_offset_ppm);
					if(trim_diff < -(int8_t)(_xtal_trim))
						_xtal_trim = 0;
					else if(trim_diff + _xtal_trim > 31)
						_xtal_trim = 31;
					else
						_xtal_trim += trim_diff;
					dwt_xtaltrim(_xtal_trim);

					// Perpetuate the flood!
					memcpy(&_sync_pkt, in_glossy_sync, sizeof(struct pp_sched_flood));
					_sync_pkt.header.seqNum = _last_sync_depth+1;

					uint32_t delay_time = (dw_timestamp >> 8) + DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US);
					delay_time &= 0xFFFFFFFE;
					dwt_forcetrxoff();
					send_sync(delay_time);
				} else {
					// We lost sync :(
					_currently_syncd = 0;
					_last_sync_depth = 0xFF;
				}
			} else {
				// Ignore this sync message if we're currently sync'd at some other depth
				if(_last_sync_timestamp + ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US * 1.5)) << 8) < dw_timestamp) {
					// If it's been too long since our last received sync packet, let's try things at a different depth
					_last_sync_timestamp = dw_timestamp;
					_last_sync_depth = in_glossy_sync->header.seqNum;
				} else {
					/* Do Nothing */
				}
			}
		}
	}
}
