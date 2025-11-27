This readme covers information concerning
i) qtiDevInf parser
ii) QdssDiag driver
iii) GobiNet driver
iv) Modem driver

Table of Contents

1. Build and Installation.

Build
-----

1. Run Make from the extracted folder, it should build qtiDevInf.ko, QdssDiag.ko, GobiNet.ko

Installation
------------
Loading Qdss/diag/dpl driver

1. QdssDiag.ko is dependent on qtiDevInf.ko
   > insmod qtiDevInf.ko (found in the extracted folder)
   > insmod QdssDiag.ko (found in the extracted InfParser folder)

Note: QdssDiag.ko, tries to read the device info from the config files(Inf file)
      to insert the QdssDiag.ko, and to map the config to custom path
   > insmod QdssDiag.ko gQdssInfFilePath=<COMPLETE_PATH> gDiagInfFilePath=<COMPLETE_PATH>
default config paths are :
     i) gQdssInfFilePath : /opt/QTI/QUD/gobi/diag/qdbusb.inf
    ii) gDiagInfFilePath : /opt/QTI/QUD/gobi/diag/qtiser.inf

Loding rmnet driver
1. GobiNet.ko is dependent on usbnet.ko, mii.ko, qtiDevInf.ko kernel modules
   > insmod mii.ko (found in /lib/modules/<kernel-version>/kernel/drivers/net)
   > insmod usbnet.ko (found in /lib/modules/<kernel-version>/kernel/drivers/net/usb)
   > insmod qtiDevInf.ko <if not already inserted>
   > insmod GobiNet.ko gQTIRmnetInfFilePath=<COMPLETE_INF_PATH>
default config paths are :
   gQTIRmnetInfFilePath=/opt/QTI/QUD/gobi/rmnet/qtiwwan.inf

Loding GobiSerial driver
1. GobiSerial.ko is dependent on usbserial.ko, qtiDevInf.ko kernel modules
   > insmod usbserial.ko (found in /lib/modules/<kernel-version>/kernel/drivers/serial/)
   > insmod qtiDevInf.ko <if not already inserted>
   > insmod GobiSerial.ko gQTIModemFileInfo=<COMPLETE_INF_PATH>
default config paths are :
   gQcModemFileInfo=/opt/QTI/QUD/gobi/modem/qcmdm.inf


Additional Info:
----------------
1. Enabled the Async support from kernel 3.18

   Tested for:
    4.4.0-148-generic, Ubuntu 14.04 LTS
    4.18.0-25-generic, Ubuntu 18.04.2 LTS
    4.5.7-generic, Ubuntu 18.04.2 LTS

