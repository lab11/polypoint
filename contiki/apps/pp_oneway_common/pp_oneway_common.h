#ifndef POLYPOINT_COMMON_H
#define POLYPOINT_COMMON_H

/***********************
 ** CONFIGURATION MACROS
 */
#define DWT_PRF_64M_RFDLY 514.462f

#define TAG 1
#define ANCHOR 2

//#define DW_DEBUG
//#define DW_CAL_TRX_DELAY

// If set, the anchor will sort the ranges as they arrive
#define SORT_MEASUREMENTS

#ifdef  SORT_MEASUREMENTS
// If set, the anchor will only report the 10th %ile distance in ANC_FINAL. Requires SORT_MEASUREMENTS
#define TARGET_PERCENTILE 0.10
#define ANC_FINAL_PERCENTILE_ONLY
#endif

#define MSG_TYPE_PP_ONEWAY_TAG_POLL   0x60
#define MSG_TYPE_PP_ONEWAY_TAG_FINAL  0x6F
#define MSG_TYPE_PP_ONEWAY_ANC_FINAL  0x70

#define ANCHOR_CAL_LEN (0.914-0.18) //0.18 is post-over-air calibration

#define NUM_ANCHORS 10

#define DW1000_PANID 0xD100

#define DELAY_MASK 0x00FFFFFFFE00
#define SPEED_OF_LIGHT 299702547.0
#define NUM_ANTENNAS 3
#define NUM_CHANNELS 3
#define NUM_MEASUREMENTS (NUM_ANTENNAS*NUM_ANTENNAS*NUM_CHANNELS)

#define TX_ANTENNA_DELAY 0


#define ANC_FINAL_RX_PKT_TIME_US	820
#define ANC_FINAL_RX_PKT_MEMCPY_TIME_US	120
#define ANC_FINAL_RX_PKT_PRINTF_TIME_US	150
#define ANC_FINAL_RX_PKT_GUARD_US	50
#ifdef DW_DEBUG
#define ANC_FINAL_RX_TIME_ON_TAG	(\
		ANC_FINAL_RX_PKT_TIME_US +\
		ANC_FINAL_RX_PKT_MEMCPY_TIME_US +\
		ANC_FINAL_RX_PKT_PRINTF_TIME_US +\
		ANC_FINAL_RX_PKT_GUARD_US\
		)
#else
#define ANC_FINAL_RX_TIME_ON_TAG	(\
		ANC_FINAL_RX_PKT_TIME_US +\
		ANC_FINAL_RX_PKT_MEMCPY_TIME_US +\
		ANC_FINAL_RX_PKT_GUARD_US\
		)
#endif

#ifdef DW_DEBUG
// lots of headroom for printfs
#define POLL_TO_SS_US		 5000
#define SS_TO_SQ_US		 3000
#define ALL_ANC_FINAL_US	80000
#define SS_PRINTF_US		  2e6
#else
#define POLL_TO_SS_US		 5000
#define SS_TO_SQ_US		 3000
#define ALL_ANC_FINAL_US	80000
#define SS_PRINTF_US		  2e6
#endif

#define US_TO_RT(_us) (RTIMER_SECOND * ((_us)/1e6))

/*
#define NODE_DELAY_US 7000
#define TAG_SETTINGS_SETUP_US 850
#define TAG_SEND_POLL_DELAY_US 250
#define TAG_SEND_FINAL_DELAY_US 290 // final is longer packet than poll
#define ANC_RESP_SEND_TIME_US 300
#define ANC_RESP_DELAY_US 500
#define TAG_FINAL_DELAY_US (\
			TAG_SETTINGS_SETUP_US +\
			TAG_SEND_POLL_DELAY_US +\
			ANC_RESP_DELAY_US +\
			ANC_RESP_SEND_TIME_US * (NUM_ANCHORS+1)\
			)
#define ANC_RX_AND_PROC_TAG_FINAL_US 2400

#ifdef ANC_FINAL_PERCENTILE_ONLY
#define ANC_FINAL_BUFFER_FILL_TIME_US 250
#define ANC_FINAL_SEND_TIME_US 300
#define RT_FINAL_PRINTF_DURATION (RTIMER_SECOND * 0.012)
#else
#define ANC_FINAL_BUFFER_FILL_TIME_US 500
#define ANC_FINAL_SEND_TIME_US 700
#define RT_FINAL_PRINTF_DURATION (RTIMER_SECOND * 0.125)
#endif

#define ANC_SETTINGS_SETUP_US 500

#define SUBSEQUENCE_PERIOD_US (\
			TAG_FINAL_DELAY_US \
			+ ANC_RX_AND_PROC_TAG_FINAL_US \
			+ ANC_SETTINGS_SETUP_US \
			)
*/

#define APP_US_TO_DEVICETIMEU32(_microsecu)\
	(\
	 (uint32_t) ( ((_microsecu) / (double) DWT_TIME_UNITS) / 1e6 )\
	)
// uint32_t delay_time = temp + (APP_US_TO_DEVICETIMEU32(TAG_SEND_POLL_DELAY_US) >> 8);
#define SPI_US_PER_BYTE 1.18
#define SPI_SLACK_US	200
#define DW_DELAY_FROM_PKT_LEN(_len)\
	(\
	 APP_US_TO_DEVICETIMEU32(SPI_US_PER_BYTE * (_len) + SPI_SLACK_US) >> 8\
	)
#define DW_DELAY_FROM_US(_us)\
	(\
	 APP_US_TO_DEVICETIMEU32(_us) >> 8\
	)


extern const double txDelayCal[11*3];

struct ieee154_header {
	uint8_t frameCtrl[2];                             //  frame control bytes 00-01
	uint8_t seqNum;                                   //  sequence_number 02
	uint8_t panID[2];                                 //  PAN ID 03-04
	uint8_t destAddr[8];
	uint8_t sourceAddr[8];
};

struct ieee154_footer {
	uint8_t fcs[2] ;                                  //  we allow space for the CRC as it is logically part of the message. However ScenSor TX calculates and adds these bytes.
};

struct pp_tag_poll  {
	struct ieee154_header header;
	uint8_t message_type;
	uint8_t roundNum;
	uint8_t subsequence;
	struct ieee154_footer footer;
} __attribute__ ((__packed__));

struct pp_anc_final {
	struct ieee154_header header;
	uint8_t message_type;
	uint8_t anchor_id;
	uint32_t dw_time_sent;
	uint64_t TOAs[NUM_MEASUREMENTS+1];
	struct ieee154_footer footer;
} __attribute__ ((__packed__));


_Static_assert(offsetof(struct pp_tag_poll, message_type) == offsetof(struct pp_anc_final, message_type),\
			"message_type field at inconsistent offsets");


uint8_t subseq_num_to_chan(uint8_t subseq_num, bool return_channel_index);
void set_subsequence_settings(uint8_t subseq_num, int role);
int app_dw1000_init (
		int HACK_role,
		int HACK_EUI,
		void (*txcallback)(const dwt_callback_data_t *),
		void (*rxcallback)(const dwt_callback_data_t *)
		);



#ifdef DW_DEBUG
#define DEBUG_P(...)\
	printf("[%u %u]  ", global_round_num, global_subseq_num); printf(__VA_ARGS__)
#else
#define DEBUG_P(...)
#endif

#ifdef DW_DEBUG
#define PDBL(dbl)\
{\
	int64_t _dbl1000 = (int64_t)(dbl*1000);\
	printf(# dbl ": %lld.%lld\r\n", _dbl1000/1000, _dbl1000%1000);\
}
#else
#define PDBL(dbl)
#endif

#define MIN(_a, _b) ((_a < _b) ? (_a) : (_b))
#define MAX(_a, _b) ((_a > _b) ? (_a) : (_b))

#define DEBUG_B6_LOW\
	GPIO_CLR_PIN(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(6))
#define DEBUG_B6_HIGH\
	GPIO_SET_PIN(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(6))
#define DEBUG_B6_INIT\
	GPIO_SET_OUTPUT(GPIO_PORT_TO_BASE(GPIO_B_NUM), GPIO_PIN_MASK(6))

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

#endif // POLYPOINT_COMMON_H
