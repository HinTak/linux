obj-y				:= sdp_hwmem.o sdp_mem.o
obj-$(CONFIG_SDP_INTC64)	+= sdp_irq64.o
obj-$(CONFIG_SDP12XX_TIMER)	+= sdp12xx_timer.o
obj-$(CONFIG_SDP_TIMER)		+= sdp_timer.o
ifeq ($(CONFIG_OF), y)
obj-$(CONFIG_SDP_GPIO)		+= sdp_gpio_of.o
else
obj-$(CONFIG_SDP_GPIO)		+= sdp_gpio.o
endif
obj-$(CONFIG_SDP_I2C)		+= sdp_i2c.o
ifeq ($(CONFIG_OF), y)
else
obj-$(CONFIG_SERIAL_SDP) 	+= sdp_uart.o 
endif

ifeq ($(CONFIG_SDP_MAC),y)
obj-y				+= sdp_mac_phy.o
endif

ifeq ($(CONFIG_SDP_MAC),m)
obj-y				+= sdp_mac_phy.o
endif

obj-$(CONFIG_SDP_MAC)		+= sdp_mac.o

#obj-$(CONFIG_SDP_PCI)		+= pci.o sdp_pci.o 
#obj-$(CONFIG_SDP_PCIE)		+= pcie.o sdp_pcie.o 
obj-$(CONFIG_SDP_PCIE_EP)	+= sdp_pcie_ep.o 

#MMC
obj-$(CONFIG_SDP_MMC)		+= sdp_mmc.o

#SMC
obj-$(CONFIG_SDP_SMC)		+= sdp_smc.o

#SPI
obj-$(CONFIG_SDP_SPI)		+= sdp_spi.o
obj-$(CONFIG_SDP_SPIDEV)	+= sdp_spidev.o

#DMA
obj-$(CONFIG_SDP_DMA330)	+= sdp_dma330_core.o sdp_dma330.o
ifeq ($(CONFIG_SDP_DMA330_DEBUG),y)
CFLAGS_sdp_dma330_core.o	+= -DDEBUG
CFLAGS_sdp_dma330.o			+= -DDEBUG
endif
obj-$(CONFIG_SDP_GADMA)		+= sdp_gadma.o
obj-$(CONFIG_SDP_GDMA)		+= sdp_gdma.o
obj-$(CONFIG_SDP_DMADEV)	+= sdp_dmadev.o
obj-$(CONFIG_SDP_DMATEST)	+= sdp_dmatest.o

#GZIP
obj-$(CONFIG_SDP_UNZIP)		+= sdp_unzip.o

#TMU
obj-$(CONFIG_SDP_THERMAL)	+= sdp_tmu.o
obj-$(CONFIG_SDP1304_THERMAL)	+= sdp1304_tmu.o
obj-$(CONFIG_SDP1302_THERMAL)	+= sdp1302_tmu.o
obj-$(CONFIG_SDP1307_THERMAL)	+= sdp1307_tmu.o