#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000_anchor.h"
#include "dw1000.h"
#include "firmware.h"

dw1000_err_e dw1000_anchor_init () {
	uint8_t eui_array[8];

	// Set the anchor so it only receives data and ack packets
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	// Set the ID and PAN ID for this anchor
	dw1000_read_eui(eui_array);
	dwt_seteui(eui_array);
	dwt_setpanid(POLYPOINT_PANID);

	// Automatically go back to receive
	dwt_setautorxreenable(TRUE);

	// Don't use these
	dwt_setdblrxbuffmode(FALSE);
	dwt_setrxtimeout(FALSE);

	// Don't receive at first
	dwt_rxenable(FALSE);

	return DW1000_NO_ERR;
}

void dw1000_anchor_txcallback (const dwt_callback_data_t *data) {

}

void dw1000_anchor_rxcallback (const dwt_callback_data_t *data) {

}
