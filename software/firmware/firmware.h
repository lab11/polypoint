#ifndef __FIRMWARE_H
#define __FIRMWARE_H

#define TRUE  1
#define FALSE 0

typedef uint8_t bool;

// Convenience functions
#define MIN(_a, _b) ((_a < _b) ? (_a) : (_b))
#define MAX(_a, _b) ((_a > _b) ? (_a) : (_b))

// All of the possible interrupt sources.
typedef enum {
	INTERRUPT_TIMER_17,
	INTERRUPT_TIMER_16,
	INTERRUPT_DW1000,
	INTERRUPT_I2C_RX,
	INTERRUPT_I2C_TIMEOUT,
	NUMBER_INTERRUPT_SOURCES
} interrupt_source_e;

void mark_interrupt (interrupt_source_e src);

/******************************************************************************/
// I2C for the application
/******************************************************************************/
#define I2C_OWN_ADDRESS         0x65

// Identification byte that we return to an interested client. This is useful
// for initializing and debugging to make sure that the I2C connection is
// working
#define INFO_BYTE_0 0xB0
#define INFO_BYTE_1 0x1A

#endif
