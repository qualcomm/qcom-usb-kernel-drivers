/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B R D . C

GENERAL DESCRIPTION
    This is the file which contains RX functions for QDSS driver.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "QDBMAIN.h"
#include "QDBRD.h"

// Forward declarations for QDSS ring helpers
VOID QdssServicePendingReads(PDEVICE_CONTEXT pDevContext);
static VOID QdssRingWriteLocked(PDEVICE_CONTEXT pDevContext, PVOID Src, ULONG Len);
static VOID QdssSchedulePoll(PDEVICE_CONTEXT pDevContext);

#ifdef EVENT_TRACING
#include "QDBRD.tmh"
#endif

#ifdef QDB_DATA_EMULATION

/****************************************************************************
 *
 * function: QDBRD_ReturnEmulation
 *
 * purpose:  Fills the read request buffer with simulated data (SimData)
 *           and completes the request. Used only when QDB_DATA_EMULATION
 *           is defined.
 *
 * arguments:Queue   = WDF I/O queue handle
 *           Request = WDF read request handle
 *           Length  = requested read length in bytes
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_ReturnEmulation(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
    static LONG readCount = 0;
    NTSTATUS ntStatus;
    PDEVICE_CONTEXT pDevContext;
    PUCHAR readBuffer, currentPtr;
    size_t readLength, dataLength, dataNeeded;

    pDevContext = QdbDeviceGetContext(WdfIoQueueGetDevice(Queue));

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBRD_ReturnEmulation: 0x%p (%dB)\n", pDevContext->PortName, Request, Length)
    );
    ntStatus = WdfRequestRetrieveOutputBuffer
    (
        Request,
        1,          // MinimumRequiredSize
        &readBuffer,
        &readLength
    );

    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_ReturnEmulation: failed to get read buffer  (0x%x)\n", pDevContext->PortName, ntStatus)
        );
        WdfRequestCompleteWithInformation(Request, ntStatus, 0);
        return;
    }
    if (readLength == Length)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_ReturnEmulation: read buffer 0x%p (%dB)\n", pDevContext->PortName, readBuffer, readLength)
        );
    }
    else
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_ReturnEmulation: read buffer 0x%p (%dB/%dB)\n", pDevContext->PortName, readBuffer,
            readLength, Length)
        );
    }
    // fill the read request with SimData[2724]
    currentPtr = readBuffer;
    dataLength = sizeof(ULONG) * 2724;
    if (readLength <= dataLength)
    {
        RtlCopyMemory(currentPtr, (PVOID)SimData, readLength);
    }
    else  // reading more than the sample data
    {
        dataNeeded = readLength;
        while (dataNeeded > dataLength)
        {
            // copy whole sample data each time
            RtlCopyMemory(currentPtr, (PVOID)SimData, dataLength);
            currentPtr += dataLength;
            dataNeeded -= dataLength;
        }
        // last copy -- copy the rest
        if (dataNeeded > 0)
        {
            RtlCopyMemory(currentPtr, (PVOID)SimData, dataNeeded);
        }
    }

    InterlockedIncrement(&readCount);
    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBRD_ReturnEmulation: Complete 0x%p (%dB) [%d]\n", pDevContext->PortName, Request, readLength, readCount)
    );
    ntStatus = STATUS_SUCCESS;
    WdfRequestSetInformation(Request, readLength);
    WdfRequestComplete(Request, ntStatus);

    return;

} // QDBRD_ReturnEmulation

#endif // QDB_DATA_EMULATION

// Re-arm the QDSS poll timer after QDB_QDSS_POLL_INTERVAL_MS.
static VOID QdssSchedulePoll(PDEVICE_CONTEXT pDevContext)
{
    KIRQL level;

    if (pDevContext->QdssPollTimer == NULL)
    {
        return;
    }

    // Check+start under RxLock: cleanup sets DeviceRemoval under it before WdfTimerStop,
    // so the timer can never be restarted after it has been stopped.
    KeAcquireSpinLock(&pDevContext->RxLock, &level);
    if (pDevContext->DeviceRemoval == FALSE)
    {
        WdfTimerStart(pDevContext->QdssPollTimer,
                      WDF_REL_TIMEOUT_IN_MS(QDB_QDSS_POLL_INTERVAL_MS));
    }
    KeReleaseSpinLock(&pDevContext->RxLock, level);
}

// Stop the QDSS poll timer. Must run at PASSIVE_LEVEL (Wait=TRUE).
VOID QDBRD_QdssStopPolling(PDEVICE_CONTEXT pDevContext)
{
    if (pDevContext->QdssPollTimer != NULL)
    {
        WdfTimerStop(pDevContext->QdssPollTimer, TRUE);
        QDB_DbgPrint(QDB_DBG_MASK_READ, QDB_DBG_LEVEL_ERROR,
            ("<%s> QDSS poll timer stopped\n", pDevContext->PortName));
    }
}


VOID QDBRD_QdssTryArmRead(PDEVICE_CONTEXT pDevContext)
{
    KIRQL irql;
    ULONG i;

    if ((pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS) ||
        (pDevContext->DeviceRemoval == TRUE))
    {
        return;
    }

    // Never re-enable the drain once cleanup has cleared it under the same lock
    KeAcquireSpinLock(&pDevContext->RxLock, &irql);
    if (pDevContext->DeviceRemoval == TRUE)
    {
        KeReleaseSpinLock(&pDevContext->RxLock, irql);
        return;
    }
    pDevContext->PipeDrain = TRUE;   // lets SendIOBlock/completion proceed
    KeReleaseSpinLock(&pDevContext->RxLock, irql);

    // Top up every free slot; SendIOBlock skips slots already in flight
    for (i = 0; i < pDevContext->NumOfRxReqs; i++)
    {
        QDBRD_SendIOBlock(pDevContext, i);
    }

    QDB_DbgPrint(QDB_DBG_MASK_READ, QDB_DBG_LEVEL_ERROR,
        ("<%s> QDSSDIAG QdssTryArmRead: posted up to %u URBs, InFlight=%u DrainingRx=%d\n",
        pDevContext->PortName, pDevContext->NumOfRxReqs,
        pDevContext->QdssUrbsInFlight, pDevContext->Stats.DrainingRx));

    // Nothing posted and nothing pending -> retry shortly rather than wedge
    if (pDevContext->QdssUrbsInFlight == 0)
    {
        QDB_DbgPrint(QDB_DBG_MASK_READ, QDB_DBG_LEVEL_ERROR,
            ("<%s> QDSSDIAG QdssTryArmRead: no URB posted, retrying in %u ms\n",
            pDevContext->PortName, QDB_QDSS_POLL_INTERVAL_MS));
        QdssSchedulePoll(pDevContext);
    }
}

// Timer callback: re-arm the pipe and flush the ring. Passive-level (see QDBPNP setup).
VOID QDBRD_QdssPollTimer(WDFTIMER Timer)
{
    PDEVICE_CONTEXT pDevContext = QdbDeviceGetContext(WdfTimerGetParentObject(Timer));

    QDBRD_QdssTryArmRead(pDevContext);

    // Drain too: the bounded service loop can leave ring data undelivered
    QdssServicePendingReads(pDevContext);
}

// Append Len bytes to the QDSS ring; caller holds RxLock. Overflow drops the whole block.
static VOID QdssRingWriteLocked(PDEVICE_CONTEXT pDevContext, PVOID Src, ULONG Len)
{
    ULONG firstSeg;

    if ((pDevContext->QdssRingBuf == NULL) || (Len == 0))
    {
        return;
    }

    // Reserved bytes are mid-copy by an unlocked consumer: not free space.
    if (pDevContext->QdssRingUsed + pDevContext->QdssRingReserved + Len >
        pDevContext->QdssRingBufSize)
    {
        // Under RxLock here, so plain arithmetic is safe
        pDevContext->Stats.BytesDropped += Len;
        pDevContext->Stats.NumRingOverruns++;
        QDB_DbgPrint(QDB_DBG_MASK_READ, QDB_DBG_LEVEL_ERROR,
            ("<%s> QdssRingWriteLocked: ring full, dropped %uB (total %I64uB / %u overruns)\n",
            pDevContext->PortName, Len,
            pDevContext->Stats.BytesDropped, pDevContext->Stats.NumRingOverruns));
        return;
    }

    firstSeg = min(Len, pDevContext->QdssRingBufSize - pDevContext->QdssRingHead);
    RtlCopyMemory(pDevContext->QdssRingBuf + pDevContext->QdssRingHead, Src, firstSeg);
    if (Len > firstSeg)
    {
        RtlCopyMemory(pDevContext->QdssRingBuf, (PUCHAR)Src + firstSeg, Len - firstSeg);
    }
    pDevContext->QdssRingHead = (pDevContext->QdssRingHead + Len) % pDevContext->QdssRingBufSize;
    pDevContext->QdssRingUsed += Len;
}

// Drain ring -> parked QDSS TRACE reads. Bounded so we cannot spin at DISPATCH_LEVEL.
VOID QdssServicePendingReads(PDEVICE_CONTEXT pDevContext)
{
    ULONG guard = (pDevContext->NumOfRxReqs + 1) * 2;

    // One deliverer at a time: concurrent dequeue+reserve would reorder the stream
    if (InterlockedCompareExchange(&pDevContext->QdssDeliverBusy, 1, 0) != 0)
    {
        return;
    }

    while (guard-- > 0)
    {
        WDFREQUEST parkedReq = NULL;
        PVOID      appBuf    = NULL;
        size_t     appLen    = 0;
        ULONG      copyLen;
        ULONG      firstSeg;
        ULONG      tail;
        KIRQL      ringLevel;
        BOOLEAN    hasData;

        KeAcquireSpinLock(&pDevContext->RxLock, &ringLevel);
        hasData = (pDevContext->QdssRingBuf != NULL && pDevContext->QdssRingUsed > 0);
        KeReleaseSpinLock(&pDevContext->RxLock, ringLevel);

        if (!hasData)
        {
            break;
        }

        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(pDevContext->QdssRxPendingQueue, &parkedReq)))
        {
            break;
        }

        // min=1 accepts any app buffer size
        if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(parkedReq, 1, &appBuf, &appLen)) ||
            appBuf == NULL || appLen == 0)
        {
            // appLen==0 copies nothing and would re-loop forever, so fail it
            WdfRequestCompleteWithInformation(parkedReq, STATUS_INVALID_PARAMETER, 0);
            continue;
        }

        KeAcquireSpinLock(&pDevContext->RxLock, &ringLevel);

        // Re-check under lock: ring may have emptied. Completing 0B looks like EOF, so requeue.
        if ((pDevContext->QdssRingBuf == NULL) || (pDevContext->QdssRingUsed == 0))
        {
            NTSTATUS requeue;

            KeReleaseSpinLock(&pDevContext->RxLock, ringLevel);

            // A dropped request wedges queue teardown (WDF_VIOLATION), so complete it
            requeue = WdfRequestRequeue(parkedReq);
            if (!NT_SUCCESS(requeue))
            {
                WdfRequestCompleteWithInformation(parkedReq, requeue, 0);
            }
            break;
        }

        // Reserve under the lock, copy outside it: no 128 KB memcpy at DISPATCH_LEVEL
        // while holding RxLock. QdssRingReserved keeps the producer off these bytes.
        copyLen  = (ULONG)min(pDevContext->QdssRingUsed, (ULONG)appLen);
        tail     = pDevContext->QdssRingTail;
        firstSeg = min(copyLen, pDevContext->QdssRingBufSize - tail);
        pDevContext->QdssRingTail      = (tail + copyLen) % pDevContext->QdssRingBufSize;
        pDevContext->QdssRingUsed     -= copyLen;
        pDevContext->QdssRingReserved += copyLen;
        KeReleaseSpinLock(&pDevContext->RxLock, ringLevel);

        RtlCopyMemory(appBuf, pDevContext->QdssRingBuf + tail, firstSeg);
        if (copyLen > firstSeg)
        {
            RtlCopyMemory((PUCHAR)appBuf + firstSeg, pDevContext->QdssRingBuf, copyLen - firstSeg);
        }

        // copy done -- release the range back to the producer
        KeAcquireSpinLock(&pDevContext->RxLock, &ringLevel);
        pDevContext->QdssRingReserved -= copyLen;
        KeReleaseSpinLock(&pDevContext->RxLock, ringLevel);

        WdfRequestCompleteWithInformation(parkedReq, STATUS_SUCCESS, copyLen);
    }

    InterlockedExchange(&pDevContext->QdssDeliverBusy, 0);

    // Work left (guard expired or a caller was turned away): finish from the timer
    {
        KIRQL   ringLevel;
        BOOLEAN hasData;
        ULONG   parked = 0;

        KeAcquireSpinLock(&pDevContext->RxLock, &ringLevel);
        hasData = ((pDevContext->QdssRingBuf != NULL) && (pDevContext->QdssRingUsed > 0));
        KeReleaseSpinLock(&pDevContext->RxLock, ringLevel);

        if (hasData)
        {
            WdfIoQueueGetState(pDevContext->QdssRxPendingQueue, &parked, NULL);
            if (parked > 0)
            {
                QdssSchedulePoll(pDevContext);
            }
        }
    }
}

/****************************************************************************
 *
 * function: QDBRD_IoRead
 *
 * purpose:  WDF read dispatch callback. Stops pipe draining, validates
 *           the file context and pipe availability, then forwards the
 *           request to QDBRD_ReadUSB.
 *
 * arguments:Queue   = WDF I/O queue handle
 *           Request = WDF read request handle
 *           Length  = requested read length in bytes
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_IoRead
(
    WDFQUEUE   Queue,
    WDFREQUEST Request,
    size_t     Length
)
{
    PDEVICE_CONTEXT pDevContext;
    PFILE_CONTEXT   fileContext = NULL;

    pDevContext = QdbDeviceGetContext(WdfIoQueueGetDevice(Queue));

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBRD_IoRead: 0x%p (%dB)\n", pDevContext->PortName, Request, Length)
    );

    fileContext = QdbFileGetContext(WdfRequestGetFileObject(Request));

    // DPL only: QDSS owns its drain, and a DEBUG read must not clear PipeDrain
    if (pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS)
    {
        QDBRD_PipeDrainStop(pDevContext);
    }

    if ((fileContext->Type == QDB_FILE_TYPE_TRACE) || (fileContext->Type == QDB_FILE_TYPE_DPL))
    {
#ifdef QDB_DATA_EMULATION

        QDBRD_ReturnEmulation(Queue, Request, Length);
        return;

#else // QDB_DATA_EMULATION

        if (fileContext->TraceIN == NULL)
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBRD_IoRead: no USB pipe for read\n", pDevContext->PortName)
            );
            WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
            return;
        }

#endif // QDB_DATA_EMULATION
    }
    else if (fileContext->Type == QDB_FILE_TYPE_DEBUG)
    {
        if ((fileContext->DebugIN == NULL) || (fileContext->DebugOUT == NULL))
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBRD_IoRead: read/write pipes not available\n", pDevContext->PortName)
            );
            WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
            return;
        }
    }

    if (Length == 0)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> QDBRD_IoRead: req length 0\n", pDevContext->PortName)
        );
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
        return;
    }

    // QDSS TRACE path: serve inline when the ring has data (parking every read
    // costs a round trip that throttles the app); park only when it is empty.
    if ((pDevContext->FunctionType == QDB_FUNCTION_TYPE_QDSS) &&
        (fileContext->Type == QDB_FILE_TYPE_TRACE))
    {
        KIRQL    ringLevel;
        NTSTATUS fwdStatus;
        NTSTATUS getBuf;
        PVOID    appBuf   = NULL;
        size_t   appLen   = 0;
        ULONG    parked   = 0;
        ULONG    copyLen  = 0;
        ULONG    firstSeg = 0;
        ULONG    tail     = 0;

        getBuf = WdfRequestRetrieveOutputBuffer(Request, 1, &appBuf, &appLen);
        if (!NT_SUCCESS(getBuf) || (appBuf == NULL))
        {
            WdfRequestCompleteWithInformation(Request,
                NT_SUCCESS(getBuf) ? STATUS_INVALID_PARAMETER : getBuf, 0);
            return;
        }

        // Inline serve is a deliverer too: own QdssDeliverBusy or park, so FIFO holds
        if (InterlockedCompareExchange(&pDevContext->QdssDeliverBusy, 1, 0) == 0)
        {
            // Parked reads are older: serving this one first would reorder the stream
            WdfIoQueueGetState(pDevContext->QdssRxPendingQueue, &parked, NULL);

            // Reserve under the lock, copy outside it (see QdssServicePendingReads)
            KeAcquireSpinLock(&pDevContext->RxLock, &ringLevel);
            if ((parked == 0) && (pDevContext->QdssRingBuf != NULL) && (pDevContext->QdssRingUsed > 0))
            {
                copyLen  = (ULONG)min(pDevContext->QdssRingUsed, (ULONG)appLen);
                tail     = pDevContext->QdssRingTail;
                firstSeg = min(copyLen, pDevContext->QdssRingBufSize - tail);
                pDevContext->QdssRingTail      = (tail + copyLen) % pDevContext->QdssRingBufSize;
                pDevContext->QdssRingUsed     -= copyLen;
                pDevContext->QdssRingReserved += copyLen;
            }
            KeReleaseSpinLock(&pDevContext->RxLock, ringLevel);

            if (copyLen > 0)
            {
                RtlCopyMemory(appBuf, pDevContext->QdssRingBuf + tail, firstSeg);
                if (copyLen > firstSeg)
                {
                    RtlCopyMemory((PUCHAR)appBuf + firstSeg, pDevContext->QdssRingBuf, copyLen - firstSeg);
                }

                // copy done -- release the range back to the producer
                KeAcquireSpinLock(&pDevContext->RxLock, &ringLevel);
                pDevContext->QdssRingReserved -= copyLen;
                KeReleaseSpinLock(&pDevContext->RxLock, ringLevel);
            }

            InterlockedExchange(&pDevContext->QdssDeliverBusy, 0);

            if (copyLen > 0)
            {
                QDB_DbgPrint
                (
                    QDB_DBG_MASK_READ,
                    QDB_DBG_LEVEL_ERROR,
                    ("<%s> QDSSDIAG IoRead: served %uB from ring (ring now %uB) InFlight=%u\n",
                    pDevContext->PortName, copyLen,
                    pDevContext->QdssRingUsed, pDevContext->QdssUrbsInFlight)
                );
                // A deliverer turned away while we held Busy has no retry: drain for it
                QdssServicePendingReads(pDevContext);
                QDBRD_QdssTryArmRead(pDevContext);
                WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, copyLen);
                return;
            }
        }

        // Ring empty, older reads parked, or deliverer busy: park behind them
        fwdStatus = WdfRequestForwardToIoQueue(Request, pDevContext->QdssRxPendingQueue);
        if (!NT_SUCCESS(fwdStatus))
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBRD_IoRead: WdfRequestForwardToIoQueue failed ST 0x%x\n",
                pDevContext->PortName, fwdStatus)
            );
            WdfRequestCompleteWithInformation(Request, fwdStatus, 0);
            return;
        }

        QdssServicePendingReads(pDevContext);
        QDBRD_QdssTryArmRead(pDevContext);
        return;
    }

    QDBRD_ReadUSB(Queue, Request, (ULONG)Length);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBRD_IoRead: 0x%p (%dB)\n", pDevContext->PortName, Request, Length)
    );
    return;
}  // QDBRD_IoRead

/****************************************************************************
 *
 * function: QDBRD_ReadUSB
 *
 * purpose:  Formats a WDF USB read URB for the appropriate IN pipe and
 *           sends it to the USB I/O target asynchronously.
 *
 * arguments:Queue   = WDF I/O queue handle
 *           Request = WDF read request handle
 *           Length  = requested read length in bytes
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_ReadUSB
(
    IN WDFQUEUE         Queue,
    IN WDFREQUEST       Request,
    IN ULONG            Length
)
{
    NTSTATUS         ntStatus;
    PDEVICE_CONTEXT  pDevContext;
    PFILE_CONTEXT    fileContext = NULL;
    PREQUEST_CONTEXT rxContext = NULL;
    WDFMEMORY        hRxBufferObj;
    WDFUSBPIPE       pipeIN;
    BOOLEAN          bResult;
    ULONG            reqIdx;

    pDevContext = QdbDeviceGetContext(WdfIoQueueGetDevice(Queue));
    rxContext = QdbRequestGetContext(Request);
    rxContext->Index = reqIdx = InterlockedIncrement(&(pDevContext->RxCount));

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBRD_ReadUSB: [%u] 0x%p (%dB)\n", pDevContext->PortName, reqIdx, Request, Length)
    );

    fileContext = QdbFileGetContext(WdfRequestGetFileObject(Request));
    if ((fileContext->Type == QDB_FILE_TYPE_TRACE) || (fileContext->Type == QDB_FILE_TYPE_DPL))
    {
        pipeIN = fileContext->TraceIN;
    }
    else if (fileContext->Type == QDB_FILE_TYPE_DEBUG)
    {
        pipeIN = fileContext->DebugIN;
    }
    else
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_ReadUSB: read pipe not available, abort\n", pDevContext->PortName)
        );
        ntStatus = STATUS_UNSUCCESSFUL;
        WdfRequestCompleteWithInformation(Request, ntStatus, 0);
        return;
    }

    ntStatus = WdfRequestRetrieveOutputMemory(Request, &hRxBufferObj);
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_ReadUSB: failure retrieving RX buffer (0x%x)\n", pDevContext->PortName, ntStatus)
        );
        WdfRequestCompleteWithInformation(Request, ntStatus, 0);
        return;
    }

    // format URB and set URB flag to (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK)
    ntStatus = WdfUsbTargetPipeFormatRequestForRead(pipeIN, Request, hRxBufferObj, NULL);
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_ReadUSB: failed to format URB (0x%x)\n", pDevContext->PortName, ntStatus)
        );
        WdfRequestCompleteWithInformation(Request, ntStatus, 0);
        return;
    }

    rxContext->IoBlock = hRxBufferObj;

    WdfRequestSetCompletionRoutine(Request, QDBRD_ReadUSBCompletion, pDevContext);

    if (Length > QDB_USB_TRANSFER_SIZE_MAX)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_ReadUSB: Warning: oversized RX (%u)\n", pDevContext->PortName, Length)
        );
    }

    InterlockedIncrement(&(pDevContext->Stats.OutstandingRx));

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> QDBRD_ReadUSB: Sending [%u] to 0x%p\n", pDevContext->PortName, reqIdx, pDevContext->MyIoTarget)
    );

    // forward to USB layer
    bResult = WdfRequestSend(Request, pDevContext->MyIoTarget, WDF_NO_SEND_OPTIONS);
    if (bResult == FALSE)
    {
        ntStatus = WdfRequestGetStatus(Request);
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_ReadUSB: WdfRequestSend failed (0x%x)\n", pDevContext->PortName, ntStatus)
        );

        // In this case, completion routine will not be called, so need to rewind
        InterlockedDecrement(&(pDevContext->Stats.OutstandingRx));
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBRD_ReadUSB: [%u] 0x%p (%dB)\n", pDevContext->PortName, reqIdx, Request, Length)
    );
}  // QDBRD_ReadUSB

/****************************************************************************
 *
 * function: QDBRD_ReadUSBCompletion
 *
 * purpose:  WDF completion routine for USB read requests. Extracts the
 *           number of bytes read, completes the original request, and
 *           updates the outstanding RX counter.
 *
 * arguments:Request          = WDF request handle
 *           Target           = WDF USB pipe I/O target
 *           CompletionParams = USB completion parameters including byte count
 *           Context          = pointer to the device context
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_ReadUSBCompletion
(
    WDFREQUEST                  Request,
    WDFIOTARGET                 Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    WDFCONTEXT                  Context
)
{
    PWDF_USB_REQUEST_COMPLETION_PARAMS usbInfo;
    NTSTATUS         ntStatus;
    PDEVICE_CONTEXT  pDevContext;
    PREQUEST_CONTEXT rxContext;
    WDFUSBPIPE       pipeIN;
    ULONG            readLength = 0;

    pDevContext = (PDEVICE_CONTEXT)Context;
    rxContext = QdbRequestGetContext(Request);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBRD_ReadUSBCompletion:[%u] 0x%p\n", pDevContext->PortName,
        rxContext->Index, Request)
    );

    pipeIN = (WDFUSBPIPE)Target;
    usbInfo = CompletionParams->Parameters.Usb.Completion;
    ntStatus = CompletionParams->IoStatus.Status;
    if (!NT_SUCCESS(ntStatus))
    {
        // TODO: need to reset pipe at PASSIVE_LEVEL?
        // QueuePassiveLevelCallback(WdfIoTargetGetDevice(Target), pipeIN);
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> QDBRD_ReadUSBCompletion: error completion 0x%p(0x%x)\n", pDevContext->PortName,
            Request, ntStatus)
        );
    }
    else
    {
        readLength = (ULONG)usbInfo->Parameters.PipeRead.Length;
        WdfRequestSetInformation(Request, readLength);
    }

    WdfRequestComplete(Request, ntStatus);
    if (InterlockedDecrement(&(pDevContext->Stats.OutstandingRx)) == 0)
    {
        InterlockedIncrement(&(pDevContext->Stats.NumRxExhausted));
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBRD_ReadUSBCompletion: 0x%p (%dB/%dB) - P%u\n", pDevContext->PortName,
        Request, readLength, pDevContext->MaxXfrSize, pDevContext->Stats.OutstandingRx)
    );

    return;

}  // QDBRD_ReadUSBCompletion

/* --------------------------------------------------------------------
 *               PIPE DRAINING FUNCTIONS
 * --------------------------------------------------------------------*/

/****************************************************************************
 *
 * function: QDBRD_AllocateRequestsRx
 *
 * purpose:  Allocates WDF request and memory objects for DPL pipe draining.
 *           Only applicable when the device function type is DPL.
 *
 * arguments:pDevContext = pointer to the device context
 *
 * returns:  NT status; STATUS_NOT_SUPPORTED if not a DPL device,
 *           STATUS_NO_MEMORY if allocation fails
 *
 ****************************************************************************/
NTSTATUS QDBRD_AllocateRequestsRx(PDEVICE_CONTEXT pDevContext)
{
    NTSTATUS              ntStatus;
    WDF_OBJECT_ATTRIBUTES reqAttrib;
    ULONG i;

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> -->QDBRD_AllocateRequestsRx: size %dB\n", pDevContext->PortName, QDB_MAX_IO_SIZE_RX)
    );

    // RxLock is initialized in QDBPNP_EvtDriverDeviceAdd (before any cleanup can take it)

    // ENABLING CHANGE: pool was DPL-only; QDSS now reuses it for EP7IN reads
    if ((pDevContext->FunctionType != QDB_FUNCTION_TYPE_DPL) &&
        (pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBRD_AllocateRequestsRx: not DPL/QDSS\n", pDevContext->PortName)
        );
        return STATUS_NOT_SUPPORTED;
    }

    // WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&reqAttrib, REQUEST_CONTEXT);
    WDF_OBJECT_ATTRIBUTES_INIT(&reqAttrib);

    for (i = 0; i < IO_REQ_NUM_RX; i++)
    {
        pDevContext->RxRequest[i].DeviceContext = pDevContext;
        pDevContext->RxRequest[i].Index = i;
        pDevContext->NumOfRxReqs = i + 1;

        // WDF request
        ntStatus = WdfRequestCreate
        (
            &reqAttrib,
            NULL,
            &(pDevContext->RxRequest[i].IoRequest)
        );
        if (ntStatus != STATUS_SUCCESS)
        {
            --pDevContext->NumOfRxReqs;
            break;
        }

        // WDF memory
        ntStatus = WdfMemoryCreate
        (
            WDF_NO_OBJECT_ATTRIBUTES,
            NonPagedPool,
            QDB_TAG_RD,
            (pDevContext->FunctionType == QDB_FUNCTION_TYPE_QDSS) ? QDB_MAX_IO_SIZE_QDSS_RX : QDB_MAX_IO_SIZE_RX,
            &(pDevContext->RxRequest[i].IoMemory),
            &(pDevContext->RxRequest[i].IoBuffer)
        );
        if (ntStatus != STATUS_SUCCESS)
        {
            --pDevContext->NumOfRxReqs;
            break;
        }

        pDevContext->RxRequest[i].IsPending = 0;

        // TEST CODE
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_DETAIL,
                ("<%s> QDBRD_AllocateRequestsRx [%d]: WDF mem 0x%p (buf 0x%p)\n", pDevContext->PortName, i,
                pDevContext->RxRequest[i].IoMemory, pDevContext->RxRequest[i].IoBuffer)
            );
        }

    }  // for

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> <--QDBRD_AllocateRequestsRx: RxReqs %d\n", pDevContext->PortName, pDevContext->NumOfRxReqs)
    );

    if (pDevContext->NumOfRxReqs == 0)
    {
        return STATUS_NO_MEMORY;
    }

    return STATUS_SUCCESS;

}  // QDBRD_AllocateRequestsRx

/****************************************************************************
 *
 * function: QDBRD_FreeIoBuffer
 *
 * purpose:  Releases WDF memory and request objects previously allocated
 *           by QDBRD_AllocateRequestsRx. Only applicable for DPL devices.
 *
 * arguments:pDevContext = pointer to the device context
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_FreeIoBuffer(PDEVICE_CONTEXT pDevContext)
{
    ULONG i;
    PQDB_IO_REQUEST ioReq = pDevContext->RxRequest;
    ULONG numElements = pDevContext->NumOfRxReqs;

    // ENABLING CHANGE: must mirror AllocateRequestsRx or QDSS objects leak
    if ((pDevContext->FunctionType != QDB_FUNCTION_TYPE_DPL) &&
        (pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS))
    {
        return;
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBRD_FreeIoBuffer: numElements %d\n", pDevContext->PortName, numElements)
    );

    if (numElements == 0)
    {
        return;
    }

    for (i = 0; i < numElements; i++)
    {
        if (ioReq[i].IoBuffer != NULL)
        {
            WdfObjectDelete(ioReq[i].IoMemory);
            WdfObjectDelete(ioReq[i].IoRequest);
        }
        ioReq[i].IoBuffer = NULL;
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBRD_FreeIoBuffer: numElements %d\n", pDevContext->PortName, numElements)
    );

}  // QDBRD_FreeIoBuffer

/****************************************************************************
 *
 * function: QDBRD_PipeDrainStart
 *
 * purpose:  Starts draining the DPL IN pipe by sending all pre-allocated
 *           RX requests to the USB target. No-op if draining is already
 *           active or the device is not a DPL function.
 *
 * arguments:pDevContext = pointer to the device context
 *
 * returns:  NT status; STATUS_NOT_SUPPORTED if not a DPL device
 *
 ****************************************************************************/
NTSTATUS QDBRD_PipeDrainStart(PDEVICE_CONTEXT pDevContext)
{
    KIRQL    oldLevel;
    ULONG i;

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBRD_PipeDrainStart\n", pDevContext->PortName)
    );

    // ENABLING CHANGE: QDSS calls this from QdssTryArmRead to set PipeDrain
    if ((pDevContext->FunctionType != QDB_FUNCTION_TYPE_DPL) &&
        (pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBRD_PipeDrainStart: not DPL/QDSS\n", pDevContext->PortName)
        );
        return STATUS_NOT_SUPPORTED;
    }

    KeAcquireSpinLock(&pDevContext->RxLock, &oldLevel);
    if (pDevContext->PipeDrain == TRUE)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> <--QDBRD_PipeDrainStart: no action\n", pDevContext->PortName)
        );
        KeReleaseSpinLock(&pDevContext->RxLock, oldLevel);
        return STATUS_SUCCESS;
    }

    pDevContext->PipeDrain = TRUE;
    pDevContext->Stats.BytesDrained = 0;
    KeReleaseSpinLock(&pDevContext->RxLock, oldLevel);

    // send read requests
    for (i = 0; i < pDevContext->NumOfRxReqs; i++)
    {
        QDBRD_SendIOBlock(pDevContext, i);
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBRD_PipeDrainStart\n", pDevContext->PortName)
    );

    return STATUS_SUCCESS;
}  // QDBRD_PipeDrainStart

// Undo SendIOBlock's gate accounting and slot claim when no completion will run.
static VOID QDBRD_SendRewind(PDEVICE_CONTEXT pDevContext, INT Index)
{
    KIRQL level;

    KeAcquireSpinLock(&pDevContext->RxLock, &level);
    InterlockedDecrement(&(pDevContext->Stats.DrainingRx));
    if ((pDevContext->FunctionType == QDB_FUNCTION_TYPE_QDSS) && (pDevContext->QdssUrbsInFlight > 0))
    {
        pDevContext->QdssUrbsInFlight--;
    }
    KeReleaseSpinLock(&pDevContext->RxLock, level);

    InterlockedExchange((volatile LONG *)&pDevContext->RxRequest[Index].IsPending, 0);
}

/****************************************************************************
 *
 * function: QDBRD_SendIOBlock
 *
 * purpose:  Formats and sends a single pre-allocated RX request to the
 *           DPL IN pipe. Skips if the request is already pending.
 *
 * arguments:pDevContext = pointer to the device context
 *           Index       = index into the RxRequest array
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_SendIOBlock(PDEVICE_CONTEXT pDevContext, INT Index)
{
    NTSTATUS                 ntStatus;
    BOOLEAN                  bResult;
    WDFUSBPIPE               pipeIN = pDevContext->TraceIN;
    WDF_REQUEST_SEND_OPTIONS sendOptions;
    PWDF_REQUEST_SEND_OPTIONS pSendOptions = WDF_NO_SEND_OPTIONS;

    // ENABLING CHANGE: EP7IN submit path, driven per-slot by QdssTryArmRead
    if ((pDevContext->FunctionType != QDB_FUNCTION_TYPE_DPL) &&
        (pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_SendIOBlock: wrong dev type %u\n", pDevContext->PortName, pDevContext->FunctionType)
        );
        return;
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBRD_SendIOBlock: [%d/%d] EP 0x%p target 0x%p F%u\n",
        pDevContext->PortName, Index, pDevContext->NumOfRxReqs, pipeIN,
        pDevContext->MyIoTarget, pDevContext->Stats.IoFailureCount)
    );

    // Claim the slot atomically: IoRead, the completion DPC and the poll timer all
    // target the same slot, and a double submit of one WDFREQUEST bugchecks.
    if (InterlockedCompareExchange((volatile LONG *)&pDevContext->RxRequest[Index].IsPending,
                                   1, 0) != 0)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> <--QDBRD_SendIOBlock[%d]: still pending\n", pDevContext->PortName, Index)
        );
        return;
    }

    // Gate + count under RxLock: cleanup clears PipeDrain under it and then waits for
    // DrainingRx==0, so no URB can be sent after that wait has completed.
    {
        KIRQL   gateLevel;
        BOOLEAN allowed;

        KeAcquireSpinLock(&pDevContext->RxLock, &gateLevel);
        allowed = (pDevContext->PipeDrain == TRUE) && (pDevContext->DeviceRemoval == FALSE);
        if (allowed)
        {
            InterlockedIncrement(&(pDevContext->Stats.DrainingRx));
            if (pDevContext->FunctionType == QDB_FUNCTION_TYPE_QDSS)
            {
                pDevContext->QdssUrbsInFlight++;
            }
        }
        KeReleaseSpinLock(&pDevContext->RxLock, gateLevel);

        if (!allowed)
        {
            InterlockedExchange((volatile LONG *)&pDevContext->RxRequest[Index].IsPending, 0);
            return;
        }
    }

    ntStatus = WdfIoTargetFormatRequestForRead
    (
        pDevContext->MyIoTarget,
        pDevContext->RxRequest[Index].IoRequest,
        pDevContext->RxRequest[Index].IoMemory,
        0, 0
    );
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBRD_SendIOBlock: failed to format RX req[%d]\n", pDevContext->PortName, Index)
        );
        QDBRD_SendRewind(pDevContext, Index);
        return;
    }

    if (pipeIN == NULL)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBRD_SendIOBlock: no EP available\n", pDevContext->PortName)
        );
        QDBRD_SendRewind(pDevContext, Index);
        return;
    }

    ntStatus = WdfUsbTargetPipeFormatRequestForRead
    (
        pipeIN,
        pDevContext->RxRequest[Index].IoRequest,
        pDevContext->RxRequest[Index].IoMemory,
        NULL
    );
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBRD_SendIOBlock: failed to format URB (0x%x)\n", pDevContext->PortName, ntStatus)
        );
        QDBRD_SendRewind(pDevContext, Index);
        return;
    }

    WdfRequestSetCompletionRoutine
    (
        pDevContext->RxRequest[Index].IoRequest,
        QDBRD_PipeDrainCompletion,
        &(pDevContext->RxRequest[Index])
    );

    // DrainingRx/QdssUrbsInFlight were already counted at the gate above
    if (pDevContext->FunctionType == QDB_FUNCTION_TYPE_QDSS)
    {
        // QDSS: bound the wait so a silent TraceIN recycles the URB, not parks the slot
        WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_TIMEOUT);
        WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&sendOptions,
                                             WDF_REL_TIMEOUT_IN_SEC(QDB_QDSS_URB_TIMEOUT_SEC));
        pSendOptions = &sendOptions;
    }

    bResult = WdfRequestSend
    (
        pDevContext->RxRequest[Index].IoRequest,
        pDevContext->MyIoTarget,
        pSendOptions
    );
    if (bResult == FALSE)
    {
        ntStatus = WdfRequestGetStatus(pDevContext->RxRequest[Index].IoRequest);
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_SendIOBlock: WdfRequestSend failed [%d] (0x%x)\n", pDevContext->PortName, Index, ntStatus)
        );

        // Send left the request completed; re-formatting it on the next re-arm
        // without a reuse bugchecks WDF_VIOLATION, so reuse it here.
        {
            WDF_REQUEST_REUSE_PARAMS failParams;
            WDF_REQUEST_REUSE_PARAMS_INIT(&failParams, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_SUCCESS);
            WdfRequestReuse(pDevContext->RxRequest[Index].IoRequest, &failParams);
        }

        // In this case, completion routine will not be called, so need to rewind
        QDBRD_SendRewind(pDevContext, Index);
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBRD_SendIOBlock: [%d]\n", pDevContext->PortName, Index)
    );
} // QDBRD_SendIOBlock

/****************************************************************************
 *
 * function: QDBRD_PipeDrainStop
 *
 * purpose:  Stops DPL pipe draining by clearing the PipeDrain flag so
 *           that completion routines will not re-submit requests.
 *
 * arguments:pDevContext = pointer to the device context
 *
 * returns:  NT status; STATUS_NOT_SUPPORTED if not a DPL device
 *
 ****************************************************************************/
NTSTATUS QDBRD_PipeDrainStop(PDEVICE_CONTEXT pDevContext)
{
    // ULONG i;
    KIRQL oldLevel;

    // ENABLING CHANGE: lets QDSS clear PipeDrain outside EvtDeviceCleanupCallback
    // (not reachable for QDSS today -- the IoRead caller excludes it).
    if ((pDevContext->FunctionType != QDB_FUNCTION_TYPE_DPL) &&
        (pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_PipeDrainStop: wrong dev type %u\n", pDevContext->PortName, pDevContext->FunctionType)
        );
        return STATUS_NOT_SUPPORTED;
    }

    KeAcquireSpinLock(&pDevContext->RxLock, &oldLevel);
    if (pDevContext->PipeDrain == FALSE)
    {
        KeReleaseSpinLock(&pDevContext->RxLock, oldLevel);
        return STATUS_SUCCESS;
    }

    // set to FALSE so that completion routine won't send Request
    pDevContext->PipeDrain = FALSE;
    KeReleaseSpinLock(&pDevContext->RxLock, oldLevel);

    /***
       for (i = 0; i < pDevContext->NumOfRxReqs; i++)
       {
          WdfRequestCancelSentRequest(pDevContext->RxRequest[i].IoRequest);
       }
    ***/

    return STATUS_SUCCESS;

}  // QDBRD_PipeDrainStop

/****************************************************************************
 *
 * function: QDBRD_PipeDrainCompletion
 *
 * purpose:  WDF completion routine for DPL pipe drain requests. Recycles
 *           the WDF request and re-submits it if draining is still active
 *           and the failure threshold has not been exceeded. Signals
 *           DrainStoppedEvent when all drain requests have completed.
 *
 * arguments:Request          = WDF request handle
 *           Target           = WDF USB pipe I/O target
 *           CompletionParams = USB completion parameters including byte count
 *           Context          = pointer to the QDB_IO_REQUEST structure
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_PipeDrainCompletion
(
    WDFREQUEST                  Request,
    WDFIOTARGET                 Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    WDFCONTEXT                  Context
)
{
    NTSTATUS        ntStatus;
    PDEVICE_CONTEXT pDevContext;
    PQDB_IO_REQUEST ioReq;
    ULONG           readLength = 0;
    WDF_REQUEST_REUSE_PARAMS reqPrams;
    BOOLEAN         sendAgain = TRUE;
    BOOLEAN         deviceGone = FALSE;
    LONG            remaining;
    PWDF_USB_REQUEST_COMPLETION_PARAMS usbInfo;

    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Target);

    ioReq = (PQDB_IO_REQUEST)Context;
    pDevContext = ioReq->DeviceContext;

    usbInfo = CompletionParams->Parameters.Usb.Completion;
    ntStatus = CompletionParams->IoStatus.Status;
    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBRD_PipeDrainCompletion: [%d] (ST 0x%x)\n", pDevContext->PortName,
        ioReq->Index, ntStatus)
    );
    if (!NT_SUCCESS(ntStatus))
    {
        // Only an absent device stops the pipeline; everything else is transient
        // and re-armed, so one bad completion cannot kill the stream.
        if ((ntStatus == STATUS_NO_SUCH_DEVICE) ||
            (ntStatus == STATUS_DEVICE_NOT_CONNECTED) ||
            (ntStatus == STATUS_DELETE_PENDING))
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDSSDIAG PipeDrainCompletion: device gone [%d] ST 0x%x, stop re-arm\n",
                pDevContext->PortName, ioReq->Index, ntStatus)
            );
            sendAgain = FALSE;
            deviceGone = TRUE;
        }
        else if (ntStatus == STATUS_IO_TIMEOUT)
        {
            // Expected on a quiet pipe: recycle the slot, do not count as a failure
            sendAgain = TRUE;

            // Keep bytes DMA'd before the abort. A full-length value means the URB was never
            // processed (length untouched) -- a full buffer completes OK, it can't time out.
            if ((pDevContext->FunctionType == QDB_FUNCTION_TYPE_QDSS) && (usbInfo != NULL) &&
                (usbInfo->Parameters.PipeRead.Length < QDB_MAX_IO_SIZE_QDSS_RX))
            {
                readLength = (ULONG)usbInfo->Parameters.PipeRead.Length;
            }
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDSSDIAG PipeDrainCompletion: URB[%d] %us timeout recycle (DrainingRx=%d)\n",
                pDevContext->PortName, ioReq->Index,
                QDB_QDSS_URB_TIMEOUT_SEC, pDevContext->Stats.DrainingRx)
            );
        }
        else if (InterlockedIncrement((volatile LONG *)&pDevContext->Stats.IoFailureCount) >
                 (LONG)pDevContext->IoFailureThreshold)
        {
            // Bound the retries: a permanently failing pipe would re-arm forever
            // and an inline completion would recurse until the stack overflows.
            sendAgain = FALSE;
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDSSDIAG PipeDrainCompletion: URB[%d] ST 0x%x failed %u times (> %u), giving up\n",
                pDevContext->PortName, ioReq->Index, ntStatus,
                pDevContext->Stats.IoFailureCount, pDevContext->IoFailureThreshold)
            );
        }
        else
        {
            // QDSS: retry errors from the timer, never inline -- a send that fails and
            // completes synchronously would recurse here once per retry (stack overflow).
            sendAgain = (pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS);
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDSSDIAG PipeDrainCompletion: transient error URB[%d] ST 0x%x, re-arming (DrainingRx=%d IoFailCnt=%u)\n",
                pDevContext->PortName, ioReq->Index, ntStatus,
                pDevContext->Stats.DrainingRx, pDevContext->Stats.IoFailureCount)
            );
        }
    }
    else
    {
        InterlockedExchange((volatile LONG *)&pDevContext->Stats.IoFailureCount, 0);
        readLength = (ULONG)usbInfo->Parameters.PipeRead.Length;

        // Interlocked: up to NumOfRxReqs completions run concurrently on other CPUs
        // 32-bit on purpose: legacy IOCTL ABI field (wraps at 4 GB, as before)
        InterlockedExchangeAdd((volatile LONG *)&pDevContext->Stats.BytesDrained, (LONG)readLength);
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_PipeDrainCompletion: read %uB/%u\n", pDevContext->PortName, readLength, pDevContext->Stats.BytesDrained)
        );
    }

    // QDSS: commit to the ring now, else the re-arm below hands IoBuffer back to
    // USB with uncommitted data still in it.
    if (pDevContext->FunctionType == QDB_FUNCTION_TYPE_QDSS)
    {
        KIRQL ringLevel;

        KeAcquireSpinLock(&pDevContext->RxLock, &ringLevel);

        QdssRingWriteLocked(pDevContext, ioReq->IoBuffer, readLength);

        if (pDevContext->QdssUrbsInFlight > 0)
        {
            pDevContext->QdssUrbsInFlight--;
        }
        KeReleaseSpinLock(&pDevContext->RxLock, ringLevel);

        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDSSDIAG EP7IN <-- bulk IN completed: %uB (ST 0x%x) InFlight=%u ring=%uB\n",
            pDevContext->PortName, readLength, ntStatus,
            pDevContext->QdssUrbsInFlight, pDevContext->QdssRingUsed)
        );
    }

    // recycle WDF request
    WDF_REQUEST_REUSE_PARAMS_INIT(&reqPrams, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_SUCCESS);
    WdfRequestReuse(ioReq->IoRequest, &reqPrams);

    // Read PipeDrain under the lock: an unsynchronised read can miss a stop and
    // re-arm after teardown began.
    {
        KIRQL   drainLevel;
        BOOLEAN draining;

        KeAcquireSpinLock(&pDevContext->RxLock, &drainLevel);
        draining = pDevContext->PipeDrain;
        KeReleaseSpinLock(&pDevContext->RxLock, drainLevel);

        if ((draining == FALSE) || (pDevContext->DeviceRemoval == TRUE))
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBRD_PipeDrainCompletion: stopping draining [%d]\n", pDevContext->PortName, ioReq->Index)
            );
            sendAgain = FALSE;
        }
    }

    // Release the slot atomically (IoRead/poll timer claim it concurrently), and
    // re-arm before servicing app reads so the endpoint is busy during the copy.
    InterlockedExchange((volatile LONG *)&pDevContext->RxRequest[ioReq->Index].IsPending, 0);
    if (sendAgain == TRUE)
    {
        QDBRD_SendIOBlock(pDevContext, ioReq->Index);
    }
    else if ((pDevContext->FunctionType == QDB_FUNCTION_TYPE_QDSS) && (deviceGone == FALSE))
    {
        // not re-arming here; retry from the timer at passive level
        QdssSchedulePoll(pDevContext);
    }

    // Endpoint is busy again: hand buffered data to any parked reads
    if (pDevContext->FunctionType == QDB_FUNCTION_TYPE_QDSS)
    {
        QdssServicePendingReads(pDevContext);
    }

    // Decrement LAST: at 0 WaitForDrainToStop frees the requests and the ring, so
    // dropping it before the ring work above opens a use-after-free.
    remaining = InterlockedDecrement(&(pDevContext->Stats.DrainingRx));

    // QDSS liveness: nothing outstanding means no completion will restart the
    // drain, so kick the timer instead.
    if ((remaining == 0) &&
        (pDevContext->FunctionType == QDB_FUNCTION_TYPE_QDSS) &&
        (pDevContext->DeviceRemoval == FALSE) &&
        (pDevContext->PipeDrain == TRUE))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDSSDIAG PipeDrainCompletion: WARNING pipeline idle [%d] -- re-arming from timer\n",
            pDevContext->PortName, ioReq->Index)
        );
        QdssSchedulePoll(pDevContext);
    }

    if (remaining == 0)
    {
        // signal the halt of draining
        if (pDevContext->DeviceRemoval == TRUE)
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBRD_PipeDrainCompletion: drain stopped, signaling\n", pDevContext->PortName)
            );
            KeSetEvent(&pDevContext->DrainStoppedEvent, IO_NO_INCREMENT, FALSE);
        }
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBRD_PipeDrainCompletion: [%d] pending %d\n", pDevContext->PortName, ioReq->Index,
        pDevContext->Stats.DrainingRx)
    );

} // QDBRD_PipeDrainCompletion

/****************************************************************************
 *
 * function: QDBRD_ProcessDrainedDPLBlock
 *
 * purpose:  Parses a drained DPL data block by iterating over QMAP-framed
 *           packets and incrementing the PacketsDrained statistics counter.
 *
 * arguments:pDevContext = pointer to the device context
 *           Buffer      = pointer to the received data buffer
 *           Length      = length of the data buffer in bytes
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_ProcessDrainedDPLBlock(PDEVICE_CONTEXT pDevContext, PVOID Buffer, ULONG Length)
{
    PVOID dataPtr, pktPtr;
    ULONG dataLen, pktLen;

    dataPtr = Buffer;
    dataLen = Length;

    pktPtr = QDBRD_RetrievePacket(&dataPtr, &dataLen, &pktLen);

    while (pktPtr != NULL)
    {
        pDevContext->Stats.PacketsDrained += 1;
        pktPtr = QDBRD_RetrievePacket(&dataPtr, &dataLen, &pktLen);
    }

}  // QDBRD_ProcessDrainedDPLBlock

/****************************************************************************
 *
 * function: QDBRD_RetrievePacket
 *
 * purpose:  Parses one QMAP-framed packet from the data buffer, advances
 *           the data pointer, and returns a pointer to the packet payload.
 *
 * arguments:DataPtr      = pointer to the current position in the buffer;
 *                          updated to point past the consumed packet
 *           DataLength   = remaining buffer length in bytes; decremented
 *                          by the consumed packet size
 *           PacketLength = receives the payload length in bytes
 *
 * returns:  pointer to the packet payload, or NULL on error
 *
 ****************************************************************************/
PVOID QDBRD_RetrievePacket(PVOID *DataPtr, PULONG DataLength, PULONG PacketLength)
{
    PQCQMAP_STRUCT qmap;
    UCHAR          padBytes = 0;
    PCHAR          pktPtr, currentDataPtr;

    if (*DataLength <= sizeof(QCQMAP_STRUCT))
    {
        // error: insufficient buffer data for QMAP header

        *PacketLength = 0;
        return NULL;
    }

    currentDataPtr = (PCHAR)(*DataPtr);

    // QMAP header
    qmap = (PQCQMAP_STRUCT)currentDataPtr;

    // get total payload length including padding
    *PacketLength = ntohs(qmap->PacketLen);  // byte swap

    // get the packet
    pktPtr = (PCHAR)((PCHAR)qmap + sizeof(QCQMAP_STRUCT));

    // update DataPtr to the next location
    currentDataPtr += (sizeof(QCQMAP_STRUCT) + (*PacketLength));
    *DataPtr = (PVOID)currentDataPtr;

    // update buffer's DataLength
    if (*DataLength < (sizeof(QCQMAP_STRUCT) + (*PacketLength)))
    {
        // error: insufficient buffer data for indicated payload

        *PacketLength = 0;
        return NULL;
    }
    *DataLength -= (sizeof(QCQMAP_STRUCT) + (*PacketLength));

    // get padding bytes
    padBytes = (0x3F & (qmap->PadCD));

    if (*PacketLength <= padBytes)
    {
        // error: malformed packet

        *PacketLength = 0;
        return NULL;
    }

    // adjust for actual packet length
    *PacketLength -= padBytes;

    return (PVOID)pktPtr;

}  // QDBRD_RetrievePacket
