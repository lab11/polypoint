#include "timing.h"
#include "stm32f0xx.h"

#define USECOND 1e9
#define MSECOND 1e6

void uDelay(uint32_t u) {
	volatile uint32_t i = 0;
	for(i = 0; i < ((float)SystemCoreClock/(float)USECOND*13)*u; i++) {
	}
}

void mDelay(uint32_t u) {
	volatile uint32_t i = 0;
	for(i = 0; i < (SystemCoreClock/MSECOND*9)*u; i++) {
	}
}

/*
volatile uint32_t counter;

void uDelay(uint32_t u) {
	//set up systick for uSecond

	//enable the handler interrupt and set the priority
	counter = 0;
	NVIC_SetPriority(SysTick_IRQn, 0x0);
	NVIC_EnableIRQ(SysTick_IRQn);
	SysTick_Config(SystemCoreClock/USECOND);

	while(counter < u) {
		asm volatile("nop");
	}

	NVIC_DisableIRQ(SysTick_IRQn);
}

void mDelay(uint32_t u) {
	counter = 0;
	NVIC_SetPriority(SysTick_IRQn, 0x0);
	NVIC_EnableIRQ(SysTick_IRQn);
	SysTick_Config(SystemCoreClock/MSECOND);

	while(counter < u) {
		asm volatile("nop");
	}

	NVIC_DisableIRQ(SysTick_IRQn);

}

void SysTick_Handler(void) {
	counter++;	
}*/
