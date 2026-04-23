/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          R E G . H

GENERAL DESCRIPTION
    This file defines registry key constants and function prototypes for
    device enumeration helpers in the Qualcomm MTU configuration service.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef REG_H
#define REG_H

#include <windows.h>
#include <stdio.h>
#include <tchar.h>

#define QCNET_REG_HW_KEY "SYSTEM\\CurrentControlSet\\Enum\\USB"
#define QCNET_REG_SW_KEY "SYSTEM\\CurrentControlSet\\Control\\Class"

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383

BOOL QueryKey
(
    HKEY  hKey,
    PCHAR DeviceFriendlyName,
    PCHAR ControlFileName,
    PCHAR FullKeyName
);

BOOL FindDeviceInstance
(
    PTCHAR InstanceKey,
    PCHAR  DeviceFriendlyName,
    PCHAR  ControlFileName
);

BOOL QCWWAN_GetEntryValue
(
    HKEY  hKey,
    PCHAR DeviceFriendlyName,
    PCHAR EntryName,
    PCHAR ControlFileName
);

BOOL QCWWAN_GetControlFileName
(
    PCHAR DeviceFriendlyName,
    PCHAR ControlFileName
);

BOOL QueryUSBDeviceKeys
(
    PTCHAR InstanceKey,
    PCHAR  DeviceFriendlyName,
    PCHAR  ControlFileName
);

#endif  // REG_H
