#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -Werror
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat

obj-$(CONFIG_USB_MTK_HDRC) += usb20/
obj-$(CONFIG_MTK_USBFSH) += usb11/
obj-$(CONFIG_MTK_BTIF) += btif/
obj-$(CONFIG_MTK_COMBO) += connectivity/
obj-$(CONFIG_MTK_PMIC)	+= power/
obj-$(CONFIG_MTK_ICE_DEBUG)	+= ice_debug/
obj-$(CONFIG_MTK_XO) += xo/
obj-y += misc/
obj-y += dummy.o
