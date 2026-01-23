/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B M W T . C

GENERAL DESCRIPTION
    This file contains implementations for writing data to USB device.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdio.h>
#include "USBWT.h"
#include "USBUTL.h"
#include "USBPWR.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBMWT.tmh"

#endif   // EVENT_TRACING

extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);

extern USHORT ucThCnt;

NTSTATUS USBMWT_WriteCompletionRoutine
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    PQCMWT_BUFFER     pWtBuf = (PQCMWT_BUFFER)pContext;
    PDEVICE_EXTENSION pDevExt = pWtBuf->DeviceExtension;
    KIRQL             Irql = KeGetCurrentIrql();
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> MWT_WriteCompletionRoutine[%d]=0x%p(0x%x)\n",
        pDevExt->PortName, pWtBuf->Index, pIrp, pIrp->IoStatus.Status)
    );
    QcAcquireSpinLockWithLevel(&pDevExt->WriteSpinLock, &levelOrHandle, Irql);

    // State change: pending => completed
    pWtBuf->State = MWT_BUF_COMPLETED;

    // Remove entry from MWritePendingList
    RemoveEntryList(&pWtBuf->List);
    pDevExt->MWTPendingCnt--;

    // Put the entry into MWriteCompletionList
    InsertTailList(&pDevExt->MWriteCompletionQueue, &pWtBuf->List);

    QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, Irql);

    KeSetEvent
    (
        &pWtBuf->WtCompletionEvt,
        IO_NO_INCREMENT,
        FALSE
    );

    QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, FALSE, 4); // WT completion

    return STATUS_MORE_PROCESSING_REQUIRED;
}  // USBMWT_WriteCompletionRoutine

void USBMWT_WriteThread(PVOID pContext)
{
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pContext;
    PIO_STACK_LOCATION pNextStack;
    PIRP         pIrp;
    PURB         pUrb;
    BOOLEAN      bCancelled = FALSE, bPurged = FALSE;
    NTSTATUS     ntStatus;
    PKWAIT_BLOCK pwbArray = NULL;
    BOOLEAN      bSendZeroLength = FALSE;
    ULONG        i;
    PCHAR        pActiveBuffer = NULL;
    ULONG        ulReqBytes = 0;
    KEVENT       dummyEvent;
    BOOLEAN      bFirstTime = TRUE;
    ULONG        waitCount = WRITE_EVENT_COUNT + pDevExt->NumberOfMultiWrites;
    PQCMWT_BUFFER pWtBuf;
    PLIST_ENTRY  headOfList;
    MWT_STATE    mwtState = MWT_STATE_WORKING;
    UCHAR        reqSent;  // for debugging purpose
    PCHAR        tempBuf[1];
    QCUSB_MWT_SENT_IRP sentIrp[QCUSB_MAX_MWT_BUF_COUNT];
    BOOLEAN      bFlowOn = TRUE;
    PLIST_ENTRY  irpSent;
    PQCUSB_MWT_SENT_IRP irpSentRec;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    // allocate a wait block array for the multiple wait
    pwbArray = _ExAllocatePool
    (
        NonPagedPool,
        waitCount * sizeof(KWAIT_BLOCK),
        "MWT.pwbArray"
    );
    if (!pwbArray)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> MWT: STATUS_NO_MEMORY 1\n", pDevExt->PortName)
        );
        _closeHandle(pDevExt->hWriteThreadHandle, "MW-1");
        PsTerminateSystemThread(STATUS_NO_MEMORY);
    }

#ifdef ENABLE_LOGGING
    // Create logs
    if (pDevExt->EnableLogging == TRUE)
    {
        QCUSB_CreateLogs(pDevExt, QCUSB_CREATE_TX_LOG);
    }
#endif // ENABLE_LOGGING

    // Set WRITE thread priority
    KeSetPriorityThread(KeGetCurrentThread(), QCUSB_WT_PRIORITY);

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_INFO,
        ("<%s> W pri=%d\n", pDevExt->PortName, KeQueryPriorityThread(KeGetCurrentThread()))
    );

    pDevExt->bWriteActive = FALSE;
    pDevExt->bWriteStopped = pDevExt->PowerSuspended;
    pDevExt->pWriteCurrent = NULL;  // PIRP

    pDevExt->pWriteEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
    KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
    KeClearEvent(&dummyEvent);
    KeClearEvent(&pDevExt->WritePurgeEvent);

    RtlZeroMemory(&sentIrp, sizeof(QCUSB_MWT_SENT_IRP) * QCUSB_MAX_MWT_BUF_COUNT);
    for (i = 0; i < QCUSB_MAX_MWT_BUF_COUNT; i++)
    {
        sentIrp[i].IrpReturned = TRUE;
        InsertTailList(&pDevExt->MWTSentIrpRecordPool, &(sentIrp[i].List));
    }

#ifdef QCUSB_MUX_PROTOCOL
    USBMWT_InitializeBurstSettings(pDevExt);
#endif 

    while (TRUE)
    {
        if ((!inDevState(DEVICE_STATE_PRESENT_AND_STARTED)) ||
            (bPurged == TRUE))
        {
            USBUTL_PurgeQueue
            (
                pDevExt,
                &pDevExt->WriteDataQueue,
                &pDevExt->WriteSpinLock,
                QCUSB_IRP_TYPE_WIRP,
                0
            );
            bPurged = FALSE;

            // check to see if we need to complete the purge IRP
            USBUTL_CheckPurgeStatus(pDevExt, USB_PURGE_WT);

            goto wait_for_completion;
        }

        if (pDevExt->bWriteStopped == TRUE)
        {
            if (bFlowOn == FALSE)
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> MWT: flow_off, OUT stopped", pDevExt->PortName)
                );
            }
            else
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> MWT: flow_on, but W stopped", pDevExt->PortName)
                );
            }
            goto wait_for_completion;
        }
        else if (bFlowOn == FALSE)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WRITE,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> MWT: flow_off, but W not stopped", pDevExt->PortName)
            );
            goto wait_for_completion;
        }

        reqSent = 0;

        while (TRUE)
        {
            // De-Queue
            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

            if (((!IsListEmpty(&pDevExt->WriteDataQueue)) || (bSendZeroLength == TRUE)) &&
                (!IsListEmpty(&pDevExt->MWriteIdleQueue)) &&
                (inDevState(DEVICE_STATE_PRESENT_AND_STARTED)))
            {
                PLIST_ENTRY headOfList;
                PIO_STACK_LOCATION irpStack;

#ifdef QCUSB_PWR
                if (bCancelled == FALSE)
                {
                    // yes we have a write to deque
                    if ((QCPWR_CheckToWakeup(pDevExt, NULL, QCUSB_BUSY_WT, 1) == TRUE) ||
                        (pDevExt->bWriteStopped == TRUE))
                    {
                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                        goto wait_for_completion;  // wait for the kick event
                    }
                }
                else
                {
                    if (IsListEmpty(&pDevExt->WriteDataQueue))
                    {
                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                        break;  // get out of the loop
                    }
                    else
                    {
                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                        USBUTL_PurgeQueue
                        (
                            pDevExt,
                            &pDevExt->WriteDataQueue,
                            &pDevExt->WriteSpinLock,
                            QCUSB_IRP_TYPE_WIRP,
                            0
                        );
                        break;  // get out of the loop
                    }
                }
#endif // QCUSB_PWR

                // De-queue write idle buffer
                headOfList = RemoveHeadList(&pDevExt->MWriteIdleQueue);
                pWtBuf = CONTAINING_RECORD(headOfList, QCMWT_BUFFER, List);
                // State change: idle => pending
                pWtBuf->State = MWT_BUF_PENDING;
                InsertTailList(&pDevExt->MWritePendingQueue, &pWtBuf->List);
                pDevExt->MWTPendingCnt++;

                if (bSendZeroLength == FALSE)
                {
                    headOfList = RemoveHeadList(&pDevExt->WriteDataQueue);
                    pDevExt->pWriteCurrent = CONTAINING_RECORD
                    (
                        headOfList,
                        IRP,
                        Tail.Overlay.ListEntry
                    );
                    irpStack = IoGetCurrentIrpStackLocation(pDevExt->pWriteCurrent);

                    QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                    if ((pDevExt->pWriteCurrent)->MdlAddress)
                    {
                        pActiveBuffer = MmGetSystemAddressForMdlSafe
                        (
                            (pDevExt->pWriteCurrent)->MdlAddress,
                            HighPagePriority
                        );
                        ulReqBytes = USBUTL_GetMdlTotalCount((pDevExt->pWriteCurrent)->MdlAddress);
                    }
                    else
                    {
                        pActiveBuffer = (pDevExt->pWriteCurrent)->AssociatedIrp.SystemBuffer;
                        ulReqBytes = irpStack->Parameters.Write.Length;
                    }

                    if (ulReqBytes >= pDevExt->pWriteCurrent->IoStatus.Information)
                    {
                        ulReqBytes -= pDevExt->pWriteCurrent->IoStatus.Information;
                    }
                }
                else
                {
                    QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                    pActiveBuffer = (PCHAR)tempBuf;
                    ulReqBytes = 0;
                    pDevExt->pWriteCurrent = NULL;
                }

                pIrp = pWtBuf->Irp;
                pUrb = &pWtBuf->Urb;
                pWtBuf->Length = ulReqBytes;
                pWtBuf->CallingIrp = pDevExt->pWriteCurrent;

                // record the IRP to be sent
                irpSent = RemoveHeadList(&pDevExt->MWTSentIrpRecordPool);
                irpSentRec = CONTAINING_RECORD(irpSent, QCUSB_MWT_SENT_IRP, List);
                irpSentRec->SentIrp = pWtBuf->CallingIrp;  // could be NULL for 0-len
                if (pDevExt->pWriteCurrent != NULL)
                {
                    irpSentRec->TotalLength = ulReqBytes + pDevExt->pWriteCurrent->IoStatus.Information;
                }
                else
                {
                    // 0-len pkt
                    irpSentRec->TotalLength = ulReqBytes;
                }
                irpSentRec->MWTBuf = pWtBuf;
                irpSentRec->IrpReturned = FALSE;
                InsertTailList(&pDevExt->MWTSentIrpQueue, &irpSentRec->List);

                RtlZeroMemory
                (
                    pUrb,
                    sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER)
                ); // clear out the urb we reuse

                if (bSendZeroLength == FALSE)
                {
                    if ((pDevExt->pWriteCurrent)->MdlAddress == NULL)
                    {
                        UsbBuildInterruptOrBulkTransferRequest
                        (
                            pUrb,
                            sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                            pDevExt->Interface[pDevExt->DataInterface]
                            ->Pipes[pDevExt->BulkPipeOutput].PipeHandle,
                            (pActiveBuffer + pWtBuf->CallingIrp->IoStatus.Information),
                            NULL,
                            ulReqBytes,
                            USBD_TRANSFER_DIRECTION_OUT,
                            NULL
                        );
                    }
                    else
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_CRITICAL,
                            ("<%s> MWT: ERROR - MDL being used!!!!\n", pDevExt->PortName)
                        );
                        UsbBuildInterruptOrBulkTransferRequest
                        (
                            pUrb,
                            sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                            pDevExt->Interface[pDevExt->DataInterface]
                            ->Pipes[pDevExt->BulkPipeOutput].PipeHandle,
                            NULL,
                            (pDevExt->pWriteCurrent)->MdlAddress,
                            ulReqBytes,
                            USBD_TRANSFER_DIRECTION_OUT,
                            NULL
                        );
                    }
                }
                else
                {
                    UsbBuildInterruptOrBulkTransferRequest
                    (
                        pUrb,
                        sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                        pDevExt->Interface[pDevExt->DataInterface]
                        ->Pipes[pDevExt->BulkPipeOutput].PipeHandle,
                        pActiveBuffer,
                        NULL,
                        ulReqBytes,
                        USBD_TRANSFER_DIRECTION_OUT,
                        NULL
                    );
                    bSendZeroLength = FALSE;
                }

                if ((ulReqBytes > 0) && (ulReqBytes % pDevExt->wMaxPktSize == 0))
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_DETAIL,
                        ("<%s> MWT: add 0-len next for %uB\n", pDevExt->PortName, ulReqBytes)
                    );
                    bSendZeroLength = TRUE;
                }

                IoReuseIrp(pIrp, STATUS_SUCCESS);
                pNextStack = IoGetNextIrpStackLocation(pIrp);
                pNextStack->Parameters.DeviceIoControl.IoControlCode =
                    IOCTL_INTERNAL_USB_SUBMIT_URB;
                pNextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
                pNextStack->Parameters.Others.Argument1 = pUrb;

                IoSetCompletionRoutine
                (
                    pIrp,
                    USBMWT_WriteCompletionRoutine,
                    pWtBuf,
                    TRUE,
                    TRUE,
                    TRUE
                );

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> MWT: Sending IRP 0x%p/0x%p[%u] %uB/%uB\n", pDevExt->PortName,
                    pWtBuf->CallingIrp, pIrp, pWtBuf->Index, ulReqBytes, irpSentRec->TotalLength)
                );
                if (ulReqBytes > 0)
                {
                    if ((pDevExt->pWriteCurrent)->MdlAddress == NULL)
                    {
                        USBUTL_PrintBytes
                        (
                            pActiveBuffer, 128, ulReqBytes, "SendData", pDevExt,
                            QCUSB_DBG_MASK_WDATA, QCUSB_DBG_LEVEL_VERBOSE
                        );
                    }
                    else
                    {
                        USBUTL_MdlPrintBytes
                        (
                            (pDevExt->pWriteCurrent)->MdlAddress, 128, "MdlSendData",
                            QCUSB_DBG_MASK_WDATA, QCUSB_DBG_LEVEL_VERBOSE
                        );
                    }
                }
                else
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_DETAIL,
                        ("<%s> MWT: added 0-len\n", pDevExt->PortName)
                    );
                }

#ifdef QCUSB_MUX_PROTOCOL
                USBMWT_BurstCheck(pDevExt, ulReqBytes, BURST_SEND);
#endif

                reqSent++;
                pDevExt->bWriteActive = TRUE;
                ntStatus = IoCallDriver(pDevExt->StackDeviceObject, pIrp);
                if (ntStatus != STATUS_PENDING)
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_CRITICAL,
                        ("<%s> MWT: IoCallDriver rtn 0x%x", pDevExt->PortName, ntStatus)
                    );
                }
            }
            else
            {
                QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                break;
            }
        } // while (TRUE)

wait_for_completion:

        QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
        if (IsListEmpty(&pDevExt->MWritePendingQueue) &&
            IsListEmpty(&pDevExt->MWriteCompletionQueue) &&
            (bCancelled == TRUE))
        {
            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WRITE,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> MWT: no act Qs, exit", pDevExt->PortName)
            );
            // if nothings active and we're cancelled, bail
            break; // goto exit_WriteThread;
        }
        else
        {
            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WRITE,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> MWT: Qs act %d, wait", pDevExt->PortName, reqSent)
            );
        }

        // No matter what IoCallDriver returns, we always wait on the kernel event
        // we created earlier. Our completion routine will gain control when the IRP
        // completes to signal this event. -- Walter Oney's WDM book page 228

        if (bFirstTime == TRUE)
        {
            bFirstTime = FALSE;
            KeSetEvent(&pDevExt->WriteThreadStartedEvent, IO_NO_INCREMENT, FALSE);
        }

        // if nothing in the queue, we just wait for a KICK event
        ntStatus = KeWaitForMultipleObjects
        (
            waitCount,
            (VOID **)&pDevExt->pWriteEvents,
            WaitAny,
            Executive,
            KernelMode,
            FALSE, // non-alertable
            NULL,
            pwbArray
        );

        switch (ntStatus)
        {
            case QC_DUMMY_EVENT_INDEX:
            {
                KeClearEvent(&dummyEvent);
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_ERROR,
                    ("<%s> MW: DUMMY_EVENT\n", pDevExt->PortName)
                );
                goto wait_for_completion;
            }

            case WRITE_COMPLETION_EVENT_INDEX:
            {
                // reset write completion event
                KeClearEvent(&pDevExt->WriteCompletionEvent);

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_ERROR,
                    ("<%s> MWT: error - unsupported COMPLETION\n", pDevExt->PortName)
                );

                break;
            }

            case WRITE_CANCEL_CURRENT_EVENT_INDEX:
            {
                PQCMWT_CXLREQ pCxlReq;
                PQCMWT_BUFFER pWtBuf;

                KeClearEvent(&pDevExt->WriteCancelCurrentEvent);
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> MW CANCEL_CURRENT-0", pDevExt->PortName)
                );

                QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

                while (!IsListEmpty(&pDevExt->MWriteCancellingQueue))
                {
                    headOfList = RemoveHeadList(&pDevExt->MWriteCancellingQueue);
                    pCxlReq = CONTAINING_RECORD
                    (
                        headOfList,
                        QCMWT_CXLREQ,
                        List
                    );
                    pWtBuf = pDevExt->pMwtBuffer[pCxlReq->Index];
                    if (pWtBuf->State == MWT_BUF_PENDING)
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_DETAIL,
                            ("<%s> MW CANCEL[%d]", pDevExt->PortName, pCxlReq->Index)
                        );
                        // reset the index field (to unused)
                        pCxlReq->Index = -1;

                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                        IoCancelIrp(pWtBuf->Irp);

                        QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
                    }
                    else
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_ERROR,
                            ("<%s> MW CANCEL[%d] - done elsewhere", pDevExt->PortName, pCxlReq->Index)
                        );
                        pCxlReq->Index = -1;
                    }
                }

                QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> MW CANCEL_CURRENT-1", pDevExt->PortName)
                );

                break;
            }

            case KICK_WRITE_EVENT_INDEX:
            {
                KeClearEvent(&pDevExt->KickWriteEvent);

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> MWT KICK\n", pDevExt->PortName)
                );

                continue;
            }

            case CANCEL_EVENT_INDEX:
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> MW CANCEL_EVENT_INDEX-0 mwt %u", pDevExt->PortName, mwtState)
                );

                // clear cancel event so we don't reactivate
                KeClearEvent(&pDevExt->CancelWriteEvent);

                pDevExt->bWriteStopped = TRUE;

#ifdef ENABLE_LOGGING
                QCUSB_LogData
                (
                    pDevExt,
                    pDevExt->hTxLogFile,
                    (PVOID)NULL,
                    0,
                    QCUSB_LOG_TYPE_CANCEL_THREAD
                );
#endif // ENABLE_LOGGING

                QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

                if (!IsListEmpty(&pDevExt->MWritePendingQueue))
                {
                    PLIST_ENTRY   firstEntry;

                    mwtState = MWT_STATE_CANCELLING;
                    headOfList = &pDevExt->MWritePendingQueue;
                    firstEntry = headOfList->Flink;
                    pWtBuf = CONTAINING_RECORD(firstEntry, QCMWT_BUFFER, List);

                    // Examine the buffer state because it could be cancelled before
                    // being sent to the bus driver.
                    if (pWtBuf->State == MWT_BUF_PENDING)
                    {
                        // cancel the first outstanding irp
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_DETAIL,
                            ("<%s> MWT: CANCEL - IRP 0x%p\n", pDevExt->PortName, pWtBuf->Irp)
                        );

                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                        IoCancelIrp(pWtBuf->Irp);
                    }
                    else
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_ERROR,
                            ("<%s> MWT: CANCEL err - IRP[%d] 0x%p not pending\n",
                            pDevExt->PortName, pWtBuf->Index, pWtBuf->Irp)
                        );
                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                    }

                    goto wait_for_completion;
                }
                else
                {
                    QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                }

                bCancelled = TRUE;

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> W CANCEL_EVENT_INDEX-1", pDevExt->PortName)
                );
                break; // goto exit_WriteThread; // nothing active exit
            }

            case WRITE_PURGE_EVENT_INDEX:
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> W PURGE_EVENT-0 mwt %u", pDevExt->PortName, mwtState)
                );

                // clear purge event so we don't reactivate
                KeClearEvent(&pDevExt->WritePurgeEvent);

                QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
                if (!IsListEmpty(&pDevExt->MWritePendingQueue))
                {
                    PLIST_ENTRY   firstEntry;

                    mwtState = MWT_STATE_PURGING;
                    headOfList = &pDevExt->MWritePendingQueue;
                    firstEntry = headOfList->Flink;
                    pWtBuf = CONTAINING_RECORD(firstEntry, QCMWT_BUFFER, List);

                    // Examine the buffer state because it could be cancelled before
                    // being sent to the bus driver.
                    if (pWtBuf->State == MWT_BUF_PENDING)
                    {
                        // cancel the first outstanding irp
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_DETAIL,
                            ("<%s> MWT: PURGE - IRP 0x%p\n", pDevExt->PortName, pWtBuf->Irp)
                        );

                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                        IoCancelIrp(pWtBuf->Irp);
                    }
                    else
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_ERROR,
                            ("<%s> MWT: PURGE err - IRP[%d] 0x%p not pending\n",
                            pDevExt->PortName, pWtBuf->Index, pWtBuf->Irp)
                        );
                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                    }

                    goto wait_for_completion;
                }
                else
                {
                    QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                }

                bPurged = TRUE;

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> W PURGE_EVENT-1", pDevExt->PortName)
                );
                break;
            }

            case WRITE_STOP_EVENT_INDEX:
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> W STOP_EVENT: mwt %u", pDevExt->PortName, mwtState)
                );
                KeClearEvent(&pDevExt->WriteStopEvent);
                pDevExt->bWriteStopped = TRUE;

                QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
                if (!IsListEmpty(&pDevExt->MWritePendingQueue))
                {
                    PLIST_ENTRY   firstEntry;

                    mwtState = MWT_STATE_STOPPING;
                    headOfList = &pDevExt->MWritePendingQueue;
                    firstEntry = headOfList->Flink;
                    pWtBuf = CONTAINING_RECORD(firstEntry, QCMWT_BUFFER, List);

                    if (pWtBuf->State == MWT_BUF_PENDING)
                    {
                        // cancel the first outstanding irp
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_DETAIL,
                            ("<%s> MWT: STOP - IRP[%d] 0x%p\n",
                            pDevExt->PortName, pWtBuf->Index, pWtBuf->Irp)
                        );
                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                        IoCancelIrp(pWtBuf->Irp);
                    }
                    else
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_ERROR,
                            ("<%s> MWT: STOP err - IRP[%d] 0x%p not pending\n",
                            pDevExt->PortName, pWtBuf->Index, pWtBuf->Irp)
                        );
                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                    }

                    goto wait_for_completion;
                }
                else
                {
                    QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                }

                KeSetEvent(&pDevExt->WriteStopAckEvent, IO_NO_INCREMENT, FALSE); // ???

                break;
            }

            case WRITE_RESUME_EVENT_INDEX:
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> W RESUME_EVENT rm %d", pDevExt->PortName, pDevExt->bDeviceRemoved)
                );
                KeClearEvent(&pDevExt->WriteResumeEvent);
                if (FALSE == (pDevExt->bWriteStopped = pDevExt->bDeviceRemoved))
                {
                    pDevExt->bWriteStopped = pDevExt->PowerSuspended;
                }
                break;
            }

            case WRITE_FLOW_ON_EVENT_INDEX:
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> W FLOW_ON_EVENT rm %d", pDevExt->PortName, pDevExt->bDeviceRemoved)
                );
                KeClearEvent(&pDevExt->WriteFlowOnEvent);

                if (FALSE == (pDevExt->bWriteStopped = pDevExt->bDeviceRemoved))
                {
                    pDevExt->bWriteStopped = pDevExt->PowerSuspended;
                }

                if (bSendZeroLength == FALSE)
                {
                    /**************
                    if (bFlowOn == FALSE)
                    {
                       NTSTATUS nts;

                       nts = QCUSB_ResetOutput(pDevExt->MyDeviceObject, QCUSB_RESET_PIPE_AND_ENDPOINT);
                       QCUSB_DbgPrint
                       (
                          QCUSB_DBG_MASK_WRITE,
                          QCUSB_DBG_LEVEL_DETAIL,
                          ("<%s> W FLOW_ON: reset OUT 0x%x", pDevExt->PortName, nts)
                       );
                    }
                    else
                    {
                       QCUSB_DbgPrint
                       (
                          QCUSB_DBG_MASK_WRITE,
                          QCUSB_DBG_LEVEL_DETAIL,
                          ("<%s> W FLOW_ON: already on, no act", pDevExt->PortName)
                       );
                    }
                    ****************/
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_DETAIL,
                        ("<%s> W FLOW_ON: no 0-len, no act", pDevExt->PortName)
                    );
                }
                else
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_DETAIL,
                        ("<%s> W FLOW_ON: resend 0-len, not to reset OUT", pDevExt->PortName)
                    );
                }
                bFlowOn = TRUE;

                break;
            }

            case WRITE_FLOW_OFF_EVENT_INDEX:
            {
                BOOLEAN bAbortPipe = FALSE;

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> W FLOW_OFF_EVENT: pending %d", pDevExt->PortName,
                    pDevExt->MWTPendingCnt)
                );
                KeClearEvent(&pDevExt->WriteFlowOffEvent);

                pDevExt->bWriteStopped = TRUE;
                bFlowOn = FALSE;
                if (FALSE == USBUTL_IsHighSpeedDevice(pDevExt))
                {
                    mwtState = MWT_STATE_FLOW_OFF;
                }
                else
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_DETAIL,
                        ("<%s> W FLOW_OFF: no cxl", pDevExt->PortName)
                    );
                    KeSetEvent(&pDevExt->WriteFlowOffAckEvent, IO_NO_INCREMENT, FALSE);
                    break;
                }

                QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
                if (!IsListEmpty(&pDevExt->MWritePendingQueue))
                {
                    bAbortPipe = TRUE;
                }
                else
                {
                    KeSetEvent(&pDevExt->WriteFlowOffAckEvent, IO_NO_INCREMENT, FALSE);
                }
                QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                if (bAbortPipe == TRUE)
                {
                    QCUSB_AbortOutput(pDevExt->MyDeviceObject);
                }
                else
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_DETAIL,
                        ("<%s> W FLOW_OFF: WPendingQ empty, no cancellation", pDevExt->PortName)
                    );
                }
                break;
            }

            default:
            {
                int           idx;
                PQCMWT_BUFFER pWtBuf;
                PLIST_ENTRY   peekEntry;

                if ((ntStatus < WRITE_EVENT_COUNT) || (ntStatus >= waitCount))
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> MWT: default sig 0x%x", pDevExt->PortName, ntStatus)
                    );
                    // log status
#ifdef ENABLE_LOGGING
                    QCUSB_LogData
                    (
                        pDevExt,
                        pDevExt->hTxLogFile,
                        (PVOID)&ntStatus,
                        sizeof(NTSTATUS),
                        QCUSB_LOG_TYPE_RESPONSE_WT
                    );
#endif // ENABLE_LOGGING

                    // Ignore for now
                    break;
                }

                // Multi-write completion
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_INFO,
                    ("<%s> MWT: COMPLETION 0x%x/0x%x/0x%x", pDevExt->PortName,
                    ntStatus, WRITE_EVENT_COUNT, waitCount)
                );

                QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

                // Sanity checking
                if (IsListEmpty(&pDevExt->MWriteCompletionQueue))
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> MWT: error-empty completion queue", pDevExt->PortName)
                    );
                    QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                    break;
                }

                // Peek the completed entry, do not de-queue here
                headOfList = &pDevExt->MWriteCompletionQueue;
                peekEntry = headOfList->Flink;
                pWtBuf = CONTAINING_RECORD(peekEntry, QCMWT_BUFFER, List);
                KeClearEvent(&pWtBuf->WtCompletionEvt);
                idx = pWtBuf->Index;
                pIrp = pWtBuf->Irp;
                pUrb = &pWtBuf->Urb;
                pDevExt->bWriteActive = (!IsListEmpty(&pDevExt->MWritePendingQueue));
                QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> MWT: COMP[%d/%d] 0x%p (0x%x) act %d\n", pDevExt->PortName, idx,
                    (ntStatus - WRITE_EVENT_COUNT),
                    pWtBuf->CallingIrp, pIrp->IoStatus.Status,
                    pDevExt->bWriteActive)
                );

                ntStatus = pIrp->IoStatus.Status;

                if (ntStatus == STATUS_SUCCESS)
                {
                    USBMAIN_UpdateXferStats(pDevExt, pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength, FALSE);
                }

#ifdef QCUSB_MUX_PROTOCOL
                USBMWT_BurstCheck(pDevExt, pWtBuf->Length, BURST_COMPLETION);
#endif

                // check completion status

                // log status
#ifdef ENABLE_LOGGING
                if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hTxLogFile != NULL))
                {
                    if (ntStatus == STATUS_SUCCESS)
                    {
                        QCUSB_LogData
                        (
                            pDevExt,
                            pDevExt->hTxLogFile,
                            (PVOID)(pUrb->UrbBulkOrInterruptTransfer.TransferBuffer),
                            pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                            QCUSB_LOG_TYPE_WRITE
                        );
                    }
                    else
                    {
                        QCUSB_LogData
                        (
                            pDevExt,
                            pDevExt->hTxLogFile,
                            (PVOID)&ntStatus,
                            sizeof(NTSTATUS),
                            QCUSB_LOG_TYPE_RESPONSE_WT
                        );
                    }
                }
#endif // ENABLE_LOGGING

                if (ntStatus == STATUS_SUCCESS)
                {
                    ULONG ulXferred = pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;

                    // have we written the full request yet?
                    if (ulXferred < pWtBuf->Length)
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_ERROR,
                            ("<%s> MWT: error[%d] - TX'ed %u/%uB\n", pDevExt->PortName, idx,
                            ulXferred, pWtBuf->Length)
                        );
                    }
                } // if STATUS_SUCCESS
                else // error???
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> MWT: TX failure 0x%x xferred %u/%u", pDevExt->PortName, ntStatus,
                        pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                        pWtBuf->Length)
                    );
                    if (pWtBuf->CallingIrp != 0)
                    {
                        if (pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength < pWtBuf->Length)
                        {
                            pWtBuf->CallingIrp->IoStatus.Information =
                                pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
                        }
                    }
                }

                // Reset pipe if halt, which runs at PASSIVE_LEVEL
                if ((ntStatus == STATUS_DEVICE_DATA_ERROR) ||
                    (ntStatus == STATUS_DEVICE_NOT_READY) ||
                    (ntStatus == STATUS_UNSUCCESSFUL))
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> MWT: resetting pipe OUT 0x%x", pDevExt->PortName, ntStatus)
                    );
                    if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
                    {
                        QCUSB_ResetOutput(pDevExt->MyDeviceObject, QCUSB_RESET_HOST_PIPE);
                        USBUTL_Wait(pDevExt, -(50 * 1000L)); // 5ms
                    }
                }

                if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
                {
                    ntStatus = STATUS_CANCELLED;
                }

                QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

                if (mwtState == MWT_STATE_FLOW_OFF)
                {
                    BOOLEAN bAllReturned;

                    // Mark the returned IRP
                    // If all returned, put the IRP(s) back to the data queue
                    // for re-transmission

                    bAllReturned = USBMWT_MarkAndCheckReturnedIrp(pDevExt, pWtBuf, FALSE, mwtState, ntStatus, pUrb);
                    if (bAllReturned == TRUE)
                    {
                        PLIST_ENTRY irpReturned;
                        PQCUSB_MWT_SENT_IRP irpRec;
                        PLIST_ENTRY headOfList, peekEntry;

                        // First, examine the head to see if it's a 0-len packet
                        if (!IsListEmpty(&pDevExt->MWTSentIrpQueue))
                        {
                            headOfList = &pDevExt->MWTSentIrpQueue;
                            peekEntry = headOfList->Flink;
                            irpRec = CONTAINING_RECORD(peekEntry, QCUSB_MWT_SENT_IRP, List);
                            if (irpRec->SentIrp == NULL)
                            {
                                bSendZeroLength = TRUE;

                                QCUSB_DbgPrint
                                (
                                    QCUSB_DBG_MASK_WRITE,
                                    QCUSB_DBG_LEVEL_DETAIL,
                                    ("<%s> MWT: flow off, 0-len detected [%u]\n",
                                    pDevExt->PortName, irpRec->MWTBuf->Index)
                                );
                            }
                            else
                            {
                                if ((irpRec->SentIrp->IoStatus.Information > 0) &&
                                    (irpRec->TotalLength > irpRec->SentIrp->IoStatus.Information))
                                {
                                    // It's possible that bSendZeroLength has been assigned TRUE
                                    // at this point because the last data IRP in the SentIrpQueue
                                    // contains data of multiple of 64/MAX-USB-TRANS bytes. In such
                                    // a case, we need to reset bSendZeroLength when the first IRP
                                    // was partially transferred.
                                    if (bSendZeroLength == TRUE)
                                    {
                                        bSendZeroLength = FALSE;
                                        QCUSB_DbgPrint
                                        (
                                            QCUSB_DBG_MASK_WRITE,
                                            QCUSB_DBG_LEVEL_DETAIL,
                                            ("<%s> MWT: flow off, partial transfer, reset 0-len flag\n",
                                            pDevExt->PortName)
                                        );
                                    }
                                }
                            }
                        }

                        while (!IsListEmpty(&pDevExt->MWTSentIrpQueue))
                        {
                            PIRP pIrp;

                            irpReturned = RemoveTailList(&pDevExt->MWTSentIrpQueue);
                            irpRec = CONTAINING_RECORD(irpReturned, QCUSB_MWT_SENT_IRP, List);
                            pIrp = irpRec->SentIrp;
                            if (pIrp != NULL)
                            {
                                QCUSB_DbgPrint
                                (
                                    QCUSB_DBG_MASK_WRITE,
                                    QCUSB_DBG_LEVEL_DETAIL,
                                    ("<%s> MWT: flow_off, re-q IRP[%d] 0x%p(%uB/%uB)\n",
                                    pDevExt->PortName, irpRec->MWTBuf->Index, pIrp,
                                    (irpRec->TotalLength - pIrp->IoStatus.Information),
                                    irpRec->TotalLength)
                                );
                                InsertHeadList(&pDevExt->WriteDataQueue, &pIrp->Tail.Overlay.ListEntry);
                                irpRec->SentIrp = NULL;
                            }
                            else
                            {
                                QCUSB_DbgPrint
                                (
                                    QCUSB_DBG_MASK_WRITE,
                                    QCUSB_DBG_LEVEL_DETAIL,
                                    ("<%s> MWT: flow_off, NUL irp, recycle sndRec[%d]\n",
                                    pDevExt->PortName, irpRec->MWTBuf->Index)
                                );
                            }
                            irpRec->MWTBuf = NULL;
                            InsertTailList(&pDevExt->MWTSentIrpRecordPool, &irpRec->List);
                        }
                    }
                }
                else
                {
                    // mark and remove the returned sent-irp
                    USBMWT_MarkAndCheckReturnedIrp(pDevExt, pWtBuf, TRUE, mwtState, ntStatus, pUrb);

                    // complete the IRP
                    USBMWT_WriteIrpCompletion
                    (
                        pDevExt, pWtBuf,
                        pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                        ntStatus, 7
                    );
                }
                pActiveBuffer = NULL;
                ulReqBytes = 0;
                pDevExt->pWriteCurrent = NULL;

                pWtBuf->Length = 0;
                pWtBuf->CallingIrp = NULL;

                // De-queue, completion => idle
                RemoveEntryList(&pWtBuf->List);
                InsertTailList(&pDevExt->MWriteIdleQueue, &pWtBuf->List);

                // State change: completed => idle
                pWtBuf->State = MWT_BUF_IDLE;

                if (mwtState == MWT_STATE_FLOW_OFF)
                {
                    if (IsListEmpty(&pDevExt->MWTSentIrpQueue))
                    {
                        mwtState = MWT_STATE_WORKING;
                        KeSetEvent(&pDevExt->WriteFlowOffAckEvent, IO_NO_INCREMENT, FALSE);
                    }
                    QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                    break;
                }

                if (mwtState != MWT_STATE_WORKING)
                {
                    if (!IsListEmpty(&pDevExt->MWritePendingQueue))
                    {
                        PLIST_ENTRY   firstEntry;

                        headOfList = &pDevExt->MWritePendingQueue;
                        firstEntry = headOfList->Flink;
                        pWtBuf = CONTAINING_RECORD(firstEntry, QCMWT_BUFFER, List);

                        // cancel the first outstanding irp
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_WRITE,
                            QCUSB_DBG_LEVEL_ERROR,
                            ("<%s> MWT_comp: CANCEL - IRP 0x%p\n", pDevExt->PortName, pWtBuf->Irp)
                        );
                        QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                        IoCancelIrp(pWtBuf->Irp);

                        goto wait_for_completion;
                    }
                    else
                    {
                        if (mwtState == MWT_STATE_STOPPING)
                        {
                            KeSetEvent(&pDevExt->WriteStopAckEvent, IO_NO_INCREMENT, FALSE);
                        }
                        else if (mwtState == MWT_STATE_CANCELLING)
                        {
                            bCancelled = TRUE;
                        }
                        else if (mwtState == MWT_STATE_PURGING)
                        {
                            KeSetEvent(&pDevExt->WritePurgeAckEvent, IO_NO_INCREMENT, FALSE);
                        }
                        mwtState = MWT_STATE_WORKING;
                    }
                }
                QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                break;
            }
        }  // switch

        // go round again
    }  // end while forever 

exit_WriteThread:

#ifdef QCUSB_MUX_PROTOCOL
    USBMWT_CancelBurstTimer(pDevExt);
#endif

    if (pDevExt->hTxLogFile != NULL)
    {
#ifdef ENABLE_LOGGING
        QCUSB_LogData
        (
            pDevExt,
            pDevExt->hTxLogFile,
            (PVOID)NULL,
            0,
            QCUSB_LOG_TYPE_THREAD_END
        );
#endif // ENABLE_LOGGING
        ZwClose(pDevExt->hTxLogFile);
        pDevExt->hTxLogFile = NULL;
    }

    if (pwbArray)
    {
        _ExFreePool(pwbArray);
    }
    // Empty queue so that items can be added next time
    QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
    while (!IsListEmpty(&pDevExt->MWTSentIrpRecordPool))
    {
        irpSent = RemoveHeadList(&pDevExt->MWTSentIrpRecordPool);
    }
    QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

    KeSetEvent
    (
        &pDevExt->WriteThreadClosedEvent,
        IO_NO_INCREMENT,
        FALSE
    ); // signal write thread closed

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> Wth: OUT, RmlCount[0]=%d\n", pDevExt->PortName, pDevExt->Sts.lRmlCount[0])
    );

    PsTerminateSystemThread(STATUS_SUCCESS); // end this thread
}  // USBMWT_WriteThread

NTSTATUS USBMWT_InitializeMultiWriteElements(PDEVICE_EXTENSION pDevExt)
{
    int           i;
    PQCMWT_BUFFER pWtBuf;

    if (pDevExt->bFdoReused == TRUE)
    {
        return STATUS_SUCCESS;
    }

    InitializeListHead(&pDevExt->MWriteIdleQueue);
    InitializeListHead(&pDevExt->MWritePendingQueue);
    InitializeListHead(&pDevExt->MWriteCompletionQueue);
    InitializeListHead(&pDevExt->MWriteCancellingQueue);

    KeInitializeEvent(&pDevExt->WriteStopAckEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&pDevExt->WritePurgeAckEvent, NotificationEvent, FALSE);

    if (pDevExt->QosSetting >= 3)
    {
        pDevExt->NumberOfMultiWrites = QCUSB_MULTI_WRITE_BUFFERS; //must < QCUSB_MAX_MWT_BUF_COUNT
    }
    else
    {
        pDevExt->NumberOfMultiWrites = QCUSB_MULTI_WRITE_BUFFERS_DEFAULT;
    }
    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_CRITICAL,
        ("<%s> MWT-init: %d elements\n", pDevExt->PortName, pDevExt->NumberOfMultiWrites)
    );

    for (i = 0; i < pDevExt->NumberOfMultiWrites; i++)
    {
        pWtBuf = ExAllocatePool(NonPagedPool, sizeof(QCMWT_BUFFER));
        if (pWtBuf == NULL)
        {
            goto MWT_InitExit;
        }
        else
        {
            pWtBuf->CallingIrp = NULL;
            pWtBuf->Irp = IoAllocateIrp
            (
                (CCHAR)(pDevExt->MyDeviceObject->StackSize),
                FALSE
            );
            if (pWtBuf->Irp == NULL)
            {
                ExFreePool(pWtBuf);
                goto MWT_InitExit;
            }

            pDevExt->pMwtBuffer[i] = pWtBuf;
            pWtBuf->DeviceExtension = pDevExt;
            pWtBuf->Length = 0;
            pWtBuf->Index = i;
            pWtBuf->State = MWT_BUF_IDLE;
            KeInitializeEvent
            (
                &pWtBuf->WtCompletionEvt,
                NotificationEvent, FALSE
            );

            InsertTailList(&pDevExt->MWriteIdleQueue, &pWtBuf->List);
            pDevExt->pWriteEvents[WRITE_EVENT_COUNT + i] = &(pWtBuf->WtCompletionEvt);
        }
    }

    // Initialize elements for MWT req to be cancelled

    for (i = 0; i < QCUSB_MAX_MWT_BUF_COUNT; i++)
    {
        pDevExt->CxlRequest[i].Index = -1;
    }

    return STATUS_SUCCESS;

MWT_InitExit:

    pDevExt->NumberOfMultiWrites = i;

    if (i == 0)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> MWT-init: ERR - NO_MEMORY\n", pDevExt->PortName)
        );
        return STATUS_NO_MEMORY;
    }
    else
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> MWT-init: WRN - degrade to %d\n", pDevExt->PortName, i)
        );
        return STATUS_SUCCESS;
    }

}  // USBMWT_InitializeMultiWriteElements

NTSTATUS USBMWT_WriteIrpCompletion
(
    PDEVICE_EXTENSION DeviceExtension,
    PQCMWT_BUFFER     WriteItem,
    ULONG             TransferredBytes,
    NTSTATUS          Status,
    UCHAR             Cookie
)
{
    PDEVICE_EXTENSION pDevExt = DeviceExtension;
    PQCMWT_BUFFER     pWtBuf = WriteItem;
    PIRP pIrp;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    pIrp = pWtBuf->CallingIrp;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> MWIC<%d>: IRP 0x%p (0x%x)\n", pDevExt->PortName, Cookie, pIrp, Status)
    );


    if (NT_SUCCESS(Status) || (Status == STATUS_CANCELLED))
    {
        pDevExt->WtErrorCount = 0;
    }
    else
    {
        pDevExt->WtErrorCount++;

        // after some magic number of times of failure,
        // we mark the device as 'removed'
        if ((pDevExt->WtErrorCount > pDevExt->NumOfRetriesOnError) && (pDevExt->ContinueOnDataError == FALSE))
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WRITE,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> MWIC: failure %d, dev removed\n", pDevExt->PortName, pDevExt->WtErrorCount)
            );
            clearDevState(DEVICE_STATE_PRESENT);
            pDevExt->bWriteStopped = TRUE;
            pDevExt->WtErrorCount = pDevExt->NumOfRetriesOnError;
            USBWT_SetStopState(pDevExt, TRUE);  // non-blocking
        }
    }

    if (pIrp != NULL) // if it wasn't canceled out of the block
    {
        if (!IoSetCancelRoutine(pIrp, NULL))
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WIRP,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> MWIRPC: WIRP 0x%p already cxled\n", pDevExt->PortName, pIrp)
            );
            goto ExitWriteCompletion;
        }

        pIrp->IoStatus.Information += TransferredBytes;
        pIrp->IoStatus.Status = Status;

        InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
        KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);

    }
    else
    {
        if (pWtBuf->Length != 0)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WRITE,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> MWIC: error NUL IRP (%uB)-%d\n", pDevExt->PortName, pWtBuf->Length, Cookie)
            );
        }
        else
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WRITE,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> MWIC: 0-len done-%d\n", pDevExt->PortName, Cookie)
            );
        }
    }

ExitWriteCompletion:

    return Status;
}  // USBMWT_WriteIrpCompletion

VOID USBMWT_CancelWriteRoutine(PDEVICE_OBJECT DeviceObject, PIRP pIrp)
{
    KIRQL irql = KeGetCurrentIrql();
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    BOOLEAN bIrpInQueue;
    PQCMWT_BUFFER pWtBuf = NULL;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WIRP,
        QCUSB_DBG_LEVEL_ERROR,
        ("<%s> CancelWriteRoutine-0 IRP 0x%p\n", pDevExt->PortName, pIrp)
    );
    IoReleaseCancelSpinLock(pIrp->CancelIrql);

    QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

    bIrpInQueue = USBUTL_IsIrpInQueue
    (
        pDevExt,
        &pDevExt->WriteDataQueue,
        pIrp,
        QCUSB_IRP_TYPE_WIRP,
        2
    );

    if (bIrpInQueue == TRUE)
    {
        RemoveEntryList(&pIrp->Tail.Overlay.ListEntry);
        pIrp->IoStatus.Status = STATUS_CANCELLED;
        pIrp->IoStatus.Information = 0;
        InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
        KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);
    }
    else
    {
        pWtBuf = USBMWT_IsIrpPending(pDevExt, pIrp);

        if (pWtBuf == NULL)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WIRP,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> MWT_CxlW: no buf for IRP 0x%p, simply complete\n", pDevExt->PortName, pIrp)
            );

            pIrp->IoStatus.Status = STATUS_CANCELLED;
            pIrp->IoStatus.Information = 0;
            InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
            KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);
        }
        else
        {
            // Cancel the current
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WIRP,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> MWT_CxlW: cxl curr W[%d]\n", pDevExt->PortName, pWtBuf->Index)
            );
            pDevExt->CxlRequest[pWtBuf->Index].Index = pWtBuf->Index;
            InsertTailList
            (
                &pDevExt->MWriteCancellingQueue,
                &(pDevExt->CxlRequest[pWtBuf->Index].List)
            );

            IoSetCancelRoutine(pIrp, USBMWT_CancelWriteRoutine);  // restore the cxl routine
            KeSetEvent(&pDevExt->WriteCancelCurrentEvent, IO_NO_INCREMENT, FALSE);
            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WIRP,
                QCUSB_DBG_LEVEL_TRACE,
                ("<%s> <--MWT_CancelWriteRoutine IRP 0x%p\n", pDevExt->PortName, pIrp)
            );
            return;
        }
    }

    // If the IRP is not in IO queue or not active, then it should be in
    // the completion queue by now, and we are fine.

    QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
}  // USBMWT_CancelWriteRoutine

PQCMWT_BUFFER USBMWT_IsIrpPending
(
    PDEVICE_EXTENSION pDevExt,
    PIRP              Irp
)
{
    PLIST_ENTRY     headOfList, peekEntry;
    PQCMWT_BUFFER   pWtBuf = NULL;
    BOOLEAN         enqueueResult = FALSE;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> -->MWT_IsIrpPending\n", pDevExt->PortName)
    );

    // Examine MWritePendingQueue
    if (!IsListEmpty(&pDevExt->MWritePendingQueue))
    {
        headOfList = &pDevExt->MWritePendingQueue;
        peekEntry = headOfList->Flink;

        while (peekEntry != headOfList)
        {
            pWtBuf = CONTAINING_RECORD
            (
                peekEntry,
                QCMWT_BUFFER,
                List
            );
            if ((pWtBuf->CallingIrp == Irp) && (pWtBuf->State == MWT_BUF_PENDING))
            {
                break;
            }
            peekEntry = peekEntry->Flink;
            pWtBuf = NULL;  // need to reset
        }
    }

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> <--MWT_IsIrpPending\n", pDevExt->PortName)
    );

    return pWtBuf;
}  // USBMWT_IsIrpPending

BOOLEAN USBMWT_MarkAndCheckReturnedIrp
(
    PDEVICE_EXTENSION pDevExt,
    PQCMWT_BUFFER     MWTBuf,
    BOOLEAN           RemoveIrp,
    MWT_STATE         OperationState,
    NTSTATUS          Status,
    PURB              Urb
)
{
    PLIST_ENTRY         irpSent, headOfList;
    PQCUSB_MWT_SENT_IRP irpBlock, irpReturned = NULL;
    BOOLEAN             bAllReturned = TRUE;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> --> MarkAndCheckReturnedIrp[%u]-%d ST %u\n", pDevExt->PortName,
        MWTBuf->Index, RemoveIrp, OperationState)
    );
    // Peek the queue
    headOfList = &pDevExt->MWTSentIrpQueue;
    irpSent = headOfList->Flink;

    while (irpSent != headOfList)
    {
        irpBlock = CONTAINING_RECORD(irpSent, QCUSB_MWT_SENT_IRP, List);
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> MarkAndCheckReturnedIrp: chking [%u, %u] 0x%p/%uB\n", pDevExt->PortName,
            irpBlock->MWTBuf->Index, irpBlock->IrpReturned, irpBlock->SentIrp,
            irpBlock->TotalLength)
        );

        if (irpBlock->MWTBuf == MWTBuf)
        {
            irpBlock->IrpReturned = TRUE;
            irpReturned = irpBlock;
        }
        else
        {
            if (irpBlock->IrpReturned != TRUE)
            {
                bAllReturned = FALSE;
            }
        }

        irpSent = irpSent->Flink;
    }

    if (irpReturned != NULL)
    {
        BOOLEAN completeIrp = FALSE;

        if ((RemoveIrp == TRUE) || (NT_SUCCESS(Status)))
        {
            if (RemoveIrp == FALSE)
            {
                completeIrp = TRUE;
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_WRITE,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> MarkAndCheckReturnedIrp: comp under FLOW_OFF: st 0x%x\n",
                    pDevExt->PortName, Status)
                );
            }

            irpReturned->MWTBuf = NULL;
            RemoveEntryList(&irpReturned->List);
            InsertTailList(&pDevExt->MWTSentIrpRecordPool, &irpReturned->List);
        }

        if (completeIrp == TRUE)  // should only during FLOW_OFF
        {
            USBMWT_WriteIrpCompletion
            (
                pDevExt, MWTBuf,
                Urb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                Status, 8
            );
        }
    }
    else
    {
        // error
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> MarkAndCheckReturnedIrp: error - no returned IRP\n", pDevExt->PortName)
        );
    }

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> <-- MarkAndCheckReturnedIrp: %d\n", pDevExt->PortName, bAllReturned)
    );

    return bAllReturned;

}  // USBMWT_MarkAndCheckReturnedIrp

#ifdef QCUSB_MUX_PROTOCOL
// start of BURST_SUPPORT
VOID USBMWT_InitializeBurstSettings(PDEVICE_EXTENSION pDevExt)
{
    NTSTATUS ntStatus;
    HANDLE   hRegKey = NULL;
    UNICODE_STRING ucValueName;
    ULONG          ulReturnLength;

#define MWT_BURST_INTERVAL 0
#define MWT_BURST_SIZE     0
#define MWT_BURST_INTERVAL_ENTRY L"QCDriverBurstInterval"
#define MWT_BURST_SIZE_ENTRY     L"QCDriverBurstSize"
#define BURST_FASHION_EQUAL_GAP      0
#define BURST_FASHION_EQUAL_INTERVAL 1

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> -->USBMWT_InitializeBurstSettings\n", pDevExt->PortName)
    );
    KeInitializeTimer(&pDevExt->BurstTimer);
    KeInitializeDpc(&pDevExt->BurstDpc, USBMWT_BurstDpcRoutine, pDevExt);
    pDevExt->BurstFashion = BURST_FASHION_EQUAL_GAP;
    pDevExt->BurstPendingCount = 0;
    pDevExt->BurstCount = 0;
    pDevExt->BurstTimerLaunched = FALSE;

    // need to retrieve from registry
    ntStatus = IoOpenDeviceRegistryKey
    (
        pDevExt->PhysicalDeviceObject,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_ALL_ACCESS,
        &hRegKey
    );
    if (!NT_SUCCESS(ntStatus))
    {
        pDevExt->BurstInterval = MWT_BURST_INTERVAL;
        pDevExt->BurstSize = MWT_BURST_SIZE;
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> <--USBMWT_InitializeBurstSettings, reg access failure, use default (%dms, 0x%XB)\n",
            pDevExt->PortName, pDevExt->BurstInterval, pDevExt->BurstSize)
        );

        return;
    }

    // get burst interval
    RtlInitUnicodeString(&ucValueName, MWT_BURST_INTERVAL_ENTRY);
    ntStatus = getRegDwValueEntryData
    (
        hRegKey,
        ucValueName.Buffer,
        &pDevExt->BurstInterval
    );

    if (ntStatus != STATUS_SUCCESS)
    {
        pDevExt->BurstInterval = MWT_BURST_INTERVAL;
    }
    else
    {
        if ((pDevExt->BurstInterval & 0x80000000) != 0)
        {
            pDevExt->BurstFashion = BURST_FASHION_EQUAL_INTERVAL;
            pDevExt->BurstInterval &= 0x0FFFFFFF;
        }
        if (pDevExt->BurstInterval > 1000)
        {
            pDevExt->BurstInterval = 1000;
        }
    }

    // get burst size
    RtlInitUnicodeString(&ucValueName, MWT_BURST_SIZE_ENTRY);
    ntStatus = getRegDwValueEntryData
    (
        hRegKey,
        ucValueName.Buffer,
        &pDevExt->BurstSize
    );

    if (ntStatus != STATUS_SUCCESS)
    {
        pDevExt->BurstSize = MWT_BURST_SIZE;
    }
    else if (pDevExt->BurstSize < 1024 * 256)
    {
        pDevExt->BurstSize = 1024 * 256;
    }
    else if (pDevExt->BurstSize > 1024 * 1024 * 5)
    {
        pDevExt->BurstSize = 1024 * 1024 * 5;
    }

    _closeRegKey(hRegKey, "MWT-1");

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> <--USBMWT_InitializeBurstSettings: %dms, 0x%XB\n", pDevExt->PortName,
        pDevExt->BurstInterval, pDevExt->BurstSize)
    );
}  // USBMWT_InitializeBurstSettings

VOID USBMWT_BurstCheck(PDEVICE_EXTENSION pDevExt, ULONG TransferSize, INT SendOrCompletion)
{
    LARGE_INTEGER dueTime;
    NTSTATUS ntStatus;

    if ((pDevExt->BurstSize == 0) || (pDevExt->BurstInterval == 0))
    {
        // burst not enabled
        return;
    }

    // update BurstCount whenever a transfer is made
    if (SendOrCompletion == BURST_SEND)
    {
        pDevExt->BurstPendingCount += TransferSize;
        if (pDevExt->BurstPendingCount >= pDevExt->BurstSize)  // to stop sending to bus
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_PIRP,
                QCUSB_DBG_LEVEL_TRACE,
                ("<%s> BurstCheck: enough pending, stop flow %u/%u\n", pDevExt->PortName, pDevExt->BurstPendingCount, pDevExt->BurstSize)
            );
            pDevExt->bWriteStopped = TRUE;
            pDevExt->BurstPendingCount = 0;
        }

        if (pDevExt->BurstFashion != 0)  // EQUAL_INTERVAL
        {
            if (pDevExt->BurstTimerLaunched == FALSE)
            {
                QcInterlockedIncrement(3, &pDevExt->BurstTimer, 3);
                dueTime.QuadPart = (LONGLONG)(-10000) * pDevExt->BurstInterval;
                pDevExt->BurstTimerLaunched = TRUE;
                KeSetTimer(&pDevExt->BurstTimer, dueTime, &pDevExt->BurstDpc);

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_PIRP,
                    QCUSB_DBG_LEVEL_TRACE,
                    ("<%s> BurstCheck: EQUAL_INTERVAL timer started %u/%u\n", pDevExt->PortName, pDevExt->BurstPendingCount, pDevExt->BurstSize)
                );
            }
        }

        return;
    }

    // case of BURST_COMPLETION
    if (pDevExt->BurstFashion != 0)  // EQUAL_INTERVAL
    {
        // no checking on completion for EQUAL_INTERVAL
        return;
    }

    pDevExt->BurstCount += TransferSize;
    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_PIRP,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> BurstCheck: completion %u/%u\n", pDevExt->PortName, pDevExt->BurstCount, pDevExt->BurstSize)
    );

    if (pDevExt->BurstCount >= pDevExt->BurstSize)  // end of burst (==BurstPendingCount)
    {
        // Set timer
        ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, (PVOID)&pDevExt->BurstTimer);
        if (NT_SUCCESS(ntStatus))
        {
            // transit to accumulation phase
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_WRITE,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> USBMWT_BurstCheck: sent %dB, pause for data accumulation\n",
                pDevExt->PortName, pDevExt->BurstCount)
            );
            QcInterlockedIncrement(3, &pDevExt->BurstTimer, 3);
            dueTime.QuadPart = (LONGLONG)(-10000) * pDevExt->BurstInterval;
            pDevExt->BurstTimerLaunched = TRUE;
            KeSetTimer(&pDevExt->BurstTimer, dueTime, &pDevExt->BurstDpc);
            pDevExt->BurstCount = 0;
        }
        else
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_PIRP,
                QCUSB_DBG_LEVEL_TRACE,
                ("<%s> BurstCheck: IoAcquireRemoveLock failure\n", pDevExt->PortName)
            );
        }
    }
}  // USBMWT_BurstCheck

VOID USBMWT_BurstDpcRoutine
(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2
)
{
    PDEVICE_EXTENSION      pDevExt;

    pDevExt = (PDEVICE_EXTENSION)DeferredContext;
    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_PIRP,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> BurstDpcRoutine: context 0x%p, start to burst\n", pDevExt->PortName, DeferredContext)
    );

    // transit to bursting phase
    pDevExt->bWriteStopped = FALSE;
    KeSetEvent(&pDevExt->KickWriteEvent, IO_NO_INCREMENT, FALSE);

    if (pDevExt->BurstFashion != 0)  // EQUAL_INTERVAL
    {
        // set the flag so the timer could be triggered from SEND again
        pDevExt->BurstTimerLaunched = FALSE;
    }
    QcIoReleaseRemoveLock(pDevExt->pRemoveLock, (PVOID)&pDevExt->BurstTimer, 3);

}  // USBMWT_BurstDpcRoutine

VOID USBMWT_CancelBurstTimer(PDEVICE_EXTENSION pDevExt)
{
    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_WRITE,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> USBMWT_CancelBurstTimer: %dms, 0x%XB\n", pDevExt->PortName,
        pDevExt->BurstInterval, pDevExt->BurstSize)
    );

    if ((pDevExt->BurstSize == 0) || (pDevExt->BurstInterval == 0))
    {
        // burst not enabled
        return;
    }

    if (KeCancelTimer(&pDevExt->BurstTimer) == TRUE)
    {
        QcIoReleaseRemoveLock(pDevExt->pRemoveLock, (PVOID)&pDevExt->BurstTimer, 3);
    }
}  // USBMWT_CancelBurstTimer
// end of BURST_SUPPORT
#endif // QCUSB_MUX_PROTOCOL
