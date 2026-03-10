#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

DEST_QCOM_PATH=/opt/qcom/
DEST_QUD_PATH=/opt/qcom/QUD
DEST_SIGN_PATH=/opt/qcom/QUD/sign
OLD_DEST_SIGN_PATH=/opt/QTI/sign
QCOM_USBNET_DIR=qcom_usbnet
QCOM_USB_DIR=qcom_usb
QCOM_USBINF_DIR=InfParser
QCOM_SERIAL_DIR=qcom_serial
DEST_QCOM_USBNET_PATH=$DEST_QUD_PATH/$QCOM_USBNET_DIR
DEST_QCOM_USB_PATH=$DEST_QUD_PATH/$QCOM_USB_DIR
DEST_QCOM_USBINF_PARSER_PATH=$DEST_QUD_PATH/$QCOM_USBINF_DIR
DEST_MODEM_SERIAL_PATH=$DEST_QUD_PATH/$QCOM_SERIAL_DIR
QCOM_UDEV_PATH=/etc/udev/rules.d
QCOM_MODULE_INF_NAME=qtiDevInf.ko
QCOM_MODEM_SERIAL_NAME=qcom_serial.ko
QCOM_USB_MODULE_NAME=qcom_usb.ko
QCOM_USBNET_MODULE_NAME=qcom_usbnet.ko
MODULE_BLACKLIST_PATH=/lib/modules/`uname -r`/kernel/drivers/usb/serial
QCOM_USBNET_AND_QMI_WWAN=/lib/modules/`uname -r`/kernel/drivers/net/usb
QCOM_NET_DEPENDENCY_PATH=/lib/modules/`uname -r`/kernel/drivers/net
QCOM_USB_KERNEL_PATH=/lib/modules/`uname -r`/kernel/drivers/usb/misc
QCOM_MODBIN_DIR=/sbin
QCOM_MAKE_DIR=/usr/bin
QCOM_LN_RM_MK_DIR=/bin
QCOM_DRIVER_DEBUG_ENABLE=$2
MODULE_BLACKLIST_CONFIG=/etc/modprobe.d
OS_RELEASE="`cat /etc/os-release | grep PRETTY_NAME`"
OSName=`echo $OS_RELEASE | awk -F= '{printf $2}'`
KERNEL_VERSION=`uname -r`
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
RESET='\033[0m'

#check and install mokutil package
if [ ! -f "$QCOM_MAKE_DIR/mokutil" ]; then
   echo -e ${RED}"Error: mokutil not found, installing..\n"${RESET}
fi

if [ ! -f "$QCOM_MAKE_DIR/keyctl" ]; then
   echo -e ${RED}"Error: keyutils not found, installing..\n"${RESET}
fi

if [[ ! -f "$QCOM_MAKE_DIR/mokutil" ]] || [[ ! -f "$QCOM_MAKE_DIR/keyctl" ]]; then
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

QCOM_SECURE_BOOT_CHECK=`mokutil --sb-state`

if [[ $QCOM_SECURE_BOOT_CHECK = "SecureBoot enabled" ]]; then
   QCOM_PUBLIC_KEY_VERIFY=`mokutil --test-key $DEST_SIGN_PATH/Signkey_pub.der`
fi

if [  ! -d $DEST_QCOM_PATH  ]; then
   echo -e ${RED}"Error: $DEST_QCOM_PATH doesn't exist. Creating Now."${RESET}
   $QCOM_LN_RM_MK_DIR/mkdir -m 0755 -p $DEST_QCOM_PATH
fi

if [  ! -d $DEST_QUD_PATH  ]; then
   echo -e ${RED}"Error: $DEST_QUD_PATH doesn't exist. Creating Now."${RESET}
   $QCOM_LN_RM_MK_DIR/mkdir -m 0755 -p $DEST_QUD_PATH
fi
$QCOM_LN_RM_MK_DIR/chmod -R 777 $DEST_QUD_PATH

if [ $# == 0 ]; then
	echo -e ${RED}"Usage:"${RESET}
   	echo -e ${RED}"driver_load.sh OPTION1 OPTION2"${RESET}
	echo -e ${RED}"OPTION 1"${RESET}
	echo -e ${RED}"driver_load.sh <install | uninstall>"${RESET}
	echo -e ${RED}"OPTION 2"${RESET}
	echo -e ${RED}"driver_load.sh <install | uninstall> <enable debug messages for driver - choose option qcom_usb | qcom_usbnet | all>"${RESET}
	echo -e ${RED}"example: ./driver_load.sh install qcom_usb"${RESET}
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

      if [ -d $DEST_QCOM_USB_PATH ]; then
         $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH
         if [ ! -d $DEST_QCOM_USB_PATH ]; then
            echo -e "Successfully removed $DEST_QCOM_USB_PATH"
         else
            echo -e ${RED}"Failed to remove $DEST_QCOM_USB_PATH"${RESET}
         fi
      else
         echo -e "$DEST_QCOM_USB_PATH does not exist, nothing to remove"
      fi
      if [ -d $DEST_QCOM_USBNET_PATH ]; then
         $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
         if [ ! -d $DEST_QCOM_USBNET_PATH ]; then
            echo -e "Successfully removed $DEST_QCOM_USBNET_PATH"
         else
            echo -e ${RED}"Failed to remove $DEST_QCOM_USBNET_PATH"${RESET}
         fi
      else
         echo -e "$DEST_QCOM_USBNET_PATH does not exist, nothing to remove"
      fi

      if [ -d $DEST_MODEM_SERIAL_PATH ]; then
         $QCOM_LN_RM_MK_DIR/rm -rf $DEST_MODEM_SERIAL_PATH
         if [ ! -d $DEST_MODEM_SERIAL_PATH ]; then
            echo -e "Successfully removed $DEST_MODEM_SERIAL_PATH"
         else
            echo -e "${RED}Failed to remove $DEST_MODEM_SERIAL_PATH${RESET}"
         fi
      else
         echo -e "$DEST_MODEM_SERIAL_PATH does not exist, nothing to remove"
      fi

      if [ -d $DEST_QCOM_USBINF_PARSER_PATH ]; then
         $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBINF_PARSER_PATH
         if [ ! -d $DEST_QCOM_USBINF_PARSER_PATH ]; then
            echo -e "Successfully removed $DEST_QCOM_USBINF_PARSER_PATH"
         else
            echo -e ${RED}"Failed to remove $DEST_QCOM_USBINF_PARSER_PATH"${RESET}
         fi
      else
         echo -e "$DEST_QCOM_USBINF_PARSER_PATH does not exist, nothing to remove"
      fi

      if [ -f $QCOM_UDEV_PATH/qcom-usb-devices.rules ]; then
         rm -r $QCOM_UDEV_PATH/qcom-usb-devices.rules
         if [ ! -f $QCOM_UDEV_PATH/qcom-usb-devices.rules ]; then
            echo -e "Successfully removed $QCOM_UDEV_PATH/qcom-usb-devices.rules"
         else
            echo -e ${RED}"Failed to remove $QCOM_UDEV_PATH/qcom-usb-devices.rules"${RESET}
         fi
      else
         echo -e "$QCOM_UDEV_PATH/qcom-usb-devices.rules does not exist, nothing to remove"
      fi
      if [ -f $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules ]; then
         sed -i '/usb/d' $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules
         if [ ! -s $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules ]; then
            echo -e "Removed qcom-usbnet rule from $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules"
            rm -rf $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules
            if [ ! -f $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules ]; then
               echo -e "File was empty. Removed $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules successfully"
            else
               echo -e "File is empty but Failed to remove $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules"
            fi
         else
               echo -e "Pre-Existing data $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules, so not removing it."
         fi
      else
         echo -e "$QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules does not exist, nothing to remove"
      fi

      # Informs udev deamon to reload the newly added device rule and re-trigger service
      sudo udevadm control --reload-rules
      sudo udevadm trigger

      if [ "`lsmod | grep -w qcom_serial`" ]; then
         ($QCOM_MODBIN_DIR/rmmod $QCOM_MODEM_SERIAL_NAME && echo -e "$QCOM_MODEM_SERIAL_NAME removed successfully") || { echo -e "$QCOM_MODEM_SERIAL_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"${RESET}; exit 1; }
      else
         echo -e "Module $QCOM_MODEM_SERIAL_NAME is not currently loaded"
      fi
      if [ "`lsmod | grep -w qcom_usbnet`" ]; then
         ( $QCOM_MODBIN_DIR/rmmod $QCOM_USBNET_MODULE_NAME && echo -e "$QCOM_USBNET_MODULE_NAME removed successfully" ) || { echo -e "$QCOM_USBNET_MODULE_NAME in use"; echo -e ${RED}"Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e ${RED}"ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS$"; echo -e ${GREEN}"Try $1ation again!"${RESET}; exit 1; }
      else
         echo -e "Module $QCOM_USBNET_MODULE_NAME is not currently loaded"
      fi
      if [ "`lsmod | grep -w qcom_usb`" ]; then
         ($QCOM_MODBIN_DIR/rmmod $QCOM_USB_MODULE_NAME && echo -e "$QCOM_USB_MODULE_NAME removed successfully") || { echo -e "$QCOM_USB_MODULE_NAME in use"; echo -e ${RED}"Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e ${RED}"ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS$"; echo -e ${GREEN}"Try $1ation again!"${RESET}; exit 1; }
      else
         echo -e "Module $QCOM_USB_MODULE_NAME is not currently loaded"
      fi
      if [ "`lsmod | grep -w qtiDevInf`" ]; then
         ($QCOM_MODBIN_DIR/rmmod $QCOM_MODULE_INF_NAME && echo -e "$QCOM_MODULE_INF_NAME removed successfully") || { echo -e "$QCOM_MODULE_INF_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"${RESET}; exit 1; }
      else
         echo -e "Module $QCOM_MODULE_INF_NAME is not currently loaded"
      fi

      MODLOADED="`/sbin/lsmod | grep usb_wwan`"
      if [ "$MODLOADED" != "" ]; then
         echo -e "usb_wwan module is already loaded. nothing to do"
      fi
      if [  -f $MODULE_BLACKLIST_PATH/usb_wwan_dup* ]; then
         echo -e "usb_wwan_dup is found. restoring to usb_wwan"
         mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/usb_wwan_dup* /lib/modules/`uname -r`/kernel/drivers/usb/serial/usb_wwan.ko
         #$QCOM_MODBIN_DIR/insmod /lib/modules/`uname -r`/kernel/drivers/usb/serial/usb_wwan.ko

         MODLOADED="`/sbin/lsmod | grep usb_wwan`"
         if [ "$MODLOADED" != "" ]; then
            echo -e "Successfully loaded usb_wwan module."
         fi
      fi

      MODLOADED="`/sbin/lsmod | grep -w qcserial`"
      if [ "$MODLOADED" != "" ]; then
         echo -e "qcserial module is already loaded. nothing to do"
      fi
      if [  -f $MODULE_BLACKLIST_PATH/qcserial_dup* ]; then
         echo -e "qcserial_dup is found. restoring to qcserial"
         mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/qcserial_dup* /lib/modules/`uname -r`/kernel/drivers/usb/serial/qcserial.ko
         #$QCOM_MODBIN_DIR/insmod /lib/modules/`uname -r`/kernel/drivers/usb/serial/qcserial.ko

         MODLOADED="`/sbin/lsmod | grep -w qcserial`"
         if [ "$MODLOADED" != "" ]; then
            echo -e "Successfully loaded qcserial module."
         fi
      fi

      MODLOADED="`/sbin/lsmod | grep option`"
      if [ "$MODLOADED" != "" ]; then
         echo -e "option module is already loaded. nothing to do"
      fi
      if [  -f $MODULE_BLACKLIST_PATH/option_dup* ]; then
         echo -e "option_dup is found. restoring to option"
         mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/option_dup* /lib/modules/`uname -r`/kernel/drivers/usb/serial/option.ko
         #$QCOM_MODBIN_DIR/insmod /lib/modules/`uname -r`/kernel/drivers/usb/serial/option.ko

         MODLOADED="`/sbin/lsmod | grep option`"
         if [ "$MODLOADED" != "" ]; then
            echo -e "Successfully loaded option module."
         fi
      fi

      MODLOADED="`/sbin/lsmod | grep -w qmi_wwan`"
      if [ "$MODLOADED" != "" ]; then
         echo -e "qmi_wwan module is already loaded. nothing to do"
      fi
      if [  -f $QCOM_USBNET_AND_QMI_WWAN/qmi_wwan_dup* ]; then
         echo -e "qmi_wwan_dup is found. restoring to qmi_wwan"
         mv /lib/modules/`uname -r`/kernel/drivers/usb/class/cdc-wdm_dup* /lib/modules/`uname -r`/kernel/drivers/usb/class/cdc-wdm.ko
         mv /lib/modules/`uname -r`/kernel/drivers/net/usb/qmi_wwan_dup* /lib/modules/`uname -r`/kernel/drivers/net/usb/qmi_wwan.ko
         #$QCOM_MODBIN_DIR/insmod /lib/modules/`uname -r`/kernel/drivers/usb/class/cdc-wdm.ko
         #$QCOM_MODBIN_DIR/insmod /lib/modules/`uname -r`/kernel/drivers/net/usb/qmi_wwan.ko

         MODLOADED="`/sbin/lsmod | grep -w qmi_wwan`"
         if [ "$MODLOADED" != "" ]; then
            echo -e "Successfully loaded qmi_wwan module."
         fi
      fi

      if  [[ $OSName =~ "Ubuntu 24.04" ]]; then
         if [ -f $QCOM_NET_DEPENDENCY_PATH/mii.ko ]; then
            $QCOM_LN_RM_MK_DIR/rm -rf $QCOM_NET_DEPENDENCY_PATH/mii.ko
         fi
         if [ -f $QCOM_USBNET_AND_QMI_WWAN/usbnet.ko ]; then
            $QCOM_LN_RM_MK_DIR/rm -rf $QCOM_USBNET_AND_QMI_WWAN/usbnet.ko
         fi
      fi

      if [ "`grep -nr 'Qualcomm clients' $MODULE_BLACKLIST_CONFIG/blacklist.conf`" != "" ]; then
         sed -i '/# Blacklist these module so that Qualcomm clients use only/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
         sed -i '/# qcom_usbnet, qcom_usb driver/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
      fi

      MOD_EXIST="`grep -nr  'blacklist qcserial' $MODULE_BLACKLIST_CONFIG/blacklist.conf`"
      if [ "$MOD_EXIST" != "" ]; then
         sed -i '/qcserial/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
         echo -e "Successfully removed qcserial from $MODULE_BLACKLIST_CONFIG/blacklist.conf"
      fi

      MOD_EXIST="`grep -nr  'blacklist qmi_wwan' $MODULE_BLACKLIST_CONFIG/blacklist.conf`"
      if [ "$MOD_EXIST" != "" ]; then
         sed -i '/qmi_wwan/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
         echo -e "Successfully removed qmi_wwan from $MODULE_BLACKLIST_CONFIG/blacklist.conf"
      fi

      MOD_EXIST="`grep -nr  'blacklist option' $MODULE_BLACKLIST_CONFIG/blacklist.conf`"
      if [ "$MOD_EXIST" != "" ]; then
         sed -i '/option/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
         echo -e "Successfully removed option from $MODULE_BLACKLIST_CONFIG/blacklist.conf"
      fi

      MOD_EXIST="`grep -nr  'blacklist usb_wwan' $MODULE_BLACKLIST_CONFIG/blacklist.conf`"
      if [ "$MOD_EXIST" != "" ]; then
         sed -i '/usb_wwan/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
         echo -e "Successfully removed usb_wwan from $MODULE_BLACKLIST_CONFIG/blacklist.conf"
      fi

      #change to permission to default mode
      $QCOM_LN_RM_MK_DIR/chmod 644 $MODULE_BLACKLIST_CONFIG/blacklist.conf

      echo -e "Removed modules for /etc/modules."
      MODUPDATE="`grep -r qtiDevInf /etc/modules`"
      if [ "$MODUPDATE" == "qtiDevInf" ]; then
	  sed -i '/qtiDevInf/d' /etc/modules
      fi
      MODUPDATE="`grep -r qcom_usb /etc/modules`"
      if [ "$MODUPDATE" == "qcom_usb" ]; then
	  sed -i '/qcom_usb/d' /etc/modules
      fi
      MODUPDATE="`grep -r qcom_usbnet /etc/modules`"
      if [ "$MODUPDATE" == "qcom_usbnet" ]; then
	  sed -i '/qcom_usbnet/d' /etc/modules
      fi
      MODUPDATE="`grep -r qcom_serial /etc/modules`"
      if [ "$MODUPDATE" == "qcom_serial" ]; then
	  sed -i '/qcom_serial/d' /etc/modules
      fi
      if [[ $OSName != *"Red Hat Enterprise Linux"* ]]; then
      	MODUPDATE="`grep -nr  'iface usb0 inet static' /etc/network/interfaces`"
      	if [ "$MODUPDATE" != "" ]; then
        	 sed -i '/iface usb0 inet static/d' /etc/network/interfaces
      	fi
      fi

      if [ -f $MODULE_BLACKLIST_PATH/$QCOM_MODEM_SERIAL_NAME ]; then
         echo -e "Removed module from $MODULE_BLACKLIST_PATH/$QCOM_MODEM_SERIAL_NAME"
         rm -rf $MODULE_BLACKLIST_PATH/$QCOM_MODEM_SERIAL_NAME
      fi

      if [ -f $QCOM_USB_KERNEL_PATH/$QCOM_USB_MODULE_NAME ]; then
         echo -e "Removed module from $QCOM_USB_KERNEL_PATH/$QCOM_USB_MODULE_NAME"
         rm -rf $QCOM_USB_KERNEL_PATH/$QCOM_USB_MODULE_NAME
      fi

      if [ -f $QCOM_USB_KERNEL_PATH/$QCOM_MODULE_INF_NAME ]; then
         echo -e "Removed module from $QCOM_USB_KERNEL_PATH/$QCOM_MODULE_INF_NAME"
         rm -rf $QCOM_USB_KERNEL_PATH/$QCOM_MODULE_INF_NAME
      fi

      if [ -f $QCOM_USBNET_AND_QMI_WWAN/$QCOM_USBNET_MODULE_NAME ]; then
         echo -e "Removed module from $QCOM_USBNET_AND_QMI_WWAN/$QCOM_USBNET_MODULE_NAME"
         rm -rf $QCOM_USBNET_AND_QMI_WWAN/$QCOM_USBNET_MODULE_NAME
      fi
	   # update modules.dep and modules.alias
      depmod

	   echo -e "Uninstallation completed successfully."
      exit 0
   else
      if [ $1 != "install" ]; then
            echo -e ${RED}"Usage: OPTION1 mandatory"${RESET}
            echo -e ${RED}"./driver_load.sh OPTION1 OPTION2"${RESET}
	    echo -e ${RED}"OPTION 1: Only install|uninstall driver"${RESET}
	    echo -e ${RED}"./driver_load.sh <install | uninstall>"${RESET}
	    echo -e ${RED}"OPTION 2: install driver with debug messages enabled"${RESET}
	    echo -e ${RED}"driver_load.sh <install | uninstall> <enable debug messages for driver - choose option qcom_usb | qcom_usbnet | all>"${RESET}
            echo -e ${RED}"example: ./driver_load.sh install qcom_usb"${RESET}
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
if [[ $OSName =~ "Ubuntu 22." ]] && (( $major_ver >= 6 && $minor_ver >= 5 )); then
   echo -e "Installing gcc 12 version ..."
   sudo apt install -y gcc-12 g++-12
fi
if [[ $OSName =~ "Ubuntu 24." ]] && (( $major_ver >= 6 && $minor_ver >= 14 )); then
   echo -e "Installing gcc 14 version ..."
   sudo apt install -y gcc-14 g++-14
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
echo $DEST_MODEM_SERIAL_PATH
echo $DEST_QCOM_USB_PATH
echo $DEST_QCOM_USBNET_PATH

# this script will use in qik uninstallation process
if [ -f "$DEST_QUD_PATH/qcom_drivers.sh" ]; then
   echo -e "Delete old file and copy again (qcom_drivers.sh)"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QUD_PATH/qcom_drivers.sh
   $QCOM_LN_RM_MK_DIR/cp -rf ./qcom_drivers.sh $DEST_QUD_PATH/
else
   echo -e "Does not exist and copying now.. (qcom_drivers.sh)"${RESET}
   $QCOM_LN_RM_MK_DIR/cp -rf ./qcom_drivers.sh $DEST_QUD_PATH/
fi

# Create directories
if [ -d $DEST_MODEM_SERIAL_PATH ]; then
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_MODEM_SERIAL_PATH
fi

$QCOM_LN_RM_MK_DIR/mkdir -m 0755 -p $DEST_MODEM_SERIAL_PATH
if [  ! -d $DEST_MODEM_SERIAL_PATH  ]; then
   echo -e ${RED}"Error: Failed to create installation path: $DEST_MODEM_SERIAL_PATH"${RESET}
   exit 1
fi

if [ -d $DEST_QCOM_USB_PATH ]; then
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH
fi

$QCOM_LN_RM_MK_DIR/mkdir -m 0755  -p $DEST_QCOM_USB_PATH
if [  ! -d $DEST_QCOM_USB_PATH  ]; then
   echo -e ${RED}"Error: Failed to create installation path: $DEST_QCOM_USB_PATH"${RESET}
   exit 1
fi

if [ -d $DEST_QCOM_USBINF_PARSER_PATH ]; then
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBINF_PARSER_PATH
fi

$QCOM_LN_RM_MK_DIR/mkdir -m 0755  -p $DEST_QCOM_USBINF_PARSER_PATH
if [  ! -d $DEST_QCOM_USBINF_PARSER_PATH  ]; then
   echo -e ${RED}"Error: Failed to create installation path: $DEST_QCOM_USBINF_PARSER_PATH"${RESET}
   exit 1
fi

if [ -d $DEST_QCOM_USBNET_PATH ]; then
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
fi

$QCOM_LN_RM_MK_DIR/mkdir -m 0755  -p $DEST_QCOM_USBNET_PATH
if [  ! -d $DEST_QCOM_USBNET_PATH  ]; then
   echo -e ${RED}"Error: Failed to create installation pathL $DEST_QCOM_USBNET_PATH"${RESET}
   exit 1
fi

# Important: Do not delete or recreate the "sign" folder.
# The sign files will be automatically generated whenever the Signpub key is not enrolled.
$QCOM_LN_RM_MK_DIR/mkdir -m 0777 -p $DEST_SIGN_PATH
if [ ! -d $DEST_SIGN_PATH ]; then
   echo -e ${RED}"Error: Failed to create installation path, please run installer under root."${RESET}
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_SERIAL_DIR/qcom_serial.c $DEST_MODEM_SERIAL_PATH
if [ ! -f $DEST_MODEM_SERIAL_PATH/qcom_serial.c ]; then
   echo -e "${RED}Error: Failed to copy qcom_serial.c to installation path"${RESET}
   rm -r $DEST_MODEM_SERIAL_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_SERIAL_DIR/qcom_serial.h $DEST_MODEM_SERIAL_PATH
if [ ! -f $DEST_MODEM_SERIAL_PATH/qcom_serial.h ]; then
   echo -e "${RED}Error: Failed to copy qcom_serial.h to installation path"${RESET}
   rm -r $DEST_MODEM_SERIAL_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_SERIAL_DIR/Makefile  $DEST_MODEM_SERIAL_PATH
if [ ! -f $DEST_MODEM_SERIAL_PATH/Makefile ]; then
   echo -e "${RED}Error: Failed to copy Makefile installation path"${RESET}
   rm -r $DEST_MODEM_SERIAL_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_SERIAL_DIR/qtidev.pl  $DEST_MODEM_SERIAL_PATH
if [ ! -f $DEST_MODEM_SERIAL_PATH/qtidev.pl ]; then
   echo -e "${RED}Error: Failed to copy qtidev.pl installation path"${RESET}
   rm -r $DEST_MODEM_SERIAL_PATH
   exit 1
fi
$QCOM_LN_RM_MK_DIR/chmod 755 $DEST_MODEM_SERIAL_PATH/qtidev.pl

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_SERIAL_DIR/qtiname.inf $DEST_MODEM_SERIAL_PATH
if [ ! -f $DEST_MODEM_SERIAL_PATH/qtiname.inf ]; then
   echo -e "${RED}Error: Failed to copy qtiname.inf installation path"${RESET}
   rm -r $DEST_MODEM_SERIAL_PATH
   exit 1
fi
$QCOM_LN_RM_MK_DIR/chmod 644 $DEST_MODEM_SERIAL_PATH/qtiname.inf

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_SERIAL_DIR/qtimdm.inf $DEST_MODEM_SERIAL_PATH
if [ ! -f $DEST_MODEM_SERIAL_PATH/qtimdm.inf ]; then
   echo -e "${RED}Error: Failed to copy qtimdm.inf installation path"${RESET}
   rm -r $DEST_MODEM_SERIAL_PATH
   exit 1
fi
$QCOM_LN_RM_MK_DIR/chmod 644 $DEST_MODEM_SERIAL_PATH/qtimdm.inf

$QCOM_LN_RM_MK_DIR/cp ./InfParser/qtiDevInf.h $DEST_QCOM_USBINF_PARSER_PATH/
if [ ! -f $DEST_QCOM_USBINF_PARSER_PATH/qtiDevInf.h ]; then
   echo -e "${RED}Error: Failed to copy 'InfParser/qtiDevInf.h' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBINF_PARSER_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./InfParser/qtiDevInf.c $DEST_QCOM_USBINF_PARSER_PATH/
if [ ! -f $DEST_QCOM_USBINF_PARSER_PATH/qtiDevInf.c ]; then
   echo -e "${RED}Error: Failed to copy 'InfParser/qtiDevInf.c' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBINF_PARSER_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./InfParser/Makefile $DEST_QCOM_USBINF_PARSER_PATH/
if [ ! -f $DEST_QCOM_USBINF_PARSER_PATH/Makefile ]; then
   echo -e "${RED}Error: Failed to copy 'InfParser/Makefile' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBINF_PARSER_PATH
   exit 1
fi

# copy modem INFs to QCOM_USB folder for enumeration of modem node
$QCOM_LN_RM_MK_DIR/cp ./$QCOM_SERIAL_DIR/qtiname.inf $DEST_QCOM_USB_PATH/
if [ ! -f $DEST_QCOM_USB_PATH/qtiname.inf ]; then
   echo -e "${RED}Error: Failed to copy qtiname.inf installation path"${RESET}
   rm -r $DEST_QCOM_USB_PATH/
   exit 1
fi
$QCOM_LN_RM_MK_DIR/chmod 644 $DEST_MODEM_SERIAL_PATH/qtiname.inf

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_SERIAL_DIR/qtimdm.inf $DEST_QCOM_USB_PATH/
if [ ! -f $DEST_QCOM_USB_PATH/qtimdm.inf ]; then
   echo -e "${RED}Error: Failed to copy qtiname.inf installation path"${RESET}
   rm -r $DEST_QCOM_USB_PATH/
   exit 1
fi
$QCOM_LN_RM_MK_DIR/chmod 644 $DEST_QCOM_USB_PATH/qtimdm.inf


$QCOM_LN_RM_MK_DIR/cp ./$QCOM_USB_DIR/qdbusb.inf $DEST_QCOM_USB_PATH/
if [ ! -f $DEST_QCOM_USB_PATH/qdbusb.inf ]; then
   echo -e "${RED}Error: Failed to copy 'qdbusb.inf' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_USB_DIR/qtiser.inf $DEST_QCOM_USB_PATH/
if [ ! -f $DEST_QCOM_USB_PATH/qtiser.inf ]; then
   echo -e "${RED}Error: Failed to copy 'qtiser.inf' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/chmod 644 $DEST_QCOM_USB_PATH/qdbusb.inf
$QCOM_LN_RM_MK_DIR/chmod 644 $DEST_QCOM_USB_PATH/qtiser.inf

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_USB_DIR/qtiDevInf.h $DEST_QCOM_USB_PATH/
if [ ! -f $DEST_QCOM_USB_PATH/qtiDevInf.h ]; then
   echo -e "${RED}Error: Failed to copy '$QCOM_USB_DIR/qtiDevInf.h' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_USB_DIR/qcom_usb.h $DEST_QCOM_USB_PATH/
if [ ! -f $DEST_QCOM_USB_PATH/qcom_usb.h ]; then
   echo -e "${RED}Error: Failed to copy '$QCOM_USB_DIR/qcom_usb.h' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_USB_DIR/qcom_usb_main.c $DEST_QCOM_USB_PATH/
if [ ! -f $DEST_QCOM_USB_PATH/qcom_usb_main.c ]; then
   echo -e "${RED}Error: Failed to copy '$QCOM_USB_DIR/qcom_usb_main.c' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_USB_DIR/Makefile $DEST_QCOM_USB_PATH/
if [ ! -f $DEST_QCOM_USB_PATH/Makefile ]; then
   echo -e "${RED}Error: Failed to copy '$QCOM_USB_DIR/Makefile' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_USB_DIR/qcom_event.c $DEST_QCOM_USB_PATH/
if [ ! -f $DEST_QCOM_USB_PATH/qcom_event.c ]; then
   echo -e "${RED}Error: Failed to copy '$QCOM_USB_DIR/qcom_event.c' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_USB_DIR/qcom_event.h $DEST_QCOM_USB_PATH/
if [ ! -f $DEST_QCOM_USB_PATH/qcom_event.h ]; then
   echo -e "${RED}Error: Failed to copy '$QCOM_USB_DIR/qcom_event.h' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH
   exit 1
fi

# All modules makefile
$QCOM_LN_RM_MK_DIR/cp ./Makefile $DEST_QUD_PATH/
if [ ! -f $DEST_QUD_PATH/Makefile ]; then
   echo -e "${RED}Error: Failed to copy '$DEST_QUD_PATH/Makefile' to installation path"${RESET}
   #$QCOM_LN_RM_MK_DIR/rm -rf $DEST_QUD_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/ipassignment.sh $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/ipassignment.sh ]; then
   echo -e ${RED}"Error: Failed to copy ipassignment.sh to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/chmod 755 $DEST_QCOM_USBNET_PATH/ipassignment.sh

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/qcom_usbnet_main.c $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/qcom_usbnet_main.c ]; then
   echo -e ${RED}"Error: Failed to copy qcom_usbnet_main.c to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/qmidevice.c $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/qmidevice.c ]; then
   echo -e ${RED}"Error: Failed to copy qmidevice.c to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/qmidevice.h $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/qmidevice.h ]; then
   echo -e ${RED}"Error: Failed to copy qmidevice.h to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/qmi.c $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/qmi.c ]; then
   echo -e ${RED}"Error: Failed to copy qmi.c to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/qmi.h $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/qmi.h ]; then
   echo -e ${RED}"Error: Failed to copy qmi.h to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/qmap.c $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/qmap.c ]; then
   echo -e ${RED}"Error: Failed to copy qmap.c to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/qmap.h $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/qmap.h ]; then
   echo -e ${RED}"Error: Failed to copy qmap.h to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/common.h $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/common.h ]; then
   echo -e ${RED}"Error: Failed to copy common.h to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/Makefile  $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/Makefile ]; then
   echo -e ${RED}"Error: Failed to copy Makefile installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_USBNET_DIR/qtiDevInf.h $DEST_QCOM_USBNET_PATH/
if [ ! -f $DEST_QCOM_USBNET_PATH/qtiDevInf.h ]; then
   echo -e "${RED}Error: Failed to copy '$QCOM_USBNET_DIR/qtiDevInf.h' to installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi

$QCOM_LN_RM_MK_DIR/cp ./$QCOM_USBNET_DIR/qtiwwan.inf $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/qtiwwan.inf ]; then
   echo -e "${RED}Error: Failed to copy qtiwwan.inf installation path"${RESET}
   $QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH
   exit 1
fi
$QCOM_LN_RM_MK_DIR/chmod 644 $DEST_QCOM_USBNET_PATH/qtiwwan.inf

#DEST_SIGN_PATH=/opt/qcom/QUD/sign
#OLD_DEST_SIGN_PATH=/opt/QTI/sign
if [[ $QCOM_SECURE_BOOT_CHECK = "SecureBoot enabled" ]]; then
   if [[ -d $OLD_DEST_SIGN_PATH ]] && [[ -f $OLD_DEST_SIGN_PATH/Signkey_pub.der ]]; then
      QCOM_OLD_PUBLIC_KEY_VERIFY=`mokutil --test-key $OLD_DEST_SIGN_PATH/Signkey_pub.der`
      if [[ $QCOM_OLD_PUBLIC_KEY_VERIFY = "$OLD_DEST_SIGN_PATH/Signkey_pub.der is already enrolled" ]]; then
         $QCOM_LN_RM_MK_DIR/cp -rf $OLD_DEST_SIGN_PATH/* $DEST_SIGN_PATH/
         $QCOM_LN_RM_MK_DIR/chmod -R 777 $DEST_SIGN_PATH
         $QCOM_LN_RM_MK_DIR/chmod 644 $DEST_SIGN_PATH/signReadme.txt
         $QCOM_LN_RM_MK_DIR/chmod 755 $DEST_SIGN_PATH/Signkey_pub.der
         $QCOM_LN_RM_MK_DIR/chmod 755 $DEST_SIGN_PATH/Signkey.priv
      fi
   elif [[ -d $DEST_SIGN_PATH ]]; then
      QCOM_PUBLIC_KEY_VERIFY=`mokutil --test-key $DEST_SIGN_PATH/Signkey_pub.der`
      if [[ $QCOM_PUBLIC_KEY_VERIFY = "$DEST_SIGN_PATH/Signkey_pub.der is already enrolled" ]]; then
         echo -e ${GREEN}"$DEST_SIGN_PATH/Signkey_pub.der is enrolled"${RESET}
      else
         echo -e ${RED}"Signkey_pub.der is not enrolled or doesn't exist"${RESET}
         $QCOM_LN_RM_MK_DIR/cp -rf ./sign/SignConf.config $DEST_SIGN_PATH
         $QCOM_LN_RM_MK_DIR/chmod 777 $DEST_SIGN_PATH/SignConf.config
         awk -i inplace -v name=`hostname` '{gsub(/O = /,"O = "name)}1' $DEST_SIGN_PATH/SignConf.config
         awk -i inplace -v name=`hostname` '{gsub(/CN = /,"CN = "name" Signing Key")}1' $DEST_SIGN_PATH/SignConf.config
         awk -i inplace -v name=`hostname` '{gsub(/emailAddress = /,"emailAddress = "name"@no-reply.com")}1' $DEST_SIGN_PATH/SignConf.config

         if [ ! -f $DEST_SIGN_PATH/SignConf.config ]; then
            echo -e ${RED}"Error: Failed to copy SignConf.config installation path"${RESET}
            $QCOM_LN_RM_MK_DIR/rm -rf $DEST_SIGN_PATH
            exit 1
         fi

         $QCOM_LN_RM_MK_DIR/cp -rf ./sign/signReadme.txt $DEST_SIGN_PATH
         if [ ! -f $DEST_SIGN_PATH/signReadme.txt ]; then
            echo -e ${RED}"Error: Failed to copy signReadme.txt installation path"${RESET}
            $QCOM_LN_RM_MK_DIR/rm -rf $DEST_SIGN_PATH
            exit 1
         fi
         $QCOM_LN_RM_MK_DIR/chmod 644 $DEST_SIGN_PATH/signReadme.txt
      fi
   fi
fi

if [[ $QCOM_SECURE_BOOT_CHECK = "SecureBoot enabled" ]]; then
   echo -e ${GREEN}"SecureBoot enabled"${RESET}
   if [ -f $DEST_SIGN_PATH/Signkey_pub.der ]; then
      QCOM_PUBLIC_KEY_VERIFY=`mokutil --test-key $DEST_SIGN_PATH/Signkey_pub.der`
      if [[ $QCOM_PUBLIC_KEY_VERIFY = "$DEST_SIGN_PATH/Signkey_pub.der is already enrolled" ]]; then
         echo -e ${CYAN}"==========================================================="
         echo -e ${GREEN}"$DEST_SIGN_PATH/Signkey_pub.der is enrolled"
         echo -e ${CYAN}"==========================================================="${RESET}
      else
         echo -e ${RED}"==========================================================="
         echo -e ${CYAN}"Secure Boot is enabled. Public key $DEST_SIGN_PATH/Signkey_pub.der enrolling failed!"
         echo -e ${CYAN}"Try again and follow mandatory instructions properly ($DEST_SIGN_PATH/signReadme.txt)"
         echo -e ${CYAN}"The QUD driver installation Failed!!!"
         echo -e ${RED}"==========================================================="${RESET}
         exit 1
      fi
   else
      echo -e ${RED}"Signkey_pub.der doesn't exist. Creating public and private key"${RESET}
      openssl req -x509 -new -nodes -utf8 -sha256 -days 36500 -batch -config $DEST_SIGN_PATH/SignConf.config -outform DER -out $DEST_SIGN_PATH/Signkey_pub.der -keyout $DEST_SIGN_PATH/Signkey.priv
      $QCOM_LN_RM_MK_DIR/chmod 755 $DEST_SIGN_PATH/Signkey_pub.der
      $QCOM_LN_RM_MK_DIR/chmod 755 $DEST_SIGN_PATH/Signkey.priv
      echo -e ${RED}"##############################################################"
      echo -e ${CYAN}"Secure Boot is enabled. User must enroll the Public Signkey located at $DEST_SIGN_PATH/Signkey_pub.der"
      echo -e ${CYAN}"Please follow the mandatory instructions in $DEST_SIGN_PATH/signReadme.txt"
      echo -e ${CYAN}"The QUD driver installation Failed!!!"
      echo -e ${RED}"##############################################################"${RESET}
      exit 1
   fi
else
   echo -e ${GREEN}"Secure boot disabled"${RESET}
fi

# "********************* Compilation of modules Starts ***************************"
$QCOM_MAKE_DIR/make build
if [ ! -f ./InfParser/$QCOM_MODULE_INF_NAME ]; then
   echo -e "${RED}Error: Failed to generate kernel module $QCOM_MODULE_INF_NAME${RESET}"
   exit 1
fi
if [ ! -f ./$QCOM_USB_DIR/$QCOM_USB_MODULE_NAME ]; then
   echo -e ${RED}"Error: Failed to generate kernel module $QCOM_USB_MODULE_NAME"${RESET}
   exit 1
fi

if [ ! -f ./$QCOM_USBNET_DIR/$QCOM_USBNET_MODULE_NAME ]; then
  echo -e ${RED}"Error: Failed to generate kernel module $QCOM_USBNET_MODULE_NAME"${RESET}
  exit 1
fi

# if [ ! -f ./$QCOM_SERIAL_DIR/$QCOM_MODEM_SERIAL_NAME ]; then
#    echo -e "${RED}Error: Failed to generate kernel module $QCOM_MODEM_SERIAL_NAME"${RESET}
#    $QCOM_LN_RM_MK_DIR/rm -rf $QCOM_MODEM_SERIAL_NAME
#    $QCOM_MAKE_DIR/make clean
#    exit 1
# fi

# Copy .ko object to destination folders
# $QCOM_LN_RM_MK_DIR/cp ./$QCOM_SERIAL_DIR/$QCOM_MODEM_SERIAL_NAME $DEST_MODEM_SERIAL_PATH
# if [ ! -f $DEST_MODEM_SERIAL_PATH/$QCOM_MODEM_SERIAL_NAME ]; then
#    echo -e "${RED}Error: Failed to copy $QCOM_MODEM_SERIAL_NAME to installation path"${RESET}
#    $QCOM_LN_RM_MK_DIR/rm -r $DEST_MODEM_SERIAL_PATH
#    $QCOM_MAKE_DIR/make clean
#    exit 1
# fi

$QCOM_LN_RM_MK_DIR/cp ./InfParser/$QCOM_MODULE_INF_NAME $DEST_QCOM_USBINF_PARSER_PATH
if [ ! -f $DEST_QCOM_USBINF_PARSER_PATH/$QCOM_MODULE_INF_NAME ]; then
   echo -e "${RED}Error: Failed to copy $QCOM_MODULE_INF_NAME to installation path${RESET}"
   exit 1
fi

$QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USB_PATH/$QCOM_USB_MODULE_NAME

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USB_DIR/$QCOM_USB_MODULE_NAME $DEST_QCOM_USB_PATH
if [ ! -f $DEST_QCOM_USB_PATH/$QCOM_USB_MODULE_NAME ]; then
   echo -e ${RED}"Error: Failed to copy $QCOM_USB_MODULE_NAME to installation path"${RESET}
   exit 1
fi

$QCOM_LN_RM_MK_DIR/rm -rf $DEST_QCOM_USBNET_PATH/$QCOM_USBNET_MODULE_NAME

$QCOM_LN_RM_MK_DIR/cp -rf ./$QCOM_USBNET_DIR/$QCOM_USBNET_MODULE_NAME $DEST_QCOM_USBNET_PATH
if [ ! -f $DEST_QCOM_USBNET_PATH/$QCOM_USBNET_MODULE_NAME ]; then
  echo -e ${RED}"Error: Failed to copy $QCOM_USBNET_MODULE_NAME to installation path"${RESET}
  exit 1
fi

# commented for testing
#$QCOM_MAKE_DIR/make clean

# echo SUBSYSTEMS==\"tty\", PROGRAM=\"$DEST_MODEM_SERIAL_PATH/qtidev.pl $DEST_MODEM_SERIAL_PATH/qtiname.inf %k\", SYMLINK+=\"%c\" , MODE=\"0666\" > ./qti_usb_device.rules
echo SUBSYSTEMS==\"qcom_usbnet\", MODE=\"0666\" >> ./qcom-usb-devices.rules
echo SUBSYSTEMS==\"qcom_usb\", MODE=\"0666\" >> ./qcom-usb-devices.rules
echo SUBSYSTEMS==\"qcom_ports\", MODE=\"0666\" >> ./qcom-usb-devices.rules

$QCOM_LN_RM_MK_DIR/chmod 644 ./qcom-usb-devices.rules
$QCOM_LN_RM_MK_DIR/cp -rf ./qcom-usb-devices.rules $QCOM_UDEV_PATH
echo -e "Generated QC rules"

# udev rules for qmi
if [ -f $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules ]; then
   RULE_EXIST="`grep -nr  'usb' $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules`"
   if [ "$RULE_EXIST" != "" ]; then
      echo -e "Subsystem qcom-usbnet rule already exist in $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules, nothing to add"
   else
      echo -e "Subsystem qcom-usbnet rule doesn't exist in $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules, so adding now"
      $QCOM_LN_RM_MK_DIR/chmod 644 $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules
      echo SUBSYSTEMS==\"usb\", ATTRS{idVendor}==\"05c6\", NAME=\"usb%n\" >> $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules
   fi
else
   echo SUBSYSTEMS==\"usb\", ATTRS{idVendor}==\"05c6\", NAME=\"usb%n\" >> ./80-qcom-usbnet-devices.rules
   $QCOM_LN_RM_MK_DIR/chmod 644 ./80-qcom-usbnet-devices.rules
   $QCOM_LN_RM_MK_DIR/cp -rf ./80-qcom-usbnet-devices.rules $QCOM_UDEV_PATH
   echo -e "Creating new udev rule for qcom-usbnet in $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules"
fi

# Informs udev deamon to reload the newly added device rule and re-trigger service
sudo udevadm control --reload-rules
sudo udevadm trigger

if [ ! -f $QCOM_UDEV_PATH/qcom-usb-devices.rules ]; then
   echo -e ${RED}"Error: Failed to generate $QCOM_UDEV_PATH/qcom-usb-devices.rules"
   exit 1
fi

if [ ! -f $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules ]; then
   echo -e ${RED}"Error: Failed to generate $QCOM_UDEV_PATH/80-qcom-usbnet-devices.rules"
   exit 1
fi

rm -f ./qcom-usb-devices.rules
rm -f ./80-qcom-usbnet-devices.rules
echo -e "Removed local rules"

MODLOADED="`/sbin/lsmod | grep usbserial`"
if [ "$MODLOADED" == "" ]; then
   echo -e "To load dependency"
   echo -e "Loading module usbserial"
   if [[ $OSName =~ "Red Hat Enterprise Linux" ]]; then
      if [ -f $MODULE_BLACKLIST_PATH/usbserial.ko.xz ]; then
	xz -d $MODULE_BLACKLIST_PATH/usbserial.ko.xz 
      	$QCOM_MODBIN_DIR/insmod $MODULE_BLACKLIST_PATH/usbserial.ko
      fi
      MODLOADED="`/sbin/lsmod | grep usbserial`"
      if [ "$MODLOADED" == "" ]; then
        echo -e "$OSName: usbserial.ko module not present at $MODULE_BLACKLIST_PATH"
      fi
   else
	   $QCOM_MODBIN_DIR/insmod $MODULE_BLACKLIST_PATH/usbserial.ko
   fi
else
   echo -e "Module usbserial already in place"
fi

#echo Changing Permission of blacklist file
$QCOM_LN_RM_MK_DIR/chmod 777 $MODULE_BLACKLIST_CONFIG/blacklist.conf
echo -e "Changed Permission of blacklist file"
if [ "`grep -nr 'Qualcomm clients' $MODULE_BLACKLIST_CONFIG/blacklist.conf`" != "" ]; then
   sed -i '/# Blacklist these module so that Qualcomm clients use only/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
   sed -i '/# qcom_usbnet, qcom_usb, qtiDevInf driver/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
fi
echo -e "# Blacklist these module so that Qualcomm clients use only" >> $MODULE_BLACKLIST_CONFIG/blacklist.conf
echo -e "# qcom_usbnet, qcom_usb driver" >> $MODULE_BLACKLIST_CONFIG/blacklist.conf

MOD_EXIST="`grep -nr  'blacklist qcserial' $MODULE_BLACKLIST_CONFIG/blacklist.conf`"
if [ "$MOD_EXIST" != "" ]; then
   sed -i '/qcserial/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
fi
echo -e "blacklist qcserial" >> $MODULE_BLACKLIST_CONFIG/blacklist.conf
echo -e "install qcserial /bin/false" >> $MODULE_BLACKLIST_CONFIG/blacklist.conf
echo -e "blacklisted qcserial module"

MODLOADED="`/sbin/lsmod | grep -w qcserial`"
if [ "$MODLOADED" != "" ]; then
   echo -e "qcserial is found. Unloaded qcserial module"
   $QCOM_MODBIN_DIR/rmmod qcserial.ko
   MODLOADED="`/sbin/lsmod | grep -w qcserial`"
   if [ "$MODLOADED" != "" ]; then
      echo -e ${RED}"Failed to unload qcserial.ko. try manually sudo rmmod ModuleName"${RESET}
   fi
fi
if [  -f $MODULE_BLACKLIST_PATH/qcserial.ko ]; then
   echo -e "qcserial is found. renamed to qcserial_dup"
   mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/qcserial.ko /lib/modules/`uname -r`/kernel/drivers/usb/serial/qcserial_dup
fi

MOD_EXIST="`grep -nr  'blacklist qmi_wwan' $MODULE_BLACKLIST_CONFIG/blacklist.conf`"
if [ "$MOD_EXIST" != "" ]; then
   sed -i '/qmi_wwan/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
fi
echo -e "blacklist qmi_wwan" >> $MODULE_BLACKLIST_CONFIG/blacklist.conf
echo -e "install qmi_wwan /bin/false" >> $MODULE_BLACKLIST_CONFIG/blacklist.conf
echo -e "blacklisted qmi_wwan module"

MODLOADED="`/sbin/lsmod | grep -w qmi_wwan`"
if [ "$MODLOADED" != "" ]; then
   echo -e "qmi_wwan is found. Unloaded qmi_wwan module"
   echo -e "cdc-wdm is found. Unloaded cdc-wdm module"
   $QCOM_MODBIN_DIR/rmmod qmi_wwan.ko
   $QCOM_MODBIN_DIR/rmmod cdc-wdm.ko
   MODLOADED="`/sbin/lsmod | grep -w qmi_wwan`"
   if [ "$MODLOADED" != "" ]; then
      echo -e ${RED}"Failed to unload qmi_wwan.ko. Run manually sudo rmmod ModuleName"${RESET}
   fi
   MODLOADED="`/sbin/lsmod | grep cdc_wdm`"
   if [ "$MODLOADED" != "" ]; then
      echo -e ${RED}"Failed to unload cdc_wdm.ko. Run manually sudo rmmod ModuleName"${RESET}
   fi
fi
if [  -f $QCOM_USBNET_AND_QMI_WWAN/qmi_wwan.ko ]; then
   echo -e "qmi_wwan is found. renamed to qmi_wwan_dup"
   echo -e "cdc-wdm is found. renamed to cdc-wdm_dup"
   mv /lib/modules/`uname -r`/kernel/drivers/usb/class/cdc-wdm.ko /lib/modules/`uname -r`/kernel/drivers/usb/class/cdc-wdm_dup
   mv /lib/modules/`uname -r`/kernel/drivers/net/usb/qmi_wwan.ko /lib/modules/`uname -r`/kernel/drivers/net/usb/qmi_wwan_dup
   depmod
fi

MOD_EXIST="`grep -nr  'blacklist option' $MODULE_BLACKLIST_CONFIG/blacklist.conf`"
if [ "$MOD_EXIST" != "" ]; then
   sed -i '/option/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
fi
echo -e "blacklist option" >> $MODULE_BLACKLIST_CONFIG/blacklist.conf
echo -e "install option /bin/false" >> $MODULE_BLACKLIST_CONFIG/blacklist.conf
echo -e "blacklisted option module"

MODLOADED="`/sbin/lsmod | grep option`"
if [ "$MODLOADED" != "" ]; then
   echo -e "option is found. Unloaded option module"
   $QCOM_MODBIN_DIR/rmmod option.ko
   MODLOADED="`/sbin/lsmod | grep option`"
   if [ "$MODLOADED" != "" ]; then
      echo -e ${RED}"Failed to unload option.ko. Run manually sudo rmmod option"${RESET}
   fi
fi
if [  -f $MODULE_BLACKLIST_PATH/option.ko ]; then
   echo -e "option is found. renamed to to option_dup"
   mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/option.ko /lib/modules/`uname -r`/kernel/drivers/usb/serial/option_dup
fi

MOD_EXIST="`grep -nr  'blacklist usb_wwan' $MODULE_BLACKLIST_CONFIG/blacklist.conf`"
if [ "$MOD_EXIST" != "" ]; then
   sed -i '/usb_wwan/d' $MODULE_BLACKLIST_CONFIG/blacklist.conf
fi
echo -e "blacklist usb_wwan" >> $MODULE_BLACKLIST_CONFIG/blacklist.conf
echo -e "install usb_wwan /bin/false" >> $MODULE_BLACKLIST_CONFIG/blacklist.conf
echo -e "blacklisted usb_wwan module"

MODLOADED="`/sbin/lsmod | grep usb_wwan`"
if [ "$MODLOADED" != "" ]; then
   echo -e "usb_wwan is found. Unloaded usb_wwan module"
   $QCOM_MODBIN_DIR/rmmod usb_wwan.ko
   MODLOADED="`/sbin/lsmod | grep usb_wwan`"
   if [ "$MODLOADED" != "" ]; then
      echo -e ${RED}"Failed to unload usb_wwan.ko. Run manually sudo rmmod ModuleName"${RESET}
   fi
fi
if [  -f $MODULE_BLACKLIST_PATH/usb_wwan.ko ]; then
   echo -e "usb_wwan is found. renamed to usb_wwan_dup"
   mv /lib/modules/`uname -r`/kernel/drivers/usb/serial/usb_wwan.ko /lib/modules/`uname -r`/kernel/drivers/usb/serial/usb_wwan_dup
fi

echo -e "Loading $QCOM_USBNET_MODULE_NAME module dependency"
MODLOADED="`/sbin/lsmod | grep mii`"
if [ "$MODLOADED" == "" ]; then
   echo -e "Loading module mii"
   if [[ $OSName =~ "Red Hat Enterprise Linux" ]] || [[ $OSName =~ "Fedora Linux" ]] || [[ $OSName =~ "Ubuntu 24.04" ]]; then
      if [ -f $QCOM_NET_DEPENDENCY_PATH/mii.ko.xz ]; then
        xz -d $QCOM_NET_DEPENDENCY_PATH/mii.ko.xz
      fi
      if [ -f $QCOM_NET_DEPENDENCY_PATH/mii.ko.zst ]; then
        unzstd -d $QCOM_NET_DEPENDENCY_PATH/mii.ko.zst
      fi
      if [ -f $QCOM_NET_DEPENDENCY_PATH/mii.ko ]; then
         $QCOM_MODBIN_DIR/insmod $QCOM_NET_DEPENDENCY_PATH/mii.ko
      fi
      MODLOADED="`/sbin/lsmod | grep mii`"
      if [ "$MODLOADED" == "" ]; then
        echo -e "$OSName: mii.ko module not present at $QCOM_NET_DEPENDENCY_PATH"
      fi
   else
      if [ -f $QCOM_NET_DEPENDENCY_PATH/mii.ko ]; then
         $QCOM_MODBIN_DIR/insmod $QCOM_NET_DEPENDENCY_PATH/mii.ko
      else
         echo -e "$OSName: mii.ko module not present at $QCOM_NET_DEPENDENCY_PATH"
      fi
   fi
else
   echo -e "Module mii already in place"
fi

MODLOADED="`/sbin/lsmod | grep usbnet`"
if [ "$MODLOADED" == "" ]; then
   echo -e "Loading module usbnet"
   if [[ $OSName =~ "Red Hat Enterprise Linux" ]] || [[ $OSName =~ "Fedora Linux" ]] || [[ $OSName =~ "Ubuntu 24.04" ]]; then
      if [ -f $QCOM_USBNET_AND_QMI_WWAN/usbnet.ko.xz ]; then
        xz -d $QCOM_USBNET_AND_QMI_WWAN/usbnet.ko.xz
      fi
      if [ -f $QCOM_USBNET_AND_QMI_WWAN/usbnet.ko.zst ]; then
        unzstd -d $QCOM_USBNET_AND_QMI_WWAN/usbnet.ko.zst
      fi
      if [ -f $QCOM_USBNET_AND_QMI_WWAN/usbnet.ko ]; then
         $QCOM_MODBIN_DIR/insmod $QCOM_USBNET_AND_QMI_WWAN/usbnet.ko
      fi
      MODLOADED="`/sbin/lsmod | grep usbnet`"
      if [ "$MODLOADED" == "" ]; then
        echo -e "$OSName: usbnet.ko module not present at $QCOM_USBNET_AND_QMI_WWAN"
      fi
   else
      if [ -f $QCOM_USBNET_AND_QMI_WWAN/usbnet.ko ]; then
   	   $QCOM_MODBIN_DIR/insmod $QCOM_USBNET_AND_QMI_WWAN/usbnet.ko
      else
         echo -e "$OSName: usbnet.ko module not present at $QCOM_USBNET_AND_QMI_WWAN"
      fi
   fi
else
   echo -e "Module usbnet already in place"
fi

MODLOADED="`/sbin/lsmod | grep -w qcom_serial`"
if [ "$MODLOADED" != "" ]; then
   ( $QCOM_MODBIN_DIR/rmmod $QCOM_MODEM_SERIAL_NAME && echo -e "$QCOM_MODEM_SERIAL_NAME removed successfully.." ) || { echo -e "$QCOM_MODEM_SERIAL_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"${RESET}; exit 1; }
fi

MODLOADED="`/sbin/lsmod | grep -w qcom_usb`"
if [ "$MODLOADED" != "" ]; then
  ($QCOM_MODBIN_DIR/rmmod $QCOM_USB_MODULE_NAME && echo -e "$QCOM_USB_MODULE_NAME removed successfully..") || { echo -e "$QCOM_USB_MODULE_NAME in use"; echo -e ${RED}"Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e ${RED}"ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e ${GREEN}"Try $1ation again!"${RESET}; exit 1; }
fi

MODLOADED="`/sbin/lsmod | grep -w qcom_usbnet`"
if [ "$MODLOADED" != "" ]; then
  ($QCOM_MODBIN_DIR/rmmod $QCOM_USBNET_MODULE_NAME && echo -e "$QCOM_USBNET_MODULE_NAME removed successfully..") || { echo -e "$QCOM_USBNET_MODULE_NAME in use"; echo -e ${RED}"Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e ${RED}"ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e ${GREEN}"Try $1ation again!"${RESET}; exit 1; }
fi

MODLOADED="`/sbin/lsmod | grep -w qtiDevInf`"
if [ "$MODLOADED" != "" ]; then
  ($QCOM_MODBIN_DIR/rmmod $QCOM_MODULE_INF_NAME && echo -e "$QCOM_MODULE_INF_NAME removed successfully..") || { echo -e "$QCOM_MODULE_INF_NAME in use"; echo -e "${RED}Note: ${CYAN} Close all applications that make use of the driver, including QUTS clients."; echo -e "${RED}ps -aux | grep QUTS, sudo kill -9 <PID> OR sudo pkill QUTS"; echo -e "${GREEN}Try $1ation again!"${RESET}; exit 1; }
fi

echo -e "Loading new module $QCOM_MODULE_INF_NAME"
$QCOM_MODBIN_DIR/insmod $DEST_QCOM_USBINF_PARSER_PATH/$QCOM_MODULE_INF_NAME debug_g=0
MODLOADED="`/sbin/lsmod | grep -w qtiDevInf`"
if [ "$MODLOADED" == "" ]; then
   echo -e "${RED}Failed to load new $QCOM_MODULE_INF_NAME module${RESET}"
   exit 1
fi

echo -e "Loading new module $QCOM_USB_MODULE_NAME"
$QCOM_MODBIN_DIR/insmod $DEST_QCOM_USB_PATH/$QCOM_USB_MODULE_NAME debug_g=1
MODLOADED="`/sbin/lsmod | grep -w qcom_usb`"
if [ "$MODLOADED" == "" ]; then
   echo -e ${RED}"Failed to load new $QCOM_USB_MODULE_NAME module"${RESET}
   exit 1
fi

echo -e "Loading new module $QCOM_USBNET_MODULE_NAME"

$QCOM_MODBIN_DIR/insmod $DEST_QCOM_USBNET_PATH/$QCOM_USBNET_MODULE_NAME debug_g=1 debug_aggr=0
MODLOADED="`/sbin/lsmod | grep -w qcom_usbnet`"
if [ "$MODLOADED" == "" ]; then
   echo -e ${RED}"Failed to load new $QCOM_USBNET_MODULE_NAME module"${RESET}
   exit 1
fi

$QCOM_LN_RM_MK_DIR/rm -rf $QCOM_USB_KERNEL_PATH/$QCOM_MODULE_INF_NAME
$QCOM_LN_RM_MK_DIR/cp -rf $DEST_QCOM_USBINF_PARSER_PATH/$QCOM_MODULE_INF_NAME $QCOM_USB_KERNEL_PATH
if [ ! -f $QCOM_USB_KERNEL_PATH/$QCOM_MODULE_INF_NAME ]; then
   echo -e ${RED}"Error: Failed to copy $QCOM_MODULE_INF_NAME to $QCOM_USB_KERNEL_PATH path."${RESET}
   exit 1
fi

$QCOM_LN_RM_MK_DIR/rm -rf $QCOM_USB_KERNEL_PATH/$QCOM_USB_MODULE_NAME
$QCOM_LN_RM_MK_DIR/cp -rf $DEST_QCOM_USB_PATH/$QCOM_USB_MODULE_NAME $QCOM_USB_KERNEL_PATH
if [ ! -f $QCOM_USB_KERNEL_PATH/$QCOM_USB_MODULE_NAME ]; then
   echo -e ${RED}"Error: Failed to copy $QCOM_USB_MODULE_NAME to $QCOM_USB_KERNEL_PATH path."${RESET}
   exit 1
fi

$QCOM_LN_RM_MK_DIR/rm -rf $QCOM_USBNET_AND_QMI_WWAN/$QCOM_USBNET_MODULE_NAME
$QCOM_LN_RM_MK_DIR/cp -rf $DEST_QCOM_USBNET_PATH/$QCOM_USBNET_MODULE_NAME $QCOM_USBNET_AND_QMI_WWAN
if [ ! -f $QCOM_USBNET_AND_QMI_WWAN/$QCOM_USBNET_MODULE_NAME ]; then
  echo -e ${RED}"Error: Failed to copy $QCOM_USBNET_MODULE_NAME to $QCOM_USBNET_AND_QMI_WWAN path."${RESET}
  exit 1
fi
# update modules.dep and modules.alias
depmod

# Enable debug messages for qcom_usb driver
# Note: For qcom_usbnet, command will enable debug messages only for 1 device
if [[ "$QCOM_DRIVER_DEBUG_ENABLE" = "qcom_usb" ]]; then
   echo "module qcom_usb +p" > /sys/kernel/debug/dynamic_debug/control
   echo -e "Enable debug messages for qcom_usb driver"
elif [[ "$QCOM_DRIVER_DEBUG_ENABLE" = "qcom_usbnet" ]]; then
   echo 0F02 > /sys/QCOM_WWAN_Adapter_*:*usb0_0/Debug
   echo -e "Enable debug messages for qcom_usbnet driver"
elif [[ "$QCOM_DRIVER_DEBUG_ENABLE" = "all" ]]; then
   echo "module qcom_usb +p" > /sys/kernel/debug/dynamic_debug/control
   echo 0F02 > /sys/QCOM_WWAN_Adapter_*:*usb0_0/Debug
   echo -e "Enable debug messages for qcom_usb, qcom_usbnet driver"
else
   echo -e "No debug message enabled"
fi

$QCOM_MAKE_DIR/make clean

$QCOM_MAKE_DIR/find $DEST_QUD_PATH -type d -exec chmod 0755 {} \;

echo -e "Qualcomm INF Parser driver is installed at $DEST_QCOM_USBINF_PARSER_PATH"
echo -e "Qualcomm Modem driver is installed at $DEST_MODEM_SERIAL_PATH"
echo -e "Qualcomm usbnet driver is installed at $DEST_QCOM_USBNET_PATH"
echo -e "Qualcomm usb driver is installed at $DEST_QCOM_USB_PATH"
echo -e "Qualcomm udev naming/permission rules are installed at $QCOM_UDEV_PATH"

if [ -f "$DEST_QUD_PATH/ReleaseNotes*.txt" ]; then
   echo -e "QUD Release Notes available at $DEST_QUD_PATH"
fi

MODUPDATE="`grep -nr  qtiDevInf /etc/modules`"
if [ "$MODUPDATE" == "" ]; then
	echo -e "qtiDevInf" >> /etc/modules
fi

MODUPDATE="`grep -nr  qcom_usb /etc/modules`"
if [ "$MODUPDATE" == "" ]; then
	echo -e "qcom_usb" >> /etc/modules
fi

MODUPDATE="`grep -nr  qcom_usbnet /etc/modules`"
if [ "$MODUPDATE" == "" ]; then
	echo -e "qcom_usbnet" >> /etc/modules
fi

# MODUPDATE="`grep -nr  qcom_serial /etc/modules`"
# if [ "$MODUPDATE" == "" ]; then
# 	echo -e "qcom_serial" >> /etc/modules
# fi

if [[ $OSName != *"Red Hat Enterprise Linux"* ]]; then
   MODUPDATE="`grep -nr  'iface usb0 inet static' /etc/network/interfaces`"
   if [ "$MODUPDATE" == "" ]; then
	echo -e "iface usb0 inet static" >> /etc/network/interfaces
  fi
fi

exit 0
