#include "nrf_drv_twi.h"
#include "sdk_errors.h"
#include "app_util_platform.h"
#include "nrf_drv_config.h"
#include "nrf_drv_gpiote.h"
#include "nrf_delay.h"

#include "boards.h"
#include "led.h"

#include "tripoint_interface.h"

uint8_t response[256];

nrf_drv_twi_t twi_instance = NRF_DRV_TWI_INSTANCE(1);

// Save the callback that we use to signal the main application that we
// received data over I2C.
tripoint_interface_data_cb_f _data_callback = NULL;


void tripoint_interrupt_handler (nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
	// verify interrupt is from tripoint
	if (pin == TRIPOINT_INTERRUPT_PIN) {
		// Ask whats up over I2C
		uint32_t ret;
		uint8_t cmd = TRIPOINT_CMD_READ_INTERRUPT;
		ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, &cmd, 1, false);
		if (ret != NRF_SUCCESS) return;

		// Figure out the length of what we need to receive by
		// checking the first byte of the response.
		uint8_t len = 0;
		ret = nrf_drv_twi_rx(&twi_instance, TRIPOINT_ADDRESS, &len, 1, true);
		if (ret != NRF_SUCCESS) return;

		// Read the rest of the packet
		if (len == 0) {
			// some error?
		} else {
			ret = nrf_drv_twi_rx(&twi_instance, TRIPOINT_ADDRESS, response, len, false);
			if (ret != NRF_SUCCESS) return;
		}

		// Send back the I2C data
		_data_callback(response, len);
	}
}

ret_code_t tripoint_hw_init () {
	nrf_drv_twi_config_t twi_config;
	ret_code_t ret;

	// Initialize the I2C module
	twi_config.sda                = I2C_SDA_PIN;
	twi_config.scl                = I2C_SCL_PIN;
	twi_config.frequency          = NRF_TWI_FREQ_400K;
	twi_config.interrupt_priority = APP_IRQ_PRIORITY_HIGH;

	ret = nrf_drv_twi_init(&twi_instance, &twi_config, NULL);
	if (ret != NRF_SUCCESS) return ret;
	nrf_drv_twi_enable(&twi_instance);

	// Initialize the GPIO interrupt from the device
	ret = nrf_drv_gpiote_init();
	if (ret != NRF_SUCCESS) return ret;

	nrf_drv_gpiote_in_config_t in_config = GPIOTE_CONFIG_IN_SENSE_LOTOHI(true);
	ret = nrf_drv_gpiote_in_init(TRIPOINT_INTERRUPT_PIN, &in_config, tripoint_interrupt_handler);
	if (ret != NRF_SUCCESS) return ret;

	nrf_drv_gpiote_in_event_enable(TRIPOINT_INTERRUPT_PIN, true);
	return NRF_SUCCESS;
}

ret_code_t tripoint_init (tripoint_interface_data_cb_f cb) {
	_data_callback = cb;
	ret_code_t ret;

	// Wait for 500 ms to make sure the tripoint module is ready
	nrf_delay_us(500000);

	// Now try to read the info byte to make sure we have I2C connection
	{
		uint16_t id;
		uint8_t version;
		ret = tripoint_get_info(&id, &version);
		if (ret != NRF_SUCCESS) return ret;
		if (id != TRIPOINT_ID) return NRF_ERROR_INVALID_DATA;
	}

	return NRF_SUCCESS;
}

ret_code_t tripoint_get_info (uint16_t* id, uint8_t* version) {
	uint8_t buf_cmd[1] = {TRIPOINT_CMD_INFO};
	uint8_t buf_resp[3];
	ret_code_t ret;

	// Send outgoing command that indicates we want the device info string
	ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, buf_cmd, 1, false);
	if (ret != NRF_SUCCESS) return ret;

	// Read back the 3 byte payload
	ret = nrf_drv_twi_rx(&twi_instance, TRIPOINT_ADDRESS, buf_resp, 3, false);
	if (ret != NRF_SUCCESS) return ret;

	*id = (buf_resp[0] << 8) | buf_resp[1];
	*version = buf_resp[2];

	return NRF_SUCCESS;
}

ret_code_t tripoint_start_ranging (bool periodic, uint8_t rate) {
	uint8_t buf_cmd[4];
	ret_code_t ret;

	buf_cmd[0] = TRIPOINT_CMD_CONFIG;

	// TAG for now
	buf_cmd[1] = 0;

	// TAG options
	buf_cmd[2] = 0;
	if (periodic) {
		// leave 0
	} else {
		buf_cmd[2] |= 0x2;
	}

	// Use sleep mode on the TAG
	buf_cmd[2] |= 0x08;

	// And rate
	buf_cmd[3] = rate;

	ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, buf_cmd, 4, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}

// Tell the attached TriPoint module to become an anchor.
ret_code_t tripoint_start_anchor (bool is_glossy_master) {
	uint8_t buf_cmd[4];
	ret_code_t ret;

	buf_cmd[0] = TRIPOINT_CMD_CONFIG;

	// Make ANCHOR
	if(is_glossy_master)
		buf_cmd[1] = 0x01 | 0x20;
	else
		buf_cmd[1] = 0x01;

	// // TAG options
	// buf_cmd[2] = 0;
	// if (periodic) {
	// 	// leave 0
	// } else {
	// 	buf_cmd[2] |= 0x2;
	// }

	// // And rate
	// buf_cmd[3] = rate;

	ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, buf_cmd, 2, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}

// Tell the attached TriPoint module that it should enter the calibration
// mode.
ret_code_t tripoint_start_calibration (uint8_t index) {
	uint8_t buf_cmd[4];
	ret_code_t ret;

	buf_cmd[0] = TRIPOINT_CMD_CONFIG;

	// Make TAG in CALIBRATION
	buf_cmd[1] = 0x04;

	// Set the index of the node in calibration
	buf_cmd[2] = index;

	ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, buf_cmd, 3, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}

// Read the TriPoint stored calibration values into a buffer (must be
// at least 18 bytes long).
ret_code_t tripoint_get_calibration (uint8_t* calib_buf) {
	uint8_t buf_cmd[1] = {TRIPOINT_CMD_READ_CALIBRATION};
	ret_code_t ret;

	// Send outgoing command that indicates we want the device info string
	ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, buf_cmd, 1, false);
	if (ret != NRF_SUCCESS) return ret;

	// Read back the 18 bytes of calibration values
	ret = nrf_drv_twi_rx(&twi_instance, TRIPOINT_ADDRESS, calib_buf, 18, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}

// Stop the TriPoint module and put it in sleep mode
ret_code_t tripoint_sleep () {
	uint8_t buf_cmd[1] = {TRIPOINT_CMD_SLEEP};
	ret_code_t ret;

	ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, buf_cmd, 1, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}

// Restart the TriPoint module. This should only be called if the TriPoint was
// once running and then was stopped. If the TriPoint was never configured,
// this won't do anything.
ret_code_t tripoint_resume () {
	uint8_t buf_cmd[1] = {TRIPOINT_CMD_RESUME};
	ret_code_t ret;

	ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, buf_cmd, 1, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}




