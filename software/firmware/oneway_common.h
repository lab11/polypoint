#ifndef __ONEWAY_COMMON_H
#define __ONEWAY_COMMON_H

#include "polypoint_conf.h"
#include "system.h"
#include "dw1000.h"
#include "timer.h"

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
#define NUM_UNIQUE_PACKET_CONFIGURATIONS (NUM_RANGING_CHANNELS*NUM_ANTENNAS*NUM_ANTENNAS)

// Use 30 broadcasts from the tag for ranging.
// (3 channels * 3 antennas on tag * 3 antennas on anchor) + 2
// We iterate through the first 2 twice so that we can make sure we made
// contact will all anchors, even if the anchors aren't listening on the
// first channel, plus we don't lose the first two if the anchor was listening
// on the third channel.
#define NUM_RANGING_BROADCASTS ((NUM_RANGING_CHANNELS*NUM_ANTENNAS*NUM_ANTENNAS) + NUM_RANGING_CHANNELS)

// Listen for responses from the anchors on different channels
#define NUM_RANGING_LISTENING_WINDOWS 3

// How many slots should be in each listening window for the anchors to respond
// in.
#define NUM_RANGING_LISTENING_SLOTS 20

// How long the slots inside each window should be for the anchors to choose from
#define RANGING_LISTENING_SLOT_US (RANGING_LISTENING_WINDOW_US/NUM_RANGING_LISTENING_SLOTS)

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


/******************************************************************************/
// Data Structs for packet messages between tags and anchors
/******************************************************************************/

// Message types that identify the UWB packets. Very reminiscent of
// Active Messages from the TinyOS days.
#define MSG_TYPE_PP_NOSLOTS_TAG_POLL  0x80
#define MSG_TYPE_PP_NOSLOTS_ANC_FINAL 0x81

// Packet the tag broadcasts to all nearby anchors
struct pp_tag_poll  {
	struct ieee154_header_broadcast header;
	uint8_t message_type;                   // Packet type identifier so the anchor knows what it is receiving.
	uint8_t subsequence;                    // Index of which broadcast sequence number this packet is.
	uint8_t reply_after_subsequence;        // Tells anchor which broadcast subsequence number to respond after.
	uint32_t anchor_reply_window_in_us;     // How long each anchor response window is. Each window allows multiple anchor responses.
	uint16_t anchor_reply_slot_time_in_us;  // How long that slots that break up each window are.
	struct ieee154_footer footer;
} __attribute__ ((__packed__));

// Packet the anchor sends back to the tag.
struct pp_anc_final {
	struct ieee154_header_unicast ieee154_header_unicast;
	uint8_t message_type;
	uint8_t final_antenna;                 // The antenna the anchor used when sending this packet.
	uint64_t dw_time_sent;                 // The anchor timestamp of when it sent this packet
	uint64_t TOAs[NUM_RANGING_BROADCASTS]; // The anchor timestamps of when it received the tag poll messages.
	struct ieee154_footer footer;
} __attribute__ ((__packed__));


/******************************************************************************/
// State objects for the oneway application
/******************************************************************************/

// Enum for what the module should provide the host.
typedef enum {
	ONEWAY_REPORT_MODE_RANGES = 0,   // Return just range measurements to anchors
	ONEWAY_REPORT_MODE_LOCATION = 1  // Determine location and provide location coordinates
} oneway_report_mode_e;

// Enum for when the TAG should do a ranging event
typedef enum {
	ONEWAY_UPDATE_MODE_PERIODIC = 0,  // Range at regular intervals
	ONEWAY_UPDATE_MODE_DEMAND = 1     // Range only when the host instructs
} oneway_update_mode_e;

// Keep config settings for a oneway node
typedef struct {
	dw1000_role_e my_role;
	oneway_report_mode_e report_mode;
	oneway_update_mode_e update_mode;
	uint8_t update_rate;
	bool sleep_mode;
} oneway_config_t;

typedef struct {
	uint8_t  anchor_addr[EUI_LEN];
	uint8_t  anchor_final_antenna_index; // The antenna the anchor used when it responded.
	uint8_t  window_packet_recv;         // The window the tag was in when it received the packet from the anchor.
	uint64_t anc_final_tx_timestamp; // When the anchor node sent the ANC_FINAL
	uint64_t anc_final_rx_timestamp; // When the tag received the ANC_FINAL
	uint64_t tag_poll_TOAs[NUM_RANGING_BROADCASTS];
} anchor_responses_t;


void oneway_configure (oneway_config_t* config, stm_timer_t* app_timer, void *app_scratchspace);
void oneway_start ();
void oneway_stop ();
void oneway_reset ();
void oneway_do_range ();
oneway_config_t* oneway_get_config ();
void oneway_set_ranges (int32_t* ranges_millimeters, anchor_responses_t* anchor_responses);


uint8_t oneway_subsequence_number_to_antenna (dw1000_role_e role, uint8_t subseq_num);
void oneway_set_ranging_broadcast_subsequence_settings (dw1000_role_e role, uint8_t subseq_num);
void oneway_set_ranging_listening_window_settings (dw1000_role_e role, uint8_t slot_num, uint8_t antenna_num);
uint8_t oneway_get_ss_index_from_settings (uint8_t anchor_antenna_index, uint8_t window_num);
uint64_t oneway_get_txdelay_from_subsequence (dw1000_role_e role, uint8_t subseq_num);
uint64_t oneway_get_rxdelay_from_subsequence (dw1000_role_e role, uint8_t subseq_num);
uint64_t oneway_get_txdelay_from_ranging_listening_window (uint8_t window_num);
uint64_t oneway_get_rxdelay_from_ranging_listening_window (uint8_t window_num);

#endif
