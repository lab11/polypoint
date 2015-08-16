#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/*
 * @author: Pat Pannnuto <ppannuto@umich.edu>
 */

#define NETSTACK_CONF_RDC     nullrdc_driver
#define NETSTACK_CONF_MAC     nullmac_driver

// Set the RF channel and panid
#define IEEE802154_CONF_PANID 0x0022
#define CC2538_RF_CONF_CHANNEL 24

// No need for UART
// #define STARTUP_CONF_VERBOSE 0
// #define UART_CONF_ENABLE 0

#define WATCHDOG_CONF_ENABLE 1
#define WATCHDOG_ATUM_BYPASS 1
#define LPM_CONF_ENABLE 0

#endif /* PROJECT_CONF_H_ */

/** @} */
