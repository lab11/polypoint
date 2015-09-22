#ifndef __FIRMWARE_H
#define __FIRMWARE_H

#include "dw1000.h"

// All of the possible interrupt sources.
typedef enum {
	INTERRUPT_TIMER_17,
	INTERRUPT_TIMER_16,
	INTERRUPT_DW1000,
	INTERRUPT_I2C_RX,
	INTERRUPT_I2C_TX,
	INTERRUPT_I2C_TIMEOUT,
	NUMBER_INTERRUPT_SOURCES
} interrupt_source_e;


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


// Keep config settings for the TAG
typedef struct {
	dw1000_report_mode_e report_mode;
	dw1000_update_mode_e update_mode;
	uint8_t update_rate;
	bool sleep_mode;
} dw1000_tag_config_t;

typedef struct {
	uint8_t  anchor_addr[EUI_LEN];
	uint8_t  anchor_final_antenna_index; // The antenna the anchor used when it responded.
	uint8_t  window_packet_recv;         // The window the tag was in when it received the packet from the anchor.
	uint64_t anc_final_tx_timestamp; // When the anchor node sent the ANC_FINAL
	uint64_t anc_final_rx_timestamp; // When the tag received the ANC_FINAL
	uint64_t tag_poll_TOAs[NUM_RANGING_BROADCASTS];
} anchor_responses_t;



/******************************************************************************/
// I2C for the application
/******************************************************************************/
#define I2C_OWN_ADDRESS         0x65

// Identification byte that we return to an interested client. This is useful
// for initializing and debugging to make sure that the I2C connection is
// working
#define INFO_BYTE_0 0xB0
#define INFO_BYTE_1 0x1A

/******************************************************************************/
// Main firmware application functions.
/******************************************************************************/
void app_configure_tag (dw1000_report_mode_e report_mode,
                        dw1000_update_mode_e update_mode,
                        bool sleep_mode,
                        uint8_t update_rate);
void app_configure_anchor ();
void app_start ();
void app_stop ();
void app_reset ();
bool app_ready ();

void app_tag_do_range ();

dw1000_report_mode_e app_get_report_mode ();
void app_set_ranges (int32_t* ranges_millimeters, anchor_responses_t* anchor_responses);

/******************************************************************************/
// OS functions.
/******************************************************************************/
void mark_interrupt (interrupt_source_e src);

#endif
