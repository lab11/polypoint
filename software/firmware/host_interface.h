#ifndef __HOST_INTERFACE_H
#define __HOST_INTERFACE_H

// List of command byte opcodes for messages from the I2C master to us
#define HOST_CMD_INFO             0x01
#define HOST_CMD_CONFIG           0x02
#define HOST_CMD_READ_INTERRUPT   0x03
#define HOST_CMD_DO_RANGE         0x04
#define HOST_CMD_SLEEP            0x05
#define HOST_CMD_RESUME           0x06
#define HOST_CMD_SET_LOCATION     0x07
#define HOST_CMD_READ_CALIBRATION 0x08


// Structs for parsing the messages for each command
#define HOST_PKT_CONFIG_MAIN_ANCTAG_MASK    0x03
#define HOST_PKT_CONFIG_MAIN_ANCTAG_SHIFT   0
#define HOST_PKT_CONFIG_MAIN_APP_MASK       0x1C
#define HOST_PKT_CONFIG_MAIN_APP_SHIFT      2
#define HOST_PKT_CONFIG_MAIN_GLOSSY_MASK    0x20
#define HOST_PKT_CONFIG_MAIN_GLOSSY_SHIFT   5

#define HOST_PKT_CONFIG_ONEWAY_TAG_RMODE_MASK   0x01
#define HOST_PKT_CONFIG_ONEWAY_TAG_RMODE_SHIFT  0
#define HOST_PKT_CONFIG_ONEWAY_TAG_UMODE_MASK   0x06
#define HOST_PKT_CONFIG_ONEWAY_TAG_UMODE_SHIFT  1
#define HOST_PKT_CONFIG_ONEWAY_TAG_SLEEP_MASK   0x08
#define HOST_PKT_CONFIG_ONEWAY_TAG_SLEEP_SHIFT  3

// Defines for identifying data sent to host
typedef enum {
	HOST_IFACE_INTERRUPT_RANGES = 0x01,
	HOST_IFACE_INTERRUPT_CALIBRATION = 0x02,
} interrupt_reason_e;


uint32_t host_interface_init();
uint32_t host_interface_wait ();
uint32_t host_interface_respond (uint8_t length);
void host_interface_notify_ranges (uint8_t* anchor_ids_ranges, uint8_t len);
void host_interface_notify_calibration (uint8_t* calibration_data, uint8_t len);


// Interrupt callbacks
void host_interface_rx_fired ();
void host_interface_tx_fired ();
void host_interface_timeout_fired ();

#endif
