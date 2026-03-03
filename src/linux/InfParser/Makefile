obj-m := qtiDevInf.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
QCOM_USBINF_PARSER_OUTPUTDIR=/lib/modules/$(shell uname -r)/kernel/drivers/usb/misc

build: clean
	make -C $(KDIR) M=$(PWD) modules

install: build
	mkdir -p $(QCOM_USBINF_PARSER_OUTPUTDIR)
	cp -f qtiDevInf.ko $(QCOM_USBINF_PARSER_OUTPUTDIR)
	depmod
	modprobe qtiDevInf

uninstall: clean
	rmmod qtiDevInf
	rm -rf $(QCOM_USBINF_PARSER_OUTPUTDIR)/qtiDevInf.ko
	depmod

clean:
	make -C $(KDIR) M=$(PWD) clean


