#
# Makefile for the linux emmcfs-filesystem routines.
#

obj-$(CONFIG_EMMCFS_FS) += emmcfs.o

emmcfs-y	:= btree.o bnode.o cattree.o file.o inode.o \
		   options.o super.o fsm.o fsm_btree.o \
		   extents.o snapshot.o orphan.o data.o procfs.o xattr.o

emmcfs-$(CONFIG_EMMCFS_DEBUG)		+= debug.o

GIT_BRANCH = v.468dev-2013_11_05
GIT_REV_HASH = 30cb0818bb8f3829c89e272d832d9d114d6baf7e
VERSION = v.468dev-2013_11_05

ifneq ($(GIT_BRANCH),)
CFLAGS_super.o				+= -DEMMCFS_GIT_BRANCH=\"$(GIT_BRANCH)\"
CFLAGS_procfs.o				+= -DEMMCFS_GIT_BRANCH=\"$(GIT_BRANCH)\"
endif
ifneq ($(GIT_REV_HASH),)
CFLAGS_super.o				+= -DEMMCFS_GIT_REV_HASH=\"$(GIT_REV_HASH)\"
CFLAGS_procfs.o				+= -DEMMCFS_GIT_REV_HASH=\"$(GIT_REV_HASH)\"
endif
ifneq ($(VERSION),)
CFLAGS_super.o				+= -DEMMCFS_VERSION=\"$(VERSION)\"
CFLAGS_procfs.o				+= -DEMMCFS_VERSION=\"$(VERSION)\"
endif
