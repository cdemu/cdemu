VHBA_VERSION := 20210418

KERNELRELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNELRELEASE)/build
PWD ?= $(shell pwd)

obj-m := vhba.o
ccflags-y := -DVHBA_VERSION=\"$(VHBA_VERSION)\" -Werror

default: modules
install: modules_install

modules modules_install clean:
	$(MAKE) -C $(KDIR) M=$(PWD) $@
