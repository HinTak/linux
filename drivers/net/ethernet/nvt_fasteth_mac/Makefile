obj-$(CONFIG_NVT_FASTETH_MAC) += ntkethmac.o

ntkethmac-objs := m_internal_Phy.o synopGMAC_Dev.o synopGMAC_network_interface.o synopGMAC_plat.o synopGMAC_Host.o

ifdef CONFIG_NVT_PHY_NVT72673
ntkethmac-objs += phy_673.o
endif
ifdef CONFIG_NVT_PHY_NVT72172
ntkethmac-objs += phy_172.o
endif

