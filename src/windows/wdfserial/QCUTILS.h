/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef QCUTILS_H
#define QCUTILS_H

#include "QCMAIN.h"

VOID QCUTIL_RingBufferInit
(
    PRING_BUFFER ringBuffer,
    size_t       capacity
);

VOID QCUTIL_RingBufferClear
(
    PRING_BUFFER ringBuffer
);

VOID QCUTIL_RingBufferDelete
(
    PRING_BUFFER ringBuffer
);

NTSTATUS QCUTIL_RingBufferWrite
(
    PRING_BUFFER ringBuffer,
    PUCHAR       source,
    size_t       writeLength
);

NTSTATUS QCUTIL_RingBufferRead
(
    PRING_BUFFER ringBuffer,
    PUCHAR       destination,
    size_t       readLength,
    size_t      *bytesRead
);

BOOLEAN QCUTIL_RingBufferReadByte
(
    PRING_BUFFER ringBuffer,
    PUCHAR       out,
    size_t       offset
);

size_t QCUTIL_RingBufferBytesUsed
(
    PRING_BUFFER ringBuffer
);

size_t QCUTIL_RingBufferBytesFree
(
    PRING_BUFFER ringBuffer
);

BOOLEAN QCUTIL_IsIoQueueEmpty
(
    WDFQUEUE ioQueue
);

VOID QCUTIL_InsertTailList
(
    PLIST_ENTRY     head,
    PLIST_ENTRY     entry,
    WDFSPINLOCK     lock,
    size_t         *pListLength
);

VOID QCUTIL_RemoveEntryList
(
    PLIST_ENTRY     entry,
    WDFSPINLOCK     lock,
    size_t         *pListLength
);

BOOLEAN QCUTIL_FindEntryInList
(
    PLIST_ENTRY head,
    PLIST_ENTRY entry
);

NTSTATUS QCUTIL_RequestCopyToBuffer
(
    WDFREQUEST      Request,
    PVOID           Destination,
    SIZE_T          NumBytes,
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCUTIL_RequestCopyFromBuffer
(
    WDFREQUEST      Request,
    PVOID           Source,
    SIZE_T          NumBytes,
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCUTIL_RequestCopyFromBufferWithOffset
(
    WDFREQUEST      Request,
    PVOID           Source,
    SIZE_T          NumBytes,
    SIZE_T          RequestMomoryOffset,
    PDEVICE_CONTEXT pDevContext
);

VOID QCUTIL_PrintNewLine
(
    ULONG DbgMask,
    ULONG DbgLevel,
    PDEVICE_CONTEXT pDevContext
);

VOID QCUTIL_PrintBytes
(
    PVOID Buf,
    ULONG len,
    ULONG PktLen,
    char *info,
    ULONG DbgMask,
    ULONG DbgLevel,
    PDEVICE_CONTEXT pDevContext
);

#endif
