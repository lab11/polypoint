#include "stm32f0xx_spi.h"
#include "stm32f0xx_dma.h"

#include "board.h"


DMA_InitTypeDef DMA_InitStructure;
__IO uint32_t TimeOut = 0x0;

static void setup () {

	GPIO_InitTypeDef GPIO_InitStructure;
	SPI_InitTypeDef SPI_InitStructure;


	// Enable the SPI peripheral
	RCC_APB2PeriphClockCmd(SPI1_CLK, ENABLE);

	// Enable the DMA peripheral
	RCC_AHBPeriphClockCmd(DMA1_CLK, ENABLE);

	// Enable the TIM peripheral
	// RCC_APB1PeriphClockCmd(TIMx_CLK, ENABLE);

	// Enable SCK, MOSI, MISO and NSS GPIO clocks
	RCC_AHBPeriphClockCmd(SPI1_SCK_GPIO_CLK |
	                      SPI1_MISO_GPIO_CLK |
	                      SPI1_MOSI_GPIO_CLK |
	                      SPI1_NSS_GPIO_CLK , ENABLE);

	/* Enable TIM DMA trigger clock */
	// RCC_AHBPeriphClockCmd(TIMx_TRIGGER_GPIO_CLK, ENABLE);

	// SPI pin mappings
	GPIO_PinAFConfig(SPI1_SCK_GPIO_PORT, SPI1_SCK_SOURCE, SPI1_SCK_AF);
	GPIO_PinAFConfig(SPI1_MOSI_GPIO_PORT, SPI1_MOSI_SOURCE, SPI1_MOSI_AF);
	GPIO_PinAFConfig(SPI1_MISO_GPIO_PORT, SPI1_MISO_SOURCE, SPI1_MISO_AF);
	GPIO_PinAFConfig(SPI1_NSS_GPIO_PORT, SPI1_NSS_SOURCE, SPI1_NSS_AF);

	// TIM capture compare pin mapping
	// GPIO_PinAFConfig(TIMx_TRIGGER_GPIO_PORT, TIMx_TRIGGER_SOURCE, TIMx_TRIGGER_AF);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	// GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_3;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	// SPI SCK pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_SCK_PIN;
	GPIO_Init(SPI1_SCK_GPIO_PORT, &GPIO_InitStructure);

	// SPI  MOSI pin configuration
	GPIO_InitStructure.GPIO_Pin =  SPI1_MOSI_PIN;
	GPIO_Init(SPI1_MOSI_GPIO_PORT, &GPIO_InitStructure);

	// SPI MISO pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_MISO_PIN;
	GPIO_Init(SPI1_MISO_GPIO_PORT, &GPIO_InitStructure);

	// SPI NSS pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_NSS_PIN;
	GPIO_Init(SPI1_NSS_GPIO_PORT, &GPIO_InitStructure);

	// Configure the TIM channelx capture compare as DMA Trigger
	// GPIO_InitStructure.GPIO_Pin =  TIMx_TRIGGER_PIN;
	// GPIO_Init(TIMx_TRIGGER_GPIO_PORT, &GPIO_InitStructure);

	// SPI configuration
	SPI_I2S_DeInit(SPI1);
	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
	SPI_InitStructure.SPI_NSS = SPI_NSS_Hard;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial = 7;
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
	SPI_Init(SPI1, &SPI_InitStructure);

	/* Initialize the FIFO threshold */
  // SPI_RxFIFOThresholdConfig(SPIx, SPI_RxFIFOThreshold_QF);


	// Pre-populate DMA fields that don't need to change
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) SPI1_DR_ADDRESS;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize =  DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;

	led_toggle(LED1);


}


static void setup_dma (uint32_t length, uint8_t* rx, uint8_t* tx) {

	NVIC_InitTypeDef NVIC_InitStructure;

	// DMA channel Rx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) rx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_RX_DMA_CHANNEL, &DMA_InitStructure);

	// DMA channel Tx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) tx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_TX_DMA_CHANNEL, &DMA_InitStructure);

	// Enable DMA1 Channel1 Transfer Complete interrupt
	// DMA_ITConfig(SPI1_RX_DMA_CHANNEL, DMA_IT_TC, ENABLE);

	// Enable DMA1 channel1 IRQ Channel
	NVIC_InitStructure.NVIC_IRQChannel = SPI1_DMA_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

}

void TimeOut_UserCallback () {
	// led_toggle(LED1);
}

/**
  * @brief  This function handles DMA1 Channel 1 interrupt request.
  * @param  None
  * @retval None
  */
void DMA1_Channel2_3_IRQHandler(void)
{


  /* Test on DMA1 Channel2 Transfer Complete interrupt */
  if(DMA_GetITStatus(DMA1_IT_TC2))
  {
    /* DMA1 finished the transfer of SrcBuffer */
    // EndOfTransfer = 1;
    // led_toggle(LED2);

    /* Clear DMA1 Channel1 Half Transfer, Transfer Complete and Global interrupt pending bits */
    DMA_ClearITPendingBit(DMA1_IT_GL2);
  }

  if (DMA_GetITStatus(DMA1_IT_TC3)== SET)
  {

  	led_toggle(LED2);

    // TxStatus = 1;
    DMA_ClearITPendingBit(DMA1_IT_TC3);


    /* Clear DMA1 global flags */
    DMA_ClearFlag(SPI1_TX_DMA_FLAG_GL);
    DMA_ClearFlag(SPI1_RX_DMA_FLAG_GL);

    /* Disable the DMA channels */
    DMA_Cmd(SPI1_RX_DMA_CHANNEL, DISABLE);
    DMA_Cmd(SPI1_TX_DMA_CHANNEL, DISABLE);

    /* Disable the SPI peripheral */
    SPI_Cmd(SPI1, DISABLE);

    /* Disable the SPI Rx and Tx DMA requests */
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, DISABLE);
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);

  }
}

static void spi_transfer () {
// Enable the SPI peripheral
	// SPI_Cmd(SPI1, ENABLE);

	// // Enable the SPI Rx and Tx DMA requests
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

	// Enable the SPI peripheral
	SPI_Cmd(SPI1, ENABLE);

	// Enable NSS output for master mode
	SPI_SSOutputCmd(SPI1, ENABLE);

	// Enable DMA1 Channel1 Transfer Complete interrupt
	DMA_ITConfig(SPI1_TX_DMA_CHANNEL, DMA_IT_TC, ENABLE);

	// Enable the DMA channels
	DMA_Cmd(SPI1_RX_DMA_CHANNEL, ENABLE);
	DMA_Cmd(SPI1_TX_DMA_CHANNEL, ENABLE);

	// SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
	// SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);

	 // Enable the SPI Rx and Tx DMA requests
	// SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
	// SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);









/* Enable the DMA channels */
    // DMA_Cmd(SPI1_RX_DMA_CHANNEL, ENABLE);
    // DMA_Cmd(SPI1_TX_DMA_CHANNEL, ENABLE);

    /* Wait the SPI DMA transfers complete or time out */
    // while (DMA_GetFlagStatus(SPI1_RX_DMA_FLAG_TC) == RESET)
    // {}

    // TimeOut = USER_TIMEOUT;
    // while ((DMA_GetFlagStatus(SPI1_TX_DMA_FLAG_TC) == RESET)&&(TimeOut != 0x00))
    // {}
    // if(TimeOut == 0)
    // {
    //   TimeOut_UserCallback();
    // }

    // /* The BSY flag can be monitored to ensure that the SPI communication is complete.
    // This is required to avoid corrupting the last transmission before disabling
    // the SPI or entering the Stop mode. The software must first wait until TXE=1
    // and then until BSY=0.*/
    // TimeOut = USER_TIMEOUT;
    // while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET)&&(TimeOut != 0x00))
    // {}
    // if(TimeOut == 0)
    // {
    //   TimeOut_UserCallback();
    // }

    // TimeOut = USER_TIMEOUT;
    // while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET)&&(TimeOut != 0x00))
    // {}
    // if(TimeOut == 0)
    // {
    //   TimeOut_UserCallback();
    // }

    // /* Clear DMA1 global flags */
    // DMA_ClearFlag(SPI1_TX_DMA_FLAG_GL);
    // DMA_ClearFlag(SPI1_RX_DMA_FLAG_GL);

    // /* Disable the DMA channels */
    // DMA_Cmd(SPI1_RX_DMA_CHANNEL, DISABLE);
    // DMA_Cmd(SPI1_TX_DMA_CHANNEL, DISABLE);

    // /* Disable the SPI peripheral */
    // SPI_Cmd(SPI1, DISABLE);

    // /* Disable the SPI Rx and Tx DMA requests */
    // SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, DISABLE);
    // SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);


	// led_toggle(LED2);




}

void dw1000_init () {

	uint8_t tx[5] = {5,7,9,11,13};
	uint8_t rx[5] = {2,4,6,8,10};

	setup();

	setup_dma(5, rx, tx);
	spi_transfer();


}


