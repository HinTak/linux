#
# Makefile for the linux vdfs-filesystem routines.
#

ifdef VDFS_NO_WARN
EXTRA_CFLAGS+=-Werror
endif

obj-$(CONFIG_VDFS_FS) += vdfs.o

vdfs-y	:= btree.o bnode.o cattree.o file.o inode.o \
		   options.o super.o fsm.o fsm_btree.o ioctl.o \
		   extents.o snapshot.o orphan.o data.o \
		   cattree-helper.o hlinktree.o xattr.o \
		   packtree_inode.o packtree.o small.o quota.o

vdfs-$(CONFIG_VDFS_DEBUG)		+= debug.o
vdfs-$(CONFIG_VDFS_PROC)		+= procfs.o

GIT_BRANCH = vdfs.0265-2014_12_26
GIT_REV_HASH = 5de3a608bb2429a38ba653c035cb2e336d36371a
VERSION = vdfs.0265-2014_12_26

ifneq ($(GIT_BRANCH),)
CFLAGS_super.o				+= -DVDFS_GIT_BRANCH=\"$(GIT_BRANCH)\"
CFLAGS_procfs.o				+= -DVDFS_GIT_BRANCH=\"$(GIT_BRANCH)\"
CFLAGS_packtree_inode.o			+= -DVDFS_GIT_BRANCH=\"$(GIT_BRANCH)\"
endif
ifneq ($(GIT_REV_HASH),)
CFLAGS_super.o				+= -DVDFS_GIT_REV_HASH=\"$(GIT_REV_HASH)\"
CFLAGS_procfs.o				+= -DVDFS_GIT_REV_HASH=\"$(GIT_REV_HASH)\"
CFLAGS_packtree_inode.o			+= -DVDFS_GIT_REV_HASH=\"$(GIT_REV_HASH)\"
endif
ifneq ($(VERSION),)
CFLAGS_super.o				+= -DVDFS_VERSION=\"$(VERSION)\"
CFLAGS_procfs.o				+= -DVDFS_VERSION=\"$(VERSION)\"
CFLAGS_packtree_inode.o			+= -DVDFS_VERSION=\"$(VERSION)\"
endif

