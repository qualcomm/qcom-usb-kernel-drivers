/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          R E G . H

GENERAL DESCRIPTION
    This file defines constants and function prototypes for registry
    access used by the qdcfg driver configuration utility.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef REG_H
#define REG_H

#define QCNET_REG_HW_KEY "SYSTEM\\CurrentControlSet\\Enum\\USB"
#define QCNET_REG_SW_KEY "SYSTEM\\CurrentControlSet\\Control\\Class"

#define MAX_KEY_LENGTH   255
#define MAX_VALUE_NAME   6144

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

__declspec(dllexport) ULONG InspectLogging
(
    PCHAR deviceFriendlyName,
    PULONG errorCode
);

__declspec(dllexport) ULONG InspectEntry
(
    PCHAR deviceFriendlyName,
    PCHAR entryName,
    PDWORD entryValue
);

ULONG QueryUSBDeviceKeys2(PCHAR rootKey, PCHAR deviceFriendlyName, PCHAR swKeyBuffer, SIZE_T swKeyBufferSize);

#endif  // REG_H
