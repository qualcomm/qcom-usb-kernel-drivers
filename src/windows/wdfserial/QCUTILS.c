/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "QCUTILS.h"

#ifdef EVENT_TRACING
#include "QCWPP.h"
#include "QCUTILS.tmh"      //  this is the file that will be auto generated
#endif

VOID QCUTIL_RingBufferInit
(
    PRING_BUFFER ringBuffer,
    size_t       capacity
)
{
    if (ringBuffer != NULL && capacity > 0)
    {
        capacity += 1; // to distinguish empty and full states
        ringBuffer->pBuffer = ExAllocatePoolUninitialized(NonPagedPoolNx, capacity, 'gniR');
        if (ringBuffer->pBuffer == NULL)
        {
            ringBuffer->Capacity = 0;
        }
        else
        {
            ringBuffer->Capacity = capacity;
        }
        ringBuffer->WriteHead = 0;
        ringBuffer->ReadHead = 0;
    }
}

VOID QCUTIL_RingBufferClear
(
    PRING_BUFFER ringBuffer
)
{
    if (ringBuffer != NULL)
    {
        ringBuffer->WriteHead = 0;
        ringBuffer->ReadHead = 0;
    }
}

VOID QCUTIL_RingBufferDelete
(
    PRING_BUFFER ringBuffer
)
{
    if (ringBuffer != NULL && ringBuffer->pBuffer != NULL)
    {
        ExFreePoolWithTag(ringBuffer->pBuffer, 'gniR');
        ringBuffer->pBuffer = NULL;
        ringBuffer->Capacity = 0;
    }
}

NTSTATUS QCUTIL_RingBufferWrite
(
    PRING_BUFFER ringBuffer,
    PUCHAR       source,
    size_t       writeLength
)
{
    if (ringBuffer == NULL || source == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (QCUTIL_RingBufferBytesFree(ringBuffer) < writeLength)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    for (size_t i = 0; i < writeLength; i++)
    {
        ringBuffer->pBuffer[ringBuffer->WriteHead] = source[i];
        ringBuffer->WriteHead = (ringBuffer->WriteHead + 1) % ringBuffer->Capacity;
    }

    return STATUS_SUCCESS;
}

NTSTATUS QCUTIL_RingBufferRead
(
    PRING_BUFFER ringBuffer,
    PUCHAR       destination,
    size_t       readLength,
    size_t      *bytesRead
)
{
    if (ringBuffer == NULL || destination == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    size_t bytesUsed = QCUTIL_RingBufferBytesUsed(ringBuffer);
    size_t bytesToRead = min(bytesUsed, readLength);

    for (size_t i = 0; i < bytesToRead; i++)
    {
        destination[i] = ringBuffer->pBuffer[ringBuffer->ReadHead];
        ringBuffer->ReadHead = (ringBuffer->ReadHead + 1) % ringBuffer->Capacity;
    }

    if (bytesRead != NULL)
    {
        *bytesRead = bytesToRead;
    }
    return STATUS_SUCCESS;
}

BOOLEAN QCUTIL_RingBufferReadByte
(
    PRING_BUFFER ringBuffer,
    PUCHAR       out,
    size_t       offset
)
{
    if (offset >= QCUTIL_RingBufferBytesUsed(ringBuffer))
    {
        return FALSE;
    }

    size_t index = (ringBuffer->ReadHead + offset) % ringBuffer->Capacity;
    *out = ringBuffer->pBuffer[index];
    return TRUE;
}

size_t QCUTIL_RingBufferBytesUsed
(
    PRING_BUFFER ringBuffer
)
{
    if (ringBuffer->WriteHead >= ringBuffer->ReadHead)
    {
        return ringBuffer->WriteHead - ringBuffer->ReadHead;
    }
    else
    {
        return ringBuffer->Capacity + ringBuffer->WriteHead - ringBuffer->ReadHead;
    }
}

size_t QCUTIL_RingBufferBytesFree
(
    PRING_BUFFER ringBuffer
)
{
    return ringBuffer->Capacity - 1 - QCUTIL_RingBufferBytesUsed(ringBuffer);
}

BOOLEAN QCUTIL_IsIoQueueEmpty
(
    WDFQUEUE ioQueue
)
{
    WDF_IO_QUEUE_STATE queueStatus;
    queueStatus = WdfIoQueueGetState
    (
        ioQueue,
        NULL,
        NULL
    );
    return (WDF_IO_QUEUE_IDLE(queueStatus)) ? TRUE : FALSE;
}

VOID QCUTIL_InsertTailList
(
    PLIST_ENTRY     head,
    PLIST_ENTRY     entry,
    WDFSPINLOCK     lock,
    size_t         *pListLength
)
{
    if (lock != NULL)
    {
        WdfSpinLockAcquire(lock);
    }
    InsertTailList(head, entry);
    if (pListLength != NULL)
    {
        (*pListLength)++;
    }
    if (lock != NULL)
    {
        WdfSpinLockRelease(lock);
    }
}

VOID QCUTIL_RemoveEntryList
(
    PLIST_ENTRY     entry,
    WDFSPINLOCK     lock,
    size_t         *pListLength
)
{
    if (lock != NULL)
    {
        WdfSpinLockAcquire(lock);
    }
    RemoveEntryList(entry);
    if (pListLength != NULL)
    {
        (*pListLength)--;
    }
    if (lock != NULL)
    {
        WdfSpinLockRelease(lock);
    }
}

BOOLEAN QCUTIL_FindEntryInList
(
    PLIST_ENTRY head,
    PLIST_ENTRY entry
)
{
    PLIST_ENTRY peek = head->Flink;
    while (peek != head)
    {
        if (peek == entry)
        {
            return TRUE;
        }
        peek = peek->Flink;
    }
    return FALSE;
}

NTSTATUS QCUTIL_RequestCopyToBuffer
(
    WDFREQUEST      Request,
    PVOID           Destination,
    SIZE_T          NumBytes,
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS    status;
    WDFMEMORY   memory;

    status = WdfRequestRetrieveInputMemory(Request, &memory);
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> Retrive input buffer FAILED request: 0x%p, status: 0x%x\n", pDevContext->PortName, Request, status)
        );
        return status;
    }

    status = WdfMemoryCopyToBuffer
    (
        memory,
        0,
        Destination,
        NumBytes
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> Copy from input buffer FAILED request: 0x%p, status: 0x%x\n", pDevContext->PortName, Request, status)
        );
        return status;
    }

    WdfRequestSetInformation(Request, NumBytes);
    return status;
}

NTSTATUS QCUTIL_RequestCopyFromBuffer
(
    WDFREQUEST      Request,
    PVOID           Source,
    SIZE_T          NumBytes,
    PDEVICE_CONTEXT pDevContext
)
{
    return QCUTIL_RequestCopyFromBufferWithOffset(Request, Source, NumBytes, 0, pDevContext);
}

NTSTATUS QCUTIL_RequestCopyFromBufferWithOffset
(
    WDFREQUEST      Request,
    PVOID           Source,
    SIZE_T          NumBytes,
    SIZE_T          RequestMomoryOffset,
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS    status;
    WDFMEMORY   memory;
    size_t      bufferSize = 0;

    status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> Retrive output buffer FAILED request: 0x%p, status: 0x%x\n", pDevContext->PortName, Request, status)
        );
        return status;
    }

    WdfMemoryGetBuffer(memory, &bufferSize);
    status = WdfMemoryCopyFromBuffer
    (
        memory,
        RequestMomoryOffset,
        Source,
        NumBytes
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> Copy to output buffer FAILED request: 0x%p, status: 0x%x, min buffer size: %llu, actual buffer size: %llu, offset: %llu\n",
            pDevContext->PortName, Request, status, NumBytes, bufferSize, RequestMomoryOffset)
        );
    }
    return status;
}

VOID QCUTIL_PrintNewLine
(
    ULONG DbgMask,
    ULONG DbgLevel,
    PDEVICE_CONTEXT pDevContext
)
{
    QCSER_DbgPrint
    (
        DbgMask,
        DbgLevel,
        ("\n")
    );
}

VOID QCUTIL_PrintBytes
(
    PVOID Buf,
    ULONG len,
    ULONG PktLen,
    char *info,
    ULONG DbgMask,
    ULONG DbgLevel,
    PDEVICE_CONTEXT pDevContext
)
{
    char *buf, *p, *cBuf, *cp;
    char *buffer;
    ULONG lastCnt = 0, spaceNeeded;
    ULONG i, s;
    PCHAR dbgOutputBuffer;
    ULONG myTextSize = 1280;

#define SPLIT_CHAR '|'

    // QCSER_DBG_LEVEL_FORCE does not work here
    if (pDevContext != NULL)
    {
#ifdef EVENT_TRACING
        if (!((pDevContext->DebugMask != 0) && (QCWPP_USER_FLAGS(WPP_DRV_MASK_CONTROL) & DbgMask) && (QCWPP_USER_LEVEL(WPP_DRV_MASK_CONTROL) >= DbgLevel)))
        {
            return;
        }
#else
        if (((pDevContext->DebugMask & DbgMask) == 0) || (pDevContext->DebugLevel < DbgLevel))
        {
            return;
        }
#endif
    }

    // re-calculate text buffer size
    if (myTextSize < (len * 5 + 360))
    {
        myTextSize = len * 5 + 360;
    }

    buffer = (char *)Buf;

    dbgOutputBuffer = ExAllocatePoolZero(NonPagedPoolNx, myTextSize, '0gbd');
    if (dbgOutputBuffer == NULL)
    {
        return;
    }

    RtlZeroMemory(dbgOutputBuffer, myTextSize);
    cBuf = dbgOutputBuffer;
    buf = dbgOutputBuffer + 128;
    p = buf;
    cp = cBuf;

    if (PktLen < len)
    {
        len = PktLen;
    }

    RtlStringCchPrintfA(p, myTextSize - 128, "\r\n\t   --- <%s> DATA %u/%u BYTES ---\r\n", info, len, PktLen);
    p += strlen(p);

    int steps = strlen(p); //for counting number of bytes to be removed from the buffer size
    for (i = 1; i <= len; i++)
    {
        if (i % 16 == 1)
        {
            RtlStringCchPrintfA(p, myTextSize - steps - 128, "  %04u:  ", i - 1);
            p += 9;
            steps += 9;
        }

        RtlStringCchPrintfA(p, myTextSize - steps - 128, "%02X ", (UCHAR)buffer[i - 1]);
        if (isprint(buffer[i - 1]) && (!isspace(buffer[i - 1])))
        {
            RtlStringCchPrintfA(cp, myTextSize, "%c", buffer[i - 1]);
        }
        else
        {
            RtlStringCchPrintfA(cp, myTextSize, ".");
        }

        p += 3;
        steps += 3;
        cp += 1;

        if ((i % 16) == 8)
        {
            RtlStringCchPrintfA(p, myTextSize - steps - 128, "  ");
            p += 2;
            steps += 2;
        }

        if (i % 16 == 0)
        {
            if (i % 64 == 0)
            {
                RtlStringCchPrintfA(p, myTextSize - steps - 128, " %c  %s\r\n\r\n", SPLIT_CHAR, cBuf);
            }
            else
            {
                RtlStringCchPrintfA(p, myTextSize - steps - 128, " %c  %s\r\n", SPLIT_CHAR, cBuf);
            }
            QCSER_DbgPrintX
            (
                pDevContext,
                DbgMask,
                DbgLevel,
                ("%s", buf)
            );
            RtlZeroMemory(dbgOutputBuffer, myTextSize);
            p = buf;
            cp = cBuf;
            steps = strlen(p);
        }
    }

    lastCnt = i % 16;

    if (lastCnt == 0)
    {
        lastCnt = 16;
    }

    if (lastCnt != 1)
    {
        // 10 + 3*8 + 2 + 3*8 = 60 (full line bytes)
        spaceNeeded = (16 - lastCnt + 1) * 3;
        if (lastCnt <= 8)
        {
            spaceNeeded += 2;
        }
        for (s = 0; s < spaceNeeded; s++)
        {
            RtlStringCchPrintfA(p++, myTextSize - steps - 128, " ");
        }
        RtlStringCchPrintfA(p, myTextSize - steps - 128, " %c  %s\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n",
            SPLIT_CHAR, cBuf, info, len, PktLen);
        QCSER_DbgPrintX
        (
            pDevContext,
            DbgMask,
            DbgLevel,
            ("%s", buf)
        );
    }
    else
    {
        RtlStringCchPrintfA(buf, myTextSize - steps - 128, "\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n", info, len, PktLen);
        QCSER_DbgPrintX
        (
            pDevContext,
            DbgMask,
            DbgLevel,
            ("%s", buf)
        );
    }

    ExFreePool(dbgOutputBuffer);
}
