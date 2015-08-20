#include <stdio.h>
#include <string.h>

#include "stm32f0xx_i2c_cpal.h"

#include "board.h"
#include "firmware.h"
#include "i2c_interface.h"
#include "dw1000.h"
#include "operation_api.h"

#define BUFFER_SIZE 128
uint8_t rxBuffer[BUFFER_SIZE];
uint8_t txBuffer[BUFFER_SIZE];


// Save a callback to use when data comes in from i2c for the main application
static i2c_interface_callback callback;


/* CPAL local transfer structures */
CPAL_TransferTypeDef rxStructure;
CPAL_TransferTypeDef txStructure;

// Just pre-set the INFO response packet.
// Last byte is the version. Set to 1 for now
uint8_t INFO_PKT[3] = {0xb0, 0x1a, 1};


uint32_t i2c_interface_init (i2c_interface_callback cb) {

	// Save callback
	callback = cb;

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
	txStructure.wAddr1 = (I2C_OWN_ADDRESS << 1); /* The own board address */
	txStructure.wAddr2 = 0;               /* Not needed */

	/* Set SYSCLK as I2C clock source */
	RCC_I2CCLKConfig(RCC_I2C1CLK_SYSCLK);

	/* Configure the device structure */
	CPAL_I2C_StructInit(&I2C1_DevStructure);      /* Set all fields to default values */
	I2C1_DevStructure.CPAL_Dev = 0;
	I2C1_DevStructure.CPAL_Direction = CPAL_DIRECTION_TXRX;
	I2C1_DevStructure.CPAL_Mode = CPAL_MODE_SLAVE;
	I2C1_DevStructure.CPAL_State = CPAL_STATE_READY;
	I2C1_DevStructure.wCPAL_Timeout = 6;
	I2C1_DevStructure.wCPAL_Options =  CPAL_OPT_NO_MEM_ADDR | CPAL_OPT_DMATX_TCIT | CPAL_OPT_DMARX_TCIT;
	// I2C1_DevStructure.wCPAL_Options =  0;
	I2C1_DevStructure.CPAL_ProgModel = CPAL_PROGMODEL_INTERRUPT;
	I2C1_DevStructure.pCPAL_I2C_Struct->I2C_Timing = I2C_TIMING;
	I2C1_DevStructure.pCPAL_I2C_Struct->I2C_OwnAddress1 = (I2C_OWN_ADDRESS << 1);
	I2C1_DevStructure.pCPAL_TransferRx = &rxStructure;
	I2C1_DevStructure.pCPAL_TransferTx = &txStructure;

	/* Initialize CPAL device with the selected parameters */
	return CPAL_I2C_Init(&I2C1_DevStructure);

}

// Doesn't block, but waits for an I2C master to initiate a WRITE.
uint32_t i2c_interface_wait () {
	uint32_t ret;

	// Setup the buffer to receive the contents of the WRITE in
	rxStructure.wNumData = BUFFER_SIZE;     // Maximum Number of data to be received
	rxStructure.pbBuffer = rxBuffer;        // Common Rx buffer for all received data

	// Device is ready, not clear if this is needed
	I2C1_DevStructure.CPAL_State = CPAL_STATE_READY;

	// Now wait for something to happen in slave mode.
	// Start waiting for data to be received in slave mode.
	ret = CPAL_I2C_Read(&I2C1_DevStructure);

	return ret;
}

// Wait for a READ from the master. Setup the buffers
uint32_t i2c_interface_respond (uint8_t length, uint8_t* buf) {
	uint32_t ret;

	if (length > BUFFER_SIZE) {
		return CPAL_FAIL;
	}

	// Setup outgoing data
	txStructure.wNumData = length;
	memcpy(txBuffer, buf, length);
	txStructure.pbBuffer = txBuffer;

	// Device is ready, not clear if this is needed
	I2C1_DevStructure.CPAL_State = CPAL_STATE_READY;

	// Now wait for something to happen in slave mode.
	// Start waiting for data to be received in slave mode.
	ret = CPAL_I2C_Write(&I2C1_DevStructure);

	return ret;
}


// Called when the I2C interface receives a WRITE message on the bus.
// Based on what was received, either act or setup a response
void i2c_interface_rx_fired () {
	uint8_t opcode;

	// First byte of every correct WRITE packet is the opcode of the
	// packet.
	opcode = rxBuffer[0];
	switch (opcode) {
		case I2C_CMD_INFO:
			// Info packet is a good way to check that I2C is working.
			i2c_interface_respond(3, INFO_PKT);
			break;

		case I2C_CMD_CONFIG: {

			// Just go back to waiting for a WRITE after a config message
			i2c_interface_wait();

			// This packet configures the TriPoint module and
			// is what kicks off using it.
			uint8_t config_main = rxBuffer[1];
			dw1000_role_e my_role;

			// Check if this module should be an anchor or tag
			if ((config_main & I2C_PKT_CONFIG_MAIN_ANCTAG_MASK) == I2C_PKT_CONFIG_MAIN_ANCTAG_ANCHOR) {
				my_role = ANCHOR;
			} else {
				my_role = TAG;
			}

			// Now that we know what this module is going to be, we can
			// interpret the remainder of the packet.
			if (my_role == TAG) {
				uint8_t config_tag = rxBuffer[2];
				uint8_t config_tur = rxBuffer[3];
				dw1000_report_mode_e report_mode;
				dw1000_update_mode_e update_mode;

				report_mode = (config_tag & I2C_PKT_CONFIG_TAG_RMODE_MASK) >> I2C_PKT_CONFIG_TAG_RMODE_SHIFT;
				update_mode = (config_tag & I2C_PKT_CONFIG_TAG_UMODE_MASK) >> I2C_PKT_CONFIG_TAG_UMODE_SHIFT;

				// Now that we know how we should operate,
				// call the main tag function to get things rollin'.
				run_tag(report_mode, update_mode, config_tur);

			} else if (my_role == ANCHOR) {
				// TODO: setup this node as an anchor
			}
			break;
		}
		default:
			break;
	}


}

// Called after a READ message from the master.
// We don't need to do anything after the master reads from us, except
// to go back to waiting for a WRITE.
void i2c_interface_tx_fired () {
	i2c_interface_wait();
}

// Called after timeout
void i2c_interface_timeout_fired () {
}


/**
  * @brief  User callback that manages the Timeout error
  * @param  pDevInitStruct
  * @retval None.
  */
uint32_t CPAL_TIMEOUT_UserCallback(CPAL_InitTypeDef* pDevInitStruct) {
	// Handle this interrupt on the main thread
	mark_interrupt(INTERRUPT_I2C_TIMEOUT);

  return CPAL_PASS;
}


/**
  * @brief  Manages the End of Rx transfer event.
  * @param  pDevInitStruct
  * @retval None
  */
void CPAL_I2C_RXTC_UserCallback(CPAL_InitTypeDef* pDevInitStruct) {
	// Handle this interrupt on the main thread
	mark_interrupt(INTERRUPT_I2C_RX);
}

/**
  * @brief  Manages the End of Tx transfer event.
  * @param  pDevInitStruct
  * @retval None
  */
void CPAL_I2C_TXTC_UserCallback(CPAL_InitTypeDef* pDevInitStruct) {
	mark_interrupt(INTERRUPT_I2C_TX);
}

/**
  * @brief  User callback that manages the I2C device errors.
  * @note   Make sure that the define USE_SINGLE_ERROR_CALLBACK is uncommented in
  *         the cpal_conf.h file, otherwise this callback will not be functional.
  * @param  pDevInitStruct.
  * @param  DeviceError.
  * @retval None
  */
void CPAL_I2C_ERR_UserCallback(CPAL_DevTypeDef pDevInstance, uint32_t DeviceError) {


}
