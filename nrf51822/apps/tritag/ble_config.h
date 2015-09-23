#ifndef __BLE_CONFIG_H
#define __BLE_CONFIG_H

#include "app_timer.h"

#define PHYSWEB_URL "goo.gl/XMRl3M"

#define APP_COMPANY_IDENTIFIER			0x11BB
#define MANUFACTURER_NAME 				"Lab11UMich"
#define MODEL_NUMBER 					DEVICE_NAME
#define HARDWARE_REVISION 				"A"
#define FIRMWARE_REVISION 				"0.1"


#define UPDATE_RATE                     APP_TIMER_TICKS(1000, 0)

//RTC1_Prescale
#define APP_TIMER_PRESCALER             0

#define APP_TIMER_MAX_TIMERS            6

//size of op queues
#define APP_TIMER_OP_QUEUE_SIZE         5





typedef struct ble_app_s {
    // uint16_t                     revision;           /**< Handle of DFU Service (as provided by the S110 SoftDevice). */
    uint16_t                     service_handle;        /**< Handle of DFU Service (as provided by the S110 SoftDevice). */
    ble_gatts_char_handles_t     char_location_handle; /**< Handles related to the DFU Packet characteristic. */
    ble_gatts_char_handles_t     char_range_handle; /**< Handles related to the DFU Packet characteristic. */
    ble_gatts_char_handles_t     char_ranging_enable_handles; /**< Handles related to the DFU Packet characteristic. */
    ble_gatts_char_handles_t     char_calibration_index_handle; /**< Handles related to the DFU Packet characteristic. */
    ble_srv_error_handler_t      error_handler;         /**< Function to be called in case of an error. */
    uint8_t                      current_location[6];    /** Value of num characteristic */
    uint8_t                      app_raw_response_buffer[128]; // Buffer to store raw responses from TriPoint so that it can be sent over BLE
    uint8_t                      app_ranging; // Whether or not the TriPoint module is running and ranging. 1 = yes, 0 = no
    uint8_t                      calibration_index;
} ble_app_t;

#endif
