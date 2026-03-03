KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
QCOM_USB_OUTPUTDIR=/lib/modules/$(shell uname -r)/kernel/drivers/usb/misc
EXTRA_CFLAGS:= -U LPC_TEST_ONLY

#obj-m := qcom_usb.o ../InfParser/qtiDevInf.o
obj-m := qcom_usb.o
qcom_usb-objs := qcom_usb_main.o qcom_event.o

build: clean
	make -C $(KDIR) M=$(PWD) modules

install: build
	mkdir -p $(QCOM_USB_OUTPUTDIR)
	cp -f qcom_usb.ko $(QCOM_USB_OUTPUTDIR)
	depmod
	modprobe qcom_usb

uninstall: clean
	rmmod qcom_usb
	rm -rf $(QCOM_USB_OUTPUTDIR)/qcom_usb.ko
	depmod

debug: install
	@echo 2 > /sys/QTI_HS-USB_Diagnostics_*/Debug

clean:
	make -C $(KDIR) M=$(PWD) clean
