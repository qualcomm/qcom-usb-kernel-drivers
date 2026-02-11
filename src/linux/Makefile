obj-y := InfParser/ qdssdiag/ rmnet/ #GobiSerial/
PWD := $(shell pwd)
OUTPUTDIR=/lib/modules/$(shell uname -r)/kernel/drivers/net/usb/
OUTPUTDIR_MDM=/lib/modules/$(shell uname -r)/kernel/drivers/usb/serial/
SECUREBOOT := $(shell mokutil --sb-state)

all: clean
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install: all
#	mkdir -p $(OUTPUTDIR)
ifeq ($(SECUREBOOT),SecureBoot enabled)
	/usr/src/linux-headers-$(shell uname -r)/scripts/sign-file sha256 /opt/QTI/sign/Signkey.priv /opt/QTI/sign/Signkey_pub.der ./InfParser/qtiDevInf.ko
#	/usr/src/linux-headers-$(shell uname -r)/scripts/sign-file sha256 /opt/QTI/sign/Signkey.priv /opt/QTI/sign/Signkey_pub.der ./GobiSerial/GobiSerial.ko
	/usr/src/linux-headers-$(shell uname -r)/scripts/sign-file sha256 /opt/QTI/sign/Signkey.priv /opt/QTI/sign/Signkey_pub.der ./QdssDiag/QdssDiag.ko
	/usr/src/linux-headers-$(shell uname -r)/scripts/sign-file sha256 /opt/QTI/sign/Signkey.priv /opt/QTI/sign/Signkey_pub.der ./rmnet/GobiNet.ko
endif
	cp -f ./qdssdiag/qdssdiag.ko $(OUTPUTDIR)
	cp -f ./rmnet/GobiNet.ko $(OUTPUTDIR)
	cp -f ./InfParser/qtiDevInf.ko $(OUTPUTDIR)
#	cp -f ./GobiSerial/GobiSerial.ko $(OUTPUTDIR_MDM)
	depmod

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	$(RM) Module.markers modules.order
