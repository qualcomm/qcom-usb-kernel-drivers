/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                            U S B M R D . C

GENERAL DESCRIPTION
    This file contains implementation for reading data over USB.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "USBRD.h"
#include "USBUTL.h"
#include "USBENC.h"
#include "USBPWR.h"
#include "USBIF.h"


// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBMRD.tmh"

#endif   // EVENT_TRACING

extern USHORT ucThCnt;

#undef QCOM_TRACE_IN

// The following protypes are implemented in ntoskrnl.lib
extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);

void USBMRD_L1MultiReadThread(PVOID pContext)
{
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pContext;
    BOOLEAN           bCancelled = FALSE;
    NTSTATUS          ntStatus;
    BOOLEAN           bFirstTime = TRUE;
    BOOLEAN           bNotDone = TRUE;
    PKWAIT_BLOCK      pwbArray;
    OBJECT_ATTRIBUTES objAttr;
    KEVENT            dummyEvent;
    int               i, oldFillIdx;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    pDevExt->TlpBufferState = QCTLP_BUF_STATE_IDLE;
    pDevExt->bL1Stopped = FALSE;

    // allocate a wait block array for the multiple wait
    pwbArray = _ExAllocatePool
    (
        NonPagedPool,
        (L1_READ_EVENT_COUNT + 1) * sizeof(KWAIT_BLOCK),
        "USBRD_ReadThread.pwbArray"
    );
    if (!pwbArray)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> L1: STATUS_NO_MEMORY 01\n", pDevExt->PortName)
        );
        _closeHandle(pDevExt->hL1ReadThreadHandle, "L1R-0");
        KeSetEvent(&pDevExt->ReadThreadStartedEvent, IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_NO_MEMORY);
    }

    // Set L1 thread priority
    //KeSetPriorityThread(KeGetCurrentThread(), QCUSB_L1_PRIORITY);
    if (gVendorConfig.ThreadPriority > 10)
    {
        KeSetPriorityThread(KeGetCurrentThread(), gVendorConfig.ThreadPriority - 1);
    }

    // Reset L2 buffer
    USBMRD_ResetL2Buffers(pDevExt);
    USBMRD_RecycleFrameBuffer(pDevExt);

    pDevExt->bL1ReadActive = FALSE;

    pDevExt->pL1ReadEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
    KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
    KeClearEvent(&dummyEvent);
    KeClearEvent(&pDevExt->L1ReadPurgeEvent);

    while (bNotDone)
    {
        QCUSB_DbgPrint2
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> L1 checking rd Q\n", pDevExt->PortName)
        );


        // check any status' which would ask us to drain que
        if (pDevExt->bL1Stopped == TRUE)
        {
            if (bCancelled == TRUE)
            {
                bNotDone = FALSE;
                continue;
            }
            goto wait_for_completion2;

        }  // if (pDevExt->bL1Stopped == TRUE)

wait_for_completion:

        if (bFirstTime == TRUE)
        {
            bFirstTime = FALSE;

            // To ensure when L2 runs, the L1 has already been waiting for data
            if ((pDevExt->hL2ReadThreadHandle == NULL) && (pDevExt->pL2ReadThread == NULL))
            {
                // Start the L2 read thread
                KeClearEvent(&pDevExt->L2ReadThreadStartedEvent);
                InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
                ucThCnt++;
                ntStatus = PsCreateSystemThread
                (
                    OUT & pDevExt->hL2ReadThreadHandle,
                    IN THREAD_ALL_ACCESS,
                    IN & objAttr, // POBJECT_ATTRIBUTES
                    IN NULL,     // HANDLE  ProcessHandle
                    OUT NULL,    // PCLIENT_ID  ClientId
                    IN(PKSTART_ROUTINE)USBMRD_L2MultiReadThread,
                    IN(PVOID) pDevExt
                );
                if ((!NT_SUCCESS(ntStatus)) || (pDevExt->hL2ReadThreadHandle == NULL))
                {
                    pDevExt->hL2ReadThreadHandle = NULL;
                    pDevExt->pL2ReadThread = NULL;
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_CRITICAL,
                        ("<%s> L1: L2 th failure\n", pDevExt->PortName)
                    );
                    KeSetEvent(&pDevExt->ReadThreadStartedEvent, IO_NO_INCREMENT, FALSE);
                    PsTerminateSystemThread(STATUS_NO_MEMORY);
                }
                ntStatus = KeWaitForSingleObject
                (
                    &pDevExt->L2ReadThreadStartedEvent,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL
                );

                ntStatus = ObReferenceObjectByHandle
                (
                    pDevExt->hL2ReadThreadHandle,
                    THREAD_ALL_ACCESS,
                    NULL,
                    KernelMode,
                    (PVOID *)&pDevExt->pL2ReadThread,
                    NULL
                );
                if (!NT_SUCCESS(ntStatus))
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_CRITICAL,
                        ("<%s> L2: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
                    );
                    pDevExt->pL2ReadThread = NULL;
                }
                else
                {
                    if (!NT_SUCCESS(ntStatus = ZwClose(pDevExt->hL2ReadThreadHandle)))
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_READ,
                            QCUSB_DBG_LEVEL_DETAIL,
                            ("<%s> L2 ZwClose failed - 0x%x\n", pDevExt->PortName, ntStatus)
                        );
                    }
                    else
                    {
                        ucThCnt--;
                        pDevExt->hL2ReadThreadHandle = NULL;
                    }
                }

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L2 handle=0x%p thOb=0x%p\n", pDevExt->PortName,
                    pDevExt->hL2ReadThreadHandle, pDevExt->pL2ReadThread)
                );

            }  // end of creating L2 thread

            // inform read function that we've started
            KeSetEvent(&pDevExt->ReadThreadStartedEvent, IO_NO_INCREMENT, FALSE);
        }  // if (bFirstTime == TRUE)

wait_for_completion2:

        if ((pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].State == L2BUF_STATE_COMPLETED) &&
            (pDevExt->bL1Stopped == FALSE))
        {
            // No MUXing enabled
            if (pDevExt->MuxInterface.MuxEnabled == 0x00)
            {
                QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
                if (IsListEmpty(&pDevExt->ReadDataQueue))
                {
                    QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
                    ntStatus = KeWaitForMultipleObjects
                    (
                        L1_READ_EVENT_COUNT,
                        (VOID **)&pDevExt->pL1ReadEvents,
                        WaitAny,
                        Executive,
                        KernelMode,
                        FALSE, // non-alertable // TRUE,
                        NULL,
                        pwbArray
                    );
                }
                else
                {
                    QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_INFO,
                        ("<%s> L1 direct completion[%u](0x%x)\n", pDevExt->PortName,
                        pDevExt->L2FillIdx, pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status)
                    );
                    ntStatus = L1_READ_COMPLETION_EVENT_INDEX;
                }
            }
            else
            {
                if (IsEmptyAllReadQueue(pDevExt) == TRUE)
                {
                    ntStatus = KeWaitForMultipleObjects
                    (
                        L1_READ_EVENT_COUNT,
                        (VOID **)&pDevExt->pL1ReadEvents,
                        WaitAny,
                        Executive,
                        KernelMode,
                        FALSE, // non-alertable // TRUE,
                        NULL,
                        pwbArray
                    );
                }
                else
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_INFO,
                        ("<%s> L1 direct completion[%u](0x%x)\n", pDevExt->PortName,
                        pDevExt->L2FillIdx, pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status)
                    );
                    ntStatus = L1_READ_COMPLETION_EVENT_INDEX;
                }
            }
        }
        else
        {
            ntStatus = KeWaitForMultipleObjects
            (
                L1_READ_EVENT_COUNT,
                (VOID **)&pDevExt->pL1ReadEvents,
                WaitAny,
                Executive,
                KernelMode,
                FALSE, // non-alertable // TRUE,
                NULL,
                pwbArray
            );
        }

        switch (ntStatus)
        {
            case QC_DUMMY_EVENT_INDEX:
            {
                KeClearEvent(&dummyEvent);
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_ERROR,
                    ("<%s> L1: DUMMY_EVENT\n", pDevExt->PortName)
                );
                goto wait_for_completion;
            }
            case L1_READ_COMPLETION_EVENT_INDEX:
            {
                KeClearEvent(&pDevExt->L1ReadCompletionEvent);

                if (pDevExt->bL1Stopped == FALSE)
                {
                    USBMRD_FillReadRequest(pDevExt, 1);
                }

                break;
            }

            case L1_KICK_READ_EVENT_INDEX:
            {
                KeClearEvent(&pDevExt->L1KickReadEvent);

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L1 R kicked, active=%d\n", pDevExt->PortName, pDevExt->bL1ReadActive)
                );

                if (pDevExt->bL1Stopped == FALSE)
                {
                    // kick L2 thread
                    KeSetEvent
                    (
                        pDevExt->pL2ReadEvents[L2_KICK_READ_EVENT_INDEX],
                        IO_NO_INCREMENT,
                        FALSE
                    ); // kick the read thread

                    USBMRD_FillReadRequest(pDevExt, 2);
                }
                break;
            }

            case L1_CANCEL_EVENT_INDEX:
            {
                pDevExt->bL1InCancellation = TRUE;
                pDevExt->bL1Stopped = TRUE;

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L1_CANCEL_EVENT_INDEX\n", pDevExt->PortName)
                );

                // reset read cancel event so we dont reactive
                KeClearEvent(&pDevExt->L1CancelReadEvent); // never set

                // Alert L2 thread
                if ((pDevExt->hL2ReadThreadHandle != NULL) || (pDevExt->pL2ReadThread != NULL))
                {
                    KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
                    KeSetEvent
                    (
                        &pDevExt->CancelReadEvent,
                        IO_NO_INCREMENT,
                        FALSE
                    );

                    if (pDevExt->pL2ReadThread != NULL)
                    {
                        KeWaitForSingleObject
                        (
                            pDevExt->pL2ReadThread,
                            Executive,
                            KernelMode,
                            FALSE,
                            NULL
                        );
                        ObDereferenceObject(pDevExt->pL2ReadThread);
                        KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
                        _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-7");
                        pDevExt->pL2ReadThread = NULL;
                    }
                    else  // best effort
                    {
                        KeWaitForSingleObject
                        (
                            &pDevExt->L2ReadThreadClosedEvent,
                            Executive,
                            KernelMode,
                            FALSE,
                            NULL
                        );
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_READ,
                            QCUSB_DBG_LEVEL_DETAIL,
                            ("<%s> L1R cxl: L2 OUT\n", pDevExt->PortName)
                        );
                        KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
                        _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-8");
                    }
                }

                bCancelled = TRUE; // singnal the loop that a cancel has occurred
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L1_CANCEL_EVENT_INDEX END\n", pDevExt->PortName)
                );
                break;
            }

            case L1_READ_PURGE_EVENT_INDEX:
            {
                KeClearEvent(&pDevExt->L1ReadPurgeEvent);

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L1_READ_PURGE_EVENT\n", pDevExt->PortName)
                );

                USBUTL_PurgeQueue
                (
                    pDevExt,
                    &pDevExt->ReadDataQueue,
                    &pDevExt->ReadSpinLock,
                    QCUSB_IRP_TYPE_RIRP,
                    1
                );

                // Purge READ_CONTROL items
                USBENC_PurgeQueue(pDevExt, &pDevExt->EncapsulatedDataQueue, FALSE, 0);

                // Purge SEND_CONTROL items
                KeClearEvent(&pDevExt->DispatchPurgeCompleteEvent);
                KeSetEvent(&pDevExt->DispatchPurgeEvent, IO_NO_INCREMENT, FALSE);
                KeWaitForSingleObject
                (
                    &pDevExt->DispatchPurgeCompleteEvent,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL
                );

                USBUTL_CheckPurgeStatus(pDevExt, (USB_PURGE_RD | USB_PURGE_EC));

                break;
            }

            case L1_STOP_EVENT_INDEX:
            {
                KeClearEvent(&pDevExt->L1StopEvent);
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L1_STOP_EVENT\n", pDevExt->PortName)
                );
                pDevExt->bL1Stopped = TRUE;

                // signal L2
                KeClearEvent(&pDevExt->L2StopAckEvent);
                KeSetEvent(&pDevExt->L2StopEvent, IO_NO_INCREMENT, FALSE);
                KeWaitForSingleObject
                (
                    &pDevExt->L2StopAckEvent,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL
                );
                KeClearEvent(&pDevExt->L2StopAckEvent);

                // Ack the caller
                KeSetEvent(&pDevExt->L1StopAckEvent, IO_NO_INCREMENT, FALSE);
                break;
            }

            case L1_RESUME_EVENT_INDEX:
            {
                KeClearEvent(&pDevExt->L1ResumeEvent);
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L1_RESUME_EVENT rm %d\n", pDevExt->PortName, pDevExt->bDeviceRemoved)
                );
                pDevExt->bL1Stopped = pDevExt->bDeviceRemoved;
                pDevExt->InputPipeStatus = STATUS_SUCCESS;

                // signal L2
                KeClearEvent(&pDevExt->L2ResumeAckEvent);
                KeSetEvent(&pDevExt->L2ResumeEvent, IO_NO_INCREMENT, FALSE);
                KeWaitForSingleObject
                (
                    &pDevExt->L2ResumeAckEvent,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL
                );
                KeClearEvent(&pDevExt->L2ResumeAckEvent);

                // Ack the caller
                KeSetEvent(&pDevExt->L1ResumeAckEvent, IO_NO_INCREMENT, FALSE);
                break;
            }

            default:
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_ERROR,
                    ("<%s> L1 unsupported st 0x%x\n", pDevExt->PortName, ntStatus)
                );

                // Ignore it for now
                break;
            }  // default

        }  // switch (ntStatus)

    }  // while (bNoptDone)

    // exiting thread, release allocated resources
    if (pwbArray)
    {
        _ExFreePool(pwbArray);
        pwbArray = NULL;
    }

    if ((pDevExt->hL2ReadThreadHandle != NULL) || (pDevExt->pL2ReadThread != NULL))
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> L1 thread exiting, cancelling L2\n", pDevExt->PortName)
        );

        KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
        KeSetEvent
        (
            &pDevExt->CancelReadEvent,
            IO_NO_INCREMENT,
            FALSE
        );

        if (pDevExt->pL2ReadThread != NULL)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_READ,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> L1 thread exiting, cancelling L2 - 2 \n", pDevExt->PortName)
            );
            KeWaitForSingleObject
            (
                pDevExt->pL2ReadThread,
                Executive,
                KernelMode,
                FALSE,
                NULL
            );
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_READ,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> L1 thread exiting, cancelling L2 - 3 \n", pDevExt->PortName)
            );
            ObDereferenceObject(pDevExt->pL2ReadThread);
            KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
            _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-5");
            pDevExt->pL2ReadThread = NULL;
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_READ,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> L1 thread exiting, cancelling L2 - 4 \n", pDevExt->PortName)
            );
        }
        else  // best effort
        {
            KeWaitForSingleObject
            (
                &pDevExt->L2ReadThreadClosedEvent,
                Executive,
                KernelMode,
                FALSE,
                NULL       // &delayValue
            );
            KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
            _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-6");
        }
    }

    USBUTL_PurgeQueue
    (
        pDevExt,
        &pDevExt->ReadDataQueue,
        &pDevExt->ReadSpinLock,
        QCUSB_IRP_TYPE_RIRP,
        0
    );

    // Reset L2 Buffers
    USBMRD_ResetL2Buffers(pDevExt);
    USBMRD_RecycleFrameBuffer(pDevExt);

    KeSetEvent(&pDevExt->L1ReadThreadClosedEvent, IO_NO_INCREMENT, FALSE);
    pDevExt->bL1InCancellation = FALSE;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> L1: OUT 0x%x RmlCount[0]=%d\n", pDevExt->PortName, ntStatus, pDevExt->Sts.lRmlCount[0])
    );

    PsTerminateSystemThread(STATUS_SUCCESS); // terminate this thread
}  // USBMRD_L1MultiReadThread

NTSTATUS MultiReadCompletionRoutine
(
    PDEVICE_OBJECT DO,
    PIRP           Irp,
    PVOID          Context
)
{
    PUSBMRD_L2BUFFER  pL2Ctx = (PUSBMRD_L2BUFFER)Context;
    PDEVICE_EXTENSION pDevExt = pL2Ctx->DeviceExtension;
    KIRQL             Irql = KeGetCurrentIrql();
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    QcAcquireSpinLockWithLevel(&pDevExt->L2Lock, &levelOrHandle, Irql);

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> ML2 COMPL (IRQL-%d): Irp[%u]=0x%p(0x%x)\n", pDevExt->PortName,
        Irql, pL2Ctx->Index, Irp, Irp->IoStatus.Status)
    );

    if (InterlockedDecrement(&(pDevExt->NumberOfPendingReadIRPs)) <= 0)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_FORCE,
            ("<%s> ML2 COMPL: RIRP pending %d\n", pDevExt->PortName, pDevExt->NumberOfPendingReadIRPs)
        );
        InterlockedIncrement(&(pDevExt->FlowControlEnabledCount));
    }
    InsertTailList(&pDevExt->L2CompletionQueue, &pL2Ctx->List);

    RemoveEntryList(&pL2Ctx->PendingList);

    //KeSetEvent(&pL2Ctx->CompletionEvt, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&pDevExt->L2USBReadCompEvent, IO_NO_INCREMENT, FALSE);

    QcReleaseSpinLockWithLevel(&pDevExt->L2Lock, levelOrHandle, Irql);
    if (pDevExt->NumberOfPendingReadIRPs <= 0)
    {
        QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, FALSE, 24); // RD completion: clear busy; idle allowed only if no new work
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}  // MultiReadCompletionRoutine

void USBMRD_L2MultiReadThread(PDEVICE_EXTENSION pDevExt)
{
    PIO_STACK_LOCATION pNextStack;
    UCHAR    ucPipeIndex;
    BOOLEAN  bCancelled = FALSE;
    NTSTATUS ntStatus;
    BOOLEAN  bFirstTime = TRUE;
    PKWAIT_BLOCK pwbArray;
    KIRQL    OldRdIrqLevel;
    ULONG    ulReadSize;
    KEVENT   dummyEvent;
    int      i, devBusyCnt = 0, devErrCnt = 0;
    ULONG    waitCount = L2_READ_EVENT_COUNT; // must < 64
    UCHAR              reqSent;  // for debugging purpose
    L2_STATE           l2State = L2_STATE_WORKING;
    PUSBMRD_L2BUFFER   pActiveL2Buf = NULL;
    PLIST_ENTRY        headOfList;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> ML2 ERR: wrong IRQL\n", pDevExt->PortName)
        );
#ifdef DEBUG_MSGS
        KdBreakPoint();
#endif
    }

    // allocate a wait block array for the multiple wait
    pwbArray = _ExAllocatePool
    (
        NonPagedPool,
        waitCount * sizeof(KWAIT_BLOCK),
        "Level2ReadThread.pwbArray"
    );
    if (!pwbArray)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> ML2 - STATUS_NO_MEMORY 1\n", pDevExt->PortName)
        );
        _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-2");
        KeSetEvent(&pDevExt->L2ReadThreadStartedEvent, IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_NO_MEMORY);
    }

#ifdef ENABLE_LOGGING
    // Create logs
    if (pDevExt->EnableLogging == TRUE)
    {
        QCUSB_CreateLogs(pDevExt, QCUSB_CREATE_RX_LOG);
    }
#endif  // ENABLE_LOGGING

    // Set L2 thread priority
    if (gVendorConfig.ThreadPriority > 10)
    {
        KeSetPriorityThread(KeGetCurrentThread(), gVendorConfig.ThreadPriority);
    }

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_INFO,
        ("<%s> ML2: pri=%d, L2Buf=%d\n",
        pDevExt->PortName,
        KeQueryPriorityThread(KeGetCurrentThread()),
        pDevExt->NumberOfL2Buffers
    )
    );

    pDevExt->bL2ReadActive = FALSE;

    pDevExt->pL2ReadEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
    KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
    KeClearEvent(&dummyEvent);

    // Assign read completion events
    // for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
    // {
    //    pDevExt->pL2ReadEvents[L2_READ_EVENT_COUNT+i] =
    //       &(pDevExt->pL2ReadBuffer[i].CompletionEvt);
    // }

    pDevExt->L2IrpStartIdx = pDevExt->L2IrpEndIdx = 0;

    pDevExt->bL2Stopped = TRUE; // pDevExt->PowerSuspended;

    while (bCancelled == FALSE)
    {
        reqSent = 0;

        while ((pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].State == L2BUF_STATE_READY) &&
               inDevState(DEVICE_STATE_PRESENT_AND_STARTED) &&
               (pDevExt->bL2Polling == TRUE))
        {
            PIRP pIrp = pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].Irp;
            PURB pUrb = &(pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].Urb);

            if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
                (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
            {
                pDevExt->bL2Stopped = TRUE;
            }

            if (pDevExt->bL2Stopped == FALSE)
            {
                IoReuseIrp(pIrp, STATUS_SUCCESS);

                pNextStack = IoGetNextIrpStackLocation(pIrp);
                pNextStack->Parameters.Others.Argument1 = pUrb;
                pNextStack->Parameters.DeviceIoControl.IoControlCode =
                    IOCTL_INTERNAL_USB_SUBMIT_URB;
                pNextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;

                IoSetCompletionRoutine
                (
                    pIrp,
                    MultiReadCompletionRoutine,
                    (PVOID) & (pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx]),
                    TRUE,
                    TRUE,
                    TRUE
                );

                ulReadSize = pDevExt->MaxPipeXferSize;

                UsbBuildInterruptOrBulkTransferRequest
                (
                    pUrb,
                    sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                    pDevExt->Interface[pDevExt->DataInterface]->Pipes[pDevExt->BulkPipeInput].PipeHandle,
                    pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].Buffer,
                    NULL,
                    ulReadSize,
                    USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN,
                    NULL
                )

                    pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].State = L2BUF_STATE_PENDING;

                //PendingQueue

                QcAcquireSpinLock(&pDevExt->L2Lock, &levelOrHandle);

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_TRACE,
                    ("<%s> ML2 PENDING: Index[%u]\n", pDevExt->PortName,
                    pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].Index)
                );

                InsertTailList(&pDevExt->L2PendingQueue, &(pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].PendingList));

                QcReleaseSpinLock(&pDevExt->L2Lock, levelOrHandle);

                InterlockedIncrement(&(pDevExt->NumberOfPendingReadIRPs));
                if (pDevExt->NumberOfPendingReadIRPs < 5)
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_FORCE,
                        ("<%s> ML2: IoCallDriver-IRP[%u]=0x%p, L1Q/Bus %d/%d\n", pDevExt->PortName,
                        pDevExt->L2IrpEndIdx, pIrp, pDevExt->NumberOfQueuedReadIRPs, pDevExt->NumberOfPendingReadIRPs)
                    );
                }

                pDevExt->bL2ReadActive = TRUE;
                QCPWR_CancelIdleTimer(pDevExt, QCUSB_BUSY_WT, TRUE, 23);  // RX in progress, set busy and cancel idle IRP
                ntStatus = IoCallDriver(pDevExt->StackDeviceObject, pIrp);
                if (ntStatus != STATUS_PENDING)
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_CRITICAL,
                        ("<%s> ML2: IoCallDriver rtn 0x%x", pDevExt->PortName, ntStatus)
                    );

#if 0
                    QcAcquireSpinLock(&pDevExt->L2Lock, &levelOrHandle);

                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_TRACE,
                        ("<%s> ML2 DELETE: Index[%u]\n", pDevExt->PortName,
                        pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].Index)
                    );

                    RemoveEntryList(&(pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].PendingList));

                    QcReleaseSpinLock(&pDevExt->L2Lock, levelOrHandle);
#endif               

                    pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].Status = ntStatus;
                    pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].State = L2BUF_STATE_COMPLETED;
                    // if ((ntStatus == STATUS_DEVICE_NOT_READY) || (ntStatus == STATUS_DEVICE_DATA_ERROR))
                    if (!NT_SUCCESS(ntStatus))
                    {
                        devErrCnt++;
                        if (devErrCnt > 3)
                        {
                            pDevExt->bL2Stopped = TRUE;
                            l2State = L2_STATE_STOPPING;
                            QCUSB_DbgPrint
                            (
                                QCUSB_DBG_MASK_READ,
                                QCUSB_DBG_LEVEL_ERROR,
                                ("<%s> ML2 err, stopped\n", pDevExt->PortName)
                            );
                            KeSetEvent
                            (
                                &pDevExt->L2StopEvent,
                                IO_NO_INCREMENT,
                                FALSE
                            );
                        }
                    }
                    else
                    {
                        devErrCnt = 0;
                    }
                }
                else
                {
                    devErrCnt = 0;
                }

                if (++(pDevExt->L2IrpEndIdx) == pDevExt->NumberOfL2Buffers)
                {
                    pDevExt->L2IrpEndIdx = 0;
                }
                ++reqSent;

#ifdef QCOM_TRACE_IN
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_ERROR,
                    ("<%s> ML2 ON %ld/%lu\n", pDevExt->PortName, ulReadSize, CountReadQueue(pDevExt))
                );
#endif
            } // if L2 is inactive and not stopped
            else
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_ERROR,
                    ("<%s> ML2: ERR-ACTorSTOP(L2Stop=%u)\n", pDevExt->PortName, pDevExt->bL2Stopped)
                );
                break;
            }
        } // end while -- if L2 buffer is available to receive

#ifdef QCUSB_DBGPRINT
        if ((reqSent == 0) && (USBMRD_L2ActiveBuffers(pDevExt) == 0))
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_READ,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> ML2 buf exhausted or no dev, wait...\n", pDevExt->PortName)
            );
        }
        else
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_READ,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> ML2: %d REQ's sent\n", pDevExt->PortName, reqSent)
            );
        }
#endif // QCUSB_DBGPRINT

        // No matter what IoCallDriver returns, we always wait on the kernel event
        // we created earlier. Our completion routine will gain control when the IRP
        // completes to signal this event. -- Walter Oney's WDM book page 228
wait_for_completion:

        if (bFirstTime == TRUE)
        {
            bFirstTime = FALSE;

            // inform read function that we've started
            KeSetEvent(&pDevExt->L2ReadThreadStartedEvent, IO_NO_INCREMENT, FALSE);
        }

        ntStatus = KeWaitForMultipleObjects
        (
            waitCount,
            (VOID **)&pDevExt->pL2ReadEvents,
            WaitAny,
            Executive,
            KernelMode,
            FALSE,    // non-alertable
            NULL,     // no timeou for bulk read
            pwbArray
        );

        switch (ntStatus)
        {
            case QC_DUMMY_EVENT_INDEX:
            {
                KeClearEvent(&dummyEvent);
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_ERROR,
                    ("<%s> L2: DUMMY_EVENT\n", pDevExt->PortName)
                );
                goto wait_for_completion;
            }

            case L2_READ_COMPLETION_EVENT_INDEX:
            {
                // clear read completion event
                KeClearEvent(&pDevExt->L2ReadCompletionEvent);
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_CRITICAL,
                    ("<%s> ML2 ERR: COMPLETION_EVENT\n", pDevExt->PortName)
                );
                break;
            }

            case L2_KICK_READ_EVENT_INDEX:
            {
                KeClearEvent(&pDevExt->L2KickReadEvent);

                // Use error level as output priority
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> ML2 R kicked, act=%d\n", pDevExt->PortName, pDevExt->bL2ReadActive)
                );

                break;
            }

            case CANCEL_EVENT_INDEX:
            {
                // reset read cancel event so we dont reactive
                KeClearEvent(&pDevExt->CancelReadEvent); // never set

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L2_CANCEL Rml[0]=%u\n", pDevExt->PortName, pDevExt->Sts.lRmlCount[0])
                );

                pActiveL2Buf = USBMRD_L2NextActive(pDevExt);

#ifdef ENABLE_LOGGING
                QCUSB_LogData
                (
                    pDevExt,
                    pDevExt->hRxLogFile,
                    (PVOID)NULL,
                    0,
                    QCUSB_LOG_TYPE_CANCEL_THREAD
                );
#endif // ENABLE_LOGGING

                pDevExt->bL2Stopped = TRUE;
                if (pActiveL2Buf != NULL)
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_INFO,
                        ("<%s> L2 CANCEL - IRP[%u]\n", pDevExt->PortName, pActiveL2Buf->Index)
                    );
                    l2State = L2_STATE_CANCELLING;

                    // direct cancellation to completion section
                    IoCancelIrp(pActiveL2Buf->Irp);

                    goto wait_for_completion;
                }

                bCancelled = TRUE;

                break;
            }

            case L2_STOP_EVENT_INDEX:
            {
                KeClearEvent(&pDevExt->L2StopEvent);
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L2_STOP_EVENT\n", pDevExt->PortName)
                );
                if (l2State == L2_STATE_CANCELLING)
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_DETAIL,
                        ("<%s> L2_STOP_EVENT: superceded by CANCELLATION\n", pDevExt->PortName)
                    );
                    goto wait_for_completion;
                }

                pActiveL2Buf = USBMRD_L2NextActive(pDevExt);

                pDevExt->bL2Stopped = TRUE;

                if (pActiveL2Buf != NULL)
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_INFO,
                        ("<%s> L2 STOP: Cancel Irp[%u]\n", pDevExt->PortName, pActiveL2Buf->Index)
                    );
                    l2State = L2_STATE_STOPPING;

                    // direct STOP to completion section
                    IoCancelIrp(pActiveL2Buf->Irp);

                    goto wait_for_completion;
                }

                KeSetEvent(&pDevExt->L2StopAckEvent, IO_NO_INCREMENT, FALSE);

                break;
            }

            case L2_RESUME_EVENT_INDEX:
            {
                KeClearEvent(&pDevExt->L2ResumeEvent);
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L2_RESUME_EVENT rm %d ss %d\n", pDevExt->PortName,
                    pDevExt->bDeviceRemoved, pDevExt->PowerSuspended)
                );
                pDevExt->bL2Stopped = ((!inDevState(DEVICE_STATE_PRESENT)) ||
                                       pDevExt->PowerSuspended == TRUE);

                // Ack the caller
                KeSetEvent(&pDevExt->L2ResumeAckEvent, IO_NO_INCREMENT, FALSE);
                break;
            }

            default:
            {
                int            l2BufIdx;
                PIRP           pIrp;
                PURB           pUrb;

                // if ((ntStatus < L2_READ_EVENT_COUNT) || (ntStatus >= waitCount))
                // {
                //   QCUSB_DbgPrint
                //   (
                //      QCUSB_DBG_MASK_READ,
                //      QCUSB_DBG_LEVEL_ERROR,
                //      ("<%s> L2 unsupported st 0x%x\n", pDevExt->PortName, ntStatus)
                //   );

                   // Ignore for now
                //   break;
                // }

                // L2_READ_COMPLETION_EVENT

                // De-queue L2 buffer item
                QcAcquireSpinLock(&pDevExt->L2Lock, &levelOrHandle);

                pActiveL2Buf = NULL;
                if (!IsListEmpty(&pDevExt->L2CompletionQueue))
                {
                    headOfList = RemoveHeadList(&pDevExt->L2CompletionQueue);
                    pActiveL2Buf = CONTAINING_RECORD
                    (
                        headOfList,
                        USBMRD_L2BUFFER,
                        List
                    );
                }
                QcReleaseSpinLock(&pDevExt->L2Lock, levelOrHandle);

                // l2BufIdx = ntStatus - L2_READ_EVENT_COUNT;
                if (pActiveL2Buf == NULL)
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> ML2 err: comp Q empty - %u\n", pDevExt->PortName, ntStatus)
                    );
                    break;
                }
                l2BufIdx = pActiveL2Buf->Index;

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L2 completion [%u]=wait rtn[%u]\n", pDevExt->PortName,
                    l2BufIdx, (ntStatus - L2_READ_EVENT_COUNT))
                );


                if (IsListEmpty(&pDevExt->L2CompletionQueue))
                {
                    // clear read completion event
                        //KeClearEvent(&(pDevExt->pL2ReadBuffer[l2BufIdx].CompletionEvt));
                    KeClearEvent(&(pDevExt->L2USBReadCompEvent));
                }

                pIrp = pDevExt->pL2ReadBuffer[l2BufIdx].Irp;
                pUrb = &(pDevExt->pL2ReadBuffer[l2BufIdx].Urb);

                ntStatus = pIrp->IoStatus.Status;

                if (ntStatus == STATUS_SUCCESS)
                {
                    USBMAIN_UpdateXferStats(pDevExt, pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength, TRUE);
                }

#ifdef ENABLE_LOGGING
                if (ntStatus == STATUS_SUCCESS)
                {
#ifdef QCOM_TRACE_IN
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_DETAIL,
                        ("<%s> L2R[%d]=%ldB\n", pDevExt->PortName, l2BufIdx,
                        pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength)
                    );
#endif
                    // log to file
                    if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hRxLogFile != NULL))
                    {
                        QCUSB_LogData
                        (
                            pDevExt,
                            pDevExt->hRxLogFile,
                            (PVOID)(pUrb->UrbBulkOrInterruptTransfer.TransferBuffer),
                            pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                            QCUSB_LOG_TYPE_READ
                        );
                    }
                }
                else
                {
                    if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hRxLogFile != NULL))
                    {
                        // log to file
                        QCUSB_LogData
                        (
                            pDevExt,
                            pDevExt->hRxLogFile,
                            (PVOID)&ntStatus,
                            sizeof(NTSTATUS),
                            QCUSB_LOG_TYPE_RESPONSE_RD
                        );
                    }

                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_CRITICAL,
                        ("<%s> L2 RX failed - 0x%x\n", pDevExt->PortName, pIrp->IoStatus.Status)
                    );
                }
#endif  // ENABLE_LOGGING

#ifdef QCUSB_MUX_PROTOCOL

                if ((pDevExt->MuxEnabled == TRUE) && (ntStatus == STATUS_SUCCESS))
                {
                    ULONG  muxPayloadLength = 0;
                    PUCHAR p;

                    p = USBIF_DecodeMuxFrame
                    (
                        pDevExt->MyDeviceObject,
                        pUrb->UrbBulkOrInterruptTransfer.TransferBuffer,
                        pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                        &muxPayloadLength,
                        TRUE
                    );
                    // adjust buffer ptr and length to reflect mux payload
                    if ((p != NULL) && (muxPayloadLength != 0))
                    {
                        // there's no performance concern for mux
                        RtlMoveMemory
                        (
                            pUrb->UrbBulkOrInterruptTransfer.TransferBuffer,
                            p,
                            muxPayloadLength
                        );
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_READ,
                            QCUSB_DBG_LEVEL_DETAIL,
                            ("<%s> L2 RX [MUX] decoded RX %d/%dB\n", pDevExt->PortName,
                            muxPayloadLength,
                            pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength)
                        );
                        pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength = muxPayloadLength;
                    }
                    // drop the frame on error
                    if ((p == NULL) && (muxPayloadLength == 0))
                    {
                        ntStatus = STATUS_CANCELLED;
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_READ,
                            QCUSB_DBG_LEVEL_ERROR,
                            ("<%s> L2 RX [MUX] decoding error, sig failure\n", pDevExt->PortName)
                        );
                    }
                }

#endif // QCUSB_MUX_PROTOCOL

                // if ((pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength > 0)
                //     || (ntStatus != STATUS_SUCCESS))
                {
                    pDevExt->pL2ReadBuffer[l2BufIdx].Status = ntStatus;
                    pDevExt->pL2ReadBuffer[l2BufIdx].Length =
                        pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;

                    if ((pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength > 0)
                        || (ntStatus != STATUS_SUCCESS))
                    {
                        pDevExt->pL2ReadBuffer[l2BufIdx].bFilled = TRUE;
                    }
                    pDevExt->pL2ReadBuffer[l2BufIdx].State = L2BUF_STATE_COMPLETED;

                    if (++l2BufIdx == pDevExt->NumberOfL2Buffers)
                    {
                        l2BufIdx = 0;
                    }
                    pDevExt->L2IrpStartIdx = l2BufIdx;

                    if (!NT_SUCCESS(ntStatus))
                    {
                        // STATUS_DEVICE_NOT_READY      0xC00000A3
                        // STATUS_DEVICE_DATA_ERROR     0xC000009C
                        // STATUS_DEVICE_NOT_CONNECTED  0xC000009D
                        // STATUS_UNSUCCESSFUL          0xC0000001
                        pDevExt->InputPipeStatus = ntStatus;
                        if (ntStatus != STATUS_CANCELLED)
                        {
                            if (++devBusyCnt >= (pDevExt->NumOfRetriesOnError + 2))
                            {
                                pDevExt->bL2Stopped = TRUE;
                                QCUSB_DbgPrint
                                (
                                    QCUSB_DBG_MASK_READ,
                                    QCUSB_DBG_LEVEL_CRITICAL,
                                    ("<%s> L2 cont failure, stopped...\n", pDevExt->PortName)
                                );
                                devBusyCnt = 0;

                                // Set STOP event
                                KeSetEvent
                                (
                                    &pDevExt->L2StopEvent,
                                    IO_NO_INCREMENT,
                                    FALSE
                                );

                                // Notify L1
                                KeSetEvent
                                (
                                    &pDevExt->L1ReadCompletionEvent,
                                    IO_NO_INCREMENT,
                                    FALSE
                                );
                                goto wait_for_completion;
                            }
                            if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
                            {
                                QCUSB_ResetInput(pDevExt->MyDeviceObject, QCUSB_RESET_HOST_PIPE);
                                USBUTL_Wait(pDevExt, -(50 * 1000L)); // 5ms
                            }
                        }
                    }

                    // Notify L1 thread
                    KeSetEvent
                    (
                        &pDevExt->L1ReadCompletionEvent,
                        IO_NO_INCREMENT,
                        FALSE
                    );
                }

                pActiveL2Buf = USBMRD_L2NextActive(pDevExt);
                pDevExt->bL2ReadActive = (pActiveL2Buf != NULL);

                if (l2State != L2_STATE_WORKING)
                {
                    if (pActiveL2Buf != NULL)
                    {
                        IoCancelIrp(pActiveL2Buf->Irp);
                        goto wait_for_completion;
                    }
                    else
                    {
                        if (l2State == L2_STATE_STOPPING)
                        {
                            QCUSB_DbgPrint
                            (
                                QCUSB_DBG_MASK_READ,
                                QCUSB_DBG_LEVEL_DETAIL,
                                ("<%s> Sig L2 STOP\n", pDevExt->PortName)
                            );
                            KeSetEvent(&pDevExt->L2StopAckEvent, IO_NO_INCREMENT, FALSE);
                        }
                        else if (l2State == L2_STATE_CANCELLING)
                        {
                            // all active IRP's have been cancelled, time to exit
                            bCancelled = TRUE;
                        }

                        l2State = L2_STATE_WORKING;
                    }
                }

                if (!inDevState(DEVICE_STATE_PRESENT))
                {
                    // Set STOP event
                    KeSetEvent
                    (
                        &pDevExt->L2StopEvent,
                        IO_NO_INCREMENT,
                        FALSE
                    );

                    // Do we need to clear L2 buffers???
                    pDevExt->bL2Stopped = TRUE;
                }

                break;
            }  // default

        }  // switch
    }  // end while forever 

    // exiting thread, release allocated resources
    if (pwbArray)
    {
        _ExFreePool(pwbArray);
        pwbArray = NULL;
    }

    if (pDevExt->hRxLogFile != NULL)
    {
#ifdef ENABLE_LOGGING
        QCUSB_LogData
        (
            pDevExt,
            pDevExt->hRxLogFile,
            (PVOID)NULL,
            0,
            QCUSB_LOG_TYPE_THREAD_END
        );
#endif // ENABLE_LOGGING
        ZwClose(pDevExt->hRxLogFile);
        pDevExt->hRxLogFile = NULL;
    }

    // Empty the L2 completion queue
    QcAcquireSpinLock(&pDevExt->L2Lock, &levelOrHandle);
    while (!IsListEmpty(&pDevExt->L2CompletionQueue))
    {
        headOfList = RemoveHeadList(&pDevExt->L2CompletionQueue);
    }
    QcReleaseSpinLock(&pDevExt->L2Lock, levelOrHandle);

    KeSetEvent(&pDevExt->L2ReadThreadClosedEvent, IO_NO_INCREMENT, FALSE);

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> L2: OUT 0x%x RmlCount[0]=%u\n", pDevExt->PortName, ntStatus, pDevExt->Sts.lRmlCount[0])
    );

    PsTerminateSystemThread(STATUS_SUCCESS); // terminate this thread
}  // USBMRD_L2MultiReadThread

VOID USBMRD_ResetL2Buffers(PDEVICE_EXTENSION pDevExt)
{
    int i;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> -->USBMRD_ResetL2Buffers: Rml[0]=%u\n", pDevExt->PortName, pDevExt->Sts.lRmlCount[0])
    );

    pDevExt->L2IrpStartIdx = pDevExt->L2IrpEndIdx = pDevExt->L2FillIdx = 0;

    for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
    {
        pDevExt->pL2ReadBuffer[i].Status = STATUS_PENDING;
        pDevExt->pL2ReadBuffer[i].Length = 0;
        pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
        pDevExt->pL2ReadBuffer[i].DataPtr = pDevExt->pL2ReadBuffer[i].Buffer;

        pDevExt->pL2ReadBuffer[i].DeviceExtension = pDevExt;
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> USBMRD_ResetL2Buffers: ReuseIRP Index[%d], IRP %p\n", pDevExt->PortName, i, pDevExt->pL2ReadBuffer[i].Irp)
        );
        IoReuseIrp(pDevExt->pL2ReadBuffer[i].Irp, STATUS_SUCCESS);
        pDevExt->pL2ReadBuffer[i].State = L2BUF_STATE_READY;
    }

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> <--USBMRD_ResetL2Buffers: Rml[0]=%u\n", pDevExt->PortName, pDevExt->Sts.lRmlCount[0])
    );
}  // USBMRD_ResetL2Buffers

PUSBMRD_L2BUFFER USBMRD_L2NextActive(PDEVICE_EXTENSION pDevExt)
{
    PLIST_ENTRY   listHead, thisEntry;
    PUSBMRD_L2BUFFER pActiveL2Buf = NULL;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    QcAcquireSpinLock(&pDevExt->L2Lock, &levelOrHandle);

    // Be sure you own the LOCK before you get here!!!!
    if (!IsListEmpty(&pDevExt->L2PendingQueue))
    {
        // Start at the head of the list
        listHead = &pDevExt->L2PendingQueue;
        thisEntry = listHead->Flink;

        pActiveL2Buf = CONTAINING_RECORD
        (
            thisEntry,
            USBMRD_L2BUFFER,
            PendingList
        );
    }

    QcReleaseSpinLock(&pDevExt->L2Lock, levelOrHandle);

    return pActiveL2Buf;
}  // USBMRD_L2NextActive

int USBMRD_L2ActiveBuffers(PDEVICE_EXTENSION pDevExt)
{
    int i, num = 0;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    QcAcquireSpinLock(&pDevExt->L2Lock, &levelOrHandle);

    for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
    {
        if (pDevExt->pL2ReadBuffer[i].State == L2BUF_STATE_PENDING)
        {
            ++num;
        }
    }

    QcReleaseSpinLock(&pDevExt->L2Lock, levelOrHandle);

    return num;
}  // USBMRD_L2ActiveBuffers

VOID USBMRD_FillReadRequest(PDEVICE_EXTENSION pDevExt, UCHAR Cookie)
{
    NTSTATUS ntStatus = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status;
    int oldFillIdx;
    LONG currentFillIdx = pDevExt->L2FillIdx;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> -->USBMRD_FillReadRequest[%d] %d\n", pDevExt->PortName, pDevExt->L2FillIdx, Cookie)
    );

    if (pDevExt->pL2ReadBuffer[currentFillIdx].State != L2BUF_STATE_COMPLETED)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> L1: no buf for filling the index is %d and state %x\n", pDevExt->PortName, currentFillIdx,
            pDevExt->pL2ReadBuffer[currentFillIdx].State)
        );
        return;
    }

    if (ntStatus == STATUS_SUCCESS)
    {
        if (pDevExt->pL2ReadBuffer[currentFillIdx].Length > 20)
        {
        }
        else
        {
            USBUTL_PrintBytes
            (
                pDevExt->pL2ReadBuffer[currentFillIdx].Buffer,
                128,
                pDevExt->pL2ReadBuffer[currentFillIdx].Length,
                "ReadData",
                pDevExt,
                QCUSB_DBG_MASK_RDATA,
                QCUSB_DBG_LEVEL_VERBOSE
            );
        }

        // Check if we received a zero-length packet
        if (pDevExt->pL2ReadBuffer[currentFillIdx].Length == 0)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_READ,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> L1_Fill: got 0-len pkt, recycle L2\n", pDevExt->PortName)
            );
            USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 0);
            return;
        }
        else if (pDevExt->bEnableDataBundling[QC_LINK_DL] == FALSE
#if defined(QCMP_MBIM_DL_SUPPORT)
                 && pDevExt->MBIMDLEnabled == FALSE
#endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
                 && pDevExt->QMAPEnabledV2 == FALSE
                 && pDevExt->QMAPEnabledV3 == FALSE
#endif                 
#endif
#if defined(QCMP_QMAP_V1_SUPPORT)
                 && pDevExt->QMAPEnabledV4 == FALSE
                 && pDevExt->QMAPEnabledV1 == FALSE
#endif                 
                 )
        {
#ifdef QC_IP_MODE      
            if (pDevExt->IPOnlyMode == 0)
            {
#endif         
                if (pDevExt->pL2ReadBuffer[currentFillIdx].Length > (pDevExt->MTU + ETH_HEADER_SIZE))
                {
                    USBUTL_PrintBytes
                    (
                        pDevExt->pL2ReadBuffer[currentFillIdx].Buffer,
                        128,
                        pDevExt->pL2ReadBuffer[currentFillIdx].Length,
                        "OversizedReadEth",
                        pDevExt,
                        QCUSB_DBG_MASK_RDATA,
                        QCUSB_DBG_LEVEL_ERROR
                    );
                    QcRecordOversizedPkt(pDevExt);
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_CRITICAL,
                        ("<%s> L1_Fill: pkt too big %uB/%d, recycle L2\n", pDevExt->PortName,
                        pDevExt->pL2ReadBuffer[currentFillIdx].Length, pDevExt->MTU)
                    );
                    USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 1);
                    return;
                }
#ifdef QC_IP_MODE            
            }
            else
            {
                if (pDevExt->pL2ReadBuffer[currentFillIdx].Length > pDevExt->MTU)
                {
                    USBUTL_PrintBytes
                    (
                        pDevExt->pL2ReadBuffer[currentFillIdx].Buffer,
                        128,
                        pDevExt->pL2ReadBuffer[currentFillIdx].Length,
                        "OversizedReadIP",
                        pDevExt,
                        QCUSB_DBG_MASK_RDATA,
                        QCUSB_DBG_LEVEL_ERROR
                    );
                    QcRecordOversizedPkt(pDevExt);
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_CRITICAL,
                        ("<%s> L1_Fill: oversized pkt %uB/%d, recycle L2\n", pDevExt->PortName,
                        pDevExt->pL2ReadBuffer[currentFillIdx].Length, ETH_MAX_DATA_SIZE)
                    );
                    USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 1);
                    return;
                }
            }
#endif         
        }
    }

    // check completion status
    QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);

    if (pDevExt->bEnableDataBundling[QC_LINK_DL] == FALSE
#if defined(QCMP_MBIM_DL_SUPPORT)
        && pDevExt->MBIMDLEnabled == FALSE
#endif
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
        && pDevExt->QMAPEnabledV2 == FALSE
        && pDevExt->QMAPEnabledV3 == FALSE
#endif                 
#endif
#if defined(QCMP_QMAP_V1_SUPPORT)
        && pDevExt->QMAPEnabledV4 == FALSE
        && pDevExt->QMAPEnabledV1 == FALSE
#endif                 
        )
    {
        ntStatus = USBRD_ReadIrpCompletion(pDevExt, 4);

        if ((NT_SUCCESS(ntStatus)) ||  // buffer consumed
            (!NT_SUCCESS(pDevExt->pL2ReadBuffer[currentFillIdx].Status)))  // err return

        {
            // reset the buffer record
            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status = STATUS_PENDING;
            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Length = 0;
            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bFilled = FALSE;
            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].State = L2BUF_STATE_READY;
            oldFillIdx = pDevExt->L2FillIdx;
            if (InterlockedIncrement(&(pDevExt->L2FillIdx)) == pDevExt->NumberOfL2Buffers)
            {
                pDevExt->L2FillIdx = 0;
            }

            if (oldFillIdx == pDevExt->L2IrpEndIdx)
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> L1: L2 buf available, kick L2\n", pDevExt->PortName)
                );
                // kick L2 thread
                KeSetEvent
                (
                    pDevExt->pL2ReadEvents[L2_KICK_READ_EVENT_INDEX],
                    IO_NO_INCREMENT,
                    FALSE
                );
            }
        }
        else
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_READ,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> USBMRD_FillReadRequest[%d/%d] %d: error 0x%x, retry next time\n", pDevExt->PortName,
                pDevExt->L2FillIdx, currentFillIdx, Cookie, ntStatus)
            );
        }
    }
    else
    {
        ntStatus = USBMRD_ReadIrpCompletion(pDevExt, 4, &levelOrHandle);
    }

    if (!inDevState(DEVICE_STATE_PRESENT))
    {
        pDevExt->bL1Stopped = TRUE;
    }
    QCUSB_DbgPrint2
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> MRCOMP to OUT lock -2\n", pDevExt->PortName)
    );

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> <--USBMRD_FillReadRequest[%d/%d] %d\n", pDevExt->PortName, pDevExt->L2FillIdx, currentFillIdx, Cookie)
    );
    QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

    // STATUS_WAIT_63 for this specific scheduling purpose
    if (pDevExt->EnableData5G != 0)
    {
        if ((pDevExt->NumberOfQueuedReadIRPs < 200) || (ntStatus == STATUS_WAIT_63))
        {
            // LARGE_INTEGER delayValue;

            // delayValue.QuadPart = -(5 * 100L);

            DbgPrint("<%s> NDIS COUNT LOW/ZERO - Bus %ld L1Q %ld DROP %ld\n", pDevExt->PortName,
                     pDevExt->NumberOfPendingReadIRPs, pDevExt->NumberOfQueuedReadIRPs, pDevExt->FrameDropCount);

            // KeDelayExecutionThread(KernelMode, FALSE, &delayValue);  // 50us
        }
    }
    else if (ntStatus == STATUS_WAIT_63)
    {
        DbgPrint("<%s> NDIS COUNT ZERO - Bus %ld L1Q %ld DROP %ld\n", pDevExt->PortName,
                 pDevExt->NumberOfPendingReadIRPs, pDevExt->NumberOfQueuedReadIRPs, pDevExt->FrameDropCount);

    }

    return;

}  // USBMRD_FillReadRequest

VOID USBMRD_RecycleL2FillBuffer(PDEVICE_EXTENSION pDevExt, LONG FillIdx, UCHAR Cookie)
{
    int oldFillIdx = pDevExt->L2FillIdx;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> RecycleL2FillBuffer[%d/%d]<%d>\n", pDevExt->PortName, FillIdx, pDevExt->L2FillIdx, Cookie)
    );

    if (FillIdx != pDevExt->L2FillIdx)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> RecycleL2FillBuffer[%d/%d]-idx not match<%d>\n", pDevExt->PortName, FillIdx, pDevExt->L2FillIdx, Cookie)
        );
        return;
    }
    // reset the buffer record
    pDevExt->pL2ReadBuffer[FillIdx].Status = STATUS_PENDING;
    pDevExt->pL2ReadBuffer[FillIdx].Length = 0;
    pDevExt->pL2ReadBuffer[FillIdx].bFilled = FALSE;
    pDevExt->pL2ReadBuffer[FillIdx].State = L2BUF_STATE_READY;
    pDevExt->pL2ReadBuffer[FillIdx].DataPtr =
        pDevExt->pL2ReadBuffer[FillIdx].Buffer;
    if (InterlockedIncrement(&(pDevExt->L2FillIdx)) == pDevExt->NumberOfL2Buffers)
    {
        pDevExt->L2FillIdx = 0;
    }
    if (oldFillIdx == pDevExt->L2IrpEndIdx)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> L1: L2 buf available, kick L2\n", pDevExt->PortName)
        );
        // kick L2 thread
        KeSetEvent
        (
            pDevExt->pL2ReadEvents[L2_KICK_READ_EVENT_INDEX],
            IO_NO_INCREMENT,
            FALSE
        );
    }
}  // USBMRD_ResetL2FillBuffer

VOID USBMRD_RecycleFrameBuffer(PDEVICE_EXTENSION pDevExt)
{
    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> RecycleFrameBuffer\n", pDevExt->PortName)
    );
    pDevExt->TlpFrameBuffer.PktLength = 0;
    pDevExt->TlpFrameBuffer.BytesNeeded = 0;
} // USBMRD_RecycleFrameBuffer

// called by read thread within ReadQueMutex because it manipulates pCallingIrp
//NTSTATUS USBMRD_ReadIrpCompletion(PDEVICE_EXTENSION pDevExt, UCHAR Cookie)
NTSTATUS USBMRD_ReadIrpCompletion(PDEVICE_EXTENSION pDevExt, UCHAR Cookie, KIRQL *levelOrHandle)
{
    PIO_STACK_LOCATION irpStack;
    PIRP         pIrp = NULL;
    PLIST_ENTRY  headOfList, peekEntry;
    PUCHAR       ioBuffer = NULL;
    ULONG        ulReqLength;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandleMux;
#else
    KIRQL levelOrHandleMux;
#endif
    NTSTATUS     ntStatus = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status;
    ULONG        ulL2Length = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Length;
    PQCTLP_PKT   pTlpPkt;
    BOOLEAN      bCompleteIrp = FALSE;
    LONG         currentFillIdx = pDevExt->L2FillIdx;
    PDEVICE_EXTENSION pReturnDevExt = NULL;
    BOOLEAN      bDirectCompletion = TRUE;

    if (NT_SUCCESS(ntStatus) || (ntStatus == STATUS_CANCELLED))
    {
        pDevExt->RdErrorCount = 0;
    }
    else
        //if ((ntStatus == STATUS_DEVICE_DATA_ERROR) ||
        //    (ntStatus == STATUS_DEVICE_NOT_READY)  ||
        //    (ntStatus == STATUS_UNSUCCESSFUL))
    {
        pDevExt->RdErrorCount++;
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> MRIC - error: device err %d <%u>\n", pDevExt->PortName, pDevExt->RdErrorCount, Cookie)
        );

        // after some magic number of times of failure,
        // we mark the device as 'removed'
        if ((pDevExt->RdErrorCount > pDevExt->NumOfRetriesOnError) && (pDevExt->ContinueOnDataError == FALSE))
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_READ,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> MRIC - error %d - dev removed\n\n", pDevExt->PortName, pDevExt->RdErrorCount)
            );
            clearDevState(DEVICE_STATE_PRESENT);
            pDevExt->RdErrorCount = pDevExt->NumOfRetriesOnError;
        }
    }

    // Peek
    if (!IsListEmpty(&pDevExt->ReadDataQueue))
    {
        // headOfList = RemoveHeadList(&pDevExt->ReadDataQueue);
        headOfList = &pDevExt->ReadDataQueue;
        peekEntry = headOfList->Flink;
        pIrp = CONTAINING_RECORD
        (
            peekEntry,
            IRP,
            Tail.Overlay.ListEntry
        );
    }

    QCUSB_DbgPrint
    (
        (QCUSB_DBG_MASK_RIRP | QCUSB_DBG_MASK_READ),
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> MRIC RIRP=0x%p (L2:[%d] %id/%ldB) st 0x%x bus %d <%d>\n",
        pDevExt->PortName, pIrp, pDevExt->L2FillIdx, pDevExt->TlpFrameBuffer.PktLength, ulL2Length,
        ntStatus, pDevExt->NumberOfPendingReadIRPs, Cookie
    )
    );

    // DbgPrint("<%s> FRAME DROPPED Count : %ld and Total Size : 0x%ldB FC : %ld\n", pDevExt->PortName, pDevExt->FrameDropCount, pDevExt->FrameDropBytes, pDevExt->FlowControlEnabledCount);

    if (pIrp != NULL)
    {
        irpStack = IoGetCurrentIrpStackLocation(pIrp);
        if (pIrp->MdlAddress)
        {
            ulReqLength = MmGetMdlByteCount(pIrp->MdlAddress);
            ioBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, HighPagePriority);
            if (ioBuffer == NULL)
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_CRITICAL,
                    ("<%s> ERROR: Mdl translation 0x%p\n", pDevExt->PortName, pIrp->MdlAddress)
                );
            }
        }
        else
        {
            ulReqLength = irpStack->Parameters.Read.Length;
            ioBuffer = (PUCHAR)pIrp->AssociatedIrp.SystemBuffer;
        }
    }

    if ((pDevExt->QMAPEnabledV1 == TRUE)
        || (pDevExt->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
        || (pDevExt->QMAPEnabledV2 == TRUE)
        || (pDevExt->QMAPEnabledV3 == TRUE)
#endif
#endif                         
        )
    {
    }
    else
    {
        if ((pIrp == NULL) || (ioBuffer == NULL))
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_RIRP,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> MRIC - error: pIrp 1 - is NULL\n", pDevExt->PortName)
            );
            ntStatus = STATUS_UNSUCCESSFUL;
            return ntStatus;
        }
    }

    if (ntStatus == STATUS_SUCCESS)
    {
        // if (ioBuffer == NULL)
        // {
        //    ntStatus = STATUS_NO_MEMORY;
        // }

        if (ntStatus == STATUS_SUCCESS)
        {
            switch (pDevExt->TlpBufferState)
            {
                case QCTLP_BUF_STATE_IDLE:
                case QCTLP_BUF_STATE_HDR_ONLY:
                {
                    QCTLP_PKT  tlpHolder;
                    PQCTLP_PKT tlp = &tlpHolder;
                    USHORT     pktLen;
                    PCHAR      dataPtr;
                    UCHAR      padBytes = 0;
                    BOOLEAN    ReadQueueEmpty = FALSE;
#if defined(QCMP_MBIM_DL_SUPPORT)
                    PQCNDP_16BIT_HEADER pNDPHrd;
                    PQCDATAGRAM_STRUCT pDatagramPtr = NULL;
                    PQCNTB_16BIT_HEADER pNTBHrd = (PQCNTB_16BIT_HEADER)pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer;
#endif
#if defined(QCMP_QMAP_V1_SUPPORT) || defined(QCMP_QMAP_V2_SUPPORT) 
                    PQCQMAP_STRUCT qmap;
#endif

                    PCHAR      bufStart = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer;
                    PCHAR      bufEnd = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer + ulL2Length;
                    int        remBytes = bufEnd - pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr;

                    if (pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr == pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer)
                    {
                        // Print the packet
                        USBUTL_PrintBytes
                        (
                            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer,
                            128,
                            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Length,
                            "READDATA",
                            pDevExt,
                            QCUSB_DBG_MASK_RDATA,
                            QCUSB_DBG_LEVEL_VERBOSE
                        );
                    }

                    if (pDevExt->TlpBufferState == QCTLP_BUF_STATE_IDLE)
                    {
                        if (pDevExt->QMAPEnabledV4 == TRUE)
                        {
                            if (remBytes > (sizeof(QCQMAP_STRUCT) + sizeof(QCQMAP_DL_CHECKSUM)))
                            {
                                PUCHAR ipPtr;
                                UCHAR ipVer;
                                USHORT CkOffset = 0;
                                PUSHORT ckOffsetPtr;
                                ULONG CheckSumTemp;
                                USHORT CheckSumTempShort;
                                USHORT pseudoChecksum = 0;
                                UCHAR FragPacket = 0x00;
                                PQCQMAP_DL_CHECKSUM pDLCheckSum = NULL;
                                qmap = (PQCQMAP_STRUCT)(pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr);
                                pktLen = RtlUshortByteSwap(qmap->PacketLen);
                                dataPtr = (PCHAR)((PCHAR)qmap + sizeof(QCQMAP_STRUCT));

                                // Padding //TODO: check order
                                {
                                    padBytes = (0x3F & (qmap->PadCD));
                                }

                                if (pDevExt->QMAPDLMinPadding > 0)
                                {
                                    padBytes += pDevExt->QMAPDLMinPadding;
                                }

                                QCUSB_DbgPrint
                                (
                                    QCUSB_DBG_MASK_READ,
                                    QCUSB_DBG_LEVEL_DETAIL,
                                    ("<%s> RIC : QMAP: Actual Length of IP packet : 0x%x Pad : 0x%x\n", pDevExt->PortName, pktLen, padBytes)
                                );

                                // Get the adapter and respective IRP
                                if (pDevExt->MuxInterface.MuxEnabled != 0)
                                {
                                    pIrp = GetAdapterReadDataItemIrp(pDevExt, qmap->MuxId, &pReturnDevExt, &ReadQueueEmpty);
                                }
                                else
                                {
                                    if (pIrp == NULL)
                                    {
                                        ReadQueueEmpty = TRUE;
                                    }
                                }
                                if (pIrp != NULL)
                                {
                                    if ((pReturnDevExt != NULL) && (pDevExt != pReturnDevExt))
                                    {
                                        // check completion status
                                        QcAcquireSpinLock(&pReturnDevExt->ReadSpinLock, &levelOrHandleMux);
                                    }

                                    irpStack = IoGetCurrentIrpStackLocation(pIrp);
                                    if (pIrp->MdlAddress)
                                    {
                                        ulReqLength = MmGetMdlByteCount(pIrp->MdlAddress);
                                        ioBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, HighPagePriority);
                                        if (ioBuffer == NULL)
                                        {
                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_READ,
                                                QCUSB_DBG_LEVEL_CRITICAL,
                                                ("<%s> ERROR: Mdl translation 0x%p\n", pDevExt->PortName, pIrp->MdlAddress)
                                            );
                                        }
                                    }
                                    else
                                    {
                                        ulReqLength = irpStack->Parameters.Read.Length;
                                        ioBuffer = (PUCHAR)pIrp->AssociatedIrp.SystemBuffer;
                                    }
                                }
                                else
                                {
                                    // Done with this buffer
                                    QCUSB_DbgPrint
                                    (
                                        QCUSB_DBG_MASK_RIRP,
                                        QCUSB_DBG_LEVEL_ERROR,
                                        ("<%s> MRIC - error: pIrp 2 - is NULL\n", pDevExt->PortName)
                                    );

                                    ntStatus = STATUS_WAIT_63;
                                    if ((ReadQueueEmpty == FALSE) || ((pktLen == 0) || ((pktLen - padBytes) > ulL2Length)))
                                    {
                                        DbgPrint("<%s> FRAME DROPPED : %ldB\n", pDevExt->PortName, ulL2Length);
                                        pDevExt->TLPCount = 0;
                                        USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                                        DbgPrint("<%s> FRAME DROPPED Count : %ld and Total Size : 0x%ldB\n", pDevExt->PortName, ++(pDevExt->FrameDropCount), (pDevExt->FrameDropBytes += ulL2Length));
                                        ntStatus = STATUS_UNSUCCESSFUL;
                                    }
                                    if ((qmap->PadCD & 0x80))
                                    {
                                    }
                                    else
                                    {
                                        return ntStatus;
                                    }
                                }

                                if (qmap->PadCD & 0x80)
                                {
                                    NTSTATUS Status;
                                    PQCQMAPCONTROLQUEUE pQMAPControl;
                                    if ((pktLen != 0) &&
                                        ((pktLen - padBytes) <= ulL2Length))
                                    {
                                        // Status = NdisAllocateMemoryWithTag( &pQMAPControl, sizeof(QCQMAPCONTROLQUEUE), 'MOCQ' );
                                        // if( Status != NDIS_STATUS_SUCCESS )
                                        // {
                                        //   QCUSB_DbgPrint
                                        //   (
                                        //     QCUSB_DBG_MASK_READ,
                                        //     QCUSB_DBG_LEVEL_ERROR,
                                        //     ("<%s> RIC : QMAP: error: Failed to allocate memory for QMI Writes\n", pDevExt->PortName)
                                        //   );
                                        // }
                                        // else
                                        // {
                                        //    RtlCopyMemory(pQMAPControl->Buffer, dataPtr, (pktLen - padBytes));
                                        if ((pReturnDevExt != NULL) && (pDevExt != pReturnDevExt))
                                        {
                                            // USBIF_QMAPAddtoControlQueue(pReturnDevExt->MyDeviceObject, pQMAPControl);
                                            USBIF_QMAPProcessControlMessage(pReturnDevExt, dataPtr, (pktLen - padBytes));
                                        }
                                        else
                                        {
                                            // USBIF_QMAPAddtoControlQueue(pDevExt->MyDeviceObject, pQMAPControl);
                                            USBIF_QMAPProcessControlMessage(pDevExt, dataPtr, (pktLen - padBytes));
                                        }
                                        // }

                                        dataPtr += pktLen;
                                        if (dataPtr >= (bufStart + ulL2Length)) // "==" in fact
                                        {
                                            // Done with this buffer
                                            pDevExt->TLPCount = 0;
                                            USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                                        }
                                        else
                                        {
                                            // Continue signaling completion
                                            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr = dataPtr;
                                        }
                                    }
                                    else
                                    {
                                        // Done with this buffer
                                        QCUSB_DbgPrint
                                        (
                                            QCUSB_DBG_MASK_RIRP,
                                            QCUSB_DBG_LEVEL_ERROR,
                                            ("<%s> MRIC - error: invalid data - end of buf\n", pDevExt->PortName)
                                        );
                                        pDevExt->TLPCount = 0;
                                        USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                                    }

                                    break;
                                }
                            }
                            else
                            {
                                QCUSB_DbgPrint
                                (
                                    QCUSB_DBG_MASK_RIRP,
                                    QCUSB_DBG_LEVEL_ERROR,
                                    ("<%s> MRIC - error: invalid data - end of buf\n", pDevExt->PortName)
                                );
                                pDevExt->TLPCount = 0;
                                USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 4);
                                break;
                            }
                        }
                        else
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
                            if ((pDevExt->QMAPEnabledV2 == TRUE) || (pDevExt->QMAPEnabledV3 == TRUE))
                            {
                                if (remBytes > (sizeof(QCQMAP_STRUCT) + sizeof(QCQMAP_DL_CHECKSUM)))
                                {
                                    PUCHAR ipPtr;
                                    UCHAR ipVer;
                                    USHORT CkOffset = 0;
                                    PUSHORT ckOffsetPtr;
                                    ULONG CheckSumTemp;
                                    USHORT CheckSumTempShort;
                                    USHORT pseudoChecksum = 0;
                                    UCHAR FragPacket = 0x00;
                                    PQCQMAP_DL_CHECKSUM pDLCheckSum = NULL;
                                    qmap = (PQCQMAP_STRUCT)(pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr);
                                    pktLen = RtlUshortByteSwap(qmap->PacketLen);
                                    dataPtr = (PCHAR)((PCHAR)qmap + sizeof(QCQMAP_STRUCT));

                                    // Padding //TODO: check order
                                    {
                                        padBytes = (0x3F & (qmap->PadCD));
                                    }

                                    if (pDevExt->QMAPDLMinPadding > 0)
                                    {
                                        padBytes += pDevExt->QMAPDLMinPadding;
                                    }

                                    QCUSB_DbgPrint
                                    (
                                        QCUSB_DBG_MASK_READ,
                                        QCUSB_DBG_LEVEL_DETAIL,
                                        ("<%s> RIC : QMAP: Actual Length of IP packet : 0x%x Pad : 0x%x\n", pDevExt->PortName, pktLen, padBytes)
                                    );

                                    // Get the adapter and respective IRP
                                    if (pDevExt->MuxInterface.MuxEnabled != 0)
                                    {
                                        pIrp = GetAdapterReadDataItemIrp(pDevExt, qmap->MuxId, &pReturnDevExt, &ReadQueueEmpty);
                                    }
                                    else
                                    {
                                        if (pIrp == NULL)
                                        {
                                            ReadQueueEmpty = TRUE;
                                        }
                                    }
                                    if (pIrp != NULL)
                                    {
                                        if ((pReturnDevExt != NULL) && (pDevExt != pReturnDevExt))
                                        {
                                            // check completion status
                                            QcAcquireSpinLock(&pReturnDevExt->ReadSpinLock, &levelOrHandleMux);
                                        }

                                        irpStack = IoGetCurrentIrpStackLocation(pIrp);
                                        if (pIrp->MdlAddress)
                                        {
                                            ulReqLength = MmGetMdlByteCount(pIrp->MdlAddress);
                                            ioBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, HighPagePriority);
                                            if (ioBuffer == NULL)
                                            {
                                                QCUSB_DbgPrint
                                                (
                                                    QCUSB_DBG_MASK_READ,
                                                    QCUSB_DBG_LEVEL_CRITICAL,
                                                    ("<%s> ERROR: Mdl translation 0x%p\n", pDevExt->PortName, pIrp->MdlAddress)
                                                );
                                            }
                                        }
                                        else
                                        {
                                            ulReqLength = irpStack->Parameters.Read.Length;
                                            ioBuffer = (PUCHAR)pIrp->AssociatedIrp.SystemBuffer;
                                        }
                                    }
                                    else
                                    {
                                        // Done with this buffer
                                        QCUSB_DbgPrint
                                        (
                                            QCUSB_DBG_MASK_RIRP,
                                            QCUSB_DBG_LEVEL_ERROR,
                                            ("<%s> MRIC - error: pIrp 2 - is NULL\n", pDevExt->PortName)
                                        );

                                        ntStatus = STATUS_WAIT_63;
                                        if ((ReadQueueEmpty == FALSE) || ((pktLen == 0) || ((pktLen - padBytes) > ulL2Length)))
                                        {
                                            DbgPrint("<%s> FRAME DROPPED : %ldB\n", pDevExt->PortName, ulL2Length);
                                            pDevExt->TLPCount = 0;
                                            USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                                            // DbgPrint("<%s> FRAME DROPPED Count : %ld and Total Size : 0x%ldB\n", pDevExt->PortName, ++(pDevExt->FrameDropCount), (pDevExt->FrameDropBytes+= ulL2Length));
                                            ntStatus = STATUS_UNSUCCESSFUL;
                                        }
                                        if ((qmap->PadCD & 0x80))
                                        {
                                        }
                                        else
                                        {
                                            return ntStatus;
                                        }
                                    }

                                    if (qmap->PadCD & 0x80)
                                    {
                                        NTSTATUS Status;
                                        PQCQMAPCONTROLQUEUE pQMAPControl;
                                        if ((pktLen != 0) &&
                                            ((pktLen - padBytes) <= ulL2Length))
                                        {
                                            // Status = NdisAllocateMemoryWithTag( &pQMAPControl, sizeof(QCQMAPCONTROLQUEUE), 'MOCQ' );
                                            // if( Status != NDIS_STATUS_SUCCESS )
                                            // {
                                            //   QCUSB_DbgPrint
                                            //   (
                                            //     QCUSB_DBG_MASK_READ,
                                            //     QCUSB_DBG_LEVEL_ERROR,
                                            //     ("<%s> RIC : QMAP: error: Failed to allocate memory for QMI Writes\n", pDevExt->PortName)
                                            //   );
                                            // }
                                            // else
                                            // {
                                            //    RtlCopyMemory(pQMAPControl->Buffer, dataPtr, (pktLen - padBytes));
                                            if ((pReturnDevExt != NULL) && (pDevExt != pReturnDevExt))
                                            {
                                                // USBIF_QMAPAddtoControlQueue(pReturnDevExt->MyDeviceObject, pQMAPControl);
                                                USBIF_QMAPProcessControlMessage(pReturnDevExt, dataPtr, (pktLen - padBytes));
                                            }
                                            else
                                            {
                                                // USBIF_QMAPAddtoControlQueue(pDevExt->MyDeviceObject, pQMAPControl);
                                                USBIF_QMAPProcessControlMessage(pDevExt, dataPtr, (pktLen - padBytes));
                                            }
                                            // }

                                            dataPtr += pktLen;
                                            if (dataPtr >= (bufStart + ulL2Length)) // "==" in fact
                                            {
                                                // Done with this buffer
                                                pDevExt->TLPCount = 0;
                                                USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                                            }
                                            else
                                            {
                                                // Continue signaling completion
                                                pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr = dataPtr;
                                            }
                                        }
                                        else
                                        {
                                            // Done with this buffer
                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_RIRP,
                                                QCUSB_DBG_LEVEL_ERROR,
                                                ("<%s> MRIC - error: invalid data - end of buf\n", pDevExt->PortName)
                                            );
                                            pDevExt->TLPCount = 0;
                                            USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                                        }

                                        break;
                                    }

                                    ipPtr = (pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr) + sizeof(QCQMAP_STRUCT);
                                    ipVer = (*ipPtr & 0xF0) >> 4;
                                    if (ipVer == 4)
                                    {
                                        PQCQMAP_IPV4 ipV4;
                                        QCIPV4_FRAG_PKT fragPkt;
                                        QCQMAP_PSEUDO_CHECKSUM pseudoChkSum;
                                        ipV4 = (PQCQMAP_IPV4)ipPtr;
                                        pseudoChkSum.DestAddr = ipV4->DestAddr;
                                        pseudoChkSum.SrcAddr = ipV4->SrcAddr;
                                        pseudoChkSum.Zeros = 0x00;
                                        pseudoChkSum.Protocol = ipV4->Protocol;
                                        pseudoChkSum.Length = RtlUshortByteSwap(RtlUshortByteSwap(ipV4->TotaLlen) - ((*ipPtr & 0x0F) * 4));

                                        if (ipV4->Protocol == 0x06) /* TCP */
                                        {
                                            CkOffset = ((*ipPtr & 0x0F) * 4) + (sizeof(QCQMAP_TCP) - sizeof(USHORT));
                                        }
                                        else if (ipV4->Protocol == 0x11) /* UDP */
                                        {
                                            CkOffset = ((*ipPtr & 0x0F) * 4) + (sizeof(QCQMAP_UDP) - sizeof(USHORT));
                                        }
                                        else if (ipV4->Protocol == 0x01) /* ICMP */
                                        {
                                            CkOffset = ((*ipPtr & 0x0F) * 4) + (sizeof(QCQMAP_ICMP) - sizeof(USHORT));
                                        }

                                        if (CkOffset != 0x00)
                                        {
                                            ckOffsetPtr = (PUSHORT)(ipPtr + CkOffset);
                                            pDLCheckSum = (PQCQMAP_DL_CHECKSUM)((pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr) + sizeof(QCQMAP_STRUCT) + pktLen);
                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_READ,
                                                QCUSB_DBG_LEVEL_DETAIL,
                                                ("<%s> RIC : QMAP: Actual Checksum returned in IP packet : 0x%x and Meta Checksum : 0x%x\n", pDevExt->PortName, *ckOffsetPtr, pDLCheckSum->CksumValue)
                                            );

                                            // Print the packet
                                            USBUTL_PrintBytes
                                            (
                                                pDLCheckSum,
                                                128,
                                                sizeof(QCQMAP_DL_CHECKSUM),
                                                "METADATA",
                                                pDevExt,
                                                QCUSB_DBG_MASK_RDATA,
                                                QCUSB_DBG_LEVEL_VERBOSE
                                            );

                                            pseudoChecksum = CheckSum((PUSHORT)(&pseudoChkSum), sizeof(pseudoChkSum));
                                            if (RtlUshortByteSwap(pDLCheckSum->CksumValue) < RtlUshortByteSwap(*ckOffsetPtr))
                                            {
                                                // QCUSB_DbgPrint
                                                // (
                                                // QCUSB_DBG_MASK_READ,
                                                // QCUSB_DBG_LEVEL_DETAIL,
                                                // ("<%s> RIC : QMAP: Meta data checksum less than extracted checksum\n", pDevExt->PortName)
                                                // );
                                            }
#if 1                                    
                                            if (pDevExt->QMAPEnabledV3 == TRUE)
                                            {
                                                USHORT tempval;
                                                USHORT ipHdrChecksum = CheckSum((unsigned short *)ipPtr, ((*ipPtr & 0x0F) * 4));
                                                tempval = RtlUshortByteSwap(pDLCheckSum->CksumValue);
                                                tempval = (~tempval) - 1;
                                                CheckSumTemp = tempval - RtlUshortByteSwap(ipHdrChecksum);
                                                CheckSumTemp -= RtlUshortByteSwap(*ckOffsetPtr);
                                            }
                                            else
                                            {
                                                CheckSumTemp = RtlUshortByteSwap(pDLCheckSum->CksumValue) - RtlUshortByteSwap(*ckOffsetPtr);
                                            }
#else
                                            CheckSumTemp = RtlUshortByteSwap(pDLCheckSum->CksumValue) - RtlUshortByteSwap(*ckOffsetPtr);
#endif
                                            if (ipV4->Protocol != 0x01) /* ICMP */
                                            {
                                                CheckSumTemp += RtlUshortByteSwap(pseudoChecksum);
                                            }
                                            /*  Fold 32-bit sum to 16 bits */
                                            CheckSumTemp = (CheckSumTemp >> 16) + (CheckSumTemp & 0xffff);
                                            CheckSumTemp += (CheckSumTemp >> 16);
                                            CheckSumTemp = ~CheckSumTemp;
                                            CheckSumTempShort = (unsigned short)CheckSumTemp;

                                            fragPkt.Rsvd2 = RtlUshortByteSwap(ipV4->Rsvd2);

                                            if (fragPkt.Flags & 0x01)
                                            {
                                                FragPacket = 0x01;
                                            }
                                            if (fragPkt.Offset != 0)
                                            {
                                                FragPacket = 0x01;
                                            }

                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_READ,
                                                QCUSB_DBG_LEVEL_DETAIL,
                                                ("<%s> RIC : FragPacket Flag 0x%x and Offset 0x%x\n", pDevExt->PortName, fragPkt.Flags, fragPkt.Offset)
                                            );

                                            if (FragPacket != 0x01)
                                            {
                                                if (CheckSumTempShort != RtlUshortByteSwap(*ckOffsetPtr))
                                                {
                                                    QCUSB_DbgPrint
                                                    (
                                                        QCUSB_DBG_MASK_READ,
                                                        QCUSB_DBG_LEVEL_DETAIL,
                                                        ("<%s> RIC : QMAP: extracted checksum is not same as calculated checksum, calculated checksum : 0x%x\n", pDevExt->PortName, CheckSumTempShort)
                                                    );
                                                }
                                                *ckOffsetPtr = RtlUshortByteSwap(CheckSumTempShort);
                                            }

                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_READ,
                                                QCUSB_DBG_LEVEL_DETAIL,
                                                ("<%s> RIC : QMAP: Pseudo checksum : 0x%x and calculated checksum : 0x%x\n", pDevExt->PortName, pseudoChecksum, *ckOffsetPtr)
                                            );

                                        }
                                        else
                                        {
                                            /* TODO: debug print */
                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_READ,
                                                QCUSB_DBG_LEVEL_DETAIL,
                                                ("<%s> RIC : QMAP: bad Packet Type : 0x%x\n", pDevExt->PortName, ipV4->Protocol)
                                            );
                                        }
                                    }
                                    else
                                    {
                                        PQCQMAP_IPV6 ipV6;
                                        QCQMAP_PSEUDO_CHECKSUMV6 pseudoChkSum;
                                        ipV6 = (PQCQMAP_IPV6)ipPtr;
                                        RtlCopyMemory(pseudoChkSum.DestAddr, ipV6->DestAddr, sizeof(ipV6->DestAddr));
                                        RtlCopyMemory(pseudoChkSum.SrcAddr, ipV6->SrcAddr, sizeof(ipV6->SrcAddr));
                                        RtlZeroMemory(pseudoChkSum.Zeros, 0x03);
                                        pseudoChkSum.payloadLen = ipV6->payloadLen;
                                        pseudoChkSum.NextHdr = ipV6->nextHdr;

                                        if (ipV6->nextHdr == 0x06) /* TCP */
                                        {
                                            CkOffset = sizeof(QCQMAP_IPV6) + (sizeof(QCQMAP_TCP) - sizeof(USHORT));
                                        }
                                        else if (ipV6->nextHdr == 0x11) /* UDP */
                                        {
                                            CkOffset = sizeof(QCQMAP_IPV6) + (sizeof(QCQMAP_UDP) - sizeof(USHORT));
                                        }
                                        else if (ipV6->nextHdr == 0x01) /* ICMP */
                                        {
                                            CkOffset = sizeof(QCQMAP_IPV6) + (sizeof(QCQMAP_ICMP) - sizeof(USHORT));
                                        }

                                        if (CkOffset != 0x00)
                                        {
                                            ckOffsetPtr = (PUSHORT)(ipPtr + CkOffset);
                                            pDLCheckSum = (PQCQMAP_DL_CHECKSUM)((pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr) + sizeof(QCQMAP_STRUCT) + pktLen);
                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_READ,
                                                QCUSB_DBG_LEVEL_DETAIL,
                                                ("<%s> RIC : QMAP: Actual Checksum returned in IP packet : 0x%x and Pseudo Checksum : 0x%x\n", pDevExt->PortName, *ckOffsetPtr, pDLCheckSum->CksumValue)
                                            );

                                            // Print the packet
                                            USBUTL_PrintBytes
                                            (
                                                pDLCheckSum,
                                                128,
                                                sizeof(QCQMAP_DL_CHECKSUM),
                                                "METADATA",
                                                pDevExt,
                                                QCUSB_DBG_MASK_RDATA,
                                                QCUSB_DBG_LEVEL_VERBOSE
                                            );

                                            pseudoChecksum = CheckSum((PUSHORT)(&pseudoChkSum), sizeof(pseudoChkSum));
                                            if (RtlUshortByteSwap(pDLCheckSum->CksumValue) < RtlUshortByteSwap(*ckOffsetPtr))
                                            {
                                                // QCUSB_DbgPrint
                                                // (
                                                // QCUSB_DBG_MASK_READ,
                                                // QCUSB_DBG_LEVEL_DETAIL,
                                                // ("<%s> RIC : QMAP: Meta data checksum less than extracted checksum\n", pDevExt->PortName)
                                                // );
                                            }
#if 1                                   
                                            if (pDevExt->QMAPEnabledV3 == TRUE)
                                            {
                                                USHORT tempval;
                                                USHORT ipHdrChecksum = CheckSum((unsigned short *)ipPtr, sizeof(QCQMAP_IPV6));
                                                tempval = RtlUshortByteSwap(pDLCheckSum->CksumValue);
                                                tempval = (~tempval) - 1;
                                                CheckSumTemp = tempval - RtlUshortByteSwap(ipHdrChecksum);
                                                CheckSumTemp -= RtlUshortByteSwap(*ckOffsetPtr);
                                            }
                                            else
                                            {
                                                CheckSumTemp = RtlUshortByteSwap(pDLCheckSum->CksumValue) - RtlUshortByteSwap(*ckOffsetPtr);
                                            }
#else
                                            CheckSumTemp = RtlUshortByteSwap(pDLCheckSum->CksumValue) - RtlUshortByteSwap(*ckOffsetPtr);
#endif
                                            CheckSumTemp += RtlUshortByteSwap(pseudoChecksum);
                                            /*  Fold 32-bit sum to 16 bits */
                                            CheckSumTemp = (CheckSumTemp >> 16) + (CheckSumTemp & 0xffff);
                                            CheckSumTemp += (CheckSumTemp >> 16);
                                            CheckSumTemp = ~CheckSumTemp;
                                            CheckSumTempShort = (unsigned short)CheckSumTemp;
                                            if (CheckSumTempShort != RtlUshortByteSwap(*ckOffsetPtr))
                                            {
                                                QCUSB_DbgPrint
                                                (
                                                    QCUSB_DBG_MASK_READ,
                                                    QCUSB_DBG_LEVEL_DETAIL,
                                                    ("<%s> RIC : QMAP: extracted checksum is not same as calculated checksum, calculated checksum : 0x%x\n", pDevExt->PortName, CheckSumTempShort)
                                                );
                                            }
                                            *ckOffsetPtr = RtlUshortByteSwap(CheckSumTempShort);
                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_READ,
                                                QCUSB_DBG_LEVEL_DETAIL,
                                                ("<%s> RIC : QMAP: Pseudo checksum : 0x%x and calculated checksum : 0x%x\n", pDevExt->PortName, pseudoChecksum, *ckOffsetPtr)
                                            );
                                        }
                                        else
                                        {
                                            /* TODO : debug message */
                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_READ,
                                                QCUSB_DBG_LEVEL_DETAIL,
                                                ("<%s> RIC : QMAP: bad or fragmented Packet Type : 0x%x\n", pDevExt->PortName, ipV6->nextHdr)
                                            );
                                        }
                                    }
                                }
                                else
                                {
                                    QCUSB_DbgPrint
                                    (
                                        QCUSB_DBG_MASK_RIRP,
                                        QCUSB_DBG_LEVEL_ERROR,
                                        ("<%s> MRIC - error: invalid data - end of buf\n", pDevExt->PortName)
                                    );
                                    pDevExt->TLPCount = 0;
                                    USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 4);
                                    break;
                                }
                            }
                            else
#endif
#endif                         
                                if (pDevExt->QMAPEnabledV1 == TRUE)
                                {
                                    if (remBytes > sizeof(QCQMAP_STRUCT))
                                    {
                                        qmap = (PQCQMAP_STRUCT)(pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr);
                                        pktLen = RtlUshortByteSwap(qmap->PacketLen);
                                        dataPtr = (PCHAR)((PCHAR)qmap + sizeof(QCQMAP_STRUCT));

                                        // Padding //TODO: check order
                                        {
                                            padBytes = (0x3F & (qmap->PadCD));
                                        }

                                        if (pDevExt->QMAPDLMinPadding > 0)
                                        {
                                            padBytes += pDevExt->QMAPDLMinPadding;
                                        }

                                        // Get the adapter and respective IRP
                                        if (pDevExt->MuxInterface.MuxEnabled != 0)
                                        {
                                            pIrp = GetAdapterReadDataItemIrp(pDevExt, qmap->MuxId, &pReturnDevExt, &ReadQueueEmpty);
                                        }
                                        else
                                        {
                                            if (pIrp == NULL)
                                            {
                                                ReadQueueEmpty = TRUE;
                                            }
                                        }

                                        if (pIrp != NULL)
                                        {
                                            if ((pReturnDevExt != NULL) && (pDevExt != pReturnDevExt))
                                            {
                                                // check completion status
                                                QcAcquireSpinLock(&pReturnDevExt->ReadSpinLock, &levelOrHandleMux);
                                            }
                                            irpStack = IoGetCurrentIrpStackLocation(pIrp);
                                            if (pIrp->MdlAddress)
                                            {
                                                ulReqLength = MmGetMdlByteCount(pIrp->MdlAddress);
                                                ioBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, HighPagePriority);
                                                if (ioBuffer == NULL)
                                                {
                                                    QCUSB_DbgPrint
                                                    (
                                                        QCUSB_DBG_MASK_READ,
                                                        QCUSB_DBG_LEVEL_CRITICAL,
                                                        ("<%s> ERROR: Mdl translation 0x%p\n", pDevExt->PortName, pIrp->MdlAddress)
                                                    );
                                                }
                                            }
                                            else
                                            {
                                                ulReqLength = irpStack->Parameters.Read.Length;
                                                ioBuffer = (PUCHAR)pIrp->AssociatedIrp.SystemBuffer;
                                            }
                                        }
                                        else
                                        {
                                            // Done with this buffer
                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_RIRP,
                                                QCUSB_DBG_LEVEL_ERROR,
                                                ("<%s> MRIC - error: pIrp 3 - is NULL - pending %ld Q %ld\n", pDevExt->PortName, pDevExt->NumberOfPendingReadIRPs, pDevExt->NumberOfQueuedReadIRPs)
                                            );
                                            ntStatus = STATUS_WAIT_63;
                                            if ((ReadQueueEmpty == FALSE) || ((pktLen == 0) || ((pktLen - padBytes) > ulL2Length)))
                                            {
                                                DbgPrint("<%s> FRAME DROPPED : %ldB\n", pDevExt->PortName, ulL2Length);
                                                pDevExt->TLPCount = 0;
                                                USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                                                // DbgPrint("<%s> FRAME DROPPED Count : %ld and Total Size : 0x%ldB\n", pDevExt->PortName, ++(pDevExt->FrameDropCount), (pDevExt->FrameDropBytes+= ulL2Length));
                                                ntStatus = STATUS_UNSUCCESSFUL;
                                            }
                                            if ((qmap->PadCD & 0x80))
                                            {
                                            }
                                            else
                                            {
                                                return ntStatus;
                                            }
                                        }

                                        if (qmap->PadCD & 0x80)
                                        {
                                            NTSTATUS Status;
                                            PQCQMAPCONTROLQUEUE pQMAPControl;
                                            if ((pktLen != 0) &&
                                                ((pktLen - padBytes) <= ulL2Length))
                                            {
                                                // Status = NdisAllocateMemoryWithTag( &pQMAPControl, sizeof(QCQMAPCONTROLQUEUE), 'MOCQ' );
                                                // if( Status != NDIS_STATUS_SUCCESS )
                                                // {
                                                //   QCUSB_DbgPrint
                                                //   (
                                                //     QCUSB_DBG_MASK_READ,
                                                //     QCUSB_DBG_LEVEL_ERROR,
                                                //     ("<%s> RIC : QMAP: error: Failed to allocate memory for QMI Writes\n", pDevExt->PortName)
                                                //   );
                                                // }
                                                // else
                                                // {
                                                //    RtlCopyMemory(pQMAPControl->Buffer, dataPtr, (pktLen - padBytes));
                                                if ((pReturnDevExt != NULL) && (pDevExt != pReturnDevExt))
                                                {
                                                    // USBIF_QMAPAddtoControlQueue(pReturnDevExt->MyDeviceObject, pQMAPControl);
                                                    USBIF_QMAPProcessControlMessage(pReturnDevExt, dataPtr, (pktLen - padBytes));
                                                }
                                                else
                                                {
                                                    // USBIF_QMAPAddtoControlQueue(pDevExt->MyDeviceObject, pQMAPControl);
                                                    USBIF_QMAPProcessControlMessage(pDevExt, dataPtr, (pktLen - padBytes));
                                                }
                                                // }

                                                dataPtr += pktLen;
                                                if (dataPtr >= (bufStart + ulL2Length)) // "==" in fact
                                                {
                                                    // Done with this buffer
                                                    pDevExt->TLPCount = 0;
                                                    USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                                                }
                                                else
                                                {
                                                    // Continue signaling completion
                                                    pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr = dataPtr;
                                                }
                                            }
                                            else
                                            {
                                                // Done with this buffer
                                                QCUSB_DbgPrint
                                                (
                                                    QCUSB_DBG_MASK_RIRP,
                                                    QCUSB_DBG_LEVEL_ERROR,
                                                    ("<%s> MRIC - error: invalid data - end of buf\n", pDevExt->PortName)
                                                );
                                                pDevExt->TLPCount = 0;
                                                USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                                            }
                                            break;
                                        }
                                    }
                                    else
                                    {
                                        QCUSB_DbgPrint
                                        (
                                            QCUSB_DBG_MASK_RIRP,
                                            QCUSB_DBG_LEVEL_ERROR,
                                            ("<%s> MRIC - error: invalid data - end of buf\n", pDevExt->PortName)
                                        );
                                        pDevExt->TLPCount = 0;

                                        // recycle the L2
                                        USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 4);
                                        break;
                                    }
                                }
                                else if (pDevExt->bEnableDataBundling[QC_LINK_DL] == TRUE)
                                {
                                    if (remBytes > QCTLP_HDR_LENGTH)
                                    {
                                        tlp = (PQCTLP_PKT)(pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr);
                                        pktLen = (tlp->Length & QCTLP_MASK_LENGTH);
                                        dataPtr = (PCHAR) & (tlp->Payload);
                                    }
                                    // We need to assemble TLP header
                                    else if (remBytes == QCTLP_HDR_LENGTH)
                                    {
                                        QCUSB_DbgPrint
                                        (
                                            QCUSB_DBG_MASK_RIRP,
                                            QCUSB_DBG_LEVEL_DETAIL,
                                            ("<%s> MRIC - TLP_BUF_STATE_HDR_ONLY\n", pDevExt->PortName)
                                        );
                                        dataPtr = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr;
                                        RtlCopyMemory
                                        (
                                            pDevExt->TlpFrameBuffer.Buffer,
                                            dataPtr,
                                            remBytes
                                        );
                                        pDevExt->TlpFrameBuffer.PktLength = remBytes;
                                        pDevExt->TlpFrameBuffer.BytesNeeded = 0;
                                        pDevExt->TlpBufferState = QCTLP_BUF_STATE_HDR_ONLY;

                                        // Done with this buffer
                                        USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 2);

                                        break;
                                    }
                                    else if (remBytes > 0)
                                    {
                                        QCUSB_DbgPrint
                                        (
                                            QCUSB_DBG_MASK_RIRP,
                                            QCUSB_DBG_LEVEL_DETAIL,
                                            ("<%s> MRIC - TLP_BUF_STATE_PARTIAL_HDR\n", pDevExt->PortName)
                                        );
                                        dataPtr = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr;
                                        RtlCopyMemory
                                        (
                                            pDevExt->TlpFrameBuffer.Buffer,
                                            dataPtr,
                                            remBytes
                                        );
                                        pDevExt->TlpFrameBuffer.PktLength = remBytes;
                                        pDevExt->TlpFrameBuffer.BytesNeeded = QCTLP_HDR_LENGTH - remBytes;
                                        pDevExt->TlpBufferState = QCTLP_BUF_STATE_PARTIAL_HDR;


                                        // Done with this buffer
                                        USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 3);
                                        break;
                                    }
                                    else
                                    {
                                        QCUSB_DbgPrint
                                        (
                                            QCUSB_DBG_MASK_RIRP,
                                            QCUSB_DBG_LEVEL_ERROR,
                                            ("<%s> MRIC - error: invalid data - end of buf\n", pDevExt->PortName)
                                        );
                                        pDevExt->TLPCount = 0;

                                        // recycle the L2
                                        USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 4);
                                        break;
                                    }
                                }
                                else
                                {
#if defined(QCMP_MBIM_DL_SUPPORT)

                                    pNTBHrd = (PQCNTB_16BIT_HEADER)pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer;
                                    if (pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer == pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr)
                                    {

                                        // need to check the MBIM sanity but for now just skip the MBIM NCM header
                                        if (pNTBHrd->wBlockLength != remBytes)
                                        {
                                            QCUSB_DbgPrint
                                            (
                                                QCUSB_DBG_MASK_READ,
                                                QCUSB_DBG_LEVEL_DETAIL,
                                                ("<%s> RIC : Wrong MBIM packet BlockLength : %d and PacketLen : %d\n", pDevExt->PortName, pNTBHrd->wBlockLength, remBytes)
                                            );
                                            pDevExt->TLPCount = 0;

                                            // recycle the L2
                                            USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 4);
                                            break;
                                        }
                                        pNDPHrd = (PQCNDP_16BIT_HEADER)((PCHAR)pNTBHrd + pNTBHrd->wNdpIndex);
                                        if (pNDPHrd->wNextNdpIndex != 0x0000)
                                        {
                                            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].nextIndex = (PCHAR)((PCHAR)pNTBHrd + pNDPHrd->wNextNdpIndex);
                                        }
                                        else
                                        {
                                            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].nextIndex = 0x00;
                                        }
                                        pDatagramPtr = (PQCDATAGRAM_STRUCT)((PCHAR)pNDPHrd + sizeof(QCNDP_16BIT_HEADER));

                                        QCUSB_DbgPrint
                                        (
                                            QCUSB_DBG_MASK_READ,
                                            QCUSB_DBG_LEVEL_DETAIL,
                                            ("<%s> RIC : MBIMDL: Current Datagram Ptr : 0x%d and Datagram len : %d\n", pDevExt->PortName, pDatagramPtr->DatagramPtr, pDatagramPtr->DatagramLen)
                                        );
                                        pktLen = pDatagramPtr->DatagramLen;
                                        dataPtr = (PCHAR)pNTBHrd + pDatagramPtr->DatagramPtr;
                                    }
                                    else
                                    {

                                        pDatagramPtr = (PQCDATAGRAM_STRUCT)(pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr);

                                        QCUSB_DbgPrint
                                        (
                                            QCUSB_DBG_MASK_READ,
                                            QCUSB_DBG_LEVEL_DETAIL,
                                            ("<%s> RIC : MBIMDL: Current Datagram Ptr : 0x%d and Datagram len : %d\n", pDevExt->PortName, pDatagramPtr->DatagramPtr, pDatagramPtr->DatagramLen)
                                        );
                                        pktLen = pDatagramPtr->DatagramLen;
                                        dataPtr = (PCHAR)pNTBHrd + pDatagramPtr->DatagramPtr;
                                    }
#endif
                                }
                    }
                    else // QCTLP_BUF_STATE_HDR_ONLY
                    {
                        tlp->Length = *((PUSHORT)pDevExt->TlpFrameBuffer.Buffer);
                        pktLen = (tlp->Length & QCTLP_MASK_LENGTH);
                        dataPtr = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr;
                        USBMRD_RecycleFrameBuffer(pDevExt);
                        pDevExt->TlpBufferState = QCTLP_BUF_STATE_IDLE;
                    }

                    // Validate TLP Packet Header
                    // Not needed according to the new Spec
#if 0                  
                    if ((tlp->Length & QCTLP_MASK_SYNC) != QCTLP_BITS_SYNC)
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_READ,
                            QCUSB_DBG_LEVEL_ERROR,
                            ("<%s> MRIC - error invalid TLP hdr 0x%x (%uB/%uB)\n", pDevExt->PortName,
                            tlp->Length, ulL2Length, pDevExt->MaxPipeXferSize)
                        );
                        pDevExt->TLPCount = 0;
                        if (ulL2Length == pDevExt->MaxPipeXferSize)
                        {
                            pDevExt->TlpBufferState = QCTLP_BUF_STATE_ERROR;
                        }
                        USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 5);
                        break;
                    }
#endif                  
                    if ((pktLen != 0) &&
                        ((pktLen - padBytes) <= ulL2Length) &&
                        ((dataPtr + pktLen) <= (bufStart + ulL2Length))
                        ) // within L2 buf
                    {
                        if (pktLen <= ulReqLength)
                        {
                            NTSTATUS nts;

                            nts = QCMRD_FillIrpData(pDevExt, ioBuffer, ulReqLength, dataPtr, (pktLen - padBytes));
                            if (nts == STATUS_SUCCESS)
                            {
                                pIrp->IoStatus.Information = pktLen - padBytes;
#ifdef QC_IP_MODE
                                if (pDevExt->IPOnlyMode == TRUE)
                                {
                                    pIrp->IoStatus.Information += sizeof(QCUSB_ETH_HDR);
                                }
#endif // QC_IP_MODE
                            }
                            else
                            {
                                pIrp->IoStatus.Information = 0;
                            }
                            bCompleteIrp = TRUE;
                            pDevExt->TLPCount++;
                            QCUSB_DbgPrint
                            (
                                QCUSB_DBG_MASK_RIRP,
                                QCUSB_DBG_LEVEL_DETAIL,
                                ("<%s> MRIC - Fill RIRP 0x%p[%u] %uB\n", pDevExt->PortName,
                                pIrp, pDevExt->TLPCount, pktLen)
                            );
                        }
                        else
                        {

                            if (pktLen >= ETH_MAX_PACKET_SIZE)
                            {
                                // invalid received pkt 
                                QCUSB_DbgPrint
                                (
                                    QCUSB_DBG_MASK_RIRP,
                                    QCUSB_DBG_LEVEL_ERROR,
                                    ("<%s> MRIC - error, received pkt too big %u/%u/%u/%u, drop\n",
                                    pDevExt->PortName, pktLen, ulReqLength, ulL2Length, pDevExt->MaxPipeXferSize)
                                );
                                pDevExt->TLPCount = 0;
                                if (ulL2Length == pDevExt->MaxPipeXferSize)
                                {
                                    // treat as bundle error, need to drop the whole xfer
                                    pDevExt->TlpBufferState = QCTLP_BUF_STATE_ERROR;
                                    USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 6);
                                    break;
                                }
                                // else try to jump to the next TLP pkt
                            }
                            else
                            {
                                // IRP buffer size too small
                                QCUSB_DbgPrint
                                (
                                    QCUSB_DBG_MASK_RIRP,
                                    QCUSB_DBG_LEVEL_ERROR,
                                    ("<%s> MRIC - error, IRP buf too small %u/%u/%u\n",
                                    pDevExt->PortName, pktLen, ulReqLength, ulL2Length)
                                );
                                ntStatus = STATUS_BUFFER_TOO_SMALL;
                                bCompleteIrp = TRUE;
                                break;
                            }
                        }

                        if ((pDevExt->QMAPEnabledV1 == TRUE)
                            || (pDevExt->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
                            || (pDevExt->QMAPEnabledV2 == TRUE)
                            || (pDevExt->QMAPEnabledV3 == TRUE)
#endif
#endif                         
                            )
                        {
                            dataPtr += pktLen;
                            if (pDevExt->QMAPEnabledV4 == TRUE)
                            {
                                dataPtr += sizeof(QCQMAP_DL_CHECKSUM);
                            }
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
                            else
                                if ((pDevExt->QMAPEnabledV2 == TRUE) || (pDevExt->QMAPEnabledV3 == TRUE))
                                {
                                    dataPtr += sizeof(QCQMAP_DL_CHECKSUM);
                                }
#endif
#endif                         
                            if (dataPtr >= (bufStart + ulL2Length)) // "==" in fact
                            {
                                // Done with this buffer
                                pDevExt->TLPCount = 0;
                                USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                            }
                            else
                            {
                                // Continue signaling completion
                                pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr = dataPtr;
                                KeSetEvent
                                (
                                    &pDevExt->L1ReadCompletionEvent,
                                    IO_NO_INCREMENT, FALSE
                                );
                            }
                        }
                        else if (pDevExt->bEnableDataBundling[QC_LINK_DL] == TRUE)
                        {
                            dataPtr += pktLen;
                            if (dataPtr >= (bufStart + ulL2Length)) // "==" in fact
                            {
                                // Done with this buffer
                                pDevExt->TLPCount = 0;
                                USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                            }
                            else
                            {
                                // Continue signaling completion
                                pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr = dataPtr;
                                KeSetEvent
                                (
                                    &pDevExt->L1ReadCompletionEvent,
                                    IO_NO_INCREMENT, FALSE
                                );
                            }
                        }
                        else
                        {
#if defined(QCMP_MBIM_DL_SUPPORT)                     
                            pDatagramPtr = (PQCDATAGRAM_STRUCT)((PCHAR)pDatagramPtr + sizeof(QCDATAGRAM_STRUCT));

                            if (pDatagramPtr->Datagram == 0)
                            {
                                if (pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].nextIndex != 0x0000)
                                {
                                    pNDPHrd = (PQCNDP_16BIT_HEADER)pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].nextIndex;
                                    if (pNDPHrd->wNextNdpIndex != 0x0000)
                                    {
                                        pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].nextIndex = (PCHAR)((PCHAR)pNTBHrd + pNDPHrd->wNextNdpIndex);
                                    }
                                    else
                                    {
                                        pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].nextIndex = 0x00;
                                    }

                                    pDatagramPtr = (PQCDATAGRAM_STRUCT)((PCHAR)pNDPHrd + sizeof(QCNDP_16BIT_HEADER));

                                    // Continue signaling completion
                                    pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr = (PCHAR)pDatagramPtr;
                                    KeSetEvent
                                    (
                                        &pDevExt->L1ReadCompletionEvent,
                                        IO_NO_INCREMENT, FALSE
                                    );
                                }
                                else
                                {
                                    // Done with this buffer
                                    pDevExt->TLPCount = 0;
                                    USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 7);
                                }
                            }
                            else
                            {
                                // Continue signaling completion
                                pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr = (PCHAR)pDatagramPtr;
                                KeSetEvent
                                (
                                    &pDevExt->L1ReadCompletionEvent,
                                    IO_NO_INCREMENT, FALSE
                                );
                            }
#endif                        
                        }
                    }
                    else
                    {
                        if (pDevExt->bEnableDataBundling[QC_LINK_DL] == TRUE)
                        {
                            // Partial Fill
                            if (pktLen <= ulReqLength)
                            {
                                int tmpLen = bufStart + ulL2Length - dataPtr;

                                QCUSB_DbgPrint
                                (
                                    QCUSB_DBG_MASK_RIRP,
                                    QCUSB_DBG_LEVEL_DETAIL,
                                    ("<%s> MRIC - partial fill, first leg %uB/%uB/%uB/%uB\n",
                                    pDevExt->PortName, tmpLen, pktLen, ulReqLength, ulL2Length)
                                );
                                RtlCopyMemory
                                (
                                    pDevExt->TlpFrameBuffer.Buffer,
                                    dataPtr,
                                    tmpLen
                                );
                                pDevExt->TlpFrameBuffer.PktLength = tmpLen;
                                pDevExt->TlpFrameBuffer.BytesNeeded = pktLen - tmpLen;
                                pDevExt->TlpBufferState = QCTLP_BUF_STATE_PARTIAL_FILL;

                                // Done with this buffer
                                USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 8);
                            }
                            else
                            {
                                if (pktLen >= ETH_MAX_PACKET_SIZE)
                                {
                                    QCUSB_DbgPrint
                                    (
                                        QCUSB_DBG_MASK_RIRP,
                                        QCUSB_DBG_LEVEL_ERROR,
                                        ("<%s> MRIC - error partial fill, rcvd pkt too big %u/%u/%u/%u, drop\n",
                                        pDevExt->PortName, pktLen, ulReqLength, ulL2Length, pDevExt->MaxPipeXferSize)
                                    );
                                    pDevExt->TLPCount = 0;
                                    if (ulL2Length == pDevExt->MaxPipeXferSize)
                                    {
                                        pDevExt->TlpBufferState = QCTLP_BUF_STATE_ERROR;
                                    }
                                    USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 9);
                                }
                                else
                                {
                                    // IRP buffer size too small
                                    QCUSB_DbgPrint
                                    (
                                        QCUSB_DBG_MASK_RIRP,
                                        QCUSB_DBG_LEVEL_ERROR,
                                        ("<%s> MRIC - error partial fill, IRP buf too small %u/%u/%u\n",
                                        pDevExt->PortName, pktLen, ulReqLength, ulL2Length)
                                    );
                                    ntStatus = STATUS_BUFFER_TOO_SMALL;
                                    bCompleteIrp = TRUE;
                                }
                            }
                        }
                        else
                        {
#if defined(QCMP_MBIM_DL_SUPPORT)                     
                            QCUSB_DbgPrint
                            (
                                QCUSB_DBG_MASK_RIRP,
                                QCUSB_DBG_LEVEL_ERROR,
                                ("<%s> MBIM - error bad packet %u/%u/%u/%u, drop\n",
                                pDevExt->PortName, pktLen, ulReqLength, ulL2Length, pDevExt->MaxPipeXferSize)
                            );
                            pDevExt->TLPCount = 0;
                            USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 9);
#endif                         
                        }
                    }
                    break;
                }  // QCTLP_BUF_STATE_IDLE

                case QCTLP_BUF_STATE_PARTIAL_HDR:
                {
                    // WARNING:
                    // The assumption here is only one byte is needed.
                    // May not work (Need to refine) if TLP header changes.
                    PCHAR bufStart = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer;
                    PCHAR bufEnd = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer + ulL2Length;
                    PCHAR dataPtr = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr;
                    int   partialLength = pDevExt->TlpFrameBuffer.PktLength;

                    // assemble the whole TLP header
                    RtlCopyMemory
                    (
                        (PCHAR)pDevExt->TlpFrameBuffer.Buffer + partialLength,
                        dataPtr,
                        pDevExt->TlpFrameBuffer.BytesNeeded
                    );
                    dataPtr += pDevExt->TlpFrameBuffer.BytesNeeded;
                    pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr = dataPtr;

                    pDevExt->TlpBufferState = QCTLP_BUF_STATE_HDR_ONLY;

                    if (dataPtr < bufEnd)
                    {
                        KeSetEvent
                        (
                            &pDevExt->L1ReadCompletionEvent,
                            IO_NO_INCREMENT, FALSE
                        );
                    }
                    else
                    {
                        // recycle L2
                        USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 10);
                    }

                    break;
                }  // QCTLP_BUF_STATE_PARTIAL_HDR

                case QCTLP_BUF_STATE_PARTIAL_FILL:
                {
                    if (pDevExt->TlpFrameBuffer.BytesNeeded > ulL2Length)
                    {
                        // error, both L2 buf and frame buf need to be discarded
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_RIRP,
                            QCUSB_DBG_LEVEL_ERROR,
                            ("<%s> MRIC - error, frame spans > 2 buffers\n", pDevExt->PortName)
                        );
                        pDevExt->TlpBufferState = QCTLP_BUF_STATE_ERROR;
                        USBMRD_RecycleFrameBuffer(pDevExt);
                        USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 11);
                    }
                    else
                    {
                        NTSTATUS nts;

                        // Fill IRP
                        //    1. Move bytes in TlpFrameBuffer (PktLength) to IRP
                        //    2. Move bytes in L2 (BytesNeeded) to IRP
                        PCHAR dataPtr = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr;
                        PCHAR bufStart = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer;

                        // The IRP buffer length was validated in the partial-fill phase,
                        // so we do not validate it here, just copy into the IRP.
                        pDevExt->TLPCount++;
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_RIRP,
                            QCUSB_DBG_LEVEL_DETAIL,
                            ("<%s> MRIC - M Filling RIRP 0x%p[%u] (%uB+%uB)\n", pDevExt->PortName,
                            pIrp, pDevExt->TLPCount, pDevExt->TlpFrameBuffer.PktLength,
                            pDevExt->TlpFrameBuffer.BytesNeeded)
                        );
                        nts = QCMRD_FillIrpData
                        (
                            pDevExt,
                            ioBuffer, ulReqLength,
                            pDevExt->TlpFrameBuffer.Buffer,
                            pDevExt->TlpFrameBuffer.PktLength
                        );
                        if (nts == STATUS_SUCCESS)
                        {
                            pIrp->IoStatus.Information = pDevExt->TlpFrameBuffer.PktLength + pDevExt->TlpFrameBuffer.BytesNeeded;
#ifdef QC_IP_MODE
                            if (pDevExt->IPOnlyMode == TRUE)
                            {
                                pIrp->IoStatus.Information += sizeof(QCUSB_ETH_HDR);
                            }
#endif // QC_IP_MODE
                        }
                        else
                        {
                            pIrp->IoStatus.Information = 0;
                        }
                        RtlCopyMemory
                        (
                            (PVOID)(ioBuffer + pDevExt->TlpFrameBuffer.PktLength),
                            dataPtr,
                            pDevExt->TlpFrameBuffer.BytesNeeded
                        );
                        bCompleteIrp = TRUE;
                        pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr += pDevExt->TlpFrameBuffer.BytesNeeded;
                        USBMRD_RecycleFrameBuffer(pDevExt);
                        pDevExt->TlpBufferState = QCTLP_BUF_STATE_IDLE;

                        // See if end of L2 buf is reached
                        if (pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].DataPtr >= (bufStart + ulL2Length))
                        {
                            pDevExt->TLPCount = 0;
                            USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 12);
                        }
                        else
                        {
                            KeSetEvent
                            (
                                &pDevExt->L1ReadCompletionEvent,
                                IO_NO_INCREMENT, FALSE
                            );
                        }
                    }
                    break;
                }  // QCTLP_BUF_STATE_PARTIAL_FILL

                case QCTLP_BUF_STATE_ERROR:
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_RIRP,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> MRIC - error bad bundle, discard L2[%u] %uB\n", pDevExt->PortName,
                        pDevExt->L2FillIdx, ulL2Length)
                    );

                    // Look for the end of a transaction -- not very reliable method but OK
                    if (ulL2Length < pDevExt->MaxPipeXferSize)
                    {
                        // This should be the end of the packet bundle.
                        pDevExt->TlpBufferState = QCTLP_BUF_STATE_IDLE;

                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_RIRP,
                            QCUSB_DBG_LEVEL_DETAIL,
                            ("<%s> MRIC - end of bad bundle reached %uB\n", pDevExt->PortName, ulL2Length)
                        );
                        pDevExt->TLPCount = 0;
                    }
                    USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 13);
                    break;
                }  // QCTLP_BUF_STATE_ERROR

                default:
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_RIRP,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> MRIC - error: wrong TLP state\n", pDevExt->PortName)
                    );
                    break;
                }
            }  // switch
        }  // // if (ntStatus == STATUS_SUCCESS)
    }  // if (ntStatus == STATUS_SUCCESS)
    else
    {
        bCompleteIrp = TRUE;
        USBMRD_RecycleL2FillBuffer(pDevExt, currentFillIdx, 14);
    }

    if (pIrp != NULL)
    {
        if (bCompleteIrp == TRUE)
        {
            // De-queue
            RemoveEntryList(&(pIrp->Tail.Overlay.ListEntry));

            if (pIrp->Cancel)
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_RIRP,
                    QCUSB_DBG_LEVEL_ERROR,
                    ("<%s> MRIC - error RIRP 0x%p C: in callellation!!\n", pDevExt->PortName, pIrp)
                );
            }

            if (IoSetCancelRoutine(pIrp, NULL) == NULL)
            {
                // the IRP is in Cancellation
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_RIRP,
                    QCUSB_DBG_LEVEL_ERROR,
                    ("<%s> MRIC - error RIRP 0x%p C: 0\n", pDevExt->PortName, pIrp)
                );
                if ((pReturnDevExt != NULL) && (pDevExt != pReturnDevExt))
                {
                    // check completion status
                    QcReleaseSpinLock(&pReturnDevExt->ReadSpinLock, levelOrHandleMux);
                }
                return ntStatus;
            }

            pIrp->IoStatus.Status = ntStatus;

            if (ntStatus == STATUS_CANCELLED)
            {
                pIrp->IoStatus.Information = 0;
            }

            QCUSB_DbgPrint2
            (
                QCUSB_DBG_MASK_RIRP,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> MRIC<%d> - QtoC RIRP (0x%x) 0x%p\n", pDevExt->PortName,
                Cookie, ntStatus, pIrp)
            );

            if ((pReturnDevExt != NULL) && (pDevExt != pReturnDevExt))
            {
                if (bDirectCompletion == FALSE)
                {
                    InsertTailList(&pReturnDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
                    KeSetEvent(&pReturnDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
                }
                else
                {
                    QcIoReleaseRemoveLock(pReturnDevExt->pRemoveLock, pIrp, QCUSB_IRP_TYPE_RIRP);
                    QcReleaseSpinLock(&pReturnDevExt->ReadSpinLock, levelOrHandleMux);
                    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
                    QcAcquireSpinLock(&pReturnDevExt->ReadSpinLock, &levelOrHandleMux);
                }
            }
            else
            {
                if (bDirectCompletion == FALSE)
                {
                    InsertTailList(&pDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
                    KeSetEvent(&pDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
                }
                else
                {
                    QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, QCUSB_IRP_TYPE_RIRP);
                    QcReleaseSpinLock(&pDevExt->ReadSpinLock, *levelOrHandle);
                    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
                    QcAcquireSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
                }
            }
        }  // if (bCompleteIrp == TRUE)
        if ((pReturnDevExt != NULL) && (pDevExt != pReturnDevExt))
        {
            // check completion status
            QcReleaseSpinLock(&pReturnDevExt->ReadSpinLock, levelOrHandleMux);
        }
    }
    else
    {
        ntStatus = STATUS_UNSUCCESSFUL;
    }

    return ntStatus;
}  // USBMRD_ReadIrpCompletion

NTSTATUS USBRD_IrpCompletionSetEvent
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           Irp,
    IN PVOID          Context
)
{
    PKEVENT pEvent = Context;

    KeSetEvent(pEvent, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}  // USBRD_IrpCompletionSetEvent

BOOLEAN USBRD_InPipeOk(PDEVICE_EXTENSION pDevExt)
{
    PIO_STACK_LOCATION pNextStack;
    PIRP               pIrp;
    PURB               pUrb;
    PCHAR              dummyBuf;
    PKEVENT            pEvent;
    LARGE_INTEGER      delayValue;
    NTSTATUS           ntStatus;
    PUSBMRD_L2BUFFER   pRdBuf;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> --> USBRD_InPipeOk 0x%x\n", pDevExt->PortName, pDevExt->BulkPipeInput)
    );
    if (pDevExt->BulkPipeInput == (UCHAR)-1)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _InPipeOk: no IN pipe!\n", pDevExt->PortName)
        );
        return FALSE;
    }
    pRdBuf = &(pDevExt->pL2ReadBuffer[0]);
    dummyBuf = pDevExt->pL2ReadBuffer[0].Buffer;
    if (dummyBuf == NULL)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _InPipeOk: no MEM!\n", pDevExt->PortName)
        );
        return FALSE;
    }
    pIrp = pRdBuf->Irp;
    pUrb = &(pRdBuf->Urb);
    //pEvent = &(pRdBuf->CompletionEvt);
    pEvent = &(pDevExt->L2USBReadCompEvent);
    RtlZeroMemory
    (
        pUrb,
        sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER)
    );
    UsbBuildInterruptOrBulkTransferRequest
    (
        pUrb,
        sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
        pDevExt->Interface[pDevExt->DataInterface]
        ->Pipes[pDevExt->BulkPipeInput].PipeHandle,
        dummyBuf,
        NULL,
        1600,
        (USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN),
        NULL
    );

    IoReuseIrp(pIrp, STATUS_SUCCESS);
    pNextStack = IoGetNextIrpStackLocation(pIrp);
    pNextStack->Parameters.DeviceIoControl.IoControlCode =
        IOCTL_INTERNAL_USB_SUBMIT_URB;
    pNextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    pNextStack->Parameters.Others.Argument1 = pUrb;

    IoSetCompletionRoutine
    (
        pIrp,
        USBRD_IrpCompletionSetEvent,
        pEvent,
        TRUE,
        TRUE,
        TRUE
    );
    ntStatus = IoCallDriver(pDevExt->StackDeviceObject, pIrp);

    // Pull briefly 
    delayValue.QuadPart = -(15 * 1000 * 1000);   // 1.5 sec
    if (ntStatus == STATUS_PENDING)
    {
        ntStatus = KeWaitForSingleObject
        (
            pEvent, Executive, KernelMode,
            FALSE, &delayValue
        );
        if (ntStatus == STATUS_TIMEOUT)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_READ,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> _InPipeOk: 1st timeout\n", pDevExt->PortName)
            );
            IoCancelIrp(pIrp);
            ntStatus = KeWaitForSingleObject
            (
                pEvent,
                Executive,
                KernelMode,
                FALSE,
                &delayValue
            );
            if (ntStatus == STATUS_TIMEOUT)
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_READ,
                    QCUSB_DBG_LEVEL_CRITICAL,
                    ("<%s> _InPipeOk: Cxl timeout!\n", pDevExt->PortName)
                );
            }
        }
    }
    else
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _InPipeOk: IoCallDriver failed 0x%x\n", pDevExt->PortName, ntStatus)
        );
        KeWaitForSingleObject
        (
            pEvent, Executive, KernelMode,
            FALSE, &delayValue
        );
    }

    KeClearEvent(pEvent);

    pDevExt->InputPipeStatus = pIrp->IoStatus.Status;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_READ,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> <-- USBRD_InPipeOk 0x%x/0x%x\n", pDevExt->PortName,
        pIrp->IoStatus.Status, STATUS_DEVICE_NOT_CONNECTED)
    );
    return (pIrp->IoStatus.Status != STATUS_DEVICE_NOT_CONNECTED);
}  // USBRD_InPipeOk

NTSTATUS QCMRD_FillIrpData
(
    PDEVICE_EXTENSION pDevExt,
    PCHAR             IrpBuffer,
    ULONG             BufferSize,
    PCHAR             DataBuffer,
    ULONG             DataLength
)
{
    PUCHAR pPktStart = IrpBuffer;

#ifdef QC_IP_MODE
    if (pDevExt->IPOnlyMode == TRUE)
    {
        BufferSize -= sizeof(QCUSB_ETH_HDR);
        if (DataLength > BufferSize)
        {
            // insufficient memory
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_READ,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> IPO: MRD_FillIrpData: IRP buf too small: %d/%dB\n", pDevExt->PortName,
                BufferSize, DataLength)
            );
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlCopyMemory(IrpBuffer, &(pDevExt->EthHdr), sizeof(QCUSB_ETH_HDR));
        IrpBuffer += sizeof(QCUSB_ETH_HDR);
    }
#endif // QC_IP_MODE

    // Print the packet
    USBUTL_PrintBytes
    (
        DataBuffer,
        128,
        DataLength,
        "RECEIVED PACKET",
        pDevExt,
        QCUSB_DBG_MASK_RDATA,
        QCUSB_DBG_LEVEL_VERBOSE
    );

#ifdef QC_IP_MODE         
    if ((pDevExt->IPOnlyMode == TRUE) || (pDevExt->WWANMode == TRUE))
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> RECEIVED PACKET : seq num (%02X %02X) pending %ld Q %ld\n", pDevExt->PortName,
            (UCHAR)DataBuffer[4], (UCHAR)DataBuffer[5], pDevExt->NumberOfPendingReadIRPs, pDevExt->NumberOfQueuedReadIRPs)
        );
        // DbgPrint("<%s> RECEIVED PACKET : seq num (%02X %02X)\n", pDevExt->PortName, (UCHAR)DataBuffer[4], (UCHAR)DataBuffer[5]);
    }
    else
    {
#endif         
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> RECEIVED PACKET : seq num (%02X %02X) pending %ld Q %ld\n", pDevExt->PortName,
            (UCHAR)DataBuffer[18], (UCHAR)DataBuffer[19], pDevExt->NumberOfPendingReadIRPs, pDevExt->NumberOfQueuedReadIRPs)
        );
#ifdef QC_IP_MODE
    }
#endif         

    RtlCopyMemory
    (
        IrpBuffer,
        DataBuffer,
        DataLength
    );

#ifdef QC_IP_MODE
    if (pDevExt->IPOnlyMode == TRUE)
    {
        USBUTL_SetEtherType(pPktStart);
    }
#endif // QC_IP_MODE


    InterlockedDecrement(&(pDevExt->NumberOfQueuedReadIRPs));
    return STATUS_SUCCESS;
}  // QCMRD_FillIrpData
