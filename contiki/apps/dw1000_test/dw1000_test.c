
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

static struct rtimer periodic_timer;

#define DWT_PRF_64M_RFDLY 514.462f

#define TAG 1
#define ANCHOR 2

#define DW_DEBUG
//#define DW_CAL_TRX_DELAY

// 4 packet types
#define MSG_TYPE_TAG_POLL   0x61
#define MSG_TYPE_ANC_RESP   0x50
#define MSG_TYPE_TAG_FINAL  0x69
#define MSG_TYPE_ANC_FINAL  0x51

#define DW1000_ROLE_TYPE ANCHOR
// #define DW1000_ROLE_TYPE TAG

#define ANCHOR_CAL_LEN (0.914-0.18) //0.18 is post-over-air calibration

#define TAG_EUI 0
#define ANCHOR_EUI 10
#define NUM_ANCHORS 10

#define DW1000_PANID 0xD100

#define NODE_DELAY_US 6500
#define ANC_RESP_DELAY 1000
#define DELAY_MASK 0x00FFFFFFFE00
#define SPEED_OF_LIGHT 299702547.0
#define NUM_ANTENNAS 3
#define NUM_CHANNELS 3
#define SUBSEQUENCE_PERIOD (RTIMER_SECOND*0.110)
#define SEQUENCE_WAIT_PERIOD (RTIMER_SECOND)

uint32_t global_seq_count = 0;
uint16_t global_tx_antenna_delay = 0;

uint32_t global_pkt_delay_upper32 = 0;

uint64_t global_tag_poll_tx_time = 0;
uint64_t global_tag_anchor_resp_rx_time = 0;

uint64_t global_tRP = 0;
uint32_t global_tSR = 0;
uint64_t global_tRF = 0;
uint8_t global_recv_pkt[512];

uint32_t global_subseq_num = 0xFFFFFFFF;
uint8_t global_chan = 1;
float global_distances[NUM_ANCHORS*NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS];

dwt_config_t   global_ranging_config;
dwt_txconfig_t global_tx_config;

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

const uint8_t xtaltrim[11] = {
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

const uint8_t txPower[8] = {
    0x0,
    0x67,
    0x67,
    0x8b,
    0x9a,
    0x85,
    0x0,
    0xd1
};

const double txDelayCal[11*3] = {
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
   155.4877777777778,   154.9033333333333,   155.3533333333333
};

// convert microseconds to device time
uint32 app_us_to_devicetimeu32 (double microsecu)
{
    uint32_t dt;
    long double dtime;

    dtime = (microsecu / (double) DWT_TIME_UNITS) / 1e6 ;

    dt =  (uint32_t) (dtime) ;

    return dt;
}

uint8_t subseq_num_to_chan(uint32_t subseq_num, bool return_channel_index){
    uint8_t mod_choice = ((subseq_num/NUM_ANTENNAS/NUM_ANTENNAS) % NUM_CHANNELS);
    uint8_t return_choice = (mod_choice == 0) ? 1 :
                            (mod_choice == 1) ? 4 : 3;
    if(return_channel_index)
        return mod_choice;
    else
        return return_choice;
}

uint8_t subseq_num_to_tag_sel(uint32_t subseq_num){
#ifdef DW_CAL_TRX_DELAY
    return 0;
#else
    return (subseq_num/NUM_ANTENNAS) % NUM_ANTENNAS;
#endif
}

uint8_t subseq_num_to_anchor_sel(uint32_t subseq_num){
#ifdef DW_CAL_TRX_DELAY
    return 0;
#else
    return subseq_num % NUM_ANTENNAS;
#endif
}

void incr_subsequence_counter(){
    global_subseq_num++;
    if(global_subseq_num > NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS+1){
        global_subseq_num = 0;
    }
}

void set_subsequence_settings(){
    //Last subsequence, reset everything decawave
    if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS+1) {
        int err = app_dw1000_init();
        if (err == -1)
            leds_on(LEDS_RED);
        else
            leds_off(LEDS_RED);
    }

    //Change the channel depending on what subsequence number we're at
    uint32_t chan = (uint32_t)(subseq_num_to_chan(global_subseq_num, false));
    global_chan = (uint8_t)chan;
    global_ranging_config.chan = (uint8_t)chan;
    dwt_configure(&global_ranging_config, 0);//(DWT_LOADANTDLY | DWT_LOADXTALTRIM));
    global_tx_config.PGdly = pgDelay[global_ranging_config.chan];
    global_tx_config.power = txPower[global_ranging_config.chan];
    dwt_configuretxrf(&global_tx_config);
    dwt_setrxantennadelay(0);
    dwt_settxantennadelay(0);


    //Change what antenna we're listening on
    uint8_t ant_sel;
    if(DW1000_ROLE_TYPE == ANCHOR) ant_sel = subseq_num_to_anchor_sel(global_subseq_num);
    else ant_sel = subseq_num_to_tag_sel(global_subseq_num);
    dw1000_choose_antenna(ant_sel);
}

void send_poll(){
    //Reset all the tRRs at the beginning of each poll event
    memset(bcast_msg.tRR, 0, sizeof(bcast_msg.tRR));

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
    bcast_msg.messageType = MSG_TYPE_TAG_POLL;

    // Tell the DW1000 about the packet
    dwt_writetxfctrl(tx_frame_length, 0);

    // We'll get multiple responses, so let them all come in
    dwt_setrxtimeout(NODE_DELAY_US*NUM_ANCHORS);

    // Delay RX?
    dwt_setrxaftertxdelay(0); // us

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
            leds_on(LEDS_BLUE);
            struct ieee154_msg* msg_ptr;
            uint8_t packet_type_byte;

            // Get the timestamp first
            uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};
            dwt_readrxtimestamp(txTimeStamp);
            global_tag_anchor_resp_rx_time = (uint64_t) txTimeStamp[0] +
                                             (((uint64_t) txTimeStamp[1]) << 8) +
                                             (((uint64_t) txTimeStamp[2]) << 16) +
                                             (((uint64_t) txTimeStamp[3]) << 24) +
                                             (((uint64_t) txTimeStamp[4]) << 32);

            // Get the packet
            dwt_readrxdata(global_recv_pkt, rxd->datalength, 0);
            msg_ptr = (struct ieee154_msg*) global_recv_pkt;
            packet_type_byte = global_recv_pkt[21];
            printf("msg: %X\r\n",packet_type_byte);

            // Packet type byte is at a know location
            if (packet_type_byte == MSG_TYPE_ANC_RESP) {
                // Great, got an anchor response.
                // Now send a final message with the timings we know in it

                // First, set the time we want the packet to go out at.
                // This is based on our precalculated delay plus when we got
                // the anchor response packet. Note that we only add the upper
                // 32 bits together and use that time because this chip is
                // weird.
                uint8_t anchor_id = msg_ptr->anchorID;
                if(anchor_id >= NUM_ANCHORS) anchor_id = NUM_ANCHORS;

                // Need to actually fill out the packet
                bcast_msg.tRR[anchor_id-1] = global_tag_anchor_resp_rx_time;

                //TODO: Hack.... But not sure of any other way to get this to time out correctly...
                dwt_setrxtimeout(NODE_DELAY_US*(NUM_ANCHORS-anchor_id)+ANC_RESP_DELAY+1000);
            } else if(packet_type_byte == MSG_TYPE_ANC_FINAL){
                struct ieee154_final_msg* final_msg_ptr;
                final_msg_ptr = (struct ieee154_final_msg*) global_recv_pkt;
                int offset_idx = (final_msg_ptr->anchorID-1)*NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS;
                memcpy(&global_distances[offset_idx],final_msg_ptr->distanceHist,sizeof(fin_msg.distanceHist));
            }
        } else if (rxd->event == DWT_SIG_RX_TIMEOUT) {
            uint32_t delay_time = dwt_readsystimestamphi32() + global_pkt_delay_upper32;
            delay_time &= 0xFFFFFFFE;
            dwt_setdelayedtrxtime(delay_time);
            // Set the packet length
            uint16_t tx_frame_length = sizeof(bcast_msg);
            // Put at beginning of TX fifo
            dwt_writetxfctrl(tx_frame_length, 0);

            bcast_msg.seqNum++;
            bcast_msg.messageType = MSG_TYPE_TAG_FINAL;
            bcast_msg.tSF = delay_time;
            dwt_writetxdata(tx_frame_length, (uint8_t*) &bcast_msg, 0);
            err = dwt_starttx(DWT_START_TX_DELAYED);
            dwt_settxantennadelay(global_tx_antenna_delay);
            #ifdef DW_DEBUG
                if (err) {
                    printf("Error sending final message\r\n");
                }
            #endif
        }

    } else if (DW1000_ROLE_TYPE == ANCHOR) {

        // The anchor should receive two packets: a POLL from a tag and
        // a FINAL from a tag.

        if (rxd->event == DWT_SIG_RX_OKAY) {
            uint8_t packet_type_byte;
            uint64_t timestamp;
            uint8_t subseq_num;

            leds_on(LEDS_BLUE);

            // Get the timestamp first
            uint8_t txTimeStamp[5] = {0, 0, 0, 0, 0};
            dwt_readrxtimestamp(txTimeStamp);
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

                // Send response

                // Calculate the delay
                uint32_t delay_time =
                    ((uint32_t) (global_tRP >> 8)) +
                    global_pkt_delay_upper32*ANCHOR_EUI + (app_us_to_devicetimeu32(ANC_RESP_DELAY) >> 8);
                delay_time &= 0xFFFFFFFE;
                global_tSR = delay_time;
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
                // Hopefully we will receive the FINAL message after this...
                dwt_setrxaftertxdelay(1000);
                err = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
                dwt_settxantennadelay(global_tx_antenna_delay);
                #ifdef DW_DEBUG
                    if (err) {
                        printf("Could not send anchor response\r\n");
                    }
                #endif

                // Start timer which will sequentially tune through all the channels
                dwt_readrxdata(&subseq_num, 1, 16);
                if(subseq_num == 0){
                    global_subseq_num = 0;
                    rtimer_set(&periodic_timer, RTIMER_NOW() + SUBSEQUENCE_PERIOD - RTIMER_SECOND*0.011805, 1,  //magic number from saleae to approximately line up config times
                                (rtimer_callback_t)periodic_task, NULL);
                    global_seq_count++;
                    //Reset all the distance measurements
                    memset(fin_msg.distanceHist, 0, sizeof(fin_msg.distanceHist));

                }



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

                #ifdef DW_DEBUG
                    printf("tRF = %llu\r\n", (uint64_t)tRF);
                    printf("tSR = %llu\r\n", (uint64_t)tSR);
                    printf("tRR = %llu\r\n", (uint64_t)tRR);
                    printf("tSP = %llu\r\n", (uint64_t)tSP);
                    printf("tSF = %llu\r\n", (uint64_t)tSF);
                    printf("tRP = %llu\r\n", (uint64_t)tRP);
                #endif

                if(tRF != 0.0 && tSR != 0.0 && tRR != 0.0 && tSP != 0.0 && tSF != 0.0 && tRP != 0.0){
                    double aot = (tRF-tRP)/(tSF-tSP);
                    double tTOF = (tRF-tSR)-(tSF-tRR)*aot;
                    double dist = (tTOF*DWT_TIME_UNITS)/2;
                    
                    ////tTOF^2 + (-tRF + tSR - tRR + tSP)*tTOF + (tRR*tRF - tSP*tRF - tSR*tRR + tSP*tSR - (tSF-tRR)*(tSR-tRP)) = 0
                    //double a = 1.0;
                    //double b = -tRF + tSR - tRR + tSP;
                    //double c = tRR*tRF - tSP*tRF - tSR*tRR + tSP*tSR - (tSF-tRR)*(tSR-tRP);

                    ////Perform quadratic equation
                    //double tTOF = (-b-sqrt(pow(b,2)-4*a*c))/(2*a);
                    //double dist = (tTOF * (double) DWT_TIME_UNITS) * 0.5;
                    dist *= SPEED_OF_LIGHT;
                    //double range_bias = 0.0;//dwt_getrangebias(global_chan, (float) dist, DWT_PRF_64M);
#ifndef DW_CAL_TRX_DELAY
                    dist += ANCHOR_CAL_LEN;
                    dist -= txDelayCal[ANCHOR_EUI*NUM_CHANNELS + subseq_num_to_chan(global_subseq_num, true)];
#endif
                    #ifdef DW_DEBUG
                        printf("dist*100 = %d\r\n", (int)(dist*100));
                        //printf("range_bias*100 = %d\r\n", (int)(range_bias*100));
                    #endif
                    //dist -= dwt_getrangebias(2, (float) dist, DWT_PRF_64M);
                    fin_msg.distanceHist[global_subseq_num] = (float)dist;
                }

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
    // Note: using auto rx re-enable so don't need to trigger on error frames
    dwt_setinterrupt(DWT_INT_TFRS |
                     DWT_INT_RFCG |
                     DWT_INT_SFDT |
                     DWT_INT_RFTO |
                     DWT_INT_RPHE |
                     DWT_INT_RFCE |
                     DWT_INT_RFSL |
                     DWT_INT_RXPTO |
                     DWT_INT_SFDT, 1);

    // Configure the callbacks from the dwt library
    dwt_setcallbacks(app_dw1000_txcallback, app_dw1000_rxcallback);

    // Set the parameters of ranging and channel and whatnot
    global_ranging_config.chan           = 2;
    global_ranging_config.prf            = DWT_PRF_64M;
    global_ranging_config.txPreambLength = DWT_PLEN_4096;//DWT_PLEN_4096
    // global_ranging_config.txPreambLength = DWT_PLEN_256;
    global_ranging_config.rxPAC          = DWT_PAC64;
    global_ranging_config.txCode         = 9;  // preamble code
    global_ranging_config.rxCode         = 9;  // preamble code
    global_ranging_config.nsSFD          = 1;
    global_ranging_config.dataRate       = DWT_BR_110K;
    global_ranging_config.phrMode        = DWT_PHRMODE_EXT; //Enable extended PHR mode (up to 1024-byte packets)
    global_ranging_config.smartPowerEn   = 0;
    global_ranging_config.sfdTO          = 4096+64+1;//(1025 + 64 - 32);
    dwt_configure(&global_ranging_config, 0);//(DWT_LOADANTDLY | DWT_LOADXTALTRIM));

    // Configure TX power
    {
        global_tx_config.PGdly = pgDelay[global_ranging_config.chan];
        global_tx_config.power = txPower[global_ranging_config.chan];
        dwt_configuretxrf(&global_tx_config);
    }

    if(DW1000_ROLE_TYPE == TAG)
        dwt_xtaltrim(xtaltrim[0]);
    else
        dwt_xtaltrim(xtaltrim[ANCHOR_EUI]);

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
        global_tx_antenna_delay = antenna_delay;

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

    // Setup the constants in the outgoing packet
    msg.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
    msg.frameCtrl[1] = 0xCC; // ext addr
    msg.panID[0] = DW1000_PANID & 0xff;
    msg.panID[1] = DW1000_PANID >> 8;
    msg.seqNum = 0;
    msg.anchorID = ANCHOR_EUI;

    // Setup the constants in the outgoing packet
    fin_msg.frameCtrl[0] = 0x41; // data frame, ack req, panid comp
    fin_msg.frameCtrl[1] = 0xCC; // ext addr
    fin_msg.panID[0] = DW1000_PANID & 0xff;
    fin_msg.panID[1] = DW1000_PANID >> 8;
    fin_msg.seqNum = 0;
    fin_msg.anchorID = ANCHOR_EUI;

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

    #ifdef DW_DEBUG
        printf("global_seq_count: %u\r\n", (unsigned int)global_seq_count);
        printf("delay: %x\r\n", (unsigned int)global_pkt_delay_upper32);
    #endif


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
    REG(SSI0_BASE + SSI_CPSR) = 2;
    REG(SSI0_BASE + SSI_CR1) |= SSI_CR1_SSE;

    return 0;
}

static char periodic_task(struct rtimer *rt, void* ptr){

    rtimer_clock_t next_start_time = RTIMER_TIME(rt);
    
    incr_subsequence_counter();
    if(DW1000_ROLE_TYPE == ANCHOR){
        if(global_subseq_num == 0)
            next_start_time = 0;
        else if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS)
            next_start_time += SUBSEQUENCE_PERIOD*5;
        else
            next_start_time += SUBSEQUENCE_PERIOD;
    } else {
        if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS+1)
            next_start_time += SEQUENCE_WAIT_PERIOD;
        else if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS)
            next_start_time += SUBSEQUENCE_PERIOD*5;
        else
            next_start_time += SUBSEQUENCE_PERIOD;
    }
 
    if(next_start_time != 0){
        rtimer_set(rt, next_start_time, 1, 
                    (rtimer_callback_t)periodic_task, ptr);
    }
    #ifdef DW_DEBUG
        printf("in periodic_task\r\n");
    #endif
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
    #ifdef DW_DEBUG
        printf("Inited the DW1000 driver (setup SPI)\r\n");
    #endif

    leds_off(LEDS_ALL);

    #ifdef DW_DEBUG
        if (DW1000_ROLE_TYPE == ANCHOR) {
            printf("Setting up DW1000 as an Anchor.\r\n");
        } else {
            printf("Setting up DW1000 as a Tag.\r\n");
        }
    #endif

    err = app_dw1000_init();
    if (err == -1)
        leds_on(LEDS_RED);
    else
        leds_off(LEDS_RED);

    //Set up the real-time task scheduling
    rtimer_init();
    rtimer_set(&periodic_timer, RTIMER_NOW() + RTIMER_SECOND, 1, 
                (rtimer_callback_t)periodic_task, NULL);

    global_subseq_num = NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS;

    while(1) {
        PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL);
        #ifdef DW_DEBUG
            printf("after process_yield\r\n");
        #endif
   
        set_subsequence_settings();
        if(DW1000_ROLE_TYPE == TAG){
            if(global_subseq_num < NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS){
                //Make sure we're out of rx mode before attempting to transmit
                dwt_forcetrxoff();

                send_poll();
            } else if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS){
                dwt_rxenable(0);
                dwt_setrxtimeout(0); // disable timeout
            } else if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS+1){
                int ii, jj;
                for(ii=0; ii < NUM_ANCHORS; ii++){
                    int offset_idx = ii*NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS;
                    printf("tagstart %d\r\n",ii+1);
                    for(jj=0; jj < NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS; jj++){
                        int dist_times_1000 = (int)(global_distances[offset_idx+jj]*1000);
                        printf("%d.%d\r\n",dist_times_1000/1000,dist_times_1000%1000);
                    }
                    printf("tagend\r\n");
                }
                printf("done\r\n");
                memset(global_distances,0,sizeof(global_distances));
            }
        } else {
            //If it's after the last ranging operation, queue outgoing range estimates
            if(global_subseq_num == NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS){
                //We're likely in RX mode, so we need to exit before transmission
                dwt_forcetrxoff();

                //Schedule this transmission for our scheduled time slot
                uint32_t delay_time = dwt_readsystimestamphi32() + global_pkt_delay_upper32*(NUM_ANCHORS-ANCHOR_EUI+1)*2;
                delay_time &= 0xFFFFFFFE;
                dwt_setdelayedtrxtime(delay_time);
                dwt_writetxfctrl(sizeof(fin_msg), 0);
                dwt_writetxdata(sizeof(fin_msg), (uint8_t*) &fin_msg, 0);
                dwt_starttx(DWT_START_TX_DELAYED);
                dwt_settxantennadelay(global_tx_antenna_delay);
            }
        }
       
    }

    PROCESS_END();
}
