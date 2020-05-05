#
# Makefile for the linux kernel.
#

obj-y := x14_irq.o x14_arch.o x14_timer.o x14_early_printk.o 
obj-$(CONFIG_SMP)			+= platsmp.o
obj-y += platform.o

obj-$(CONFIG_MP_MSTAR_STR_BASE)+= sleep.o pm.o sleep_helper.o sleep_helper_asm.o self_mode.o
