#include "nrf_drv_twi.h"
#include "sdk_errors.h"
#include "app_util_platform.h"

#include "boards.h"

#include "tripoint_interface.h"

nrf_drv_twi_t twi_instance;


void tripoint_i2c_callback (nrf_drv_twi_evt_t* event) {

}

ret_code_t tripoint_init () {
	nrf_drv_twi_config_t twi_config;
	ret_code_t ret;

	// Initialize the I2C module
	twi_config.sda                = I2C_SDA_PIN;
	twi_config.scl                = I2C_SCL_PIN;
	twi_config.frequency          = NRF_TWI_FREQ_400K;
	twi_config.interrupt_priority = APP_IRQ_PRIORITY_HIGH;

	ret = nrf_drv_twi_init(&twi_instance, &twi_config, tripoint_i2c_callback);
	if (ret != NRF_SUCCESS) return ret;
	nrf_drv_twi_enable(&twi_instance);

	return NRF_SUCCESS;
}

ret_code_t tripoint_start_ranging () {
	uint8_t buf[1] = {TRIPOINT_CMD_START_RANGING};
	ret_code_t ret;

	ret = nrf_drv_twi_tx(&twi_instance, TRIPOINT_ADDRESS, buf, 1, false);
	return ret;
}




