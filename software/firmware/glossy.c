#include "dw1000.h"
#include "glossy.h"
#include "timer.h"

static stm_timer_t* _glossy_timer;
static struct pp_glossy_sync _sync_pkt;

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
		.depth = 0,
		.dw_time_sent = 0,
	};

	// If the anchor, let's kick off a task which unconditionally kicks off sync messages with depth = 0
	if(role == GLOSSY_MASTER){
		timer_start(_glossy_timer, GLOSSY_UPDATE_INTERVAL_US, glossy_sync_task);
	}
}

void glossy_sync_task(){
	uint16_t frame_len = sizeof(struct pp_glossy_sync);
	dwt_writetxfctrl(frame_len, 0);

	uint32_t delay_time = dwt_readsystimestamphi32() + DW_DELAY_FROM_US(dw1000_preamble_time_in_us() + 0);
	delay_time &= 0xFFFFFFFE;

	_sync_pkt.dw_time_sent = delay_time;

	dwt_setdelayedtrxtime(delay_time);

	dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
	dwt_writetxdata(sizeof(_sync_pkt), (uint8_t*) &_sync_pkt, 0);
}
