EXTRA_CFLAGS := -Werror-implicit-function-declaration -Werror

obj-m += $(TESTNAME)

KERNELRELEASE ?= `uname -r`
KDIR ?= /lib/modules/$(KERNELRELEASE)/build
KMAKE := $(MAKE) -C $(KDIR) M=$(PWD)

all: modules

modules:
	$(KMAKE) modules

clean:
	$(KMAKE) clean
	rm -f *odule* kat.out

.PHONY: clean
