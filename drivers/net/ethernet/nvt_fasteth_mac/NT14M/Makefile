ifeq ($(CONFIG_ARCH_NVT72668),y)
        EXTRA_CFLAGS += -I$(srctree)/arch/arm/mach-nvt72668/include/
endif

obj-$(CONFIG_NVT_FASTETH_MAC) += ntkethmac.o

ntkethmac-objs := synopGMAC_Dev.o synopGMAC_network_interface.o synopGMAC_plat.o synopGMAC_Host.o
