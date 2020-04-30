obj-y	+= common.o
obj-y	+= sdp_hwmem.o
obj-y	+= sdp_kade.o

#SDP UNZIP
obj-$(CONFIG_SDP_UNZIP)         += sdp_unzip.o
obj-$(CONFIG_SDP_UNZIP_TEST)            += sdp_unzip_test.o
obj-$(CONFIG_SDP_UNZIP_HWDCOMP_VDFS)	+= sdp_unzip_vdfsapi.o

obj-$(CONFIG_SDP_MICOM_CECIF)			+= sdp_micom_cecif.o
obj-$(CONFIG_SDP_MICOM_CECIF_TEST)		+= sdp_micom_cecif_test.o
obj-$(CONFIG_SDP_MICOM_PINCTRL)	+= sdp_micom_pinctrl.o

obj-$(CONFIG_SDP_MICOM_IRB) 			+= sdp_micom_irb.o


#SPI
obj-$(CONFIG_SDP_SPI)           += sdp_spi.o
obj-$(CONFIG_SDP_SPIDEV)        += sdp_spidev.o

obj-$(CONFIG_SDP_AVS)		+= sdp_avs.o
obj-$(CONFIG_SDP_THERMAL)	+= sdp_thermal.o
obj-$(CONFIG_SDP_BUSERR)	+= sdp_buserr.o
obj-$(CONFIG_SDP_BUSMON)	+= sdp_busmon.o
obj-$(CONFIG_SDP_RUNTIME_PM) += sdp_runtime_pm.o
obj-$(CONFIG_SDP_KVALUE) 	+= sdp_kvalue.o

# smem allocator: y or n
obj-$(CONFIG_SDP_SMEM)		+= smem.o
obj-$(CONFIG_SDP_SMEM)		+= smem_heap_carveout.o smem_heap_cma.o smem_heap_cmapool.o
obj-$(CONFIG_SDP_SMEM_CACHE)	+= smem_cache.o
obj-$(CONFIG_SDP_SMEM_TEST) 	+= smem_test.o

obj-$(CONFIG_SDP_BUILTIN_SERDES)	+= sdp_serdes.o

