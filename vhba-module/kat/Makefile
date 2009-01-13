PACKAGE = kat-test-$(TESTNAME)

obj-m += $(TESTNAME)

ifndef KERNELRELEASE
	KERNELRELEASE := `uname -r`
endif
KDIR := /lib/modules/$(KERNELRELEASE)/build
KMAKE := $(MAKE) -C $(KDIR) M=$(PWD)

all: modules

modules:
	$(KMAKE) modules

clean:
	$(KMAKE) clean
	rm -f *odule*
