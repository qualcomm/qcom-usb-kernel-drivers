/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B D E V . C

GENERAL DESCRIPTION
    This is the file which contains dispatch functions for QDSS driver.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "QDBMAIN.h"
#include "QDBDSP.h"

#ifdef EVENT_TRACING
#include "QDBDSP.tmh"
#endif

VOID QDBDSP_IoCompletion
(
    WDFREQUEST                  Request,
    WDFIOTARGET                 Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    WDFCONTEXT                  Context
)
{
    NTSTATUS         ntStatus;
    PDEVICE_CONTEXT  pDevContext;
    UNREFERENCED_PARAMETER(Target);

    pDevContext = (PDEVICE_CONTEXT)Context;
    ntStatus = CompletionParams->IoStatus.Status;

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBDSP_IoCompletion: 0x%p ST 0x%x\n", pDevContext->PortName, Request, ntStatus)
    );

    WdfRequestComplete(Request, ntStatus);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBDSP_IoCompletion: 0x%p ST 0x%x\n", pDevContext->PortName, Request, ntStatus)
    );

    return;
}  // QDBDSP_IoCompletion


VOID QDBDSP_IoDeviceControl
(
    WDFQUEUE   Queue,
    WDFREQUEST Request,
    size_t     OutputBufferLength,
    size_t     InputBufferLength,
    ULONG      IoControlCode
)
{
    NTSTATUS        ntStatus;
    PDEVICE_CONTEXT pDevContext;
    PFILE_CONTEXT   fileContext;
    PVOID           outBuffer;
    size_t          bufferSize = 0;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    pDevContext = QdbDeviceGetContext(WdfIoQueueGetDevice(Queue));
    fileContext = QdbFileGetContext(WdfRequestGetFileObject(Request));

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBDSP_IoDeviceControl: 0x%p code 0x%x\n", pDevContext->PortName,
        Request, IoControlCode)
    );

    if (fileContext->Type == QDB_FILE_TYPE_TRACE)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> QDBDSP_IoDeviceControl: [TRACE]\n", pDevContext->PortName)
        );
    }
    else if (fileContext->Type == QDB_FILE_TYPE_DEBUG)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> QDBDSP_IoDeviceControl: [DEBUG]\n", pDevContext->PortName)
        );
    }
    else if (fileContext->Type == QDB_FILE_TYPE_DPL)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> QDBDSP_IoDeviceControl: [DPL]\n", pDevContext->PortName)
        );
    }

    switch (IoControlCode)
    {
        case IOCTL_QCDEV_DPL_STATS:
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_DETAIL,
                ("<%s> QDBDSP_IoDeviceControl: IOCTL_QCDEV_DPL_STATS (IN/OUT %d/%d) \n",
                pDevContext->PortName, InputBufferLength, OutputBufferLength)
            );
            ntStatus = WdfRequestRetrieveOutputBuffer(Request, 0, &outBuffer, &bufferSize);
            if (NT_SUCCESS(ntStatus))
            {
                if (OutputBufferLength >= sizeof(QDB_STATS))
                {
                    RtlCopyMemory(outBuffer, &(pDevContext->Stats), sizeof(QDB_STATS));
                    WdfRequestSetInformation(Request, sizeof(QDB_STATS));
                }
                else
                {
                    ntStatus = STATUS_BUFFER_TOO_SMALL;
                }
            }
            else
            {
                ntStatus = STATUS_UNSUCCESSFUL;
            }

            break;
        }

        case IOCTL_QCDEV_REQUEST_DEVICEID:
        {
            ntStatus = WdfRequestRetrieveOutputBuffer(Request, 0, &outBuffer, &bufferSize);

            QDB_DbgPrint
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_DETAIL,
                ("<%s> QDBDSP_IoDeviceControl: IOCTL_QCDEV_REQUEST_DEVICEID (IN/OUT %d/%d/%d)\n",
                pDevContext->PortName, InputBufferLength, OutputBufferLength, bufferSize)
            );

            if (NT_SUCCESS(ntStatus))
            {
                ntStatus = QDBDSP_GetParentId(pDevContext, outBuffer, (ULONG)OutputBufferLength);
                if (NT_SUCCESS(ntStatus))
                {
                    WdfRequestSetInformation(Request, sizeof(ULONGLONG));
                }
            }
            break;
        }

        default:
        {
            ntStatus = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }
    }  // switch

    WdfRequestComplete(Request, ntStatus);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBDSP_IoDeviceControl: code 0x%x (NTS 0x%x)\n", pDevContext->PortName, IoControlCode, ntStatus)
    );

    return;
}  // QDBDSP_IoDeviceControl

VOID QDBDSP_IoStop
(
    WDFQUEUE   Queue,
    WDFREQUEST Request,
    ULONG      Flags
)
{
    PDEVICE_CONTEXT  pDevContext;

    pDevContext = QdbDeviceGetContext(WdfIoQueueGetDevice(Queue));

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBDSP_IoStop: queue 0x%p req 0x%p action 0x%x\n", pDevContext->PortName, Queue, Request, Flags)
    );

    if (Flags & WdfRequestStopActionPurge)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> <--QDBDSP_IoStop: queue 0x%p req 0x%p - WdfRequestStopActionPurge\n", pDevContext->PortName, Queue, Request)
        );
        WdfRequestCancelSentRequest(Request);

        return;
    }

    if (Flags & WdfRequestStopActionSuspend)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_DETAIL,
            ("<%s> <--QDBDSP_IoStop: queue 0x%p req 0x%p - WdfRequestStopActionSuspend\n", pDevContext->PortName, Queue, Request)
        );
        WdfRequestStopAcknowledge(Request, FALSE);

        return;
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBDSP_IoStop: queue 0x%p req 0x%p action 0x%x\n", pDevContext->PortName, Queue, Request, Flags)
    );

    return;
}  // QDBDSP_IoStop

VOID QDBDSP_IoResume
(
    WDFQUEUE   Queue,
    WDFREQUEST Request
)
{
    PDEVICE_CONTEXT pDevContext;
    PFILE_CONTEXT   fileContext = NULL;
    WDFFILEOBJECT   fileObj;
    UCHAR           devType = 0;  // none

    pDevContext = QdbDeviceGetContext(WdfIoQueueGetDevice(Queue));
    if ((fileObj = WdfRequestGetFileObject(Request)) != NULL)
    {
        fileContext = QdbFileGetContext(fileObj);  // for debugging
        devType = fileContext->Type;
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBDSP_IoResume: File %d queue 0x%p req 0x%p\n", pDevContext->PortName,
        devType, Queue, Request)
    );

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBDSP_IoResume: queue 0x%p req 0x%p\n", pDevContext->PortName, Queue, Request)
    );

    return;
}  // QDBDSP_IoResume

NTSTATUS QDBDSP_IrpIoCompletion
(
    PDEVICE_OBJECT DeviceObject,
    PIRP           Irp,
    PVOID          Context
)
{
    PDEVICE_CONTEXT pDevContext = (PDEVICE_CONTEXT)Context;
    UNREFERENCED_PARAMETER(DeviceObject);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBDSP_IrpIoCompletion: (IRP 0x%p IoStatus: 0x%x)\n", pDevContext->PortName,
        Irp, Irp->IoStatus.Status)
    );

    KeSetEvent(Irp->UserEvent, 0, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}  // QDBDSP_IrpIoCompletion

NTSTATUS QDBDSP_GetParentId
(
    PDEVICE_CONTEXT pDevContext,
    PVOID           IoBuffer,
    ULONG           BufferLen
)
{
    PIRP               pIrp;
    PIO_STACK_LOCATION nextstack;
    NTSTATUS           ntStatus;
    KEVENT             event;
    ULONGLONG          pid = 0;

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBDSP_GetParentId\n", pDevContext->PortName)
    );
    KeInitializeEvent(&event, SynchronizationEvent, FALSE);

    pIrp = IoAllocateIrp((CCHAR)(pDevContext->MyDevice->StackSize + 1), FALSE);
    if (pIrp == NULL)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBDSP_GetParentId: failed to allocate IRP\n", pDevContext->PortName)
        );
        return STATUS_UNSUCCESSFUL;
    }

    pIrp->AssociatedIrp.SystemBuffer = IoBuffer;

    // set the event
    pIrp->UserEvent = &event;

    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    nextstack->Parameters.DeviceIoControl.IoControlCode = IOCTL_QCDEV_REQUEST_DEVICEID;
    nextstack->Parameters.DeviceIoControl.OutputBufferLength = BufferLen;

    IoSetCompletionRoutine
    (
        pIrp,
        (PIO_COMPLETION_ROUTINE)QDBDSP_IrpIoCompletion,
        (PVOID)pDevContext,
        TRUE, TRUE, TRUE
    );

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_ERROR,
        ("<%s> QDBDSP_GetParentId: IRP 0x%p DO StackSize %d\n", pDevContext->PortName, pIrp,
        pDevContext->MyDevice->StackSize)
    );

    ntStatus = IoCallDriver(pDevContext->TargetDevice, pIrp);

    KeWaitForSingleObject(&event, Executive, KernelMode, TRUE, 0);

    ntStatus = pIrp->IoStatus.Status;

    IoFreeIrp(pIrp);

    if (ntStatus == STATUS_SUCCESS)
    {
        pid = *((PULONGLONG)IoBuffer);
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBDSP_GetParentId: 0x%I64x\n", pDevContext->PortName, pid)
    );
    return ntStatus;

}  // QDBDSP_GetParentId

