#ifndef __OPERATION_API_H
#define __OPERATION_API_H

#include "dw1000_tag.h"

void run_tag (dw1000_report_mode_e report_mode,
              dw1000_update_mode_e update_mode,
              uint8_t update_rate);

dw1000_report_mode_e main_get_report_mode ();
void main_set_ranges (int32_t* ranges_millimeters, anchor_responses_t* anchor_responses);

#endif
