KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
QCOM_NET_OUTPUTDIR=/lib/modules/$(shell uname -r)/kernel/drivers/net/usb/
OS_RELEASE = $(shell cat /etc/os-release | grep PRETTY_NAME)
#obj-m := qcom_usbnet.o ../InfParser/qtiDevInf.o
obj-m += qcom_usbnet.o
qcom_usbnet-objs := qcom_usbnet_main.o qmap.o qmidevice.o qmi.o

ifneq (,$(findstring Red Hat Enterprise Linux,$(OS_RELEASE)))
EXTRA_CFLAGS:= -D QCUSB_RHEL
endif

build: clean
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install: build
	mkdir -p $(QCOM_NET_OUTPUTDIR)
	rm -rf $(QCOM_NET_OUTPUTDIR)/qcom_usbnet.ko
	cp -rf qcom_usbnet.ko $(QCOM_NET_OUTPUTDIR)
	depmod
	modprobe qcom_usbnet debug_g=1 debug_aggr=0

uninstall: clean
	rmmod qcom_usbnet
	rm -rf $(QCOM_NET_OUTPUTDIR)/qcom_usbnet.ko
	depmod

# Enable debug messages only for 1st adapter
debug: install
	@echo 0102 > /sys/QTI_HS-USB_WWAN_Adapter_*usb0_0/Debug

clean:
	make -C $(KDIR) M=$(PWD) clean