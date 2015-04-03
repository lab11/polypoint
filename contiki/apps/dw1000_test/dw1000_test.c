
#include "contiki.h"
#include "sys/rtimer.h"
#include "dev/leds.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "dw1000.h"
#include "dev/ssi.h"
#include "cpu/cc2538/lpm.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/*---------------------------------------------------------------------------*/
PROCESS(dw1000_test, "DW1000Test");
AUTOSTART_PROCESSES(&dw1000_test);
static char periodic_task(struct rtimer *rt, void* ptr);
int app_dw1000_init ();
/*---------------------------------------------------------------------------*/

// uint8_t buf[100] = {144};

static struct rtimer periodic_timer;

#define DWT_PRF_64M_RFDLY 514.462f

#define TAG 1
#define ANCHOR 2

// 4 packet types
#define MSG_TYPE_TAG_POLL   0x61
#define MSG_TYPE_ANC_RESP   0x50
#define MSG_TYPE_TAG_FINAL  0x69
#define MSG_TYPE_ANC_FINAL  0x51

#define DW1000_ROLE_TYPE ANCHOR
// #define DW1000_ROLE_TYPE TAG

#define TAG_EUI 0
#define ANCHOR_EUI 1
#define NUM_ANCHORS 1

#define DW1000_PANID 0xD100

#define NODE_DELAY_US 5000
#define DELAY_MASK 0x00FFFFFFFE00
#define SPEED_OF_LIGHT 299702547.0
#define ANCHOR_EUI_BZ (ANCHOR_EUI-1)
#define NUM_ANTENNAS 3
#define NUM_CHANNELS 6
#define SUBSEQUENCE_PERIOD (RTIMER_SECOND*0.05)
#define SEQUENCE_WAIT_PERIOD (RTIMER_SECOND)

uint16_t global_tx_antenna_delay = 0;

uint32_t global_pkt_delay_upper32 = 0;

uint64_t global_tag_poll_tx_time = 0;
uint64_t global_tag_anchor_resp_rx_time = 0;

uint64_t global_tRP = 0;
uint32_t global_tSR = 0;
uint64_t global_tRF = 0;

uint32_t global_subseq_num = 0xFFFFFFFF;

dwt_config_t   global_ranging_config;

struct ieee154_msg  {
    uint8_t frameCtrl[2];                             //  frame control bytes 00-01
    uint8_t seqNum;                                   //  sequence_number 02
    uint8_t panID[2];                                 //  PAN ID 03-04
    uint8_t destAddr[8];
    uint8_t sourceAddr[8];
    uint8_t messageType; //   (application data and any user payload)
    uint8_t anchorID;
    uint8_t fcs[2] ;                                  //  we allow space for the CRC as it is logically part of the message. However ScenSor TX calculates and adds these bytes.
} __attribute__ ((__packed__));

struct ieee154_final_msg  {
    uint8_t frameCtrl[2];                             //  frame control bytes 00-01
    uint8_t seqNum;                                   //  sequence_number 02
    uint8_t panID[2];                                 //  PAN ID 03-04
    uint8_t destAddr[8];
    uint8_t sourceAddr[8];
    uint8_t messageType; //   (application data and any user payload)
    uint8_t anchorID;
    float distanceHist[NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS];
    uint8_t fcs[2] ;                                  //  we allow space for the CRC as it is logically part of the message. However ScenSor TX calculates and adds these bytes.
} __attribute__ ((__packed__));

struct ieee154_bcast_msg  {
    uint8_t frameCtrl[2];                             //  frame control bytes 00-01
    uint8_t seqNum;                                   //  sequence_number 02
    uint8_t panID[2];                                 //  PAN ID 03-04
    uint8_t destAddr[2];
    uint8_t sourceAddr[8];
    uint8_t messageType; //   (application data and any user payload)
    uint8_t subSeqNum;
    uint32_t tSP;
    uint32_t tSF;
    uint64_t tRR[NUM_ANCHORS]; // time differences
    uint8_t fcs[2] ;                                  //  we allow space for the CRC as it is logically part of the message. However ScenSor TX calculates and adds these bytes.
} __attribute__ ((__packed__));

struct ieee154_msg msg;
struct ieee154_final_msg fin_msg;
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

uint8_t subseq_num_to_chan(uint32_t subseq_num){
    uint8_t return_choice = ((subseq_num/NUM_ANTENNAS/NUM_ANTENNAS) % NUM_CHANNELS) +1;
    if(return_choice == 6) return_choice = 7;
    return return_choice;
}

uint8_t subseq_num_to_tag_sel(uint32_t subseq_num){
    return (subseq_num/NUM_ANTENNAS) % NUM_ANTENNAS;
}

uint8_t subseq_num_to_anchor_sel(uint32_t subseq_num){
    return subseq_num % NUM_ANTENNAS;
}

void incr_subsequence_counter(){
    global_subseq_num++;
    if(global_subseq_num > NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS+1){
        global_subseq_num = 0;
    }
}

void set_subsequence_settings(){
    //Last subsequence, reset everything decawave
    if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS+1)
        app_dw1000_init();

    //Change the channel depending on what subsequence number we're at
    uint32_t chan_ctrl = dwt_read32bitreg(CHAN_CTRL_ID);
    chan_ctrl &= 0xFFFFFF00;
    uint32_t chan = (uint32_t)(subseq_num_to_chan(global_subseq_num));
    chan_ctrl |= chan;
    chan_ctrl |= chan<<4;
    dwt_write32bitreg(CHAN_CTRL_ID,chan_ctrl);

    //Change what antenna we're listening on
    uint8_t ant_sel;
    if(DW1000_ROLE_TYPE == ANCHOR) ant_sel = subseq_num_to_anchor_sel(global_subseq_num);
    else ant_sel = subseq_num_to_tag_sel(global_subseq_num);
    dw1000_choose_antenna(ant_sel);
}

void send_poll(uint8_t msg_type){
    // FCS + SEQ + PANID:  5
    // ADDR:              10
    // PKT:                6
    // CRC:                2
    // EXTRA (??):         2
    // total              25
    uint16_t tx_frame_length = 25;
    memset(bcast_msg.destAddr, 0xFF, 2);

    bcast_msg.seqNum++;
    bcast_msg.subSeqNum = global_subseq_num;

    // First byte identifies this as a POLL
    bcast_msg.messageType = msg_type;

    // Tell the DW1000 about the packet
    dwt_writetxfctrl(tx_frame_length, 0);

    // Wait for not very long to get a response (likely an ack)
    dwt_setrxtimeout(10000); // us

    // Delay RX?
    dwt_setrxaftertxdelay(2000); // us

    uint32_t cur_time = dwt_readsystimestamphi32();
    uint32_t delay_time = cur_time + global_pkt_delay_upper32;
    delay_time &= 0xFFFFFFFE; //Make sure last bit is zero
    dwt_setdelayedtrxtime(delay_time);
    bcast_msg.tSP = delay_time;

    // Write the data
    dwt_writetxdata(tx_frame_length, (uint8_t*) &bcast_msg, 0);

    // Start the transmission
    dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);

    // MP bug - TX antenna delay needs reprogramming as it is
    // not preserved
    dwt_settxantennadelay(global_tx_antenna_delay);
}

// Triggered after a TX
void app_dw1000_txcallback (const dwt_callback_data_t *txd) {
    //NOTE: No need for tx timestamping after-the-fact (everything's done beforehand)
}

// Triggered when we receive a packet
void app_dw1000_rxcallback (const dwt_callback_data_t *rxd) {
    int err;

    if (DW1000_ROLE_TYPE == TAG) {

        // The tag receives one packet: "ANCHOR RESPONSE"
        // Make sure the packet is valid and matches an anchor response.
        // Need to timestamp it and schedule a response.

        if (rxd->event == DWT_SIG_RX_OKAY) {
            uint8_t recv_pkt[512];
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
                uint8_t anchor_id = msg_ptr->anchorID;
                if(anchor_id >= NUM_ANCHORS) anchor_id = NUM_ANCHORS;
                uint32_t delay_time =
                    ((uint32_t) (global_tag_anchor_resp_rx_time >> 8)) +
                    global_pkt_delay_upper32 * (NUM_ANCHORS-anchor_id+1);
                delay_time &= 0xFFFFFFFE;
                dwt_setdelayedtrxtime(delay_time);

                // Set the packet length
                uint16_t tx_frame_length = sizeof(bcast_msg);
                // Put at beginning of TX fifo
                dwt_writetxfctrl(tx_frame_length, 0);

                err = dwt_starttx(DWT_START_TX_DELAYED);
                if (err) {
                    printf("Error sending final message\r\n");
                } else {
                    // Need to actually fill out the packet
                    uint64_t tRR = global_tag_anchor_resp_rx_time;

                    bcast_msg.messageType = MSG_TYPE_TAG_FINAL;
                    bcast_msg.tRR[anchor_id] = tRR;
                    bcast_msg.tSP = delay_time;
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
            uint64_t timestamp;
            uint8_t subseq_num;

            // Get the timestamp first
            uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};
            dwt_readrxtimestamp(txTimeStamp);
            // printf("time0: 0x%02X\r\n", txTimeStamp[0]);
            // printf("time1: 0x%02X\r\n", txTimeStamp[1]);
            // printf("time2: 0x%02X\r\n", txTimeStamp[2]);
            // printf("time3: 0x%02X\r\n", txTimeStamp[3]);
            // printf("time4: 0x%02X\r\n", txTimeStamp[4]);
            timestamp = (uint64_t) txTimeStamp[0] +
                        (((uint64_t) txTimeStamp[1]) << 8) +
                        (((uint64_t) txTimeStamp[2]) << 16) +
                        (((uint64_t) txTimeStamp[3]) << 24) +
                        (((uint64_t) txTimeStamp[4]) << 32);

            // Get the packet
            dwt_readrxdata(&packet_type_byte, 1, 15);


            if (packet_type_byte == MSG_TYPE_TAG_POLL) {
                // Got POLL
                global_tRP = timestamp;

                // Start timer which will sequentially tune through all the channels
                dwt_readrxdata(&subseq_num, 1, 16);
                if(subseq_num == 0){
                    global_subseq_num = 0;
                    rtimer_set(&periodic_timer, RTIMER_NOW() + SUBSEQUENCE_PERIOD - RTIMER_SECOND*0.009869, 1,  //magic number from saleae to approximately line up config times
                                (rtimer_callback_t)periodic_task, NULL);
                }

                // Send response

                // Calculate the delay
                uint32_t delay_time =
                    ((uint32_t) (global_tRP >> 8)) +
                    global_pkt_delay_upper32;
                delay_time &= 0xFFFFFFFE;
                global_tSR = delay_time;
                // printf(" poll rx: %X\r\n", ((uint32_t) (global_tRP >> 8)));
                // printf(" delay: %X\r\n", global_pkt_delay_upper32);
                // printf(" newtime: %X\r\n", delay_time);
                dwt_setdelayedtrxtime(delay_time);

                // Set the packet length
                // FCS + SEQ + PANID:  5
                // ADDR:              16
                // PKT:                1
                // ANCHOR_ID:          1
                // CRC:                2
                // total              25
                uint16_t tx_frame_length = 25;
                // Put at beginning of TX fifo
                dwt_writetxfctrl(tx_frame_length, 0);

                dwt_writetxdata(tx_frame_length, (uint8_t*) &msg, 0);

                // Start delayed TX
                err = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
                if (err) {
                    printf("Could not send anchor response\r\n");
                }

                // Hopefully we will receive the FINAL message after this...
                dwt_setrxaftertxdelay(1000);


            } else if (packet_type_byte == MSG_TYPE_TAG_FINAL) {
                // Got FINAL

                // Read the whole packet
                dwt_readrxdata((uint8_t*)&bcast_msg, sizeof(bcast_msg), 0);

                global_tRF = timestamp;

                //TODO: might need to normalize all times to tSP and tRP
                double tRF = (double)global_tRF;
                double tSR = (double)(((uint64_t)global_tSR) << 8);
                double tRR = (double)bcast_msg.tRR[ANCHOR_EUI-1];
                double tSP = (double)(((uint64_t)bcast_msg.tSP) << 8);
                double tSF = (double)(((uint64_t)bcast_msg.tSF) << 8);
                double tRP = (double)global_tRP;

                printf("tRF = %f, tSR = %f, tRR = %f, tSP = %f, tSF = %f, tRP = %f\r\n", tRF, tSR, tRR, tSP, tSF, tRP);

                //tTOF^2 + (-tRF + tSR - tRR + tSP)*tTOF + (tRR*tRF - tSP*tRF - tSR*tRR + tSP*tSR - (tSF-tRR)*(tSR-tRP)) = 0
                double a = 1.0;
                double b = -tRF + tSR - tRR + tSP;
                double c = tRR*tRF - tSP*tRF - tSR*tRR + tSP*tSR - (tSF-tRR)*(tSR-tRP);

                //Perform quadratic equation
                double tTOF = (-b+sqrt(pow(b,2)-4*a*c))/(2*a);
                double dist = (tTOF * (double) DWT_TIME_UNITS) * 0.25;
                dist *= SPEED_OF_LIGHT;
                //dist -= dwt_getrangebias(2, (float) dist, DWT_PRF_64M);
                fin_msg.distanceHist[global_subseq_num] = (float)dist;

                //{
                //    // calculate the actual distance

                //    double distance;
                //    double tof;
                //    int32_t tofi;

                //    // Check for negative results and accept them making
                //    // them proper negative integers. Not sure why...
                //    tofi = (int32) time_of_flight;
                //    if (tofi < 0) {
                //        tofi *= -1; // make it positive
                //    }

                //    printf("tofi: %10i\r\n", (int)tofi);

                //    // Convert to seconds and divide by four because
                //    // there were four packets.
                //    tof = ((double) tofi * (double) DWT_TIME_UNITS) * 0.25;
                //    distance = tof * SPEED_OF_LIGHT;

                //    // Correct for range bias
                //    distance =
                //        distance - dwt_getrangebias(2, (float) distance, DWT_PRF_64M);

                //    fin_msg.distanceHist[global_subseq_num] = (float)distance;

                //    // printf("GOT RANGE: %f\r\n", distance);
                //}

                // Get ready to receive next POLL
                dwt_rxenable(0);
            }
        }
    }
}

int app_dw1000_init () {
    uint32_t devID;
    int err;
    dwt_txconfig_t tx_config;

    // Start off DW1000 comms slow
    REG(SSI0_BASE + SSI_CR1) = 0;
    REG(SSI0_BASE + SSI_CPSR) = 8;
    REG(SSI0_BASE + SSI_CR1) |= SSI_CR1_SSE;

    // Make sure we can talk to the DW1000
    devID = dwt_readdevid();
    if (devID != DWT_DEVICE_ID) {
        printf("Could not read Device ID from the DW1000\r\n");
        printf("Possible the chip is asleep...\r\n");
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
    global_ranging_config.chan           = 1;
    global_ranging_config.prf            = DWT_PRF_64M;
    global_ranging_config.txPreambLength = DWT_PLEN_1024;
    // global_ranging_config.txPreambLength = DWT_PLEN_256;
    global_ranging_config.rxPAC          = DWT_PAC32;
    global_ranging_config.txCode         = 9;  // preamble code
    global_ranging_config.rxCode         = 9;  // preamble code
    global_ranging_config.nsSFD          = 1;
    global_ranging_config.dataRate       = DWT_BR_110K;
    global_ranging_config.phrMode        = DWT_PHRMODE_EXT; //Enable extended PHR mode (up to 1024-byte packets)
    global_ranging_config.smartPowerEn   = 0;
    global_ranging_config.sfdTO          = (1025 + 64 - 32);
    dwt_configure(&global_ranging_config, (DWT_LOADANTDLY | DWT_LOADXTALTRIM));

    // Configure TX power
    {
        uint32_t power;

        // First check if these are in the OTP memory...I'm not sure if
        // we should expect this or not...
        power = dwt_getotptxpower(global_ranging_config.prf, global_ranging_config.chan);
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
        antenna_delay = dwt_readantennadelay(global_ranging_config.prf) >> 1;
        if (antenna_delay == 0) {
            // If it's not in the OTP, use a magic value from instance_calib.c
            antenna_delay = ((DWT_PRF_64M_RFDLY/ 2.0) * 1e-9 / DWT_TIME_UNITS);
            dwt_setrxantennadelay(antenna_delay);
            dwt_settxantennadelay(antenna_delay);
        }
        global_tx_antenna_delay = antenna_delay;
        printf("tx antenna delay: %u\r\n", antenna_delay);
    }

    // // Set the sleep delay. Not sure what this does actually.
    // instancesettagsleepdelay(POLL_SLEEP_DELAY, BLINK_SLEEP_DELAY);

    // Setup the constants in the outgoing packet
    msg.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
    msg.frameCtrl[1] = 0xCC; // ext addr
    msg.panID[0] = DW1000_PANID & 0xff;
    msg.panID[1] = DW1000_PANID >> 8;
    msg.seqNum = 0;
    msg.anchorID = ANCHOR_EUI_BZ;

    // Setup the constants in the outgoing packet
    fin_msg.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
    fin_msg.frameCtrl[1] = 0xCC; // ext addr
    fin_msg.panID[0] = DW1000_PANID & 0xff;
    fin_msg.panID[1] = DW1000_PANID >> 8;
    fin_msg.seqNum = 0;
    fin_msg.anchorID = ANCHOR_EUI_BZ;

    // Setup the constants in the outgoing packet
    bcast_msg.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
    bcast_msg.frameCtrl[1] = 0xC8; // ext addr
    bcast_msg.panID[0] = DW1000_PANID & 0xff;
    bcast_msg.panID[1] = DW1000_PANID >> 8;
    bcast_msg.seqNum = 0;
    bcast_msg.subSeqNum = 0;

    // Calculate the delay between packet reception and transmission.
    // This applies to the time between POLL and RESPONSE (on the anchor side)
    // and RESPONSE and POLL (on the tag side).
    global_pkt_delay_upper32 = (app_us_to_devicetimeu32(NODE_DELAY_US) & DELAY_MASK) >> 8;

    printf("delay: %x\r\n", (unsigned int)global_pkt_delay_upper32);


    // Configure as either a tag or anchor

    if (DW1000_ROLE_TYPE == ANCHOR) {
	uint8_t eui_array[8];

        // Enable frame filtering
        dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

        dw1000_populate_eui(eui_array, ANCHOR_EUI);
  	dwt_seteui(eui_array);
        dwt_setpanid(DW1000_PANID);

        // Set more packet constants
        dw1000_populate_eui(msg.sourceAddr, ANCHOR_EUI);
        dw1000_populate_eui(fin_msg.sourceAddr, ANCHOR_EUI);

        // hard code destination for now....
        dw1000_populate_eui(msg.destAddr, TAG_EUI);
        dw1000_populate_eui(fin_msg.destAddr, TAG_EUI);

        // We do want to enable auto RX
        dwt_setautorxreenable(1);
        // Let's do double buffering
        dwt_setdblrxbuffmode(0);
        // Disable RX timeout by setting to 0
        dwt_setrxtimeout(0);

        // Try pre-populating this
        msg.seqNum++;
        msg.messageType = MSG_TYPE_ANC_RESP;
        fin_msg.messageType = MSG_TYPE_ANC_FINAL;

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

    // Make it fast
    REG(SSI0_BASE + SSI_CR1) = 0;
    REG(SSI0_BASE + SSI_CPSR) = 2;
    REG(SSI0_BASE + SSI_CR1) |= SSI_CR1_SSE;

    return 0;
}

static char periodic_task(struct rtimer *rt, void* ptr){

    rtimer_clock_t next_start_time = RTIMER_TIME(rt);
    
    incr_subsequence_counter();
    if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS+1)
        next_start_time += SEQUENCE_WAIT_PERIOD;
    else if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS)
        next_start_time += SUBSEQUENCE_PERIOD*5;
    else
        next_start_time += SUBSEQUENCE_PERIOD;
    if(DW1000_ROLE_TYPE == ANCHOR){
        if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS+1)
            next_start_time = 0;
    }
 
    if(next_start_time != 0){
        rtimer_set(rt, next_start_time, 1, 
                    (rtimer_callback_t)periodic_task, ptr);
    }
    printf("in periodic_task\r\n");
    process_poll(&dw1000_test);
    return 1;
}

PROCESS_THREAD(dw1000_test, ev, data) {
    int err;

    PROCESS_BEGIN();

    leds_on(LEDS_ALL);

    //Keep things from going to sleep
    lpm_set_max_pm(0);


    dw1000_init();
    printf("Inited the DW1000 driver (setup SPI)\r\n");

    leds_off(LEDS_ALL);

    if (DW1000_ROLE_TYPE == ANCHOR) {
        printf("Setting up DW1000 as an Anchor.\r\n");
    } else {
        printf("Setting up DW1000 as a Tag.\r\n");
    }

    err = app_dw1000_init();
    if (err == -1) {
        printf("Error initializing the application.\r\n");
            leds_on(LEDS_RED);
    }

    //Set up the real-time task scheduling
    rtimer_init();
    rtimer_set(&periodic_timer, RTIMER_NOW() + RTIMER_SECOND, 1, 
                (rtimer_callback_t)periodic_task, NULL);

    global_subseq_num = NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS;

    while(1) {
        PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL);
        printf("after process_yield\r\n");
   
        set_subsequence_settings();
        if(DW1000_ROLE_TYPE == TAG){
            if(global_subseq_num < NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS){
                send_poll(MSG_TYPE_TAG_POLL);
            } else if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS){
                dwt_rxenable(0);
                dwt_setrxtimeout(0); // disable timeout
            }
        } else {
            //If it's after the last ranging operation, queue outgoing range estimates
            if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS){
                //We're likely in RX mode, so we need to exit before transmission
                dwt_forcetrxoff();

                //Schedule this transmission for our scheduled time slot
                uint32_t delay_time = dwt_readsystimestamphi32() + global_pkt_delay_upper32*(NUM_ANCHORS-ANCHOR_EUI+1);
                delay_time &= 0xFFFFFFFE;
                dwt_setdelayedtrxtime(delay_time);
                dwt_writetxfctrl(sizeof(fin_msg), 0);
                dwt_writetxdata(sizeof(fin_msg), (uint8_t*) &fin_msg, 0);
                dwt_starttx(DWT_START_TX_DELAYED);
            }
        }
       
    }

    PROCESS_END();
}
