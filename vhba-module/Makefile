VHBA_VERSION := 20211023

KERNELRELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNELRELEASE)/build
PWD ?= $(shell pwd)

obj-m := vhba.o

ifneq (,$(findstring next, $(KERNELRELEASE)))
ccflags-y := -DVHBA_VERSION=\"$(VHBA_VERSION)\" -Werror -DLINUX_NEXT=1
else
ccflags-y := -DVHBA_VERSION=\"$(VHBA_VERSION)\" -Werror
endif

default: modules
install: modules_install

modules modules_install clean:
	$(MAKE) -C $(KDIR) M=$(PWD) $@
