######################################################################
# used by LKM

CROSS_COMPILE ?= ""
#CROSS_COMPLIE ?= armv7l-tizen-linux-gnueabi-
export ARCH=arm
#TOOLCHAINDIR=$(shell readlink -f `which $(CROSS_COMPILE)gcc` | egrep -o -e '/.*/' )
TOOLCHAINDIR=/usr/bin

ifeq ($(TOOLCHAIN_GCC_VERSION), -DGCC45)
TOOLCHAIN_INC_PATH=$(TOOLCHAINDIR)/../lib/gcc/armv7l-tizen-linux-gnueabi/4.5.3/include
else ifeq ($(TOOLCHAIN_GCC_VERSION), -DGCC47)
TOOLCHAIN_INC_PATH=$(TOOLCHAINDIR)/../lib/gcc/armv7l-tizen-linux-gnueabi/4.7.3/include
else ifeq ($(TOOLCHAIN_GCC_VERSION), -DGCC48)
TOOLCHAIN_INC_PATH=$(TOOLCHAINDIR)/../lib/gcc/armv7l-tizen-linux-gnueabi/4.8.3/include
else ifeq ($(TOOLCHAIN_GCC_VERSION), -DGCC49)
TOOLCHAIN_INC_PATH=$(TOOLCHAINDIR)/../lib/gcc/armv7l-tizen-linux-gnueabi/4.9.2/include
endif

include $(KDIR)/.config

ifneq (,$(filter y, $(CONFIG_ARCH_SDP1404) $(CONFIG_ARCH_SDP1406)))
CHIP_FLAG = -mcpu=cortex-a15
else
CHIP_FLAG = -march=armv7-a
endif
ifeq ($(CONFIG_ARCH_NVT_V7),y)
ifeq ($(RELEASE_MODE), perf)
EXTRACTED_KERNEL_CFLAGS=-nostdinc -isystem $(TOOLCHAIN_INC_PATH) -I$(KDIR)/arch/arm/include -I$(KDIR)/arch/arm/include/generated -I$(KDIR)/include -I$(KDIR)/arch/arm/include/uapi -I$(KDIR)/arch/arm/include/generated/uapi -I$(KDIR)/include/uapi -I$(KDIR)/include/generated/uapi -include $(KDIR)/include/linux/kconfig.h -D__KERNEL__ -mlittle-endian -I$(KDIR)/arch/arm/mach-sdp/include -I$(KDIR)/arch/arm/plat-sdp/include -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -Wno-format-security -fno-delete-null-pointer-checks -fno-function-sections -O2 -fno-dwarf2-cfi-asm -fno-omit-frame-pointer -mapcs -mno-sched-prolog -mabi=aapcs-linux -mno-thumb-interwork -marm -D__LINUX_ARM_ARCH__=7 $(CHIP_FLAG) -Uarm -Wframe-larger-than=1024 -fno-stack-protector -fno-omit-frame-pointer -fno-optimize-sibling-calls -g -Wdeclaration-after-statement -Wno-pointer-sign -fno-strict-overflow -fconserve-stack -DCC_HAVE_ASM_GOTO    -DMODULE -mlong-calls -Wno-write-strings -fno-exceptions
else ifeq ($(RELEASE_MODE), release)
EXTRACTED_KERNEL_CFLAGS=-nostdinc -isystem $(TOOLCHAIN_INC_PATH) -I$(KDIR)/arch/arm/include -I$(KDIR)/arch/arm/include/generated  -I$(KDIR)/include -I$(KDIR)/arch/arm/include/uapi -I$(KDIR)/arch/arm/include/generated/uapi -I$(KDIR)/include/uapi -I$(KDIR)/include/generated/uapi -include $(KDIR)/include/linux/kconfig.h -D__KERNEL__ -mlittle-endian -I$(KDIR)/arch/arm/mach-sdp/include -I$(KDIR)/arch/arm/plat-sdp/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -Wno-format-security -fno-delete-null-pointer-checks -fno-function-sections -O2 -fno-dwarf2-cfi-asm -fno-omit-frame-pointer -mapcs -mno-sched-prolog -mabi=aapcs-linux -mno-thumb-interwork -marm -D__LINUX_ARM_ARCH__=7 $(CHIP_FLAG) -Uarm -Wframe-larger-than=1024 -fno-stack-protector -Wno-unused-but-set-variable -fno-omit-frame-pointer -fno-optimize-sibling-calls -g -Wdeclaration-after-statement -Wno-pointer-sign -fno-strict-overflow -fconserve-stack -DCC_HAVE_ASM_GOTO -DMODULE -mlong-calls -Wno-write-strings -fno-exceptions
else ifeq ($(RELEASE_MODE), debug)
EXTRACTED_KERNEL_CFLAGS=-nostdinc -isystem $(TOOLCHAIN_INC_PATH) -I$(KDIR)/arch/arm/include -I$(KDIR)/arch/arm/include/generated -I$(KDIR)/include -I$(KDIR)/arch/arm/include/uapi -I$(KDIR)/arch/arm/include/generated/uapi -I$(KDIR)/include/uapi -I$(KDIR)/include/generated/uapi -include $(KDIR)/include/linux/kconfig.h -I$(KDIR)/kernel/kdebugd -I$(KDIR)/kernel/kdebugd/include -I$(KDIR)/kernel/kdebugd/include/kdebugd -I$(KDIR)/kernel/kdebugd/aop -I$(KDIR)/kernel/kdebugd/elf -I$(KDIR)/kernel/kdebugd/elf/dem_src -I$(KDIR)/kernel/t2ddebugd -I$(KDIR)/kernel/t2ddebugd/include -I$(KDIR)/kernel/t2ddebugd/include/t2ddebugd -D__KERNEL__ -mlittle-endian -I$(KDIR)/arch/arm/mach-sdp/include -I$(KDIR)/arch/arm/plat-sdp/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -Wno-format-security -fno-delete-null-pointer-checks -fno-function-sections -O2 -fno-dwarf2-cfi-asm -fno-omit-frame-pointer -mapcs -mno-sched-prolog -fstack-protector -mabi=aapcs-linux -mno-thumb-interwork -marm -D__LINUX_ARM_ARCH__=7 $(CHIP_FLAG) -Uarm -Wframe-larger-than=1024 -Wno-unused-but-set-variable -fno-omit-frame-pointer -fno-optimize-sibling-calls -g $(if $(CONFIG_FUNCTION_TRACER), -pg)  -Wdeclaration-after-statement -Wno-pointer-sign -fno-strict-overflow -fconserve-stack -DCC_HAVE_ASM_GOTO  -DMODULE -mlong-calls
endif
else
ifeq ($(RELEASE_MODE), perf)
EXTRACTED_KERNEL_CFLAGS=-nostdinc -isystem $(TOOLCHAIN_INC_PATH) -I$(KDIR)/arch/arm/include -I$(KDIR)/arch/arm/include/generated -I$(KDIR)/include -I$(KDIR)/arch/arm/include/uapi -I$(KDIR)/arch/arm/include/generated/uapi -I$(KDIR)/include/uapi -I$(KDIR)/include/generated/uapi -include $(KDIR)/include/linux/kconfig.h -D__KERNEL__ -mlittle-endian -I$(KDIR)/arch/arm/mach-sdp/include -I$(KDIR)/arch/arm/plat-sdp/include -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -Wno-format-security -fno-delete-null-pointer-checks -fno-function-sections -O2 -fno-dwarf2-cfi-asm -fno-omit-frame-pointer -mapcs -mno-sched-prolog -mabi=aapcs-linux -mno-thumb-interwork -marm -D__LINUX_ARM_ARCH__=7 $(CHIP_FLAG) -Uarm -Wframe-larger-than=1024 -fno-stack-protector -fno-omit-frame-pointer -fno-optimize-sibling-calls -g -Wdeclaration-after-statement -Wno-pointer-sign -fno-strict-overflow -fconserve-stack -DCC_HAVE_ASM_GOTO    -DMODULE -mlong-calls -Wno-write-strings -fno-exceptions
else ifeq ($(RELEASE_MODE), release)
EXTRACTED_KERNEL_CFLAGS=-nostdinc -isystem $(TOOLCHAIN_INC_PATH) -I$(KDIR)/arch/arm/include -I$(KDIR)/arch/arm/include/generated  -I$(KDIR)/include -I$(KDIR)/arch/arm/include/uapi -I$(KDIR)/arch/arm/include/generated/uapi -I$(KDIR)/include/uapi -I$(KDIR)/include/generated/uapi -include $(KDIR)/include/linux/kconfig.h -D__KERNEL__ -mlittle-endian -I$(KDIR)/arch/arm/mach-sdp/include -I$(KDIR)/arch/arm/plat-sdp/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -Wno-format-security -fno-delete-null-pointer-checks -fno-function-sections -O2 -fno-dwarf2-cfi-asm -fno-omit-frame-pointer -mapcs -mno-sched-prolog -mabi=aapcs-linux -mno-thumb-interwork -marm -D__LINUX_ARM_ARCH__=7 $(CHIP_FLAG) -Uarm -Wframe-larger-than=1024 -fno-stack-protector -Wno-unused-but-set-variable -fno-omit-frame-pointer -fno-optimize-sibling-calls -g -Wdeclaration-after-statement -Wno-pointer-sign -fno-strict-overflow -fconserve-stack -DCC_HAVE_ASM_GOTO -DMODULE -mlong-calls -Wno-write-strings -fno-exceptions
else ifeq ($(RELEASE_MODE), debug)
EXTRACTED_KERNEL_CFLAGS=-nostdinc -isystem $(TOOLCHAIN_INC_PATH) -I$(KDIR)/arch/arm/include -I$(KDIR)/arch/arm/include/generated -I$(KDIR)/include -I$(KDIR)/arch/arm/include/uapi -I$(KDIR)/arch/arm/include/generated/uapi -I$(KDIR)/include/uapi -I$(KDIR)/include/generated/uapi -include $(KDIR)/include/linux/kconfig.h -I$(KDIR)/kernel/kdebugd -I$(KDIR)/kernel/kdebugd/include -I$(KDIR)/kernel/kdebugd/include/kdebugd -I$(KDIR)/kernel/kdebugd/aop -I$(KDIR)/kernel/kdebugd/elf -I$(KDIR)/kernel/kdebugd/elf/dem_src -I$(KDIR)/kernel/t2ddebugd -I$(KDIR)/kernel/t2ddebugd/include -I$(KDIR)/kernel/t2ddebugd/include/t2ddebugd -D__KERNEL__ -mlittle-endian -I$(KDIR)/arch/arm/mach-sdp/include -I$(KDIR)/arch/arm/plat-sdp/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -Wno-format-security -fno-delete-null-pointer-checks -fno-function-sections -O2 -fno-dwarf2-cfi-asm -fno-omit-frame-pointer -mapcs -mno-sched-prolog -fstack-protector -mabi=aapcs-linux -mno-thumb-interwork -marm -D__LINUX_ARM_ARCH__=7 $(CHIP_FLAG) -Uarm -Wframe-larger-than=1024 -Wno-unused-but-set-variable -fno-omit-frame-pointer -fno-optimize-sibling-calls -g $(if $(CONFIG_FUNCTION_TRACER), -pg)  -Wdeclaration-after-statement -Wno-pointer-sign -fno-strict-overflow -fconserve-stack -DCC_HAVE_ASM_GOTO  -DMODULE -mlong-calls
endif

endif

EXTRACTED_KERNEL_CFLAGS+=-I$(KDIR)/kernel/t2ddebugd/include -msoft-float

CPP_CFLAGS=-nostartfiles -nodefaultlibs -nostdlib -fpermissive -fno-rtti -fno-exceptions -nostdinc++ -msoft-float
