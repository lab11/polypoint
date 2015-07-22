#include <string.h>
#include "stm32f0xx_spi.h"
#include "stm32f0xx_dma.h"
#include "stm32f0xx_exti.h"
#include "stm32f0xx_syscfg.h"
#include "led.h"

#include "deca_device_api.h"

#include "board.h"
#include "dw1000.h"


DMA_InitTypeDef DMA_InitStructure;
SPI_InitTypeDef SPI_InitStructure;

// Keep track of state to signal the caller when we are done
dw1000_callback callback;
dw1000_cb_e     callback_event;

decaIrqStatus_t dw1000_irq_onoff = 0;


static void usleep(uint32_t uSeconds) {
	//I think we are running about 48MHz
	for(volatile uint32_t sec = 0; sec < (uSeconds*48); sec++) {

	}
}

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
	NVIC_InitTypeDef NVIC_InitStructure;

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

	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	// SPI SCK pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_SCK_PIN;
	GPIO_Init(SPI1_SCK_GPIO_PORT, &GPIO_InitStructure);

	// SPI MOSI pin configuration
	GPIO_InitStructure.GPIO_Pin =  SPI1_MOSI_PIN;
	GPIO_Init(SPI1_MOSI_GPIO_PORT, &GPIO_InitStructure);

	// SPI MISO pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_MISO_PIN;
	GPIO_Init(SPI1_MISO_GPIO_PORT, &GPIO_InitStructure);

	// SPI NSS pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_NSS_PIN;
	GPIO_Init(SPI1_NSS_GPIO_PORT, &GPIO_InitStructure);

	// SPI configuration
	SPI_I2S_DeInit(SPI1);
	SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
	SPI_InitStructure.SPI_NSS               = SPI_NSS_Hard;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
	SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial     = 7;
	SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
	SPI_Init(SPI1, &SPI_InitStructure);


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



	// Pre-populate DMA fields that don't need to change
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) SPI1_DR_ADDRESS;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize     =  DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
	DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
}

//setup to disable rx - because who cares about rx on a write
static void setup_dma_write(uint32_t length, uint8_t* tx) {

	DMA_Cmd(SPI1_RX_DMA_CHANNEL, DISABLE);

	// DMA channel Tx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) tx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_TX_DMA_CHANNEL, &DMA_InitStructure);
	DMA_Cmd(SPI1_TX_DMA_CHANNEL, ENABLE);

}

//sets tx to no increment and repeatedly sends 0's
static void setup_dma_read(uint32_t length, uint8_t* rx) {

	// DMA channel Rx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) rx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_RX_DMA_CHANNEL, &DMA_InitStructure);
	DMA_Cmd(SPI1_RX_DMA_CHANNEL, ENABLE);

	static uint8_t tx = 0;
	// DMA channel Tx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) &tx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Disable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_TX_DMA_CHANNEL, &DMA_InitStructure);
	DMA_Cmd(SPI1_TX_DMA_CHANNEL, ENABLE);
}

static void setup_dma (uint32_t length, uint8_t* rx, uint8_t* tx) {

	NVIC_InitTypeDef NVIC_InitStructure;

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
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_TX_DMA_CHANNEL, &DMA_InitStructure);

	// Enable DMA1 SPI IRQ Channel
	// NVIC_InitStructure.NVIC_IRQChannel = SPI1_DMA_IRQn;
	// NVIC_InitStructure.NVIC_IRQChannelPriority = 0;
	// NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	// NVIC_Init(&NVIC_InitStructure);
}


/**
  * @brief  This function handles DMA1 Channel 1 interrupt request.
  * @param  None
  * @retval None
  */
void DMA1_Channel2_3_IRQHandler(void) {

	// if (DMA_GetITStatus(DMA1_IT_TC3)== SET) {
	// 	// led_toggle(LED2);

	// 	// Clear the interrupt
	// 	DMA_ClearITPendingBit(DMA1_IT_TC3);

	// 	// End the SPI transaction and DMA
	// 	// Clear DMA1 global flags
	// 	DMA_ClearFlag(SPI1_TX_DMA_FLAG_GL);
	// 	DMA_ClearFlag(SPI1_RX_DMA_FLAG_GL);

	// 	// Disable the DMA channels
	// 	DMA_Cmd(SPI1_RX_DMA_CHANNEL, DISABLE);
	// 	DMA_Cmd(SPI1_TX_DMA_CHANNEL, DISABLE);

	// 	// Disable the SPI peripheral
	// 	SPI_Cmd(SPI1, DISABLE);

	// 	// Disable the SPI Rx and Tx DMA requests
	// 	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, DISABLE);
	// 	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);

	// 	// return success
	// 	callback(callback_event, 0);
	// }
}


/**
  * @brief  This function handles External line 2 to 3 interrupt request.
  * @param  None
  * @retval None
  */
void EXTI2_3_IRQHandler(void) {


  if(EXTI_GetITStatus(EXTI_Line2) != RESET) {
    led_toggle(LED2);

    // Clear the EXTI line 2 pending bit
    EXTI_ClearITPendingBit(EXTI_Line2);
  }
}


// Blocking SPI transfer
//static void spi_transfer (dw1000_callback cb, dw1000_cb_e evt) {
static void spi_transfer() {

	//callback = cb;
	//callback_event = evt;

	// Enable the SPI Rx and Tx DMA requests
	//try moving these down to kickoff the spy tranactions
	//SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
	//SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

	// Enable the SPI peripheral
	SPI_Cmd(SPI1, ENABLE);

	// Enable NSS output for master mode
	SPI_SSOutputCmd(SPI1, ENABLE);

	// Enable DMA1 Channel1 Transfer Complete interrupt
	// DMA_ITConfig(SPI1_TX_DMA_CHANNEL, DMA_IT_TC, ENABLE);

	// Enable the DMA channels
	// I'm going to try moving these to the dma setup commands
	//DMA_Cmd(SPI1_RX_DMA_CHANNEL, ENABLE);
	//DMA_Cmd(SPI1_TX_DMA_CHANNEL, ENABLE);
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

	// Wait for everything to finish
	uint32_t TimeOut = USER_TIMEOUT;
	while ((DMA_GetFlagStatus(SPI1_RX_DMA_FLAG_TC) == RESET));
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

	// Disable the SPI peripheral
	SPI_Cmd(SPI1, DISABLE);

	// Disable the SPI Rx and Tx DMA requests
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, DISABLE);
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
}

static void dw1000_reset () {
	GPIO_InitTypeDef GPIO_InitStructure;

	// Make the reset pin output
	GPIO_InitStructure.GPIO_Pin = DW_RESET_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_RESET_PORT, &GPIO_InitStructure);

	// Set it low
	DW_RESET_PORT->BRR = DW_RESET_PIN;

	// Wait for ~100ms
	usleep(100000);

	// Set it back to an input
	GPIO_InitStructure.GPIO_Pin = DW_RESET_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_RESET_PORT, &GPIO_InitStructure);
}

void dw1000_init (dw1000_callback cb) {
	uint32_t devID;

	uint8_t tx[5] = {5,7,9,11,13};
	uint8_t rx[5] = {2,4,6,8,10};

	// Do the STM setup that initializes pin and peripherals and whatnot
	setup();

	// Reset the DW1000...for some reason
	dw1000_reset();

	// Make sure we can talk to the DW1000
	devID = dwt_readdevid();
	if (devID != DWT_DEVICE_ID) {
// #ifdef DW_DEBUG
// 		printf("Could not read Device ID from the DW1000\r\n");
// 		printf("Possible the chip is asleep...\r\n");
// #endif
		return;
	}

	setup_dma(5, rx, tx);
	//spi_transfer(cb, DW1000_INIT_DONE);
	spi_transfer();

}

int readfromspi(uint16_t headerLength,
				const uint8_t *headerBuffer,
				uint32_t readlength,
				uint8_t *readBuffer) {

	setup_dma_write(headerLength, headerBuffer);
	spi_transfer();


	setup_dma_read(readlength, readBuffer);
	spi_transfer();

	return 0;
}

int writetospi(uint16_t headerLength,
				const uint8_t *headerBuffer,
				uint32_t bodylength,
				const uint8_t *bodyBuffer) {

	setup_dma_write(headerLength, headerBuffer);
	spi_transfer();

	setup_dma_write(bodylength, bodyBuffer);
	spi_transfer();
	
	return 0;
}

decaIrqStatus_t decamutexon() {
	if(dw1000_irq_onoff == 1) {
		dw1000_interrupt_disable();
		dw1000_irq_onoff = 0;
		return 1;
	}
	return 0;
}

void decamutexoff (decaIrqStatus_t s) {
	if(s) {
		dw1000_interrupt_enable();
		dw1000_irq_onoff = 1;
	}
}
