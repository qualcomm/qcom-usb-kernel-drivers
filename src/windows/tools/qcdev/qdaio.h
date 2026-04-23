/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D A I O . H

GENERAL DESCRIPTION
    This file defines data structures and function prototypes for the
    asynchronous I/O module used in Qualcomm USB device communication.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef QDAIO_H
#define QDAIO_H

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <conio.h>
#include <string.h>
#include <winioctl.h>
#include "qdpublic.h"
#include "utils.h"

#define QDA_MAX_DEV 16
#define QDAIO_IO_POOL_SIZE 100
#define BAIL_OUT_THRESHOLD 5

#define QIO_RX 0
#define QIO_TX 1

typedef enum _QDAIO_DEV_STATE
{
    QDAIO_DEV_STATE_UNINIT = 0,
    QDAIO_DEV_STATE_INIT = 1,
    QDAIO_DEV_STATE_OPEN = 2,
    QDAIO_DEV_STATE_CLOSE = 3
} QDAIO_DEV_STATE;

typedef enum _AIO_ITEM_STATE
{
    IO_ITEM_IDLE = 0,
    IO_ITEM_PENDING,
    IO_ITEM_COMPLETED
} AIO_ITEM_STATE;

// AIO status codes
typedef enum _QDAIO_STATUS
{
    QDAIO_STATUS_SUCCESS = 0,
    QDAIO_STATUS_FAILURE = -1,
    QDAIO_STATUS_INVALID_DEVICE = -2,
    QDAIO_STATUS_DEVICE_NOT_INIT = -3,
    QDAIO_STATUS_NO_DEVICE = -4,
    QDAIO_STATUS_HANDLE_FAILURE = -5,
    QDAIO_STATUS_BAD_HANDLE = -6,
    QDAIO_STATUS_NO_MEMORY = -7,
    QDAIO_STATUS_PENDING = -8,
    QDAIO_STATUS_CANCELLED = -9
} QDAIO_STATUS;

typedef VOID(_stdcall *AIO_CALLBACK)(LONG AppHdl, PVOID Context, ULONG Status, ULONG IoSize);

typedef struct _AIO_CONTEXT
{
    PVOID        UserData;
    PVOID        UserData2;
    AIO_CALLBACK Callback;
    int          Index;
    LIST_ENTRY   List;
} AIO_CONTEXT, *PAIO_CONTEXT;

typedef struct _AIO_ITEM
{
    LIST_ENTRY List;
    PVOID      Buf;
    DWORD      IoSize;
    OVERLAPPED OverlappedContext;
    AIO_CALLBACK IoCallback;
    PVOID      UserContext;
    AIO_ITEM_STATE State;
    int        Index;
    DWORD      Status;   // system-specific error code (such as Windows error code)
} AIO_ITEM, *PAIO_ITEM;

typedef struct _QDAIODEV
{
    PVOID            DevExtension;  // point to QDAIODEV_EXTENTION
    LONG             AppHandle;     // index
    HANDLE           DevHandle;
    LONG             IoItemPoolSize;
    QDAIO_DEV_STATE  DevState;
} QDAIODEV, *PQDAIODEV;

typedef struct _QDAIO_THREAD_CONTEXT
{
    PQDAIODEV Dev;
    int       IoType; // RX -- 0; TX -- 1
} QDAIO_THREAD_CONTEXT, *PQDAIO_THREAD_CONTEXT;

typedef struct _QDAIODEV_EXTENTION
{
    // RX/TX item pool
    PAIO_ITEM        IoPool[2];
    LIST_ENTRY       IoItemIdleQueue[2];
    CRITICAL_SECTION IoPoolLock[2];

    // RX/TX queues
    LIST_ENTRY DispatchQueue[2];
    LIST_ENTRY PendingQueue[2];
    LIST_ENTRY CompletionQueue[2];

    // RX/TX thread handle
    HANDLE     IoThreadHandle[2];
    HANDLE     CompletionThreadHandle[2];

    // Rx/TX thread events
    HANDLE     IoThreadStartedEvt[2];
    HANDLE     IoThreadTerminatedEvt[2];
    HANDLE     CompletionThreadStartedEvt[2];
    HANDLE     CompletionThreadTerminatedEvt[2];
    HANDLE     CancelCompletionThreadEvt[2];

    // RX/TX action events
    HANDLE     DispatchEvt[2];
    HANDLE     CompletionEvt[2];
    HANDLE     CancelEvt[2];

    // RX/TX thread context
    QDAIO_THREAD_CONTEXT ThreadContext[2];

    // Locks
    CRITICAL_SECTION IoLock[2];
    CRITICAL_SECTION CompletionLock[2];
    LONG OutstandingIo[2];
} QDAIODEV_EXTENSION, *PQDAIODEV_EXTENSION;

#define QCOMSER_IOCTL_INDEX 2048
/* Make the following code as 3339 - session total bytes */
#define IOCTL_QCUSB_SET_SESSION_TOTAL CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCOMSER_IOCTL_INDEX+31, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

namespace QDAIO
{
    extern QDAIODEV AioDevice[QDA_MAX_DEV];
    QCDEVLIB_API BOOL Initialize(VOID);
    QCDEVLIB_API VOID Cleanup(VOID);

    // following APIs return handle or QDAIO_STATUS on failure
    QCDEVLIB_API LONG OpenDevice(PVOID DeviceName);
    QCDEVLIB_API LONG OpenDevice(PVOID DeviceName, DWORD Baudrate = DEV_DEFAULT_BAUD_RATE, BOOL isLegacyTimeoutConfig = false);
    QCDEVLIB_API LONG CloseDevice(LONG AppHandle);
    QCDEVLIB_API LONG Send(LONG AppHandle, PCHAR Buffer, ULONG Length, PAIO_CONTEXT Context);
    QCDEVLIB_API LONG Read(LONG AppHandle, PCHAR Buffer, ULONG Length, PAIO_CONTEXT Context);
    QCDEVLIB_API LONG Cancel(LONG AppHandle, PAIO_CONTEXT Context);
    QCDEVLIB_API BOOL SetSessionTotal(LONG AppHandle, LONGLONG SessionTotal); // Golden-Gate specific
}  // QDAIO

#endif
