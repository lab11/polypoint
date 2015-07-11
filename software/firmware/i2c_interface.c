
#include "stm32f0xx_i2c_cpal.h"

#include "board.h"

#define BUFFER_SIZE 128
uint8_t rxBuffer[BUFFER_SIZE];
uint8_t txBuffer[BUFFER_SIZE];



/* CPAL local transfer structures */
CPAL_TransferTypeDef rxStructure;
CPAL_TransferTypeDef txStructure;


uint32_t i2c_interface_init () {

	// Enabled the Interrupt pin
	GPIO_InitTypeDef  GPIO_InitStructure;
	RCC_AHBPeriphClockCmd(INTERRUPT_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = INTERRUPT_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(INTERRUPT_PORT, &GPIO_InitStructure);
	INTERRUPT_PORT->BRR = INTERRUPT_PIN; // clear


	/* Start CPAL communication configuration ***********************************/
	/* Initialize local Reception structures */
	rxStructure.wNumData = BUFFER_SIZE;   /* Maximum Number of data to be received */
	rxStructure.pbBuffer = rxBuffer;      /* Common Rx buffer for all received data */
	rxStructure.wAddr1 = 0;               /* Not needed */
	rxStructure.wAddr2 = 0;               /* Not needed */

	/* Initialize local Transmission structures */
	txStructure.wNumData = BUFFER_SIZE;   /* Maximum Number of data to be received */
	txStructure.pbBuffer = txBuffer;      /* Common Rx buffer for all received data */
	txStructure.wAddr1 = I2C_OWN_ADDRESS; /* The own board address */
	txStructure.wAddr2 = 0;               /* Not needed */

	/* Set SYSCLK as I2C clock source */
	RCC_I2CCLKConfig(RCC_I2C1CLK_SYSCLK);

	/* Configure the device structure */
	CPAL_I2C_StructInit(&I2C1_DevStructure);      /* Set all fields to default values */
	I2C1_DevStructure.CPAL_Dev = 0;
	I2C1_DevStructure.CPAL_Mode = CPAL_MODE_SLAVE;
	I2C1_DevStructure.wCPAL_Options =  CPAL_OPT_NO_MEM_ADDR | CPAL_OPT_DMATX_TCIT | CPAL_OPT_DMARX_TCIT;
	I2C1_DevStructure.CPAL_ProgModel = CPAL_PROGMODEL_DMA;
	I2C1_DevStructure.pCPAL_I2C_Struct->I2C_Timing = I2C_TIMING;
	I2C1_DevStructure.pCPAL_I2C_Struct->I2C_OwnAddress1 = I2C_OWN_ADDRESS;
	I2C1_DevStructure.pCPAL_TransferRx = &rxStructure;
	I2C1_DevStructure.pCPAL_TransferTx = &txStructure;

	/* Initialize CPAL device with the selected parameters */
	return CPAL_I2C_Init(&I2C1_DevStructure);

}


uint32_t i2c_interface_send (uint16_t address, uint8_t length, uint8_t* buf) {
	uint32_t ret;

	rxStructure.wNumData = BUFFER_SIZE;       /* Maximum Number of data to be received */
	rxStructure.pbBuffer = txBuffer;        /* Common Rx buffer for all received data */

	/* Reconfigure device for slave receiver mode */
	I2C1_DevStructure.CPAL_Mode = CPAL_MODE_SLAVE;
	I2C1_DevStructure.CPAL_State = CPAL_STATE_READY;



	// INTERRUPT_PORT->BSRR = INTERRUPT_PIN; // set

	/* Start waiting for data to be received in slave mode */
	ret = CPAL_I2C_Read(&I2C1_DevStructure);

	INTERRUPT_PORT->BSRR = INTERRUPT_PIN; // set

	return ret;
}


/**
  * @brief  User callback that manages the Timeout error
  * @param  pDevInitStruct
  * @retval None.
  */
uint32_t CPAL_TIMEOUT_UserCallback(CPAL_InitTypeDef* pDevInitStruct)
{

  led_toggle(LED1);
  // /* Update CPAL states */
  // pDevInitStruct->CPAL_State = CPAL_STATE_READY;
  // pDevInitStruct->wCPAL_DevError = CPAL_I2C_ERR_NONE ;
  // pDevInitStruct->wCPAL_Timeout  = CPAL_I2C_TIMEOUT_DEFAULT;

  // /* DeInitialize CPAL device */
  // CPAL_I2C_DeInit(pDevInitStruct);

  //  Initialize CPAL device with the selected parameters
  // CPAL_I2C_Init(pDevInitStruct);

  // /* Switch the LCD write color */
  // Switch_ErrorColor();

  // LCD_DisplayStringLine(Line9, (uint8_t*)"  Timeout Recovered ");

  // ActionState = ACTION_NONE;

  return CPAL_PASS;
}


/**
  * @brief  Manages the End of Rx transfer event.
  * @param  pDevInitStruct
  * @retval None
  */
void CPAL_I2C_RXTC_UserCallback(CPAL_InitTypeDef* pDevInitStruct)
{


  INTERRUPT_PORT->BRR = INTERRUPT_PIN; // clear

  // uint8_t result = 0xFF, i = 0;

  // /* Activate the mode receiver only */
  // RecieverMode = 1;

  // /* Switch the LCD write color */
  // Switch_Color();

  // LCD_DisplayStringLine(Line3, (uint8_t*)"RECEIVER MODE ACTIVE");
  // LCD_DisplayStringLine(Line5, MEASSAGE_EMPTY);
  // LCD_DisplayStringLine(Line9, MEASSAGE_EMPTY);

  // STM_EVAL_LEDOff(LED2);
  // STM_EVAL_LEDToggle(LED3);

  // /* Initialize local Reception structures */
  // sRxStructure.wNumData = BufferSize;       /* Maximum Number of data to be received */
  // sRxStructure.pbBuffer = tRxBuffer;        /* Common Rx buffer for all received data */

  // /* Check the Received Buffer */
  // result = Buffer_Check(tRxBuffer, (uint8_t*)tStateSignal, (uint8_t*)tSignal1,(uint8_t*)tSignal2, (uint16_t)BufferSize);

  // switch(result)
  // {
  //   case 0:
  //     LCD_DisplayStringLine(Line7, (uint8_t*)"  State message OK  ");
  //     break;

  //   case 1:
  //     /* Display Reception Complete */
  //     LCD_DisplayStringLine(Line5, (uint8_t*)" Signal1 message OK ");
  //     break;

  //   case 2:
  //     /* Display Reception Complete */
  //     LCD_DisplayStringLine(Line5, (uint8_t*)" Signal2 message OK ");
  //     break;

  //   default:
  //     LCD_DisplayStringLine(Line7, (uint8_t*)"       Failure     ");
  //     break;
  // }

  // /* Reinitialize RXBuffer */
  // for(i = 0; i < MAX_BUFF_SIZE; i++)
  // {
  //   tRxBuffer[i]=0;
  // }
}
