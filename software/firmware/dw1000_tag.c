#include "deca_device_api.h"

#include "dw1000.h"
#include "dw1000_tag.h"
#include "firmware.h"


static struct pp_tag_poll pp_tag_poll_pkt = {
	{ // 802.15.4 HEADER
		{
			0x41, // FCF[0]: data frame, panid compression
			0xC8  // FCF[1]: ext source address, compressed destination
		},
		0,        // Sequence number
		{
			POLYPOINT_PANID & 0xFF, // PAN ID
			POLYPOINT_PANID >> 8
		},
		{
			0xFF, // Destination address: broadcast
			0xFF
		},
		{ 0 }     // Source (blank for now)
	},
	// PACKET BODY
	MSG_TYPE_PP_ONEWAY_TAG_POLL,  // Message type
	0,                            // Round number
	0                             // Sub Sequence number
};



void dw1000_tag_init (dw1000_callback cb) {

	uint8_t eui_array[8];

	// Allow data and ack frames
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	// Set this node's ID and the PAN ID for our DW1000 ranging system
	dw1000_read_eui(eui_array);
	dwt_seteui(eui_array);
	dwt_setpanid(POLYPOINT_PANID);

	// Setup parameters of how the radio should work
	dwt_setautorxreenable(TRUE);
	dwt_setdblrxbuffmode(TRUE);
	dwt_enableautoack(DW1000_ACK_RESPONSE_TIME);

	// Configure sleep
	{
		int mode = DWT_LOADUCODE    |
		           DWT_PRESRV_SLEEP |
		           DWT_CONFIG       |
		           DWT_TANDV;
		if (dwt_getldotune() != 0) {
			// If we need to use LDO tune value from OTP kick it after sleep
			mode |= DWT_LOADLDO;
		}

		// NOTE: on the EVK1000 the DEEPSLEEP is not actually putting the
		// DW1000 into full DEEPSLEEP mode as XTAL is kept on
		dwt_configuresleep(mode, DWT_WAKE_CS | DWT_SLP_EN);
	}

	// Put source EUI in the pp_tag_poll packet
	dw1000_read_eui(pp_tag_poll_pkt.header.sourceAddr);
}


