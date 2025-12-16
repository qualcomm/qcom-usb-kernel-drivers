#obj-m := serial.o ../InfParser/qtiDevInf.o
obj-m := serial.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
OUTPUTDIR=/lib/modules/`uname -r`/kernel/drivers/usb/serial/

all: clean
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

install: all
	mkdir -p $(OUTPUTDIR)
	cp -f serial.ko $(OUTPUTDIR)
	depmod

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module.* modules.order

clean_prd:
	rm -rf *.o *~ core .depend .*.cmd *.mod.c .tmp_versions Module.* modules.order
