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
#include "QDBWPP.h"
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

    if ((fileContext->Type == QDB_FILE_TYPE_TRACE) || (fileContext->Type == QDB_FILE_TYPE_DPL))
    {
#ifdef QDB_DATA_EMULATION

        QDBRD_ReturnEmulation(Queue, Request, Length);
        return;

#else // QDB_DATA_EMULATION

        if (pDevContext->TraceIN == NULL)
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
        if ((pDevContext->DebugIN == NULL) || (pDevContext->DebugOUT == NULL))
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
    WDFMEMORY        hRxBufferObj;
    WDFUSBPIPE       pipeIN;
    BOOLEAN          bResult;

    pDevContext = QdbDeviceGetContext(WdfIoQueueGetDevice(Queue));

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBRD_ReadUSB: 0x%p (%dB)\n", pDevContext->PortName, Request, Length)
    );

    fileContext = QdbFileGetContext(WdfRequestGetFileObject(Request));
    if ((fileContext->Type == QDB_FILE_TYPE_TRACE) || (fileContext->Type == QDB_FILE_TYPE_DPL))
    {
        pipeIN = pDevContext->TraceIN;
    }
    else if (fileContext->Type == QDB_FILE_TYPE_DEBUG)
    {
        pipeIN = pDevContext->DebugIN;
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
        ("<%s> QDBRD_ReadUSB: Sending req 0x%p to 0x%p\n", pDevContext->PortName, Request, pDevContext->MyIoTarget)
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
        ("<%s> <--QDBRD_ReadUSB: 0x%p (%dB)\n", pDevContext->PortName, Request, Length)
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
    WDFUSBPIPE       pipeIN;
    ULONG            readLength = 0;

    pDevContext = (PDEVICE_CONTEXT)Context;

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBRD_ReadUSBCompletion:0x%p\n", pDevContext->PortName, Request)
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
        Request, readLength, QDB_USB_TRANSFER_SIZE_MAX, pDevContext->Stats.OutstandingRx)
    );

    return;

}  // QDBRD_ReadUSBCompletion

/* --------------------------------------------------------------------
 *               PIPE DRAINING FUNCTIONS
 * --------------------------------------------------------------------*/

/****************************************************************************
 *
 * function: QDBRD_ConfigureContinuousReader
 *
 * purpose:  Configures the WDF USB continuous reader on the QDSS TraceIN
 *           pipe. The reader keeps the pipe perpetually consumed while no
 *           user application is reading. Only applicable for QDSS devices.
 *
 * arguments:pDevContext = pointer to the device context
 *
 * returns:  STATUS_NOT_SUPPORTED if not a device type is invalid,
 *           STATUS_UNSUCCESSFUL if TraceIN pipe is not available
 *
 ****************************************************************************/
NTSTATUS QDBRD_ConfigureContinuousReader(PDEVICE_CONTEXT pDevContext)
{
    WDF_USB_CONTINUOUS_READER_CONFIG readerConfig;
    NTSTATUS ntStatus;

    QDB_DbgPrintG
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> -->QDBRD_ConfigureContinuousReader\n", pDevContext->PortName)
    );

    if (pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS)
    {
        QDB_DbgPrintG
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> <--QDBRD_ConfigureContinuousReader: not QDSS\n", pDevContext->PortName)
        );
        return STATUS_NOT_SUPPORTED;
    }

    if (pDevContext->TraceIN == NULL)
    {
        QDB_DbgPrintG
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBRD_ConfigureContinuousReader: TraceIN not available\n", pDevContext->PortName)
        );
        return STATUS_UNSUCCESSFUL;
    }

    WDF_USB_CONTINUOUS_READER_CONFIG_INIT
    (
        &readerConfig,
        QDBRD_DrainReadComplete,
        pDevContext,
        QDB_MAX_IO_SIZE_RX
    );
    readerConfig.NumPendingReads = IO_REQ_NUM_RX;
    readerConfig.EvtUsbTargetPipeReadersFailed = QDBRD_DrainReadFailed;

    ntStatus = WdfUsbTargetPipeConfigContinuousReader(pDevContext->TraceIN, &readerConfig);

    QDB_DbgPrintG
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> <--QDBRD_ConfigureContinuousReader: ST 0x%x\n", pDevContext->PortName, ntStatus)
    );

    return ntStatus;
}

/****************************************************************************
 *
 * function: QDBRD_DrainReadComplete
 *
 * purpose:  WDF continuous reader completion callback for draining. Data is
 *           intentionally discarded to keep the pipe flowing.
 *
 * arguments:Pipe                = WDF USB pipe handle
 *           Buffer              = WDF memory object containing received data
 *           NumBytesTransferred = number of bytes received
 *           Context             = pointer to the device context
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_DrainReadComplete
(
    WDFUSBPIPE  Pipe,
    WDFMEMORY   Buffer,
    size_t      NumBytesTransferred,
    WDFCONTEXT  Context
)
{
    PDEVICE_CONTEXT pDevContext = (PDEVICE_CONTEXT)Context;

    UNREFERENCED_PARAMETER(Pipe);
    UNREFERENCED_PARAMETER(Buffer);

    // Discard received data — update stats only
    pDevContext->Stats.BytesDrained += (ULONG)NumBytesTransferred;

    QDB_DbgPrintG
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_ERROR,
        ("<%s> QDBRD_DrainReadComplete: %uB drained (total %u)\n",
        pDevContext->PortName, (ULONG)NumBytesTransferred, pDevContext->Stats.BytesDrained)
    );
}

/****************************************************************************
 *
 * function: QDBRD_DrainReadFailed
 *
 * purpose:  WDF continuous reader failure callback. Called when the
 *           framework encounters a USB error on the QDSS TraceIN pipe
 *           while draining. Returning FALSE stops the reader.
 *
 * arguments:Pipe       = WDF USB pipe handle
 *           Status     = NT status of the failed transfer
 *           UsbdStatus = USBD status code of the failed transfer
 *
 * returns:  FALSE to stop the continuous reader on error
 *
 ****************************************************************************/
BOOLEAN QDBRD_DrainReadFailed
(
    WDFUSBPIPE   Pipe,
    NTSTATUS     Status,
    USBD_STATUS  UsbdStatus
)
{
    PDEVICE_CONTEXT pDevContext;

    pDevContext = QdbDeviceGetContext(WdfIoTargetGetDevice(WdfUsbTargetPipeGetIoTarget(Pipe)));

    QDB_DbgPrintG
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_ERROR,
        ("<%s> QDBRD_DrainReadFailed: ST 0x%x, USBD ST 0x%x — stopping continuous reader\n",
        pDevContext->PortName, Status, UsbdStatus)
    );

    return FALSE;
}

/****************************************************************************
 *
 * function: QDBRD_StartDraining
 *
 * purpose:  Starts data draining on the QDSS TraceIN pipe.
 *
 * arguments:pDevContext = pointer to the device context
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_StartDraining(PDEVICE_CONTEXT pDevContext)
{
    NTSTATUS nts;

    if (pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS ||
        pDevContext->TraceIN == NULL)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> QDBRD_StartDraining: skip (not QDSS or no TraceIN)\n", pDevContext->PortName)
        );
    }
    else
    {
        nts = WdfIoTargetStart(WdfUsbTargetPipeGetIoTarget(pDevContext->TraceIN));
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> QDBRD_StartDraining: ST 0x%x\n", pDevContext->PortName, nts)
        );
    }
}

/****************************************************************************
 *
 * function: QDBRD_StopDraining
 *
 * purpose:  Stops data draining on the QDSS TraceIN pipe.
 *
 * arguments:pDevContext = pointer to the device context
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBRD_StopDraining(PDEVICE_CONTEXT pDevContext)
{
    if (pDevContext->FunctionType != QDB_FUNCTION_TYPE_QDSS ||
        pDevContext->TraceIN == NULL)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> QDBRD_StopDraining: skip (not QDSS or no TraceIN)\n", pDevContext->PortName)
        );
    }
    else
    {
        WdfIoTargetStop
        (
            WdfUsbTargetPipeGetIoTarget(pDevContext->TraceIN),
            WdfIoTargetCancelSentIo
        );
        QDB_DbgPrint
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> QDBRD_StopDraining: done\n", pDevContext->PortName)
        );
    }
}
