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

// Constant for the number of UWB channels
#define DW1000_NUM_CHANNELS 8

// Length of addresses in the system
#define EUI_LEN 8


/******************************************************************************/
// Timing defines for this particular MCU
/******************************************************************************/

#define APP_US_TO_DEVICETIMEU32(_microsecu) \
	((uint32_t) ( ((_microsecu) / (double) DWT_TIME_UNITS) / 1e6 ))

#define SPI_US_PER_BYTE        0.94	// 0.94 @ 8mhz, 0.47 @ 16mhz
#define SPI_US_BETWEEN_BYTES   0.25	// 0.25 @ 8mhz, 0.30 @ 16mhz
#define SPI_SLACK_US           200	// 200 @ 8mhz, 150 @ 16mhz
#define DW_DELAY_FROM_PKT_LEN(_len) \
	(APP_US_TO_DEVICETIMEU32(SPI_US_PER_BYTE * (_len) + SPI_US_BETWEEN_BYTES * (_len) + SPI_SLACK_US) >> 8)


#define DW_DELAY_FROM_US(_us) (APP_US_TO_DEVICETIMEU32((_us)) >> 8)

#define ANC_FINAL_RX_PKT_TIME_US	398  // 8mhz: 398; 16mhz: 256
#define ANC_FINAL_RX_PKT_MEMCPY_TIME_US	 79  // 8mhz: 120
#define ANC_FINAL_RX_PKT_PRINTF_TIME_US	150
#define ANC_FINAL_RX_PKT_GUARD_US	100
#ifdef DW_DEBUG
#define ANC_FINAL_RX_TIME_ON_TAG	(\
		ANC_FINAL_RX_PKT_TIME_US +\
		ANC_FINAL_RX_PKT_MEMCPY_TIME_US +\
		ANC_FINAL_RX_PKT_PRINTF_TIME_US +\
		ANC_FINAL_RX_PKT_GUARD_US\
		)
#else
#define ANC_FINAL_RX_TIME_ON_TAG	(\
		ANC_FINAL_RX_PKT_TIME_US +\
		ANC_FINAL_RX_PKT_MEMCPY_TIME_US +\
		ANC_FINAL_RX_PKT_GUARD_US\
		)
#endif

#define ANC_FINAL_INITIAL_DELAY_HACK_VALUE 300	// See note in anchor.c

/******************************************************************************/
// Parameters for the localization and ranging protocol
/******************************************************************************/

// How many of the DW1000 supported UWB channels we are using for ranging
// packets.
#define NUM_RANGING_CHANNELS 3

// It's possible that someday the number of antennas should be configurable
// to support different hardware...
#define NUM_ANTENNAS 3

// Number of packets with unique antenna and channel combinations
#define NUM_UNIQUE_PACKET_CONFIGURATIONS NUM_RANGING_CHANNELS*NUM_ANTENNAS*NUM_ANTENNAS

// Use 30 broadcasts from the tag for ranging.
// (3 channels * 3 antennas on tag * 3 antennas on anchor) + 2
// We iterate through the first 2 twice so that we can make sure we made
// contact will all anchors, even if the anchors aren't listening on the
// first channel, plus we don't lose the first two if the anchor was listening
// on the third channel.
#define NUM_RANGING_BROADCASTS ((NUM_RANGING_CHANNELS*NUM_ANTENNAS*NUM_ANTENNAS) + NUM_RANGING_CHANNELS)

// How much time between each ranging broadcast in the subsequence from the tag.
#define RANGING_BROADCASTS_PERIOD_US 1000

// Listen for responses from the anchors on different channels
#define NUM_RANGING_LISTENING_WINDOWS 3

// How much time the tag listens on each channel when receiving packets from the anchor
#define RANGING_LISTENING_WINDOW_US 10000

// How many slots should be in each listening window for the anchors to respond
// in.
#define NUM_RANGING_LISTENING_SLOTS 20

// How long the slots inside each window should be for the anchors to choose from
#define RANGING_LISTENING_SLOT_US RANGING_LISTENING_WINDOW_US/NUM_RANGING_LISTENING_SLOTS

// Maximum number of anchors a tag is willing to hear from
#define MAX_NUM_ANCHOR_RESPONSES 6

// Reasonable constants to rule out unreasonable ranges
#define MIN_VALID_RANGE_MM -1000      // -1 meter
#define MAX_VALID_RANGE_MM (50*1000)  // 50 meters

// How many valid ranges we have to get from the anchor in order to bother
// including it in our calculations for the distance to the tag.
#define MIN_VALID_RANGES_PER_ANCHOR 10

// When the tag is calculating range for each of the anchors given a bunch
// of measurements, these define which percentile of the measurements to use.
// They are split up to facilitate non-floating point math.
// EXAMPLE: N=1, D=10 means take the 90th percentile.
#define RANGE_PERCENTILE_NUMERATOR 1
#define RANGE_PERCENTILE_DENOMENATOR 10

// Constants
#define SPEED_OF_LIGHT 299702547.0

/******************************************************************************/
// Data Structs for packet messages between tags and anchors
/******************************************************************************/

#define MSG_TYPE_PP_ONEWAY_TAG_POLL   0x60
#define MSG_TYPE_PP_ONEWAY_TAG_FINAL  0x6F
#define MSG_TYPE_PP_ONEWAY_ANC_FINAL  0x70
#define MSG_TYPE_PP_NOSLOTS_TAG_POLL   0x80
#define MSG_TYPE_PP_NOSLOTS_ANC_FINAL  0x81

// Size buffers for reading in packets
#define DW1000_TAG_MAX_RX_PKT_LEN 128

struct ieee154_header_broadcast {
	uint8_t frameCtrl[2];          //  frame control bytes 00-01
	uint8_t seqNum;                //  sequence_number 02
	uint8_t panID[2];              //  PAN ID 03-04
	uint8_t destAddr[2];
	uint8_t sourceAddr[EUI_LEN];
};

struct ieee154_header_unicast {
	uint8_t frameCtrl[2];          //  frame control bytes 00-01
	uint8_t seqNum;                //  sequence_number 02
	uint8_t panID[2];              //  PAN ID 03-04
	uint8_t destAddr[EUI_LEN];
	uint8_t sourceAddr[EUI_LEN];
};

struct ieee154_footer {
	uint8_t fcs[2];                //  we allow space for the CRC as it is logically part of the message. However ScenSor TX calculates and adds these bytes.
};

// Packet the tag broadcasts to all nearby anchors
struct pp_tag_poll  {
	struct ieee154_header_broadcast header;
	uint8_t message_type;                   // Packet type identifier so the anchor knows what it is receiving.
	uint8_t subsequence;                    // Index of which broadcast sequence number this packet is.
	uint8_t reply_after_subsequence;        // Tells anchor which broadcast subsequence number to respond after.
	uint16_t anchor_reply_window_in_us;     // How long each anchor response window is. Each window allows multiple anchor responses.
	uint16_t anchor_reply_slot_time_in_us;  // How long that slots that break up each window are.
	struct ieee154_footer footer;
} __attribute__ ((__packed__));

// Packet the anchor sends back to the tag.
struct pp_anc_final {
	struct ieee154_header_unicast ieee154_header_unicast;
	uint8_t message_type;
	uint8_t final_antenna;                 // The antenna the anchor used when sending this packet.
	uint32_t dw_time_sent;                 // The anchor timestamp of when it sent this packet
	uint64_t TOAs[NUM_RANGING_BROADCASTS]; // The anchor timestamps of when it received the tag poll messages.
	struct ieee154_footer footer;
} __attribute__ ((__packed__));


/******************************************************************************/
// Helper macros for working with the DW1000
/******************************************************************************/

#define DW_TIMESTAMP_TO_UINT64(dwts) \
	(((uint64_t) dwts[0]) << 0)  | \
	(((uint64_t) dwts[1]) << 8)  | \
	(((uint64_t) dwts[2]) << 16) | \
	(((uint64_t) dwts[3]) << 24) | \
	(((uint64_t) dwts[4]) << 32)


/******************************************************************************/
// Structs and what-not for control flow throughout the DW1000 code
/******************************************************************************/

// Enum for what role this particular module should do
typedef enum {
	TAG,
	ANCHOR,
	UNDECIDED
} dw1000_role_e;

// Enum for what the module should provide the host.
typedef enum {
	REPORT_MODE_RANGES = 0,   // Return just range measurements to anchors
	REPORT_MODE_LOCATION = 1  // Determine location and provide location coordinates
} dw1000_report_mode_e;

// Enum for when the TAG should do a ranging event
typedef enum {
	UPDATE_MODE_PERIODIC = 0,  // Range at regular intervals
	UPDATE_MODE_DEMAND = 1     // Range only when the host instructs
} dw1000_update_mode_e;



typedef enum {
	DW1000_INIT_DONE,
} dw1000_cb_e;

typedef enum {
	DW1000_NO_ERR = 0,
	DW1000_COMM_ERR,
	DW1000_BUSY,
} dw1000_err_e;

// gets called with event that just finished and an error code
typedef void (*dw1000_callback)(dw1000_cb_e, dw1000_err_e);

void dw1000_spi_fast ();
// void dw1000_spi_slow ();

int dwtime_to_millimeters (double dwtime);
void insert_sorted (int arr[], int new, unsigned end);


dw1000_err_e dw1000_init ();
void dw1000_reset ();
void dw1000_choose_antenna (uint8_t antenna_number);
void dw1000_read_eui (uint8_t *eui_buf);
void dw1000_set_mode (dw1000_role_e role);
uint8_t subsequence_number_to_antenna (dw1000_role_e role, uint8_t subseq_num);
void dw1000_set_ranging_broadcast_subsequence_settings (dw1000_role_e role, uint8_t subseq_num, bool reset);
void dw1000_set_ranging_listening_window_settings (dw1000_role_e role, uint8_t slot_num, uint8_t antenna_num,bool reset);
uint8_t dw1000_get_ss_index_from_settings (uint8_t anchor_antenna_index, uint8_t window_num);

void dw1000_interrupt_fired ();

#endif
