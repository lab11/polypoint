
//
// Code for the DW1000 that is specific to this platform
//


void usleep (int microseconds) {
	__IO uint32_t index = 0;
	for(index = (34000 * microseconds); index != 0; index--) { }
}


