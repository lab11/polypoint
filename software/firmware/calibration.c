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

// Which calibration round we are currently in
static uint32_t _round_num = UINT32_MAX;

// When we sent our START calibration message
static uint64_t _calibration_start_send_time;

// Buffer to send back to the host
static uint8_t _calibration_response_buf[64];


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
	.message_type = MSG_TYPE_PP_CALIBRATION_INIT,
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


static void setup_round_antenna_channel (uint32_t round_num, bool initiator) {
	uint8_t antenna;
	uint8_t channel;
	if (initiator) {
		// This rotates the fastest
		antenna = round_num % CALIB_NUM_ANTENNAS;
	} else {
		antenna = (round_num / CALIB_NUM_ANTENNAS) % CALIB_NUM_ANTENNAS;
	}
	channel = ((round_num / CALIB_NUM_ANTENNAS) / CALIB_NUM_ANTENNAS) % CALIB_NUM_CHANNELS;
	dw1000_choose_antenna(antenna);
	dw1000_update_channel(channel);
}


// Send the
static void send_calibration_pkt (uint8_t message_type) {
	// Record the packet length to send to DW1000
	uint16_t tx_len = sizeof(struct pp_calibration_msg);

	// Setup what needs to change in the outgoing packet
	pp_calibration_pkt.header.seqNum++;
	pp_calibration_pkt.message_type = message_type;
	// Set this to the correct value so that all receivers know if they should
	// be listening for it
	if (message_type == MSG_TYPE_PP_CALIBRATION_INIT) {
		pp_calibration_pkt.seq = _round_num;
	} else if (message_type == MSG_TYPE_PP_CALIBRATION_START) {
		pp_calibration_pkt.seq = (_round_num*CALIBRATION_NUM_NODES)+_config.index;
	}

	// Make sure we're out of RX mode before attempting to transmit
	dwt_forcetrxoff();

	// Tell the DW1000 about the packet
	dwt_writetxfctrl(tx_len, 0);

	// Setup the time the packet will go out at, and save that timestamp
	uint32_t delay_time = dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(tx_len);
	delay_time &= 0xFFFFFFFE; // Make sure last bit is zero
	dwt_setdelayedtrxtime(delay_time);
	_calibration_start_send_time = ((uint64_t) delay_time) << 8;

	// Write the data
	dwt_writetxdata(tx_len, (uint8_t*) &pp_calibration_pkt, 0);

	// Start the transmission and enter RX mode
	dwt_setrxaftertxdelay(1); // us
	dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);

	// MP bug - TX antenna delay needs reprogramming as it is not preserved
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
}

static void send_calibration_pkt_response (uint32_t seq, uint64_t rx_time) {
	// Record the packet length to send to DW1000
	uint16_t tx_len = sizeof(struct pp_calibration_msg);

	// Setup what needs to change in the outgoing packet
	pp_calibration_pkt.header.seqNum++;
	pp_calibration_pkt.message_type = MSG_TYPE_PP_CALIBRATION_RESPONSE;
	// Set this to the correct value so that all receivers know if they should
	// be listening for it
	pp_calibration_pkt.seq = seq;
	pp_calibration_pkt.responder_rx = rx_time;

	// Make sure we're out of RX mode before attempting to transmit
	dwt_forcetrxoff();

	// Tell the DW1000 about the packet
	dwt_writetxfctrl(tx_len, 0);

	// Setup the time the packet will go out at, and save that timestamp
	uint32_t delay_time = dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(tx_len);
	delay_time &= 0xFFFFFFFE; // Make sure last bit is zero
	dwt_setdelayedtrxtime(delay_time);
	pp_calibration_pkt.responder_tx = ((uint64_t) delay_time) << 8;

	// Write the data
	dwt_writetxdata(tx_len, (uint8_t*) &pp_calibration_pkt, 0);

	// Start the transmission and enter RX mode
	dwt_starttx(DWT_START_TX_DELAYED);

	// MP bug - TX antenna delay needs reprogramming as it is not preserved
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
}

// Timer callback that marks the start of each round
static void calib_start_round () {

	// Increment the round number
	if (_round_num == UINT32_MAX) {
		_round_num = 0;
	} else {
		_round_num++;
	}

	// At the start of the round, configure the RX parameters
	setup_round_antenna_channel(_round_num, FALSE);

	// If we are index 0, we start the round
	if (_config.index == 0) {
		// Just need to send the calibration packet now
		send_calibration_pkt(MSG_TYPE_PP_CALIBRATION_INIT);
	}
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
	// Start at round 0, but we increment at the start
	_round_num = UINT32_MAX;

	// Setup channel and antenna settings for round 0
	setup_round_antenna_channel(0, FALSE);

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

// Use this callback to start the next cycle in the round
static void calibration_txcallback (const dwt_callback_data_t *txd) {
	// On TX callback, we may want to send packets to the next node
	// in the calibration system. This occurs if the last thing we did was
	// send a RESPONSE and we are not the node with index 0.
	if (_config.index != 0 &&
	    pp_calibration_pkt.message_type == MSG_TYPE_PP_CALIBRATION_RESPONSE) {
		// Send on the next ranging cycle in this round
		setup_round_antenna_channel(_round_num, TRUE);
		send_calibration_pkt(MSG_TYPE_PP_CALIBRATION_START);
	}
}

// Handle when we receive packets
static void calibration_rxcallback (const dwt_callback_data_t *rxd) {
	if (rxd->event == DWT_SIG_RX_OKAY) {

		// Read in parameters of this packet reception
		uint64_t dw_rx_timestamp;
		uint8_t  buf[CALIBRATION_MAX_RX_PKT_LEN];
		uint8_t  message_type;

		// Get the received time of this packet first
		dwt_readrxtimestamp(buf);
		dw_rx_timestamp = DW_TIMESTAMP_TO_UINT64(buf);

		// Get the actual packet bytes
		dwt_readrxdata(buf, MIN(CALIBRATION_MAX_RX_PKT_LEN, rxd->datalength), 0);

		// We process based on the first byte in the packet. How very active
		// message like...
		message_type = buf[offsetof(struct pp_calibration_msg, message_type)];

		// Packet
		struct pp_calibration_msg* rx_start_pkt = (struct pp_calibration_msg*) buf;

		if (message_type == MSG_TYPE_PP_CALIBRATION_INIT) {
			// Got the start of round message
			// Set the round number, and configure for that round
			_round_num = rx_start_pkt->seq;
			setup_round_antenna_channel(_round_num, FALSE);
			dwt_rxenable(0);

		} else if (message_type == MSG_TYPE_PP_CALIBRATION_START) {
			// Check if this START CALIBRATION message is for us
			uint8_t sender_index = rx_start_pkt->seq % CALIBRATION_NUM_NODES;

			if ((sender_index + 1) % CALIBRATION_NUM_NODES) {
				// This packet was intended for me!
				// Send the response packet back to the initiator
				send_calibration_pkt_response(rx_start_pkt->seq, dw_rx_timestamp);
			} else {
				// We can just ignore this packet, it's not for us
			}

		} else if (message_type == MSG_TYPE_PP_CALIBRATION_RESPONSE) {
			// Verify this came back with our index
			uint8_t sender_index = rx_start_pkt->seq % CALIBRATION_NUM_NODES;
			if (sender_index == _config.index) {
				// This is the response from the remote node.
				// Record the values.
				_calibration_response_buf[0] = _config.index;
				memcpy(_calibration_response_buf+1,  &_round_num, sizeof(uint32_t));
				memcpy(_calibration_response_buf+5,  &_calibration_start_send_time, sizeof(uint64_t));
				memcpy(_calibration_response_buf+13, &rx_start_pkt->responder_rx, sizeof(uint64_t));
				memcpy(_calibration_response_buf+21, &rx_start_pkt->responder_tx, sizeof(uint64_t));
				memcpy(_calibration_response_buf+29, &dw_rx_timestamp, sizeof(uint64_t));
				host_interface_notify_calibration(_calibration_response_buf, 1+sizeof(uint32_t)+(4*sizeof(uint64_t)));
			}

		}

	}
}
