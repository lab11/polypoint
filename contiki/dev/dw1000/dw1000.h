#ifndef _DW1000_H_
#define _DW1000_H_

void dw1000_init();
void dw1000_reset ();
void dw1000_choose_antenna (uint8_t antenna_number);
void dw1000_populate_eui (uint8_t *eui_buf, uint8_t id);


#endif
