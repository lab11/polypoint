#ifndef __CALIBRATION_H
#define __CALIBRATION_H

// Passed in over the host interface.
typedef struct {
	uint8_t index;
} calibration_config_t;

// How long the master waits before starting a new round of calibration
// timing.
#define CALIBRATION_ROUND_PERIOD_US 100000 // 100 ms

// How long non-master nodes wait before assuming something went wrong and
// resume waiting for the start of the next round.
#define CALIBRATION_ROUND_TIMEOUT_US 15000 // 15 ms

// How long between receiving a calibration packet and responding.
// This must be known.
#define CALIBRATION_EPSILON_US 3000 // 3 ms

// How many TriPoint nodes participate in calibration
#define CALIBRATION_NUM_NODES 3

// How many antennas we want to calibrate with
#define CALIB_NUM_ANTENNAS 3
#define CALIB_NUM_CHANNELS 3

#define CALIBRATION_MAX_RX_PKT_LEN 64

// Message types that identify the UWB packets. Very reminiscent of
// Active Messages from the TinyOS days.
#define MSG_TYPE_PP_CALIBRATION_INIT 0x90
#define MSG_TYPE_PP_CALIBRATION_MSG  0x91

#define CALIBRATION_ROUND_STARTED_BY_ME(round_, index_) \
	((round_ % CALIBRATION_NUM_NODES) == index_)

// Returns true if this node index is the one being calibrated on this
// round.
#define CALIBRATION_ROUND_FOR_ME(round_, index_) \
	( (( ((int) (round_%CALIBRATION_NUM_NODES)) - ((int) index_) ) == 1) || (( ((int) (round_%CALIBRATION_NUM_NODES)) - ((int) index_) ) == -2) )

// Packet the tag broadcasts to all nearby anchors
struct pp_calibration_msg  {
	struct ieee154_header_broadcast header;
	uint8_t message_type; // Packet type identifier so the anchor knows what it is receiving.
	uint32_t round_num;   // Index of which which round we are currently in. This sets antenna/channel.
	uint8_t num;          // Index of this packet in the ranging round.
	uint32_t tx_time;
	struct ieee154_footer footer;
} __attribute__ ((__packed__));


void calibration_configure (calibration_config_t* config, stm_timer_t* app_timer);
void calibration_start ();
void calibration_stop ();
void calibration_reset ();
void calib_start_round ();

#endif
