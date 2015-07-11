
#include "stm32f0xx_rcc.h"

#include "board.h"

GPIO_TypeDef* GPIO_PORT[LEDn] = {LED1_GPIO_PORT, LED2_GPIO_PORT};
const uint16_t GPIO_PIN[LEDn] = {LED1_PIN, LED2_PIN};
const uint32_t GPIO_CLK[LEDn] = {LED1_GPIO_CLK, LED2_GPIO_CLK};


void led_init (uint8_t led)
{
  GPIO_InitTypeDef GPIO_InitStructure;

  /* Enable the GPIO_LED Clock */
  RCC_AHBPeriphClockCmd(GPIO_CLK[led], ENABLE);

  /* Configure the GPIO_LED pin */
  GPIO_InitStructure.GPIO_Pin = GPIO_PIN[led];
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIO_PORT[led], &GPIO_InitStructure);
  GPIO_PORT[led]->BSRR = GPIO_PIN[led];
}

void led_on (uint8_t led)
{
  GPIO_PORT[led]->BSRR = GPIO_PIN[led];
}

void led_off (uint8_t led)
{
  GPIO_PORT[led]->BRR = GPIO_PIN[led];
}

void led_toggle (uint8_t led)
{
  GPIO_PORT[led]->ODR ^= GPIO_PIN[led];
}
