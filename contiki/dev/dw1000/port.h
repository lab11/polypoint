#ifndef PORT_H_
#define PORT_H_

// Define function that live in dw1000.c that the decawave code uses.

void port_SPIx_clear_chip_select ();
void port_SPIx_set_chip_select ();
void setup_DW1000RSTnIRQ ();

#endif