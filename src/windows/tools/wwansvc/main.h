/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M A I N . H

GENERAL DESCRIPTION
    This file defines data structures, constants, and function prototypes
    for the Qualcomm MTU configuration service (qcmtusvc).

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef MAIN_H
#define MAIN_H

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <dbt.h>
#include <conio.h>
#include <string.h>

#include <winioctl.h>
#include "api.h"

// #include <initguid.h>
// DEFINE_GUID(GUID_QCMTU_NIC, 0x2c7089aa, 0x2e0e, 0x11d1, 0xb1, 0x14, 0x00, 0xc0, 0x4f, 0xc2, 0xaa, 0xe4);
// DEFINE_GUID(GUID_QCMTU_NIC, 0x86e0d1e0L, 0x8089, 0x11d0, 0x9c, 0xe4, 0x08, 0x00, 0x3e, 0x30, 0x1f, 0x73);

extern GUID GUID_QCMTU_NIC;

extern HANDLE MtuInstance;
extern GUID MyDeviceGuid;
extern HINSTANCE AppModuleHandle;
extern HWND AppWinHandle;
extern WNDCLASSEX AppWndClass;
extern TCHAR *AppClassName;
extern TCHAR *AppTitle;
extern DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
extern SERVICE_STATUS_HANDLE ServiceStateHandle;
extern HDEVNOTIFY  DevNotificationHandle;
extern BOOL AppDebugMode;

#define DEVMN_NUM_NIC 1600
#define QCMTU_MIN_MTU 576
#define QCMTU_MAX_MTU (64*1024)

#define QMICL_RM_NOTIFY_THREAD_IDX     0
#define QMICL_V4_MTU_NOTIFY_THREAD_IDX 1
#define QMICL_V6_MTU_NOTIFY_THREAD_IDX 2
#define QMICL_MANAGING_THREAD_COUNT    3

#define QMICL_EVENT_START           0
#define QMICL_EVENT_STOP            1
#define QMICL_EVENT_TERMINATE       2
#define QMICL_EVENT_IPV4_MTU        3
#define QMICL_EVENT_IPV6_MTU        4
#define QMICL_EVENT_COUNT           5

typedef struct _QC_MTU
{
    UCHAR IpVer;
    ULONG MTU;
} QC_MTU, *PQC_MTU;

typedef struct _DEVMN_NIC_RECORD
{
    struct _DEVMN_NIC_RECORD *Next;

    // NIC info
    CHAR FriendlyName[256];
    CHAR ControlFileName[256];
    CHAR InterfaceId[60];                              // NIC GUID
    CHAR InterfaceName[256];

    // thread context
    HANDLE MtuServiceHandle;                           // control file handle
    HANDLE MtuThreadHandle;                            // main thread  handle
    HANDLE ManagedThread[QMICL_MANAGING_THREAD_COUNT]; // notification thread handles
    HANDLE Event[QMICL_EVENT_COUNT];
    OVERLAPPED Overlapped;                             // structure for read

    int Index;
    BOOL InUse;
} DEVMN_NIC_RECORD, *PDEVMN_NIC_RECORD;

// Function Prototypes

HANDLE QCMTU_InitializeLock(VOID);
VOID QCMTU_DeleteLock(VOID);
VOID QCMTU_RequestLock(VOID);
VOID QCMTU_ReleaseLock(VOID);

VOID QCMTU_InstallService(void);
VOID QCMTU_UninstallService(void);
VOID WINAPI QCMTU_ServiceMain(DWORD Argc, LPTSTR *Argv);
DWORD WINAPI QCMTU_ServiceControlHandlerEx
(
    DWORD  ControlCode,
    DWORD  EventType,
    LPVOID EventData,
    LPVOID Context
);
VOID WINAPI QCMTU_ServiceControlHandler
(
    DWORD  ControlCode
);

LRESULT MyWindowProc
(
    HWND   Wnd,
    UINT   Message,
    WPARAM WParam,
    LPARAM LParam
);

BOOL QCMTU_SetMTU(PQC_MTU Mtu, PCHAR InterfaceName);

BOOLEAN QCMTU_RunSingleInstance(VOID);

VOID QCMTU_CloseRunningInstance(VOID);

#endif // MAIN_H
