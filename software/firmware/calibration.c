#include <stddef.h>
#include <string.h>

#include "deca_device_api.h"
#include "deca_regs.h"

#include "timer.h"

#include "dw1000.h"
#include "firmware.h"
#include "calibration.h"


// All of the configuration passed to us by the host for how this application
// should operate.
static calibration_config_t _config;
// Our local reference to the timer for all of the high-level application
// code.
static stm_timer_t* _app_timer;


// Prepopulated struct of the outgoing broadcast poll packet.
static struct pp_calibration_msg pp_calibration_pkt = {
	.header = {
		.frameCtrl = {
			0x41, // FCF[0]: data frame, panid compression
			0xC8  // FCF[1]: ext source address, compressed destination
		},
		.seqNum = 0,
		.panID = {
			POLYPOINT_PANID & 0xFF,
			POLYPOINT_PANID >> 8
		},
		.destAddr = {
			0xFF, // Destination address: broadcast
			0xFF
		},
		.sourceAddr = { 0 }     // Source (blank for now)
	},
	// PACKET BODY
	.message_type = MSG_TYPE_PP_CALIBRATION,
	.seq = 0
};

static void calibration_txcallback (const dwt_callback_data_t *txd);
static void calibration_rxcallback (const dwt_callback_data_t *txd);



static void init () {
	// Make sure the SPI speed is slow for this function
	dw1000_spi_slow();

	// Setup callbacks to this TAG
	dwt_setcallbacks(calibration_txcallback, calibration_rxcallback);

	// Allow data and ack frames
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	// Setup parameters of how the radio should work
	dwt_setautorxreenable(TRUE);
	dwt_setdblrxbuffmode(TRUE);
	dwt_enableautoack(DW1000_ACK_RESPONSE_TIME);

	// Put source EUI in the pp_tag_poll packet
	dw1000_read_eui(pp_calibration_pkt.header.sourceAddr);

	// Make SPI fast now that everything has been setup
	dw1000_spi_fast();
}

static void calib_start_round () {

}

// Setup the calibration
void calibration_configure (calibration_config_t* config, stm_timer_t* app_timer) {
	// Save the settings
	memcpy(&_config, config, sizeof(calibration_config_t));

	// Save the application timer for use by this application
	_app_timer = app_timer;

	init();
}

void calibration_start () {
	// Determine what to do during calibration based on what index we are
	if (_config.index == 0) {
		// We are the master and start things.
		timer_start(_app_timer, CALIBRATION_ROUND_PERIOD_US, calib_start_round);

	} else if (_config.index > 0 && _config.index <= 2) {
		// Other nodes just enter RX mode
		dwt_rxenable(0);
	}
}

void calibration_stop () {
	timer_stop(_app_timer);
	dwt_forcetrxoff();
}

void calibration_reset (bool resume) {
	init();
	if (resume) {
		calibration_start();
	}
}

static void calibration_txcallback (const dwt_callback_data_t *txd) {

}

static void calibration_rxcallback (const dwt_callback_data_t *txd) {

}

