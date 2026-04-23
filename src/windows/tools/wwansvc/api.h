/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          A P I . H

GENERAL DESCRIPTION
    This file defines IOCTL codes, QMI message structures, and function
    prototypes for the Qualcomm MTU configuration service API.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef API_H
#define API_H

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <conio.h>
#include <string.h>
#include <winioctl.h>

typedef enum _QMI_SERVICE_TYPE
{
    QMUX_TYPE_CTL = 0x00,
    QMUX_TYPE_WDS = 0x01,
    QMUX_TYPE_DMS = 0x02,
    QMUX_TYPE_NAS = 0x03,
    QMUX_TYPE_QOS = 0x04,
    QMUX_TYPE_MAX,
    QMUX_TYPE_ALL = 0xFF
} QMI_SERVICE_TYPE;

#define SERVICE_FILE_BUF_LEN 256
#define QMUX_NUM_THREADS  3
#define QMUX_MAX_DATA_LEN 2048
#define QMUX_MAX_CMD_LEN  2048

// User-defined IOCTL code range: 2048-4095
#define QCDEV_IOCTL_INDEX                   2048
#define QCDEV_DUPLICATED_NOTIFICATION_REQ 0x00000002L

#define IOCTL_QCDEV_WAIT_NOTIFY CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3338 - USB debug mask */
#define IOCTL_QCDEV_SET_DBG_UMSK CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1290, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3339 - MP debug mask */
#define IOCTL_QCDEV_SET_DBG_MMSK CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1291, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3340 - MP debug mask */
#define IOCTL_QCDEV_GET_SERVICE_FILE CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1292, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3343 - QMI Client Id  */
#define IOCTL_QCDEV_QMI_GET_CLIENT_ID CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+1295, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

/* Make the following code as XXX - MTU size */
#define IOCTL_QCDEV_IPV4_MTU_NOTIFY CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                             QCDEV_IOCTL_INDEX+50, \
                                             METHOD_BUFFERED, \
                                             FILE_ANY_ACCESS)

/* Make the following code as XXX - MTU size */
#define IOCTL_QCDEV_IPV6_MTU_NOTIFY CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                             QCDEV_IOCTL_INDEX+51, \
                                             METHOD_BUFFERED, \
                                             FILE_ANY_ACCESS)

typedef VOID(_stdcall *NOTIFICATION_CALLBACK)(HANDLE, ULONG, PVOID);

typedef struct _NOTIFICATION_CONTEXT
{
    HANDLE                ServiceHandle;
    NOTIFICATION_CALLBACK CallBack;
    PVOID                 Context;
} NOTIFICATION_CONTEXT, *PNOTIFICATION_CONTEXT;

// Function Prototypes

VOID QCWWAN_InitializeResources(VOID);

VOID QCWWAN_ReleaseResources(VOID);

HANDLE QCWWAN_OpenService
(
    IN PCHAR DeviceFriendlyName,
    IN UCHAR ServiceType
);

BOOL QCWWAN_CloseService
(
    IN HANDLE ServiceHandle
);

BOOL QCWWAN_Cleanup
(
    IN HANDLE ServiceHandle
);

HANDLE QCWWAN_RegisterDeviceNotification
(
    HANDLE                ServiceHandle,
    NOTIFICATION_CALLBACK CallBack,
    PVOID                 Context
);

BOOL QCWWAN_ReadRaw
(
    IN  HANDLE ServiceHandle,
    OUT PCHAR  ReceiveBuffer,
    IN  ULONG  ReceiveBufferSize,
    OUT PULONG BytesRead
);

BOOL QCWWAN_SendRaw
(
    IN  HANDLE ServiceHandle,
    IN  PCHAR  SendBuffer,
    IN  ULONG  SendLength,
    OUT PULONG BytesWritten
);

BOOL QCWWAN_TextMessageToBinary
(
    IN  PCHAR  Text,
    OUT PCHAR  BinaryBuffer,
    IN  ULONG  BinaryBufferSize,
    OUT PULONG ReturnedBinaryLength
);

BOOL QCWWAN_BinaryMessageToText
(
    IN  HANDLE ServiceHandle,
    IN  PCHAR  BinaryMessage,
    IN  ULONG  BinaryLength,
    OUT PCHAR  TextBuffer,
    IN  ULONG  TextBufferSize
);

USHORT QCWWAN_GenerateTransactionId(VOID);

BOOL WINAPI QCWWAN_GetClientId
(
    IN  HANDLE ServiceHandle,
    OUT PUCHAR ClientId
);

#endif // API_H
