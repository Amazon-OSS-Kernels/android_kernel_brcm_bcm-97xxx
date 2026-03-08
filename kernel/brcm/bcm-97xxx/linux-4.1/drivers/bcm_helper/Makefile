# GNUmakefile for Linux
# Broadcom 802.11abg Networking Device Driver
#
# Copyright (C) 2017, Broadcom Corporation. All Rights Reserved.
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
# OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
# CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
# Usage:
#
# make [LINUXDIR=<path>]
#      [CROSS_COMPILE=<path>]
#
# Since WL driver uses a symbol exported by bcm_helper, it is imperative that
# bcm_helper is built before WL driver build and loaded before the WL driver
#
# Makefile fragment for bcm_helper Module
#

ifneq ($(KERNELRELEASE),)
# To build either as built-in or external bcm_helper module
#  located inside kernel subdirs

ccflags-y :=

bcm_helper-objs	=

obj-$(CONFIG_BCM_HELPER) += bcm_helper.o

else # KERNELRELEASE
# To build external bcm_helper module located outside kernel subdirs
#
# USAGE:
# make FRANK=/path/to/frank/repo
# or
# make LINUXDIR=/path/to/linux/kernel
# or
# make LINUXDIR=/path/to/linux/kernel LINUX_OUT=/path/to/linux/build
#
export CONFIG_BCM_HELPER ?= m

#FRANK ?= /path/to/frank/repo
ifndef FRANK
  $(warning Missing FRANK repo path ... )
ifndef LINUXDIR
  $(warning Missing LINUXDIR kernel path ... )
endif
endif
TCDIR := $(FRANK)/prebuilts/gcc/linux-x86/arm/stb/stbgcc-4.8-1.5/bin
KSRC := $(FRANK)/kernel/brcm/bcm-97xxx/linux-4.1/
KOBJ := $(FRANK)/out/target/product/frank/obj/KERNEL_OBJ/
PWD  := $(shell pwd)

CROSS_COMPILE ?= $(TCDIR)/arm-linux-gnueabihf-
ARCH  ?= arm

LINUXDIR  ?= $(KSRC)
ifneq ($(findstring $(LINUXDIR),$(KSRC)),)
LINUX_OUT ?= $(KOBJ)
endif

OPTIONS:= ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)
ifdef LINUX_OUT
OPTIONS += O=$(LINUX_OUT)
endif

obj-m += bcm_helper.o

default:
	$(MAKE) -C $(LINUXDIR) $(OPTIONS) M=$(PWD) modules

clean:
	$(MAKE) -C $(LINUXDIR) $(OPTIONS) M=$(PWD) clean

endif # KERNELRELEASE
