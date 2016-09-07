function ret = antenna_and_channel_to_subsequence_number(tag_antenna_index, anchor_antenna_index, channel_index)

global NUM_RANGING_CHANNELS;

anc_offset = anchor_antenna_index * NUM_RANGING_CHANNELS;
tag_offset = tag_antenna_index * NUM_RANGING_CHANNELS * NUM_RANGING_CHANNELS;
base_offset = anc_offset + tag_offset + channel_index;

ret = base_offset;
