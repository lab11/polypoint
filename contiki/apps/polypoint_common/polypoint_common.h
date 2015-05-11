#ifndef POLYPOINT_COMMON_H
#define POLYPOINT_COMMON_H

/***********************
 ** CONFIGURATION MACROS
 */
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

#define ANCHOR_CAL_LEN (0.914-0.18) //0.18 is post-over-air calibration

#define TAG_EUI 0
#define ANCHOR_EUI 1
#define NUM_ANCHORS 10

#define DW1000_PANID 0xD100

#define NUM_MEASUREMENTS (NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS)

#define NODE_DELAY_US 7000
#define ANC_RESP_DELAY_US 1000
#define DELAY_MASK 0x00FFFFFFFE00
#define SPEED_OF_LIGHT 299702547.0
#define NUM_ANTENNAS 3
#define NUM_CHANNELS 3

//#define RT_SUBSEQUENCE_PERIOD (RTIMER_SECOND * 0.110)
#define RT_SUBSEQUENCE_PERIOD (RTIMER_SECOND * 0.250)
#define RT_SEQUENCE_PERIOD (RTIMER_SECOND * 2)
/*
#define RT_ANCHOR_RESPONSE_WINDOW (RTIMER_SECOND * (\
			(NUM_ANCHORS+1)*(NODE_DELAY_US/1000000)\
			+ 2*(ANC_RESP_DELAY_US/1000000)\
			))
			*/
#define RT_ANCHOR_RESPONSE_WINDOW (RTIMER_SECOND * .2)
#define RT_TAG_FINAL_WINDOW (RTIMER_SECOND * 5*(NODE_DELAY_US/1e6))

_Static_assert(RT_SEQUENCE_PERIOD >= RT_SUBSEQUENCE_PERIOD,
		"Inter-sequence timing can't be shorter than subseuqence timing");
_Static_assert(RT_SUBSEQUENCE_PERIOD > (RT_ANCHOR_RESPONSE_WINDOW+RT_TAG_FINAL_WINDOW),
		"Subsequence period must be long enough for all anchors and tag final");

#define TX_ANTENNA_DELAY 0

#define APP_US_TO_DEVICETIMEU32(_microsecu)\
	(\
	 (uint32_t) ( (_microsecu / (double) DWT_TIME_UNITS) / 1e6 )\
	)

#define MIN(_a, _b) ((_a < _b) ? (_a) : (_b))
#define MAX(_a, _b) ((_a > _b) ? (_a) : (_b))

#define DEBUG_B5_LOW\
	GPIO_CLR_PIN(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(5))
#define DEBUG_B5_HIGH\
	GPIO_SET_PIN(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(5))
#define DEBUG_B5_INIT\
	GPIO_SET_OUTPUT(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(5))
#define DEBUG_B4_LOW\
	GPIO_CLR_PIN(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(4))
#define DEBUG_B4_HIGH\
	GPIO_SET_PIN(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(4))
#define DEBUG_B4_INIT\
	GPIO_SET_OUTPUT(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(4))

#ifdef DW_DEBUG
#define DEBUG_P(...)\
	printf("[%u %u]  ", global_round_num, global_subseq_num); printf(__VA_ARGS__)
#else
#define DEBUG_P(...)
#endif


// Calculate the delay between packet reception and transmission.
// This applies to the time between POLL and RESPONSE (on the anchor side)
// and RESPONSE and POLL (on the tag side).
#define GLOBAL_PKT_DELAY_UPPER32\
	(\
	 (APP_US_TO_DEVICETIMEU32(NODE_DELAY_US) & DELAY_MASK) >> 8\
	)

extern const double txDelayCal[11*3];

struct ieee154_anchor_poll_resp  {
	uint8_t frameCtrl[2];                             //  frame control bytes 00-01
	uint8_t seqNum;                                   //  sequence_number 02
	uint8_t panID[2];                                 //  PAN ID 03-04
	uint8_t destAddr[8];
	uint8_t sourceAddr[8];
	uint8_t messageType; //   (application data and any user payload)
	uint8_t anchorID;
	uint8_t fcs[2] ;                                  //  we allow space for the CRC as it is logically part of the message. However ScenSor TX calculates and adds these bytes.
} __attribute__ ((__packed__));

struct ieee154_anchor_final_msg  {
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
	uint8_t roundNum;
	uint8_t subSeqNum;
	uint32_t tSP;
	uint32_t tSF;
	uint64_t tRR[NUM_ANCHORS]; // time differences
	uint8_t fcs[2] ;                                  //  we allow space for the CRC as it is logically part of the message. However ScenSor TX calculates and adds these bytes.
} __attribute__ ((__packed__));


_Static_assert(offsetof(struct ieee154_anchor_poll_resp, messageType) == offsetof(struct ieee154_anchor_final_msg, messageType),\
			"messageType field at inconsisten offsets (ap,af)");


uint8_t subseq_num_to_chan(uint8_t subseq_num, bool return_channel_index);
void set_subsequence_settings(uint8_t subseq_num, int role);
int app_dw1000_init (
		int HACK,
		void (*txcallback)(const dwt_callback_data_t *),
		void (*rxcallback)(const dwt_callback_data_t *)
		);

#endif // POLYPOINT_COMMON_H
