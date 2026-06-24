/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B W T . C

GENERAL DESCRIPTION
    This is the file which contains TX functions for QDSS driver.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "QDBMAIN.h"
#include "QDBWT.h"

#ifdef EVENT_TRACING
#include "QDBWT.tmh"
#endif

/****************************************************************************
 *
 * function: QDBWT_IoWrite
 *
 * purpose:  WDF write dispatch callback. Validates the file context and
 *           pipe availability, then constructs and sends the request.
 *
 * arguments:Queue   = WDF I/O queue handle
 *           Request = WDF write request handle
 *           Length  = requested write length in bytes
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBWT_IoWrite
(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    size_t Length
)
{
    PDEVICE_CONTEXT pDevContext = QdbDeviceGetContext(WdfIoQueueGetDevice(Queue));
    PFILE_CONTEXT   fileContext = QdbFileGetContext(WdfRequestGetFileObject(Request));
    NTSTATUS        ntStatus;
    WDFMEMORY       inputMemory;
    WDFUSBPIPE      pipeOUT;
    WDFIOTARGET     ioTarget;

    QDB_DbgPrint
    (
        QDB_DBG_MASK_WRITE,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBWT_IoWrite: request 0x%p\n", pDevContext->PortName, Request)
    );

    switch (fileContext->Type)
    {
        case QDB_FILE_TYPE_TRACE:
        case QDB_FILE_TYPE_DPL:
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_WRITE,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBWT_IoWrite: no USB pipe for TRACE/DPL write\n", pDevContext->PortName)
            );
            WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
            return;
        }
        case QDB_FILE_TYPE_DEBUG:
        {
            if (fileContext->DebugOUT == NULL)
            {
                QDB_DbgPrint
                (
                    QDB_DBG_MASK_WRITE,
                    QDB_DBG_LEVEL_ERROR,
                    ("<%s> QDBWT_IoWrite: write pipes not available\n", pDevContext->PortName)
                );
                WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
                return;
            }
            break;
        }
        default:
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_WRITE,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBWT_IoWrite: unknown file type\n", pDevContext->PortName)
            );
            WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
            return;
        }
    }

    if (Length == 0)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_WRITE,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> QDBWT_IoWrite: skip length 0 TX (0x%p)\n", pDevContext->PortName, Request)
        );
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
        return;
    }

    if (Length > (size_t)QDB_USB_TRANSFER_SIZE_MAX)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_WRITE,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBWT_IoWrite: discard oversized TX (0x%p) %Iu bytes\n", pDevContext->PortName, Request, Length)
        );
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return;
    }

    // retrieve tx transfer
    ntStatus = WdfRequestRetrieveInputMemory(Request, &inputMemory);
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_WRITE,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBWT_IoWrite: failure retrieving TX buffer (0x%x)\n", pDevContext->PortName, ntStatus)
        );
        WdfRequestCompleteWithInformation(Request, ntStatus, 0);
        return;
    }

    // format URB and set URB flag to USBD_TRANSFER_DIRECTION_OUT
    pipeOUT = fileContext->DebugOUT;
    ntStatus = WdfUsbTargetPipeFormatRequestForWrite(pipeOUT, Request, inputMemory, NULL);
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_WRITE,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBWT_IoWrite: failed to format URB (0x%x)\n", pDevContext->PortName, ntStatus)
        );
        WdfRequestCompleteWithInformation(Request, ntStatus, 0);
        return;
    }

    // set completion routine and send out request
    WdfRequestSetCompletionRoutine(Request, QDBWT_WriteUSBCompletion, pDevContext);
    ioTarget = WdfUsbTargetPipeGetIoTarget(pipeOUT);
    if (WdfRequestSend(Request, ioTarget, WDF_NO_SEND_OPTIONS) == FALSE)
    {
        ntStatus = WdfRequestGetStatus(Request);
        WdfRequestCompleteWithInformation(Request, ntStatus, 0);
        QDB_DbgPrint
        (
            QDB_DBG_MASK_WRITE,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBWT_IoWrite: WdfRequestSend failed (0x%x)\n", pDevContext->PortName, ntStatus)
        );
    }
    else
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_WRITE,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> <--QDBWT_IoWrite: request sent 0x%p\n", pDevContext->PortName, Request)
        );
    }
}  // QDBWT_IoWrite

/****************************************************************************
 *
 * function: QDBWT_WriteUSBCompletion
 *
 * purpose:  WDF completion routine for USB write requests. Extracts the
 *           number of bytes written and completes the original request.
 *
 * arguments:Request          = WDF request handle
 *           Target           = WDF USB pipe I/O target
 *           CompletionParams = USB completion parameters including byte count
 *           Context          = pointer to the device context
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBWT_WriteUSBCompletion
(
    WDFREQUEST Request,
    WDFIOTARGET Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    WDFCONTEXT Context
)
{
    NTSTATUS         ntStatus;
    PDEVICE_CONTEXT  pDevContext = (PDEVICE_CONTEXT)Context;
    size_t           writeLength = 0;

    UNREFERENCED_PARAMETER(Target);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_WRITE,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBWT_WriteUSBCompletion: 0x%p)\n", pDevContext->PortName, Request)
    );

    ntStatus = CompletionParams->IoStatus.Status;
    if (!NT_SUCCESS(ntStatus))
    {
        // TODO: need to reset pipe at PASSIVE_LEVEL?
        QDB_DbgPrint
        (
            QDB_DBG_MASK_WRITE,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBWT_WriteUSBCompletion: error completion 0x%p(0x%x)\n", pDevContext->PortName,
            Request, ntStatus)
        );
    }
    else
    {
        writeLength = CompletionParams->Parameters.Usb.Completion->Parameters.PipeWrite.Length;
        WdfRequestSetInformation(Request, (ULONG_PTR)writeLength);
    }

    WdfRequestComplete(Request, ntStatus);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_WRITE,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBWT_WriteUSBCompletion: 0x%p (%IuB/%luB)\n", pDevContext->PortName,
        Request, writeLength, pDevContext->MaxXfrSize)
    );
}  // QDBWT_WriteUSBCompletion
