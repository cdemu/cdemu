VHBA_VERSION = 1.0.0
PACKAGE = vhba-module-$(VHBA_VERSION)

EXTRA_CFLAGS += -DVHBA_VERSION=\"$(VHBA_VERSION)\" -I$(PWD)

obj-m += vhba.o

PWD	?= `pwd`
KDIR := /lib/modules/`uname -r`/build
KMAKE := $(MAKE) -C $(KDIR) M=$(PWD)

all: kernel.api.h modules

kernel.api.h: kat/*.c
	kat/kat ${KDIR} $@ $^

modules:
	$(KMAKE) modules

module_install:
	$(KMAKE) modules_install

install: module_install

distdir:
	cp vhba.c $(distdir)
	cp Makefile $(distdir)

clean:
	$(KMAKE) clean
	rm -fr $(PACKAGE) kernel.api.h

dist: dist-gzip

dist-dir:
	rm -fr $(PACKAGE)
	mkdir $(PACKAGE)
	cp vhba.c Makefile $(PACKAGE)

dist-gzip: dist-dir
	tar -czf $(PACKAGE).tar.gz $(PACKAGE)
	rm -rf $(PACKAGE)

dist-bzip2: dist-dir
	tar -cjf $(PACKAGE).tar.bz2 $(PACKAGE)
	rm -rf $(PACKAGE)
