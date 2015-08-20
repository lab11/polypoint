#include "nrf_drv_twi.h"
#include "sdk_errors.h"
#include "app_util_platform.h"
#include "nrf_drv_config.h"
#include "nrf_drv_gpiote.h"

#include "boards.h"
#include "led.h"

#include "tripoint_interface.h"

nrf_drv_twi_t twi_instance = NRF_DRV_TWI_INSTANCE(1);


void tripoint_i2c_callback (nrf_drv_twi_evt_t* event) {
	uint8_t buf[10];


	led_toggle(LED_0);


	nrf_drv_twi_rx(&twi_instance, TRIPOINT_ADDRESS, buf, 5, false);

}

void tripoint_interrupt_handler (nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
   	// led_toggle(LED_0);

   	tripoint_start_ranging();
}

ret_code_t tripoint_init () {
	nrf_drv_twi_config_t twi_config;
	ret_code_t ret;

	// Initialize the I2C module
	twi_config.sda                = I2C_SDA_PIN;
	twi_config.scl                = I2C_SCL_PIN;
	twi_config.frequency          = NRF_TWI_FREQ_400K;
	twi_config.interrupt_priority = APP_IRQ_PRIORITY_HIGH;

	// ret = nrf_drv_twi_init(&twi_instance, &twi_config, tripoint_i2c_callback);
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

ret_code_t tripoint_get_info (uint16_t* id, uint8_t* version) {
	uint8_t buf_cmd[1] = {TRIPOINT_CMD_INFO};
	uint8_t buf_resp[3];
	ret_code_t ret;

	// Send outgoing command that indicates we want the device info string
	ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, buf_cmd, 1, false);
	if (ret != NRF_SUCCESS) {
		return ret;
	}

	// Read back the 3 byte payload
	ret = nrf_drv_twi_rx(&twi_instance, TRIPOINT_ADDRESS, buf_resp, 3, false);
	if (ret != NRF_SUCCESS) {
		return ret;
	}

	*id = (buf_resp[0] << 8) | buf_resp[1];
	*version = buf_resp[2];

	return NRF_SUCCESS;
}

ret_code_t tripoint_start_ranging () {
	uint8_t buf[1] = {TRIPOINT_CMD_INFO};
	uint8_t buf2[10];
	ret_code_t ret;

	// ret = nrf_drv_twi_rx(&twi_instance, TRIPOINT_ADDRESS, buf, 5, false);
	// ret = nrf_drv_twi_rx(&twi_instance, 0x64, buf, 5, false);
	ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, buf, 1, false);
	if (ret == NRF_ERROR_INTERNAL) {
		led_toggle(LED_0);
	}

	nrf_drv_twi_rx(&twi_instance, TRIPOINT_ADDRESS, buf2, 3, false);

	return ret;
}




