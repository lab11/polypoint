#ifndef __DW1000_H
#define __DW1000_H

typedef enum {
	DW1000_INIT_DONE,
} dw1000_cb_e;

typedef enum {
	DW1000_NO_ERR = 0,
	DW1000_COMM_ERR,
} dw1000_err_e;

// gets called with event that just finished and an error code
typedef void (*dw1000_callback)(dw1000_cb_e, dw1000_err_e);


void dw1000_init(dw1000_callback cb);
void dw1000_reset();
void dw1000_choose_antenna(uint8_t antenna_number);
void dw1000_populate_eui(uint8_t *eui_buf, uint8_t id);

#endif
