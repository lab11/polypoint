#include "dw1000.h"
#include "glossy.h"
#include "oneway_common.h"
#include "timer.h"

void send_sync(uint32_t delay_time);

static stm_timer_t* _glossy_timer;
static struct pp_glossy_sync _sync_pkt;

static uint8_t _last_sync_depth;
static uint64_t _last_sync_timestamp;
static uint32_t _last_time_sent;
static uint8_t _currently_syncd;
static uint8_t _xtal_trim;

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

	// If the anchor, let's kick off a task which unconditionally kicks off sync messages with depth = 0
	if(role == GLOSSY_MASTER){
		_glossy_timer = timer_init();
		timer_start(_glossy_timer, GLOSSY_UPDATE_INTERVAL_US, glossy_sync_task);
	}
}

void glossy_sync_task(){
	dwt_forcetrxoff();

	dw1000_update_channel(1);
	dw1000_choose_antenna(0);

	uint32_t delay_time = dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(sizeof(struct pp_glossy_sync));
	delay_time &= 0xFFFFFFFE;
	send_sync(delay_time);
}

void send_sync(uint32_t delay_time){
	uint16_t frame_len = sizeof(struct pp_glossy_sync);
	dwt_writetxfctrl(frame_len, 0);

	_sync_pkt.dw_time_sent = delay_time;

	dwt_setdelayedtrxtime(delay_time);

	dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
	dwt_writetxdata(sizeof(_sync_pkt), (uint8_t*) &_sync_pkt, 0);
}

#define CW_CAL_12PF ((3.494350-3.494173)/3.4944*1e6/30)
uint8_t clock_offset_to_trim_diff(double ppm_offset){
	return (uint8_t) (ppm_offset/CW_CAL_12PF);
}

void glossy_sync_process(uint64_t dw_timestamp, uint8_t *buf){
	struct pp_glossy_sync *in_glossy_sync = (struct pp_glossy_sync *) buf;

	if(in_glossy_sync->depth == _last_sync_depth){
		// Check to see if this is the next sync message from the depth previously seen
		if(_last_sync_timestamp + ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US * 1.5)) << 8) > dw_timestamp){
			// Calculate the ppm offset from the last two received sync messages
			double clock_offset_ppm = (((double)(dw_timestamp - _last_sync_timestamp) / ((_last_time_sent - in_glossy_sync->dw_time_sent) << 8)) - 1.0) * 1e6;

			// Great, we're still sync'd!
			_last_sync_timestamp = dw_timestamp;
			_last_time_sent = in_glossy_sync->dw_time_sent;
			_last_sync_depth = in_glossy_sync->depth;
			_currently_syncd = 1;

			// Update DW1000's crystal trim to account for observed PPM offset
			_xtal_trim += clock_offset_to_trim_diff(clock_offset_ppm);
			dwt_xtaltrim(_xtal_trim);

			// Perpetuate the flood!
			memcpy(&_sync_pkt, in_glossy_sync, sizeof(struct pp_glossy_sync));
			_sync_pkt.depth = _last_sync_depth+1;

			uint32_t delay_time = (dw_timestamp >> 8) + DW_DELAY_FROM_US(dw1000_preamble_time_in_us() + SPI_SLACK_US + GLOSSY_FLOOD_TIMESLOT_US);
			delay_time = 0xFFFFFFFE;
			send_sync(delay_time);
		} else {
			// We lost sync :(
			_currently_syncd = 0;
		}
	} else {
		// Ignore this sync message if we're currently sync'd at some other depth
		if(_currently_syncd){ /* Do Nothing */ }
		else {
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
}
