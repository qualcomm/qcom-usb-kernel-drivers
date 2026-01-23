/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "QCRD.h"
#include "QCUTILS.h"
#include "QCSER.h"

#ifdef EVENT_TRACING
#include "QCWPP.h"
#include "QCRD.tmh"
#endif

VOID QCRD_ScanForWaitMask
(
    PDEVICE_CONTEXT pDevContext,
    UCHAR ucEventChar,
    ULONG ulWaitMask
)
{
    USHORT usNewUartState = 0;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCRD_ScanForWaitMask event char: 0x%x, waitMask: 0x%x\n", pDevContext->PortName, ucEventChar, ulWaitMask)
    );

    if (QCUTIL_RingBufferBytesUsed(&pDevContext->ReadRingBuffer) > 0)
    {
        // If event: signal when any char has been received
        if ((ulWaitMask & SERIAL_EV_RXCHAR) != 0)
        {
            usNewUartState |= SERIAL_EV_RXCHAR;
        }

        // If event: signal when particular char has been received
        if (((ulWaitMask & SERIAL_EV_RXFLAG) != 0) && (ucEventChar != 0))
        {
            CHAR output;
            for (size_t offset = 0; QCUTIL_RingBufferReadByte(&pDevContext->ReadRingBuffer, &output, offset); offset++)
            {
                if (output == ucEventChar)
                {
                    usNewUartState |= SERIAL_EV_RXFLAG;
                    break;
                }
            }
        }

        if (usNewUartState & SERIAL_EV_RXCHAR || usNewUartState & SERIAL_EV_RXFLAG)
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_TRACE,
                ("<%ws> QCRD_ScanForWaitMask new uart state: 0x%x, event char: 0x%x, wait mask: 0x%x\n",
                pDevContext->PortName, usNewUartState, ucEventChar, ulWaitMask)
            );
            QCSER_ProcessNewUartState(pDevContext, usNewUartState, usNewUartState);
        }
        else
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_TRACE,
                ("<%ws> QCRD_ScanForWaitMask uart state unchanged\n", pDevContext->PortName)
            );
        }
    }
    else
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCRD_ScanForWaitMask skip scanning, rx ring buffer is empty\n", pDevContext->PortName)
        );
    }
}

void QCRD_ReadRequestHandlerThread
(
    PVOID pContext
)
{
    NTSTATUS         status = STATUS_SUCCESS;
    WDFREQUEST       request, pendingTimeoutRequest = NULL;
    PREQUEST_CONTEXT pReqContext;
    PDEVICE_CONTEXT  pDevContext = pContext;
    ULONG            devErrCnt = 0;
    WDFIOTARGET      ioTarget = WdfUsbTargetPipeGetIoTarget(pDevContext->BulkIN);
    PKWAIT_BLOCK     pWaitBlock = ExAllocatePoolUninitialized(NonPagedPoolNx, (READ_THREAD_RESUME_EVENT_COUNT) * sizeof(KWAIT_BLOCK), '3gaT');
    BOOLEAN          bRunning = TRUE;
    BOOLEAN          bDeviceOpened = FALSE;
    BOOLEAN          bDeviceAwaken = FALSE;
    BOOLEAN          bBufferOverflow = FALSE;

#ifdef QCUSB_MUX_PROTOCOL
    size_t           readOffset = 0;
    size_t           tempSessionTotal = 0;
#endif

    PLIST_ENTRY head;
    PLIST_ENTRY peek;
    PREAD_BUFFER_PARAM pBufferParam;        // for read urbs
    WDF_REQUEST_PARAMETERS requestParam;    // for application requests
    PRING_BUFFER rxBuffer = &pDevContext->ReadRingBuffer;

    if (pWaitBlock == NULL)
    {
        WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
        bRunning = FALSE;
    }
    else
    {
        KeClearEvent(&pDevContext->ReadThreadD0EntryReadyEvent);
        KeClearEvent(&pDevContext->ReadThreadD0ExitReadyEvent);
        KeSetEvent(&pDevContext->ReadThreadStartedEvent, IO_NO_INCREMENT, FALSE);
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread rx buffer capacity: %llu\n", pDevContext->PortName, QCUTIL_RingBufferBytesFree(rxBuffer))
    );

    while (bRunning)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread wait for event...\n", pDevContext->PortName)
        );
        status = KeWaitForMultipleObjects
        (
            READ_THREAD_RESUME_EVENT_COUNT,
            pDevContext->ReadThreadResumeEvents,
            WaitAny,
            Executive,
            KernelMode,
            FALSE,
            NULL,
            pWaitBlock
        );
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread wakeup from event... status: 0x%x\n", pDevContext->PortName, status)
        );
        switch (status)
        {
            case READ_THREAD_FILE_CLOSE_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_FILE_CLOSE_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->FileCloseEventRead);
                bDeviceOpened = FALSE;
                // reset urbs and put them into free list
                KeCancelTimer(&pDevContext->ReadTimer);
                WdfIoQueuePurgeSynchronously(pDevContext->TimeoutReadQueue);
                WdfIoTargetStop(ioTarget, WdfIoTargetCancelSentIo);
                QCRD_ClearBuffer(pDevContext);
                KeSetEvent(&pDevContext->ReadThreadFileCloseReadyEvent, IO_NO_INCREMENT, FALSE);
                break;
            }
            case READ_THREAD_FILE_CREATE_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_FILE_CREATE_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->FileCreateEvent);
                bDeviceOpened = TRUE;
                WdfIoTargetStart(ioTarget);
                WdfIoQueueStart(pDevContext->TimeoutReadQueue);

                // send read urbs to device
#ifdef QCUSB_MUX_PROTOCOL
                if (pDevContext->DeviceFunction != QCUSB_DEV_FUNC_LPC && pDevContext->DeviceFunction != QCUSB_DEV_FUNC_VI)
#endif
                {
                    size_t sentCount = QCRD_SendReadUrbs(pDevContext, ioTarget);
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_READ,
                        QCSER_DBG_LEVEL_TRACE,
                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread finish sending pre-read urbs count: %llu\n", pDevContext->PortName, sentCount)
                    );
                }
                break;
            }
            case READ_THREAD_CLEAR_BUFFER_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_CLEAR_BUFFER_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->ReadThreadClearBufferEvent);
                QCRD_ClearBuffer(pDevContext);
                QCRD_SendReadUrbs(pDevContext, ioTarget);
                KeSetEvent(&pDevContext->ReadPurgeCompletionEvent, IO_NO_INCREMENT, FALSE);
                break;
            }
            case READ_THREAD_SESSION_TOTAL_SET_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_SESSION_TOTAL_SET_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->SessionTotalSetEvent);
#ifdef QCUSB_MUX_PROTOCOL
                if (pDevContext->DeviceFunction == QCUSB_DEV_FUNC_LPC)
                {
                    QCRD_CleanupReadQueues(pDevContext);
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_READ,
                        QCSER_DBG_LEVEL_TRACE,
                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread LPC device session total read value: %llu\n",
                        pDevContext->PortName, pDevContext->QcStats.SessionTotal)
                    );
                }
                else if (pDevContext->DeviceFunction == QCUSB_DEV_FUNC_VI)
                {
                    // cleanup outdated urbs
                    if (pDevContext->PendingReadRequest != NULL)
                    {
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_TRACE,
                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread clean up outdated urbs\n", pDevContext->PortName)
                        );
                        QCRD_CleanupReadQueues(pDevContext);
                        WdfRequestCompleteWithInformation(pDevContext->PendingReadRequest, STATUS_CANCELLED, 0);
                    }
                    status = WdfIoQueueRetrieveNextRequest(pDevContext->ReadQueue, &request);
                    if (NT_SUCCESS(status) && request != NULL)
                    {
                        // set sessionTotal and save pending request
                        pDevContext->PendingReadRequest = request;
                        WDF_REQUEST_PARAMETERS_INIT(&requestParam);
                        WdfRequestGetParameters(request, &requestParam);
                        pDevContext->QcStats.SessionTotal = requestParam.Parameters.Read.Length;
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_TRACE,
                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread VI device session total set value: %llu, request: 0x%p\n",
                            pDevContext->PortName, pDevContext->QcStats.SessionTotal, pDevContext->PendingReadRequest)
                        );
                    }
                }

                // create and send read urbs
                readOffset = 0;
                tempSessionTotal = (size_t)pDevContext->QcStats.SessionTotal;
                WDFREQUEST outRequest = NULL;
                ULONGLONG readBufferSize = 0, errorCount = 0;
                ULONG index = 0;
                while (tempSessionTotal > 0)
                {
                    if (tempSessionTotal >= pDevContext->UrbReadBufferSize)
                    {
                        readBufferSize = pDevContext->UrbReadBufferSize;
                    }
                    else
                    {
                        readBufferSize = tempSessionTotal;
                    }
                    tempSessionTotal -= (size_t)readBufferSize;

                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_READ,
                        QCSER_DBG_LEVEL_TRACE,
                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread sending read urb index: [%lu]\n", pDevContext->PortName, index)
                    );

                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_READ,
                        QCSER_DBG_LEVEL_TRACE,
                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread per read buffer size: %llu, temp session total: %llu\n",
                        pDevContext->PortName, readBufferSize, tempSessionTotal)
                    );

                    // create urb
                    errorCount = 0;
                    while (!NT_SUCCESS(status = QCRD_CreateReadUrb(pDevContext, (size_t)readBufferSize, '4gaT', &outRequest)))
                    {
                        errorCount++;
                        if (errorCount > gVendorConfig.UrbReadErrorMaxLimit)
                        {
                            QCSER_DbgPrint
                            (
                                QCSER_DBG_MASK_READ,
                                QCSER_DBG_LEVEL_CRITICAL,
                                ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_SESSION_TOTAL_SET_EVENT urb create fatal error\n", pDevContext->PortName)
                            );
                            QCRD_CleanupReadQueues(pDevContext);
                            goto exit;
                        }
                        QCMAIN_Wait(pDevContext, -(1 * 10 * 1000)); // 1ms
                    }

                    // init urb
                    pReqContext = QCReqGetContext(outRequest);
                    errorCount = 0;
                    while (!NT_SUCCESS(status = QCRD_ResetReadUrb(pDevContext, outRequest)))
                    {
                        errorCount++;
                        if (errorCount > gVendorConfig.UrbReadErrorMaxLimit)
                        {
                            QCSER_DbgPrint
                            (
                                QCSER_DBG_MASK_READ,
                                QCSER_DBG_LEVEL_CRITICAL,
                                ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_SESSION_TOTAL_SET_EVENT urb init FATAL ERROR status: 0x%x\n",
                                pDevContext->PortName, status)
                            );
                            InsertHeadList(&pDevContext->UrbReadFreeList, &pReqContext->Link);
                            pDevContext->UrbReadFreeListLength++;
                            QCRD_CleanupReadQueues(pDevContext);
                            goto exit;
                        }
                        QCMAIN_Wait(pDevContext, -(1 * 10 * 1000)); // 1ms
                    }

                    // send urb
                    errorCount = 0;
                    WdfSpinLockAcquire(pDevContext->UrbReadListLock);
                    InsertTailList(&pDevContext->UrbReadPendingList, &pReqContext->Link);
                    pDevContext->UrbReadPendingListLength++;
                    WdfSpinLockRelease(pDevContext->UrbReadListLock);
                    while (!WdfRequestSend(outRequest, ioTarget, WDF_NO_SEND_OPTIONS))
                    {
                        status = WdfRequestGetStatus(outRequest);
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_ERROR,
                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_SESSION_TOTAL_SET_EVENT read urb resend FAILED request: 0x%p, status: 0x%x\n",
                            pDevContext->PortName, outRequest, status)
                        );
                        WdfSpinLockAcquire(pDevContext->UrbReadListLock);
                        RemoveEntryList(&pReqContext->Link);
                        pDevContext->UrbReadPendingListLength--;
                        WdfSpinLockRelease(pDevContext->UrbReadListLock);

                        errorCount++;
                        if (errorCount > gVendorConfig.UrbReadErrorMaxLimit)
                        {
                            status = WdfRequestGetStatus(outRequest);
                            QCSER_DbgPrint
                            (
                                QCSER_DBG_MASK_READ,
                                QCSER_DBG_LEVEL_ERROR,
                                ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_SESSION_TOTAL_SET_EVENT urb send FATAL ERROR status: 0x%x\n",
                                pDevContext->PortName, status)
                            );
                            InsertHeadList(&pDevContext->UrbReadFreeList, &pReqContext->Link);
                            pDevContext->UrbReadFreeListLength++;
                            QCRD_CleanupReadQueues(pDevContext);
                            goto exit;
                        }
                        QCMAIN_Wait(pDevContext, -(1 * 10 * 1000)); // 1ms
                        WdfSpinLockAcquire(pDevContext->UrbReadListLock);
                        InsertTailList(&pDevContext->UrbReadPendingList, &pReqContext->Link);
                        pDevContext->UrbReadPendingListLength++;
                        WdfSpinLockRelease(pDevContext->UrbReadListLock);
                    }
                    index++;
                    pDevContext->UrbReadListCapacity = index;
                }
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread session total finish sending read urbs free list length: %llu, session total left: %llu\n",
                    pDevContext->PortName, pDevContext->UrbReadFreeListLength, tempSessionTotal)
                );
#endif
                break;
            }
            case READ_THREAD_REQUEST_COMPLETION_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_REQUEST_COMPLETION_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->ReadRequestCompletionEvent);

                head = &pDevContext->UrbReadCompletionList;
                WdfSpinLockAcquire(pDevContext->UrbReadListLock);
                while (!IsListEmpty(head))
                {
                    // retrieve an completed urb
                    peek = head->Flink;
                    pReqContext = CONTAINING_RECORD(peek, REQUEST_CONTEXT, Link);
                    pBufferParam = pReqContext->ReadBufferParam;
                    request = pReqContext->Self;

                    if (!NT_SUCCESS(status = WdfRequestGetStatus(request)) && (status != STATUS_CANCELLED))
                    {
                        devErrCnt++;
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_CRITICAL,
                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread device error count at completion: %lu, request: 0x%p, status: 0x%x\n",
                            pDevContext->PortName, devErrCnt, request, WdfRequestGetStatus(request))
                        );
                        if (devErrCnt >= gVendorConfig.UrbReadErrorMaxLimit)
                        {
                            QCSER_DbgPrint
                            (
                                QCSER_DBG_MASK_READ,
                                QCSER_DBG_LEVEL_CRITICAL,
                                ("<%ws> RIRP: QCRD_ReadRequestHandlerThread device error exceeds limits: %lu, driver FAILED\n", pDevContext->PortName, devErrCnt)
                            );
                            WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
                            break;
                        }
                    }
                    else
                    {
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_CRITICAL,
                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread reset device error count at completion\n", pDevContext->PortName)
                        );
                        devErrCnt = 0;
                    }

                    if (pBufferParam->AvailableBytes == 0)
                    {
                        // nothing is received
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_TRACE,
                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread received 0 bytes request: 0x%p, status: 0x%x\n",
                            pDevContext->PortName, request, WdfRequestGetStatus(request))
                        );
                        QCUTIL_RemoveEntryList(peek, NULL, &pDevContext->UrbReadCompletionListLength);
                        QCUTIL_InsertTailList(&pDevContext->UrbReadFreeList, peek, NULL, &pDevContext->UrbReadFreeListLength);
                    }
                    else if (pBufferParam->AvailableBytes > QCUTIL_RingBufferBytesFree(rxBuffer))
                    {
                        // no space to copy the data, the data will be kept inside the urb in completion list
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_TRACE,
                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread buffer is nearly full at stage 0, bytes needed: %llu, bytes available: %llu, \n",
                            pDevContext->PortName, pBufferParam->AvailableBytes, QCUTIL_RingBufferBytesFree(rxBuffer))
                        );
                        bBufferOverflow = TRUE;
                        break;
                    }
                    else
                    {
                        QCUTIL_RemoveEntryList(peek, NULL, &pDevContext->UrbReadCompletionListLength);
                        pDevContext->UrbReadCompletionListDataSize -= pBufferParam->AvailableBytes;
                        WdfSpinLockRelease(pDevContext->UrbReadListLock);

                        // copy data into rx buffer
                        status = QCUTIL_RingBufferWrite(rxBuffer, pBufferParam->pReadBuffer, pBufferParam->AvailableBytes);
                        if (!NT_SUCCESS(status))
                        {
                            QCSER_DbgPrint
                            (
                                QCSER_DBG_MASK_READ,
                                QCSER_DBG_LEVEL_CRITICAL,
                                ("<%ws> RIRP: QCRD_ReadRequestHandlerThread copy to ring buffer FAILED status: 0x%x, driver FAILED\n", pDevContext->PortName, status)
                            );
                            QCUTIL_InsertTailList(&pDevContext->UrbReadFreeList, peek, NULL, &pDevContext->UrbReadFreeListLength);
                            WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
                            break;
                        }
                        else
                        {
                            // move the urb into free list to resend later
                            QCUTIL_InsertTailList(&pDevContext->UrbReadFreeList, peek, NULL, &pDevContext->UrbReadFreeListLength);
                            QCSER_DbgPrint
                            (
                                QCSER_DBG_MASK_READ,
                                QCSER_DBG_LEVEL_TRACE,
                                ("<%ws> RIRP: QCRD_ReadRequestHandlerThread copied %llu bytes into ring buffer, bytes used: %llu, bytes available: %llu\n",
                                pDevContext->PortName, pBufferParam->Capacity, QCUTIL_RingBufferBytesUsed(rxBuffer), QCUTIL_RingBufferBytesFree(rxBuffer))
                            );
                        }
                        pDevContext->AmountInInQueue = QCUTIL_RingBufferBytesUsed(rxBuffer);
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_TRACE,
                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread buffered %llu bytes into rx ring buffer, space used: %llu bytes, space free: %llu bytes\n",
                            pDevContext->PortName, pBufferParam->AvailableBytes, QCUTIL_RingBufferBytesUsed(rxBuffer), QCUTIL_RingBufferBytesFree(rxBuffer))
                        );
                        QCRD_ScanForWaitMask(pDevContext, pDevContext->Chars.EventChar, pDevContext->WaitMask);
                        WdfSpinLockAcquire(pDevContext->UrbReadListLock);
                    }
                }
                WdfSpinLockRelease(pDevContext->UrbReadListLock);
                KeSetEvent(&pDevContext->ReadRequestArriveEvent, IO_NO_INCREMENT, FALSE);
                break;
            }
            case READ_THREAD_REQUEST_ARRIVE_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_REQUEST_ARRIVE_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->ReadRequestArriveEvent);
                if (bDeviceOpened && bDeviceAwaken)
                {
                    // serve the pending application requests
                    if (QCUTIL_RingBufferBytesUsed(rxBuffer) == 0)
                    {
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_TRACE,
                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread NO DATA AVAILABLE\n", pDevContext->PortName)
                        );

                        // push request to timeout pending queue
                        if (QCUTIL_IsIoQueueEmpty(pDevContext->TimeoutReadQueue))
                        {
                            // get a new request from read queue
                            status = WdfIoQueueRetrieveNextRequest(pDevContext->ReadQueue, &pendingTimeoutRequest);
                            if (NT_SUCCESS(status) && pendingTimeoutRequest != NULL)
                            {
                                QCSER_DbgPrint
                                (
                                    QCSER_DBG_MASK_READ,
                                    QCSER_DBG_LEVEL_TRACE,
                                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread NO DATA AVAILABLE, start read timeout\n", pDevContext->PortName)
                                );
                                WDF_REQUEST_PARAMETERS_INIT(&requestParam);
                                WdfRequestGetParameters(pendingTimeoutRequest, &requestParam);
                                if (QCRD_StartReadTimeout(pDevContext, (ULONG)requestParam.Parameters.Read.Length) == FALSE)
                                {
                                    // timeout immediately
                                    WdfRequestComplete(pendingTimeoutRequest, STATUS_TIMEOUT);
                                    QCSER_DbgPrint
                                    (
                                        QCSER_DBG_MASK_READ,
                                        QCSER_DBG_LEVEL_TRACE,
                                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread immediately timeout request: 0x%p\n", pDevContext->PortName, pendingTimeoutRequest)
                                    );
                                }
                                else
                                {
                                    status = WdfRequestForwardToIoQueue(pendingTimeoutRequest, pDevContext->TimeoutReadQueue);
                                    if (!NT_SUCCESS(status))
                                    {
                                        WdfRequestComplete(pendingTimeoutRequest, status);
                                        QCSER_DbgPrint
                                        (
                                            QCSER_DBG_MASK_READ,
                                            QCSER_DBG_LEVEL_TRACE,
                                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread FAILED to forward to timeout read queue request: 0x%p, status: 0x%x\n", pDevContext->PortName, pendingTimeoutRequest, status)
                                        );
                                    }
                                    else
                                    {
                                        QCSER_DbgPrint
                                        (
                                            QCSER_DBG_MASK_READ,
                                            QCSER_DBG_LEVEL_TRACE,
                                            ("<%ws> RIRP: QCRD_ReadRequestHandlerThread forward to timeout queue request: 0x%p\n", pDevContext->PortName, pendingTimeoutRequest)
                                        );
                                    }
                                }
                                pendingTimeoutRequest = NULL;
                            }
                        }
                    }
                    else
                    {
                        // iterate through the ring buffer
                        while (QCUTIL_RingBufferBytesUsed(rxBuffer) > 0)
                        {
                            // get a pending request and its parameter
                            WdfIoQueueRetrieveNextRequest(pDevContext->TimeoutReadQueue, &pendingTimeoutRequest);
                            if (pendingTimeoutRequest != NULL)
                            {
                                QCSER_DbgPrint
                                (
                                    QCSER_DBG_MASK_READ,
                                    QCSER_DBG_LEVEL_TRACE,
                                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread completing pendingTimeoutRequest request: 0x%p\n", pDevContext->PortName, pendingTimeoutRequest)
                                );
                                KeClearEvent(&pDevContext->ReadRequestTimeoutEvent);
                                KeCancelTimer(&pDevContext->ReadTimer);
                                request = pendingTimeoutRequest;
                                pendingTimeoutRequest = NULL;
                            }
                            else
                            {
                                status = WdfIoQueueRetrieveNextRequest(pDevContext->ReadQueue, &request);
                            }

                            if (!NT_SUCCESS(status) || request == NULL)
                            {
                                // no request in queue, or operation failed
                                QCSER_DbgPrint
                                (
                                    QCSER_DBG_MASK_READ,
                                    QCSER_DBG_LEVEL_INFO,
                                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread get pending request from read queue FAILED status: 0x%x\n", pDevContext->PortName, status)
                                );
                                break;
                            }

                            WDF_REQUEST_PARAMETERS_INIT(&requestParam);
                            WdfRequestGetParameters(request, &requestParam);
                            size_t availableLength = QCUTIL_RingBufferBytesUsed(rxBuffer);
                            size_t requestedLength = requestParam.Parameters.Read.Length;
                            QCSER_DbgPrint
                            (
                                QCSER_DBG_MASK_READ,
                                QCSER_DBG_LEVEL_DETAIL,
                                ("<%ws> RIRP: QCRD_ReadRequestHandlerThread request: 0x%p, requestedLength: %llu, availableBytes: %llu\n", pDevContext->PortName, request, requestedLength, availableLength)
                            );

                            size_t bytesCopied = 0;
                            PUCHAR outputRxBuffer = NULL;
                            WdfRequestRetrieveOutputBuffer(request, requestedLength, &outputRxBuffer, NULL);
                            status = QCUTIL_RingBufferRead(rxBuffer, outputRxBuffer, requestedLength, &bytesCopied);
                            if (NT_SUCCESS(status))
                            {
                                WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, bytesCopied);
                                QCSER_DbgPrint
                                (
                                    QCSER_DBG_MASK_READ,
                                    QCSER_DBG_LEVEL_DETAIL,
                                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread rx data copy successfully length: %llu\n", pDevContext->PortName, bytesCopied)
                                );
                                if (bBufferOverflow == TRUE)
                                {
                                    bBufferOverflow = FALSE;
                                    KeSetEvent(&pDevContext->ReadRequestCompletionEvent, IO_NO_INCREMENT, FALSE);
                                    QCSER_DbgPrint
                                    (
                                        QCSER_DBG_MASK_READ,
                                        QCSER_DBG_LEVEL_ERROR,
                                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread notify data consumed\n", pDevContext->PortName)
                                    );
                                }
                            }
                            else
                            {
                                // operation failed, do nothing but complete the request with failed status
                                WdfRequestCompleteWithInformation(request, status, 0);
                                QCSER_DbgPrint
                                (
                                    QCSER_DBG_MASK_READ,
                                    QCSER_DBG_LEVEL_ERROR,
                                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread rx data copy FAILED status: 0x%x\n", pDevContext->PortName, status)
                                );
                            }
                            pDevContext->AmountInInQueue = QCUTIL_RingBufferBytesUsed(rxBuffer);
                        }
                    }

                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_READ,
                        QCSER_DBG_LEVEL_TRACE,
                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread start to process free list\n", pDevContext->PortName)
                    );

                    head = &pDevContext->UrbReadFreeList;
                    while (!IsListEmpty(head))
                    {
                        peek = head->Flink;
                        QCUTIL_RemoveEntryList(peek, NULL, &pDevContext->UrbReadFreeListLength);
                        pReqContext = CONTAINING_RECORD(peek, REQUEST_CONTEXT, Link);
                        pBufferParam = pReqContext->ReadBufferParam;
                        request = pReqContext->Self;

                        if (QCUTIL_RingBufferBytesFree(rxBuffer) >= pBufferParam->Capacity)
                        {
                            QCRD_ResetReadUrb(pDevContext, request);
                            QCUTIL_InsertTailList(&pDevContext->UrbReadPendingList, peek, pDevContext->UrbReadListLock, &pDevContext->UrbReadPendingListLength);
                            if (!NT_SUCCESS(status = QCRD_SendReadUrb(pDevContext, request, ioTarget)))
                            {
                                QCUTIL_RemoveEntryList(peek, pDevContext->UrbReadListLock, &pDevContext->UrbReadPendingListLength);
                                QCUTIL_InsertTailList(&pDevContext->UrbReadFreeList, peek, NULL, &pDevContext->UrbReadFreeListLength);
                                QCSER_DbgPrint
                                (
                                    QCSER_DBG_MASK_READ,
                                    QCSER_DBG_LEVEL_ERROR,
                                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread resent failed request: 0x%p, status: 0x%x\n", pDevContext->PortName, request, status)
                                );

                                devErrCnt++;
                                if (devErrCnt >= gVendorConfig.NumOfRetriesOnError)
                                {
                                    QCSER_DbgPrint
                                    (
                                        QCSER_DBG_MASK_READ,
                                        QCSER_DBG_LEVEL_CRITICAL,
                                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread device error exceeds limits: %lu, driver FAILED\n", pDevContext->PortName, devErrCnt)
                                    );
                                    WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
                                    break;
                                }
                                else if (devErrCnt % gVendorConfig.UrbReadErrorThreshold == 0)
                                {
                                    // wait 1ms to resend next
                                    QCSER_DbgPrint
                                    (
                                        QCSER_DBG_MASK_READ,
                                        QCSER_DBG_LEVEL_CRITICAL,
                                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread device error count at resend failure: %lu, request: 0x%p, status: 0x%x\n",
                                        pDevContext->PortName, devErrCnt, request, status)
                                    );
                                    QCMAIN_Wait(pDevContext, -(1 * 10 * 1000));
                                }
                            }
                            else
                            {
                                devErrCnt = 0;
                                QCSER_DbgPrint
                                (
                                    QCSER_DBG_MASK_READ,
                                    QCSER_DBG_LEVEL_TRACE,
                                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread resent successfully read request: 0x%p\n", pDevContext->PortName, request)
                                );
                            }
                        }
                        else
                        {
                            // no space for new data, put it back and wait for the next
                            QCUTIL_InsertTailList(&pDevContext->UrbReadFreeList, peek, NULL, &pDevContext->UrbReadFreeListLength);
                            break;
                        }
                    }

                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_READ,
                        QCSER_DBG_LEVEL_TRACE,
                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread completed processing free list\n", pDevContext->PortName)
                    );
                }
                break;
            }
            case READ_THREAD_DEVICE_REMOVE_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_DEVICE_REMOVE_EVENT triggered\n", pDevContext->PortName)
                );
                bRunning = FALSE;
                break;
            }
            case READ_THREAD_DEVICE_D0_EXIT_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_DEVICE_D0_EXIT_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->ReadThreadD0ExitEvent);
                bDeviceAwaken = FALSE;
                WdfIoTargetStop(ioTarget, WdfIoTargetCancelSentIo);
                KeSetEvent(&pDevContext->ReadThreadD0ExitReadyEvent, IO_NO_INCREMENT, FALSE);
                break;
            }
            case READ_THREAD_DEVICE_D0_ENTRY_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_DEVICE_D0_ENTRY_EVENT triggered\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->ReadThreadD0EntryEvent);
                bDeviceAwaken = TRUE;
                WdfIoTargetStart(ioTarget);
                if (bDeviceOpened)
                {
                    size_t sentCount = QCRD_SendReadUrbs(pDevContext, ioTarget);
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_READ,
                        QCSER_DBG_LEVEL_TRACE,
                        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_DEVICE_D0_ENTRY_EVENT resend read urb count: %llu\n", pDevContext->PortName, sentCount)
                    );
                }
                KeSetEvent(&pDevContext->ReadThreadD0EntryReadyEvent, IO_NO_INCREMENT, FALSE);
                KeSetEvent(&pDevContext->ReadRequestArriveEvent, IO_NO_INCREMENT, FALSE);
                break;
            }
            case READ_THREAD_REQUEST_TIMEOUT_EVENT:
            {
                KeClearEvent(&pDevContext->ReadRequestTimeoutEvent);
                KeCancelTimer(&pDevContext->ReadTimer);
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_REQUEST_TIMEOUT_EVENT triggered\n", pDevContext->PortName)
                );
                WdfIoQueueRetrieveNextRequest(pDevContext->TimeoutReadQueue, &pendingTimeoutRequest);
                if (pendingTimeoutRequest != NULL)
                {
                    WdfRequestCompleteWithInformation(pendingTimeoutRequest, STATUS_TIMEOUT, 0);
                    pendingTimeoutRequest = NULL;
                }
                break;
            }
            case READ_THREAD_SCAN_WAIT_MASK_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread READ_THREAD_SCAN_WAIT_MASK_EVENT triggered bDeviceOpened: %d, bDeviceAwaken: %d\n",
                    pDevContext->PortName, bDeviceOpened, bDeviceAwaken)
                );
                KeClearEvent(&pDevContext->ReadThreadScanWaitMaskEvent);
                if (bDeviceOpened)
                {
                    QCRD_ScanForWaitMask(pDevContext, pDevContext->Chars.EventChar, pDevContext->WaitMask);
                }
            }
            default:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_ReadRequestHandlerThread wait for resume event status: 0x%x\n", pDevContext->PortName, status)
                );
                break;
            }
        }
    }

exit:
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> RIRP: QCRD_ReadRequestHandlerThread exited status: 0x%x\n", pDevContext->PortName, status)
    );
    if (pWaitBlock != NULL)
    {
        ExFreePoolWithTag(pWaitBlock, '3gaT');
        pWaitBlock = NULL;
    }
    WdfIoQueuePurgeSynchronously(pDevContext->ReadQueue);
    KeSetEvent(&pDevContext->ReadThreadD0EntryReadyEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&pDevContext->ReadThreadD0ExitReadyEvent, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(status);
}

NTSTATUS QCRD_CreateReadUrb
(
    PDEVICE_CONTEXT pDevContext,
    size_t          urbBufferSize,
    ULONG           readBufferParamTag,
    WDFREQUEST *outRequest
)
{
    NTSTATUS              status;
    WDFREQUEST            request;
    PVOID                 outputBuffer;
    WDFMEMORY             outputMemory;
    PREQUEST_CONTEXT      pReqContext;
    WDFIOTARGET           ioTarget = WdfUsbTargetPipeGetIoTarget(pDevContext->BulkIN);
    WDF_OBJECT_ATTRIBUTES requestAttr;
    WDF_OBJECT_ATTRIBUTES memoryAttr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&requestAttr, REQUEST_CONTEXT);
    WDF_OBJECT_ATTRIBUTES_INIT(&memoryAttr);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> RIRP: QCRD_CreateReadUrb size: %llu\n", pDevContext->PortName, urbBufferSize)
    );

    requestAttr.ParentObject = pDevContext->UsbDevice;
    status = WdfRequestCreate
    (
        &requestAttr,
        ioTarget,
        &request
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> RIRP: QCRD_CreateReadUrb urb create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> RIRP: QCRD_CreateReadUrb urb created request: 0x%p, status: 0x%x\n", pDevContext->PortName, request, status)
    );

    if (urbBufferSize > 0)
    {
        memoryAttr.ParentObject = request;
        status = WdfMemoryCreate
        (
            &memoryAttr,
            NonPagedPoolNx,
            0,
            urbBufferSize,
            &outputMemory,
            &outputBuffer
        );
        if (!NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_READ,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> RIRP: QCRD_CreateReadUrb urb memory allocate FAILED status: 0x%x\n", pDevContext->PortName, status)
            );
            WdfObjectDelete(request);
            goto exit;
        }
        RtlZeroMemory(outputBuffer, urbBufferSize);
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> RIRP: QCRD_CreateReadUrb urb memory created status: 0x%x, length: %llu\n", pDevContext->PortName, status, urbBufferSize)
        );
    }
    else
    {
        outputBuffer = NULL;
        outputMemory = NULL;
    }

    pReqContext = QCReqGetContext(request);
    pReqContext->Self = request;
    pReqContext->ReadBufferParam = ExAllocatePoolZero(NonPagedPoolNx, sizeof(READ_BUFFER_PARAM), readBufferParamTag);
    if (pReqContext->ReadBufferParam == NULL)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> RIRP: QCRD_CreateReadUrb allocate memory for ReadBufferParam FAILED\n", pDevContext->PortName)
        );
        WdfObjectDelete(request);
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }

    pReqContext->ReadBufferParam->Capacity = urbBufferSize;
    pReqContext->ReadBufferParam->pReadMemory = outputMemory;

exit:
    if (NT_SUCCESS(status) && outRequest != NULL)
    {
        *outRequest = request;
    }
    return status;
}

NTSTATUS QCRD_ResetReadUrb
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      request
)
{
    NTSTATUS status;
    PREAD_BUFFER_PARAM pBufferParam;
    WDF_REQUEST_REUSE_PARAMS reuseParam;
    PREQUEST_CONTEXT pReqContext = QCReqGetContext(request);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> RIRP: QCRD_ResetReadUrb request: 0x%p\n", pDevContext->PortName, request)
    );

    pBufferParam = pReqContext->ReadBufferParam;
    if (request == NULL || pBufferParam == NULL)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> RIRP: QCRD_ResetReadUrb FAILED pBufferParam: 0x%p\n", pDevContext->PortName, pBufferParam)
        );
        return STATUS_INVALID_PARAMETER;
    }

    // reset buffer parameters
    pBufferParam->pReadBuffer = WdfMemoryGetBuffer(pBufferParam->pReadMemory, NULL);
    pBufferParam->AvailableBytes = 0;

    // reuse request object
    WDF_REQUEST_REUSE_PARAMS_INIT(&reuseParam, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_SUCCESS);
    status = WdfRequestReuse(request, &reuseParam);
    if (NT_SUCCESS(status))
    {
        WdfRequestSetCompletionRoutine(request, QCRD_EvtIoReadCompletionAsync, pDevContext);
        status = WdfUsbTargetPipeFormatRequestForRead
        (
            pDevContext->BulkIN,
            request,
            pBufferParam->pReadMemory,
            0
        );
        if (!NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_READ,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> RIRP: QCRD_ResetReadUrb request format FAILED status: 0x%x\n", pDevContext->PortName, status)
            );
        }
    }
    else
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> RIRP: QCRD_ResetReadUrb request reuse FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }
    return status;
}

NTSTATUS QCRD_SendReadUrb
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      request,
    WDFIOTARGET     ioTarget
)
{
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> RIRP: QCRD_SendReadUrb request: 0x%p\n", pDevContext->PortName, request)
    );

    if (request == NULL || ioTarget == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (WdfRequestSend(request, ioTarget, WDF_NO_SEND_OPTIONS) == FALSE)
    {
        NTSTATUS status = WdfRequestGetStatus(request);
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> RIRP: QCRD_SendReadUrb read resend FAILED request: 0x%p, status: 0x%x\n", pDevContext->PortName, request, status)
        );
        return status;
    }
    else
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> RIRP: QCRD_SendReadUrb read resent request: 0x%p\n", pDevContext->PortName, request)
        );
        return STATUS_SUCCESS;
    }
}

SIZE_T QCRD_SendReadUrbs
(
    PDEVICE_CONTEXT pDevContext,
    WDFIOTARGET     ioTarget
)
{
    size_t           sentCount = 0, queueCount = pDevContext->UrbReadFreeListLength;
    PLIST_ENTRY      head = &pDevContext->UrbReadFreeList;
    PLIST_ENTRY      peek;
    WDFREQUEST       request;
    PREQUEST_CONTEXT pReqContext;
    NTSTATUS         status;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> RIRP: QCRD_SendReadUrbs count: %llu\n", pDevContext->PortName, queueCount)
    );

    while (queueCount > 0)
    {
        peek = head->Flink;
        QCUTIL_RemoveEntryList(peek, NULL, &pDevContext->UrbReadFreeListLength);
        pReqContext = CONTAINING_RECORD(peek, REQUEST_CONTEXT, Link);
        request = pReqContext->Self;
        status = QCRD_ResetReadUrb(pDevContext, request);
        if (!NT_SUCCESS(status))
        {
            // failed to reset request
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_READ,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> RIRP: QCRD_SendReadUrbs request reset FAILED request: 0x%p, status: 0x%x\n", pDevContext->PortName, request, status)
            );
            QCUTIL_InsertTailList(&pDevContext->UrbReadFreeList, peek, NULL, &pDevContext->UrbReadFreeListLength);
        }
        else
        {
            QCUTIL_InsertTailList(&pDevContext->UrbReadPendingList, peek, pDevContext->UrbReadListLock, &pDevContext->UrbReadPendingListLength);
            if (WdfRequestSend(request, ioTarget, WDF_NO_SEND_OPTIONS) == FALSE)
            {
                // failed to resend request
                status = WdfRequestGetStatus(request);
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_ERROR,
                    ("<%ws> RIRP: QCRD_SendReadUrbs read resend FAILED request: 0x%p, status: 0x%x\n", pDevContext->PortName, request, status)
                );
                QCUTIL_RemoveEntryList(peek, pDevContext->UrbReadListLock, &pDevContext->UrbReadPendingListLength);
                QCUTIL_InsertTailList(&pDevContext->UrbReadFreeList, peek, NULL, &pDevContext->UrbReadFreeListLength);
            }
            else
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> RIRP: QCRD_SendReadUrbs read request resend successfully request: 0x%p\n", pDevContext->PortName, request)
                );
                sentCount++;
            }
        }
        queueCount--;
    }

    return sentCount;
}

VOID QCRD_CleanupReadQueues
(
    PDEVICE_CONTEXT pDevContext
)
{
    PLIST_ENTRY      head;
    PLIST_ENTRY      peek;
    WDFREQUEST       request;
    PREQUEST_CONTEXT pReqContext = NULL;
    WDFIOTARGET      ioTarget = WdfUsbTargetPipeGetIoTarget(pDevContext->BulkIN);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> RIRP: QCRD_CleanupReadQueues\n", pDevContext->PortName)
    );

    // purge timeout read queue
    KeCancelTimer(&pDevContext->ReadTimer);
    WdfIoQueuePurgeSynchronously(pDevContext->TimeoutReadQueue);

    // purge the bulk in pipe
    WdfIoTargetStop(ioTarget, WdfIoTargetCancelSentIo);
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> RIRP: QCRD_CleanupReadQueues finished cancelling pending list, completion list: %llu, pending list: %llu, free list: %llu\n",
        pDevContext->PortName, pDevContext->UrbReadCompletionListLength, pDevContext->UrbReadPendingListLength, pDevContext->UrbReadFreeListLength)
    );

    // all urbs are cancelled, move them from completion queue to free queue
    QCRD_ClearBuffer(pDevContext);
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> RIRP: QCRD_CleanupReadQueues finished cleaning up completion list, completion list: %llu, pending list: %llu, free list: %llu\n",
        pDevContext->PortName, pDevContext->UrbReadCompletionListLength, pDevContext->UrbReadPendingListLength, pDevContext->UrbReadFreeListLength)
    );

    // delete all urbs
    head = &pDevContext->UrbReadFreeList;
    while (!IsListEmpty(head))
    {
        peek = head->Flink;
        QCUTIL_RemoveEntryList(peek, NULL, &pDevContext->UrbReadFreeListLength);
        pReqContext = CONTAINING_RECORD(peek, REQUEST_CONTEXT, Link);
        request = pReqContext->Self;
        if (pReqContext->ReadBufferParam != NULL)
        {
            ExFreePoolWithTag(pReqContext->ReadBufferParam, '4gaT');
            pReqContext->ReadBufferParam = NULL;
        }
        WdfObjectDelete(request);
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> RIRP: QCRD_CleanupReadQueues successfully deleted free list request 0x%p, len: %llu\n", pDevContext->PortName, request, pDevContext->UrbReadFreeListLength)
        );
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> RIRP: QCRD_CleanupReadQueues finished cleaning up free list, free list: %llu\n", pDevContext->PortName, pDevContext->UrbReadFreeListLength)
    );
}

VOID QCRD_ClearBuffer
(
    PDEVICE_CONTEXT pDevContext
)
{
    PLIST_ENTRY      head;
    PLIST_ENTRY      peek;
    PREQUEST_CONTEXT pReqContext = NULL;
    WDFIOTARGET      ioTarget = WdfUsbTargetPipeGetIoTarget(pDevContext->BulkIN);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> RIRP: QCRD_ClearBuffer\n", pDevContext->PortName)
    );

    QCUTIL_RingBufferClear(&pDevContext->ReadRingBuffer);
    pDevContext->AmountInInQueue = QCUTIL_RingBufferBytesUsed(&pDevContext->ReadRingBuffer);

    head = &pDevContext->UrbReadCompletionList;
    WdfSpinLockAcquire(pDevContext->UrbReadListLock);
    while (!IsListEmpty(head))
    {
        peek = head->Flink;
        QCUTIL_RemoveEntryList(peek, NULL, &pDevContext->UrbReadCompletionListLength);
        pReqContext = CONTAINING_RECORD(peek, REQUEST_CONTEXT, Link);
        if (pReqContext->ReadBufferParam != NULL)
        {
            pDevContext->UrbReadCompletionListDataSize -= pReqContext->ReadBufferParam->AvailableBytes;
        }
        QCUTIL_InsertTailList(&pDevContext->UrbReadFreeList, peek, NULL, &pDevContext->UrbReadFreeListLength);
    }
    WdfSpinLockRelease(pDevContext->UrbReadListLock);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> RIRP: QCRD_ClearBuffer finished clearing completion list, completion list len: %llu\n", pDevContext->PortName, pDevContext->UrbReadCompletionListLength)
    );
}

BOOLEAN QCRD_StartReadTimeout
(
    PDEVICE_CONTEXT pDevContext,
    ULONG readLength
)
{
    LONGLONG timeoutMillisecond;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> RIRP: QCRD_StartReadTimeout read length: %lu, timeout type: %u, rc: %lu, rm: %lu\n",
        pDevContext->PortName, readLength, pDevContext->ReadTimeout.ucTimeoutType,
        pDevContext->Timeouts.ReadTotalTimeoutConstant, pDevContext->Timeouts.ReadTotalTimeoutMultiplier)
    );

    switch (pDevContext->ReadTimeout.ucTimeoutType)
    {
        // no timeout
        case QCSER_READ_TIMEOUT_UNDEF:
        case QCSER_READ_TIMEOUT_CASE_1:
        case QCSER_READ_TIMEOUT_CASE_2:
        case QCSER_READ_TIMEOUT_CASE_7:
        case QCSER_READ_TIMEOUT_CASE_9:
        case QCSER_READ_TIMEOUT_CASE_10:
        {
            timeoutMillisecond = 0L;
            break;
        }
        // constant timeout
        case QCSER_READ_TIMEOUT_CASE_5:
        case QCSER_READ_TIMEOUT_CASE_11:
        {
            timeoutMillisecond = pDevContext->Timeouts.ReadTotalTimeoutConstant;
            break;
        }
        // total timeout
        case QCSER_READ_TIMEOUT_CASE_3:
        case QCSER_READ_TIMEOUT_CASE_8:
        {
            // RI timeout not supported for case 5 & 11, use total timeout
            timeoutMillisecond = (LONGLONG)pDevContext->Timeouts.ReadTotalTimeoutConstant +
                (LONGLONG)pDevContext->Timeouts.ReadTotalTimeoutMultiplier * readLength;
            break;
        }
        // return immediately
        case QCSER_READ_TIMEOUT_CASE_4:
        case QCSER_READ_TIMEOUT_CASE_6:
        default:
        {
            // return immediately if no data is available
            return FALSE;
        }
    }

    if (timeoutMillisecond > 0)
    {
        LARGE_INTEGER readTimeoutValue;
        readTimeoutValue.QuadPart = -10000 * timeoutMillisecond;   // unit: 100ns
        KeSetTimer(&pDevContext->ReadTimer, readTimeoutValue, &pDevContext->ReadTimeoutDpc);
    }
    return TRUE;
}

VOID QCRD_ReadTimeoutDpc
(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
)
{
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    UNREFERENCED_PARAMETER(Dpc);
    PDEVICE_CONTEXT pDevContext = DeferredContext;

    KeSetEvent(&pDevContext->ReadRequestTimeoutEvent, IO_NO_INCREMENT, FALSE);
}

VOID QCRD_EvtIoReadCompletionAsync
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
    NTSTATUS         status = WdfRequestGetStatus(Request);
    size_t           availableLength = Params->Parameters.Usb.Completion->Parameters.PipeRead.Length;

    if (NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> RIRP: QCRD_EvtIoReadCompletionAsync request: 0x%p, len: %llu, status: 0x%x, status2: 0x%x, len2: %llu\n",
            pDevContext->PortName, Request, availableLength, status, Params->IoStatus.Status, Params->IoStatus.Information)
        );
    }
    else
    {
        // request failed
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> RIRP: QCRD_EvtIoReadCompletionAsync request FAILED request: 0x%p, len: %llu, status: 0x%x, usbd status: 0x%x, status2: 0x%x, len2: %llu\n",
            pDevContext->PortName, Request, availableLength, status, Params->Parameters.Usb.Completion->UsbdStatus, Params->IoStatus.Status, Params->IoStatus.Information)
        );
    }

    // forward the request to completion list
    WdfSpinLockAcquire(pDevContext->UrbReadListLock);
    pReqContext->ReadBufferParam->AvailableBytes = availableLength;
    if (QCUTIL_FindEntryInList(&pDevContext->UrbReadPendingList, &pReqContext->Link) == TRUE)
    {
        QCUTIL_RemoveEntryList(&pReqContext->Link, NULL, &pDevContext->UrbReadPendingListLength);
    }
    QCUTIL_InsertTailList(&pDevContext->UrbReadCompletionList, &pReqContext->Link, NULL, &pDevContext->UrbReadCompletionListLength);
    pDevContext->UrbReadCompletionListDataSize += availableLength;
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> RIRP: QCRD_EvtIoReadCompletionAsync list len comp: %llu, pend: %llu, free: %llu\n", pDevContext->PortName,
        pDevContext->UrbReadCompletionListLength, pDevContext->UrbReadPendingListLength, pDevContext->UrbReadFreeListLength)
    );
    WdfSpinLockRelease(pDevContext->UrbReadListLock);

    if (NT_SUCCESS(status) && availableLength > 0)
    {
        // print RX data here
        QCUTIL_PrintBytes(pReqContext->ReadBufferParam->pReadBuffer, 128, (ULONG)availableLength, "RxData", QCSER_DBG_MASK_RDATA, QCSER_DBG_LEVEL_TRACE, pDevContext);
    }

    KeSetEvent(&pDevContext->ReadRequestCompletionEvent, IO_NO_INCREMENT, FALSE);
}
