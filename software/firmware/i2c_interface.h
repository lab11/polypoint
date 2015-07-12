#ifndef __I2C_INTERFACE_H
#define __I2C_INTERFACE_H

typedef void (*i2c_interface_callback)(uint8_t opcode, uint8_t* buf);


uint32_t i2c_interface_init(i2c_interface_callback cb);
uint32_t i2c_interface_listen ();
uint32_t i2c_interface_send (uint16_t address, uint8_t length, uint8_t* buf);

#endif
