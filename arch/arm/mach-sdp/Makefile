obj-$(CONFIG_ARCH_SDP1202)	+= sdp_soc/sdp1202/
obj-$(CONFIG_MACH_FOXAP)	+= platform/foxap/

obj-$(CONFIG_ARCH_SDP1207)	+= sdp_soc/sdp1207/
obj-$(CONFIG_MACH_FOXB)		+= platform/foxb/

obj-$(CONFIG_ARCH_SDP1106)	+= sdp_soc/sdp1106/
obj-$(CONFIG_MACH_ECHOP)	+= platform/echop/

# Multi Core Support
obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o

#Device Tree Support
obj-$(CONFIG_OF)		+= board-sdp-dt.o board-golfs-dt.o board-golfv-dt.o common.o clk-sdp.o timer.o sdp_uart.o sdp_extint.o
obj-$(CONFIG_PM)		+= sdp_micom.o
ifeq ($(CONFIG_OF),y)
obj-$(CONFIG_PM)		+= pm.o sleep.o
endif

# DVFS Support
obj-$(CONFIG_CPU_FREQ_SDP1202)	+= cpufreq.o

# Patch Errata

