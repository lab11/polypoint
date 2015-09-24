#include <string.h>

#include "stm32f0xx.h"

#include "timer.h"
#include "firmware.h"


static uint8_t used_timers = 0;

static timer_callback timer_callbacks[TIMER_NUMBER];

// Predefine all of the timer structures
stm_timer_t timers[TIMER_NUMBER] = {
	{
		0, // index
		TIM17,
		{ // NVIC Init
			TIM17_IRQn, // Channel
			0x01,       // Priority
			ENABLE      // Enable or disable
		},
		{ // TIM init
			0,                  // Prescalar
			TIM_CounterMode_Up, // Counter Mode
			0,                  // Period
			TIM_CKD_DIV1,       // ClockDivision
			0                   // Repetition Counter
		},
		RCC_APB2Periph_TIM17
	},
	{
		1, // index
		TIM16,
		{ // NVIC Init
			TIM16_IRQn, // Channel
			0x01,       // Priority
			ENABLE      // Enable or disable
		},
		{ // TIM init
			0,                  // Prescalar
			TIM_CounterMode_Up, // Counter Mode
			0,                  // Period
			TIM_CKD_DIV1,       // ClockDivision
			0                   // Repetition Counter
		},
		RCC_APB2Periph_TIM16
	}
};

/******************************************************************************/
// API Functions
/******************************************************************************/

// Give the caller a pointer to a valid timer configuration struct.
stm_timer_t* timer_init () {
	if (used_timers >= TIMER_NUMBER) {
		return NULL;
	}
	used_timers++;
	return &timers[used_timers-1];
}

// Start a particular timer running
void timer_start (stm_timer_t* t, uint32_t us_period, timer_callback cb) {
	uint32_t prescalar = (SystemCoreClock/500000)-1;
	// Save the callback
	timer_callbacks[t->index] = cb;

	// Enable the clock
	RCC_APB2PeriphClockCmd(t->timer_clock , ENABLE);

	// Setup the interrupt
	t->nvic_init.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&t->nvic_init);

	// Need this to fit in 16 bits
	while (us_period > 65535) {
		us_period = us_period >> 1;
		prescalar = prescalar << 1;
	}

	// Setup the actual timer
	t->tim_init.TIM_Period    = us_period;
	t->tim_init.TIM_Prescaler = prescalar;
	TIM_TimeBaseInit(t->tim_ptr, &t->tim_init);

	// Enable the interrupt
	TIM_ITConfig(t->tim_ptr, TIM_IT_Update , ENABLE);
	TIM_ClearITPendingBit(t->tim_ptr, TIM_IT_Update);

	// Enable the timer
	TIM_Cmd(t->tim_ptr, ENABLE);
}

// Disable everything that timer_start enabled
void timer_stop (stm_timer_t* t) {
	// Disable the timer
	TIM_Cmd(t->tim_ptr, DISABLE);

	// Disable the interrupt
	TIM_ITConfig(t->tim_ptr, TIM_IT_Update , DISABLE);

	// Disable the interrupt channel
	t->nvic_init.NVIC_IRQChannelCmd = DISABLE;
	NVIC_Init(&t->nvic_init);

	// Disable the clock
	RCC_APB2PeriphClockCmd(t->timer_clock , DISABLE);

	// Remove the callback
	timer_callbacks[t->index] = NULL;
}

typedef void (*timer_timeout_f) (void);

void timer_timeout_set (uint8_t ms) {

}

/******************************************************************************/
// Interrupt handling
/******************************************************************************/

// Call the timer callback from main thread context
void timer_17_fired () {
	if (timer_callbacks[0] != NULL) {
		timer_callbacks[0]();
	}
}

// Call the timer callback from main thread context
void timer_16_fired () {
	if (timer_callbacks[1] != NULL) {
		timer_callbacks[1]();
	}
}

// Raw interrupt handlers from vector table
void TIM17_IRQHandler(void) {
	if (TIM_GetITStatus(TIM17, TIM_IT_Update) != RESET) {

		// Notify main loop that we got a timer interrupt
		// We save the index of the callback in our interrupt slot
		// so that when the main thread gets back to us we know what
		// to call.
		mark_interrupt(INTERRUPT_TIMER_17);

		// Clear Timer interrupt pending bit
		TIM_ClearITPendingBit(TIM17, TIM_IT_Update);
	}
}

void TIM16_IRQHandler(void) {
	if (TIM_GetITStatus(TIM16, TIM_IT_Update) != RESET) {

		// Notify main loop that we got a timer interrupt
		// We save the index of the callback in our interrupt slot
		// so that when the main thread gets back to us we know what
		// to call.
		mark_interrupt(INTERRUPT_TIMER_16);

		// Clear Timer interrupt pending bit
		TIM_ClearITPendingBit(TIM16, TIM_IT_Update);
	}
}

void TIM15_IRQHandler(void) {

}
