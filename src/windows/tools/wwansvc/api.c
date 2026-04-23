/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          A P I . C

GENERAL DESCRIPTION
    This file implements the IOCTL-based communication API for the
    Qualcomm MTU configuration service (qcmtusvc).

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "api.h"
#include "reg.h"
#include "utils.h"

static CRITICAL_SECTION TidLock;

VOID QCWWAN_InitializeResources(VOID)
{
    InitializeCriticalSection(&TidLock);
}  // QCWWAN_Initialize

VOID QCWWAN_ReleaseResources(VOID)
{
    DeleteCriticalSection(&TidLock);
}  // QCWWAN_ReleaseResources

HANDLE QCWWAN_OpenService(PCHAR DeviceFriendlyName, UCHAR ServiceType)
{
    HANDLE hServiceDevice, hDevice;
    char controlFileName[SERVICE_FILE_BUF_LEN];
    char serviceFileName[SERVICE_FILE_BUF_LEN];
    char tmpName[SERVICE_FILE_BUF_LEN];
    BOOL bResult;

    // Init service file name buffer
    ZeroMemory(serviceFileName, SERVICE_FILE_BUF_LEN);
    ZeroMemory(controlFileName, SERVICE_FILE_BUF_LEN);
    ZeroMemory(tmpName, SERVICE_FILE_BUF_LEN);

    bResult = QCWWAN_GetControlFileName
    (
        DeviceFriendlyName,
        tmpName
    );

    if (bResult == FALSE)
    {
        printf("Error: couldn't retrieve control file, error %u\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    sprintf(controlFileName, "\\\\.\\%s", tmpName);

    hDevice = CreateFile
    (
        controlFileName,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        printf("ERROR opening control file <%s>: INVALID_HANDLE_VALUE (error code %u)\n",
               controlFileName, GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    // Get QMUX service file
    ZeroMemory(tmpName, SERVICE_FILE_BUF_LEN);
    bResult = GetServiceFile(hDevice, tmpName, ServiceType);
    CloseHandle(hDevice);  // close control file handle

    if (bResult == FALSE)
    {
        printf("ERROR: cannot get service file.\n");
        return INVALID_HANDLE_VALUE;
    }

    sprintf(serviceFileName, "\\\\.\\%s", tmpName);

    // 2. Conduct read and write
    hServiceDevice = CreateFile
    (
        serviceFileName,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
    );
    if (hServiceDevice == INVALID_HANDLE_VALUE)
    {
        printf("ERROR opening service <%s>: INVALID_HANDLE_VALUE (error code 0x%x)\n",
               serviceFileName, GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    return hServiceDevice;
}  // QCWWAN_OpenService

BOOL QCWWAN_CloseService
(
    HANDLE ServiceHandle
)
{
    return CloseHandle(ServiceHandle);
}  // QCWWAN_CloseService

BOOL QCWWAN_Cleanup
(
    HANDLE ServiceHandle
)
{
    return CancelIo(ServiceHandle);
}  // QCWWAN_Cleanup

HANDLE QCWWAN_RegisterDeviceNotification
(
    HANDLE                ServiceHandle,
    NOTIFICATION_CALLBACK CallBack,
    PVOID                 Context
)
{
    HANDLE hNotificationThreadHandle;
    PNOTIFICATION_CONTEXT context;
    DWORD  tid;

    if (CallBack == NULL)
    {
        printf("Error: Unable to register notification (no callback)\n");
        return INVALID_HANDLE_VALUE;
    }

    context = (PNOTIFICATION_CONTEXT)malloc(sizeof(NOTIFICATION_CONTEXT));
    if (context == NULL)
    {
        printf("Error: NOTIFICATION_CONTEXT memory allocation failure.\n");
        return INVALID_HANDLE_VALUE;
    }

    context->ServiceHandle = ServiceHandle;
    context->CallBack = CallBack;
    context->Context = Context;

    hNotificationThreadHandle = CreateThread
    (
        NULL,
        0,
        RegisterNotification,
        (LPVOID)context,
        0,
        &tid
    );

    if (hNotificationThreadHandle == INVALID_HANDLE_VALUE)
    {
        printf("Error: Unable to register notification, error %d\n", GetLastError());
        free(context);
    }

    return hNotificationThreadHandle;
}  // QCWWAN_RegisterDeviceNotification

BOOL QCWWAN_ReadRaw
(
    HANDLE ServiceHandle,
    PCHAR  ReceiveBuffer,
    ULONG  ReceiveBufferSize,
    PULONG BytesRead
)
{
    OVERLAPPED ov;
    BOOL       bResult = FALSE;
    DWORD      dwStatus = NO_ERROR;

    ZeroMemory(&ov, sizeof(ov));
    ov.Offset = 0;
    ov.OffsetHigh = 0;
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL)
    {
        printf("Read failure, event error %u\n", GetLastError());
        return bResult;
    }

    *BytesRead = 0;

    bResult = ReadFile
    (
        ServiceHandle,
        ReceiveBuffer,
        ReceiveBufferSize,
        BytesRead,
        &ov
    );

    if (bResult == TRUE)
    {
        return bResult;
    }
    else
    {
        dwStatus = GetLastError();

        if (ERROR_IO_PENDING != dwStatus)
        {
            printf("Read failure, error %u\n", dwStatus);
        }
        else
        {
            bResult = GetOverlappedResult
            (
                ServiceHandle,
                &ov,
                BytesRead,
                TRUE  // no return until operaqtion completes
            );

            if (bResult == FALSE)
            {
                dwStatus = GetLastError();
                printf("Read/GetOverlappedResult failure, error %u\n", dwStatus);
            }
        }
    }

    if (ov.hEvent != NULL)
    {
        CloseHandle(ov.hEvent);
    }

    return bResult;

}  // QCWWAN_ReadRaw

BOOL QCWWAN_SendRaw
(
    HANDLE ServiceHandle,
    PCHAR  SendBuffer,
    ULONG  SendLength,
    PULONG BytesWritten
)
{
    BOOL       bResult = FALSE;
    OVERLAPPED ov;
    DWORD      dwStatus = NO_ERROR;

    ZeroMemory(&ov, sizeof(ov));
    ov.Offset = 0;
    ov.OffsetHigh = 0;
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL)
    {
        printf("Read error, event error %u\n", GetLastError());
        return bResult;
    }

    *BytesWritten = 0;

    // send message
    if ((SendBuffer != NULL) && (SendLength != 0))
    {
#ifdef MY_UNIT_TEST
        return 0;
#endif // MY_UNIT_TEST

        bResult = WriteFile
        (
            ServiceHandle,
            SendBuffer,
            SendLength,
            BytesWritten,
            &ov
        );

        if (bResult == FALSE)
        {
            dwStatus = GetLastError();

            if (ERROR_IO_PENDING != dwStatus)
            {
                printf("write error, error %u\n", dwStatus);
            }
            else
            {
                bResult = GetOverlappedResult
                (
                    ServiceHandle,
                    &ov,
                    BytesWritten,
                    TRUE  // no return until operaqtion completes
                );

                if (bResult == FALSE)
                {
                    dwStatus = GetLastError();
                    printf("Write error, error %u\n", dwStatus);
                }
            }
        }
    }
    else
    {
        printf("Write error: cannot get message to send.\n");
    }

    if (ov.hEvent != NULL)
    {
        CloseHandle(ov.hEvent);
    }

    return bResult;
}  // QCWWAN_SendRaw

USHORT QCWWAN_GenerateTransactionId(VOID)
{
    static USHORT tid = 0;
    USHORT retVal;

    EnterCriticalSection(&TidLock);

    if (++tid == 0)
    {
        tid = 1;
    }
    retVal = tid;

    LeaveCriticalSection(&TidLock);

    return retVal;
}  // QCWWAN_GenerateTransactionId

BOOL WINAPI QCWWAN_GetClientId
(
    HANDLE ServiceHandle,
    PUCHAR ClientId
)
{
    DWORD bytesReturned = 0;
    UCHAR cid;

    if (
        DeviceIoControl
        (
            ServiceHandle,
            IOCTL_QCDEV_QMI_GET_CLIENT_ID,
            NULL,
            0,
            (LPVOID)&cid,
            sizeof(UCHAR),
            &bytesReturned,
            NULL
        )
    )
    {
        *ClientId = cid;
        return TRUE;
    }

    return FALSE;
}  // QCWWAN_GetClientId
