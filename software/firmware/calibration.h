#ifndef __CALIBRATION_H
#define __CALIBRATION_H


typedef struct {
	uint8_t index;
} calibration_config_t;

// How long the master waits before starting a new round of calibration
// timing.
#define CALIBRATION_ROUND_PERIOD_US 100000 // 100 ms

// How many TriPoint nodes participate in calibration
#define CALIBRATION_NUM_NODES 3

// How many antennas we want to calibrate with
#define CALIB_NUM_ANTENNAS 3
#define CALIB_NUM_CHANNELS 3

#define CALIBRATION_MAX_RX_PKT_LEN 128

// Message types that identify the UWB packets. Very reminiscent of
// Active Messages from the TinyOS days.
#define MSG_TYPE_PP_CALIBRATION_INIT 0x90
#define MSG_TYPE_PP_CALIBRATION_MSG  0x91

// Packet the tag broadcasts to all nearby anchors
struct pp_calibration_msg  {
	struct ieee154_header_broadcast header;
	uint8_t message_type;                   // Packet type identifier so the anchor knows what it is receiving.
	uint32_t seq;                           // Index of which broadcast so nodes know if they should receive
	uint8_t num;                            // Index of this packet in the ranging round.
	struct ieee154_footer footer;
} __attribute__ ((__packed__));


void calibration_configure (calibration_config_t* config, stm_timer_t* app_timer);
void calibration_start ();
void calibration_stop ();
void calibration_reset ();

#endif
