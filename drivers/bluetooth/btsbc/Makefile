#
# Makefile for the Linux Bluetooth SBC Driver.
#
# Specify Include folders
EXTRA_CFLAGS = -I$(src)
EXTRA_CFLAGS += -I$(src)/inc
EXTRA_CFLAGS += -I$(src)/src

obj-$(CONFIG_BT_HCISBC) += btsbc.o

btsbc-y := src/sbc.o src/sbc_module.o src/sbc_primitives.o

