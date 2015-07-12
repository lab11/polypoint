#ifndef __DW1000_H
#define __DW1000_H

typedef enum {
	DW1000_INIT_DONE,
} dw1000_cb_e;

// gets called with event that just finished and an error code
typedef void (*dw1000_callback)(dw1000_cb_e, uint32_t);


void dw1000_init (dw1000_callback cb);


#endif
