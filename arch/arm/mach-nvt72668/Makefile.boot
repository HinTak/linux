
ifeq ($(CONFIG_MACH_NT14U), y)
zreladdr-y   := 0x04408000
endif

ifeq ($(CONFIG_MACH_NT14M), y)
zreladdr-y   := 0x10a08000
endif

#params_phys-y   := 0x08000000
#initrd_phys-y   := 0x26000000

