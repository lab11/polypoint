#include <stddef.h>
#include <string.h>

#include "timer.h"

#include "firmware.h"
#include "dw1000.h"
#include "host_interface.h"
#include "oneway_common.h"
#include "oneway_tag.h"
#include "oneway_anchor.h"

// All of the configuration passed to us by the host for how this application
// should operate.
static oneway_config_t _config;
// Our local reference to the timer for all of the high-level application
// code.
static stm_timer_t* _app_timer;

// Configure the RF channels to use. This is just a mapping from 0..2 to
// the actual RF channel numbers the DW1000 uses.
static const uint8_t channel_index_to_channel_rf_number[NUM_RANGING_CHANNELS] = {
	1, 4, 3
};

// Buffer of anchor IDs and ranges to the anchor.
// Long enough to hold an anchor id followed by the range, plus the number
// of ranges
uint8_t _anchor_ids_ranges[(MAX_NUM_ANCHOR_RESPONSES*(EUI_LEN+sizeof(int32_t)))+1];

static void *_scratchspace_ptr;

// Called by periodic timer
static void tag_execute_range_callback () {
	dw1000_err_e err;

	err = oneway_tag_start_ranging_event();
	if (err == DW1000_BUSY) {
		// TODO: get return value from this function and slow the timer if
		// we starting ranging events too quickly.
	} else if (err == DW1000_WAKEUP_ERR) {
		// DW1000 apparently was in sleep and didn't come back.
		// Not sure why, but we need to reset at this point.
		polypoint_reset();
	}
}

// This sets the settings for this node and initializes the node.
void oneway_configure (oneway_config_t* config, stm_timer_t* app_timer, void *app_scratchspace) {
	_scratchspace_ptr = app_scratchspace;

	// Save the settings
	memcpy(&_config, config, sizeof(oneway_config_t));

	// Save the application timer for use by this application
	//_app_timer = app_timer;

	// Make sure the DW1000 is awake before trying to do anything.
	dw1000_wakeup();

	// Oneway ranging requires glossy synchronization, so let's enable that now
	glossy_init(_config.my_glossy_role);

	// Now init based on role
	if (_config.my_role == TAG) {
		oneway_tag_init(_scratchspace_ptr);
	} else if (_config.my_role == ANCHOR) {
		oneway_anchor_init(_scratchspace_ptr);
	}
}

// Kick off the application
void oneway_start () {
	dw1000_err_e err;

	if (_config.my_role == ANCHOR) {
		// Start the anchor state machine. The app doesn't have to do anything
		// for this, it just runs.
		err = oneway_anchor_start();
		if (err == DW1000_WAKEUP_ERR) {
			polypoint_reset();
		}

	} else if (_config.my_role == TAG) {

		if (_config.update_mode == ONEWAY_UPDATE_MODE_PERIODIC) {
			// Host requested periodic updates.
			// Set the timer to fire at the correct rate. Multiply by 1000000 to
			// get microseconds, then divide by 10 because update_rate is in
			// tenths of hertz.
			uint32_t period = (((uint32_t) _config.update_rate) * 1000000) / 10;
			// Check if we are configured to sleep between ranges. If so,
			// we need to take wakeup time into account.
			if (_config.sleep_mode && period > DW1000_WAKEUP_DELAY_US) {
				period -= DW1000_WAKEUP_DELAY_US;
			}
			//timer_start(_app_timer, period, tag_execute_range_callback);

		} else if (_config.update_mode == ONEWAY_UPDATE_MODE_DEMAND) {
			// Just wait for the host to request a ranging event
			// over the host interface.
		}

		//
		// TODO: implement selecting between reporting ranges and locations
		//
	}
}

// Stop the oneway application
void oneway_stop () {
	if (_config.my_role == TAG) {
		if (_config.update_mode == ONEWAY_UPDATE_MODE_PERIODIC) {
			//timer_stop(_app_timer);
		}
		oneway_tag_stop();
	} else if (_config.my_role == ANCHOR) {
		oneway_anchor_stop();
	}
}

// The whole DW1000 reset, so we need to get this app running again
void oneway_reset () {
	// Start by initing based on role
	if (_config.my_role == TAG) {
		oneway_tag_init(_scratchspace_ptr);
	} else if (_config.my_role == ANCHOR) {
		oneway_anchor_init(_scratchspace_ptr);
	}
}

// Function to perform an on-demand ranging event
void oneway_do_range () {
	dw1000_err_e err;

	// If we are not a tag, or we are not
	// in on-demand ranging mode, don't do anything.
	if (_config.my_role != TAG ||
	    _config.update_mode != ONEWAY_UPDATE_MODE_DEMAND) {
		return;
	}

	// TODO: this does return an error if we are already ranging.
	err = oneway_tag_start_ranging_event();
	if (err == DW1000_WAKEUP_ERR) {
		polypoint_reset();
	}
}

// Return a pointer to the application configuration settings
oneway_config_t* oneway_get_config () {
	return &_config;
}

// Record ranges that the tag found.
void oneway_set_ranges (int32_t* ranges_millimeters, anchor_responses_t* anchor_responses) {
	uint8_t buffer_index = 1;
	uint8_t num_anchor_ranges = 0;

	// Iterate through all ranges and copy the correct data into the ranges buffer.
	for (uint8_t i=0; i<MAX_NUM_ANCHOR_RESPONSES; i++) {
		if (ranges_millimeters[i] != INT32_MAX) {
			// This is a valid range
			memcpy(_anchor_ids_ranges+buffer_index, anchor_responses[i].anchor_addr, EUI_LEN);
			buffer_index += EUI_LEN;
			memcpy(_anchor_ids_ranges+buffer_index, &ranges_millimeters[i], sizeof(int32_t));
			buffer_index += sizeof(int32_t);
			num_anchor_ranges++;
		}
	}

	// Set the first byte as the number of ranges
	_anchor_ids_ranges[0] = num_anchor_ranges;

	// Now let the host know so it can do something with the ranges.
	host_interface_notify_ranges(_anchor_ids_ranges, (num_anchor_ranges*(EUI_LEN+sizeof(int32_t)))+1);
}


/******************************************************************************/
// Ranging Protocol Algorithm Functions
/******************************************************************************/

// Break this out into two functions.
// (Mostly needed for calibration purposes.)
static uint8_t subsequence_number_to_channel_index (uint8_t subseq_num) {
	return subseq_num % NUM_RANGING_CHANNELS;;
}

// Return the RF channel to use for a given subsequence number
static uint8_t subsequence_number_to_channel (uint8_t subseq_num) {
	// ALGORITHM
	// We iterate through the channels as fast as possible. We do this to
	// find anchors that may not be listening on the first channel as quickly
	// as possible so that they can join the sequence as early as possible. This
	// increases the number of successful packet transmissions and increases
	// ranging accuracy.
	uint8_t channel_index = subsequence_number_to_channel_index(subseq_num);
	return channel_index_to_channel_rf_number[channel_index];
}

// Return the Antenna index to use for a given subsequence number
uint8_t oneway_subsequence_number_to_antenna (dw1000_role_e role, uint8_t subseq_num) {
	// ALGORITHM
	// We must rotate the anchor and tag antennas differently so the same
	// ones don't always overlap. This should also be different from the
	// channel sequence. This math is a little weird but somehow works out,
	// even if NUM_RANGING_CHANNELS != NUM_ANTENNAS.
	if (role == TAG) {
		return 	((subseq_num / NUM_RANGING_CHANNELS) / NUM_RANGING_CHANNELS) % NUM_ANTENNAS;
	} else if (role == ANCHOR) {
		return (subseq_num / NUM_RANGING_CHANNELS) % NUM_ANTENNAS;
	} else {
		return 0;
	}
}

// Go the opposite way and return the ss number based on the antenna used.
// Returns the LAST valid slot that matches the sequence.
static uint8_t antenna_and_channel_to_subsequence_number (uint8_t tag_antenna_index,
                                                          uint8_t anchor_antenna_index,
                                                          uint8_t channel_index) {
	uint8_t anc_offset = anchor_antenna_index * NUM_RANGING_CHANNELS;
	uint8_t tag_offset = tag_antenna_index * NUM_RANGING_CHANNELS * NUM_RANGING_CHANNELS;
	uint8_t base_offset = anc_offset + tag_offset + channel_index;

	return base_offset;
}

// Return the RF channel to use when the anchors respond to the tag
static uint8_t listening_window_number_to_channel (uint8_t window_num) {
	return channel_index_to_channel_rf_number[window_num % NUM_RANGING_CHANNELS];
}



// Update the Antenna and Channel settings to correspond with the settings
// for the given subsequence number.
//
// role:       anchor or tag
// subseq_num: where in the sequence we are
void oneway_set_ranging_broadcast_subsequence_settings (dw1000_role_e role,
                                                        uint8_t subseq_num) {
	// Stop the transceiver on the anchor. Don't know why.
	if (role == ANCHOR) {
		dwt_forcetrxoff();
	}

	// Change the channel depending on what subsequence number we're at
	dw1000_update_channel(subsequence_number_to_channel(subseq_num));

	// Change what antenna we're listening/sending on
	dw1000_choose_antenna(oneway_subsequence_number_to_antenna(role, subseq_num));
}

// Update the Antenna and Channel settings to correspond with the settings
// for the given listening window.
//
// role:       anchor or tag
// window_num: where in the listening window we are
void oneway_set_ranging_listening_window_settings (dw1000_role_e role,
                                                   uint8_t window_num,
                                                   uint8_t antenna_num) {
	// Change the channel depending on what window number we're at
	dw1000_update_channel(listening_window_number_to_channel(window_num));

	// Change what antenna we're listening/sending on
	dw1000_choose_antenna(antenna_num);
}

// Get the subsequence slot number that a particular set of settings
// (anchor antenna index, tag antenna index, channel) were used to send
// a broadcast poll message. The tag antenna index and channel are derived
// from the settings used in the listening window.
uint8_t oneway_get_ss_index_from_settings (uint8_t anchor_antenna_index,
                                           uint8_t window_num) {
	// NOTE: need something more rigorous than setting 0 here
	uint8_t tag_antenna_index = 0;
	uint8_t channel_index = listening_window_number_to_channel(window_num);

	return antenna_and_channel_to_subsequence_number(tag_antenna_index,
	                                                 anchor_antenna_index,
	                                                 channel_index);
}

// Get the TX delay for this node, given the channel value
uint64_t oneway_get_txdelay_from_subsequence (dw1000_role_e role,
                                                uint8_t subseq_num) {
	// Need to get channel and antenna to call the dw1000 function
	uint8_t channel_index = subsequence_number_to_channel_index(subseq_num);
	return dw1000_get_tx_delay(channel_index);
}

// Get the RX delay for this node, given the channel value
uint64_t oneway_get_rxdelay_from_subsequence (dw1000_role_e role,
                                                uint8_t subseq_num) {
	// Need to get channel and antenna to call the dw1000 function
	uint8_t channel_index = subsequence_number_to_channel_index(subseq_num);
	return dw1000_get_rx_delay(channel_index);
}

uint64_t oneway_get_txdelay_from_ranging_listening_window (uint8_t window_num){
	return dw1000_get_tx_delay(window_num % NUM_RANGING_CHANNELS);
}

uint64_t oneway_get_rxdelay_from_ranging_listening_window (uint8_t window_num){
	return dw1000_get_rx_delay(window_num % NUM_RANGING_CHANNELS);
}
