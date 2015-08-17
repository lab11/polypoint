
#include "stm32f0xx_rcc.h"

#include "board.h"
#include "led.h"

// Array of all initialized LEDs
static led_t leds[LEDn];


int led_init (uint8_t led, GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin, uint32_t RCC_AHBPeriph) {
	GPIO_InitTypeDef GPIO_InitStructure;

	// Save this new led
	if (led < LEDn) {
		leds[led].GPIOx = GPIOx;
		leds[led].GPIO_Pin = GPIO_Pin;
	} else {
		return -1;
	}

	/* Enable the GPIO_LED Clock */
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph, ENABLE);

	/* Configure the GPIO_LED pin */
	GPIO_InitStructure.GPIO_Pin = leds[led].GPIO_Pin;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(leds[led].GPIOx, &GPIO_InitStructure);
	leds[led].GPIOx->BSRR = leds[led].GPIO_Pin;
}

void led_on (uint8_t led) {
	if (led > LEDn) return;
	leds[led].GPIOx->BSRR = leds[led].GPIO_Pin;
}

void led_off (uint8_t led) {
	if (led > LEDn) return;
	leds[led].GPIOx->BRR = leds[led].GPIO_Pin;
}

void led_toggle (uint8_t led) {
	if (led > LEDn) return;
	leds[led].GPIOx->ODR ^= leds[led].GPIO_Pin;
}
