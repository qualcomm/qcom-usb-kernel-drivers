KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
OUTPUTDIR=/lib/modules/`uname -r`/kernel/drivers/usb/serial/

#obj-m := qcom-serial.o ../InfParser/qtiDevInf.o
obj-m := qcom-serial.o
qcom-serial-objs := qcom_serial.o

build: clean
	make -C $(KDIR) M=$(PWD) modules

install: all
	mkdir -p $(OUTPUTDIR)
	cp -f qcom-serial.ko $(OUTPUTDIR)
	depmod
	modprobe qcom-serial

uninstall: clean
	rmmod qcom-serial
	rm -rf $(OUTPUTDIR)/qcom-serial.ko
	depmod

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean