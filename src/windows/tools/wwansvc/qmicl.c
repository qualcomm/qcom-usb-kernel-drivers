/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                        Q M I C L . C

GENERAL DESCRIPTION
    This file implements the QMI client logic for MTU notification
    handling in the Qualcomm MTU configuration service.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include <windows.h>
#include "main.h"
#include "qmicl.h"
#include "api.h"
#include "utils.h"
#include "reg.h"
#include "devmon.h"

// define invalid MTU value which is greater thann QCMTU_MAX_MTU
#define QCDEV_INVALID_MTU 0x80000000

VOID QMICL_SetDefaultMTU(PDEVMN_NIC_RECORD NicInfo)
{
    QC_MTU mtuInfo;

    QCMTU_Print("-->QMICL_SetDefaultMTU for <%s>\n", NicInfo->FriendlyName);

    mtuInfo.MTU = 1500;

    mtuInfo.IpVer = 4;
    QCMTU_SetMTU(&mtuInfo, NicInfo->InterfaceName);

    mtuInfo.IpVer = 6;
    QCMTU_SetMTU(&mtuInfo, NicInfo->InterfaceName);

    QCMTU_Print("<--QMICL_SetDefaultMTU for <%s>\n", NicInfo->FriendlyName);
}  // QMICL_SetDefaultMTU

BOOL QMICL_StartMTUService(PVOID NicInfo)
{
    PDEVMN_NIC_RECORD activeNic = (PDEVMN_NIC_RECORD)NicInfo;
    HANDLE hServiceDevice = INVALID_HANDLE_VALUE;
    char controlFileName[SERVICE_FILE_BUF_LEN];

    if (AppDebugMode == TRUE)
    {
        QCMTU_Print("-->QMICL_StartMTUService: Client[%d] 0x%p <%s>\n", activeNic->Index, activeNic, activeNic->FriendlyName);
    }

    if (CreateEventsForMtuThread(activeNic) == FALSE)
    {
        QCMTU_Print("Error: events creation failure for <%d>\n", activeNic->FriendlyName);
        return FALSE;
    }

    sprintf(controlFileName, "\\\\.\\%s", activeNic->ControlFileName);

    hServiceDevice = CreateFile
    (
        controlFileName,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        (FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED),
        NULL
    );

    if (hServiceDevice == INVALID_HANDLE_VALUE)
    {
        CloseEventsForMtuThread(activeNic);
        if (AppDebugMode == TRUE)
        {
            QCMTU_Print("<--QMICL_StartMTUService: NIC access failure <%s>\n", activeNic->FriendlyName);
        }
        return FALSE;
    }
    QCMTU_Print("QMICL_StartMTUService: control handle 0x%x\n", hServiceDevice);

    activeNic->MtuServiceHandle = hServiceDevice;

    if (StartMtuThread(activeNic) == INVALID_HANDLE_VALUE)
    {
        CloseEventsForMtuThread(activeNic);
        QCMTU_Print("<--QMICL_StartMTUService: cannot create NIC thread\n");
        return FALSE;
    }

    if (AppDebugMode == TRUE)
    {
        QCMTU_Print("<--QMICL_StartMTUService: Client[%d] 0x%p <%s>\n", activeNic->Index, activeNic, activeNic->FriendlyName);
    }

    return TRUE;
}  // QMICL_StartMTUService

VOID MyNotificationCallback
(
    HANDLE ServiceHandle,
    ULONG Mask,
    PDEVMN_NIC_RECORD Context
)
{
    QCMTU_Print("MyNotificationCallback: Client 0x%p mask 0x%x\n", Context, Mask);
    // if (Mask & 0x00000001)
    {
        SetEvent(Context->Event[QMICL_EVENT_TERMINATE]);
    }
}  // MyNotificationCallback

HANDLE WINAPI StartMtuThread(PVOID NicInfo)
{
    PDEVMN_NIC_RECORD context = (PDEVMN_NIC_RECORD)NicInfo;
    HANDLE hNotificationThreadHandle = 0;
    HANDLE hV4MtuNotificationThreadHandle = 0;
    HANDLE hV6MtuNotificationThreadHandle = 0;
    HANDLE hMainThreadHandle = 0;
    DWORD  tid1;

    // Before register MTU notification, set default MTU
    QMICL_SetDefaultMTU(NicInfo);

    // Register MTU notifications
    QCMTU_Print("Start V4 MTU notification thread with handle 0x%x\n", context->MtuServiceHandle);
    hV4MtuNotificationThreadHandle = QMICL_RegisterMtuNotification
    (
        context->MtuServiceHandle,
        4,
        (NOTIFICATION_CALLBACK)MyIPV4MtuNotificationCallback,
        context
    );
    if (hV4MtuNotificationThreadHandle == INVALID_HANDLE_VALUE)
    {
        QCMTU_Print("Unable to register IPV4 MTU notification, error %d\n", GetLastError());
    }
    else
    {
        QCMTU_Print("V4 MTU notification thread handle 0x%x\n", hV4MtuNotificationThreadHandle);
    }

    QCMTU_Print("Start V6 MTU notification thread with handle 0x%x\n", context->MtuServiceHandle);
    hV6MtuNotificationThreadHandle = QMICL_RegisterMtuNotification
    (
        context->MtuServiceHandle,
        6,
        (NOTIFICATION_CALLBACK)MyIPV6MtuNotificationCallback,
        context
    );
    if (hV6MtuNotificationThreadHandle == INVALID_HANDLE_VALUE)
    {
        QCMTU_Print("Unable to register IPV6 MTU notification, error %d\n", GetLastError());
    }
    else
    {
        QCMTU_Print("V6 MTU notification thread handle 0x%x\n", hV6MtuNotificationThreadHandle);
    }

    // --- Last, we register the removal notification
    QCMTU_Print("Start removal notification thread with handle 0x%x\n", context->MtuServiceHandle);

    hNotificationThreadHandle = QCWWAN_RegisterDeviceNotification
    (
        context->MtuServiceHandle,
        (NOTIFICATION_CALLBACK)MyNotificationCallback,
        context
    );
    if (hNotificationThreadHandle == INVALID_HANDLE_VALUE)
    {
        QCMTU_Print("Unable to register notification, error %d\n", GetLastError());
        // return INVALID_HANDLE_VALUE;
    }
    else
    {
        QCMTU_Print("Removal notification thread handle 0x%x\n", hNotificationThreadHandle);
    }

    // --- start MTU main thread
    QCMTU_Print("Start main thread for <%s>\n", context->FriendlyName);
    hMainThreadHandle = CreateThread
    (
        NULL,
        0,
        QMICL_MainThread,
        (LPVOID)context,
        0,
        &tid1
    );
    if (hMainThreadHandle == INVALID_HANDLE_VALUE)
    {
        QCMTU_Print("Unable to launch MTU thread, error %d\n", GetLastError());
        CloseHandle(hNotificationThreadHandle);
        if (hV4MtuNotificationThreadHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hV4MtuNotificationThreadHandle);
        }
        if (hV6MtuNotificationThreadHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hV6MtuNotificationThreadHandle);
        }
    }
    else
    {
        context->ManagedThread[QMICL_RM_NOTIFY_THREAD_IDX] = hNotificationThreadHandle;
        context->ManagedThread[QMICL_V4_MTU_NOTIFY_THREAD_IDX] = hV4MtuNotificationThreadHandle;
        context->ManagedThread[QMICL_V6_MTU_NOTIFY_THREAD_IDX] = hV6MtuNotificationThreadHandle;
        context->MtuThreadHandle = hMainThreadHandle;
    }

    return hMainThreadHandle;

}  // StartMtuThread

// MTU Main Thread
DWORD WINAPI QMICL_MainThread(LPVOID Context)
{
    PDEVMN_NIC_RECORD context;
    int     cnt = 0;
    DWORD   dwStatus = NO_ERROR;
    BOOL    stopped = FALSE, terminated = FALSE;
    DWORD   waitIdx, waitTime = 1000;
    HANDLE  hMtuNotificationThreadHandle = 0;

    context = (PDEVMN_NIC_RECORD)Context;

    while (terminated == FALSE)
    {
        // wait for events
        waitIdx = WaitForMultipleObjects
        (
            QMICL_EVENT_COUNT,
            context->Event,
            FALSE,
            waitTime
        );

        if (waitIdx != WAIT_TIMEOUT)
        {
            waitIdx -= WAIT_OBJECT_0;
        }

        // process events
        switch (waitIdx)
        {
            case QMICL_EVENT_START:
            {
                QCMTU_Print("<%u> QMICL_EVENT_START\n", context->Index);
                ResetEvent(context->Event[QMICL_EVENT_START]);
                stopped = FALSE;

                // TODO: register notifications

                break;
            }
            case QMICL_EVENT_STOP:
            {
                QCMTU_Print("<%u> QMICL_EVENT_STOP\n", context->Index);
                ResetEvent(context->Event[QMICL_EVENT_STOP]);
                stopped = TRUE;

#if(_WIN32_WINNT >= 0x0600)
                CancelIoEx(context->MtuServiceHandle, NULL);
#else
                CancelIo(context->MtuServiceHandle);
#endif

                break;
            }
            case QMICL_EVENT_TERMINATE:
            {
                DWORD timeoutVal;

                QCMTU_Print("QMICL_EVENT_TERMINATE\n");

                ResetEvent(context->Event[QMICL_EVENT_TERMINATE]);

                stopped = TRUE;
                terminated = TRUE;

                /***
                            #if(_WIN32_WINNT >= 0x0600)
                            CancelIoEx(context->MtuServiceHandle, NULL);
                            #else
                            CancelIo(context->MtuServiceHandle);
                            #endif
                ***/

                // close control file
                CloseHandle(context->MtuServiceHandle);

                // Wait for all managed threads to terminate
                QCMTU_Print("QMICL_EVENT_TERMINATE: Wait for all managed threads\n");
                timeoutVal = 5000; // 5 seconds
                WaitForMultipleObjects
                (
                    QMICL_MANAGING_THREAD_COUNT,
                    context->ManagedThread,
                    TRUE,
                    INFINITE // timeoutVal
                );
                QCMTU_Print("QMICL_EVENT_TERMINATE: all managed threads exit\n");

                // close thread handle
                if (context->ManagedThread[QMICL_RM_NOTIFY_THREAD_IDX] != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(context->ManagedThread[QMICL_RM_NOTIFY_THREAD_IDX]);
                }
                if (context->ManagedThread[QMICL_V4_MTU_NOTIFY_THREAD_IDX] != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(context->ManagedThread[QMICL_V4_MTU_NOTIFY_THREAD_IDX]);
                }
                if (context->ManagedThread[QMICL_V6_MTU_NOTIFY_THREAD_IDX] != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(context->ManagedThread[QMICL_V6_MTU_NOTIFY_THREAD_IDX]);
                }

                DEVMN_RemoveActiveNic(context);

                break;
            }
            case QMICL_EVENT_IPV4_MTU:
            {
                QCMTU_Print("QMICL_EVENT_IPV4_MTU for <%s>\n", context->InterfaceName);
                ResetEvent(context->Event[QMICL_EVENT_IPV4_MTU]);

                if ((terminated == TRUE) || (stopped == TRUE))
                {
                    break;
                }
                else
                {
                    // register for MTU notification again
                    HANDLE tHandle;

                    tHandle = QMICL_RegisterMtuNotification
                    (
                        context->MtuServiceHandle,
                        4,
                        (NOTIFICATION_CALLBACK)MyIPV4MtuNotificationCallback,
                        context
                    );
                    if (tHandle == INVALID_HANDLE_VALUE)
                    {
                        QCMTU_Print("QMICL_EVENT_IPV4_MTU: Unable to register IPV4 MTU notification, error %d\n", GetLastError());
                    }
                    context->ManagedThread[QMICL_V4_MTU_NOTIFY_THREAD_IDX] = tHandle;
                }

                break;
            }

            case QMICL_EVENT_IPV6_MTU:
            {
                QCMTU_Print("QMICL_EVENT_IPV6_MTU for <%s>\n", context->InterfaceName);
                ResetEvent(context->Event[QMICL_EVENT_IPV6_MTU]);

                if ((terminated == TRUE) || (stopped == TRUE))
                {
                    break;
                }
                else
                {
                    // register for MTU notification again
                    HANDLE tHandle;

                    tHandle = QMICL_RegisterMtuNotification
                    (
                        context->MtuServiceHandle,
                        6,
                        (NOTIFICATION_CALLBACK)MyIPV6MtuNotificationCallback,
                        context
                    );
                    if (tHandle == INVALID_HANDLE_VALUE)
                    {
                        QCMTU_Print("QMICL_EVENT_IPV6_MTU: Unable to register IPV6 MTU notification, error %d\n", GetLastError());
                    }
                    context->ManagedThread[QMICL_V6_MTU_NOTIFY_THREAD_IDX] = tHandle;
                }

                break;
            }

            case WAIT_TIMEOUT:
            {
                // QCMTU_Print("<%u> WAIT_TIMEOUT\n", context->Index);
                break;
            }
            default:
            {
                QCMTU_Print("<%u> UNKNOWN EVENT - %u\n", context->Index, waitIdx);
                break;
            }
        } // switch
    } // while (terminated == FALSE)

    QCWWAN_CloseService(context->MtuServiceHandle);
    CloseEventsForMtuThread(context);

    QCMTU_Print("QMICL thread exits <%s>\n", context->FriendlyName);

    return 0;
}  // QMICL_MainThread

BOOL CreateEventsForMtuThread(PDEVMN_NIC_RECORD Context)
{
    int i;
    BOOL result = TRUE;

    if (AppDebugMode == TRUE)
    {
        QCMTU_Print("--><%u> CreateEventsForMtuThread\n", Context->Index);
    }

    QCMTU_RequestLock();

    for (i = QMICL_EVENT_START; i < QMICL_EVENT_COUNT; ++i)
    {
        Context->Event[i] = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (Context->Event[i] == NULL)
        {
            result = FALSE;
            break;
        }
        ResetEvent(Context->Event[i]);
    }

    if (result == TRUE)
    {
        Context->InUse = TRUE;
        QCMTU_ReleaseLock();

        if (AppDebugMode == TRUE)
        {
            QCMTU_Print("<--<%u> CreateEventsForMtuThread: TRUE (%d)\n", Context->Index, i);
        }

        return result;
    }

    // failed, clean up
    CloseEventsForMtuThread(Context);

    QCMTU_ReleaseLock();

    if (AppDebugMode == TRUE)
    {
        QCMTU_Print("<--<%u> CreateEventsForMtuThread: failure\n", Context->Index);
    }
    return result;
}  // CreateEventsForMtuThread

VOID CloseEventsForMtuThread(PDEVMN_NIC_RECORD Context)
{
    int i;

    QCMTU_RequestLock();

    for (i = QMICL_EVENT_START; i < QMICL_EVENT_COUNT; ++i)
    {
        if (Context->Event[i] != NULL)
        {
            CloseHandle(Context->Event[i]);
            Context->Event[i] = NULL;
        }
    }

    Context->InUse = FALSE;

    QCMTU_ReleaseLock();

}  // CloseEventsForMtuThread

VOID MyIPV4MtuNotificationCallback
(
    HANDLE ServiceHandle,
    ULONG MtuSize,
    PDEVMN_NIC_RECORD Context
)
{
    QC_MTU mtuInfo;

    mtuInfo.IpVer = 4;
    mtuInfo.MTU = (USHORT)MtuSize;

    QCMTU_Print("MyIPV4MtuNotificationCallback: IPV4 MTU 0x%x(%d) for 0x%p <%s>\n", MtuSize, mtuInfo.MTU, Context, Context->InterfaceName);

    // set MTU, QCDEV_INVALID_MTU > QCMTU_MAX_MTU
    if ((MtuSize != 0) && (MtuSize < QCMTU_MAX_MTU))
    {
        QCMTU_SetMTU(&mtuInfo, Context->InterfaceName);
    }

    if (MtuSize < QCMTU_MAX_MTU)
    {
        SetEvent(Context->Event[QMICL_EVENT_IPV4_MTU]);
    }

}  // MyIPV4NotificationCallback

VOID MyIPV6MtuNotificationCallback
(
    HANDLE ServiceHandle,
    ULONG MtuSize,
    PDEVMN_NIC_RECORD Context
)
{
    QC_MTU mtuInfo;

    mtuInfo.IpVer = 6;
    mtuInfo.MTU = (USHORT)MtuSize;

    QCMTU_Print("MyIPV6MtuNotificationCallback: IPV6 MTU 0x%x(%d) for 0x%p <%s>\n", MtuSize, mtuInfo.MTU, Context, Context->InterfaceName);

    // set MTU, QCDEV_INVALID_MTU > QCMTU_MAX_MTU
    if ((MtuSize != 0) && (MtuSize < QCMTU_MAX_MTU))
    {
        QCMTU_SetMTU(&mtuInfo, Context->InterfaceName);
    }

    if (MtuSize < QCMTU_MAX_MTU)
    {
        SetEvent(Context->Event[QMICL_EVENT_IPV6_MTU]);
    }

}  // MyIPV6NotificationCallback

HANDLE QMICL_RegisterMtuNotification
(
    HANDLE                ServiceHandle,
    UCHAR                 IpVersion,
    NOTIFICATION_CALLBACK CallBack,
    PVOID                 Context
)
{
    HANDLE hNotificationThreadHandle;
    PMTU_NOTIFICATION_CONTEXT context;
    DWORD  tid;

    if (CallBack == NULL)
    {
        QCMTU_Print("Error: Unable to register MTU notification (no callback)\n");
        return INVALID_HANDLE_VALUE;
    }

    context = (PMTU_NOTIFICATION_CONTEXT)malloc(sizeof(MTU_NOTIFICATION_CONTEXT));
    if (context == NULL)
    {
        QCMTU_Print("Error: NOTIFICATION_CONTEXT memory allocation failure.\n");
        return INVALID_HANDLE_VALUE;
    }

    context->ServiceHandle = ServiceHandle;
    context->IpVersion = IpVersion;
    context->CallBack = CallBack;
    context->Context = Context;

    hNotificationThreadHandle = CreateThread
    (
        NULL,
        0,
        RegisterMtuNotification,
        (LPVOID)context,
        0,
        &tid
    );

    if (hNotificationThreadHandle == INVALID_HANDLE_VALUE)
    {
        QCMTU_Print("Error: Unable to register notification, error %d\n", GetLastError());
        free(context);
    }

    return hNotificationThreadHandle;
}  // QMICL_RegisterMtuNotification

DWORD WINAPI RegisterMtuNotification(PVOID Context)
{
    ULONG      event, rtnBytes = 0;
    OVERLAPPED ov;
    BOOL       bResult;
    DWORD      dwStatus = NO_ERROR, dwReturnBytes = 0;
    PMTU_NOTIFICATION_CONTEXT ctxt = (PMTU_NOTIFICATION_CONTEXT)Context;
    UCHAR ipver;

    QCMTU_Print("-->RegisterMtuNotification for IPV%d, handle 0x%x\n", ctxt->IpVersion, ctxt->ServiceHandle);

    ipver = ctxt->IpVersion;

    ZeroMemory(&ov, sizeof(ov));
    ov.Offset = 0;
    ov.OffsetHigh = 0;
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL)
    {
        QCMTU_Print("RegisterMtuNotification failure, evt error %u\n", GetLastError());
        ctxt->CallBack(ctxt->ServiceHandle, QCDEV_INVALID_MTU, ctxt->Context);
        free(ctxt);
        return 0;
    }

    QCMTU_Print("RegisterMtuNotification: calling DeviceIoControl for IPV%d (handle 0x%x)\n", ctxt->IpVersion, ctxt->ServiceHandle);

    if (ctxt->IpVersion == 4)
    {
        bResult = DeviceIoControl
        (
            ctxt->ServiceHandle,
            IOCTL_QCDEV_IPV4_MTU_NOTIFY,
            NULL,
            0,
            &event,
            sizeof(event),
            &rtnBytes,
            &ov
        );
    }
    else if (ctxt->IpVersion == 6)
    {
        bResult = DeviceIoControl
        (
            ctxt->ServiceHandle,
            IOCTL_QCDEV_IPV6_MTU_NOTIFY,
            NULL,
            0,
            &event,
            sizeof(event),
            &rtnBytes,
            &ov
        );
    }
    else
    {
        QCMTU_Print("RegisterMtuNotification: failure, wrong IP version %d\n", ctxt->IpVersion);
        ctxt->CallBack(ctxt->ServiceHandle, QCDEV_INVALID_MTU, ctxt->Context);
        free(ctxt);
        return 0;
    }

    QCMTU_Print("RegisterMtuNotification: post-DeviceIoControl for IPV%d\n", ctxt->IpVersion);

    if (bResult == TRUE)
    {
        ctxt->CallBack(ctxt->ServiceHandle, event, ctxt->Context);
    }
    else
    {
        dwStatus = GetLastError();

        if (ERROR_IO_PENDING != dwStatus)
        {
            QCMTU_Print("RegisterMtuNotification failure (IPV%d), error %u\n", ctxt->IpVersion, dwStatus);
            ctxt->CallBack(ctxt->ServiceHandle, QCDEV_INVALID_MTU, ctxt->Context);
        }
        else
        {
            bResult = GetOverlappedResult
            (
                ctxt->ServiceHandle,
                &ov,
                &dwReturnBytes,
                TRUE  // no return until operaqtion completes
            );

            if (bResult == FALSE)
            {
                dwStatus = GetLastError();
                QCMTU_Print("RegisterMtuNotification/GetOverlappedResult failure (IPV%d), error %u\n", ctxt->IpVersion, dwStatus);
                ctxt->CallBack(ctxt->ServiceHandle, QCDEV_INVALID_MTU, ctxt->Context);
            }
            else
            {
                ctxt->CallBack(ctxt->ServiceHandle, event, ctxt->Context);
            }
        }
    }

    if (ov.hEvent != NULL)
    {
        CloseHandle(ov.hEvent);
    }

    free(ctxt);

    QCMTU_Print("<--RegisterMtuNotification: IPV%d\n", ipver);

    return 0;

}  // RegisterMtuNotification
