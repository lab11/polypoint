PolyPoint
=========

PolyPoint is a system for using ultra-wideband RF time-of-flight ranging to perform indoor localization.
It incorporates the DecaWave DW1000 for UWB packet transmission and timestamping
into a solder-on module that provides node-to-node ranges over an I2C interface.

Name
----

The name PolyPoint comes from the use of many polygons and shapes in the prototype design and the
desire to pinpoint where users are with the system.

Hardware
--------

The PolyPoint system is composed of several hardware pieces. At the core is the
TriPoint module which is a 1.25" on a side triangle that encompasses all of the
core ranging hardware and software. TriPoint has castellated edges and can be
soldered on to a carrier board, effectively as a ranging IC. TriTag is one such
carrier board designed to be the tag in the ranging system. It includes the
UWB antennas and a Bluetooth Low Energy radio plus a battery charging circuit.
TriTag is able to provide ranges to a mobile phone application.

### TriPoint

TriPoint includes the following components:

- DecaWave DW1000 UWB radio
- STM32F031G6U6 MCU
- RF switch

The MCU contains all the necessary code to run the DW1000 and the ranging
protocol.

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
