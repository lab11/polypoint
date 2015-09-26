#ifndef TRITAG_H
#define TRITAG_H

#include "nrf_gpio.h"

#ifndef DEVICE_NAME
#define DEVICE_NAME "tritag"
#endif


#define ADDRESS_FLASH_LOCATION 0x0003fff8


// TRITAG
#define LED_START      17
#define LED_0          17
#define LED_STOP       17

#define I2C_SCL_PIN   28
#define I2C_SDA_PIN   29

#define TRIPOINT_INTERRUPT_PIN 25

#define BATTERY_MONITOR_PIN 1

// NUCLEUM
// #define LED_START      19
// #define LED_0          19
// #define LED_STOP       19

// #define I2C_SCL_PIN   1
// #define I2C_SDA_PIN   7

// #define TRIPOINT_INTERRUPT_PIN 8


#define SER_CONN_ASSERT_LED_PIN     LED_0

#endif
