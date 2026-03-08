#
# Makefile for the Broadcom Bluetooth HCI driver over USB.
#

EXTRA_CFLAGS += -I$(src)/include -DEXPORT_SYMTAB
ccflags-y += -D__CHECK_ENDIAN__

# BTUSB_VID and BTUSB_PID can be defined to support a single BT controller
# type. The following definitions are examples for 20702A1 chip
#EXTRA_CFLAGS += -DBTUSB_VID=0x0A5C -DBTUSB_PID=0x22BE

bcm_btusb-y := btusb.o btusb_dev.o btusb_isoc.o btusb_version.o \
		gki_buffer.o gki_klinux.o
bcm_btusb-$(CONFIG_PROC_FS) += btusb_proc.o

obj-$(CONFIG_BT_HCIBCMBTUSB)	+= bcm_btusb.o
