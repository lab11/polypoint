#ifndef __DW1000_H
#define __DW1000_H

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


/******************************************************************************/
// Parameters for the localization and ranging protocol
/******************************************************************************/

#define NUM_RANGING_CHANNELS 3

// It's possible that someday the number of antennas should be configurable
// to support different hardware...
#define NUM_ANTENNAS 3

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
	ANCHOR
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


void dw1000_init(dw1000_callback cb);
void dw1000_reset();
void dw1000_choose_antenna(uint8_t antenna_number);
void dw1000_read_eui(uint8_t *eui_buf);

#endif
