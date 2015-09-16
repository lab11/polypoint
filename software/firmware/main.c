
#include <string.h>

#include "stm32f0xx_tim.h"
#include "stm32f0xx_pwr.h"

#include "tripoint.h"
#include "led.h"

#include "host_interface.h"
#include "dw1000.h"
#include "dw1000_tag.h"
#include "dw1000_anchor.h"
#include "timer.h"
#include "timing.h"
#include "firmware.h"
#include "operation_api.h"



// Put this somewhere??
typedef enum {
	APPSTATE_STOPPED,
	APPSTATE_RUNNING
} app_state_e;


/******************************************************************************/
// OS state
/******************************************************************************/

// Array of interrupt sources. When an interrupt fires, this array gets marked
// noting that the interrupt fired. The main thread then processes this array
// to get all of the functions it should call.
bool interrupts_triggered[NUMBER_INTERRUPT_SOURCES]  = {FALSE};


/******************************************************************************/
// Current application settings as set by the host
/******************************************************************************/
dw1000_tag_config_t _app_tag_config = {
	.report_mode = REPORT_MODE_RANGES,
	.update_mode = UPDATE_MODE_PERIODIC,
	.update_rate = 10
};


/******************************************************************************/
// Current application state
/******************************************************************************/
// Keep track of if the application is active or not
static app_state_e _state = APPSTATE_STOPPED;

// Timer for doing periodic operations (like TAG ranging events)
static timer_t* _periodic_timer;

// Buffer of anchor IDs and ranges to the anchor.
// Long enough to hold an anchor id followed by the range.
uint8_t _anchor_ids_ranges[MAX_NUM_ANCHOR_RESPONSES*(EUI_LEN+sizeof(int32_t))];
uint8_t _num_anchor_ranges = 0;


void start_dw1000 ();

/******************************************************************************/
// "OS" like functions
/******************************************************************************/

// This gets called from interrupt context.
// TODO: Changing interrupts_triggered should be in an atomic block as it is
//       also read from the main loop on the main thread.
void mark_interrupt (interrupt_source_e src) {
	interrupts_triggered[src] = TRUE;
}


static void error () {
	// dw1000_init();
	// led_on(LED2);
	// GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_SET);

			GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_SET);
			uDelay(10000);
			GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_RESET);
}


/******************************************************************************/
// Main operation functions called by the host interface
/******************************************************************************/

// Called by periodic timer
void tag_execute_range_callback () {
	dw1000_err_e err;


	err = dw1000_tag_start_ranging_event();
	if (err == DW1000_BUSY) {
		// TODO: get return value from this function and slow the timer if
		// we starting ranging events too quickly.
	} else if (err == DW1000_WAKEUP_ERR) {
		// DW1000 apparently was in sleep and didn't come back.
		// Not sure why, but we need to reset at this point.
		app_reset();
	}
}

// Call this to configure this TriPoint as a TAG. These settings will be
// preserved, so after this is called, start() and stop() can be used.
// If this is called while the application is stopped, it will not be
// automatically started.
// If this is called when the app is running, the app will be restarted.
void app_configure_tag (dw1000_report_mode_e report_mode,
                        dw1000_update_mode_e update_mode,
                        uint8_t update_rate) {
	bool resume = FALSE;

	// Check if this application is running.
	if (_state == APPSTATE_RUNNING) {
		// Resume with new settings.
		resume = TRUE;
		// Stop this first
		app_stop();
	}

	// Check if we are already configured as a tag. If we have, we don't
	// need to reset this.
	if (dw1000_get_mode() != TAG) {
		// If we need to, complete the init() process now that we know we are a tag.
		dw1000_set_mode(TAG);
	}

	// Save settings so that we can start and stop as needed.
	_app_tag_config.report_mode = report_mode;
	_app_tag_config.update_mode = update_mode;
	_app_tag_config.update_rate = update_rate;

	// We were running when this function was called, so we start things back
	// up here.
	if (resume) {
		app_start();
	}
}

// Configure this as an ANCHOR.
void app_configure_anchor () {
	bool resume = FALSE;

	// Check if this application is running.
	if (_state == APPSTATE_RUNNING) {
		// Resume with new settings.
		resume = TRUE;
		// Stop this first
		app_stop();
	}

	// Check if we have been configured as a ANCHOR before. If we have, we don't
	// need to reset this.
	if (dw1000_get_mode() != ANCHOR) {
		// If we need to, complete the init() process now that we know we are a anchor.
		dw1000_set_mode(ANCHOR);
	}

	// Resume if we were running before.
	if (resume) {
		app_start();
	}
}

// Start this node! This will run the anchor and tag algorithms.
void app_start () {
	// Don't start if we are already started
	if (_state == APPSTATE_RUNNING) {
		return;
	}

	dw1000_role_e my_role = dw1000_get_mode();

	if (my_role == ANCHOR) {
		_state = APPSTATE_RUNNING;

		// Start the anchor state machine. The app doesn't have to do anything
		// for this, it just runs.
		dw1000_anchor_start();

	} else if (my_role == TAG) {
		_state = APPSTATE_RUNNING;

		if (_app_tag_config.update_mode == UPDATE_MODE_PERIODIC) {
			// Host requested periodic updates.
			// Set the timer to fire at the correct rate. Multiply by 1000000 to
			// get microseconds, then divide by 10 because update_rate is in
			// tenths of hertz.
			uint32_t period = (((uint32_t) _app_tag_config.update_rate) * 1000000) / 10;
			timer_start(_periodic_timer, period, tag_execute_range_callback);

		} else if (_app_tag_config.update_mode == UPDATE_MODE_DEMAND) {
			// Just wait for the host to request a ranging event
			// over the host interface.
		}

		//
		// TODO: implement selecting between reporting ranges and locations
		//


	} else {
		// We don't know what we are, so we just don't do anything.
	}
}

// This is called when the host tells us to sleep
void app_stop () {
	// Don't stop if we are already stopped
	if (_state == APPSTATE_STOPPED) {
		return;
	}

	dw1000_role_e my_role = dw1000_get_mode();

	_state = APPSTATE_STOPPED;

	if (my_role == ANCHOR) {
		dw1000_anchor_stop();
	} else if (my_role == TAG) {
		// Check if we need to stop our periodic timer so we don't keep ranging.
		if (_app_tag_config.update_mode == UPDATE_MODE_PERIODIC) {
			timer_stop(_periodic_timer);
		}
		dw1000_tag_stop();
	}
}

// Drop the big hammer on the DW1000 and reset the chip (along with the app).
// All state should be preserved, so after the reset the tripoint should go
// back to what it was doing, just after a reset and re-init of the dw1000.
void app_reset () {
	dw1000_role_e my_role = dw1000_get_mode();

	// Stop the timer in case it was in use.
	timer_stop(_periodic_timer);

	// Init the dw1000, and loop until it works.
	// start does a reset.
	start_dw1000();

	// Re init the role of this device
	if (my_role != UNDECIDED) {
		dw1000_set_mode(my_role);

		// If we were running before, run now
		if (_state == APPSTATE_RUNNING) {
			_state = APPSTATE_STOPPED;
			// This call will set it back to running
			app_start();
		}
	}
}

// Assuming we are a TAG, and we are in on-demand ranging mode, tell
// the dw1000 algorithm to perform a range.
void app_tag_do_range () {
	// If the application isn't running, we are not a tag, or we are not
	// in on-demand ranging mode, don't do anything.
	if (_state != APPSTATE_RUNNING ||
	    dw1000_get_mode() != TAG ||
	    _app_tag_config.update_mode != UPDATE_MODE_DEMAND) {
		return;
	}

	// TODO: this does return an error if we are already ranging.
	dw1000_tag_start_ranging_event();
}


/******************************************************************************/
// Connection for the anchor/tag code to talk to the main applications
/******************************************************************************/

dw1000_report_mode_e app_get_report_mode () {
	return _app_tag_config.report_mode;
}

// Record ranges that the tag found.
void app_set_ranges (int32_t* ranges_millimeters, anchor_responses_t* anchor_responses) {
	uint8_t buffer_index = 0;

	// Reset
	_num_anchor_ranges = 0;

	// Iterate through all ranges, looking for valid ones, and copying the correct
	// data into the ranges buffer.
	for (uint8_t i=0; i<MAX_NUM_ANCHOR_RESPONSES; i++) {
		if (ranges_millimeters[i] != INT32_MAX) {
			// This is a valid range
			memcpy(_anchor_ids_ranges+buffer_index, anchor_responses[i].anchor_addr, EUI_LEN);
			buffer_index += EUI_LEN;
			memcpy(_anchor_ids_ranges+buffer_index, &ranges_millimeters[i], sizeof(int32_t));
			buffer_index += sizeof(int32_t);
			_num_anchor_ranges++;
		}
	}

	// Now let the host know so it can do something with the ranges.
	host_interface_notify_ranges(_anchor_ids_ranges, _num_anchor_ranges);
}

/******************************************************************************/
// Main
/******************************************************************************/

// Loop until the DW1000 is inited and ready to go.
void start_dw1000 () {
	uint32_t err;

	while (1) {
		// Do some preliminary setup of the DW1000. This mostly configures
		// pins and hardware peripherals, as well as straightening out some
		// of the settings on the DW1000.
		uint8_t tries = 0;
		do {
			err = dw1000_init();
			if (err) {
				uDelay(10000);
				tries++;
			}
		} while (err && tries <= DW1000_NUM_CONTACT_TRIES_BEFORE_RESET);

		if (err) {
			// We never got the DW1000 to respond. This puts us in a really
			// bad spot. Maybe if we just wait for a while things will get
			// better?
			mDelay(50000);
		} else {
			// Success
			break;
		}
	}

}


int main () {
	uint32_t err;
	bool interrupt_triggered = FALSE;



	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_AHBPeriphClockCmd(STM_GPIO3_CLK, ENABLE);
	GPIO_InitStructure.GPIO_Pin = STM_GPIO3_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(STM_GPIO3_PORT, &GPIO_InitStructure);
	GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_RESET);
	GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_SET);
	GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_RESET);



	// In case we need a timer, get one. This is used for things like periodic
	// ranging events.
	_periodic_timer = timer_init();

	// Initialize the I2C listener. This is the main interface
	// the host controller (that is using TriPoint for ranging/localization)
	// uses to configure how this module operates.
	err = host_interface_init();
	if (err) error();

	// Next up do some preliminary setup of the DW1000. This mostly configures
	// pins and hardware peripherals, as well as straightening out some
	// of the settings on the DW1000.
	start_dw1000();

	// Now we just wait for the host board to tell us what to do. Before
	// it sets us up we just sit here.
	err = host_interface_wait();
	if (err) error();


	// MAIN LOOP
	while (1) {

		PWR_EnterSleepMode(PWR_SLEEPEntry_WFI);

		// When an interrupt fires we end up here.
		// Check all of the interrupt "queues" and call the appropriate
		// callbacks for all of the interrupts that have fired.
		// Do this in a loop in case we get an interrupt during the
		// checks.
		do {
			interrupt_triggered = FALSE;

			if (interrupts_triggered[INTERRUPT_TIMER_17] == TRUE) {
				interrupts_triggered[INTERRUPT_TIMER_17] = FALSE;
				interrupt_triggered = TRUE;
				timer_17_fired();
			}

			if (interrupts_triggered[INTERRUPT_TIMER_16] == TRUE) {
				interrupts_triggered[INTERRUPT_TIMER_16] = FALSE;
				interrupt_triggered = TRUE;
				timer_16_fired();
			}

			if (interrupts_triggered[INTERRUPT_DW1000] == TRUE) {
				interrupts_triggered[INTERRUPT_DW1000] = FALSE;
				interrupt_triggered = TRUE;
				dw1000_interrupt_fired();
			}

			if (interrupts_triggered[INTERRUPT_I2C_RX] == TRUE) {
				interrupts_triggered[INTERRUPT_I2C_RX] = FALSE;
				interrupt_triggered = TRUE;
				host_interface_rx_fired();
			}

			if (interrupts_triggered[INTERRUPT_I2C_TX] == TRUE) {
				interrupts_triggered[INTERRUPT_I2C_TX] = FALSE;
				interrupt_triggered = TRUE;
				host_interface_tx_fired();
			}

			if (interrupts_triggered[INTERRUPT_I2C_TIMEOUT] == TRUE) {
				interrupts_triggered[INTERRUPT_I2C_TIMEOUT] = FALSE;
				interrupt_triggered = TRUE;
				host_interface_timeout_fired();
			}
		} while (interrupt_triggered == TRUE);
	}



	return 0;
}




