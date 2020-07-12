#
# Makefile for the SDP network device drivers.
#

#MAC
ifeq ($(CONFIG_SDP_MAC),y)
obj-y				+= sdp_mac_phy.o
endif

ifeq ($(CONFIG_SDP_MAC),m)
obj-y				+= sdp_mac_phy.o
endif

obj-$(CONFIG_SDP_MAC)		+= sdp_mac.o
