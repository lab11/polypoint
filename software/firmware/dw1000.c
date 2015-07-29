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

#define DW1000_PANID 0xD100

static void setXtalTrim(uint8_t trim) {
	//somehow set xtaltrim on dw1000 mem for calibration
}

static uint8_t getXtalTrim() {
	//somehow retrieve xtaltrim from dw1000

	//return default value
	return 8;
}

static uint8_t getEUI() {
	return 0;
}


const uint8_t pgDelay[8] = {
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
const uint32_t txPower[8] = {
	0x0,
	0x07274767UL,
	0x07274767UL,
	0x2B4B6B8BUL,
	0x3A5A7A9AUL,
	0x25456585UL,
	0x0,
	0x5171B1D1UL
};

static void setTxDelayCal(double txdelay) {
	//somehow set delay cal on dw10000 mem
}

static double getTxDelayCal() {

	//somehow retrieve delay cal from dw1000 mem
	return 0;
}

DMA_InitTypeDef DMA_InitStructure;
SPI_InitTypeDef SPI_InitStructure;

// Keep track of state to signal the caller when we are done
//dw1000_callback callback;
//dw1000_cb_e     callback_event;

decaIrqStatus_t dw1000_irq_onoff = 0;

static dwt_config_t global_ranging_config;
static dwt_txconfig_t global_tx_config;

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
	//NVIC_InitTypeDef NVIC_InitStructure;

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

	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

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
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_32;
	SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial     = 7;
	SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
	SPI1->CR1 &= 0xCFFF;
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

	//setup antenna pins - select no antennas
	RCC_AHBPeriphClockCmd(ANT_SEL0_CLK, ENABLE);
	RCC_AHBPeriphClockCmd(ANT_SEL1_CLK, ENABLE);
	RCC_AHBPeriphClockCmd(ANT_SEL2_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = ANT_SEL0_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(ANT_SEL0_PORT, &GPIO_InitStructure);
	ANT_SEL0_PORT->BRR = ANT_SEL0_PIN;

	GPIO_InitStructure.GPIO_Pin = ANT_SEL1_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(ANT_SEL1_PORT, &GPIO_InitStructure);
	ANT_SEL1_PORT->BRR = ANT_SEL1_PIN;

	GPIO_InitStructure.GPIO_Pin = ANT_SEL2_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(ANT_SEL1_PORT, &GPIO_InitStructure);
	ANT_SEL2_PORT->BRR = ANT_SEL2_PIN;


	// Pre-populate DMA fields that don't need to change
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) SPI1_DR_ADDRESS;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
	DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
}

//setup to disable rx - because who cares about rx on a write
static void setup_dma_write(uint32_t length, uint8_t* tx) {

	//volatile uint8_t throw = SPI1->DR;
	//throw = SPI1->SR;

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

//sets tx to no increment and repeatedly sends 0's
static void setup_dma_read(uint32_t length, uint8_t* rx) {

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

static void setup_dma (uint32_t length, uint8_t* rx, uint8_t* tx) {

	//NVIC_InitTypeDef NVIC_InitStructure;

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

	 /*if (DMA_GetITStatus(DMA1_IT_TC3)== SET) {
	 	// led_toggle(LED2);

	 	// Clear the interrupt
	 	DMA_ClearITPendingBit(DMA1_IT_TC3);

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

	 	// return success
	// 	callback(callback_event, 0);
	}*/
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

void dw1000_reset () {
	GPIO_InitTypeDef GPIO_InitStructure;

	// Make the reset pin output
	GPIO_InitStructure.GPIO_Pin = DW_RESET_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(DW_RESET_PORT, &GPIO_InitStructure);


	GPIO_InitStructure.GPIO_Pin = DW_WAKEUP_PIN;
	GPIO_Init(DW_WAKEUP_PORT, &GPIO_InitStructure);

	//reset logic
	// Set it high
	DW_RESET_PORT->BSRR = DW_RESET_PIN;
	DW_RESET_PORT->BRR = DW_RESET_PIN;
	// Wait for ~100ms
	usleep(100000);
	DW_RESET_PORT->BSRR = DW_RESET_PIN;

	//wakeup logic?
	DW_WAKEUP_PORT->BSRR = DW_WAKEUP_PIN;

	// Set it back to an input
	/*GPIO_InitStructure.GPIO_Pin = DW_RESET_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_RESET_PORT, &GPIO_InitStructure);*/
}

void dw1000_choose_antenna(uint8_t antenna_number) {
	//antenna selection comes from the STM32 chip instead of the DW1000 now

	//set all of them low
	ANT_SEL0_PORT->BRR = ANT_SEL0_PIN;
	ANT_SEL1_PORT->BRR = ANT_SEL1_PIN;
	ANT_SEL2_PORT->BRR = ANT_SEL2_PIN;

	switch(antenna_number) {
		case 0:
			ANT_SEL0_PORT->BSRR = ANT_SEL0_PIN;
		break;
		case 1:
			ANT_SEL1_PORT->BSRR = ANT_SEL1_PIN;
		break;
		case 2:
			ANT_SEL2_PORT->BSRR = ANT_SEL2_PIN;
		break;
	}

}

void dw1000_populate_eui(uint8_t *eui_buf, uint8_t id) {
	eui_buf[0] = id;
	eui_buf[1] = 0x55;
	eui_buf[2] = 0x44;
	eui_buf[3] = 'N';
	eui_buf[4] = 'P';
	eui_buf[5] = 0xe5;
	eui_buf[6] = 0x98;
	eui_buf[7] = 0xc0;
}

static void txcallback(const dwt_callback_data_t *data) {

}

static void rxcallback(const dwt_callback_data_t *data) {

}

void dw1000_init_tag(dw1000_callback cb) {

	uint8_t eui_array[8];

	// Allow data and ack frames
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	dw1000_populate_eui(eui_array, getEUI());
	dwt_seteui(eui_array);
	dwt_setpanid(DW1000_PANID);

	// Do this for the tag too
	dwt_setautorxreenable(1);
	dwt_setdblrxbuffmode(1);
	dwt_enableautoack(5 /*ACK_RESPONSE_TIME*/);

	// Configure sleep
	{
		int mode = DWT_LOADUCODE    |
			DWT_PRESRV_SLEEP |
			DWT_CONFIG       |
			DWT_TANDV;
		if (dwt_getldotune() != 0) {
			// If we need to use LDO tune value from OTP kick it after sleep
			mode |= DWT_LOADLDO;
		}

		// NOTE: on the EVK1000 the DEEPSLEEP is not actually putting the
		// DW1000 into full DEEPSLEEP mode as XTAL is kept on
		dwt_configuresleep(mode, DWT_WAKE_CS | DWT_SLP_EN);
	}
}

void dw1000_init_anchor(dw1000_callback cb) {
	uint8_t eui_array[8];

	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);
	dw1000_populate_eui(eui_array, getEUI());
	dwt_seteui(eui_array);
	dwt_setpanid(DW1000_PANID);

	dwt_setautorxreenable(1);

	dwt_setdblrxbuffmode(0);
	dwt_setrxtimeout(0);

	dwt_rxenable(0);

}

void dw1000_init (dw1000_callback cb) {

	// Do the STM setup that initializes pin and peripherals and whatnot
	//start spi at ~3MHz
	setup();

	//reset the dw1000...for some reason
	dw1000_reset();
	usleep(100);

	// Make sure we can talk to the DW1000
	uint32_t devID;
	devID = dwt_readdevid();
	volatile uint32_t devComp = DWT_DEVICE_ID;
	if (devID != DWT_DEVICE_ID) {
		//if we can't talk to dw1000, return with an error
		cb(DW1000_INIT_DONE, DW1000_COMM_ERR);
		return;
	}

	//choose antenna 0
	dw1000_choose_antenna(0);

	//initialize the dw1000 hardware
	uint32_t err;
	err = dwt_initialise(DWT_LOADUCODE |
						DWT_LOADLDO		|
						DWT_LOADTXCONFIG |
						DWT_LOADXTALTRIM);

	if(err != DWT_SUCCESS) {
		cb(DW1000_INIT_DONE, err);
	}

	dwt_setinterrupt(0xFFFFFFFF, 0);

	dwt_setinterrupt( DWT_INT_TFRS |
					DWT_INT_RFCG |
					DWT_INT_RPHE |
					DWT_INT_RFCE |
					DWT_INT_RFSL |
					DWT_INT_RFTO |
					DWT_INT_RXPTO |
					DWT_INT_SFDT |
					DWT_INT_ARFE, 1);

	dwt_setcallbacks(txcallback, rxcallback);

	// Set the parameters of ranging and channel and whatnot
	global_ranging_config.chan           = 2;
	global_ranging_config.prf            = DWT_PRF_64M;
	global_ranging_config.txPreambLength = DWT_PLEN_64;//DWT_PLEN_4096
	// global_ranging_config.txPreambLength = DWT_PLEN_256;
	global_ranging_config.rxPAC          = DWT_PAC8;
	global_ranging_config.txCode         = 9;  // preamble code
	global_ranging_config.rxCode         = 9;  // preamble code
	global_ranging_config.nsSFD          = 0;
	global_ranging_config.dataRate       = DWT_BR_6M8;
	global_ranging_config.phrMode        = DWT_PHRMODE_EXT; //Enable extended PHR mode (up to 1024-byte packets)
	global_ranging_config.smartPowerEn   = 1;
	global_ranging_config.sfdTO          = 64+8+1;//(1025 + 64 - 32);
	dwt_configure(&global_ranging_config, 0);//(DWT_LOADANTDLY | DWT_LOADXTALTRIM));
	dwt_setsmarttxpower(global_ranging_config.smartPowerEn);

	// Configure TX power
	{
		global_tx_config.PGdly = pgDelay[global_ranging_config.chan];
		global_tx_config.power = txPower[global_ranging_config.chan];
		dwt_configuretxrf(&global_tx_config);
	}

	dwt_xtaltrim(getXtalTrim());


	// Configure the antenna delay settings
	{
		uint16_t antenna_delay;

		//Antenna delay not really necessary if we're doing an end-to-end calibration
		antenna_delay = 0;
		dwt_setrxantennadelay(antenna_delay);
		dwt_settxantennadelay(antenna_delay);
		//global_tx_antenna_delay = antenna_delay;

		//// Shift this over a bit for some reason. Who knows.
		//// instance_common.c:508
		//antenna_delay = dwt_readantennadelay(global_ranging_config.prf) >> 1;
		//if (antenna_delay == 0) {
		//    printf("resetting antenna delay\r\n");
		//    // If it's not in the OTP, use a magic value from instance_calib.c
		//    antenna_delay = ((DWT_PRF_64M_RFDLY/ 2.0) * 1e-9 / DWT_TIME_UNITS);
		//    dwt_setrxantennadelay(antenna_delay);
		//    dwt_settxantennadelay(antenna_delay);
		//}
		//global_tx_antenna_delay = antenna_delay;
		//printf("tx antenna delay: %u\r\n", antenna_delay);
	}

	//so that they can switch at runtime
	//make spi faster?

	cb(DW1000_INIT_DONE, DW1000_NO_ERR);
}

int readfromspi(uint16_t headerLength,
				const uint8_t *headerBuffer,
				uint32_t readlength,
				uint8_t *readBuffer) {

	volatile uint8_t hold = *headerBuffer;

	SPI_Cmd(SPI1, ENABLE);
	setup_dma_write(headerLength, headerBuffer);
	spi_transfer();

	setup_dma_read(readlength, readBuffer);
	spi_transfer();

	if (readBuffer[1] == 0x30) {
		led_on(LED1);
	}

	SPI_Cmd(SPI1, DISABLE);
	return 0;
}

int writetospi(uint16_t headerLength,
				const uint8_t *headerBuffer,
				uint32_t bodylength,
				const uint8_t *bodyBuffer) {

	volatile uint8_t hold = *headerBuffer;

	SPI_Cmd(SPI1, ENABLE);
	setup_dma_write(headerLength, headerBuffer);
	spi_transfer();

	setup_dma_write(bodylength, bodyBuffer);
	spi_transfer();

	SPI_Cmd(SPI1, DISABLE);
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

/*
void port_SPIx_clear_chip_select () {

}

void port_SPIx_set_chip_select () {


}

void setup_DW1000RSTnIRQ () {


} */


void decamutexoff (decaIrqStatus_t s) {
	if(s) {
		dw1000_interrupt_enable();
		dw1000_irq_onoff = 1;
	}
}
