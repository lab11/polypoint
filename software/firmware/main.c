
#include <string.h>

#include "stm32f0xx_tim.h"
#include "stm32f0xx_pwr.h"

#include "tripoint.h"
#include "led.h"

#include "i2c_interface.h"
#include "dw1000.h"
#include "dw1000_tag.h"
#include "dw1000_anchor.h"
#include "timer.h"
#include "firmware.h"


// typedef enum {
// 	STATE_START,
// 	STATE_IDLE,
// 	STATE_DW1000_INIT_DONE,
// } state_e;


// state_e state = STATE_START;


// Array of interrupt sources. When an interrupt fires, this array gets marked
// noting that the interrupt fired. The main thread then processes this array
// to get all of the functions it should call.
bool interrupts_triggered[NUMBER_INTERRUPT_SOURCES]  = {FALSE};



/******************************************************************************/
// Current application settings as set by the host
/******************************************************************************/
dw1000_report_mode_e _report_mode = REPORT_MODE_RANGES;
dw1000_update_mode_e _update_mode = UPDATE_MODE_PERIODIC;
uint8_t _update_rate = 10;

/******************************************************************************/
// Current application state
/******************************************************************************/
// Timer for doing periodic operations (like TAG ranging events)
timer_t* _periodic_timer;

// Buffer of anchor IDs and ranges to the anchor.
// Long enough to hold an anchor id followed by the range.
uint8_t _anchor_ids_ranges[MAX_NUM_ANCHOR_RESPONSES*(EUI_LEN+sizeof(int32_t))];
uint8_t _num_anchor_ranges = 0;

/******************************************************************************/
// "OS" like functions
/******************************************************************************/

// This gets called from interrupt context
void mark_interrupt (interrupt_source_e src) {
	interrupts_triggered[src] = TRUE;
}


static void error () {
	// dw1000_init();
	// led_on(LED2);
}


void i2c_callback (uint8_t opcode, uint8_t* data) {
	// led_toggle(LED1);
}


void decawave_done (dw1000_cb_e evt, dw1000_err_e err) {
}

/******************************************************************************/
// Main operation functions called by the I2C interface
/******************************************************************************/

// This is called after we've heard from the host board that this should be
// a tag with the given settings.
void run_tag (dw1000_report_mode_e report_mode,
              dw1000_update_mode_e update_mode,
              uint8_t update_rate) {

	// Start by completing the init() process now that we know we are a tag
	dw1000_set_mode(TAG);

	if (update_mode == UPDATE_MODE_PERIODIC) {
		// dw1000_tag_start_ranging_event();
		// Host requested periodic updates.
		// Set the timer to fire at the correct rate. Multiply by 1000000 to
		// get microseconds, then divide by 10 because update_rate is in
		// tenths of hertz.
		uint32_t period = (((uint32_t) update_rate) * 1000000) / 10;
		timer_start(_periodic_timer, period, dw1000_tag_start_ranging_event);

	} else if (update_mode == UPDATE_MODE_DEMAND) {
		// Just wait for the host to request a ranging event
		// over the I2C interface.
	}

	//
	// TODO: implement selecting between reporting ranges and locations
	//
}


/******************************************************************************/
// Connection for the anchor/tag code to talk to the main applications
/******************************************************************************/

dw1000_report_mode_e main_get_report_mode () {
	return _report_mode;
}

// Record ranges that the tag found.
void main_set_ranges (int* ranges_millimeters, anchor_responses_t* anchor_responses) {
	uint8_t buffer_index = 0;

	// Reset
	_num_anchor_ranges = 0;

	// Iterate through all ranges, looking for valid ones, and copying the correct
	// data into the ranges buffer.
	for (uint8_t i=0; i<MAX_NUM_ANCHOR_RESPONSES; i++) {
		if (ranges_millimeters[i] != INT32_MAX) {
			// This is a valid range
			memcpy(_anchor_ids_ranges+buffer_index, anchor_responses->anchor_addr, EUI_LEN);
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

int main () {
	uint32_t err;
	bool interrupt_triggered = FALSE;

	// In case we need a timer, get one. This is used for things like periodic
	// ranging events.
	_periodic_timer = timer_init();

	// Initialize the I2C listener. This is the main interface
	// the host controller (that is using TriPoint for ranging/localization)
	// uses to configure how this module operates.
	err = i2c_interface_init(i2c_callback);
	if (err) error();

	// Next up do some preliminary setup of the DW1000. This mostly configures
	// pins and hardware peripherals, as well as straightening out some
	// of the settings on the DW1000.
	err = dw1000_init();
	if (err) error();

	// Now we just wait for the host board to tell us what to do. Before
	// it sets us up we just sit here.
	err = i2c_interface_wait();
	if (err) error();


	while (1) {

		// switch (state) {
		// 	case STATE_START: {
		// 		state = STATE_IDLE;

		// 		// Setup CPAL, the manager that provides an I2C interface
		// 		// for the chip.
		// 		err = i2c_interface_init(i2c_callback);
		// 		if (err) error();



		// 		uint8_t buf[5] = {4, 5, 6, 7, 8};
		// 		err = i2c_interface_wait(5, buf);
		// 		if (err) error();
		// 		break;

		// 		// Setup the DW1000 decawave chip
		// 		err = dw1000_init();

		// 		if (err == DW1000_NO_ERR) {

		// 			// Make it a tag
		// 			dw1000_set_mode(TAG);

		// 			// Do a test run
		// 			dw1000_tag_start_ranging_event();
		// 		}

		// 		// dw1000_set_mode(ANCHOR);
		// 		// dw1000_anchor_start();


		// 		break;
		// 	}


		// 	case STATE_DW1000_INIT_DONE: {
		// 		state = STATE_IDLE;

		// 		// Now wait for commands from the host chip
		// 		// i2c_interface_listen();


		// 		// uint8_t buf[5] = {4, 5, 6, 7, 8};
		// 		// uint32_t err;

		// 		// led_toggle(LED2);

		// 		// err = i2c_interface_send(0x74, 5, buf);
		// 		// if (err) {
		// 		// 	led_on(LED1);
		// 		// }


		// 		break;
		// 	}



		// 	default:
		// 		break;
		// }


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
				i2c_interface_rx_fired();
			}

			if (interrupts_triggered[INTERRUPT_I2C_TX] == TRUE) {
				interrupts_triggered[INTERRUPT_I2C_TX] = FALSE;
				interrupt_triggered = TRUE;
				i2c_interface_tx_fired();
			}

			if (interrupts_triggered[INTERRUPT_I2C_TIMEOUT] == TRUE) {
				interrupts_triggered[INTERRUPT_I2C_TIMEOUT] = FALSE;
				interrupt_triggered = TRUE;
				i2c_interface_timeout_fired();
			}
		} while (interrupt_triggered == TRUE);



	}



	return 0;
}




