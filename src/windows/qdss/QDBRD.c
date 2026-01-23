/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B R D . C

GENERAL DESCRIPTION
    This is the file which contains RX functions for QDSS driver.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "QDBMAIN.h"
#include "QDBRD.h"

#ifdef EVENT_TRACING
#include "QDBRD.tmh"
#endif

#ifdef QDB_DATA_EMULATION

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

    // Stop draining
    QDBRD_PipeDrainStop(pDevContext);

    fileContext = QdbFileGetContext(WdfRequestGetFileObject(Request));

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

    QDBRD_ReadUSB(Queue, Request, (ULONG)Length);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBRD_IoRead: 0x%p (%dB)\n", pDevContext->PortName, Request, Length)
    );
    return;
}  // QDBRD_IoRead

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

    KeInitializeSpinLock(&pDevContext->RxLock);

    if (pDevContext->FunctionType != QDB_FUNCTION_TYPE_DPL)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBRD_AllocateRequestsRx: not DPL\n", pDevContext->PortName)
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
            QDB_MAX_IO_SIZE_RX,
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

VOID QDBRD_FreeIoBuffer(PDEVICE_CONTEXT pDevContext)
{
    ULONG i;
    PQDB_IO_REQUEST ioReq = pDevContext->RxRequest;
    ULONG numElements = pDevContext->NumOfRxReqs;

    if (pDevContext->FunctionType != QDB_FUNCTION_TYPE_DPL)
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

// To drain pipe if there's no user buffer
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

    if (pDevContext->FunctionType != QDB_FUNCTION_TYPE_DPL)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBRD_PipeDrainStart: not DPL\n", pDevContext->PortName)
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

VOID QDBRD_SendIOBlock(PDEVICE_CONTEXT pDevContext, INT Index)
{
    NTSTATUS   ntStatus;
    BOOLEAN    bResult;
    WDFUSBPIPE pipeIN = pDevContext->TraceIN;

    if (pDevContext->FunctionType != QDB_FUNCTION_TYPE_DPL)
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

    if (pDevContext->RxRequest[Index].IsPending != 0)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> <--QDBRD_SendIOBlock[%d]: still pending\n", pDevContext->PortName, Index)
        );
        return;
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
        return;
    }

    WdfRequestSetCompletionRoutine
    (
        pDevContext->RxRequest[Index].IoRequest,
        QDBRD_PipeDrainCompletion,
        &(pDevContext->RxRequest[Index])
    );

    pDevContext->RxRequest[Index].IsPending = 1;
    InterlockedIncrement(&(pDevContext->Stats.DrainingRx));
    bResult = WdfRequestSend
    (
        pDevContext->RxRequest[Index].IoRequest,
        pDevContext->MyIoTarget,
        WDF_NO_SEND_OPTIONS
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

        // In this case, completion routine will not be called, so need to rewind
        InterlockedDecrement(&(pDevContext->Stats.DrainingRx));
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBRD_SendIOBlock: [%d]\n", pDevContext->PortName, Index)
    );
} // QDBRD_SendIOBlock

NTSTATUS QDBRD_PipeDrainStop(PDEVICE_CONTEXT pDevContext)
{
    // ULONG i;
    KIRQL oldLevel;

    if (pDevContext->FunctionType != QDB_FUNCTION_TYPE_DPL)
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

        if ((ntStatus != STATUS_CANCELLED) && (ntStatus != STATUS_UNSUCCESSFUL))
        {
            sendAgain = FALSE;
        }
        else if (++pDevContext->Stats.IoFailureCount > pDevContext->IoFailureThreshold)
        {
            sendAgain = FALSE;
        }
    }
    else
    {
        pDevContext->Stats.IoFailureCount = 0;
        readLength = (ULONG)usbInfo->Parameters.PipeRead.Length;
        pDevContext->Stats.BytesDrained += readLength;
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_PipeDrainCompletion: read %uB/%u\n", pDevContext->PortName, readLength, pDevContext->Stats.BytesDrained)
        );
    }

    // recycle WDF request
    WDF_REQUEST_REUSE_PARAMS_INIT(&reqPrams, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_SUCCESS);
    WdfRequestReuse(ioReq->IoRequest, &reqPrams);

    if ((pDevContext->PipeDrain == FALSE) || (pDevContext->DeviceRemoval == TRUE))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBRD_PipeDrainCompletion: stopping draining [%d]\n", pDevContext->PortName, ioReq->Index)
        );
        sendAgain = FALSE;
    }

    pDevContext->RxRequest[ioReq->Index].IsPending = 0;
    if (sendAgain == TRUE)
    {
        // re-send request
        QDBRD_SendIOBlock(pDevContext, ioReq->Index);
    }

    if (InterlockedDecrement(&(pDevContext->Stats.DrainingRx)) == 0)
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
