#
# Makefile for the linux kernel.
#
ccflags-$(CONFIG_ARCH_NVT72668) := -I$(srctree)/$(src)/include \
	-I$(srctree)/arch/arm/plat-novatek/include

obj-y					:= clk.o v2m.o nvt_micom.o
obj-$(CONFIG_SMP)			+= platsmp.o
obj-$(CONFIG_HOTPLUG_CPU)		+= hotplug.o
obj-$(CONFIG_SUSPEND)			+= nvt_sleep.o nvt_pm.o
obj-$(CONFIG_NVT_HW_CLOCK)        += nvt_hwclk.o
obj-$(CONFIG_NVT_UNZIP)                        += nvt_ungzip.o
obj-$(CONFIG_NVT_HW_SHA256) 			+= nvt_sha256.o
