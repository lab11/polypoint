PolyPoint
=========

PolyPoint is a system for using ultra-wideband RF time-of-flight ranging to perform indoor localization.
It leverages the DecaWave DW1000 for UWB packet transmission and timestamping, performs
ranging in a Contiki application, and calculates location from these ranges.

Name
----

The name PolyPoint comes from the use of many polygons and shapes in the prototype design and the
desire to pinpoint where users are with the system.

Hardware
--------

The hardware is currently one PCB that contains the DW1000, three UWB antennas, and an RF
switch. The DW1000 is a SPI peripheral and we've been using an Atum as the controller.

Software
--------

The software that runs on the Atum is a Contiki application. For this you need the Atum repo
and the Contiki repo.

    git clone git@github.com:lab11/atum
    git clone git@github.com:contiki-os/contiki
    
You also need the DecaWave library for controlling the DW1000. 
To get this you will need to download the 'EVK1000 Software Package' from the [DecaWave website](http://www.decawave.com/support/software).
Exract and copy the contents of `EVK SW Package/DecaRanging ARM based/Source UNDER LICENSE ONLY/November'14/DecaRangingEVB1000_MP_rev2p35/src/decadriver` into PolyPoint's `software/dw1000-driver` folder.

NOTE: Software is currently incompatible with newest contiki SPI drivers.  Use contiki commit 038ee9f82b4dc57eeb86f43af48d6491e788c7ed for now.
