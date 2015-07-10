


#include "tripoint.h"
#include "led.h"


int main () {
	int err;

	led_init(LED1);
	led_init(LED2);

	err = i2c_interface_init();
	if (err) {
		// do something
	}





	return 0;
}