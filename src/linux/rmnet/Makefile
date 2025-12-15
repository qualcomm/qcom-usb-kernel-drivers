#obj-m := GobiNet.o ../InfParser/qtiDevInf.o
obj-m := GobiNet.o
GobiNet-objs := qmap.o GobiUSBNet.o QMIDevice.o QMI.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
OUTPUTDIR=/lib/modules/$(shell uname -r)/kernel/drivers/net/usb/
OS_RELEASE = $(shell cat /etc/os-release | grep PRETTY_NAME)

ifneq (,$(findstring Red Hat Enterprise Linux,$(OS_RELEASE)))
EXTRA_CFLAGS:= -D QCUSB_RHEL
endif

all: clean
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install: all
	mkdir -p $(OUTPUTDIR)
	cp -f GobiNet.ko $(OUTPUTDIR)
	depmod

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module.* modules.order



