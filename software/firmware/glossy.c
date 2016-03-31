#include "dw1000.h"
#include "deca_regs.h"
#include "glossy.h"
#include "oneway_common.h"
#include "timer.h"

void send_sync(uint32_t delay_time);

static stm_timer_t* _glossy_timer;
static struct pp_glossy_sync _sync_pkt;
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

#ifdef GLOSSY_PER_TEST
static uint32_t _total_syncs_sent;
static uint32_t _total_syncs_received;
#endif

void glossy_init(glossy_role_e role){
	_sync_pkt = (struct pp_glossy_sync) {
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
		.depth = 0,
		.dw_time_sent = 0,
	};

	_currently_syncd = 0;
	_last_overall_timestamp = 0;
	_time_sent_overflow = 0;
	_last_delay_time = 0;
	_role = role;

#ifdef GLOSSY_PER_TEST
	_total_syncs_sent = 0;
#endif

	// Set crystal trim to mid-range
	_xtal_trim = 15;
	dwt_xtaltrim(_xtal_trim);

	// If the anchor, let's kick off a task which unconditionally kicks off sync messages with depth = 0
	if(role == GLOSSY_MASTER){
		uint8 ldok = OTP_SF_OPS_KICK | OTP_SF_OPS_SEL_TIGHT;
		dwt_writetodevice(OTP_IF_ID, OTP_SF, 1, &ldok); // set load LDE kick bit
		_glossy_timer = timer_init();
		timer_start(_glossy_timer, GLOSSY_UPDATE_INTERVAL_US, glossy_sync_task);
	}
}

void glossy_sync_task(){
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

	uint32_t delay_time = dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(sizeof(struct pp_glossy_sync));
	delay_time &= 0xFFFFFFFE;
	_sync_pkt.dw_time_sent = delay_time + _time_sent_overflow;
	send_sync(delay_time);
}

void send_sync(uint32_t delay_time){
	uint16_t frame_len = sizeof(struct pp_glossy_sync);
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
	struct pp_glossy_sync *in_glossy_sync = (struct pp_glossy_sync *) buf;

	// Due to frequent overflow in the decawave system time counter, we must keep a running total
	// of the number of times it's overflown
	if(dw_timestamp < _last_overall_timestamp)
		_time_overflow += 0x10000000000ULL;
	_last_overall_timestamp = dw_timestamp;
	dw_timestamp += _time_overflow;

	if(_role == GLOSSY_MASTER){
#ifdef GLOSSY_PER_TEST
		_total_syncs_received++;
#endif
		return;
	}

	if(in_glossy_sync->depth == _last_sync_depth){
		// Check to see if this is the next sync message from the depth previously seen
		if(_last_sync_timestamp + ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US * 1.5)) << 8) > dw_timestamp){
			// Calculate the ppm offset from the last two received sync messages
			double clock_offset_ppm = (((double)(dw_timestamp - _last_sync_timestamp) / ((uint64_t)(in_glossy_sync->dw_time_sent - _last_time_sent) << 8)) - 1.0) * 1e6;

			// Great, we're still sync'd!
			_last_sync_timestamp = dw_timestamp;
			_last_time_sent = in_glossy_sync->dw_time_sent;
			_last_sync_depth = in_glossy_sync->depth;
			_currently_syncd = 1;

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
			memcpy(&_sync_pkt, in_glossy_sync, sizeof(struct pp_glossy_sync));
			_sync_pkt.depth = _last_sync_depth+1;

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
			_last_time_sent = in_glossy_sync->dw_time_sent;
			_last_sync_depth = in_glossy_sync->depth;
		} else {
			/* Do Nothing */
		}
	}
}
