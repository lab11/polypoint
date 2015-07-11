#ifndef __TRIPOINT_H
#define __TRIPOINT_H

#include "stm32f0xx.h"

/******************************************************************************/
// LEDS
/******************************************************************************/
#define LEDn                             2

#define LED1                             0
#define LED1_PIN                         GPIO_Pin_9
#define LED1_GPIO_PORT                   GPIOC
#define LED1_GPIO_CLK                    RCC_AHBPeriph_GPIOC

#define LED2                             1
#define LED2_PIN                         GPIO_Pin_8
#define LED2_GPIO_PORT                   GPIOC
#define LED2_GPIO_CLK                    RCC_AHBPeriph_GPIOC


/******************************************************************************/
// I2C
/********************************************************************************/
#define INTERRUPT_PIN GPIO_Pin_5
#define INTERRUPT_PORT GPIOB
#define INTERRUPT_CLK RCC_AHBPeriph_GPIOB

// I2C
/******************************************************************************/
#define I2C_OWN_ADDRESS 0xe8
#define I2C_TIMING              0x00731012


#endif
