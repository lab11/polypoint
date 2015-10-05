PROJECT_NAME = $(shell basename "$(realpath ./)")

#List all of the application source files that need to be compiled
#############################################
APPLICATION_SRCS = $(notdir $(wildcard ./*.c))
APPLICATION_SRCS += led.c
APPLICATION_SRCS += timer.c
APPLICATION_SRCS += prng.c
APPLICATION_SRCS += delay.c

APPLICATION_SRCS += stm32f0xx_gpio.c
APPLICATION_SRCS += stm32f0xx_rcc.c
APPLICATION_SRCS += stm32f0xx_i2c.c
APPLICATION_SRCS += stm32f0xx_dma.c
APPLICATION_SRCS += stm32f0xx_misc.c
APPLICATION_SRCS += stm32f0xx_tim.c
APPLICATION_SRCS += stm32f0xx_spi.c
APPLICATION_SRCS += stm32f0xx_pwr.c
APPLICATION_SRCS += stm32f0xx_exti.c
APPLICATION_SRCS += stm32f0xx_syscfg.c

APPLICATION_SRCS += stm32f0xx_i2c_cpal.c
APPLICATION_SRCS += stm32f0xx_i2c_cpal_hal.c

APPLICATION_SRCS += deca_device.c
APPLICATION_SRCS += deca_params_init.c

CFLAGS += -DBOARD=TRIPOINT
LDFLAGS += -lm

#The exact STM part number All CAPS
############################################
DEVICE ?= STM32F031G6
DEVICE_FAMILY ?= STM32F031

## HACK FOR stm32f031
## Need to define this so that CPAL will compile.
## It is never used, however.
ifeq ($(DEVICE_FAMILY), STM32F031)
CFLAGS += -DI2C2_IRQn=24
endif

CFLAGS += -Wall

#The install locations of the STM Standard library
#################################################
LIBRARY_START_PATH ?= ../stm32f0-base/sdk/stm32f0xx-1.5.0/

#The location of the gcc setup template
################################################
TEMPLATE_PATH ?= ../stm32f0-base/stm32f0xx-gcc-setup/template/

#Any places that you put custom header files
###############################################
LIBRARY_PATHS += ../include ../dw1000-driver

#Any places that you put custom source files
##############################################
SOURCE_PATHS += ../source ../dw1000-driver

ifndef LIBRARY_START_PATH
$(error An LIBRARY_START_PATH must be defined. Edit Makefile to define a valid LIBRARY_PATH)
endif

# Set an ID for programming the nodes EUI
ifeq ($(ID), )
ID = c0:98:e5:50:50:44:50:FF
endif

# This creates a .bin file for loading the calibration values
# into the flash of the TriPoint.
USER_FLASH_COMMANDS = $(shell ./insert_calibration.py $(ID))

CFLAGS += -Os
GDB_PORT_NUMBER = 2331

include $(TEMPLATE_PATH)Makefile
