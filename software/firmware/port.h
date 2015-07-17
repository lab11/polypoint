#ifndef __PORT_H
#define __PORT_H

// Define function that live in dw1000.c that the decawave code uses.

void port_SPIx_clear_chip_select ();
void port_SPIx_set_chip_select ();
void setup_DW1000RSTnIRQ ();

#endif