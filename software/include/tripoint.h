#ifndef __TRIPOINT_H
#define __TRIPOINT_H

#include "stm32f0xx.h"

/******************************************************************************/
// EUI LOCATION IN FLASH
/******************************************************************************/
#define EUI_FLASH_LOCATION  0x08007ff8
#define INIT_FLASH_LOCATION 0x08007F80


/******************************************************************************/
// LEDS
/******************************************************************************/
#define LEDn 0


/******************************************************************************/
// INTERRUPT TO HOST DEVICE
/********************************************************************************/
#define INTERRUPT_PIN GPIO_Pin_3
#define INTERRUPT_PORT GPIOA
#define INTERRUPT_CLK RCC_AHBPeriph_GPIOA


/******************************************************************************/
// I2C
/******************************************************************************/
#define I2C_TIMING  0x00731012


/******************************************************************************/
// SPI
/******************************************************************************/

/* USER_TIMEOUT value for waiting loops. This timeout is just guarantee that the
   application will not remain stuck if the USART communication is corrupted.
   You may modify this timeout value depending on CPU frequency and application
   conditions (interrupts routines, number of data to transfer, baudrate, CPU
   frequency...). */
#define USER_TIMEOUT                    ((uint32_t)0x64) /* Waiting 1s */

/* Communication boards SPIx Interface */
#define SPI1_DR_ADDRESS                  0x4001300C
#define SPI1_TX_DMA_CHANNEL              DMA1_Channel3
#define SPI1_TX_DMA_FLAG_TC              DMA1_FLAG_TC3
#define SPI1_TX_DMA_FLAG_GL              DMA1_FLAG_GL3
#define SPI1_RX_DMA_CHANNEL              DMA1_Channel2
#define SPI1_RX_DMA_FLAG_TC              DMA1_FLAG_TC2
#define SPI1_RX_DMA_FLAG_GL              DMA1_FLAG_GL2
#define SPI1_DMA_IRQn                    DMA1_Channel2_3_IRQn

#define USART1_DR_ADDRESS                0x40013828
#define USART1_TX_DMA_CHANNEL            DMA1_Channel4
#define USART1_TX_DMA_FLAG_TC            DMA1_FLAG_TC4
#define USART1_TX_DMA_FLAG_GL            DMA1_FLAG_GL4
#define USART1_DMA_IRQn                  DMA1_Channel4_IRQn

#define DMA1_CLK                         RCC_AHBPeriph_DMA1

#define SPI1_CLK                         RCC_APB2Periph_SPI1
#define SPI1_IRQn                        SPI1_IRQn
#define SPI1_IRQHandler                  SPI1_IRQHandler

#define SPI1_SCK_PIN                     GPIO_Pin_5
#define SPI1_SCK_GPIO_PORT               GPIOA
#define SPI1_SCK_GPIO_CLK                RCC_AHBPeriph_GPIOA
#define SPI1_SCK_SOURCE                  GPIO_PinSource5
#define SPI1_SCK_AF                      GPIO_AF_0

#define SPI1_MISO_PIN                    GPIO_Pin_6
#define SPI1_MISO_GPIO_PORT              GPIOA
#define SPI1_MISO_GPIO_CLK               RCC_AHBPeriph_GPIOA
#define SPI1_MISO_SOURCE                 GPIO_PinSource6
#define SPI1_MISO_AF                     GPIO_AF_0

#define SPI1_MOSI_PIN                    GPIO_Pin_7
#define SPI1_MOSI_GPIO_PORT              GPIOA
#define SPI1_MOSI_GPIO_CLK               RCC_AHBPeriph_GPIOA
#define SPI1_MOSI_SOURCE                 GPIO_PinSource7
#define SPI1_MOSI_AF                     GPIO_AF_0

#define SPI1_NSS_PIN                     GPIO_Pin_4
#define SPI1_NSS_GPIO_PORT               GPIOA
#define SPI1_NSS_GPIO_CLK                RCC_AHBPeriph_GPIOA
#define SPI1_NSS_SOURCE                  GPIO_PinSource4
#define SPI1_NSS_AF                      GPIO_AF_0


/******************************************************************************/
// INTERRUPT FROM DECAWAVE
/********************************************************************************/
#define DW_INTERRUPT_PIN GPIO_Pin_2
#define DW_INTERRUPT_PORT GPIOA
#define DW_INTERRUPT_CLK RCC_AHBPeriph_GPIOA
#define DW_INTERRUPT_EXTI_LINE EXTI_Line2
#define DW_INTERRUPT_EXTI_IRQn EXTI2_3_IRQn
#define DW_INTERRUPT_EXTI_PORT EXTI_PortSourceGPIOA
#define DW_INTERRUPT_EXTI_PIN EXTI_PinSource2


/******************************************************************************/
// DECAWAVE RESET
/********************************************************************************/
#define DW_RESET_PIN GPIO_Pin_0
#define DW_RESET_PORT GPIOB
#define DW_RESET_CLK RCC_AHBPeriph_GPIOB


/******************************************************************************/
// DECAWAVE WAKEUP
/*****************************************************************************/
#define DW_WAKEUP_PIN GPIO_Pin_1
#define DW_WAKEUP_PORT GPIOB
#define DW_WAKEUP_CLK RCC_AHBPeriph_GPIOB


/*****************************************************************************/
// ANTENNA PINS
/*****************************************************************************/
#define ANT_SEL0_PIN	GPIO_Pin_4
#define ANT_SEL0_PORT	GPIOB
#define ANT_SEL0_CLK	RCC_AHBPeriph_GPIOB

#define ANT_SEL1_PIN	GPIO_Pin_3
#define ANT_SEL1_PORT	GPIOB
#define ANT_SEL1_CLK	RCC_AHBPeriph_GPIOB

#define ANT_SEL2_PIN	GPIO_Pin_15
#define ANT_SEL2_PORT	GPIOA
#define ANT_SEL2_CLK	RCC_AHBPeriph_GPIOA


/*****************************************************************************/
// MISC GPIOs
/*****************************************************************************/
#define STM_GPIO0_PIN	GPIO_Pin_5
#define STM_GPIO0_PORT	GPIOB
#define STM_GPIO0_CLK	RCC_AHBPeriph_GPIOB

#define STM_GPIO1_PIN	GPIO_Pin_6
#define STM_GPIO1_PORT	GPIOB
#define STM_GPIO1_CLK	RCC_AHBPeriph_GPIOB

#define STM_GPIO2_PIN	GPIO_Pin_0
#define STM_GPIO2_PORT	GPIOA
#define STM_GPIO2_CLK	RCC_AHBPeriph_GPIOA

#define STM_GPIO3_PIN	GPIO_Pin_1
#define STM_GPIO3_PORT	GPIOA
#define STM_GPIO3_CLK	RCC_AHBPeriph_GPIOA

#define STM_GPIO4_PIN	GPIO_Pin_7
#define STM_GPIO4_PORT	GPIOB
#define STM_GPIO4_CLK	RCC_AHBPeriph_GPIOB

#endif
