#include <stdio.h>
#include <string.h>

#include "stm32f0xx_i2c_cpal.h"
#include "stm32f0xx_i2c_cpal_hal.h"

#include "board.h"
#include "firmware.h"
#include "host_interface.h"
#include "dw1000.h"
#include "oneway_common.h"
#include "calibration.h"

#define BUFFER_SIZE 128
uint8_t rxBuffer[BUFFER_SIZE];
uint8_t txBuffer[BUFFER_SIZE];


/* CPAL local transfer structures */
CPAL_TransferTypeDef rxStructure;
CPAL_TransferTypeDef txStructure;

// Just pre-set the INFO response packet.
// Last byte is the version. Set to 1 for now
uint8_t INFO_PKT[3] = {0xb0, 0x1a, 1};
// If we are not ready.
uint8_t NULL_PKT[3] = {0xaa, 0xaa, 0};

// Keep track of why we interrupted the host
interrupt_reason_e _interrupt_reason;
uint8_t* _interrupt_buffer;
uint8_t  _interrupt_buffer_len;

extern I2C_TypeDef* CPAL_I2C_DEVICE[];

uint32_t host_interface_init () {
	uint32_t ret;

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

	// Start CPAL communication configuration
	// Initialize local Reception structures
	rxStructure.wNumData = BUFFER_SIZE;   /* Maximum Number of data to be received */
	rxStructure.pbBuffer = rxBuffer;      /* Common Rx buffer for all received data */
	rxStructure.wAddr1 = 0;               /* Not needed */
	rxStructure.wAddr2 = 0;               /* Not needed */

	// Initialize local Transmission structures
	txStructure.wNumData = BUFFER_SIZE;   /* Maximum Number of data to be received */
	txStructure.pbBuffer = txBuffer;      /* Common Rx buffer for all received data */
	txStructure.wAddr1 = (I2C_OWN_ADDRESS << 1); /* The own board address */
	txStructure.wAddr2 = 0;               /* Not needed */

	// Set SYSCLK as I2C clock source
	// RCC_I2CCLKConfig(RCC_I2C1CLK_SYSCLK);
	RCC_I2CCLKConfig(RCC_I2C1CLK_HSI);

	// Configure the device structure
	CPAL_I2C_StructInit(&I2C1_DevStructure);      /* Set all fields to default values */
	I2C1_DevStructure.CPAL_Dev = 0;
	I2C1_DevStructure.CPAL_Direction = CPAL_DIRECTION_TXRX;
	I2C1_DevStructure.CPAL_Mode = CPAL_MODE_SLAVE;
	I2C1_DevStructure.CPAL_State = CPAL_STATE_READY;
	I2C1_DevStructure.wCPAL_Timeout = 6;
	I2C1_DevStructure.wCPAL_Options =  CPAL_OPT_NO_MEM_ADDR | CPAL_OPT_I2C_WAKEUP_STOP;
	// I2C1_DevStructure.wCPAL_Options =  0;
	I2C1_DevStructure.CPAL_ProgModel = CPAL_PROGMODEL_INTERRUPT;
	I2C1_DevStructure.pCPAL_I2C_Struct->I2C_Timing = I2C_TIMING;
	I2C1_DevStructure.pCPAL_I2C_Struct->I2C_OwnAddress1 = (I2C_OWN_ADDRESS << 1);
	I2C1_DevStructure.pCPAL_TransferRx = &rxStructure;
	I2C1_DevStructure.pCPAL_TransferTx = &txStructure;

	// Initialize CPAL device with the selected parameters
	ret = CPAL_I2C_Init(&I2C1_DevStructure);

	// See if this takes care of issues when STM is busy and can't respond
	// right away. It's also possible this was already configured.
	__CPAL_I2C_HAL_DISABLE_NOSTRETCH(0);

	return ret;
}

static void interrupt_host_set () {
	GPIO_WriteBit(INTERRUPT_PORT, INTERRUPT_PIN, Bit_SET);
}

static void interrupt_host_clear () {
	GPIO_WriteBit(INTERRUPT_PORT, INTERRUPT_PIN, Bit_RESET);
}

// Send to the tag the ranges.
void host_interface_notify_ranges (uint8_t* anchor_ids_ranges, uint8_t len) {

	// TODO: this should be in an atomic block

	// Save the relevant state for when the host asks for it
	_interrupt_reason = HOST_IFACE_INTERRUPT_RANGES;
	_interrupt_buffer = anchor_ids_ranges;
	_interrupt_buffer_len = len;

	// Let the host know it should ask
	interrupt_host_set();
}

void host_interface_notify_calibration (uint8_t* calibration_data, uint8_t len) {
	// TODO: this should be in an atomic block

	// Save the relevant state for when the host asks for it
	_interrupt_reason = HOST_IFACE_INTERRUPT_CALIBRATION;
	_interrupt_buffer = calibration_data;
	_interrupt_buffer_len = len;

	// Let the host know it should ask
	interrupt_host_set();
}

// Doesn't block, but waits for an I2C master to initiate a WRITE.
uint32_t host_interface_wait () {
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
uint32_t host_interface_respond (uint8_t length) {
	uint32_t ret;

	if (length > BUFFER_SIZE) {
		return CPAL_FAIL;
	}

	// Setup outgoing data
	txStructure.wNumData = length;
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
void host_interface_rx_fired () {
	uint8_t opcode;

	// First byte of every correct WRITE packet is the opcode of the
	// packet.
	opcode = rxBuffer[0];
	switch (opcode) {

		/**********************************************************************/
		// Configure the TriPoint. This can be called multiple times to change the setup.
		/**********************************************************************/
		case HOST_CMD_CONFIG: {

			// Just go back to waiting for a WRITE after a config message
			host_interface_wait();

			// This packet configures the TriPoint module and
			// is what kicks off using it.
			uint8_t config_main = rxBuffer[1];
			polypoint_application_e my_app;
			dw1000_role_e my_role;

			// Check if this module should be an anchor or tag
			my_role = (config_main & HOST_PKT_CONFIG_MAIN_ANCTAG_MASK) >> HOST_PKT_CONFIG_MAIN_ANCTAG_SHIFT;

			// Check which application we should run
			my_app = (config_main & HOST_PKT_CONFIG_MAIN_APP_MASK) >> HOST_PKT_CONFIG_MAIN_APP_SHIFT;

			// Now that we know what this module is going to be, we can
			// interpret the remainder of the packet.
			if (my_app == APP_ONEWAY) {
				// Run the base normal ranging application

				oneway_config_t oneway_config;
				oneway_config.my_role = my_role;

				if (my_role == TAG) {
					// Save some TAG specific settings
					uint8_t config_tag = rxBuffer[2];
					oneway_config.my_role = TAG;
					oneway_config.report_mode = (config_tag & HOST_PKT_CONFIG_ONEWAY_TAG_RMODE_MASK) >> HOST_PKT_CONFIG_ONEWAY_TAG_RMODE_SHIFT;
					oneway_config.update_mode = (config_tag & HOST_PKT_CONFIG_ONEWAY_TAG_UMODE_MASK) >> HOST_PKT_CONFIG_ONEWAY_TAG_UMODE_SHIFT;
					oneway_config.sleep_mode  = (config_tag & HOST_PKT_CONFIG_ONEWAY_TAG_SLEEP_MASK) >> HOST_PKT_CONFIG_ONEWAY_TAG_SLEEP_SHIFT;
					oneway_config.update_rate = rxBuffer[3];
				}

				// Now that we know how we should operate,
				// call the main tag function to get things rollin'.
				polypoint_configure_app(my_app, &oneway_config);
				polypoint_start();

			} else if (my_app == APP_CALIBRATION) {
				// Run the calibration application to find the TX and RX
				// delays in the node.
				calibration_config_t cal_config;
				cal_config.index = rxBuffer[2];
				polypoint_configure_app(my_app, &cal_config);
				polypoint_start();
			}

			break;
		}


		/**********************************************************************/
		// Tell the TriPoint that it should take a range/location measurement
		/**********************************************************************/
		case HOST_CMD_DO_RANGE:

			// Just need to go back to waiting for the host to write more
			// after getting a sleep command
			host_interface_wait();

			// Tell the application to perform a range
			polypoint_tag_do_range();
			break;

		/**********************************************************************/
		// Put the TriPoint to sleep.
		/**********************************************************************/
		case HOST_CMD_SLEEP:

			// Just need to go back to waiting for the host to write more
			// after getting a sleep command
			host_interface_wait();

			// Tell the application to stop the dw1000 chip
			polypoint_stop();
			break;

		/**********************************************************************/
		// Resume the application.
		/**********************************************************************/
		case HOST_CMD_RESUME:
			// Keep listening for the next command.
			host_interface_wait();

			// And we just have to start the application.
			polypoint_start();
			break;

		/**********************************************************************/
		// These are handled from the interrupt context.
		/**********************************************************************/
		case HOST_CMD_INFO:
		case HOST_CMD_READ_INTERRUPT:
			break;


		default:
			break;
	}


}

// Called after a READ message from the master.
// We don't need to do anything after the master reads from us, except
// to go back to waiting for a WRITE.
void host_interface_tx_fired () {
	host_interface_wait();
}

// Called after timeout
void host_interface_timeout_fired () {
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
	uint8_t opcode;

	// We need to do some of the handling for the I2C here, because if
	// we wait to handle it on the main thread sometimes there is too much
	// delay and the I2C stops working.


	// First byte of every correct WRITE packet is the opcode of the
	// packet.
	opcode = rxBuffer[0];
	switch (opcode) {
		/**********************************************************************/
		// Return the INFO array
		/**********************************************************************/
		case HOST_CMD_INFO:
			// Check what status the main application is in. If it has contacted
			// the DW1000, then it will be ready and we return the correct
			// info string. If it is not ready, we return the null string
			// that says that I2C is working but that we are not ready for
			// prime time yet.
			if (polypoint_ready()) {
				// Info packet is a good way to check that I2C is working.
				memcpy(txBuffer, INFO_PKT, 3);
			} else {
				memcpy(txBuffer, NULL_PKT, 3);
			}
			host_interface_respond(3);
			break;

		/**********************************************************************/
		// Ask the TriPoint why it asserted the interrupt line.
		/**********************************************************************/
		case HOST_CMD_READ_INTERRUPT: {
			// Clear interrupt
			interrupt_host_clear();

			// Prepare a packet to send back to the host
			txBuffer[0] = 1 + _interrupt_buffer_len;
			txBuffer[1] = _interrupt_reason;
			memcpy(txBuffer+2, _interrupt_buffer, _interrupt_buffer_len);
			host_interface_respond(txBuffer[0]+1);

			break;
		}

		/**********************************************************************/
		// All of the following do not require a response and can be handled
		// on the main thread.
		/**********************************************************************/
		case HOST_CMD_CONFIG:
		case HOST_CMD_DO_RANGE:
		case HOST_CMD_SLEEP:
		case HOST_CMD_RESUME:

			// Just go back to waiting for a WRITE after a config message
			host_interface_wait();

			// Handle the rest on the main thread
			break;

		default:
			break;
	}

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
	host_interface_wait();
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
