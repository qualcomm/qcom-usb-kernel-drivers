/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                       D E V M O N . H

GENERAL DESCRIPTION
    This file defines constants and function prototypes for device
    monitoring and NIC management in the Qualcomm MTU configuration service.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef DEVMON_H
#define DEVMON_H

#include "qmicl.h"

#define DEVMON_EVENT_REG_CHANGE 0
#define DEVMON_EVENT_EXIT       1
#define DEVMON_EVENT_COUNT      2

#define DEVMON_SCAN_INTERVAL 1000

VOID DEVMN_DeviceMonitor(BOOL AppMode);

DWORD WINAPI DEVMN_DevMonThread(PVOID);

VOID GetNicList(VOID);

BOOL QueryNicSwKeys(PTCHAR InstanceKey);

BOOL QueryInstance
(
    HKEY  hKey,
    PCHAR FullKeyName
);

BOOL GetDeviceInstance(PTCHAR InstanceKey);

BOOL GetInstanceNames
(
    HKEY  hKey,
    PCHAR EntryName
);

int SpawnMtuService(VOID);

BOOL IsInActiveList(PCHAR NicFriendlyName);

PDEVMN_NIC_RECORD AddNicToActiveList(int NicIdx);

BOOL DEVMN_RemoveActiveNic(PVOID Context);

VOID TerminateAllMtuThreads(VOID);

VOID DEVMN_ResetNicItem(PDEVMN_NIC_RECORD Item);

VOID GetNetInterfaceName(VOID);

#endif  // DEVMON_H
