TriPoint Software
=================

This firmware runs on the triangular TriPoint module and performs the basis
for the PolyPoint ranging system. Ideally, each TriPoint will ship with
this firmware already installed.

The following steps demonstrate how to re-program TriPoints.

1. Get the arm-gcc compiler for your platform: https://launchpad.net/gcc-arm-embedded

2. In the `/firmware` folder, build the software:

        make

3. Programming the STM32F0 on TriPoint requires a JLink JTAG programmer:
https://www.segger.com/jlink-general-info.html, the JLink programming software:
https://www.segger.com/jlink-software.html, and a ARM JTAG to Tag-Connect
adapter:
https://github.com/lab11/nrf51-tools/tree/master/hardware/jlink_to_tag/rev_b or
https://www.segger.com/jlink-6-pin-needle-adapter.html.

4. Program the STM32F0, and set the ID:

        make flash ID=c0:98:e5:50:50:00:00:01
        
    If you have multiple JLink boxes attached to your computer:
    
        SEGGER_SERIAL=<segger id> make flash ID=c0:98:e5:50:50:00:00:01


I2C API
-------

The interface between the host and TriPoint is described in `firmware/API.md`.

