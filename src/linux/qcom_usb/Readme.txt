QdssDiag driver 2020-07-23

This readme covers important information concerning
i) qtiDevInf parser
ii) QdssDiag driver

Table of Contents

1. What's new in this release
2. Build and Installation.
3. Known issues
4. Known platform issues


-------------------------------------------------------------------------------

1. WHAT'S NEW

This Release (QdssDiag driver 2018-05-15)
a. Initial release
b. Infparser is used to parse the config file and used during probing.
c. QdssDiag driver supports
     i) Diag interface
    ii) Qdss (Bulk in/out)
   iii) Qdss (Trace in)
    iv) DPL

-------------------------------------------------------------------------------

2. Build and Installation

Build
-----

1. Run Make from the extracted folder, it should build qtiDevInf.ko, QdssDiag.ko

Installation
------------
1. QdssDiag.ko is dependent on QCDevInf.ko
   > insmod qtiDevInf.ko (found in the extracted folder)
   > insmod QdssDiag.ko (found in the extracted InfParser folder)

Note: QdssDiag.ko, tries to read the device info from the config files(Inf file)
      to insert the QdssDiag.ko, and to map the config to custom path
   > insmod QdssDiag.ko gQdssInfFilePath=<COMPLETE_PATH> gDiagInfFilePath=<COMPLETE_PATH>
default config paths are :
     i) gQdssInfFilePath : /opt/QTI/QUD/gobi/diag/qdbusb.inf
    ii) gDiagInfFilePath : /opt/QTI/QUD/gobi/diag/qtiser.inf

Test
----
The folder ./tests, contains some sample examples to test the functionality


-------------------------------------------------------------------------------

3. KNOWN ISSUES

No known issues.

-------------------------------------------------------------------------------

4. KNOWN PLATFORM ISSUES

No known issues.
