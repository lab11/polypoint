/* Blink
 */

#include <stdbool.h>
#include <stdint.h>
#include "led.h"
#include "boards.h"
#include "nordic_common.h"
#include "app_timer.h"
#include "softdevice_handler.h"


#define LED            LED_0

// Some constants about timers
#define BLINK_TIMER_PRESCALER              0                                  /**< Value of the RTC1 PRESCALER register. */
#define BLINK_TIMER_MAX_TIMERS             4                                  /**< Maximum number of simultaneously created timers. */
#define BLINK_TIMER_OP_QUEUE_SIZE          4                                  /**< Size of timer operation queues. */

// How long before the timer fires.
#define BLINK_RATE     APP_TIMER_TICKS(500, BLINK_TIMER_PRESCALER) // Blink every 0.5 seconds

// Timer data structure
static app_timer_id_t  blink_timer;                        /**< Battery timer. */

void app_error_handler(uint32_t error_code,
                       uint32_t line_num,
                       const uint8_t * p_file_name) {
    NVIC_SystemReset();
}

void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name) {
    app_error_handler(0x11, line_num, p_file_name);
}

// Timer callback
static void timer_handler (void* p_context) {
    led_toggle(LED);
}

// Setup timer
static void timer_init(void)
{
    uint32_t err_code;

    // Initialize timer module.
    APP_TIMER_INIT(BLINK_TIMER_PRESCALER,
                   BLINK_TIMER_MAX_TIMERS,
                   BLINK_TIMER_OP_QUEUE_SIZE,
                   false);

    // Create a timer
    err_code = app_timer_create(&blink_timer,
                                APP_TIMER_MODE_REPEATED,
                                timer_handler);
    APP_ERROR_CHECK(err_code);
}

// Start the blink timer
static void timer_start(void) {
    uint32_t err_code;

    // Start application timers.
    err_code = app_timer_start(blink_timer, BLINK_RATE, NULL);
    APP_ERROR_CHECK(err_code);
}

int main(void) {

    // Initialize.
    led_init(LED);
    led_off(LED);

    // Need to set the clock to something
    SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_RC_250_PPM_8000MS_CALIBRATION, false);

    timer_init();
    timer_start();

    // Enter main loop.
    while (1) {
        sd_app_evt_wait();
    }
}
