
#include "stm32f0xx_tim.h"
#include "stm32f0xx_pwr.h"

#include "tripoint.h"
#include "led.h"

#include "i2c_interface.h"
#include "dw1000.h"
#include "timer.h"
#include "firmware.h"


typedef enum {
	STATE_START,
	STATE_IDLE,
	STATE_DW1000_INIT_DONE,
} state_e;


state_e state = STATE_START;


// Array of interrupt sources. When an interrupt fires, this array gets marked
// noting that the interrupt fired. The main thread then processes this array
// to get all of the functions it should call.
bool interupts_triggered[NUMBER_INTERRUPT_SOURCES]  = {FALSE};


// This gets called from interrupt context
void mark_interrupt (interrupt_source_e src) {
	interupts_triggered[src] = TRUE;
}



static void error () {
	led_on(LED2);
}


void i2c_callback (uint8_t opcode, uint8_t* data) {
	led_toggle(LED1);
}


void decawave_done (dw1000_cb_e evt, dw1000_err_e err) {
	if (err) {
		// do something
		// led_on(LED1);
		led_on(LED2);

	} else {

		switch (evt) {
			case DW1000_INIT_DONE:
				state = STATE_DW1000_INIT_DONE;
				led_on(LED1);
				dw1000_tag_init();

				break;

			default:
				break;
		}
	}
}

int main () {
	uint32_t err;

	led_init(LED1);
	led_init(LED2);
	led_off(LED1);
	led_off(LED2);

	while (1) {

		switch (state) {
			case STATE_START: {
				state = STATE_IDLE;

				// Setup CPAL, the manager that provides an I2C interface
				// for the chip.
				err = i2c_interface_init(i2c_callback);
				if (err) error();

				// Setup the DW1000 decawave chip
				dw1000_init(decawave_done);
				break;
			}


			case STATE_DW1000_INIT_DONE: {
				state = STATE_IDLE;

				// Now wait for commands from the host chip
				i2c_interface_listen();


				// uint8_t buf[5] = {4, 5, 6, 7, 8};
				// uint32_t err;

				// led_toggle(LED2);

				// err = i2c_interface_send(0x74, 5, buf);
				// if (err) {
				// 	led_on(LED1);
				// }


				break;
			}



			default:
				break;
		}


		PWR_EnterSleepMode(PWR_SLEEPEntry_WFI);

		// When an interrupt fires we end up here

		if (interupts_triggered[TIMER_17] == TRUE) {
			interupts_triggered[TIMER_17] = FALSE;
			timer_17_fired();
		}

		if (interupts_triggered[TIMER_16] == TRUE) {
			interupts_triggered[TIMER_16] = FALSE;
			timer_16_fired();
		}





	}



	return 0;
}




