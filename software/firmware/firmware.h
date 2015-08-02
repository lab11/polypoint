#ifndef __FIRMWARE_H
#define __FIRMWARE_H

#define TRUE  1
#define FALSE 0

typedef uint8_t bool;

// All of the possible interrupt sources.
typedef enum {
	TIMER_17,
	TIMER_16,
	NUMBER_INTERRUPT_SOURCES
} interrupt_source_e;

void mark_interrupt (interrupt_source_e src);

#endif
