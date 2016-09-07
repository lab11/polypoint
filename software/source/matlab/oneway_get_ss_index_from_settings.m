function ret = oneway_get_ss_index_from_settings(anchor_antenna_index, window_num)

global NUM_RANGING_CHANNELS;

tag_antenna_index = 0;
channel_index = mod(window_num, NUM_RANGING_CHANNELS);
ret = antenna_and_channel_to_subsequence_number(tag_antenna_index, anchor_antenna_index, channel_index);
