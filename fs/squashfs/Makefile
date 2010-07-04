#
# Makefile for the linux squashfs routines.
#

-include /tmp/sqvars.mk

obj-$(CONFIG_SQUASHFS) += unlzma.o sqlzma.o squashfs.o
unlzma-y := module.o
sqlzma-y := uncomp.o
squashfs-y += inode.o
squashfs-y += squashfs2_0.o
