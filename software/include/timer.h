#ifndef __TIMER_H
#define __TIMER_H

#include "stm32f0xx.h"

// Number of supported timers
#define TIMER_NUMBER 2

typedef struct {
	uint8_t                 index;
	TIM_TypeDef*            tim_ptr;
	NVIC_InitTypeDef        nvic_init;
	TIM_TimeBaseInitTypeDef tim_init;
	uint32_t                timer_clock;
	uint32_t                divider;
} stm_timer_t;

typedef void (*timer_callback)();

// NOTE: These timers are peculiar in that they fire
// immediately then at the periodic interval.

stm_timer_t* timer_init ();
void timer_disable_interrupt (stm_timer_t* t);
void timer_enable_interrupt (stm_timer_t* t);
void timer_start (stm_timer_t* t, uint32_t us_period, timer_callback);
void timer_reset (stm_timer_t* t, uint32_t val_us);
void timer_stop (stm_timer_t* t);


// Only used for interrupt handling
void timer_17_fired ();
void timer_16_fired ();

#endif
