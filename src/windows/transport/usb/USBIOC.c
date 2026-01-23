/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B I O C . C

GENERAL DESCRIPTION
    Main file for miniport layer, it provides entry points to the miniport.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "USBMAIN.h"
#include "USBIOC.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBIOC.tmh"

#endif   // EVENT_TRACING

NTSTATUS USBIOC_NotifyClient(PIRP pIrp, ULONG info)
{
    if (IoSetCancelRoutine(pIrp, NULL) == NULL)
    {
        return STATUS_UNSUCCESSFUL;
    }
    *(ULONG *)pIrp->AssociatedIrp.SystemBuffer = info;
    pIrp->IoStatus.Information = sizeof(ULONG);
    pIrp->IoStatus.Status = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

NTSTATUS USBIOC_CacheNotificationIrp(PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp)
{
    PDEVICE_EXTENSION pDevExt;
    NTSTATUS ntStatus;
    KIRQL IrqLevel, IrqLevelCancelSpinlock;
    PIO_STACK_LOCATION irpStack;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif
    KIRQL irql = KeGetCurrentIrql();

    pDevExt = DeviceObject->DeviceExtension;
    pIrp->IoStatus.Information = 0; // default is error for now
    ntStatus = STATUS_INVALID_PARAMETER; // default is error for now

    if (!ioBuffer)
    {
        return ntStatus;
    }

    irpStack = IoGetCurrentIrpStackLocation(pIrp);
    if (irpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
    {
        ntStatus = STATUS_BUFFER_TOO_SMALL;
        return ntStatus;
    }

    if (pDevExt->bDeviceSurpriseRemoved == TRUE)
    {
        return STATUS_UNSUCCESSFUL;
    }

    if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
    {
        return STATUS_UNSUCCESSFUL;
    }

    QcAcquireSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, &levelOrHandle, irql);
    // Got duplicated requests, deny it.
    if (pDevExt->pNotificationIrp != NULL)
    {
        *(ULONG *)pIrp->AssociatedIrp.SystemBuffer = QCDEV_DUPLICATED_NOTIFICATION_REQ;
        pIrp->IoStatus.Information = sizeof(ULONG);
        pIrp->IoStatus.Status = STATUS_UNSUCCESSFUL;
        ntStatus = STATUS_UNSUCCESSFUL;
    }
    else
    {
        // always pend the IRP, never complete it immediately
        ntStatus = STATUS_PENDING;
        _IoMarkIrpPending(pIrp); // it should already be pending from the dispatch queue!
        pDevExt->pNotificationIrp = pIrp;
        IoSetCancelRoutine(pIrp, USBMAIN_NotificationIrpCancelRoutine);

        if (pIrp->Cancel)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> NotIrp: Cxled\n", pDevExt->PortName)
            );
            if (IoSetCancelRoutine(pIrp, NULL) != NULL)
            {
                pDevExt->pNotificationIrp = NULL;
                ntStatus = STATUS_CANCELLED;
            }
            else
            {
                // do nothing
            }
        } // if
    }
    QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);

    // the dispatch routine will complete the IRP is ntStatus is not pending

    return ntStatus;
}  // USBIOC_CacheNotificationIrp

NTSTATUS USBIOC_GetDriverGUIDString(PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp)
{
    NTSTATUS           nts = STATUS_SUCCESS;
    PDEVICE_EXTENSION  pDevExt;
    PIO_STACK_LOCATION irpStack;
    ULONG              inBufLen;
    ULONG              drvGuidLen;
    char *drvIdStr;

    pDevExt = DeviceObject->DeviceExtension;
    irpStack = IoGetCurrentIrpStackLocation(pIrp);

    if (pDevExt->ucModelType == MODELTYPE_NET)
    {
        drvGuidLen = strlen(QCUSB_DRIVER_GUID_DATA_STR);
        drvIdStr = QCUSB_DRIVER_GUID_DATA_STR;
    }
    else
    {
        drvGuidLen = strlen(QCUSB_DRIVER_GUID_UNKN_STR);
        drvIdStr = QCUSB_DRIVER_GUID_UNKN_STR;
    }

    inBufLen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inBufLen < drvGuidLen)
    {
        pIrp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (!ioBuffer)
    {
        pIrp->IoStatus.Information = 0;
        nts = STATUS_INVALID_PARAMETER;
    }
    else
    {
        pDevExt = DeviceObject->DeviceExtension;
        RtlCopyMemory(ioBuffer, drvIdStr, drvGuidLen);
        pIrp->IoStatus.Information = drvGuidLen;
    }
    return nts;
}  // USBIOC_GetDriverGUIDString

NTSTATUS USBIOC_GetServiceKey(PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp)
{
    NTSTATUS           nts = STATUS_SUCCESS;
    PDEVICE_EXTENSION  pDevExt;
    PIO_STACK_LOCATION irpStack;
    ULONG              inBufLen;
    ULONG              drvKeyLen;
    int                i;
    char *p;

    pDevExt = DeviceObject->DeviceExtension;
    irpStack = IoGetCurrentIrpStackLocation(pIrp);

    drvKeyLen = gServicePath.Length / 2;
    inBufLen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (inBufLen < (drvKeyLen + 1))
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> ServiceKey: expect buf siz %dB\n", pDevExt->PortName, (drvKeyLen + 1))
        );
        pIrp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (!ioBuffer)
    {
        pIrp->IoStatus.Information = 0;
        nts = STATUS_INVALID_PARAMETER;
    }
    else
    {
        p = (char *)ioBuffer;
        for (i = 0; i < drvKeyLen; i++)
        {
            p[i] = (char)gServicePath.Buffer[i];
        }
        p[i] = 0;
        pIrp->IoStatus.Information = drvKeyLen;
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> ServiceKey: %s\n", pDevExt->PortName, p)
        );
    }
    return nts;
}  // USBIOC_GetServiceKey

NTSTATUS USBIOC_GetDataHeaderLength
(
    PDEVICE_OBJECT DeviceObject,
    PVOID          pInBuf,
    PIRP           pIrp
)
{
    PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(pIrp);
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;

    if (irpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(USHORT))
    {
        pIrp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (pInBuf == NULL)
    {
        pIrp->IoStatus.Information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    *(USHORT *)pInBuf = (USHORT)QCDEV_DATA_HEADER_LENGTH;
    pIrp->IoStatus.Information = sizeof(USHORT);

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> GetDataHeaderLength: %lu\n", pDevExt->PortName, QCDEV_DATA_HEADER_LENGTH)
    );

    return STATUS_SUCCESS;
}  // USBIOC_GetDataHeaderLength

