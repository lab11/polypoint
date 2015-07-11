#ifndef __I2C_INTERFACE_H
#define __I2C_INTERFACE_H

uint32_t i2c_interface_init();
uint32_t i2c_interface_send (uint16_t address, uint8_t length, uint8_t* buf);

#endif
