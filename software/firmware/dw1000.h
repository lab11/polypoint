#ifndef __DW1000_H
#define __DW1000_H

#include "firmware.h"

/******************************************************************************/
// General defines for the DW1000
/******************************************************************************/
#define POLYPOINT_PANID 0x6611

// Configure how long after a reception the ack is sent
#define DW1000_ACK_RESPONSE_TIME 5

// Whether the OTP has been setup (or we expect it to be)
// This is where constants for calibration of a given device and its
// radios should go.
#define DW1000_USE_OTP 0

// The antenna delays are set to 0 because our other calibration takes
// care of this.
#define DW1000_ANTENNA_DELAY_TX 0
#define DW1000_ANTENNA_DELAY_RX 0

/******************************************************************************/
// Timing defines for this particular mcu
/******************************************************************************/

#define APP_US_TO_DEVICETIMEU32(_microsecu) \
	((uint32_t) ( ((_microsecu) / (double) DWT_TIME_UNITS) / 1e6 ))

#define SPI_US_PER_BYTE        0.94	// 0.94 @ 8mhz, 0.47 @ 16mhz
#define SPI_US_BETWEEN_BYTES   0.25	// 0.25 @ 8mhz, 0.30 @ 16mhz
#define SPI_SLACK_US           200	// 200 @ 8mhz, 150 @ 16mhz
#define DW_DELAY_FROM_PKT_LEN(_len) \
	(APP_US_TO_DEVICETIMEU32(SPI_US_PER_BYTE * (_len) + SPI_US_BETWEEN_BYTES * (_len) + SPI_SLACK_US) >> 8)


/******************************************************************************/
// Parameters for the localization and ranging protocol
/******************************************************************************/

#define NUM_RANGING_CHANNELS 3

// It's possible that someday the number of antennas should be configurable
// to support different hardware...
#define NUM_ANTENNAS 3

// Use 30 broadcasts from the tag for ranging.
// (3 channels * 3 antennas on tag * 3 antennas on anchor) + 2
// We iterate through the first 2 twice so that we can make sure we made
// contact will all anchors, even if the anchors aren't listening on the
// first channel, plus we don't lose the first two if the anchor was listening
// on the third channel.
#define NUM_RANGING_BROADCASTS ((NUM_RANGING_CHANNELS*NUM_ANTENNAS*NUM_ANTENNAS) + (NUM_RANGING_CHANNELS-1))

// How much time between each ranging broadcast in the subsequence from the tag.
#define RANGING_BROADCASTS_PERIOD_US 1000

/******************************************************************************/
// Data Structs for packet messages between tags and anchors
/******************************************************************************/

#define MSG_TYPE_PP_ONEWAY_TAG_POLL   0x60
#define MSG_TYPE_PP_ONEWAY_TAG_FINAL  0x6F
#define MSG_TYPE_PP_ONEWAY_ANC_FINAL  0x70

struct ieee154_header_broadcast {
	uint8_t frameCtrl[2];          //  frame control bytes 00-01
	uint8_t seqNum;                //  sequence_number 02
	uint8_t panID[2];              //  PAN ID 03-04
	uint8_t destAddr[2];
	uint8_t sourceAddr[8];
};

struct ieee154_footer {
	uint8_t fcs[2] ;                                  //  we allow space for the CRC as it is logically part of the message. However ScenSor TX calculates and adds these bytes.
};

struct pp_tag_poll  {
	struct ieee154_header_broadcast header;
	uint8_t message_type;
	uint8_t roundNum;
	uint8_t subsequence;
	struct ieee154_footer footer;
} __attribute__ ((__packed__));




typedef enum {
	TAG,
	ANCHOR,
	UNDECIDED
} dw1000_role_e;


typedef enum {
	DW1000_INIT_DONE,
} dw1000_cb_e;

typedef enum {
	DW1000_NO_ERR = 0,
	DW1000_COMM_ERR,
} dw1000_err_e;

// gets called with event that just finished and an error code
typedef void (*dw1000_callback)(dw1000_cb_e, dw1000_err_e);


dw1000_err_e dw1000_init ();
void dw1000_reset ();
void dw1000_choose_antenna (uint8_t antenna_number);
void dw1000_read_eui (uint8_t *eui_buf);
void dw1000_set_ranging_broadcast_subsequence_settings (dw1000_role_e role, uint8_t subseq_num, bool reset);

void dw1000_interrupt_fired ();

#endif
