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
	NUMBER_INTERRUPT_SOURCES
} interrupt_source_e;

void mark_interrupt (interrupt_source_e src);

#endif
