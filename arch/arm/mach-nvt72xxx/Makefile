#
# Makefile for the linux kernel.
#
obj-y					:= clk.o v2m.o
obj-$(CONFIG_OF)			+= board-nvt-dt.o
obj-$(CONFIG_SMP)			+= platsmp.o
obj-$(CONFIG_HOTPLUG_CPU)		+= hotplug.o
obj-$(CONFIG_SUSPEND)			+= nvt_sleep.o nvt_pm.o
ifeq ($(CONFIG_NVT_HW_CLOCK),y)
obj-y		+= nvt_hwclk.o
else ifeq ($(CONFIG_NVT_CA53_HW_CLOCK),y)
obj-y		+= nvt_hwclk.o
endif
