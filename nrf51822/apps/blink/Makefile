PROJECT_NAME = $(shell basename "$(realpath ./)")

APPLICATION_SRCS = $(notdir $(wildcard ./*.c))
APPLICATION_SRCS += softdevice_handler.c
APPLICATION_SRCS += app_timer.c
APPLICATION_SRCS += led.c

DEVICE = NRF51

ifndef TARGET
	TARGET = tritag
endif

TARGET_UPPER = $(shell echo $(TARGET) | tr a-z A-Z)
BOARD = BOARD_$(TARGET_UPPER)

USE_SOFTDEVICE = s110

SDK_VERSION = 9
TEMPLATE_PATH ?= ../../nrf5x-base/nrf51-pure-gcc-setup/template/

LIBRARY_PATHS += ../../nrf5x-base/peripherals ../../nrf5x-base/devices ../../nrf5x-base/services ../../src
SOURCE_PATHS += ../../nrf5x-base/peripherals ../../nrf5x-base/devices ../../nrf5x-base/services

CFLAGS = -Os
GDB_PORT_NUMBER = 2331

include ../../Makefile.include
include $(TEMPLATE_PATH)Makefile
