#ifndef __FIRMWARE_H
#define __FIRMWARE_H

#include "dw1000.h"

// States the main application can be in.
typedef enum {
	APPSTATE_NOT_INITED,
	APPSTATE_STOPPED,
	APPSTATE_RUNNING
} app_state_e;

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

// Enum for what ranging application to run on this node
typedef enum {
	APP_ONEWAY = 0,
	APP_CALIBRATION = 1,
} polypoint_application_e;


/******************************************************************************/
// Define our PANID
/******************************************************************************/
#define POLYPOINT_PANID 0x6611

/******************************************************************************/
// I2C for the application
/******************************************************************************/
#define I2C_OWN_ADDRESS 0x65

// Identification byte that we return to an interested client. This is useful
// for initializing and debugging to make sure that the I2C connection is
// working
#define INFO_BYTE_0 0xB0
#define INFO_BYTE_1 0x1A

/******************************************************************************/
// Main firmware application functions.
/******************************************************************************/
void polypoint_configure_app (polypoint_application_e app, void* app_config);
void polypoint_start ();
void polypoint_stop ();
void polypoint_reset ();
bool polypoint_ready ();
void polypoint_tag_do_range ();

/******************************************************************************/
// OS functions.
/******************************************************************************/
void mark_interrupt (interrupt_source_e src);

#endif
