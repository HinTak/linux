#ifdef CONFIG_PHYS_OFFSET
zreladdr-y   := $(shell perl -e 'printf "0x%08x", $(CONFIG_PHYS_OFFSET) + $(TEXT_OFFSET)')
#else
zreladdr-y   := 0x10008000
#endif
