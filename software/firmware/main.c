
#include "stm32f0xx_tim.h"
#include "stm32f0xx_pwr.h"

#include "tripoint.h"
#include "led.h"

#include "i2c_interface.h"
#include "dw1000.h"


typedef enum {
	STATE_START,
	STATE_IDLE,
	STATE_DW1000_INIT_DONE,
} state_e;


state_e state = STATE_START;



static void TIM17_Config(uint32_t Period) {
  /* TIM17 is used to generate periodic interrupts. At each interrupt
  if Transmitter mode is selected, a status message is sent to other Board */

  NVIC_InitTypeDef NVIC_InitStructure;
  TIM_TimeBaseInitTypeDef   TIM_TimeBaseStructure;

  /* TIMER clock enable */
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM17 , ENABLE);

  NVIC_InitStructure.NVIC_IRQChannel = TIM17_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPriority = 0x01;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  TIM_TimeBaseStructure.TIM_Period  = Period;
  TIM_TimeBaseStructure.TIM_Prescaler = (SystemCoreClock/10000)-1;
  TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseStructure.TIM_CounterMode =  TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM17, &TIM_TimeBaseStructure);

  /* TIM IT enable */
  TIM_ITConfig(TIM17, TIM_IT_Update , ENABLE);

  /* TIM17 enable counter */
  TIM_Cmd(TIM17, ENABLE);
}


static void error () {
	led_on(LED2);
}


void i2c_callback (uint8_t opcode, uint8_t* data) {
	led_toggle(LED1);
}


void decawave_done (dw1000_cb_e evt, uint32_t err) {
	if (err) {
		// do something
	}

	switch (evt) {
		case DW1000_INIT_DONE:
			state = STATE_DW1000_INIT_DONE;

			break;

		default:
			break;
	}
}

int main () {
	uint32_t err;

	led_init(LED1);
	led_init(LED2);
	led_off(LED1);
	led_off(LED2);

	// err = i2c_interface_init();
	// if (err) {
	// 	// do something
	// 	led_on(LED2);
	// }

	// TIM17_Config(10000);

	// dw1000_init(decawave_done);


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





	}



	return 0;
}





void TIM17_IRQHandler(void) {

	if (TIM_GetITStatus(TIM17, TIM_IT_Update) != RESET) {



		// led_toggle(LED1);
		// err = i2c_interface_send(0x74, 5, buf);
		// if (err) {
		// 	led_on(LED2);
		// }

		// dw1000_init(decawave_done);

		/* Clear Timer interrupt pending bit */
		TIM_ClearITPendingBit(TIM17, TIM_IT_Update);
	}
}