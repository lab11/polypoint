#ifndef __LED_H
#define __LED_H

typedef struct {
	GPIO_TypeDef* GPIOx;
	uint16_t GPIO_Pin;
} led_t;

int led_init (uint8_t led, GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin, uint32_t RCC_AHBPeriph);
void led_on (uint8_t led);
void led_off (uint8_t led);
void led_toggle (uint8_t led);

#endif