#include <string.h>
#include "stm32f0xx_spi.h"
#include "stm32f0xx_dma.h"
#include "stm32f0xx_exti.h"
#include "stm32f0xx_syscfg.h"
#include "led.h"

#include "deca_device_api.h"
#include "deca_regs.h"

#include "port.h"
#include "board.h"
#include "dw1000.h"
#include "dw1000_tag.h"
#include "dw1000_anchor.h"
#include "delay.h"
#include "firmware.h"


/******************************************************************************/
// Constants for the DW1000
/******************************************************************************/

const uint8_t pgDelay[DW1000_NUM_CHANNELS] = {
	0x0,
	0xc9,
	0xc2,
	0xc5,
	0x95,
	0xc0,
	0x0,
	0x93
};

//NOTE: THIS IS DEPENDENT ON BAUDRATE
const uint32_t txPower[DW1000_NUM_CHANNELS] = {
	0x0,
	0x07274767UL,
	0x07274767UL,
	0x2B4B6B8BUL,
	0x3A5A7A9AUL,
	0x25456585UL,
	0x0,
	0x5171B1D1UL
};

// Configure the RF channels to use. This is just a mapping from 0..2 to
// the actual RF channel numbers the DW1000 uses.
const uint8_t channel_index_to_channel_rf_number[NUM_RANGING_CHANNELS] = {
	1, 4, 3
};

/******************************************************************************/
// Data structures used in multiple functions
/******************************************************************************/

// These are for configuring the hardware peripherals on the STM32F0
static DMA_InitTypeDef DMA_InitStructure;
static SPI_InitTypeDef SPI_InitStructure;

// Setup TX/RX settings on the DW1000
static dwt_config_t global_ranging_config;
static dwt_txconfig_t global_tx_config;


/******************************************************************************/
// Internal state for this file
/******************************************************************************/

// Keep track of whether we have inited the STM hardware
static bool _stm_dw1000_interface_setup = FALSE;

// Keep track of whether we are a tag or anchor
static dw1000_role_e _my_role = UNDECIDED;

// Whether or not interrupts are enabled.
decaIrqStatus_t dw1000_irq_onoff = 0;


/******************************************************************************/
// STM32F0 Hardware setup functions
/******************************************************************************/

static void dw1000_interrupt_enable() {
	NVIC_InitTypeDef NVIC_InitStructure;

	// Enable and set EXTIx Interrupt
	NVIC_InitStructure.NVIC_IRQChannel = DW_INTERRUPT_EXTI_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

static void dw1000_interrupt_disable() {
	NVIC_InitTypeDef NVIC_InitStructure;

	// Enable and set EXTIx Interrupt
	NVIC_InitStructure.NVIC_IRQChannel = DW_INTERRUPT_EXTI_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;
	NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
	NVIC_Init(&NVIC_InitStructure);

}

// Configure SPI + GPIOs for SPI. Also preset some DMA constants.
static void setup () {

	GPIO_InitTypeDef GPIO_InitStructure;
	EXTI_InitTypeDef EXTI_InitStructure;

	// Enable the SPI peripheral
	RCC_APB2PeriphClockCmd(SPI1_CLK, ENABLE);

	// Enable the DMA peripheral
	RCC_AHBPeriphClockCmd(DMA1_CLK, ENABLE);

	// Enable SCK, MOSI, MISO and NSS GPIO clocks
	RCC_AHBPeriphClockCmd(SPI1_SCK_GPIO_CLK |
	                      SPI1_MISO_GPIO_CLK |
	                      SPI1_MOSI_GPIO_CLK |
	                      SPI1_NSS_GPIO_CLK , ENABLE);

	// SPI pin mappings
	GPIO_PinAFConfig(SPI1_SCK_GPIO_PORT,  SPI1_SCK_SOURCE,  SPI1_SCK_AF);
	GPIO_PinAFConfig(SPI1_MOSI_GPIO_PORT, SPI1_MOSI_SOURCE, SPI1_MOSI_AF);
	GPIO_PinAFConfig(SPI1_MISO_GPIO_PORT, SPI1_MISO_SOURCE, SPI1_MISO_AF);
	GPIO_PinAFConfig(SPI1_NSS_GPIO_PORT,  SPI1_NSS_SOURCE,  SPI1_NSS_AF);

	// Configure SPI pins
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	// SPI SCK pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_SCK_PIN;
	GPIO_Init(SPI1_SCK_GPIO_PORT, &GPIO_InitStructure);

	// SPI MOSI pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_MOSI_PIN;
	GPIO_Init(SPI1_MOSI_GPIO_PORT, &GPIO_InitStructure);

	// SPI MISO pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_MISO_PIN;
	GPIO_Init(SPI1_MISO_GPIO_PORT, &GPIO_InitStructure);

	// SPI NSS pin configuration
	// Need a pull up here
	GPIO_InitStructure.GPIO_Pin  = SPI1_NSS_PIN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(SPI1_NSS_GPIO_PORT, &GPIO_InitStructure);

	// SPI configuration
	SPI_I2S_DeInit(SPI1);
	SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
	SPI_InitStructure.SPI_NSS               = SPI_NSS_Hard;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_64;
	SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial     = 7;
	SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
	SPI_Init(SPI1, &SPI_InitStructure);

	// Initialize the FIFO threshold
	// This is critical for 8 bit transfers
	SPI_RxFIFOThresholdConfig(SPI1, SPI_RxFIFOThreshold_QF);

	// Setup interrupt from the DW1000
	// Enable GPIOA clock
	RCC_AHBPeriphClockCmd(DW_INTERRUPT_CLK, ENABLE);

	// Configure PA0 pin as input floating
	GPIO_InitStructure.GPIO_Pin = DW_INTERRUPT_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_INTERRUPT_PORT, &GPIO_InitStructure);

	// Enable SYSCFG clock
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
	// Connect EXTIx Line to DW Int pin
	SYSCFG_EXTILineConfig(DW_INTERRUPT_EXTI_PORT, DW_INTERRUPT_EXTI_PIN);

	// Configure EXTIx line for interrupt
	EXTI_InitStructure.EXTI_Line = DW_INTERRUPT_EXTI_LINE;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	// Enable interrupt from the DW1000
	dw1000_interrupt_enable();
	dw1000_irq_onoff = 1;

	// Setup reset pin. Make it input unless we need it
	RCC_AHBPeriphClockCmd(DW_RESET_CLK, ENABLE);
	// Configure reset pin
	GPIO_InitStructure.GPIO_Pin = DW_RESET_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_RESET_PORT, &GPIO_InitStructure);

	// Setup wakeup pin.
	RCC_AHBPeriphClockCmd(DW_WAKEUP_CLK, ENABLE);
	GPIO_InitStructure.GPIO_Pin = DW_WAKEUP_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_WAKEUP_PORT, &GPIO_InitStructure);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_RESET);

	// Make the reset pin output
	GPIO_InitStructure.GPIO_Pin = DW_RESET_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_RESET_PORT, &GPIO_InitStructure);
	GPIO_WriteBit(DW_RESET_PORT, DW_RESET_PIN, Bit_SET);

	// Setup antenna pins - select no antennas
	RCC_AHBPeriphClockCmd(ANT_SEL0_CLK, ENABLE);
	RCC_AHBPeriphClockCmd(ANT_SEL1_CLK, ENABLE);
	RCC_AHBPeriphClockCmd(ANT_SEL2_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = ANT_SEL0_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(ANT_SEL0_PORT, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = ANT_SEL1_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(ANT_SEL1_PORT, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = ANT_SEL2_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(ANT_SEL1_PORT, &GPIO_InitStructure);

	// Initialize the RF Switch
	GPIO_WriteBit(ANT_SEL0_PORT, ANT_SEL0_PIN, Bit_SET);
	GPIO_WriteBit(ANT_SEL1_PORT, ANT_SEL1_PIN, Bit_RESET);
	GPIO_WriteBit(ANT_SEL2_PORT, ANT_SEL2_PIN, Bit_RESET);

	// Pre-populate DMA fields that don't need to change
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) SPI1_DR_ADDRESS;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
	DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;

	// Mark that this function has run so we don't do it again.
	_stm_dw1000_interface_setup = TRUE;
}

// Functions to configure the SPI speed
void dw1000_spi_fast () {
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
	SPI_Init(SPI1, &SPI_InitStructure);
}

void dw1000_spi_slow () {
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_64;
	SPI_Init(SPI1, &SPI_InitStructure);
}

// Only write data to the DW1000, and use DMA to do it.
static void setup_dma_write (uint32_t length, const uint8_t* tx) {
	static uint8_t throwAway;

	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) &throwAway;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Disable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_RX_DMA_CHANNEL, &DMA_InitStructure);

	// DMA channel Tx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) tx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_TX_DMA_CHANNEL, &DMA_InitStructure);
}

// Setup just a read over SPI using DMA.
static void setup_dma_read (uint32_t length, uint8_t* rx) {

	//volatile uint8_t throw = SPI1->DR;
	//throw = SPI1->SR;
	// DMA channel Rx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) rx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_RX_DMA_CHANNEL, &DMA_InitStructure);

	static uint8_t tx = 0;
	// DMA channel Tx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) &tx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Disable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_TX_DMA_CHANNEL, &DMA_InitStructure);
}

// Setup full duplex SPI over DMA.
static void setup_dma (uint32_t length, uint8_t* rx, uint8_t* tx) {

	// DMA channel Rx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) rx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_RX_DMA_CHANNEL, &DMA_InitStructure);

	// DMA channel Tx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) tx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_Low;
	DMA_Init(SPI1_TX_DMA_CHANNEL, &DMA_InitStructure);
}


/******************************************************************************/
// Interrupt callbacks
/******************************************************************************/

// Not needed, but handle the interrupt from the SPI DMA
void DMA1_Channel2_3_IRQHandler(void) {
}


// HW interrupt for the interrupt pin from the DW1000
void EXTI2_3_IRQHandler (void) {
	if (EXTI_GetITStatus(EXTI_Line2) != RESET) {
		// Mark this interrupt as had occurred in the main thread.
		mark_interrupt(INTERRUPT_DW1000);

		// Clear the EXTI line 2 pending bit
		EXTI_ClearITPendingBit(EXTI_Line2);
	}
}

// Main thread interrupt handler for the interrupt from the DW1000. Basically
// just passes knowledge of the interrupt on to the DW1000 library.
void dw1000_interrupt_fired () {
	// Keep calling the decawave interrupt handler as long as the interrupt pin
	// is asserted, but add an escape hatch so we don't get stuck forever.
	uint8_t count = 0;
	do {
		dwt_isr();
		count++;
	} while (GPIO_ReadInputDataBit(DW_INTERRUPT_PORT, DW_INTERRUPT_PIN) &&
	         count < DW1000_NUM_CONSECUTIVE_INTERRUPTS_BEFORE_RESET);

	if (count >= DW1000_NUM_CONSECUTIVE_INTERRUPTS_BEFORE_RESET) {
		// Well this is not good. It looks like the interrupt got stuck high,
		// so we'd spend the rest of the time just reading this interrupt.
		// Not much we can do here but reset everything.
		app_reset();
	}
}


// Callbacks from the decawave library. Just pass these to the correct
// anchor or tag code.
static void txcallback (const dwt_callback_data_t *data) {
	if (_my_role == TAG) {
		dw1000_tag_txcallback(data);
	} else if (_my_role == ANCHOR) {
		dw1000_anchor_txcallback(data);
	}
}

// Callback from the DW1000 library, after it has processed an interrupt.
// Pass these on to the correct libraries.
static void rxcallback (const dwt_callback_data_t *data) {
	if (_my_role == TAG) {
		dw1000_tag_rxcallback(data);
	} else if (_my_role == ANCHOR) {
		dw1000_anchor_rxcallback(data);
	}
}

/******************************************************************************/
// Required API implementation for the DecaWave library
/******************************************************************************/

// Blocking SPI transfer
static void spi_transfer () {

	// Enable NSS output for master mode
	SPI_SSOutputCmd(SPI1, ENABLE);

	// Enable DMA1 Channel1 Transfer Complete interrupt
	//DMA_ITConfig(SPI1_TX_DMA_CHANNEL, DMA_IT_TC, ENABLE);

	// Enable the DMA channels
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
	DMA_Cmd(SPI1_RX_DMA_CHANNEL, ENABLE);
	DMA_Cmd(SPI1_TX_DMA_CHANNEL, ENABLE);

	// Wait for everything to finish
	//TODO: Implement timeout so we don't get stuck
	//uint32_t TimeOut = USER_TIMEOUT;
	while ((DMA_GetFlagStatus(SPI1_RX_DMA_FLAG_TC) == RESET));
	while ((DMA_GetFlagStatus(SPI1_TX_DMA_FLAG_TC) == RESET));
	/* The BSY flag can be monitored to ensure that the SPI communication is complete.
	This is required to avoid corrupting the last transmission before disabling
	the SPI or entering the Stop mode. The software must first wait until TXE=1
	and then until BSY=0.*/
	while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET));
	while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET));

	// End the SPI transaction and DMA
	// Clear DMA1 global flags
	DMA_ClearFlag(SPI1_TX_DMA_FLAG_GL);
	DMA_ClearFlag(SPI1_RX_DMA_FLAG_GL);

	// Disable the DMA channels
	DMA_Cmd(SPI1_RX_DMA_CHANNEL, DISABLE);
	DMA_Cmd(SPI1_TX_DMA_CHANNEL, DISABLE);

	// Disable the SPI Rx and Tx DMA requests
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, DISABLE);
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
}

// Called by the DW1000 library to issue a read command to the DW1000.
int readfromspi (uint16_t headerLength,
                 const uint8_t *headerBuffer,
                 uint32_t readlength,
                 uint8_t *readBuffer) {

	SPI_Cmd(SPI1, ENABLE);
	setup_dma_write(headerLength, headerBuffer);
	spi_transfer();

	setup_dma_read(readlength, readBuffer);
	spi_transfer();

	SPI_Cmd(SPI1, DISABLE);
	return 0;
}

// Called by the DW1000 library to issue a write to the DW1000.
int writetospi (uint16_t headerLength,
                const uint8_t *headerBuffer,
                uint32_t bodylength,
                const uint8_t *bodyBuffer) {

	SPI_Cmd(SPI1, ENABLE);
	setup_dma_write(headerLength, headerBuffer);
	spi_transfer();

	setup_dma_write(bodylength, bodyBuffer);
	spi_transfer();

	SPI_Cmd(SPI1, DISABLE);
	return 0;
}

// Atomic blocks for the DW1000 library
decaIrqStatus_t decamutexon () {
	if (dw1000_irq_onoff == 1) {
		dw1000_interrupt_disable();
		dw1000_irq_onoff = 0;
		return 1;
	}
	return 0;
}

// Atomic blocks for the DW1000 library
void decamutexoff (decaIrqStatus_t s) {
	if (s) {
		dw1000_interrupt_enable();
		dw1000_irq_onoff = 1;
	}
}

// Rename this function for the DW1000 library.
void usleep (uint32_t u) {
	uDelay(u);
}


/******************************************************************************/
// Generic DW1000 functions - shared with anchor and tag
/******************************************************************************/

// Hard reset the DW1000 using its reset pin
void dw1000_reset () {
	// To reset, assert the reset pin for 100ms
	GPIO_WriteBit(DW_RESET_PORT, DW_RESET_PIN, Bit_RESET);
	// Wait for ~100ms
	mDelay(100);
	GPIO_WriteBit(DW_RESET_PORT, DW_RESET_PIN, Bit_SET);
}

// Choose which antenna to connect to the radio
void dw1000_choose_antenna(uint8_t antenna_number) {
	// Antenna selection comes from the STM32 chip instead of the DW1000 now

	// Set all of them low
	GPIO_WriteBit(ANT_SEL0_PORT, ANT_SEL0_PIN, Bit_RESET);
	GPIO_WriteBit(ANT_SEL1_PORT, ANT_SEL1_PIN, Bit_RESET);
	GPIO_WriteBit(ANT_SEL2_PORT, ANT_SEL2_PIN, Bit_RESET);

	// Set the one we want high
	switch (antenna_number) {
		case 0: GPIO_WriteBit(ANT_SEL0_PORT, ANT_SEL0_PIN, Bit_SET); break;
		case 1: GPIO_WriteBit(ANT_SEL1_PORT, ANT_SEL1_PIN, Bit_SET); break;
		case 2: GPIO_WriteBit(ANT_SEL2_PORT, ANT_SEL2_PIN, Bit_SET); break;
	}
}

// Read this node's EUI from the correct address in flash
void dw1000_read_eui (uint8_t *eui_buf) {
	memcpy(eui_buf, (uint8_t*) EUI_FLASH_LOCATION, EUI_LEN);
}

// First (generic) init of the DW1000
dw1000_err_e dw1000_init () {

	// Do the STM setup that initializes pin and peripherals and whatnot.
	if (!_stm_dw1000_interface_setup) {
		setup();
	}

	// Make sure the SPI clock is slow so that the DW1000 doesn't miss any
	// edges.
	dw1000_spi_slow();

	// Reset the dw1000...for some reason
	dw1000_reset();
	uDelay(100);

	// Make sure we can talk to the DW1000
	uint32_t devID;
	devID = dwt_readdevid();
	if (devID != DWT_DEVICE_ID) {
		//if we can't talk to dw1000, return with an error
		uDelay(1000);
		return DW1000_COMM_ERR;
	}

	// Choose antenna 0 as a default
	dw1000_choose_antenna(0);

	// Setup our settings for the DW1000
	dw1000_configure_settings();

	// Put the SPI back.
	dw1000_spi_fast();

	return DW1000_NO_ERR;
}

// Apply a suite of baseline settings that we care about.
// This is split out so we can call it after sleeping.
void dw1000_configure_settings () {

	// Also need the SPI slow here.
	dw1000_spi_slow();

	// Initialize the dw1000 hardware
	uint32_t err;
	err = dwt_initialise(DWT_LOADUCODE |
	                     DWT_LOADLDO |
	                     DWT_LOADTXCONFIG |
	                     DWT_LOADXTALTRIM);

	if (err != DWT_SUCCESS) {
		return DW1000_COMM_ERR;
	}

	// Configure sleep parameters.
	// Note: This is taken from the decawave fast2wr_t.c file. I don't have
	//       a great idea as to whether this is right or not.
	dwt_configuresleep(DWT_LOADLDO |
	                   DWT_LOADUCODE |
	                   DWT_PRESRV_SLEEP |
	                   DWT_LOADOPSET |
	                   DWT_CONFIG,
	                   DWT_WAKE_WK | DWT_SLP_EN);

	// Configure interrupts and callbacks
	dwt_setinterrupt(0xFFFFFFFF, 0);
	dwt_setinterrupt(DWT_INT_TFRS |
	                 DWT_INT_RFCG |
	                 DWT_INT_RPHE |
	                 DWT_INT_RFCE |
	                 DWT_INT_RFSL |
	                 DWT_INT_RFTO |
	                 DWT_INT_RXPTO |
	                 DWT_INT_SFDT |
	                 DWT_INT_RXOVRR |
	                 DWT_INT_ARFE, 1);

	dwt_setcallbacks(txcallback, rxcallback);

	// Set the parameters of ranging and channel and whatnot
	global_ranging_config.chan           = 2;
	global_ranging_config.prf            = DWT_PRF_64M;
	global_ranging_config.txPreambLength = DWT_PLEN_64;
	global_ranging_config.rxPAC          = DWT_PAC8;
	global_ranging_config.txCode         = 9;  // preamble code
	global_ranging_config.rxCode         = 9;  // preamble code
	global_ranging_config.nsSFD          = 0;
	global_ranging_config.dataRate       = DWT_BR_6M8;
	global_ranging_config.phrMode        = DWT_PHRMODE_EXT; //Enable extended PHR mode (up to 1024-byte packets)
	global_ranging_config.smartPowerEn   = 1;
	global_ranging_config.sfdTO          = 64+8+1;//(1025 + 64 - 32);
#if DW1000_USE_OTP
	dwt_configure(&global_ranging_config, (DWT_LOADANTDLY | DWT_LOADXTALTRIM));
#else
	dwt_configure(&global_ranging_config, 0);
#endif
	dwt_setsmarttxpower(global_ranging_config.smartPowerEn);

	// Configure TX power based on the channel used
	global_tx_config.PGdly = pgDelay[global_ranging_config.chan];
	global_tx_config.power = txPower[global_ranging_config.chan];
	dwt_configuretxrf(&global_tx_config);

	// Need to set some radio properties. Ideally these would come from the
	// OTP memory on the DW1000
#if DW1000_USE_OTP == 0

	// This defaults to 8. Don't know why.
	dwt_xtaltrim(DW1000_DEFAULT_XTALTRIM);

	// Antenna delay we don't really care about so we just use 0
	dwt_setrxantennadelay(DW1000_ANTENNA_DELAY_RX);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
#endif

	// Always good to make sure we don't trap the SPI speed too slow
	dw1000_spi_fast();
}

// This allows the code to select if this device is a TAG or ANCHOR.
// This will also init the correct mode.
void dw1000_set_mode (dw1000_role_e role) {
	_my_role = role;
	if (role == TAG) {
		dw1000_tag_init();
	} else if (role == ANCHOR) {
		dw1000_anchor_init();
	}
}

// Returns the mode this device is set to.
dw1000_role_e dw1000_get_mode () {
	return _my_role;
}

// Wake the DW1000 from sleep by asserting the WAKEUP pin
dw1000_err_e dw1000_wakeup () {
	// Assert the WAKEUP pin. There seems to be some weirdness where a single
	// WAKEUP assert can get missed, so we do it multiple times to make
	// sure the DW1000 is awake.
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_SET);
	uDelay(1000);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_RESET);
	uDelay(100);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_SET);
	uDelay(1000);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_RESET);
	uDelay(100);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_SET);
	uDelay(1000);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_RESET);
	uDelay(100);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_SET);
	uDelay(1000);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_RESET);

	// Now wait for 5ms for the chip to move from the wakeup to the idle
	// state. The datasheet says 4ms, but we buffer a little in case things
	// take longer or mDelay isn't exact.
	mDelay(5);

	// Slow SPI and wait for the DW1000 to respond.
	dw1000_spi_slow();
	uint32_t devID;
	uint8_t tries = 0;
	do {
		devID = dwt_readdevid();
		if (devID != DWT_DEVICE_ID) {
			//if we can't talk to dw1000, try again
			uDelay(100);
			tries++;
		}
	} while ((devID != DWT_DEVICE_ID) && (tries <= DW1000_NUM_CONTACT_TRIES_BEFORE_RESET));

	if (tries > DW1000_NUM_CONTACT_TRIES_BEFORE_RESET) {
		// Something went wrong with wakeup. In theory, this won't happen,
		// but having an unterminated while() loop is probably bad, so we
		// have this escape hatch. At this point I have no idea what went wrong,
		// but we probably need to reset the chip at this point.
		return DW1000_WAKEUP_ERR;
	}

	// Go back fast again
	dw1000_spi_fast();

	return DW1000_NO_ERR;
}


/******************************************************************************/
// Decawave specific utility functions
/******************************************************************************/

// Convert a time of flight measurement to millimeters
int dwtime_to_millimeters (double dwtime) {
	// Get meters using the speed of light
	double dist = dwtime * DWT_TIME_UNITS * SPEED_OF_LIGHT;

	// And return millimeters
	int millimeters = (int) (dist*1000.0);

	// TODO
	// TODO
	// TODO
	// TODO
	// TODO
	// TODO: ADD IN THE ACTUAL CALIBRATION DATA SOMEWHERE!!!!!!!!!!!!
	// TODO
	// TODO
	// TODO
	// TODO
	// TODO
	// TODO
	// TODO
	// Subtract off 154 meters.
	return millimeters - 154000;
}


/******************************************************************************/
// Misc Utility
/******************************************************************************/

// Shoved this here for now.
// Insert an element into a sorted array.
// end is the number of elements in the array.
void insert_sorted (int arr[], int new, unsigned end) {
	unsigned insert_at = 0;
	while ((insert_at < end) && (new >= arr[insert_at])) {
		insert_at++;
	}
	if (insert_at == end) {
		arr[insert_at] = new;
	} else {
		while (insert_at <= end) {
			int temp = arr[insert_at];
			arr[insert_at] = new;
			new = temp;
			insert_at++;
		}
	}
}

/******************************************************************************/
// Ranging Protocol Algorithm Functions
/******************************************************************************/

// Return the RF channel to use for a given subsequence number
static uint8_t subsequence_number_to_channel (uint8_t subseq_num) {
	// ALGORITHM
	// We iterate through the channels as fast as possible. We do this to
	// find anchors that may not be listening on the first channel as quickly
	// as possible so that they can join the sequence as early as possible. This
	// increases the number of successful packet transmissions and increases
	// ranging accuracy.
	uint8_t channel_index = subseq_num % NUM_RANGING_CHANNELS;
	return channel_index_to_channel_rf_number[channel_index];
}

// Return the Antenna index to use for a given subsequence number
uint8_t subsequence_number_to_antenna (dw1000_role_e role, uint8_t subseq_num) {
	// ALGORITHM
	// We must rotate the anchor and tag antennas differently so the same
	// ones don't always overlap. This should also be different from the
	// channel sequence. This math is a little weird but somehow works out,
	// even if NUM_RANGING_CHANNELS != NUM_ANTENNAS.
	if (role == TAG) {
		return (subseq_num / NUM_RANGING_CHANNELS) % NUM_ANTENNAS;
	} else if (role == ANCHOR) {
		return ((subseq_num / NUM_RANGING_CHANNELS) / NUM_RANGING_CHANNELS) % NUM_ANTENNAS;
	} else {
		return 0;
	}
}

// Go the opposite way and return the ss number based on the antenna used.
// Returns the LAST valid slot that matches the sequence.
static uint8_t antenna_and_channel_to_subsequence_number (uint8_t tag_antenna_index,
                                                          uint8_t anchor_antenna_index,
                                                          uint8_t channel_index) {
	uint8_t anc_offset = anchor_antenna_index * NUM_RANGING_CHANNELS * NUM_RANGING_CHANNELS;
	uint8_t tag_offset = tag_antenna_index * NUM_RANGING_CHANNELS;
	uint8_t base_offset = anc_offset + tag_offset + channel_index;

	// Now find the last one that matches this index.
	// We do this by finding the last possible breaking point between
	// repeated rounds and determining if we should go just pass that point
	// or before it.
	uint8_t a = (uint8_t) ((NUM_RANGING_BROADCASTS / NUM_UNIQUE_PACKET_CONFIGURATIONS) * NUM_UNIQUE_PACKET_CONFIGURATIONS);
	if ((base_offset + a) < NUM_RANGING_BROADCASTS) {
		return base_offset + a;
	} else {
		return a - (NUM_UNIQUE_PACKET_CONFIGURATIONS - base_offset);
	}
}

// Return the RF channel to use when the anchors respond to the tag
static uint8_t listening_window_number_to_channel (uint8_t window_num) {
	return window_num % NUM_RANGING_CHANNELS;
}

// Called when dw1000 tx/rx config settings and constants should be re applied
static void reset_configuration () {
#if DW1000_USE_OTP
	dwt_configure(&global_ranging_config, (DWT_LOADANTDLY | DWT_LOADXTALTRIM));
#else
	dwt_configure(&global_ranging_config, 0);
#endif
	dwt_setsmarttxpower(global_ranging_config.smartPowerEn);
	global_tx_config.PGdly = pgDelay[global_ranging_config.chan];
	global_tx_config.power = txPower[global_ranging_config.chan];
	dwt_configuretxrf(&global_tx_config);
#if DW1000_USE_OTP == 0
	dwt_setrxantennadelay(DW1000_ANTENNA_DELAY_RX);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
#endif
}

// Update the Antenna and Channel settings to correspond with the settings
// for the given subsequence number.
//
// role:       anchor or tag
// subseq_num: where in the sequence we are
void dw1000_set_ranging_broadcast_subsequence_settings (dw1000_role_e role,
                                                        uint8_t subseq_num) {
	// Stop the transceiver on the anchor. Don't know why.
	if (role == ANCHOR) {
		dwt_forcetrxoff();
	}

	// Change the channel depending on what subsequence number we're at
	global_ranging_config.chan = subsequence_number_to_channel(subseq_num);

	// Force the settings upon the DW1000
	reset_configuration();

	// Change what antenna we're listening/sending on
	dw1000_choose_antenna(subsequence_number_to_antenna(role, subseq_num));
}

// Update the Antenna and Channel settings to correspond with the settings
// for the given listening window.
//
// role:       anchor or tag
// window_num: where in the listening window we are
void dw1000_set_ranging_listening_window_settings (dw1000_role_e role,
                                                   uint8_t window_num,
                                                   uint8_t antenna_num) {
	// Change the channel depending on what window number we're at
	global_ranging_config.chan = listening_window_number_to_channel(window_num);

	// Force the settings to apply
	reset_configuration();

	// Change what antenna we're listening/sending on
	dw1000_choose_antenna(antenna_num);
}

// Get the subsequence slot number that a particular set of settings
// (anchor antenna index, tag antenna index, channel) were used to send
// a broadcast poll message. The tag antenna index and channel are derived
// from the settings used in the listening window.
uint8_t dw1000_get_ss_index_from_settings (uint8_t anchor_antenna_index,
                                           uint8_t window_num) {
	// NOTE: need something more rigorous than setting 0 here
	uint8_t tag_antenna_index = 0;
	uint8_t channel_index = listening_window_number_to_channel(window_num);

	return antenna_and_channel_to_subsequence_number(tag_antenna_index,
	                                                 anchor_antenna_index,
	                                                 channel_index);
}
