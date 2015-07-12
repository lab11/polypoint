
#include "stm32f0xx_tim.h"

#include "tripoint.h"
#include "led.h"

#include "i2c_interface.h"
#include "dw1000.h"

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

	TIM17_Config(10000);

	return 0;
}


void decawave_done (dw1000_cb_e evt, uint32_t err) {
	if (err) {
		// do something
	}

	switch (evt) {
		case DW1000_INIT_DONE:
			led_toggle(LED2);
			break;

		default:
			break;
	}
}


void TIM17_IRQHandler(void) {

	if (TIM_GetITStatus(TIM17, TIM_IT_Update) != RESET) {

		uint8_t buf[5] = {4, 5, 6, 7, 8};
		uint32_t err;

		// led_toggle(LED1);
		// err = i2c_interface_send(0x74, 5, buf);
		// if (err) {
		// 	led_on(LED2);
		// }

		dw1000_init(decawave_done);

		/* Clear Timer interrupt pending bit */
		TIM_ClearITPendingBit(TIM17, TIM_IT_Update);
	}
}