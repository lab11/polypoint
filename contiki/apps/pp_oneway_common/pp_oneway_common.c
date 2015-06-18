#include "contiki.h"
#include "sys/rtimer.h"
#include "dev/leds.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "dw1000.h"
#include "dev/ssi.h"
#include "cpu/cc2538/lpm.h"
#include "cpu/cc2538/dev/sys-ctrl.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "pp_oneway_common.h"

static const uint8_t xtaltrim[1+NUM_ANCHORS] = {
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8
};

const uint8_t pgDelay[8] = {
	0x0,
	0xc9,
	0xc2,
	0xc5,
	0x95,
	0xc0,
	0x0,
	0x93
};

//NOTE: THIS IS DEPENDENT ON BAUDRATE
const uint32_t txPower[8] = {
	0x0,
	0x07274767UL,
	0x07274767UL,
	0x2B4B6B8BUL,
	0x3A5A7A9AUL,
	0x25456585UL,
	0x0,
	0x5171B1D1UL
};

const double txDelayCal[(1+NUM_ANCHORS)*3] = {
	//    -0.13, 0.36, -0.05,//T2
	//    -0.14, 0.37, -0.03,//A1
	//    -0.20, 0.39, -0.06,//A2
	//    -0.07, 0.43, 0.04,//A3
	//    -0.15, 0.38, -0.01,//A4
	//    -0.03, 0.49, 0.07,//A5
	//    -0.18, 0.45, -0.07,//A6
	//    -0.08, 0.42, -0.00,//A7
	//    0.04, 0.51, 0.07,//A8
	//    -0.15, 0.38, -0.01,//A9
	//    -0.12, 0.44, 0.01,//A10
	0.0, 0.0, 0.0, //dummy for now (EUI = 0)
	155.5433333333333,   155.0400000000000,   155.4433333333333,
	155.5766666666667,   154.9922222222222,   155.4500000000000,
	155.4855555555555,   154.9122222222222,   155.3466666666667,
	155.5000000000000,   154.9377777777778,   155.3444444444445,
	155.3955555555556,   154.8555555555555,   155.2922222222222,
	155.4966666666667,   154.9100000000000,   155.3755555555556,
	155.4133333333333,   154.9233333333333,   155.3422222222222,
	155.3266666666667,   154.8377777777778,   155.2800000000000,
	155.5188888888889,   154.9277777777778,   155.3722222222222,
	155.4877777777778,   154.9033333333333,   155.3533333333333,
	155.2920,            154.6010,            155.1730,
	155.3400,            154.8600,            155.2380,
	155.3930,            154.9030,            155.3220,
	155.3850,            154.9120,            155.3040,
	155.3970,            154.8170,            155.2610,
	155.3220,            154.8350,            155.2600,
	155.3800,            154.8780,            155.3260,
	155.3520,            154.8610,            155.2910,
};

////////////////////////////////////////////////////////////////////////////////

static dwt_config_t   global_ranging_config;
static dwt_txconfig_t global_tx_config;

uint8_t subseq_num_to_chan(uint8_t subseq_num, bool return_channel_index){
	uint8_t mod_choice = ((subseq_num/NUM_ANTENNAS/NUM_ANTENNAS) % NUM_CHANNELS);
	uint8_t return_choice = (mod_choice == 0) ? 1 :
		(mod_choice == 1) ? 4 : 3;
	if(return_channel_index)
		return mod_choice;
	else
		return return_choice;
}

static uint8_t subseq_num_to_tag_sel(uint8_t subseq_num){
#ifdef DW_CAL_TRX_DELAY
	return 0;
#else
	return (subseq_num/NUM_ANTENNAS) % NUM_ANTENNAS;
#endif
}

uint8_t subseq_num_to_anchor_sel(uint8_t subseq_num){
#ifdef DW_CAL_TRX_DELAY
	return 0;
#else
	return subseq_num % NUM_ANTENNAS;
#endif
}


int app_dw1000_init (
		int HACK_role,
		int HACK_EUI,
		void (*txcallback)(const dwt_callback_data_t *),
		void (*rxcallback)(const dwt_callback_data_t *)
		) {
	uint32_t devID;
	int err;

	// Start off DW1000 comms slow
	REG(SSI0_BASE + SSI_CR1) = 0;
	REG(SSI0_BASE + SSI_CPSR) = 8;
	REG(SSI0_BASE + SSI_CR1) |= SSI_CR1_SSE;

	// Reset the DW1000...for some reason
	dw1000_reset();

	// Make sure we can talk to the DW1000
	devID = dwt_readdevid();
	if (devID != DWT_DEVICE_ID) {
#ifdef DW_DEBUG
		printf("Could not read Device ID from the DW1000\r\n");
		printf("Possible the chip is asleep...\r\n");
#endif
		return -1;
	}

	// Select which of the three antennas on the board to use
	dw1000_choose_antenna(0);

	// Init the dw1000 hardware
	err = dwt_initialise(DWT_LOADUCODE    |
			DWT_LOADLDO      |
			DWT_LOADTXCONFIG |
			DWT_LOADXTALTRIM);
	if (err != DWT_SUCCESS) {
		return -1;
	}

	// Setup interrupts
	/*
	 * The following events can be enabled:
	 * DWT_INT_TFRS         0x00000080          // frame sent
	 * DWT_INT_RFCG         0x00004000          // frame received with good CRC
	 * DWT_INT_RPHE         0x00001000          // receiver PHY header error
	 * DWT_INT_RFCE         0x00008000          // receiver CRC error
	 * DWT_INT_RFSL         0x00010000          // receiver sync loss error
	 * DWT_INT_RFTO         0x00020000          // frame wait timeout
	 * DWT_INT_RXPTO        0x00200000          // preamble detect timeout
	 * DWT_INT_SFDT         0x04000000          // SFD timeout
	 * DWT_INT_ARFE         0x20000000          // frame rejected (due to frame filtering configuration)
	 */
	// Note: using auto rx re-enable so don't need to trigger on error frames
	/*
	dwt_setinterrupt(DWT_INT_TFRS |
			DWT_INT_RFCG |
			DWT_INT_SFDT |
			DWT_INT_RFTO |
			DWT_INT_RPHE |
			DWT_INT_RFCE |
			DWT_INT_RFSL |
			DWT_INT_RXPTO |
			DWT_INT_SFDT, 1);
	*/
	dwt_setinterrupt(0xFFFFFFFF, 0);
	dwt_setinterrupt(
			DWT_INT_TFRS |
			DWT_INT_RFCG |
			DWT_INT_RPHE |
			DWT_INT_RFCE |
			DWT_INT_RFSL |
			DWT_INT_RFTO |
			DWT_INT_RXPTO |
			DWT_INT_SFDT |
			DWT_INT_ARFE, 1);

	// Configure the callbacks from the dwt library
	dwt_setcallbacks(txcallback, rxcallback);

	// Set the parameters of ranging and channel and whatnot
	global_ranging_config.chan           = 2;
	global_ranging_config.prf            = DWT_PRF_64M;
	global_ranging_config.txPreambLength = DWT_PLEN_64;//DWT_PLEN_4096
	// global_ranging_config.txPreambLength = DWT_PLEN_256;
	global_ranging_config.rxPAC          = DWT_PAC8;
	global_ranging_config.txCode         = 9;  // preamble code
	global_ranging_config.rxCode         = 9;  // preamble code
	global_ranging_config.nsSFD          = 0;
	global_ranging_config.dataRate       = DWT_BR_6M8;
	global_ranging_config.phrMode        = DWT_PHRMODE_EXT; //Enable extended PHR mode (up to 1024-byte packets)
	global_ranging_config.smartPowerEn   = 1;
	global_ranging_config.sfdTO          = 64+8+1;//(1025 + 64 - 32);
	dwt_configure(&global_ranging_config, 0);//(DWT_LOADANTDLY | DWT_LOADXTALTRIM));
	dwt_setsmarttxpower(global_ranging_config.smartPowerEn);

	// Configure TX power
	{
		global_tx_config.PGdly = pgDelay[global_ranging_config.chan];
		global_tx_config.power = txPower[global_ranging_config.chan];
		dwt_configuretxrf(&global_tx_config);
	}

	/* All constants same anyway
	if(DW1000_ROLE_TYPE == TAG)
		dwt_xtaltrim(xtaltrim[0]);
	else
		dwt_xtaltrim(xtaltrim[ANCHOR_EUI]);
	*/
	dwt_xtaltrim(xtaltrim[0]);

	////TEST 1: XTAL trim calibration
	//dwt_configcwmode(global_ranging_config.chan);
	//dwt_xtaltrim(8);
	//while(1);

	//{
	//    //TEST 2: TX Power level calibration
	//    uint8_t msg[127] = "The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the l";
	//    dwt_configcontinuousframemode(0x1000);
	//    dwt_writetxdata(127, (uint8 *)  msg, 0) ;
	//    dwt_writetxfctrl(127, 0);
	//    dwt_starttx(DWT_START_TX_IMMEDIATE);
	//    while(1);
	//}

	// Configure the antenna delay settings
	{
		uint16_t antenna_delay;

		//Antenna delay not really necessary if we're doing an end-to-end calibration
		antenna_delay = 0;
		dwt_setrxantennadelay(antenna_delay);
		dwt_settxantennadelay(antenna_delay);
		//global_tx_antenna_delay = antenna_delay;

		//// Shift this over a bit for some reason. Who knows.
		//// instance_common.c:508
		//antenna_delay = dwt_readantennadelay(global_ranging_config.prf) >> 1;
		//if (antenna_delay == 0) {
		//    printf("resetting antenna delay\r\n");
		//    // If it's not in the OTP, use a magic value from instance_calib.c
		//    antenna_delay = ((DWT_PRF_64M_RFDLY/ 2.0) * 1e-9 / DWT_TIME_UNITS);
		//    dwt_setrxantennadelay(antenna_delay);
		//    dwt_settxantennadelay(antenna_delay);
		//}
		//global_tx_antenna_delay = antenna_delay;
		//printf("tx antenna delay: %u\r\n", antenna_delay);
	}

	// // Set the sleep delay. Not sure what this does actually.
	// instancesettagsleepdelay(POLL_SLEEP_DELAY, BLINK_SLEEP_DELAY);


	// Configure as either a tag or anchor

	if (HACK_role == ANCHOR) {
		uint8_t eui_array[8];

		// Enable frame filtering
		dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

		dw1000_populate_eui(eui_array, HACK_EUI);
		dwt_seteui(eui_array);
		dwt_setpanid(DW1000_PANID);

		// We do want to enable auto RX
		dwt_setautorxreenable(1);
		// Let's do double buffering
		dwt_setdblrxbuffmode(0);
		// Disable RX timeout by setting to 0
		dwt_setrxtimeout(0);

		// Go for receiving
		dwt_rxenable(0);

	} else if (HACK_role == TAG) {
		uint8_t eui_array[8];

		// Allow data and ack frames
		dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

		dw1000_populate_eui(eui_array, HACK_EUI);
		dwt_seteui(eui_array);
		dwt_setpanid(DW1000_PANID);

		// Do this for the tag too
		dwt_setautorxreenable(1);
		dwt_setdblrxbuffmode(1);
		dwt_enableautoack(5 /*ACK_RESPONSE_TIME*/);

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

	}

	// Make it fast
	REG(SSI0_BASE + SSI_CR1) = 0;
	REG(SSI0_BASE + SSI_CPSR) = 4;
	REG(SSI0_BASE + SSI_CR1) |= SSI_CR1_SSE;

        // Quicken up the IO clock speed
	REG(SYS_CTRL_CLOCK_CTRL) = SYS_CTRL_CLOCK_CTRL_OSC_PD;

	return 0;
}

void set_subsequence_settings(uint8_t subseq_num, int role, bool force_config_reset){
#ifdef DW_DEBUG
	//printf("radio conf -> %u\r\n", subseq_num);
#endif
	if(role == ANCHOR) {
        	dwt_forcetrxoff();
	}

	//Change the channel depending on what subsequence number we're at
	uint32_t chan = (uint32_t)(subseq_num_to_chan(subseq_num, false));
	global_ranging_config.chan = (uint8_t)chan;
	if(force_config_reset) {
		dwt_configure(&global_ranging_config, 0);//(DWT_LOADANTDLY | DWT_LOADXTALTRIM));
		dwt_setsmarttxpower(global_ranging_config.smartPowerEn);
		global_tx_config.PGdly = pgDelay[global_ranging_config.chan];
		global_tx_config.power = txPower[global_ranging_config.chan];
		dwt_configuretxrf(&global_tx_config);
		dwt_setrxantennadelay(0);
		dwt_settxantennadelay(0);
	} else {
		dwt_setchannel(&global_ranging_config, 0);
	}

	//Change what antenna we're listening on
	uint8_t ant_sel;
	if (role == ANCHOR) {
		ant_sel = subseq_num_to_anchor_sel(subseq_num);
	} else { // (role == TAG)
		ant_sel = subseq_num_to_tag_sel(subseq_num);
	}
	dw1000_choose_antenna(ant_sel);
}
