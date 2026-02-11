#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

DEST_QTI_PATH=/opt/QTI/
DEST_QUD_PATH=/opt/QTI/QUD
DEST_INS_RMNET_PATH=/opt/QTI/QUD/rmnet
DEST_INS_QDSS_PATH=/opt/QTI/QUD/diag
DEST_INF_PATH=/opt/QTI/QUD/diag/InfParser
DEST_QDSS_DAIG_PATH=/opt/QTI/QUD/diag/qdssdiag
DEST_SIGN_PATH=/opt/QTI/sign
QC_MODBIN_DIR=/sbin
QC_MAKE_DIR=/usr/bin
QC_MODULE_INF_NAME=qtiDevInf.ko
QC_MODULE_QDSS_DIAG_NAME=qdssdiag.ko
QC_MODULE_RMNET_NAME=GobiNet.ko
QC_DIAG_INF_PATH=/opt/QTI/QUD/diag/qtiser.inf
QC_QDSS_INF_PATH=/opt/QTI/QUD/diag/qdbusb.inf
QC_MODEM_INF_PATH=/opt/QTI/QUD/serial/qtimdm.inf
DEST_INS_SERIAL_PATH=/opt/QTI/QUD/serial
QC_UDEV_PATH=/etc/udev/rules.d
QC_MODULE_GOBISERIAL_NAME=serial.ko
QC_SERIAL=/lib/modules/`uname -r`/kernel/drivers/usb/serial
QC_QMI_WWAN=/lib/modules/`uname -r`/kernel/drivers/net/usb
QC_NET=/lib/modules/`uname -r`/kernel/drivers/net
QC_LN_RM_MK_DIR=/bin
MODULE_BLACKLIST=/etc/modprobe.d
OS_RELEASE="`cat /etc/os-release | grep PRETTY_NAME`"
OSName=`echo $OS_RELEASE | awk -F= '{printf $2}'`
KERNEL_VERSION=`uname -r`
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
RESET='\033[0m'

#check and install mokutil package
if [ ! -f "$QC_MAKE_DIR/mokutil" ]; then
   echo -e "${RED}Error: mokutil not found, installing..\n${RESET}"
fi

if [ ! -f "$QC_MAKE_DIR/keyctl" ]; then
   echo -e ${RED}"Error: keyutils not found, installing..\n"${RESET}
fi

if [[ ! -f "$QC_MAKE_DIR/mokutil" ]] || [[ ! -f "$QC_MAKE_DIR/keyctl" ]]; then
   if [[ $OSName =~ "Red Hat Enterprise Linux" ]]; then
      sudo dnf install -y mokutil
      sudo dnf install -y keyutils
   fi

   if [[ $OSName =~ "Ubuntu" ]]; then
      sudo apt-get install -y mokutil
      sudo apt-get install -y keyutils
   fi
fi

if [[ $OSName =~ "Fedora Linux" ]]; then
   sudo dnf install -y make automake gcc gcc-c++ kernel-devel
fi

QC_SECURE_BOOT_CHECK=`mokutil --sb-state`

if [[ $QC_SECURE_BOOT_CHECK = "SecureBoot enabled" ]]; then
   QC_PUBLIC_KEY_VERIFY=`mokutil --test-key $DEST_SIGN_PATH/Signkey_pub.der`
fi

if [  ! -d $DEST_QUD_PATH  ]; then
   echo -e "${RED}Error: $DEST_QUD_PATH doesn't exist. Creating Now.${RESET}"
   $QC_LN_RM_MK_DIR/mkdir -m 0755 -p $DEST_QUD_PATH
fi

if [ $# == 0 ]; then
   echo -e "${RED}Usage: QCDevInstaller.sh <install | uninstall>${RESET}"
   exit 1
else
   if [ $1 == "uninstall" ]; then
      echo -e "Operating System: $OSName"
      echo -e "Kernel Version: "\""`uname -r`"\"""

      if [ -f ./version.h ]; then
         VERSION="`grep -r '#define DRIVER_VERSION' version.h`"
         DRIVER_VERSION=`echo $VERSION | awk '{printf $3}'`
         echo -e "Driver Version: $DRIVER_VERSION"
      elif [ -f ./BuildPackage/version.h ]; then
         VERSION="`grep -r '#define DRIVER_VERSION' ./BuildPackage/version.h`"
         DRIVER_VERSION=`echo $VERSION | awk '{printf $3}'`
         echo -e "Driver Version: $DRIVER_VERSION"
      fi

      if [ -d $DEST_INS_QDSS_PATH ]; then
         $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
         if [ ! -d $DEST_INS_QDSS_PATH ]; then
            echo -e "Successfully removed $DEST_INS_QDSS_PATH"
         else
            echo -e "${RED}Failed to remove $DEST_INS_QDSS_PATH${RESET}"
         fi
      else
         echo -e "$DEST_INS_QDSS_PATH does not exist, nothing to remove"
      fi
      if [ -d $DEST_INS_RMNET_PATH ]; then
         $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
         if [ ! -d $DEST_INS_RMNET_PATH ]; then
            echo -e "Successfully removed $DEST_INS_RMNET_PATH"
         else
            echo -e "${RED}Failed to remove $DEST_INS_RMNET_PATH${RESET}"
         fi
      else
         echo -e "$DEST_INS_RMNET_PATH does not exist, nothing to remove"
      fi
      if [ -d $DEST_INS_SERIAL_PATH ]; then
         $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_SERIAL_PATH
         if [ ! -d $DEST_INS_SERIAL_PATH ]; then
            echo -e "Successfully removed $DEST_INS_SERIAL_PATH"
         else
            echo -e "${RED}Failed to remove $DEST_INS_SERIAL_PATH${RESET}"
         fi
      else
         echo -e "$DEST_INS_SERIAL_PATH does not exist, nothing to remove"
      fi

      if [ -f $QC_UDEV_PATH/qti_usb_device.rules ]; then
         rm -r $QC_UDEV_PATH/qti_usb_device.rules
         if [ ! -f $QC_UDEV_PATH/qti_usb_device.rules ]; then
            echo -e "Successfully removed $QC_UDEV_PATH/qti_usb_device.rules"
         else
            echo -e "${RED}Failed to remove $QC_UDEV_PATH/qti_usb_device.rules${RESET}"
         fi
      else
         echo -e "$QC_UDEV_PATH/qti_usb_device.rules does not exist, nothing to remove"
      fi
      # < "Redundant 80-net-setup-link.rules. This code will be eliminated after few releases"
      if [ -f $QC_UDEV_PATH/80-net-setup-link.rules ]; then
         sed -i '/GobiQMI/d' $QC_UDEV_PATH/80-net-setup-link.rules
         if [ ! -s $QC_UDEV_PATH/80-net-setup-link.rules ]; then
            echo -e "Removed GobiQMI rule from $QC_UDEV_PATH/80-net-setup-link.rules"
            rm -rf $QC_UDEV_PATH/80-net-setup-link.rules
            if [ ! -f $QC_UDEV_PATH/80-net-setup-link.rules ]; then
               echo -e "File was empty. Removed $QC_UDEV_PATH/80-net-setup-link.rules successfully"
            else
               echo -e "File is empty but Failed to remove $QC_UDEV_PATH/80-net-setup-link.rules"
            fi
         else
               echo -e "Pre-Existing data $QC_UDEV_PATH/80-net-setup-link.rules, so not removing it."
         fi
      else
         echo -e "$QC_UDEV_PATH/80-net-setup-link.rules does not exist, nothing to remove"
      fi
      # >
      if [ -f $QC_UDEV_PATH/80-gobinet-usbdevice.rules ]; then
         sed -i '/usb/d' $QC_UDEV_PATH/80-gobinet-usbdevice.rules
         if [ ! -s $QC_UDEV_PATH/80-gobinet-usbdevice.rules ]; then
            echo -e "Removed GobiQMI rule from $QC_UDEV_PATH/80-gobinet-usbdevice.rules"
            rm -rf $QC_UDEV_PATH/80-gobinet-usbdevice.rules
            if [ ! -f $QC_UDEV_PATH/80-gobinet-usbdevice.rules ]; then
               echo -e "File was empty. Removed $QC_UDEV_PATH/80-gobinet-usbdevice.rules successfully"
            else
               echo -e "File is empty but Failed to remove $QC_UDEV_PATH/80-gobinet-usbdevice.rules"
            fi
         else
               echo -e "Pre-Existing data $QC_UDEV_PATH/80-gobinet-usbdevice.rules, so not removing it."
         fi
      else
         echo -e "$QC_UDEV_PATH/80-gobinet-usbdevice.rules does not exist, nothing to remove"
      fi

      # Informs udev deamon to reload the newly added device rule and re-trigger service
      sudo udevadm control --reload-rules
      sudo udevadm trigger

      # if [ "`lsmod | grep serial`" ]; then
      #    ($QC_MODBIN_DIR/rmmod $QC_MODULE_GOBISERIAL_NAME && echo -e "$QC_MODULE_GOBISERIAL_NAME removed successfully") || { echo -e "$QC_MODULE_GOBISERIAL_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"; exit 1; }
      # else
      #    echo -e "Module $QC_MODULE_GOBISERIAL_NAME is not currently loaded"
      # fi
      if [ "`lsmod | grep GobiNet`" ]; then
         ( $QC_MODBIN_DIR/rmmod $QC_MODULE_RMNET_NAME && echo -e "$QC_MODULE_RMNET_NAME removed successfully" ) || { echo -e "$QC_MODULE_RMNET_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"; exit 1; }
      else
         echo -e "Module $QC_MODULE_RMNET_NAME is not currently loaded"
      fi
      if [ "`lsmod | grep qdssdiag`" ]; then
         ($QC_MODBIN_DIR/rmmod $QC_MODULE_QDSS_DIAG_NAME && echo -e "$QC_MODULE_QDSS_DIAG_NAME removed successfully") || { echo -e "$QC_MODULE_QDSS_DIAG_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"; exit 1; }
      else
         echo -e "Module $QC_MODULE_QDSS_DIAG_NAME is not currently loaded"
      fi
      if [ "`lsmod | grep qtiDevInf`" ]; then
         ($QC_MODBIN_DIR/rmmod $QC_MODULE_INF_NAME && echo -e "$QC_MODULE_INF_NAME removed successfully") || { echo -e "$QC_MODULE_INF_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"; exit 1; }
      else
         echo -e "Module $QC_MODULE_INF_NAME is not currently loaded"
      fi

      MODLOADED="`/sbin/lsmod | grep usb_wwan`"
      if [ "$MODLOADED" != "" ]; then
         echo -e "usb_wwan module is already loaded. nothing to do"
      fi
      if [  -f $QC_SERIAL/usb_wwan_dup* ]; then
         echo -e "usb_wwan_dup is found. restoring to usb_wwan"
         mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/usb_wwan_dup* /lib/modules/`uname -r`/kernel/drivers/usb/serial/usb_wwan.ko
         #$QC_MODBIN_DIR/insmod /lib/modules/`uname -r`/kernel/drivers/usb/serial/usb_wwan.ko

         MODLOADED="`/sbin/lsmod | grep usb_wwan`"
         if [ "$MODLOADED" != "" ]; then
            echo -e "Successfully loaded usb_wwan module."
         fi
      fi

      MODLOADED="`/sbin/lsmod | grep qcserial`"
      if [ "$MODLOADED" != "" ]; then
         echo -e "qcserial module is already loaded. nothing to do"
      fi
      if [  -f $QC_SERIAL/qcserial_dup* ]; then
         echo -e "qcserial_dup is found. restoring to qcserial"
         mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/qcserial_dup* /lib/modules/`uname -r`/kernel/drivers/usb/serial/qcserial.ko
         #$QC_MODBIN_DIR/insmod /lib/modules/`uname -r`/kernel/drivers/usb/serial/qcserial.ko

         MODLOADED="`/sbin/lsmod | grep qcserial`"
         if [ "$MODLOADED" != "" ]; then
            echo -e "Successfully loaded qcserial module."
         fi
      fi

      MODLOADED="`/sbin/lsmod | grep option`"
      if [ "$MODLOADED" != "" ]; then
         echo -e "option module is already loaded. nothing to do"
      fi
      if [  -f $QC_SERIAL/option_dup* ]; then
         echo -e "option_dup is found. restoring to option"
         mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/option_dup* /lib/modules/`uname -r`/kernel/drivers/usb/serial/option.ko
         #$QC_MODBIN_DIR/insmod /lib/modules/`uname -r`/kernel/drivers/usb/serial/option.ko

         MODLOADED="`/sbin/lsmod | grep option`"
         if [ "$MODLOADED" != "" ]; then
            echo -e "Successfully loaded option module."
         fi
      fi

      MODLOADED="`/sbin/lsmod | grep qmi_wwan`"
      if [ "$MODLOADED" != "" ]; then
         echo -e "qmi_wwan module is already loaded. nothing to do"
      fi
      if [  -f $QC_QMI_WWAN/qmi_wwan_dup* ]; then
         echo -e "qmi_wwan_dup is found. restoring to qmi_wwan"
         mv /lib/modules/`uname -r`/kernel/drivers/usb/class/cdc-wdm_dup* /lib/modules/`uname -r`/kernel/drivers/usb/class/cdc-wdm.ko
         mv /lib/modules/`uname -r`/kernel/drivers/net/usb/qmi_wwan_dup* /lib/modules/`uname -r`/kernel/drivers/net/usb/qmi_wwan.ko
         #$QC_MODBIN_DIR/insmod /lib/modules/`uname -r`/kernel/drivers/usb/class/cdc-wdm.ko
         #$QC_MODBIN_DIR/insmod /lib/modules/`uname -r`/kernel/drivers/net/usb/qmi_wwan.ko

         MODLOADED="`/sbin/lsmod | grep qmi_wwan`"
         if [ "$MODLOADED" != "" ]; then
            echo -e "Successfully loaded qmi_wwan module."
         fi
      fi

      if  [[ $OSName =~ "Ubuntu 24.04" ]]; then
         if [ -f $QC_NET/mii.ko ]; then
            $QC_LN_RM_MK_DIR/rm -rf $QC_NET/mii.ko
         fi
         if [ -f $QC_QMI_WWAN/usbnet.ko ]; then
            $QC_LN_RM_MK_DIR/rm -rf $QC_QMI_WWAN/usbnet.ko
         fi
      fi

      if [ "`grep -nr 'Qualcomm clients' /etc/modprobe.d/blacklist.conf`" != "" ]; then
         sed -i '/# Blacklist these module so that Qualcomm clients use only/d' /etc/modprobe.d/blacklist.conf
         sed -i '/# GobiNet, serial, qdssdiag, qtiDevInf driver/d' /etc/modprobe.d/blacklist.conf
      fi

      MOD_EXIST="`grep -nr  'blacklist qcserial' /etc/modprobe.d/blacklist.conf`"
      if [ "$MOD_EXIST" != "" ]; then
         sed -i '/qcserial/d' $MODULE_BLACKLIST/blacklist.conf
         echo -e "Successfully removed qcserial from $MODULE_BLACKLIST/blacklist.conf"
      fi

      MOD_EXIST="`grep -nr  'blacklist qmi_wwan' /etc/modprobe.d/blacklist.conf`"
      if [ "$MOD_EXIST" != "" ]; then
         sed -i '/qmi_wwan/d' $MODULE_BLACKLIST/blacklist.conf
         echo -e "Successfully removed qmi_wwan from $MODULE_BLACKLIST/blacklist.conf"
      fi

      MOD_EXIST="`grep -nr  'blacklist option' /etc/modprobe.d/blacklist.conf`"
      if [ "$MOD_EXIST" != "" ]; then
         sed -i '/option/d' $MODULE_BLACKLIST/blacklist.conf
         echo -e "Successfully removed option from $MODULE_BLACKLIST/blacklist.conf"
      fi

      MOD_EXIST="`grep -nr  'blacklist usb_wwan' /etc/modprobe.d/blacklist.conf`"
      if [ "$MOD_EXIST" != "" ]; then
         sed -i '/usb_wwan/d' $MODULE_BLACKLIST/blacklist.conf
         echo -e "Successfully removed usb_wwan from $MODULE_BLACKLIST/blacklist.conf"
      fi

      #change to permission to default mode
      $QC_LN_RM_MK_DIR/chmod 644 $MODULE_BLACKLIST/blacklist.conf

      echo -e "Removing modules for /etc/modules."
      MODUPDATE="`grep -r qtiDevInf /etc/modules`"
      if [ "$MODUPDATE" == "qtiDevInf" ]; then
	  sed -i '/qtiDevInf/d' /etc/modules
      fi
      MODUPDATE="`grep -r qdssdiag /etc/modules`"
      if [ "$MODUPDATE" == "qdssdiag" ]; then
	  sed -i '/qdssdiag/d' /etc/modules
      fi
      MODUPDATE="`grep -r GobiNet /etc/modules`"
      if [ "$MODUPDATE" == "GobiNet" ]; then
	  sed -i '/GobiNet/d' /etc/modules
      fi
      MODUPDATE="`grep -r serial /etc/modules`"
      if [ "$MODUPDATE" == "serial" ]; then
	  sed -i '/serial/d' /etc/modules
      fi
      if [[ $OSName != *"Red Hat Enterprise Linux"* ]]; then
      	MODUPDATE="`grep -nr  'iface usb0 inet static' /etc/network/interfaces`"
      	if [ "$MODUPDATE" != "" ]; then
        	 sed -i '/iface usb0 inet static/d' /etc/network/interfaces
      	fi
      fi

      echo -e "Removing modules from $QC_SERIAL"
      if [ -f $QC_SERIAL/$QC_MODULE_GOBISERIAL_NAME ]; then
         rm -rf $QC_SERIAL/$QC_MODULE_GOBISERIAL_NAME
      fi
      echo -e "Removing modules from $QC_QMI_WWAN"
      if [ -f $QC_QMI_WWAN/$QC_MODULE_QDSS_DIAG_NAME ]; then
         rm -rf $QC_QMI_WWAN/$QC_MODULE_QDSS_DIAG_NAME
      fi
      if [ -f $QC_QMI_WWAN/$QC_MODULE_RMNET_NAME ]; then
         rm -rf $QC_QMI_WWAN/$QC_MODULE_RMNET_NAME
      fi
      if [ -f $QC_QMI_WWAN/$QC_MODULE_INF_NAME ]; then
         rm -rf $QC_QMI_WWAN/$QC_MODULE_INF_NAME
      fi
	   # update modules.dep and modules.alias
      depmod

	   echo -e "Uninstallation completed successfully."
      exit 0
   else
      if [ $1 != "install" ]; then
         echo -e "Usage: QCDevInstaller.sh <install | uninstall>"
         exit 1
      fi
   fi
fi

######## Installation ###########

if [[ $OSName =~ "Ubuntu" ]]; then
   sudo apt-get update
   sudo apt-get install -y build-essential
   sudo apt-get install -y gawk
   sudo apt-get install -y python3-tk
fi

IFS=. read -r major_ver minor_ver patch_ver <<< "$KERNEL_VERSION"
if [[ $OSName =~ "Ubuntu 22." ]] && (( "$major_ver" >= 6 && "$minor_ver" >= 5 )); then
   echo -e "Installing gcc 12 version ..."
   sudo apt install -y gcc-12 g++-12
fi

echo -e "${CYAN}======================================================================================="
echo -e "=======================================================================================${RESET}"
echo -e " "

echo -e "Operating System:${RED} $OSName"${RESET}
echo -e "Kernel Version: ${RED}"\"$KERNEL_VERSION\"""${RESET}

if [ -f ./version.h ]; then
   VERSION="`grep -r '#define DRIVER_VERSION' version.h`"
   DRIVER_VERSION=`echo $VERSION | awk '{printf $3}'`
   echo -e "Driver Version: $DRIVER_VERSION"
fi

echo -e "Installing at the following paths:"
echo $DEST_INS_SERIAL_PATH
echo $DEST_INS_QDSS_PATH
echo $DEST_INS_RMNET_PATH

# this script will use in qik uninstallation process
if [ -f "$DEST_QUD_PATH/QcDevDriver.sh" ]; then
   echo -e "Delete and copy again (QcDevDriver.sh)"
   $QC_LN_RM_MK_DIR/rm -rf $DEST_QUD_PATH/QcDevDriver.sh
   $QC_LN_RM_MK_DIR/cp -rf ./QcDevDriver.sh $DEST_QUD_PATH/
else
   echo -e "Does not exist and copying now.. (QcDevDriver.sh)"
   $QC_LN_RM_MK_DIR/cp -rf ./QcDevDriver.sh $DEST_QUD_PATH/
fi

# Create directories
if [ -d $DEST_INS_SERIAL_PATH ]; then
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_SERIAL_PATH
fi

$QC_LN_RM_MK_DIR/mkdir -m 0755 -p $DEST_INS_SERIAL_PATH
if [  ! -d $DEST_INS_SERIAL_PATH  ]; then
   echo -e "${RED}Error: Failed to create installation path, please run installer under root."
   exit 1
fi

if [ -d $DEST_INS_QDSS_PATH ]; then
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
fi

$QC_LN_RM_MK_DIR/mkdir -m 0755  -p $DEST_INS_QDSS_PATH
if [  ! -d $DEST_INS_QDSS_PATH  ]; then
   echo -e "${RED}Error: Failed to create installation path, please run installer under root."
   exit 1
fi

if [ -d $DEST_INF_PATH ]; then
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INF_PATH
fi

$QC_LN_RM_MK_DIR/mkdir -m 0755  -p $DEST_INF_PATH
if [  ! -d $DEST_INF_PATH  ]; then
   echo -e "${RED}Error: Failed to create installation path, please run installer under root."
   exit 1
fi

if [ -d $DEST_QDSS_DAIG_PATH ]; then
   $QC_LN_RM_MK_DIR/rm -rf $DEST_QDSS_DAIG_PATH
fi

$QC_LN_RM_MK_DIR/mkdir -m 0755  -p $DEST_QDSS_DAIG_PATH
if [  ! -d $DEST_QDSS_DAIG_PATH  ]; then
   echo -e "${RED}Error: Failed to create installation path, please run installer under root."
   exit 1
fi

# Important: Do not delete or recreate the "sign" folder.
# The sign files will be automatically generated whenever the Signpub key is not enrolled.
$QC_LN_RM_MK_DIR/mkdir -m 0777  -p $DEST_SIGN_PATH
if [ ! -d $DEST_SIGN_PATH ]; then
   echo -e "${RED}Error: Failed to create installation path, please run installer under root."
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./serial/serial.c $DEST_INS_SERIAL_PATH
if [ ! -f $DEST_INS_SERIAL_PATH/serial.c ]; then
   echo -e "${RED}Error: Failed to copy serial.c to installation path, installation abort."
   rm -r $DEST_INS_SERIAL_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./serial/serial.h $DEST_INS_SERIAL_PATH
if [ ! -f $DEST_INS_SERIAL_PATH/serial.h ]; then
   echo -e "${RED}Error: Failed to copy serial.h to installation path, installation abort."
   rm -r $DEST_INS_SERIAL_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./serial/Makefile  $DEST_INS_SERIAL_PATH
if [ ! -f $DEST_INS_SERIAL_PATH/Makefile ]; then
   echo -e "${RED}Error: Failed to copy Makefile installation path, installation abort."
   rm -r $DEST_INS_SERIAL_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./serial/qtidev.pl  $DEST_INS_SERIAL_PATH
if [ ! -f $DEST_INS_SERIAL_PATH/qtidev.pl ]; then
   echo -e "${RED}Error: Failed to copy qtidev.pl installation path, installation abort."
   rm -r $DEST_INS_SERIAL_PATH
   exit 1
fi
$QC_LN_RM_MK_DIR/chmod 755 $DEST_INS_SERIAL_PATH/qtidev.pl

$QC_LN_RM_MK_DIR/cp ./serial/qtiname.inf $DEST_INS_SERIAL_PATH
if [ ! -f $DEST_INS_SERIAL_PATH/qtiname.inf ]; then
   echo -e "${RED}Error: Failed to copy qtiname.inf installation path, installation abort."
   rm -r $DEST_INS_SERIAL_PATH
   exit 1
fi
$QC_LN_RM_MK_DIR/chmod 644 $DEST_INS_SERIAL_PATH/qtiname.inf

$QC_LN_RM_MK_DIR/cp ./serial/qtimdm.inf $DEST_INS_SERIAL_PATH
if [ ! -f $DEST_INS_SERIAL_PATH/qtimdm.inf ]; then
   echo -e "${RED}Error: Failed to copy qtiname.inf installation path, installation abort."
   rm -r $DEST_INS_SERIAL_PATH
   exit 1
fi
$QC_LN_RM_MK_DIR/chmod 644 $DEST_INS_SERIAL_PATH/qtimdm.inf


$QC_LN_RM_MK_DIR/cp ./serial/qtiname.inf $DEST_INS_QDSS_PATH/
if [ ! -f $DEST_INS_QDSS_PATH//qtiname.inf ]; then
   echo -e "${RED}Error: Failed to copy qtiname.inf installation path, installation abort."
   rm -r $DEST_INS_QDSS_PATH/
   exit 1
fi
$QC_LN_RM_MK_DIR/chmod 644 $DEST_INS_SERIAL_PATH/qtiname.inf

$QC_LN_RM_MK_DIR/cp ./serial/qtimdm.inf $DEST_INS_QDSS_PATH/
if [ ! -f $DEST_INS_QDSS_PATH//qtimdm.inf ]; then
   echo -e "${RED}Error: Failed to copy qtiname.inf installation path, installation abort."
   rm -r $DEST_INS_QDSS_PATH/
   exit 1
fi
$QC_LN_RM_MK_DIR/chmod 644 $DEST_INS_QDSS_PATH/qtimdm.inf


$QC_LN_RM_MK_DIR/cp ./qdssdiag/qdbusb.inf $DEST_INS_QDSS_PATH/
if [ ! -f $DEST_INS_QDSS_PATH/qdbusb.inf ]; then
   echo -e "${RED}Error: Failed to copy 'qdbusb.inf' to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./qdssdiag/qtiser.inf $DEST_INS_QDSS_PATH/
if [ ! -f $DEST_INS_QDSS_PATH/qtiser.inf ]; then
   echo -e "${RED}Error: Failed to copy 'qtiser.inf' to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/chmod 644 $DEST_INS_QDSS_PATH/qdbusb.inf
$QC_LN_RM_MK_DIR/chmod 644 $DEST_INS_QDSS_PATH/qtiser.inf

$QC_LN_RM_MK_DIR/cp ./InfParser/qtiDevInf.h $DEST_INF_PATH/
if [ ! -f $DEST_INF_PATH/qtiDevInf.h ]; then
   echo -e "${RED}Error: Failed to copy 'InfParser/qtiDevInf.h' to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./InfParser/qtiDevInf.c $DEST_INF_PATH/
if [ ! -f $DEST_INF_PATH/qtiDevInf.c ]; then
   echo -e "${RED}Error: Failed to copy 'InfParser/qtiDevInf.c' to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./InfParser/Makefile $DEST_INF_PATH/
if [ ! -f $DEST_INF_PATH/Makefile ]; then
   echo -e "${RED}Error: Failed to copy 'InfParser/Makefile' to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./qdssdiag/qtiDevInf.h $DEST_QDSS_DAIG_PATH/
if [ ! -f $DEST_QDSS_DAIG_PATH/qtiDevInf.h ]; then
   echo -e "${RED}Error: Failed to copy 'qdssdiag/qtiDevInf.h' to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./qdssdiag/qtiDiag.h $DEST_QDSS_DAIG_PATH/
if [ ! -f $DEST_QDSS_DAIG_PATH/qtiDiag.h ]; then
   echo -e "${RED}Error: Failed to copy 'qdssdiag/qtiDiag.h' to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./qdssdiag/qtiDiag.c $DEST_QDSS_DAIG_PATH/
if [ ! -f $DEST_QDSS_DAIG_PATH/qtiDiag.c ]; then
   echo -e "${RED}Error: Failed to copy 'qdssdiag/qtiDiag.c' to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./Makefile $DEST_QDSS_DAIG_PATH/
if [ ! -f $DEST_QDSS_DAIG_PATH/Makefile ]; then
   echo -e "${RED}Error: Failed to copy 'qdssdiag/Makefile' to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   exit 1
fi

if [ -d $DEST_INS_RMNET_PATH ]; then
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
fi

$QC_LN_RM_MK_DIR/mkdir -p $DEST_INS_RMNET_PATH
if [  ! -d $DEST_INS_RMNET_PATH  ]; then
   echo -e "${RED}Error: Failed to create installation path, please run installer under root."
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/IPAssignmentScript.sh $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/IPAssignmentScript.sh ]; then
   echo -e "${RED}Error: Failed to copy IPAssignmentScript.sh to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/chmod 755 $DEST_INS_RMNET_PATH/IPAssignmentScript.sh

$QC_LN_RM_MK_DIR/cp ./rmnet/GobiUSBNet.c $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/GobiUSBNet.c ]; then
   echo -e "${RED}Error: Failed to copy GobiUSBNet.c to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/QMIDevice.c $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/QMIDevice.c ]; then
   echo -e "${RED}Error: Failed to copy QMIDevice.c to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/QMIDevice.h $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/QMIDevice.h ]; then
   echo -e "${RED}Error: Failed to copy QMIDevice.h to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/QMI.c $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/QMI.c ]; then
   echo -e "${RED}Error: Failed to copy QMI.c to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/QMI.h $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/QMI.h ]; then
   echo -e "${RED}Error: Failed to copy QMI.h to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/qmap.c $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/qmap.c ]; then
   echo -e "${RED}Error: Failed to copy qmap.c to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/qmap.h $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/qmap.h ]; then
   echo -e "${RED}Error: Failed to copy qmap.h to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/Structs.h $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/Structs.h ]; then
   echo -e "${RED}Error: Failed to copy Structs.h to installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/Makefile  $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/Makefile ]; then
   echo -e "${RED}Error: Failed to copy Makefile installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/qtiwwan.inf $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/qtiwwan.inf ]; then
   echo -e "${RED}Error: Failed to copy qtiwwan.inf installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
   exit 1
fi
$QC_LN_RM_MK_DIR/chmod 644 $DEST_INS_RMNET_PATH/qtiwwan.inf

#DEST_SIGN_PATH=/opt/QTI/sign/
$QC_LN_RM_MK_DIR/cp ./sign/SignConf.config $DEST_SIGN_PATH
$QC_LN_RM_MK_DIR/chmod 777 $DEST_SIGN_PATH/SignConf.config
awk -i inplace -v name=`hostname` '{gsub(/O = /,"O = "name)}1' $DEST_SIGN_PATH/SignConf.config
awk -i inplace -v name=`hostname` '{gsub(/CN = /,"CN = "name" Signing Key")}1' $DEST_SIGN_PATH/SignConf.config
awk -i inplace -v name=`hostname` '{gsub(/emailAddress = /,"emailAddress = "name"@no-reply.com")}1' $DEST_SIGN_PATH/SignConf.config

if [ ! -f $DEST_SIGN_PATH/SignConf.config ]; then
   echo -e "${RED}Error: Failed to copy SignConf.config installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_SIGN_PATH
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./sign/signReadme.txt $DEST_SIGN_PATH
if [ ! -f $DEST_SIGN_PATH/signReadme.txt ]; then
   echo -e "${RED}Error: Failed to copy signReadme.txt installation path, installation abort."
   $QC_LN_RM_MK_DIR/rm -rf $DEST_SIGN_PATH
   exit 1
fi
$QC_LN_RM_MK_DIR/chmod 644 $DEST_SIGN_PATH/signReadme.txt


if [[ $QC_SECURE_BOOT_CHECK = "SecureBoot enabled" ]]; then
   echo -e "${RED}SecureBoot enabled"
   if [ -f $DEST_SIGN_PATH/Signkey_pub.der ]; then
      if [[ $QC_PUBLIC_KEY_VERIFY = "$DEST_SIGN_PATH/Signkey_pub.der is already enrolled" ]]; then
         echo -e "${CYAN}==========================================================="
         echo -e "${GREEN}$DEST_SIGN_PATH/Signkey_pub.der is enrolled"
         echo -e "${CYAN}===========================================================${RESET}"
      else
         echo -e "${RED}==========================================================="
         echo -e "${CYAN}Secure Boot is enabled. User must enroll the Public Signkey located at /opt/QTI/sign/Signkey_pub.der"
         echo -e "${CYAN}Please follow the mandatory instructions in /opt/QTI/sign/signReadme.txt"
         echo -e "${CYAN}The QUD driver installation Failed!!!"
         echo -e "${RED}===========================================================${RESET}"
         exit 1
      fi
   else
      echo -e "${RED}Signkey_pub.der doesn't exist. Creating public and private key${RESET}"
      openssl req -x509 -new -nodes -utf8 -sha256 -days 36500 -batch -config $DEST_SIGN_PATH/SignConf.config -outform DER -out $DEST_SIGN_PATH/Signkey_pub.der -keyout $DEST_SIGN_PATH/Signkey.priv
      $QC_LN_RM_MK_DIR/chmod 755 $DEST_SIGN_PATH/Signkey_pub.der
      $QC_LN_RM_MK_DIR/chmod 755 $DEST_SIGN_PATH/Signkey.priv
      echo -e "${RED}##############################################################"
      echo -e "${CYAN}Secure Boot is enabled. User must enroll the Public Signkey located at /opt/QTI/sign/Signkey_pub.der"
      echo -e "${CYAN}Please follow the mandatory instructions in /opt/QTI/sign/signReadme.txt"
      echo -e "${CYAN}The QUD driver installation Failed!!!"
      echo -e "${RED}##############################################################${RESET}"
      exit 1
   fi
else
   echo -e "${GREEN}Secure boot disabled${RESET}"
fi

# "********************* Compilation of modules Starts ***************************"
$QC_MAKE_DIR/make install
if [ ! -f ./InfParser/$QC_MODULE_INF_NAME ]; then
   echo -e "${RED}Error: Failed to generate kernel module $QC_MODULE_INF_NAME, installation abort.${RESET}"
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   $QC_MAKE_DIR/make clean
   exit 1
fi
if [ ! -f ./qdssdiag/$QC_MODULE_QDSS_DIAG_NAME ]; then
   echo -e "${RED}Error: Failed to generate kernel module $QC_MODULE_QDSS_DIAG_NAME, installation abort.${RESET}"
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   $QC_MAKE_DIR/make clean
   exit 1
fi

if [ ! -f ./rmnet/$QC_MODULE_RMNET_NAME ]; then
  echo -e "${RED}Error: Failed to generate kernel module $QC_MODULE_RMNET_NAME, installation abort.${RESET}"
  $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
  $QC_MAKE_DIR/make clean
  exit 1
fi

# if [ ! -f ./serial/$QC_MODULE_GOBISERIAL_NAME ]; then
#    echo -e "${RED}Error: Failed to generate kernel module $QC_MODULE_GOBISERIAL_NAME, installation abort."
#    $QC_LN_RM_MK_DIR/rm -rf $QC_MODULE_GOBISERIAL_NAME
#    $QC_MAKE_DIR/make clean
#    exit 1
# fi

# Copy .ko object to destination folders
# $QC_LN_RM_MK_DIR/cp ./serial/$QC_MODULE_GOBISERIAL_NAME $DEST_INS_SERIAL_PATH
# if [ ! -f $DEST_INS_SERIAL_PATH/$QC_MODULE_GOBISERIAL_NAME ]; then
#    echo -e "${RED}Error: Failed to copy $QC_MODULE_GOBISERIAL_NAME to installation path, installation abort."
#    $QC_LN_RM_MK_DIR/rm -r $DEST_INS_SERIAL_PATH
#    $QC_MAKE_DIR/make clean
#    exit 1
# fi

$QC_LN_RM_MK_DIR/cp ./InfParser/$QC_MODULE_INF_NAME $DEST_INF_PATH
if [ ! -f $DEST_INF_PATH/$QC_MODULE_INF_NAME ]; then
   echo -e "${RED}Error: Failed to copy $QC_MODULE_INF_NAME to installation path, installation abort.${RESET}"
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   $QC_MAKE_DIR/make clean
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./qdssdiag/$QC_MODULE_QDSS_DIAG_NAME $DEST_QDSS_DAIG_PATH
if [ ! -f $DEST_QDSS_DAIG_PATH/$QC_MODULE_QDSS_DIAG_NAME ]; then
   echo -e "${RED}Error: Failed to copy $QC_MODULE_QDSS_DIAG_NAME to installation path, installation abort.${RESET}"
   $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_QDSS_PATH
   $QC_MAKE_DIR/make clean
   exit 1
fi

$QC_LN_RM_MK_DIR/cp ./rmnet/$QC_MODULE_RMNET_NAME $DEST_INS_RMNET_PATH
if [ ! -f $DEST_INS_RMNET_PATH/$QC_MODULE_RMNET_NAME ]; then
  echo -e "${RED}Error: Failed to copy $QC_MODULE_RMNET_NAME to installation path, installation abort.${RESET}"
  $QC_LN_RM_MK_DIR/rm -rf $DEST_INS_RMNET_PATH
  exit 1
fi

$QC_MAKE_DIR/make clean

# echo SUBSYSTEMS==\"tty\", PROGRAM=\"$DEST_INS_SERIAL_PATH/qtidev.pl $DEST_INS_SERIAL_PATH/qtiname.inf %k\", SYMLINK+=\"%c\" , MODE=\"0666\" > ./qti_usb_device.rules
echo SUBSYSTEMS==\"GobiQMI\", MODE=\"0666\" >> ./qti_usb_device.rules
echo SUBSYSTEMS==\"GobiUSB\", MODE=\"0666\" >> ./qti_usb_device.rules
echo SUBSYSTEMS==\"GobiPorts\", MODE=\"0666\" >> ./qti_usb_device.rules

$QC_LN_RM_MK_DIR/chmod 644 ./qti_usb_device.rules
$QC_LN_RM_MK_DIR/cp ./qti_usb_device.rules $QC_UDEV_PATH
echo -e "Generated QC rules"

# udev rules for QMI
if [ -f $QC_UDEV_PATH/80-gobinet-usbdevice.rules ]; then
   RULE_EXIST="`grep -nr  'usb' $QC_UDEV_PATH/80-gobinet-usbdevice.rules`"
   if [ "$RULE_EXIST" != "" ]; then
      echo -e "Subsystem GobiQMI rule already exist in $QC_UDEV_PATH/80-gobinet-usbdevice.rules, nothing to add"
   else
      echo -e "Subsystem GobiQMI rule doesn't exist in $QC_UDEV_PATH/80-gobinet-usbdevice.rules, so adding now"
      $QC_LN_RM_MK_DIR/chmod 644 $QC_UDEV_PATH/80-gobinet-usbdevice.rules
      echo SUBSYSTEMS==\"usb\", ATTRS{idVendor}==\"05c6\", NAME=\"usb%n\" >> $QC_UDEV_PATH/80-gobinet-usbdevice.rules
   fi
else
   echo SUBSYSTEMS==\"usb\", ATTRS{idVendor}==\"05c6\", NAME=\"usb%n\" >> ./80-gobinet-usbdevice.rules
   $QC_LN_RM_MK_DIR/chmod 644 ./80-gobinet-usbdevice.rules
   $QC_LN_RM_MK_DIR/cp ./80-gobinet-usbdevice.rules $QC_UDEV_PATH
   echo -e "Creating new udev rule for GobiQMI in $QC_UDEV_PATH/80-gobinet-usbdevice.rules"
fi

# Informs udev deamon to reload the newly added device rule and re-trigger service
sudo udevadm control --reload-rules
sudo udevadm trigger

if [ ! -f $QC_UDEV_PATH/qti_usb_device.rules ]; then
   echo -e "${RED}Error: Failed to generate $QC_UDEV_PATH/qti_usb_device.rules"
   exit 1
fi

if [ ! -f $QC_UDEV_PATH/80-gobinet-usbdevice.rules ]; then
   echo -e "${RED}Error: Failed to generate $QC_UDEV_PATH/80-gobinet-usbdevice.rules"
   exit 1
fi

rm -f ./qti_usb_device.rules
rm -f ./80-net-setup-link.rules
rm -f ./80-gobinet-usbdevice.rules
echo -e "Removed local rules"

MODLOADED="`/sbin/lsmod | grep usbserial`"
if [ "$MODLOADED" == "" ]; then
   echo -e "To load dependency"
   echo -e "Loading module usbserial"
   if [[ $OSName =~ "Red Hat Enterprise Linux" ]]; then
      if [ -f $QC_SERIAL/usbserial.ko.xz ]; then
	xz -d $QC_SERIAL/usbserial.ko.xz 
      	$QC_MODBIN_DIR/insmod $QC_SERIAL/usbserial.ko
      fi
      MODLOADED="`/sbin/lsmod | grep usbserial`"
      if [ "$MODLOADED" == "" ]; then
        echo -e "$OSName: usbserial.ko module not present at $QC_SERIAL"
      fi
   else
	   $QC_MODBIN_DIR/insmod $QC_SERIAL/usbserial.ko
   fi
else
   echo -e "Module usbserial already in place"
fi

#echo Changing Permission of blacklist file
$QC_LN_RM_MK_DIR/chmod 777 $MODULE_BLACKLIST/blacklist.conf
echo -e "Changed Permission of blacklist file"
if [ "`grep -nr 'Qualcomm clients' /etc/modprobe.d/blacklist.conf`" != "" ]; then
   sed -i '/# Blacklist these module so that Qualcomm clients use only/d' /etc/modprobe.d/blacklist.conf
   sed -i '/# GobiNet, serial, qdssdiag, qtiDevInf driver/d' /etc/modprobe.d/blacklist.conf
fi
echo -e "# Blacklist these module so that Qualcomm clients use only" >> /etc/modprobe.d/blacklist.conf
echo -e "# GobiNet, serial, qdssdiag, qtiDevInf driver" >> /etc/modprobe.d/blacklist.conf

MOD_EXIST="`grep -nr  'blacklist qcserial' /etc/modprobe.d/blacklist.conf`"
if [ "$MOD_EXIST" != "" ]; then
   sed -i '/qcserial/d' $MODULE_BLACKLIST/blacklist.conf
fi
echo -e "blacklist qcserial" >> /etc/modprobe.d/blacklist.conf
echo -e "install qcserial /bin/false" >> /etc/modprobe.d/blacklist.conf
echo -e "blacklisted qcserial module"

MODLOADED="`/sbin/lsmod | grep qcserial`"
if [ "$MODLOADED" != "" ]; then
   echo -e "qcserial is found. Unloaded qcserial module"
   $QC_MODBIN_DIR/rmmod qcserial.ko
   MODLOADED="`/sbin/lsmod | grep qcserial`"
   if [ "$MODLOADED" != "" ]; then
      echo -e "${RED}Failed to unload qcserial.ko. try manually sudo rmmod ModuleName${RESET}"
   fi
fi
if [  -f $QC_SERIAL/qcserial.ko ]; then
   echo -e "qcserial is found. renamed to qcserial_dup"
   mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/qcserial.ko /lib/modules/`uname -r`/kernel/drivers/usb/serial/qcserial_dup
fi

MOD_EXIST="`grep -nr  'blacklist qmi_wwan' /etc/modprobe.d/blacklist.conf`"
if [ "$MOD_EXIST" != "" ]; then
   sed -i '/qmi_wwan/d' $MODULE_BLACKLIST/blacklist.conf
fi
echo -e "blacklist qmi_wwan" >> /etc/modprobe.d/blacklist.conf
echo -e "install qmi_wwan /bin/false" >> /etc/modprobe.d/blacklist.conf
echo -e "blacklisted qmi_wwan module"

MODLOADED="`/sbin/lsmod | grep qmi_wwan`"
if [ "$MODLOADED" != "" ]; then
   echo -e "qmi_wwan is found. Unloaded qmi_wwan module"
   echo -e "cdc-wdm is found. Unloaded cdc-wdm module"
   $QC_MODBIN_DIR/rmmod qmi_wwan.ko
   $QC_MODBIN_DIR/rmmod cdc-wdm.ko
   MODLOADED="`/sbin/lsmod | grep qmi_wwan`"
   if [ "$MODLOADED" != "" ]; then
      echo -e "${RED}Failed to unload qmi_wwan.ko. Run manually sudo rmmod ModuleName${RESET}"
   fi
   MODLOADED="`/sbin/lsmod | grep cdc_wdm`"
   if [ "$MODLOADED" != "" ]; then
      echo -e "${RED}Failed to unload cdc_wdm.ko. Run manually sudo rmmod ModuleName${RESET}"
   fi
fi
if [  -f $QC_QMI_WWAN/qmi_wwan.ko ]; then
   echo -e "qmi_wwan is found. renamed to qmi_wwan_dup"
   echo -e "cdc-wdm is found. renamed to cdc-wdm_dup"
   mv /lib/modules/`uname -r`/kernel/drivers/usb/class/cdc-wdm.ko /lib/modules/`uname -r`/kernel/drivers/usb/class/cdc-wdm_dup
   mv /lib/modules/`uname -r`/kernel/drivers/net/usb/qmi_wwan.ko /lib/modules/`uname -r`/kernel/drivers/net/usb/qmi_wwan_dup
fi

MOD_EXIST="`grep -nr  'blacklist option' /etc/modprobe.d/blacklist.conf`"
if [ "$MOD_EXIST" != "" ]; then
   sed -i '/option/d' $MODULE_BLACKLIST/blacklist.conf
fi
echo -e "blacklist option" >> /etc/modprobe.d/blacklist.conf
echo -e "install option /bin/false" >> /etc/modprobe.d/blacklist.conf
echo -e "blacklisted option module"

MODLOADED="`/sbin/lsmod | grep option`"
if [ "$MODLOADED" != "" ]; then
   echo -e "option is found. Unloaded option module"
   $QC_MODBIN_DIR/rmmod option.ko
   MODLOADED="`/sbin/lsmod | grep option`"
   if [ "$MODLOADED" != "" ]; then
      echo -e "${RED}Failed to unload option.ko. Run manually sudo rmmod option${RESET}"
   fi
fi
if [  -f $QC_SERIAL/option.ko ]; then
   echo -e "option is found. renamed to to option_dup"
   mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/option.ko /lib/modules/`uname -r`/kernel/drivers/usb/serial/option_dup
fi

MOD_EXIST="`grep -nr  'blacklist usb_wwan' /etc/modprobe.d/blacklist.conf`"
if [ "$MOD_EXIST" != "" ]; then
   sed -i '/usb_wwan/d' $MODULE_BLACKLIST/blacklist.conf
fi
echo -e "blacklist usb_wwan" >> /etc/modprobe.d/blacklist.conf
echo -e "install usb_wwan /bin/false" >> /etc/modprobe.d/blacklist.conf
echo -e "blacklisted usb_wwan module"

MODLOADED="`/sbin/lsmod | grep usb_wwan`"
if [ "$MODLOADED" != "" ]; then
   echo -e "usb_wwan is found. Unloaded usb_wwan module"
   $QC_MODBIN_DIR/rmmod usb_wwan.ko
   MODLOADED="`/sbin/lsmod | grep usb_wwan`"
   if [ "$MODLOADED" != "" ]; then
      echo -e "${RED}Failed to unload usb_wwan.ko. Run manually sudo rmmod ModuleName${RESET}"
   fi
fi
if [  -f $QC_SERIAL/usb_wwan.ko ]; then
   echo -e "usb_wwan is found. renamed to usb_wwan_dup"
   mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/usb_wwan.ko /lib/modules/`uname -r`/kernel/drivers/usb/serial/usb_wwan_dup
fi

# MODLOADED="`/sbin/lsmod | grep serial`"
# if [ "$MODLOADED" != "" ]; then
#    ( $QC_MODBIN_DIR/rmmod $QC_MODULE_GOBISERIAL_NAME && echo -e "$QC_MODULE_GOBISERIAL_NAME removed successfully.." ) || { echo -e "$QC_MODULE_GOBISERIAL_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"; exit 1; }
# fi

# echo -e "Loading module $QC_MODULE_GOBISERIAL_NAME"
# $QC_MODBIN_DIR/insmod $DEST_INS_SERIAL_PATH/$QC_MODULE_GOBISERIAL_NAME gQTIModemInfFilePath=$QC_MODEM_INF_PATH debug=0

MODLOADED="`/sbin/lsmod | grep qdssdiag`"
if [ "$MODLOADED" != "" ]; then
   ($QC_MODBIN_DIR/rmmod $QC_MODULE_QDSS_DIAG_NAME && echo -e "$QC_MODULE_QDSS_DIAG_NAME removed successfully..") ||  { echo -e "$QC_MODULE_QDSS_DIAG_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"; exit 1; }
fi

echo -e "Loading module dependency"
MODLOADED="`/sbin/lsmod | grep mii`"
if [ "$MODLOADED" == "" ]; then
   echo -e "Loading module mii"
   if [[ $OSName =~ "Red Hat Enterprise Linux" ]] || [[ $OSName =~ "Fedora Linux" ]] || [[ $OSName =~ "Ubuntu 24.04" ]]; then
      if [ -f $QC_NET/mii.ko.xz ]; then
        xz -d $QC_NET/mii.ko.xz
      fi
      if [ -f $QC_NET/mii.ko.zst ]; then
        unzstd -d $QC_NET/mii.ko.zst
      fi
      if [ -f $QC_NET/mii.ko ]; then
         $QC_MODBIN_DIR/insmod $QC_NET/mii.ko
      fi
      MODLOADED="`/sbin/lsmod | grep mii`"
      if [ "$MODLOADED" == "" ]; then
        echo -e "$OSName: mii.ko module not present at $QC_NET"
      fi
   else
      $QC_MODBIN_DIR/insmod $QC_NET/mii.ko
   fi
else
   echo -e "Module mii already in place"
fi

MODLOADED="`/sbin/lsmod | grep usbnet`"
if [ "$MODLOADED" == "" ]; then
   echo -e "Loading module usbnet"
   if [[ $OSName =~ "Red Hat Enterprise Linux" ]] || [[ $OSName =~ "Fedora Linux" ]] || [[ $OSName =~ "Ubuntu 24.04" ]]; then
      if [ -f $QC_QMI_WWAN/usbnet.ko.xz ]; then
        xz -d $QC_QMI_WWAN/usbnet.ko.xz
      fi
      if [ -f $QC_QMI_WWAN/usbnet.ko.zst ]; then
        unzstd -d $QC_QMI_WWAN/usbnet.ko.zst
      fi
      if [ -f $QC_QMI_WWAN/usbnet.ko ]; then
         $QC_MODBIN_DIR/insmod $QC_QMI_WWAN/usbnet.ko
      fi
      MODLOADED="`/sbin/lsmod | grep usbnet`"
      if [ "$MODLOADED" == "" ]; then
        echo -e "$OSName: usbnet.ko module not present at $QC_QMI_WWAN"
      fi
   else
   	$QC_MODBIN_DIR/insmod $QC_QMI_WWAN/usbnet.ko
   fi
else
   echo -e "Module usbnet already in place"
fi

MODLOADED="`/sbin/lsmod | grep GobiNet`"
if [ "$MODLOADED" != "" ]; then
  ($QC_MODBIN_DIR/rmmod $QC_MODULE_RMNET_NAME && echo -e "$QC_MODULE_RMNET_NAME removed successfully..") || { echo -e "$QC_MODULE_RMNET_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"; exit 1; }
fi

MODLOADED="`/sbin/lsmod | grep qtiDevInf`"
if [ "$MODLOADED" != "" ]; then
  ($QC_MODBIN_DIR/rmmod $QC_MODULE_INF_NAME && echo -e "$QC_MODULE_INF_NAME removed successfully..") || { echo -e "$QC_MODULE_INF_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"; exit 1; }
fi

echo -e "Loading new module $QC_MODULE_INF_NAME"
$QC_MODBIN_DIR/insmod $DEST_INF_PATH/$QC_MODULE_INF_NAME debug_g=0
MODLOADED="`/sbin/lsmod | grep qtiDevInf`"
if [ "$MODLOADED" == "" ]; then
   echo -e "${RED}Failed to load new $QC_MODULE_INF_NAME module${RESET}"
   exit 1
fi
echo -e "Loading new module $QC_MODULE_QDSS_DIAG_NAME"
$QC_MODBIN_DIR/insmod $DEST_QDSS_DAIG_PATH/$QC_MODULE_QDSS_DIAG_NAME gQdssInfFilePath=$QC_QDSS_INF_PATH gDiagInfFilePath=$QC_DIAG_INF_PATH debug_g=0
MODLOADED="`/sbin/lsmod | grep qdssdiag`"
if [ "$MODLOADED" == "" ]; then
   echo -e "${RED}Failed to load new $QC_MODULE_INF_NAME module${RESET}"
   exit 1
fi
echo -e "Loading new module $QC_MODULE_RMNET_NAME"
$QC_MODBIN_DIR/insmod $DEST_INS_RMNET_PATH/$QC_MODULE_RMNET_NAME debug_g=0 debug_aggr=0
MODLOADED="`/sbin/lsmod | grep GobiNet`"
if [ "$MODLOADED" == "" ]; then
   echo -e "${RED}Failed to load new $QC_MODULE_INF_NAME module${RESET}"
   exit 1
fi
# update modules.dep and modules.alias
depmod

$QC_MAKE_DIR/find $DEST_QTI_PATH -type d -exec chmod 0755 {} \;

echo -e "Qualcomm GobiNet driver is installed at $DEST_INS_RMNET_PATH"
echo -e "Qualcomm INF Parser driver is installed at $DEST_INF_PATH"
echo -e "Qualcomm QDSS/Diag driver is installed at $DEST_QDSS_DAIG_PATH"
echo -e "Qualcomm Modem driver is installed at $DEST_INS_SERIAL_PATH"
echo -e "Qualcomm Gobi device naming rules are installed at $QC_UDEV_PATH"

if [ -f "$DEST_QUD_PATH/ReleaseNotes*.txt" ]; then
   echo -e "QUD Release Notes available at $DEST_QUD_PATH"
fi

MODUPDATE="`grep -r QCDevInf /etc/modules`"
if [ "$MODUPDATE" == "QCDevInf" ]; then
  sed -i '/QCDevInf/d' /etc/modules
fi
MODUPDATE="`grep -nr  qtiDevInf /etc/modules`"
if [ "$MODUPDATE" == "" ]; then
	echo -e "qtiDevInf" >> /etc/modules
fi

MODUPDATE="`grep -nr  qdssdiag /etc/modules`"
if [ "$MODUPDATE" == "" ]; then
	echo -e "qdssdiag" >> /etc/modules
fi

MODUPDATE="`grep -nr  GobiNet /etc/modules`"
if [ "$MODUPDATE" == "" ]; then
	echo -e "GobiNet" >> /etc/modules
fi

# MODUPDATE="`grep -nr  serial /etc/modules`"
# if [ "$MODUPDATE" == "" ]; then
# 	echo -e "serial" >> /etc/modules
# fi

if [[ $OSName != *"Red Hat Enterprise Linux"* ]]; then
   MODUPDATE="`grep -nr  'iface usb0 inet static' /etc/network/interfaces`"
   if [ "$MODUPDATE" == "" ]; then
	echo -e "iface usb0 inet static" >> /etc/network/interfaces
  fi
fi

exit 0
