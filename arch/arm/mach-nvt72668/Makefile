#
# Makefile for the linux kernel.
#
ccflags-$(CONFIG_ARCH_MULTIPLATFORM) := -I$(srctree)/$(src)/include \
	-I$(srctree)/arch/arm/plat-novatek/include

obj-y					:= clk.o v2m.o ct-ca9x4.o nvt_ahb_status.o nvt_micom.o
obj-$(CONFIG_SMP)			+= platsmp.o
obj-$(CONFIG_HOTPLUG_CPU)		+= hotplug.o
obj-$(CONFIG_NVT_MMC_IF)		+= nvt_mmc_dev.o
obj-$(CONFIG_USB_NT72668_HCD)		+= nvt_usb_dev.o
obj-$(CONFIG_SUSPEND)	        += nvt_sleep.o nvt_pm.o
