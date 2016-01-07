#include <string.h>

#include "stm32f0xx_tim.h"
#include "stm32f0xx_pwr.h"

#include "tripoint.h"
#include "led.h"

#include "host_interface.h"
#include "dw1000.h"
#include "oneway_common.h"
#include "oneway_tag.h"
#include "oneway_anchor.h"
#include "calibration.h"
#include "timer.h"
#include "delay.h"
#include "firmware.h"

/******************************************************************************/
// OS state
/******************************************************************************/

// Array of interrupt sources. When an interrupt fires, this array gets marked
// noting that the interrupt fired. The main thread then processes this array
// to get all of the functions it should call.
bool interrupts_triggered[NUMBER_INTERRUPT_SOURCES]  = {FALSE};


/******************************************************************************/
// Current application state
/******************************************************************************/
// Keep track of if the application is active or not
static app_state_e _state = APPSTATE_NOT_INITED;

// Keep track of what application we are running
static polypoint_application_e _current_app;

// Timer for doing periodic operations (like TAG ranging events)
static stm_timer_t* _app_timer;


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
	GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_SET);
	GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_RESET);
}

union app_scratchspace {
	oneway_tag_scratchspace_struct ot_scratch;
	oneway_anchor_scratchspace_struct oa_scratch;
	calibration_scratchspace_struct cal_scratch;
} _app_scratchspace;

/******************************************************************************/
// Main operation functions called by the host interface
/******************************************************************************/

// Call this to configure this TriPoint as the correct application.
// If this is called while the application is stopped, it will not be
// automatically started.
// If this is called when the app is running, the app will be restarted.
void polypoint_configure_app (polypoint_application_e app, void* app_config) {
	bool resume = FALSE;

	// Check if this application is running.
	if (_state == APPSTATE_RUNNING) {
		// Resume with new settings.
		resume = TRUE;
		// Stop this first
		polypoint_stop();
	}

	// Tell the correct application that it should init()
	_current_app = app;
	switch (_current_app) {
		case APP_ONEWAY:
			oneway_configure((oneway_config_t*) app_config, _app_timer, (void*)&_app_scratchspace);
			break;

		case APP_CALIBRATION:
			calibration_configure((calibration_config_t*) app_config, _app_timer, (void*)&_app_scratchspace);
			break;

		default:
			break;
	}

	// We were running when this function was called, so we start things back
	// up here.
	if (resume) {
		polypoint_start();
	}
}


// Start this node! This will run the anchor and tag algorithms.
void polypoint_start () {
	// Don't start if we are already started
	if (_state == APPSTATE_RUNNING) {
		return;
	}

	_state = APPSTATE_RUNNING;

	switch (_current_app) {
		case APP_ONEWAY:
			oneway_start();
			break;

		case APP_CALIBRATION:
			calibration_start();
			break;

		default:
			break;
	}
}

// This is called when the host tells us to sleep
void polypoint_stop () {
	// Don't stop if we are already stopped
	if (_state == APPSTATE_STOPPED) {
		return;
	}

	_state = APPSTATE_STOPPED;

	switch (_current_app) {
		case APP_ONEWAY:
			oneway_stop();
			break;

		case APP_CALIBRATION:
			calibration_stop();
			break;

		default:
			break;
	}
}

// Drop the big hammer on the DW1000 and reset the chip (along with the app).
// All state should be preserved, so after the reset the tripoint should go
// back to what it was doing, just after a reset and re-init of the dw1000.
void polypoint_reset () {
	bool resume = FALSE;

	if (_state == APPSTATE_RUNNING) {
		resume = TRUE;
	}

	// Put the app back in the not inited state
	_state = APPSTATE_NOT_INITED;

	// Stop the timer in case it was in use.
	timer_stop(_app_timer);

	// Init the dw1000, and loop until it works.
	// start does a reset.
	start_dw1000();

	// Re init the app
	switch (_current_app) {
		case APP_ONEWAY:
			oneway_reset();
			break;

		case APP_CALIBRATION:
			calibration_reset();
			break;

		default:
			break;
	}

	if (resume) {
		polypoint_start();
	}
}

// Return true if we are good for app_configure
// to be called, false otherwise.
bool polypoint_ready () {
	return _state != APPSTATE_NOT_INITED;
}

// Assuming we are a TAG, and we are in on-demand ranging mode, tell
// the dw1000 algorithm to perform a range.
void polypoint_tag_do_range () {
	// If the application isn't running, we are not a tag, or we are not
	// in on-demand ranging mode, don't do anything.
	if (_state != APPSTATE_RUNNING) {
		return;
	}

	// Call the relevant function based on the current application
	switch (_current_app) {
		case APP_ONEWAY:
			oneway_do_range();
			break;

		case APP_CALIBRATION:
			// Not a thing for this app
			break;

		default:
			break;
	}
}


/******************************************************************************/
// Connection for the anchor/tag code to talk to the main applications
/******************************************************************************/





/******************************************************************************/
// Main
/******************************************************************************/

// Loop until the DW1000 is inited and ready to go.
// TODO: this really shouldn't be blocking as it messes with the I2C interrupt
//       code.
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

	// Successfully started the DW1000
	_state = APPSTATE_STOPPED;
}


int main () {
	uint32_t err;
	bool interrupt_triggered = FALSE;

	// Enable PWR APB clock
	// Not entirely sure why.
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);


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
	_app_timer = timer_init();

	// Initialize the I2C listener. This is the main interface
	// the host controller (that is using TriPoint for ranging/localization)
	// uses to configure how this module operates.
	err = host_interface_init();
	if (err) error();

	// Need to wait for the host board to tell us what to do.
	err = host_interface_wait();
	if (err) error();

	// Next up do some preliminary setup of the DW1000. This mostly configures
	// pins and hardware peripherals, as well as straightening out some
	// of the settings on the DW1000.
	start_dw1000();



	// MAIN LOOP
	while (1) {

		PWR_EnterSleepMode(PWR_SLEEPEntry_WFI);
		// PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);

		GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_SET);
		GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_RESET);

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
