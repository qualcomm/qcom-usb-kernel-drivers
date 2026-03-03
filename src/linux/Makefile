obj-y := InfParser/ qcom_usb/ qcom_usbnet/ qcom_serial/
PWD := $(shell pwd)
KDIR := /lib/modules/$(shell uname -r)/build
KHEADERS := /usr/src/linux-headers-$(shell uname -r)
QCOM_USB_OUTPUTDIR=/lib/modules/$(shell uname -r)/kernel/drivers/usb/misc
QCOM_NET_OUTPUTDIR=/lib/modules/$(shell uname -r)/kernel/drivers/net/usb
OUTPUTDIR_MDM=/lib/modules/$(shell uname -r)/kernel/drivers/usb/serial/
SECUREBOOT := $(shell mokutil --sb-state)
DEST_SIGN_PATH=/opt/qcom/QUD/sign

build: clean
	$(MAKE) -C $(KDIR) M=$(PWD) modules
ifeq ($(SECUREBOOT),SecureBoot enabled)
	$(KHEADERS)/scripts/sign-file sha256 $(DEST_SIGN_PATH)/Signkey.priv $(DEST_SIGN_PATH)/Signkey_pub.der ./InfParser/qtiDevInf.ko
# 	$(KHEADERS)/scripts/sign-file sha256 $(DEST_SIGN_PATH)/Signkey.priv $(DEST_SIGN_PATH)/Signkey_pub.der ./qcom_serial/qcom_serial.ko
	$(KHEADERS)/scripts/sign-file sha256 $(DEST_SIGN_PATH)/Signkey.priv $(DEST_SIGN_PATH)/Signkey_pub.der ./qcom_usb/qcom_usb.ko
	$(KHEADERS)/scripts/sign-file sha256 $(DEST_SIGN_PATH)/Signkey.priv $(DEST_SIGN_PATH)/Signkey_pub.der ./qcom_net/qcom_usbnet.ko
endif

install: build
	mkdir -p $(QCOM_USB_OUTPUTDIR)
	mkdir -p $(QCOM_NET_OUTPUTDIR)
	mkdir -p $(OUTPUTDIR_MDM)
	rm -rf $(QCOM_USB_OUTPUTDIR)/qtiDevInf.ko
	rm -rf $(QCOM_USB_OUTPUTDIR)/qcom_usb.ko
	rm -rf $(QCOM_NET_OUTPUTDIR)/qcom_usbnet.ko
	rm -rf $(OUTPUTDIR_MDM)/qcom_serial.ko
	cp -rf ./InfParser/qtiDevInf.ko $(QCOM_USB_OUTPUTDIR)
	cp -rf ./qcom_usb/qcom_usb.ko $(QCOM_USB_OUTPUTDIR)
	cp -rf ./qcom_usbnet/qcom_usbnet.ko $(QCOM_NET_OUTPUTDIR)
	cp -rf ./qcom_serial/qcom_serial.ko $(OUTPUTDIR_MDM)
	depmod
# 	modprobe qcom_serial
	modprobe qtiDevInf
	modprobe qcom_usb
	modprobe qcom_usbnet debug_g=1 debug_aggr=0

uninstall: clean
	rmmod qtiDevInf
	rmmod qcom_usb
	rmmod qcom_usbnet
	rmmod qcom_serial
	rm -rf $(QCOM_USB_OUTPUTDIR)/qtiDevInf.ko
	rm -rf $(QCOM_USB_OUTPUTDIR)/qcom_usb.ko
	rm -rf $(QCOM_NET_OUTPUTDIR)/qcom_usbnet.ko
	rm -rf $(OUTPUTDIR_MDM)/qcom_serial.ko
	depmod

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

