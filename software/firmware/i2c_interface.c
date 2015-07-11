
#include "stm32f0xx_i2c_cpal.h"

#include "board.h"

#define BUFFER_SIZE 128
uint8_t rxBuffer[BUFFER_SIZE];
uint8_t txBuffer[BUFFER_SIZE];



/* CPAL local transfer structures */
CPAL_TransferTypeDef rxStructure;
CPAL_TransferTypeDef txStructure;


uint32_t i2c_interface_init () {

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

	txStructure.wNumData = length;
	txStructure.wAddr1   = address;
	txStructure.wAddr2   = 0;
	txStructure.pbBuffer = buf;

	I2C1_DevStructure.CPAL_Mode = CPAL_MODE_MASTER;
	I2C1_DevStructure.CPAL_State = CPAL_STATE_READY;

	/* Start writing data in master mode */
	return CPAL_I2C_Write(&I2C1_DevStructure);
}
