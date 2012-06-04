VHBA_VERSION := $(shell date -r $(PWD)/vhba.c +%Y%m%d)
PACKAGE := vhba-module-$(VHBA_VERSION)
DOCS := AUTHORS ChangeLog COPYING INSTALL NEWS README
DIST := vhba.c Makefile

KERNELRELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNELRELEASE)/build

obj-m := vhba.o
ccflags-y := -DVHBA_VERSION=\"$(VHBA_VERSION)\" -Werror
clean-dirs := $(PACKAGE)
clean-files := $(PACKAGE).tar.gz $(PACKAGE).tar.bz2

export VHBA_VERSION

.PHONY: default install dist modules modules_install clean dist-dir dist-gzip dist-bzip2

default: modules
install: modules_install
dist: dist-bzip2

modules modules_install clean:
	$(MAKE) -C $(KDIR) M=$(PWD) $@

dist-dir:
	rm -rf $(PACKAGE)
	install -m 755 -d $(PACKAGE)
	install -m 644 -t $(PACKAGE) $(DIST) $(DOCS)
	install -m 755 -d $(PACKAGE)/debian $(PACKAGE)/debian/patches $(PACKAGE)/debian/source
	install -m 644 -t $(PACKAGE)/debian         $(shell find debian -type f)
	install -m 644 -t $(PACKAGE)/debian/patches $(shell find debian/patches -type f)
	install -m 644 -t $(PACKAGE)/debian/source  $(shell find debian/source -type f)

dist-gzip: dist-dir
	tar -czf $(PACKAGE).tar.gz $(PACKAGE)
	rm -rf $(PACKAGE)

dist-bzip2: dist-dir
	tar -cjf $(PACKAGE).tar.bz2 $(PACKAGE)
	rm -rf $(PACKAGE)

