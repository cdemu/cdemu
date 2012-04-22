VHBA_VERSION = $(shell date -r vhba.c +%Y%m%d)
PACKAGE = vhba-module-$(VHBA_VERSION)

EXTRA_CFLAGS += -DVHBA_VERSION=\"$(VHBA_VERSION)\" -I$(PWD) -Werror

obj-m += vhba.o

PWD	?= `pwd`
KERNELRELEASE ?= `uname -r`
KDIR ?= /lib/modules/$(KERNELRELEASE)/build
KMAKE := $(MAKE) -C $(KDIR) M=$(PWD)

DOCS = AUTHORS ChangeLog COPYING INSTALL NEWS README

all: modules

modules:
	$(KMAKE) modules

module_install:
	$(KMAKE) modules_install

install: module_install

clean:
	$(KMAKE) clean
	rm -fr $(PACKAGE)

dist: dist-gzip

dist-dir:
	rm -fr $(PACKAGE)
	mkdir $(PACKAGE)
	cp vhba.c Makefile $(DOCS) $(PACKAGE)
	mkdir $(PACKAGE)/debian $(PACKAGE)/debian/patches $(PACKAGE)/debian/source
	cp -f $(subst debian/source,,$(subst debian/patches,,$(wildcard debian/*))) $(PACKAGE)/debian
	cp -f $(wildcard debian/patches/*) $(PACKAGE)/debian/patches
	cp -f $(wildcard debian/source/*) $(PACKAGE)/debian/source

dist-gzip: dist-dir
	tar -czf $(PACKAGE).tar.gz $(PACKAGE)
	rm -rf $(PACKAGE)

dist-bzip2: dist-dir
	tar -cjf $(PACKAGE).tar.bz2 $(PACKAGE)
	rm -rf $(PACKAGE)

.PHONY: clean
