#include "delay.h"
#include "stm32f0xx.h"

#define USECOND 1e9
#define MSECOND 1e6

void uDelay(uint32_t u) {
	volatile uint32_t i = 0;
	for (i = 0; i < ((float)SystemCoreClock/(float)USECOND*13)*u; i++) { }
}

void mDelay(uint32_t m) {
	volatile uint32_t i = 0;
	for (i = 0; i < (SystemCoreClock/MSECOND*9)*m; i++) { }
}
