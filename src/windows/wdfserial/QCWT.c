/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "QCWT.h"
#include "QCUTILS.h"

#ifdef EVENT_TRACING
#include "QCWPP.h"
#include "QCWT.tmh"
#endif

void QCWT_EvtIoWrite
(
    WDFQUEUE   Queue,
    WDFREQUEST Request,
    size_t     Length
)
{
    NTSTATUS         status;
    WDFUSBPIPE       pipeOUT;
    WDFIOTARGET      ioTarget;
    WDFMEMORY        memory;
    PDEVICE_CONTEXT  pDevContext = QCDevGetContext(WdfIoQueueGetDevice(Queue));
    PREQUEST_CONTEXT pReqContext = QCReqGetContext(Request);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_WRITE,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> WIRP: QCWT_EvtIoWrite request: 0x%p, length: %llu bytes\n", pDevContext->PortName, Request, Length)
    );

    if (Length == 0)
    {
        status = STATUS_SUCCESS;
        WdfRequestCompleteWithInformation(Request, status, 0);
    }
    else if (pDevContext->BulkOUT == NULL)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> WIRP: QCWT_EvtIoWrite ERROR no USB pipe for write\n", pDevContext->PortName)
        );
        status = STATUS_INVALID_DEVICE_REQUEST;
        WdfRequestCompleteWithInformation(Request, status, 0);
    }
    else
    {
        pipeOUT = pDevContext->BulkOUT;
        status = WdfRequestRetrieveInputMemory(Request, &memory);
        if (!NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_WRITE,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> WIRP: QCWT_EvtIoWrite retrive input memory FAILED status: 0x%x\n", pDevContext->PortName, status)
            );
            WdfRequestCompleteWithInformation(Request, status, 0);
        }
        else
        {
            // format and send URB
            status = WdfUsbTargetPipeFormatRequestForWrite
            (
                pipeOUT,
                Request,
                memory,
                0
            );
            if (!NT_SUCCESS(status))
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_WRITE,
                    QCSER_DBG_LEVEL_ERROR,
                    ("<%ws> WIRP: QCWT_EvtIoWrite format URB FAILED status: 0x%x\n", pDevContext->PortName, status)
                );
                WdfRequestCompleteWithInformation(Request, status, 0);
            }
            else
            {
                pReqContext->Self = Request;
                ioTarget = WdfUsbTargetPipeGetIoTarget(pipeOUT);
                WdfRequestSetCompletionRoutine(Request, QCWT_EvtIoWriteCompletion, pDevContext);
                WdfSpinLockAcquire(pDevContext->WriteRequestPendingListLock);
                InsertTailList(&pDevContext->WriteRequestPendingList, &pReqContext->Link);
                pDevContext->WriteRequestPendingListLength++;
                pDevContext->WriteRequestPendingListDataSize += Length;
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_WRITE,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> WIRP: QCWT_EvtIoWrite pending list len: %llu, data size: %llu\n", pDevContext->PortName,
                        pDevContext->WriteRequestPendingListLength, pDevContext->WriteRequestPendingListDataSize)
                );
                WdfSpinLockRelease(pDevContext->WriteRequestPendingListLock);

                BOOLEAN sendResult;
                LONGLONG writeTimeoutValue = pDevContext->Timeouts.WriteTotalTimeoutConstant;
                if (pDevContext->Timeouts.WriteTotalTimeoutMultiplier < MAXULONG)
                {
                    writeTimeoutValue += (LONGLONG)pDevContext->Timeouts.WriteTotalTimeoutMultiplier * Length;
                }
                if (writeTimeoutValue > 0)
                {
                    WDF_REQUEST_SEND_OPTIONS writeRequestOptions;
                    WDF_REQUEST_SEND_OPTIONS_INIT(&writeRequestOptions, WDF_REQUEST_SEND_OPTION_TIMEOUT);
                    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&writeRequestOptions, WDF_REL_TIMEOUT_IN_MS(writeTimeoutValue));
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_WRITE,
                        QCSER_DBG_LEVEL_DETAIL,
                        ("<%ws> WIRP: QCWT_EvtIoWrite start write timeout value :%ld ms\n", pDevContext->PortName, writeTimeoutValue)
                    );
                    sendResult = WdfRequestSend(Request, ioTarget, &writeRequestOptions);
                }
                else
                {
                    sendResult = WdfRequestSend(Request, ioTarget, WDF_NO_SEND_OPTIONS);
                }
                if (sendResult == FALSE)    // by default, async
                {
                    status = WdfRequestGetStatus(Request);
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_WRITE,
                        QCSER_DBG_LEVEL_ERROR,
                        ("<%ws> WIRP: QCWT_EvtIoWrite WdfRequestSend FAILED status: 0x%x\n", pDevContext->PortName, status)
                    );
                    WdfSpinLockAcquire(pDevContext->WriteRequestPendingListLock);
                    RemoveEntryList(&pDevContext->WriteRequestPendingList);
                    pDevContext->WriteRequestPendingListLength--;
                    pDevContext->WriteRequestPendingListDataSize -= Length;
                    WdfSpinLockRelease(pDevContext->WriteRequestPendingListLock);
                    WdfRequestCompleteWithInformation(Request, status, 0);  // complete or not?
                }
            }
        }
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> WIRP: QCWT_EvtIoWrite FAILED request: 0x%p, status: 0x%x\n", pDevContext->PortName, Request, status)
        );
    }
}

VOID QCWT_EvtIoWriteCompletion
(
    WDFREQUEST  Request,
    WDFIOTARGET Target,
    PWDF_REQUEST_COMPLETION_PARAMS Params,
    WDFCONTEXT  Context
)
{
    UNREFERENCED_PARAMETER(Target);

    PDEVICE_CONTEXT  pDevContext = (PDEVICE_CONTEXT)Context;
    PREQUEST_CONTEXT pReqContext = QCReqGetContext(Request);
    NTSTATUS         status = Params->IoStatus.Status;
    ULONGLONG        writeLength = Params->Parameters.Usb.Completion->Parameters.PipeWrite.Length;  // the actual transfer length
    ULONGLONG        bufferLength = 0;

    if (pReqContext != NULL)
    {
        if (pReqContext->pDevContext == NULL)   // requests received from the i/o manager
        {
            WDF_REQUEST_PARAMETERS writeParam;
            WDF_REQUEST_PARAMETERS_INIT(&writeParam);
            WdfRequestGetParameters(Request, &writeParam);
            bufferLength = writeParam.Parameters.Write.Length;  // the expected transfer length
        }

        WdfSpinLockAcquire(pDevContext->WriteRequestPendingListLock);
        if (QCUTIL_FindEntryInList(&pDevContext->WriteRequestPendingList, &pReqContext->Link))
        {
            RemoveEntryList(&pReqContext->Link);
            pDevContext->WriteRequestPendingListLength--;
        }
        pDevContext->WriteRequestPendingListDataSize -= bufferLength;

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> WIRP: QCWT_EvtIoWriteCompletion pending list len: %llu, data size: %llu\n", pDevContext->PortName,
                pDevContext->WriteRequestPendingListLength, pDevContext->WriteRequestPendingListDataSize)
        );
        WdfSpinLockRelease(pDevContext->WriteRequestPendingListLock);
    }

    if (NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> WIRP: QCWT_EvtIoWriteCompletion request: 0x%p, len: %llu/%llu, status: 0x%x\n", pDevContext->PortName, Request, writeLength, bufferLength, status)
        );
    }
    else
    {
        // request failed
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> WIRP: QCWT_EvtIoWriteCompletion request FAILED request: 0x%p, len: %llu/%llu, status: 0x%x, usbd status: 0x%x\n",
                pDevContext->PortName, Request, writeLength, bufferLength, status, Params->Parameters.Usb.Completion->UsbdStatus)
        );
    }

    if (WdfRequestGetIoQueue(Request) == NULL)
    {
        // driver created request, the only case is 0-len packet
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> WIRP: QCWT_EvtIoWriteCompletion finish processing 0-len packet\n", pDevContext->PortName)
        );
        WdfObjectDelete(Request);
        WdfDeviceResumeIdle(pDevContext->Device);
    }
    else
    {
        if (NT_SUCCESS(status) && writeLength > 0)
        {
            // print TX data here
            PVOID txBuffer = NULL;
            status = WdfRequestRetrieveInputBuffer(Request, (size_t)writeLength, &txBuffer, NULL);
            if (NT_SUCCESS(status) && txBuffer != NULL)
            {
                QCUTIL_PrintBytes(txBuffer, 128, (ULONG)writeLength, "TxData", QCSER_DBG_MASK_TDATA, QCSER_DBG_LEVEL_TRACE, pDevContext);
            }
        }
        WdfRequestCompleteWithInformation(Request, status, (ULONG_PTR)writeLength);
    }
}

void QCWT_WriteRequestHandlerThread
(
    PVOID pContext
)
{
    NTSTATUS         status = STATUS_SUCCESS;
    WDFREQUEST       request;
    PDEVICE_CONTEXT  pDevContext = pContext;
    PKWAIT_BLOCK     pWaitBlock = ExAllocatePoolUninitialized(NonPagedPoolNx, (WRITE_THREAD_RESUME_EVENT_COUNT) * sizeof(KWAIT_BLOCK), '5gaT');
    BOOLEAN          bRunning = TRUE;
    WDF_REQUEST_PARAMETERS writeParam;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_WRITE,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> WIRP: QCWT_WriteRequestHandlerThread\n", pDevContext->PortName)
    );

    if (pWaitBlock == NULL)
    {
        WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
        bRunning = FALSE;
    }
    else
    {
        KeSetEvent(&pDevContext->WriteThreadStartedEvent, IO_NO_INCREMENT, FALSE);
    }

    while (bRunning)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> WIRP: QCWT_WriteRequestHandlerThread wait for event...\n", pDevContext->PortName)
        );
        status = KeWaitForMultipleObjects
        (
            WRITE_THREAD_RESUME_EVENT_COUNT,
            pDevContext->WriteThreadResumeEvents,
            WaitAny,
            Executive,
            KernelMode,
            FALSE,
            NULL,
            pWaitBlock
        );
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> WIRP: QCWT_WriteRequestHandlerThread wakeup from event... status: 0x%x\n", pDevContext->PortName, status)
        );
        switch (status)
        {
            case WRITE_THREAD_FILE_CLOSE_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_WRITE,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> WIRP: QCWT_WriteRequestHandlerThread WRITE_THREAD_FILE_CLOSE_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->FileCloseEventWrite);
                QCWT_CleanupWriteQueue(pDevContext);   // this could return before all pending requests are cancelled
                KeSetEvent(&pDevContext->WriteThreadFileCloseReadyEvent, IO_NO_INCREMENT, FALSE);
                break;
            }
            case WRITE_THREAD_PURGE_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_WRITE,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> WIRP: QCWT_WriteRequestHandlerThread WRITE_THREAD_PURGE_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->WriteThreadPurgeEvent);
                QCWT_CleanupWriteQueue(pDevContext);
                KeSetEvent(&pDevContext->WritePurgeCompletionEvent, IO_NO_INCREMENT, FALSE);
                break;
            }
            case WRITE_THREAD_REQUEST_ARRIVE_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_WRITE,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> WIRP: QCWT_WriteRequestHandlerThread WRITE_THREAD_REQUEST_ARRIVE_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->WriteRequestArriveEvent);
                while (NT_SUCCESS(status = WdfIoQueueRetrieveNextRequest(pDevContext->WriteQueue, &request)))
                {
                    if (request != NULL)
                    {
                        WDF_REQUEST_PARAMETERS_INIT(&writeParam);
                        WdfRequestGetParameters(request, &writeParam);
                        QCWT_EvtIoWrite(pDevContext->WriteQueue, request, writeParam.Parameters.Write.Length);
                        if (gVendorConfig.EnableZeroLengthPacket && (writeParam.Parameters.Write.Length % pDevContext->wMaxPktSize == 0))
                        {
    #ifdef QCUSB_MUX_PROTOCOL
                            if (pDevContext->DeviceFunction != QCUSB_DEV_FUNC_LPC)
    #endif
                            {
                                // 0-length packet needed
                                QCSER_DbgPrint
                                (
                                    QCSER_DBG_MASK_WRITE,
                                    QCSER_DBG_LEVEL_TRACE,
                                    ("<%ws> WIRP: QCWT_WriteRequestHandlerThread 0-length packet NEEDED transfer size: %llu, max packet size: %lu\n",
                                        pDevContext->PortName, writeParam.Parameters.Write.Length, pDevContext->wMaxPktSize)
                                );
                                status = WdfDeviceStopIdle(pDevContext->Device, TRUE);
                                if (status == STATUS_SUCCESS || status == STATUS_PENDING)
                                {
                                    QCSER_DbgPrint
                                    (
                                        QCSER_DBG_MASK_WRITE,
                                        QCSER_DBG_LEVEL_TRACE,
                                        ("<%ws> WIRP: QCWT_WriteRequestHandlerThread device suspend is temporarily disabled\n", pDevContext->PortName)
                                    );
                                    status = QCWT_SendUsbShortPacket(pDevContext);
                                    if (!NT_SUCCESS(status))
                                    {
                                        WdfDeviceResumeIdle(pDevContext->Device);
                                    }
                                }
                            }
                        }
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_WRITE,
                            QCSER_DBG_LEVEL_TRACE,
                            ("<%ws> WIRP: QCWT_WriteRequestHandlerThread retrieved and processed request: 0x%p\n", pDevContext->PortName, request)
                        );
                    }
                }
                break;
            }
            case WRITE_THREAD_DEVICE_REMOVE_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_WRITE,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> WIRP: QCWT_WriteRequestHandlerThread WRITE_THREAD_DEVICE_REMOVE_EVENT triggered\n", pDevContext->PortName)
                );
                bRunning = FALSE;
                break;
            }
            default:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_WRITE,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> WIRP: QCWT_WriteRequestHandlerThread wait for resume event status: 0x%x\n", pDevContext->PortName, status)
                );
                break;
            }
        }
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_WRITE,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> WIRP: QCWT_WriteRequestHandlerThread exited status: 0x%x\n", pDevContext->PortName, status)
    );

    WdfIoQueuePurgeSynchronously(pDevContext->WriteQueue);
    if (pWaitBlock != NULL)
    {
        ExFreePoolWithTag(pWaitBlock, '5gaT');
        pWaitBlock = NULL;
    }
    PsTerminateSystemThread(status);
}

VOID QCWT_CleanupWriteQueue
(
    PDEVICE_CONTEXT pDevContext
)
{
    PREQUEST_CONTEXT pReqContext;
    PLIST_ENTRY head = &pDevContext->WriteRequestPendingList;
    PLIST_ENTRY peek;
    WDFREQUEST  request;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_WRITE,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> WIRP: QCWT_CleanupWriteQueue\n", pDevContext->PortName)
    );

    WdfSpinLockAcquire(pDevContext->WriteRequestPendingListLock);
    while (!IsListEmpty(head))
    {
        peek = RemoveHeadList(head);
        pDevContext->WriteRequestPendingListLength--;
        pReqContext = CONTAINING_RECORD(peek, REQUEST_CONTEXT, Link);
        request = pReqContext->Self;
        WdfObjectReference(request);
        WdfSpinLockRelease(pDevContext->WriteRequestPendingListLock);
        if (WdfRequestCancelSentRequest(request))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_WRITE,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> WIRP: QCWT_CleanupWriteQueue successfully cancelled pending request 0x%p\n", pDevContext->PortName, request)
            );
        }
        WdfObjectDereference(request);
        WdfSpinLockAcquire(pDevContext->WriteRequestPendingListLock);
    }
    WdfSpinLockRelease(pDevContext->WriteRequestPendingListLock);
}

NTSTATUS QCWT_SendUsbShortPacket
(
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS    status;
    WDFREQUEST  shortRequest;
    WDFIOTARGET ioTarget = WdfUsbTargetPipeGetIoTarget(pDevContext->BulkOUT);
    WDF_OBJECT_ATTRIBUTES shortRequestAttr;
    PREQUEST_CONTEXT pReqContext;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&shortRequestAttr, REQUEST_CONTEXT);
    shortRequestAttr.ParentObject = pDevContext->UsbDevice;
    status = WdfRequestCreate(&shortRequestAttr, ioTarget, &shortRequest);

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> WIRP: QCWT_SendUsbShortPacket short packet request create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }
    else
    {
        status = WdfUsbTargetPipeFormatRequestForWrite
        (
            pDevContext->BulkOUT,
            shortRequest,
            NULL,
            0
        );
        if (!NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_WRITE,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> WIRP: QCWT_SendUsbShortPacket short packet WdfUsbTargetPipeFormatRequestForWrite FAILED status: 0x%x\n",
                    pDevContext->PortName, status)
            );
            WdfObjectDelete(shortRequest);
        }
        else
        {
            pReqContext = QCReqGetContext(shortRequest);
            pReqContext->Self = shortRequest;
            pReqContext->pDevContext = pDevContext;
            WdfRequestSetCompletionRoutine(shortRequest, QCWT_EvtIoWriteCompletion, pDevContext);
            WdfSpinLockAcquire(pDevContext->WriteRequestPendingListLock);
            InsertTailList(&pDevContext->WriteRequestPendingList, &pReqContext->Link);
            pDevContext->WriteRequestPendingListLength++;
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_WRITE,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> WIRP: QCWT_SendUsbShortPacket WriteRequestPendingListLength: %llu\n", pDevContext->PortName, pDevContext->WriteRequestPendingListLength)
            );
            WdfSpinLockRelease(pDevContext->WriteRequestPendingListLock);

            if (WdfRequestSend(shortRequest, ioTarget, WDF_NO_SEND_OPTIONS) == FALSE)
            {
                status = WdfRequestGetStatus(shortRequest);
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_WRITE,
                    QCSER_DBG_LEVEL_ERROR,
                    ("<%ws> WIRP: QCWT_SendUsbShortPacket short packet sent FAILED status: 0x%x\n", pDevContext->PortName, status)
                );
                WdfSpinLockAcquire(pDevContext->WriteRequestPendingListLock);
                RemoveEntryList(&pReqContext->Link);
                pDevContext->WriteRequestPendingListLength--;
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_WRITE,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> WIRP: QCWT_SendUsbShortPacket WriteRequestPendingListLength: %llu\n", pDevContext->PortName, pDevContext->WriteRequestPendingListLength)
                );
                WdfSpinLockRelease(pDevContext->WriteRequestPendingListLock);
                WdfObjectDelete(shortRequest);
            }
            else
            {
                status = STATUS_SUCCESS;
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_WRITE,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> WIRP: QCWT_SendUsbShortPacket short packet send successfully\n", pDevContext->PortName)
                );
            }
        }
    }

    return status;
}