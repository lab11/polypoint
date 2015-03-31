
#include "contiki.h"
#include "sys/etimer.h"
#include "dev/leds.h"
#include "deca_device_api.h"
#include "dw1000.h"
#include "dev/ssi.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
PROCESS(dw1000_test, "DW1000Test");
AUTOSTART_PROCESSES(&dw1000_test);
/*---------------------------------------------------------------------------*/

// uint8_t buf[100] = {144};

static struct etimer periodic_timer;

#define DWT_PRF_64M_RFDLY 514.462f

#define TAG 1
#define ANCHOR 2

// 3 packet types
#define MSG_TYPE_TAG_POLL  0x61
#define MSG_TYPE_ANC_RESP  0x50
#define MSG_TYPE_TAG_FINAL 0x69

#define DW1000_ROLE_TYPE ANCHOR
// #define DW1000_ROLE_TYPE TAG

#define TAG_EUI 0
#define ANCHOR_EUI 1

#define DW1000_PANID 0xD100

#define NODE_DELAY_US 5000
#define DELAY_MASK 0x00FFFFFFFE00
#define SPEED_OF_LIGHT 299702547.0

uint16_t global_tx_antenna_delay = 0;

uint32_t global_pkt_delay_upper32 = 0;

uint64_t global_tag_poll_tx_time = 0;
uint64_t global_tag_anchor_resp_rx_time = 0;

uint64_t global_anchor_poll_rx_time = 0;
uint64_t global_anchor_resp_tx_time = 0;
uint64_t global_anchor_final_rx_time = 0;



struct ieee154_msg  {
    uint8_t frameCtrl[2];                             //  frame control bytes 00-01
    uint8_t seqNum;                                   //  sequence_number 02
    uint8_t panID[2];                                 //  PAN ID 03-04
    uint8_t destAddr[8];
    uint8_t sourceAddr[8];
    uint8_t messageType; //   (application data and any user payload)
    uint32_t responseMinusPoll; // time differences
    uint32_t finalMinusResponse;
    uint8_t fcs[2] ;                                  //  we allow space for the CRC as it is logically part of the message. However ScenSor TX calculates and adds these bytes.
} __attribute__ ((__packed__));

struct ieee154_bcast_msg  {
    uint8_t frameCtrl[2];                             //  frame control bytes 00-01
    uint8_t seqNum;                                   //  sequence_number 02
    uint8_t panID[2];                                 //  PAN ID 03-04
    uint8_t destAddr[2];
    uint8_t sourceAddr[8];
    uint8_t messageType; //   (application data and any user payload)
    uint32_t responseMinusPoll; // time differences
    uint32_t finalMinusResponse;
    uint8_t fcs[2] ;                                  //  we allow space for the CRC as it is logically part of the message. However ScenSor TX calculates and adds these bytes.
} __attribute__ ((__packed__));

struct ieee154_msg msg;
struct ieee154_bcast_msg bcast_msg;

// typedef enum {
//     TAG_SEND_POLL,
// } tag_state_e;

// tag_state_e tag_state;

// convert microseconds to device time
uint32 app_us_to_devicetimeu32 (double microsecu)
{
    uint32_t dt;
    long double dtime;

    dtime = (microsecu / (double) DWT_TIME_UNITS) / 1e6 ;

    dt =  (uint32_t) (dtime) ;

    return dt;
}


// Triggered after a TX
void app_dw1000_txcallback (const dwt_callback_data_t *txd) {


    if (DW1000_ROLE_TYPE == TAG) {

        if (txd->event == DWT_SIG_TX_DONE) {

            uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};

            // If our current state is "SEND FINAL" then we need to capture
            // the time our POLL message went out
            dwt_readtxtimestamp(txTimeStamp);
            global_tag_poll_tx_time = (uint64_t) txTimeStamp[0] +
                                      (((uint64_t) txTimeStamp[1]) << 8) +
                                      (((uint64_t) txTimeStamp[2]) << 16) +
                                      (((uint64_t) txTimeStamp[3]) << 24) +
                                      (((uint64_t) txTimeStamp[4]) << 32);
        }



    } else if (DW1000_ROLE_TYPE == ANCHOR) {

        if (txd->event == DWT_SIG_TX_DONE) {

            // Capture the TX timestamp
            uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};
            dwt_readtxtimestamp(txTimeStamp);
            uint64_t timestamp = (uint64_t) txTimeStamp[0] +
                                 (((uint64_t) txTimeStamp[1]) << 8) +
                                 (((uint64_t) txTimeStamp[2]) << 16) +
                                 (((uint64_t) txTimeStamp[3]) << 24) +
                                 (((uint64_t) txTimeStamp[4]) << 32);
            global_anchor_resp_tx_time = timestamp;
        }




    }

}

// Triggered when we receive a packet
void app_dw1000_rxcallback (const dwt_callback_data_t *rxd) {
    int err;

    if (DW1000_ROLE_TYPE == TAG) {

        // The tag receives one packet: "ANCHOR RESPONSE"
        // Make sure the packet is valid and matches an anchor response.
        // Need to timestamp it and schedule a response.

        if (rxd->event == DWT_SIG_RX_OKAY) {
            uint8_t recv_pkt[128];
            struct ieee154_msg* msg_ptr;

            // Get the timestamp first
            uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};
            dwt_readrxtimestamp(txTimeStamp);
            global_tag_anchor_resp_rx_time = (uint64_t) txTimeStamp[0] +
                                             (((uint64_t) txTimeStamp[1]) << 8) +
                                             (((uint64_t) txTimeStamp[2]) << 16) +
                                             (((uint64_t) txTimeStamp[3]) << 24) +
                                             (((uint64_t) txTimeStamp[4]) << 32);

            // Get the packet
            dwt_readrxdata(recv_pkt, rxd->datalength, 0);
            msg_ptr = (struct ieee154_msg*) recv_pkt;

            // Packet type byte is at a know location
            if (msg_ptr->messageType == MSG_TYPE_ANC_RESP) {
                // Great, got an anchor response.
                // Now send a final message with the timings we know in it

                // First, set the time we want the packet to go out at.
                // This is based on our precalculated delay plus when we got
                // the anchor response packet. Note that we only add the upper
                // 32 bits together and use that time because this chip is
                // weird.
                uint32_t delay_time =
                    ((uint32_t) (global_tag_anchor_resp_rx_time >> 8)) +
                    global_pkt_delay_upper32;
                dwt_setdelayedtrxtime(delay_time);

                // Set the packet length
                // FCS + SEQ + PANID:  5
                // ADDR:              10
                // PKT:    1 + 4 + 4 = 9
                // CRC:                2
                // total              32
                uint16_t tx_frame_length = 26;
                // Put at beginning of TX fifo
                dwt_writetxfctrl(tx_frame_length, 0);

                err = dwt_starttx(DWT_START_TX_DELAYED);
                if (err) {
                    printf("Error sending final message\n");
                } else {
                    // Need to actually fill out the packet
                    // Calculate the delay between the tag sending a POLL and
                    // the anchor responding.
                    uint32_t responseMinusPoll =
                        (uint32_t) global_tag_anchor_resp_rx_time -
                        (uint32_t) global_tag_poll_tx_time;

                    uint32_t finalMinusResponse =
                        (uint32_t) global_tx_antenna_delay +
                        ((uint32_t) global_pkt_delay_upper32 << 8) -
                        (((uint32_t) global_tag_anchor_resp_rx_time) & 0x1FF);

                    bcast_msg.messageType = MSG_TYPE_TAG_FINAL;
                    bcast_msg.responseMinusPoll = responseMinusPoll;
                    bcast_msg.finalMinusResponse = finalMinusResponse;
                    bcast_msg.seqNum++;

                    dwt_writetxdata(tx_frame_length, (uint8_t*) &bcast_msg, 0);
                }
            }
        }

    } else if (DW1000_ROLE_TYPE == ANCHOR) {

        // The anchor should receive two packets: a POLL from a tag and
        // a FINAL from a tag.

        if (rxd->event == DWT_SIG_RX_OKAY) {
            uint8_t packet_type_byte;
            uint8_t recv_pkt[128];
            struct ieee154_msg* msg_ptr;
            uint64_t timestamp;

            // Get the timestamp first
            uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};
            dwt_readrxtimestamp(txTimeStamp);
            // printf("time0: 0x%02X\n", txTimeStamp[0]);
            // printf("time1: 0x%02X\n", txTimeStamp[1]);
            // printf("time2: 0x%02X\n", txTimeStamp[2]);
            // printf("time3: 0x%02X\n", txTimeStamp[3]);
            // printf("time4: 0x%02X\n", txTimeStamp[4]);
            timestamp = (uint64_t) txTimeStamp[0] +
                        (((uint64_t) txTimeStamp[1]) << 8) +
                        (((uint64_t) txTimeStamp[2]) << 16) +
                        (((uint64_t) txTimeStamp[3]) << 24) +
                        (((uint64_t) txTimeStamp[4]) << 32);

            // Get the packet
            dwt_readrxdata(&packet_type_byte, 1, 15);
            // dwt_readrxdata(recv_pkt, rxd->datalength, 0);
            // msg_ptr = (struct ieee154_msg*) recv_pkt;


            // if (msg_ptr->messageType == MSG_TYPE_TAG_POLL) {
            if (packet_type_byte == MSG_TYPE_TAG_POLL) {
                // Got POLL

                global_anchor_poll_rx_time = timestamp;

                // Send response

                // Calculate the delay
                uint32_t delay_time =
                    ((uint32_t) (global_anchor_poll_rx_time >> 8)) +
                    global_pkt_delay_upper32;
                // printf(" poll rx: %X\n", ((uint32_t) (global_anchor_poll_rx_time >> 8)));
                // printf(" delay: %X\n", global_pkt_delay_upper32);
                // printf(" newtime: %X\n", delay_time);
                dwt_setdelayedtrxtime(delay_time);

                // Set the packet length
                // FCS + SEQ + PANID:  5
                // ADDR:              16
                // PKT:                1
                // CRC:                2
                // total              24
                uint16_t tx_frame_length = 24;
                // Put at beginning of TX fifo
                dwt_writetxfctrl(tx_frame_length, 0);

                // Start delayed TX
                err = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
                if (err) {
                    printf("Could not send anchor response\n");
                }

                // Hopefully we will receive the FINAL message after this...
                dwt_setrxaftertxdelay(1000);


            // } else if (msg_ptr->messageType == MSG_TYPE_TAG_FINAL) {
            } else if (packet_type_byte == MSG_TYPE_TAG_FINAL) {
                // Got FINAL

                // Read the whole packet
                dwt_readrxdata(recv_pkt, rxd->datalength, 0);
                msg_ptr = (struct ieee154_msg*) recv_pkt;

                global_anchor_final_rx_time = timestamp;

                // Poll response round trip delay is
                // (anchorRespRxTime - tagPollTxTime) - (anchorRespTxTime - tagPollRxTime)
                uint32_t aTxT = (uint32_t) global_anchor_resp_tx_time -
                                (uint32_t) global_anchor_poll_rx_time;
                uint32_t pollResponseRTD = msg_ptr->responseMinusPoll - aTxT;

                // Response final round trip delay time is
                // (tagFinalRxTime - anchorRespTxTime) - (tagFinalTxTime - anchorRespRxTime)
                uint32_t tRxT = (uint32_t) global_anchor_final_rx_time -
                                (uint32_t) global_anchor_resp_tx_time;
                uint32_t responseFinalRTD = tRxT - msg_ptr->finalMinusResponse;

                uint32_t time_of_flight = pollResponseRTD + responseFinalRTD;

                printf("GOT RANGE:\n");
                printf("atxt:    %u\n", aTxT);
                printf("RMP:     %u\n", msg_ptr->responseMinusPoll);
                printf("tRxT:    %u\n", tRxT);
                printf("FMR:     %u\n", msg_ptr->finalMinusResponse);
                printf("pollRTD: %u\n", pollResponseRTD);
                printf("respRTD: %u\n", responseFinalRTD);
                printf("tof:     %u\n", time_of_flight);





                // printf("tof: %u\n", time_of_flight);
                // printf("test: %f\n", 3.5);

                {
                    // calculate the actual distance

                    double distance;
                    double tof;
                    int32_t tofi;

                    // Check for negative results and accept them making
                    // them proper negative integers. Not sure why...
                    tofi = (int32) time_of_flight;
                    if (tofi < 0) {
                        tofi *= -1; // make it positive
                    }

                    printf("tofi: %10i\n", tofi);

                    // Convert to seconds and divide by four because
                    // there were four packets.
                    tof = ((double) tofi * (double) DWT_TIME_UNITS) * 0.25;
                    distance = tof * SPEED_OF_LIGHT;

                    // Correct for range bias
                    distance =
                        distance - dwt_getrangebias(2, (float) distance, DWT_PRF_64M);

                    // printf("GOT RANGE: %f\n", distance);
                }

                // Get ready to receive next POLL
                dwt_rxenable(0);
            }
        }
    }
}

int app_dw1000_init () {
    uint32_t devID;
    int err;
    dwt_config_t   ranging_config;
    dwt_txconfig_t tx_config;

    // Make sure we can talk to the DW1000
    devID = dwt_readdevid();
    if (devID != DWT_DEVICE_ID) {
        printf("Could not read Device ID from the DW1000\n");
        printf("Possible the chip is asleep...\n");
        return -1;
    }

    // Reset the DW1000...for some reason
    dw1000_reset();

    // Select which of the three antennas on the board to use
    dw1000_choose_antenna(1);

    // Init the dw1000 hardware
    err = dwt_initialise(DWT_LOADUCODE    |
                         DWT_LOADLDO      |
                         DWT_LOADTXCONFIG |
                         DWT_LOADANTDLY   |
                         DWT_LOADXTALTRIM);
    if (err != DWT_SUCCESS) {
        return -1;
    }

    // Setup interrupts
    // Note: using auto rx re-enable so don't need to trigger on error frames
    dwt_setinterrupt(DWT_INT_TFRS |
                     DWT_INT_RFCG |
                     DWT_INT_SFDT |
                     DWT_INT_RFTO, 1);

    // Configure the callbacks from the dwt library
    dwt_setcallbacks(app_dw1000_txcallback, app_dw1000_rxcallback);

    // Set the parameters of ranging and channel and whatnot
    ranging_config.chan           = 2;
    ranging_config.prf            = DWT_PRF_64M;
    ranging_config.txPreambLength = DWT_PLEN_1024;
    // ranging_config.txPreambLength = DWT_PLEN_256;
    ranging_config.rxPAC          = DWT_PAC32;
    ranging_config.txCode         = 9;  // preamble code
    ranging_config.rxCode         = 9;  // preamble code
    ranging_config.nsSFD          = 1;
    ranging_config.dataRate       = DWT_BR_110K;
    ranging_config.phrMode        = DWT_PHRMODE_STD;
    ranging_config.smartPowerEn   = 0;
    ranging_config.sfdTO          = (1025 + 64 - 32);
    dwt_configure(&ranging_config, (DWT_LOADANTDLY | DWT_LOADXTALTRIM));

    // Configure TX power
    {
        uint32_t power;

        // First check if these are in the OTP memory...I'm not sure if
        // we should expect this or not...
        power = dwt_getotptxpower(ranging_config.prf, ranging_config.chan);
        if (power == 0 || power == 0xFFFFFFFF) {
            // No power values stored. Use one from the evm sample
            // application.
            power = 0x07274767;
        }
        // We are not using smartpower, so we do something weird here
        // to set the power
        power = power & 0xFF;
        power = (power | (power << 8) | (power << 16) | (power << 24));

        tx_config.PGdly = 0xc2; // magic value from instance_calib.c
        tx_config.power = power;
        dwt_configuretxrf(&tx_config);
    }

    // Configure the antenna delay settings
    {
        uint16_t antenna_delay;

        // Shift this over a bit for some reason. Who knows.
        // instance_common.c:508
        antenna_delay = dwt_readantennadelay(ranging_config.prf) >> 1;
        if (antenna_delay == 0) {
            // If it's not in the OTP, use a magic value from instance_calib.c
            antenna_delay = ((DWT_PRF_64M_RFDLY/ 2.0) * 1e-9 / DWT_TIME_UNITS);
            dwt_setrxantennadelay(antenna_delay);
            dwt_settxantennadelay(antenna_delay);
        }
        global_tx_antenna_delay = antenna_delay;
        printf("tx antenna delay: %u\n", antenna_delay);
    }

    // // Set the sleep delay. Not sure what this does actually.
    // instancesettagsleepdelay(POLL_SLEEP_DELAY, BLINK_SLEEP_DELAY);

    // Setup the constants in the outgoing packet
    msg.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
    msg.frameCtrl[1] = 0xCC; // ext addr
    msg.panID[0] = DW1000_PANID & 0xff;
    msg.panID[1] = DW1000_PANID >> 8;
    msg.seqNum = 0;

    // Setup the constants in the outgoing packet
    bcast_msg.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
    bcast_msg.frameCtrl[1] = 0xC8; // ext addr
    bcast_msg.panID[0] = DW1000_PANID & 0xff;
    bcast_msg.panID[1] = DW1000_PANID >> 8;
    bcast_msg.seqNum = 0;

    // Calculate the delay between packet reception and transmission.
    // This applies to the time between POLL and RESPONSE (on the anchor side)
    // and RESPONSE and POLL (on the tag side).
    global_pkt_delay_upper32 =
        (app_us_to_devicetimeu32(NODE_DELAY_US) & DELAY_MASK) >> 8;

    printf("delay: %x\n", global_pkt_delay_upper32);


    // Configure as either a tag or anchor

    if (DW1000_ROLE_TYPE == ANCHOR) {
	uint8_t eui_array[8];

        // Disable frame filtering
        dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

        dw1000_populate_eui(eui_array, ANCHOR_EUI);
  	dwt_seteui(eui_array);
        dwt_setpanid(DW1000_PANID);

        // Set more packet constants
        dw1000_populate_eui(msg.sourceAddr, ANCHOR_EUI);

        // hard code destination for now....
        dw1000_populate_eui(msg.destAddr, TAG_EUI);

        // We do want to enable auto RX
        dwt_setautorxreenable(1);
        // No double buffering
        dwt_setdblrxbuffmode(0);
        // Disable RX timeout by setting to 0
        dwt_setrxtimeout(0);

        // Try pre-populating this
        msg.seqNum++;
        msg.messageType = MSG_TYPE_ANC_RESP;
        dwt_writetxdata(24, (uint8_t*) &msg, 0);

        // Go for receiving
        dwt_rxenable(0);

    } else if (DW1000_ROLE_TYPE == TAG) {
	uint8_t eui_array[8];

        // First thing we do as a TAG is send the POLL message when the
        // timer fires
        // tag_state = TAG_SEND_POLL;

        // Allow data and ack frames
        dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

        dw1000_populate_eui(eui_array, TAG_EUI);
  	dwt_seteui(eui_array);
        dwt_setpanid(DW1000_PANID);

        // Set more packet constants
        dw1000_populate_eui(bcast_msg.sourceAddr, TAG_EUI);
	memset(bcast_msg.destAddr, 0xFF, 2);

        // Do this for the tag too
        dwt_setautorxreenable(1);
        dwt_setdblrxbuffmode(0);
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


    return 0;
}

PROCESS_THREAD(dw1000_test, ev, data) {
    int i;
    int err;

    PROCESS_BEGIN();

    leds_on(LEDS_ALL);

    // Start off DW1000 comms slow
    REG(SSI0_BASE + SSI_CR1) = 0;
    REG(SSI0_BASE + SSI_CPSR) = 8;
    REG(SSI0_BASE + SSI_CR1) |= SSI_CR1_SSE;

    dw1000_init();
    printf("Inited the DW1000 driver (setup SPI)\n");

    leds_off(LEDS_ALL);

    if (DW1000_ROLE_TYPE == ANCHOR) {
        printf("Setting up DW1000 as an Anchor.\n");
    } else {
        printf("Setting up DW1000 as a Tag.\n");
    }

    err = app_dw1000_init();
    if (err == -1) {
        printf("Error initializing the application.\n");
            leds_on(LEDS_RED);
    }

    // Make it fast
    REG(SSI0_BASE + SSI_CR1) = 0;
    REG(SSI0_BASE + SSI_CPSR) = 2;
    REG(SSI0_BASE + SSI_CR1) |= SSI_CR1_SSE;

    if (DW1000_ROLE_TYPE == ANCHOR) {
        printf("Awaiting POLL\n");

    } else if (DW1000_ROLE_TYPE == TAG) {

        etimer_set(&periodic_timer, CLOCK_SECOND*3);
    }

    while(1) {
        PROCESS_YIELD();

        if (etimer_expired(&periodic_timer)) {

            leds_toggle(LEDS_BLUE);

            // On timer fire, send a POLL message to an anchor

            // FCS + SEQ + PANID:  5
            // ADDR:              10
            // PKT:                1
            // CRC:                2
            // EXTRA (??):         2
            // total              20
            uint16_t tx_frame_length = 20;
	    memset(bcast_msg.destAddr, 0xFF, 2);

            bcast_msg.seqNum++;

            // First byte identifies this as a POLL
            bcast_msg.messageType = MSG_TYPE_TAG_POLL;

            // Tell the DW1000 about the packet
            dwt_writetxfctrl(tx_frame_length, 0);

            // Wait for not very long to get a response (likely an ack)
            dwt_setrxtimeout(10000); // us
            // dwt_setrxtimeout(0); // us

            // Delay RX?
            dwt_setrxaftertxdelay(2000); // us

            // Write the data
            dwt_writetxdata(tx_frame_length, (uint8_t*) &bcast_msg, 0);

            // Start the transmission
            dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

            // MP bug - TX antenna delay needs reprogramming as it is
            // not preserved
            dwt_settxantennadelay(global_tx_antenna_delay);

            etimer_restart(&periodic_timer);

        }
    }

    PROCESS_END();
}
