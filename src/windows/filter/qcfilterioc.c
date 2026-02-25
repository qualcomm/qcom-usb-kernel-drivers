/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q C F I L T E R I O C . C

GENERAL DESCRIPTION
    This file implements the IRP dispatch processing and the filter thread
    for the Qualcomm USB composite device control object. It handles file
    requests, manages the IRP queue and worker threads.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "Ntddk.h"
#include "Usbdi.h"
#include "Usbdlib.h"
#include "Wdm.h"

#include "qcfilter.h"
#include "qcfilterioc.h"

#ifdef EVENT_TRACING
#define WPP_GLOBALLOGGER
#include "qcfilterioc.tmh"      //  this is the file that will be auto generated
#endif


/****************************************************************************
 *
 * function: ProcessDispatchIrp
 *
 * purpose:  This routine is the dispatch routine for filter device object.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *           Irp = pointer to dispatch Irp
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
ProcessDispatchIrp
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
)
{
    KIRQL levelOrHandle;
    PIO_STACK_LOCATION irpStack, nextStack;
    PDEVICE_EXTENSION pDevExt;
    PVOID ioBuffer;
    ULONG inputBufferLength, outputBufferLength, ioControlCode;
    NTSTATUS ntStatus = STATUS_SUCCESS, ntCloseStatus = STATUS_SUCCESS, myNts;
    USHORT usLength;
    BOOLEAN bRemoveRequest = FALSE;
    KIRQL irqLevel = KeGetCurrentIrql();

    pDevExt = DeviceObject->DeviceExtension;

    irpStack = IoGetCurrentIrpStackLocation(Irp);
    nextStack = IoGetNextIrpStackLocation(Irp);

    // get the parameters from an IOCTL call
    ioBuffer = Irp->AssociatedIrp.SystemBuffer;
    inputBufferLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    outputBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
    ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;

    QCFLT_DbgPrint
    (
        DBG_LEVEL_TRACE,
        ("<%s> D: ProcessDispatchIrp\n", pDevExt->PortName)
    );

    switch (irpStack->MajorFunction)
    {
        case IRP_MJ_CREATE:
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = 0;

            break;
        }
        case IRP_MJ_CLOSE:
        {
            ntStatus = Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = 0;

            break;
        }
        case IRP_MJ_CLEANUP:
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = 0;
            break; // IRP_MJ_CLEANUP
        }
        case IRP_MJ_DEVICE_CONTROL:
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = 0;

            switch (ioControlCode)
            {
                case IOCTL_QCDEV_CREATE_CTL_FILE:
                {
                    PFILTER_DEVICE_INFO pFilterDeviceInfo;
                    PFILTER_DEVICE_LIST pFilterDeviceList;
                    if (inputBufferLength != sizeof(FILTER_DEVICE_INFO))
                    {
                        ntStatus = STATUS_INVALID_PARAMETER;
                        break;
                    }


                    QCFLT_DbgPrint
                    (
                        DBG_LEVEL_TRACE,
                        ("<%s> D: ProcessDispatchIrp IOCTL_QCDEV_CREATE_CTL_FILE \n", pDevExt->PortName)
                    );
                    pFilterDeviceInfo = (PFILTER_DEVICE_INFO)ioBuffer;
                    if ((pFilterDeviceList = QCFLT_FindFilterDevice(DeviceObject, pFilterDeviceInfo)) == NULL)
                    {
                        ntStatus = QCFilterCreateControlObject(DeviceObject, pFilterDeviceInfo);
                        Irp->IoStatus.Status = ntStatus;
                        if (ntStatus == STATUS_SUCCESS)
                        {
                            Irp->IoStatus.Information = sizeof(FILTER_DEVICE_INFO);
                        }
                    }
                    else
                    {
                        Irp->IoStatus.Status = ntStatus;
                        if (ntStatus == STATUS_SUCCESS)
                        {
                            Irp->IoStatus.Information = sizeof(FILTER_DEVICE_INFO);
                        }
                    }
                    if (ntStatus == STATUS_SUCCESS)
                    {
                        KIRQL levelOrHandle;
                        PCONTROL_DEVICE_EXTENSION deviceExtension = pFilterDeviceInfo->pControlDeviceObject->DeviceExtension;
                    }
                }
                break;
                case IOCTL_QCDEV_CLOSE_CTL_FILE:
                {
                    KIRQL levelOrHandle;
                    PFILTER_DEVICE_INFO pFilterDeviceInfo;
                    if (inputBufferLength != sizeof(FILTER_DEVICE_INFO))
                    {
                        ntStatus = STATUS_INVALID_PARAMETER;
                        break;
                    }
                    QCFLT_DbgPrint
                    (
                        DBG_LEVEL_TRACE,
                        ("<%s> D: ProcessDispatchIrp IOCTL_QCDEV_CLOSE_CTL_FILE \n", pDevExt->PortName)
                    );
                    pFilterDeviceInfo = (PFILTER_DEVICE_INFO)ioBuffer;

                    QCFLT_DbgPrint
                    (
                        DBG_LEVEL_DETAIL,
                        ("<%s> Before calling QCFilterDeleteControlObject\n", pDevExt->PortName)
                    );
                    ntStatus = QCFilterDeleteControlObject(DeviceObject, pFilterDeviceInfo);
                    QCFLT_DbgPrint
                    (
                        DBG_LEVEL_DETAIL,
                        ("<%s> After calling QCFilterDeleteControlObject\n", pDevExt->PortName)
                    );

                    Irp->IoStatus.Status = ntStatus;
                    if (ntStatus == STATUS_SUCCESS)
                    {
                        Irp->IoStatus.Information = sizeof(FILTER_DEVICE_INFO);
                    }
                }
                break;
                default:
                {
                }
                break;
            }
        }

        default:
        {
            break;
        }

    }

    ntStatus = Irp->IoStatus.Status;

    if (ntStatus == STATUS_PENDING)
    {
        _IoMarkIrpPending(Irp);
    }
    else
    {
        // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("ProcessDispatchIrp : Release RemoveLock\n"));
        IoReleaseRemoveLock(&pDevExt->RemoveLock, Irp);
        QCFLT_DbgPrint
        (
            DBG_LEVEL_TRACE,
            ("<%s> D: Process Dispatch IRP : \n", pDevExt->PortName)
        );

        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    return ntStatus;
}

/****************************************************************************
 *
 * function: ControlFilterThread
 *
 * purpose:  This routine is the thread routine for filter device object dispatch.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *
 * returns:  void
 *
 ****************************************************************************/
VOID
ControlFilterThread(PVOID pContext)
{
    PDEVICE_OBJECT pDevobj = (PDEVICE_OBJECT)pContext;
    PDEVICE_EXTENSION pDevExt = pDevobj->DeviceExtension;
    PKWAIT_BLOCK pwbArray = NULL;
    NTSTATUS ntStatus;
    KEVENT dummyEvent;
    BOOLEAN bKeepRunning = TRUE;
    BOOLEAN bCancelled = FALSE;
    BOOLEAN bFirstTime = TRUE;
    PLIST_ENTRY headOfList;
    PIRP pDispatchIrp = NULL;
    LARGE_INTEGER checkRegInterval;
    KIRQL levelOrHandle;

    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
    {
        QCFLT_DbgPrint
        (
            DBG_LEVEL_CRITICAL,
            ("<%s> ControlFilterThread: wrong IRQL\n", pDevExt->PortName)
        );
    }

    // allocate a wait block array for the multiple wait
    pwbArray = _ExAllocatePool
    (
        NonPagedPool,
        (QCFLT_FILTER_EVENT_COUNT + 1) * sizeof(KWAIT_BLOCK),
        "Flt.pwbArray"
    );
    if (pwbArray == NULL)
    {
        QCFLT_DbgPrint
        (
            DBG_LEVEL_CRITICAL,
            ("<%s> ControlFilterThread: STATUS_NO_MEMORY 1\n", pDevExt->PortName)
        );
        _closeHandle(pDevExt->FilterThread.hFilterThreadHandle, "D-1");
        PsTerminateSystemThread(STATUS_NO_MEMORY);
    }

    pDevExt->FilterThread.pFilterEvents[DUMMY_EVENT_INDEX] = &dummyEvent;
    KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
    KeClearEvent(&dummyEvent);

    while (bKeepRunning == TRUE)
    {

        QCFLT_DbgPrint
        (
            DBG_LEVEL_TRACE,
            ("<%s> FilterThread: BEGIN %d\n", pDevExt->PortName, bCancelled)
        );

        if (bCancelled == TRUE)
        {
            // TODO: Purge the DispatchQueue
            //USBFLT_PurgeQueue(pDevExt, &pDevExt->DispatchQueue, TRUE, 4);
            QcAcquireSpinLock(&pDevExt->FilterSpinLock, &levelOrHandle);
            if (IsListEmpty(&pDevExt->DispatchQueue))
            {
                // nothing left, exit main loop
                QcReleaseSpinLock(&pDevExt->FilterSpinLock, levelOrHandle);
                bKeepRunning = FALSE;
                continue;
            }
            QcReleaseSpinLock(&pDevExt->FilterSpinLock, levelOrHandle);
            goto dispatch_wait;
        }

        // De-queue
        QcAcquireSpinLock(&pDevExt->FilterSpinLock, &levelOrHandle);

        if ((!IsListEmpty(&pDevExt->DispatchQueue)) && (pDispatchIrp == NULL))
        {
            PLIST_ENTRY   peekEntry;

            headOfList = &pDevExt->DispatchQueue;
            peekEntry = headOfList->Flink;
            pDispatchIrp = CONTAINING_RECORD
            (
                peekEntry,
                IRP,
                Tail.Overlay.ListEntry
            );


            // De-queue
            RemoveEntryList(peekEntry);
            pDevExt->pCurrentDispatch = pDispatchIrp;

            QcReleaseSpinLock(&pDevExt->FilterSpinLock, levelOrHandle);

            if (pDispatchIrp != NULL)
            {
                if (IoSetCancelRoutine(pDispatchIrp, NULL) == NULL)
                {
                    QCFLT_DbgPrint
                    (
                        DBG_LEVEL_CRITICAL,
                        ("<%s> FilterThread: Cxled IRP 0x%p\n", pDevExt->PortName, pDispatchIrp)
                    );
                    pDispatchIrp = pDevExt->pCurrentDispatch = NULL;
                    continue;
                }
                else if (bCancelled == TRUE)
                {
                    QCFLT_DbgPrint
                    (
                        DBG_LEVEL_DETAIL,
                        ("<%s> FilterThread CIRP: (Cx 0x%p)\n", pDevExt->PortName, pDispatchIrp)
                    );
                    pDispatchIrp->IoStatus.Status = STATUS_CANCELLED;
                    // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("ControlFilterThread : Release RemoveLock\n"));
                    IoReleaseRemoveLock(&pDevExt->RemoveLock, pDispatchIrp);
                    IoCompleteRequest(pDispatchIrp, IO_NO_INCREMENT);
                    pDispatchIrp = pDevExt->pCurrentDispatch = NULL;
                    continue;
                }

                QCFLT_DbgPrint
                (
                    DBG_LEVEL_DETAIL,
                    ("<%s> QCFILTER THREAD: Processing the Dispatch IRP \n", pDevExt->PortName)
                );
                ProcessDispatchIrp(pDevobj, pDispatchIrp);
                pDispatchIrp = pDevExt->pCurrentDispatch = NULL;
            }
        }
        else
        {
            QcReleaseSpinLock(&pDevExt->FilterSpinLock, levelOrHandle);
            if ((bCancelled == TRUE))
            {
                bKeepRunning = FALSE;
                continue;
            }
        } //if ((!IsListEmpty(&pDevExt->DispatchQueue)) && (pDispatchIrp == NULL))

dispatch_wait:

        if (bFirstTime == TRUE)
        {
            bFirstTime = FALSE;
            KeSetEvent(&pDevExt->FilterThread.FilterThreadStartedEvent, IO_NO_INCREMENT, FALSE);
        }

        QCFLT_DbgPrint
        (
            DBG_LEVEL_TRACE,
            ("<%s> D: WAIT...\n", pDevExt->PortName)
        );

        checkRegInterval.QuadPart = -(10 * 1000 * 1000); // 1.0 sec
        ntStatus = KeWaitForMultipleObjects
        (
            QCFLT_FILTER_EVENT_COUNT,
            (VOID **)&pDevExt->FilterThread.pFilterEvents,
            WaitAny,
            Executive,
            KernelMode,
            FALSE,
            &checkRegInterval,
            pwbArray
        );

        switch (ntStatus)
        {
            case DUMMY_EVENT_INDEX:
            {
                KeClearEvent(&dummyEvent);
                QCFLT_DbgPrint
                (
                    DBG_LEVEL_DETAIL,
                    ("<%s> D: DUMMY_EVENT\n", pDevExt->PortName)
                );
                goto dispatch_wait;
            }

            case QCFLT_FILTER_CREATE_CONTROL:
            {
                KeClearEvent(&pDevExt->FilterThread.FilterCreateControlDeviceEvent);
                QCFLT_DbgPrint
                (
                    DBG_LEVEL_TRACE,
                    ("<%s> D: FilterCreateControlDeviceEvent\n", pDevExt->PortName)
                );
                continue;
            }

            case QCFLT_FILTER_CLOSE_CONTROL:
            {
                KeClearEvent(&pDevExt->FilterThread.FilterCloseControlDeviceEvent);
                QCFLT_DbgPrint
                (
                    DBG_LEVEL_TRACE,
                    ("<%s> D: FilterCloseControlDeviceEvent\n", pDevExt->PortName)
                );
                continue;
            }

            case QCFLT_STOP_FILTER:
            {
                KeClearEvent(&pDevExt->FilterThread.FilterStopEvent);
                QCFLT_DbgPrint
                (
                    DBG_LEVEL_DETAIL,
                    ("<%s> D: CANCEL_DISPATCH_EVENT\n", pDevExt->PortName)
                );
                bCancelled = TRUE;
                continue;
            }

            default:
            {
                QCFLT_DbgPrint
                (
                    DBG_LEVEL_TRACE,
                    ("<%s> D: default sig 0x%x\n", pDevExt->PortName, ntStatus)
                );
                continue;
            }

        }
    } // end while keep running

    if (pwbArray != NULL)
    {
        _ExFreePool(pwbArray);
    }

    KeSetEvent
    (
        &pDevExt->FilterThread.FilterThreadExitEvent,
        IO_NO_INCREMENT,
        FALSE
    );

    QCFLT_DbgPrint
    (
        DBG_LEVEL_DETAIL,
        ("<%s> QCFILTER THREAD: Filter thread terminated\n", pDevExt->PortName)
    );

    PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}

/****************************************************************************
 *
 * function: QCFilter_StartFilterThread
 *
 * purpose:  This routine starts the thread for filter device object dispatch.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS
QCFilter_StartFilterThread(PDEVICE_OBJECT devObj)
{
    PDEVICE_EXTENSION pDevExt = devObj->DeviceExtension;
    NTSTATUS          ntStatus = STATUS_SUCCESS;
    KIRQL             irql = KeGetCurrentIrql();
    OBJECT_ATTRIBUTES objAttr;
    KIRQL levelOrHandle;

    if (pDevExt->Type != DEVICE_TYPE_QCFIDO)
    {
        QCFLT_DbgPrint
        (
            DBG_LEVEL_ERROR,
            ("<%s> QCFILTER THREAD The device object is not FIDO\n", pDevExt->PortName)
        );
        return STATUS_UNSUCCESSFUL;
    }

    // Make sure the write thread is created with IRQL==PASSIVE_LEVEL
    QcAcquireSpinLock(&pDevExt->FilterSpinLock, &levelOrHandle);

    if (((pDevExt->FilterThread.pFilterThread == NULL) &&
        (pDevExt->FilterThread.hFilterThreadHandle == NULL)) &&
        (irql > PASSIVE_LEVEL))
    {
        QCFLT_DbgPrint
        (
            DBG_LEVEL_DETAIL,
            ("<%s> QCFILTER THREAD IRQL high %d\n", pDevExt->PortName, irql)
        );
        QcReleaseSpinLock(&pDevExt->FilterSpinLock, levelOrHandle);
        return STATUS_UNSUCCESSFUL;
    }

    if ((pDevExt->FilterThread.hFilterThreadHandle == NULL) &&
        (pDevExt->FilterThread.pFilterThread == NULL))
    {
        NTSTATUS ntStatus;

        if (pDevExt->FilterThread.bFilterThreadInCreation == FALSE)
        {
            pDevExt->FilterThread.bFilterThreadInCreation = TRUE;
            QcReleaseSpinLock(&pDevExt->FilterSpinLock, levelOrHandle);
        }
        else
        {
            QcReleaseSpinLock(&pDevExt->FilterSpinLock, levelOrHandle);
            QCFLT_DbgPrint
            (
                DBG_LEVEL_DETAIL,
                ("<%s> QCFILTER THREAD in creation\n", pDevExt->PortName)
            );
            return STATUS_SUCCESS;
        }

        KeClearEvent(&pDevExt->FilterThread.FilterThreadStartedEvent);
        InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
        ntStatus = PsCreateSystemThread
        (
            OUT & pDevExt->FilterThread.hFilterThreadHandle,
            IN THREAD_ALL_ACCESS,
            IN & objAttr,     // POBJECT_ATTRIBUTES  ObjectAttributes
            IN NULL,         // HANDLE  ProcessHandle
            OUT NULL,        // PCLIENT_ID  ClientId
            IN(PKSTART_ROUTINE)ControlFilterThread,
            IN(PVOID)devObj
        );
        if (ntStatus != STATUS_SUCCESS)
        {
            QCFLT_DbgPrint
            (
                DBG_LEVEL_DETAIL,
                ("<%s> Filter THREAD failure 0x%x\n", pDevExt->PortName, ntStatus)
            );
            pDevExt->FilterThread.pFilterThread = NULL;
            pDevExt->FilterThread.hFilterThreadHandle = NULL;
            pDevExt->FilterThread.bFilterThreadInCreation = FALSE;
            return ntStatus;
        }

        ntStatus = KeWaitForSingleObject
        (
            &pDevExt->FilterThread.FilterThreadStartedEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        KeClearEvent(&pDevExt->FilterThread.FilterThreadStartedEvent);

        ntStatus = ObReferenceObjectByHandle
        (
            pDevExt->FilterThread.hFilterThreadHandle,
            THREAD_ALL_ACCESS,
            NULL,
            KernelMode,
            (PVOID *)&pDevExt->FilterThread.pFilterThread,
            NULL
        );
        if (!NT_SUCCESS(ntStatus))
        {
            QCFLT_DbgPrint
            (
                DBG_LEVEL_DETAIL,
                ("<%s> FILTER THREAD ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
            );
            pDevExt->FilterThread.pFilterThread = NULL;
        }
        else
        {
            QCFLT_DbgPrint
            (
                DBG_LEVEL_DETAIL,
                ("<%s> FILTER THREAD handle=0x%p thOb=0x%p\n", pDevExt->PortName,
                pDevExt->FilterThread.hFilterThreadHandle, pDevExt->FilterThread.pFilterThread)
            );
            ZwClose(pDevExt->FilterThread.hFilterThreadHandle);
            pDevExt->FilterThread.hFilterThreadHandle = NULL;
        }

        pDevExt->FilterThread.bFilterThreadInCreation = FALSE;
    }
    else
    {
        QcReleaseSpinLock(&pDevExt->FilterSpinLock, levelOrHandle);
    }

    QCFLT_DbgPrint
    (
        DBG_LEVEL_DETAIL,
        ("<%s> FILTER THREAD alive-%d    irql-0x%x\n", pDevExt->PortName, ntStatus, irql)
    );

    return ntStatus;
}  // QCFilter_StartFilterThread

/****************************************************************************
 *
 * function: QCFilter_CancelFilterThread
 *
 * purpose:  This routine cancel's the thread for filter device object dispatch.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
VOID QCFilter_CancelFilterThread
(
    PDEVICE_OBJECT pDevObj
)
{

    PDEVICE_EXTENSION pDevExt = pDevObj->DeviceExtension;
    NTSTATUS          ntStatus = STATUS_SUCCESS;
    KIRQL             irql = KeGetCurrentIrql();
    LARGE_INTEGER timeoutValue;

    if (pDevExt->Type != DEVICE_TYPE_QCFIDO)
    {
        QCFLT_DbgPrint
        (
            DBG_LEVEL_ERROR,
            ("<%s> QCFILTER THREAD The device object is not FIDO\n", pDevExt->PortName)
        );
        return;
    }

    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
    {
        QCFLT_DbgPrint
        (
            DBG_LEVEL_CRITICAL,
            ("<%s> QCFILTER THREAD: wrong IRQL\n", pDevExt->PortName)
        );
        return;
    }

    if (InterlockedIncrement(&pDevExt->FilterThread.FilterThreadInCancellation) > 1)
    {
        while ((pDevExt->FilterThread.hFilterThreadHandle != NULL) || (pDevExt->FilterThread.pFilterThread != NULL))
        {
            QCFLT_DbgPrint
            (
                DBG_LEVEL_ERROR,
                ("<%s> QCFILTER THREAD in pro %d\n", pDevExt->PortName, pDevExt->FilterThread.FilterThreadInCancellation)
            );
            QCFLT_Wait(pDevExt, -(3 * 1000 * 1000));  // 300ms
        }
        InterlockedDecrement(&pDevExt->FilterThread.FilterThreadInCancellation);
        return;
    }

    if ((pDevExt->FilterThread.hFilterThreadHandle == NULL) &&
        (pDevExt->FilterThread.pFilterThread == NULL))
    {
        QCFLT_DbgPrint
        (
            DBG_LEVEL_DETAIL,
            ("<%s> QCFILTER THREAD: already cancelled IODEV 0x%p\n", pDevExt->PortName, pDevExt)
        );
        InterlockedDecrement(&pDevExt->FilterThread.FilterThreadInCancellation);
        return;
    }

    if ((pDevExt->FilterThread.hFilterThreadHandle != NULL) || (pDevExt->FilterThread.pFilterThread != NULL))
    {
        KeSetEvent(&pDevExt->FilterThread.FilterStopEvent, IO_NO_INCREMENT, FALSE);
        QCFLT_DbgPrint
        (
            DBG_LEVEL_DETAIL,
            ("<%s> QCFILTER THREAD : Stopped\n", pDevExt->PortName)
        );

        if (pDevExt->FilterThread.pFilterThread != NULL)
        {
            ntStatus = KeWaitForSingleObject
            (
                pDevExt->FilterThread.pFilterThread,
                Executive,
                KernelMode,
                FALSE,
                NULL
            );
            ObDereferenceObject(pDevExt->FilterThread.pFilterThread);
            KeClearEvent(&pDevExt->FilterThread.FilterStopEvent);
            ZwClose(pDevExt->FilterThread.hFilterThreadHandle);
        }
        else // best effort
        {
            timeoutValue.QuadPart = -(50 * 1000 * 1000);   // 5.0 sec
            ntStatus = KeWaitForSingleObject
            (
                &pDevExt->FilterThread.FilterThreadExitEvent,
                Executive,
                KernelMode,
                FALSE,
                &timeoutValue
            );
            if (ntStatus == STATUS_TIMEOUT)
            {
                QCFLT_DbgPrint
                (
                    DBG_LEVEL_ERROR,
                    ("<%s> QCFILTER THREAD: timeout for dev 0x%p\n", pDevExt->PortName, pDevExt)
                );
            }
            KeClearEvent(&pDevExt->FilterThread.FilterThreadExitEvent);
            ZwClose(pDevExt->FilterThread.hFilterThreadHandle);
        }
        pDevExt->FilterThread.hFilterThreadHandle = NULL;
        pDevExt->FilterThread.pFilterThread = NULL;
    }
    else
    {
        QCFLT_DbgPrint
        (
            DBG_LEVEL_DETAIL,
            ("<%s> QCFILTER THREAD: device already closed, no action.\n", pDevExt->PortName)
        );
    }

    InterlockedDecrement(&pDevExt->FilterThread.FilterThreadInCancellation);

    QCFLT_DbgPrint
    (
        DBG_LEVEL_DETAIL,
        ("<%s> QCFILTER THREAD: 0x%p\n", pDevExt->PortName,
        pDevExt)
    );
}
