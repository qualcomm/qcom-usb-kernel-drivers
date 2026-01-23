/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B M A I N . C

GENERAL DESCRIPTION
    This file contains entry functions for the USB driver.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/


#include "USBMAIN.h"
#include "USBIOC.h"
#include "USBWT.h"
#include "USBRD.h"
#include "USBUTL.h"
#include "USBPNP.h"
#include "USBDSP.h"
#include "USBINT.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBMAIN.tmh"

#endif   // EVENT_TRACING


BOOLEAN bPowerManagement = TRUE;  // global switch, OK

UNICODE_STRING gServicePath;
KEVENT   gSyncEntryEvent;
char     gDeviceName[255];
char     gServiceName[255];
int      gModemType = -1;
USHORT   ucThCnt = 0;
QCUSB_VENDOR_CONFIG gVendorConfig;
BOOLEAN  bAddNewDevice;
KSPIN_LOCK gGenSpinLock;


VOID USBMAIN_CleanupDeviceExtension
(
    PDEVICE_OBJECT DeviceObject
)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    KIRQL oldIrql;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> CleanupDeviceExtension DeviceObject 0x%p\n", pDevExt->PortName, DeviceObject)
    );

    USBUTL_CleanupReadWriteQueues(pDevExt);

    if (pDevExt->pUsbDevDesc != NULL)
    {
        ExFreePool(pDevExt->pUsbDevDesc);
        pDevExt->pUsbDevDesc = NULL;
    }

    if (pDevExt->pUsbConfigDesc != NULL)
    {
        ExFreePool(pDevExt->pUsbConfigDesc);
        pDevExt->pUsbConfigDesc = NULL;
    }

    if (pDevExt->UsbConfigUrb != NULL)
    {
        ExFreePool(pDevExt->UsbConfigUrb);
        pDevExt->UsbConfigUrb = NULL;
    }

    for (int i = 0; i < MAX_INTERFACE; i++)
    {
        if (pDevExt->Interface[i] != NULL)
        {
            ExFreePool(pDevExt->Interface[i]);
            pDevExt->Interface[i] = NULL;
        }
    }

    _freeUcBuf(pDevExt->ucLoggingDir);

    if (pDevExt->TlpFrameBuffer.Buffer != NULL)
    {
        ExFreePool(pDevExt->TlpFrameBuffer.Buffer);
        pDevExt->TlpFrameBuffer.Buffer = NULL;
    }

    if (pDevExt->pucReadBufferStart)
    {
        ExFreePool(pDevExt->pucReadBufferStart);
        pDevExt->pucReadBufferStart = NULL;
    }

    if (pDevExt->pL2ReadBuffer != NULL)
    {
        for (int i = 0; i < pDevExt->NumberOfL2Buffers; i++)
        {
            if (pDevExt->pL2ReadBuffer[i].Buffer != NULL)
            {
                ExFreePool(pDevExt->pL2ReadBuffer[i].Buffer);
                pDevExt->pL2ReadBuffer[i].Buffer = NULL;

                if (pDevExt->pL2ReadBuffer[i].Irp != NULL)
                {
                    IoFreeIrp(pDevExt->pL2ReadBuffer[i].Irp);
                    pDevExt->pL2ReadBuffer[i].Irp = NULL;
                }
            }
        }
        ExFreePool(pDevExt->pL2ReadBuffer);
        pDevExt->pL2ReadBuffer = NULL;
    }

#ifdef QCUSB_MULTI_WRITES
    PLIST_ENTRY   listEntry;
    PQCMWT_BUFFER writeBuffer;

    QcAcquireSpinLock(&pDevExt->WriteSpinLock, &oldIrql);
    while (!IsListEmpty(&pDevExt->MWriteIdleQueue))
    {
        listEntry = RemoveHeadList(&pDevExt->MWriteIdleQueue);
        writeBuffer = CONTAINING_RECORD(listEntry, QCMWT_BUFFER, List);
        if (writeBuffer->Irp != NULL)
        {
            IoFreeIrp(writeBuffer->Irp);
        }
        ExFreePool(writeBuffer);
    }
    QcReleaseSpinLock(&pDevExt->WriteSpinLock, oldIrql);
#endif

    QcEmptyCompletionQueue
    (
        pDevExt,
        &pDevExt->RdCompletionQueue,
        &pDevExt->ReadSpinLock,
        QCUSB_IRP_TYPE_RIRP
    );
    QcEmptyCompletionQueue
    (
        pDevExt,
        &pDevExt->WtCompletionQueue,
        &pDevExt->WriteSpinLock,
        QCUSB_IRP_TYPE_WIRP
    );
    QcEmptyCompletionQueue
    (
        pDevExt,
        &pDevExt->CtlCompletionQueue,
        &pDevExt->ControlSpinLock,
        QCUSB_IRP_TYPE_CIRP
    );
    QcEmptyCompletionQueue
    (
        pDevExt,
        &pDevExt->SglCompletionQueue,
        &pDevExt->SingleIrpSpinLock,
        QCUSB_IRP_TYPE_CIRP
    );

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> CleanupDeviceExtension completed\n", pDevExt->PortName)
    );
}

VOID USBMAIN_CancalNotificationIrp
(
    PDEVICE_OBJECT DeviceObject
)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    KIRQL oldIrql;
    PIRP  pIrp;

    QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &oldIrql);
    if ((pIrp = pDevExt->pNotificationIrp) != NULL)
    {
        if (IoSetCancelRoutine(pIrp, NULL) != NULL)
        {
            pDevExt->pNotificationIrp->IoStatus.Status = STATUS_CANCELLED;
            pDevExt->pNotificationIrp->IoStatus.Information = 0;
            pDevExt->pNotificationIrp = NULL;
            InsertTailList(&pDevExt->SglCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
            KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);
        }
    }
    QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, oldIrql);
}

VOID USBMAIN_CancalIoThreads
(
    PDEVICE_OBJECT DeviceObject
)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_INFO,
        ("<%s> USBMAIN_CancalIoThreads - Rx: 0x%x, Tx: 0x%x\n", pDevExt->PortName,
        USBRD_CancelReadThread(pDevExt, 2), USBWT_CancelWriteThread(pDevExt, 1))
    );
}

VOID USBMAIN_DriverUnload
(
    PDRIVER_OBJECT DriverObject
)
{
    _freeString(gServicePath);

#ifdef QCUSB_SHARE_INTERRUPT
    USBSHR_FreeReadControlElement(NULL);
#endif

    DbgPrint("   ================================\n");
    DbgPrint("     Driver(%d) Unloaded by System\n", gModemType);
    DbgPrint("       Version: %-10s         \n", gVendorConfig.DriverVersion);
    DbgPrint("       Device:  %-10s         \n", gDeviceName);
    DbgPrint("       Port:    %-50s         \n", gVendorConfig.PortName);
    DbgPrint("   ================================\n");
}

VOID USBMAIN_NotificationIrpCancelRoutine
(
    PDEVICE_OBJECT DeviceObject,
    PIRP pIrp
)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    KIRQL oldIrql;

    IoReleaseCancelSpinLock(pIrp->CancelIrql);

    QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &oldIrql);
    pDevExt->pNotificationIrp = NULL;
    pIrp->IoStatus.Status = STATUS_CANCELLED;
    pIrp->IoStatus.Information = 0;
    InsertTailList(&pDevExt->SglCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
    KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);
    QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, oldIrql);
}

VOID USBMAIN_DispatchDebugOutput
(
    PIRP               Irp,
    PIO_STACK_LOCATION IrpStack,
    PDEVICE_OBJECT     DeviceObject,
    KIRQL              Irql
)
{
    PDEVICE_EXTENSION pDevExt = NULL;
    size_t remainingLength = MAX_DEBUG_MESSAGE_LEN, usedLength = 0;
    char msgBuffer[MAX_DEBUG_MESSAGE_LEN], *msg = msgBuffer;
    RtlZeroMemory(msgBuffer, sizeof(msgBuffer));

    if (DeviceObject == NULL)
    {
        if (gVendorConfig.DebugLevel < QCUSB_DBG_LEVEL_DETAIL)
        {
            return;
        }
        RtlStringCchPrintfA(msg, remainingLength, "<%s DO: 0x%p, IRQL: %d, Irp: 0x%p> ", gDeviceName, DeviceObject, Irql, Irp);
    }
    else
    {
        pDevExt = DeviceObject->DeviceExtension;
        if (pDevExt->DebugLevel < QCUSB_DBG_LEVEL_DETAIL)
        {
            return;
        }
        RtlStringCchPrintfA(msg, remainingLength, "<%s DO: 0x%p, IRQL: %d, Irp: 0x%p> ", pDevExt->PortName, DeviceObject, Irql, Irp);
    }
    RtlStringCchLengthA(msgBuffer, MAX_DEBUG_MESSAGE_LEN, &usedLength);
    remainingLength = MAX_DEBUG_MESSAGE_LEN - usedLength;
    msg = msgBuffer + usedLength;

    switch (IrpStack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_CREATE");
            break;
        case IRP_MJ_CLOSE:
            RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_CLOSE");
            break;
        case IRP_MJ_CLEANUP:
            RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_CLEANUP");
            break;
        case IRP_MJ_DEVICE_CONTROL:
        {
            switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
            {
                case IOCTL_SERIAL_SET_BAUD_RATE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_BAUD_RATE");
                    break;
                case IOCTL_SERIAL_SET_QUEUE_SIZE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_QUEUE_SIZE");
                    break;
                case IOCTL_SERIAL_SET_LINE_CONTROL:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_LINE_CONTROL");
                    break;
                case IOCTL_SERIAL_SET_BREAK_ON:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_BREAK_ON");
                    break;
                case IOCTL_SERIAL_SET_BREAK_OFF:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_BREAK_OFF");
                    break;
                case IOCTL_SERIAL_IMMEDIATE_CHAR:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_IMMEDIATE_CHAR");
                    break;
                case IOCTL_SERIAL_SET_TIMEOUTS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_TIMEOUTS");
                    break;
                case IOCTL_SERIAL_GET_TIMEOUTS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_TIMEOUTS");
                    break;
                case IOCTL_SERIAL_SET_DTR:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_DTR");
                    break;
                case IOCTL_SERIAL_CLR_DTR:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_CLR_DTR");
                    break;
                case IOCTL_SERIAL_RESET_DEVICE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_RESET_DEVICE");
                    break;
                case IOCTL_SERIAL_SET_RTS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_RTS");
                    break;
                case IOCTL_SERIAL_CLR_RTS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_RTS");
                    break;
                case IOCTL_SERIAL_SET_XOFF:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_XOFF");
                    break;
                case IOCTL_SERIAL_SET_XON:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_XON");
                    break;
                case IOCTL_SERIAL_GET_WAIT_MASK:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_WAIT_MASK");
                    break;
                case IOCTL_SERIAL_SET_WAIT_MASK:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_WAIT_MASK");
                    break;
                case IOCTL_SERIAL_WAIT_ON_MASK:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_WAIT_ON_MASK");
                    break;
                case IOCTL_SERIAL_PURGE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_PURGE");
                    break;
                case IOCTL_SERIAL_GET_BAUD_RATE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_BAUD_RATE");
                    break;
                case IOCTL_SERIAL_GET_LINE_CONTROL:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_LINE_CONTROL");
                    break;
                case IOCTL_SERIAL_GET_CHARS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_CHARS");
                    break;
                case IOCTL_SERIAL_SET_CHARS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_CHARS");
                    break;
                case IOCTL_SERIAL_GET_HANDFLOW:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_HANDFLOW");
                    break;
                case IOCTL_SERIAL_SET_HANDFLOW:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_HANDFLOW");
                    break;
                case IOCTL_SERIAL_GET_MODEMSTATUS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_MODEMSTATUS");
                    break;
                case IOCTL_SERIAL_GET_COMMSTATUS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_COMMSTATUS");
                    break;
                case IOCTL_SERIAL_XOFF_COUNTER:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_XOFF_COUNTER");
                    break;
                case IOCTL_SERIAL_GET_PROPERTIES:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_PROPERTIES");
                    break;
                case IOCTL_SERIAL_GET_DTRRTS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_DTRRTS");
                    break;

                    // begin_winioctl
                case IOCTL_SERIAL_LSRMST_INSERT:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_LSRMST_INSERT");
                    break;
                case IOCTL_SERENUM_EXPOSE_HARDWARE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERENUM_EXPOSE_HARDWARE");
                    break;
                case IOCTL_SERENUM_REMOVE_HARDWARE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERENUM_REMOVE_HARDWARE");
                    break;
                case IOCTL_SERENUM_PORT_DESC:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERENUM_PORT_DESC");
                    break;
                case IOCTL_SERENUM_GET_PORT_NAME:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERENUM_GET_PORT_NAME");
                    break;
                    // end_winioctl

                case IOCTL_SERIAL_CONFIG_SIZE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_CONFIG_SIZE");
                    break;
                case IOCTL_SERIAL_GET_COMMCONFIG:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_COMMCONFIG");
                    break;
                case IOCTL_SERIAL_SET_COMMCONFIG:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_COMMCONFIG");
                    break;

                case IOCTL_SERIAL_GET_STATS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_STATS");
                    break;
                case IOCTL_SERIAL_CLEAR_STATS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_CLEAR_STATS");
                    break;
                case IOCTL_SERIAL_GET_MODEM_CONTROL:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_MODEM_CONTROL");
                    break;
                case IOCTL_SERIAL_SET_MODEM_CONTROL:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_MODEM_CONTROL");
                    break;
                case IOCTL_SERIAL_SET_FIFO_CONTROL:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_FIFO_CONTROL");
                    break;

                case IOCTL_QCDEV_WAIT_NOTIFY:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCDEV_WAIT_NOTIFY");
                    break;
                case IOCTL_QCDEV_DRIVER_ID:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCDEV_DRIVER_ID");
                    break;
                case IOCTL_QCDEV_GET_SERVICE_KEY:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCDEV_GET_SERVICE_KEY");
                    break;
                case IOCTL_QCDEV_SEND_CONTROL:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCDEV_SEND_CONTROL");
                    break;
                case IOCTL_QCDEV_READ_CONTROL:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCDEV_READ_CONTROL");
                    break;
                case IOCTL_QCDEV_GET_HDR_LEN:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCDEV_GET_HDR_LEN");
                    break;
                case IOCTL_QCDEV_LOOPBACK_DATA_PKT:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCDEV_LOOPBACK_DATA_PKT");
                    break;
                default:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_DEVICE_CONTROL/ControlCode_0x%x",
                                        IrpStack->Parameters.DeviceIoControl.IoControlCode);
            }
            break;
        }
        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
        {
            switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
            {
                case IOCTL_SERIAL_INTERNAL_DO_WAIT_WAKE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_INTERNAL_DEVICE_CONTROL/IOCTL_SERIAL_INTERNAL_DO_WAIT_WAKE");
                    break;
                case IOCTL_SERIAL_INTERNAL_CANCEL_WAIT_WAKE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_INTERNAL_DEVICE_CONTROL/IOCTL_SERIAL_INTERNAL_CANCEL_WAIT_WAKE");
                    break;
                case IOCTL_SERIAL_INTERNAL_BASIC_SETTINGS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_INTERNAL_DEVICE_CONTROL/IOCTL_SERIAL_INTERNAL_BASIC_SETTINGS");
                    break;
                case IOCTL_SERIAL_INTERNAL_RESTORE_SETTINGS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_INTERNAL_DEVICE_CONTROL/IOCTL_SERIAL_INTERNAL_RESTORE_SETTINGS");
                    break;
                default:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_INTERNAL_DEVICE_CONTROL/ControlCode_0x%x",
                                        IrpStack->Parameters.DeviceIoControl.IoControlCode);
            }
            break;
        }
        case IRP_MJ_POWER:
            switch (IrpStack->MinorFunction)
            {
                case IRP_MN_WAIT_WAKE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_POWER/IRP_MN_WAIT_WAKE");
                    break;
                case IRP_MN_POWER_SEQUENCE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_POWER/IRP_MN_POWER_SEQUENCE");
                    break;
                case IRP_MN_SET_POWER:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_POWER/IRP_MN_SET_POWER");
                    break;
                case IRP_MN_QUERY_POWER:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_POWER/IRP_MN_QUERY_POWER");
                    break;
                default:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_POWER/MinorFunc_0x%x", IrpStack->MinorFunction);
            }
            break;
        case IRP_MJ_PNP:
        {
            switch (IrpStack->MinorFunction)
            {
                case IRP_MN_QUERY_CAPABILITIES:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_QUERY_CAPABILITIES");
                    break;
                case IRP_MN_START_DEVICE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_START_DEVICE");
                    break;
                case IRP_MN_QUERY_STOP_DEVICE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_QUERY_STOP_DEVICE");
                    break;
                case IRP_MN_STOP_DEVICE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_STOP_DEVICE");
                    break;
                case IRP_MN_QUERY_REMOVE_DEVICE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_QUERY_REMOVE_DEVICE");
                    break;
                case IRP_MN_SURPRISE_REMOVAL:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_SURPRISE_REMOVAL");
                    break;
                case IRP_MN_REMOVE_DEVICE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_REMOVE_DEVICE");
                    break;
                case IRP_MN_QUERY_ID:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_QUERY_ID");
                    break;
                case IRP_MN_QUERY_PNP_DEVICE_STATE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_QUERY_PNP_DEVICE_STATE");
                    break;
                case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_QUERY_RESOURCE_REQUIREMENTS");
                    break;
                case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_FILTER_RESOURCE_REQUIREMENTS");
                    break;
                case IRP_MN_QUERY_DEVICE_RELATIONS:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_QUERY_DEVICE_RELATIONS(0x%x)",
                                        IrpStack->Parameters.QueryDeviceRelations.Type);
                    break;
                case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_QUERY_LEGACY_BUS_INFORMATION");
                    break;
                case IRP_MN_QUERY_INTERFACE:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_QUERY_INTERFACE");
                    break;
                case IRP_MN_QUERY_DEVICE_TEXT:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/IRP_MN_QUERY_DEVICE_TEXT");
                    break;
                default:
                    RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_PNP/MINOR_FUNCTION_0x%x",
                                        IrpStack->MinorFunction);
            }
            break;
        }
        case IRP_MJ_SYSTEM_CONTROL:
            RtlStringCchPrintfA(msg, remainingLength, "IRP_MJ_SYSTEM_CONTROL");
            break;
        default:
            RtlStringCchPrintfA(msg, remainingLength, "MAJOR_FUNCTIN: 0x%x", IrpStack->MajorFunction);
            break;
    }

    RtlStringCchLengthA(msgBuffer, MAX_DEBUG_MESSAGE_LEN, &usedLength);
    remainingLength = MAX_DEBUG_MESSAGE_LEN - usedLength;
    msg = msgBuffer + usedLength;
    RtlStringCchPrintfA(msg, remainingLength, "\n");

    if (DeviceObject == NULL)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("%s", msgBuffer)
        );
    }
    else
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("%s", msgBuffer)
        );
    }
}

VOID USBMAIN_ResetRspAvailableCount
(
    PDEVICE_EXTENSION pDevExt,
    USHORT Interface,
    char *info,
    UCHAR cookie
)
{
    QCUSB_DbgPrintX
    (
        pDevExt,
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> ResetRspAvailableCount - %d \n", info, cookie)
    );

    if (pDevExt != NULL)
    {
#ifdef QCUSB_SHARE_INTERRUPT
        USBSHR_ResetRspAvailableCount(pDevExt->MyDeviceObject, Interface);
#else
        InterlockedExchange(&(pDevExt->lRspAvailableCount), 0);
#endif
    }
}

NTSTATUS USBMAIN_StopDataThreads(PDEVICE_EXTENSION pDevExt, BOOLEAN CancelWaitWake)
{
    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> -->StopDataThreads\n", pDevExt->PortName)
    );

    if (pDevExt->bLowPowerMode == TRUE)
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _StopDataThreads: already in low power mode\n", pDevExt->PortName)
        );
        return STATUS_SUCCESS;
    }

    // D0 -> Dn, stop Device
    pDevExt->PowerSuspended = TRUE;
    pDevExt->bLowPowerMode = TRUE;

    QCUSB_CDC_SetInterfaceIdle
    (
        pDevExt->MyDeviceObject,
        pDevExt->DataInterface,
        TRUE,
        1
    );

    // If device powers down, drop DTR
    if ((pDevExt->PowerState >= PowerDeviceD3) &&
        ((pDevExt->SystemPower > PowerSystemWorking) ||
        (pDevExt->PrepareToPowerDown == TRUE)))
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> StopDataThreads: drop DTR\n", pDevExt->PortName)
        );
        USBCTL_ClrDtrRts(pDevExt->MyDeviceObject);
    }
    else
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> StopDataThreads: still in S0, keep DTR\n", pDevExt->PortName)
        );
    }
    USBRD_SetStopState(pDevExt, TRUE, 7);   // blocking call
    USBWT_SetStopState(pDevExt, TRUE);      // non-blocking
    USBINT_StopInterruptService(pDevExt, TRUE, CancelWaitWake, 5);
    USBMAIN_ResetRspAvailableCount
    (
        pDevExt,
        pDevExt->DataInterface,
        pDevExt->PortName,
        6
    );

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> <--StopDataThreads\n", pDevExt->PortName)
    );

    return STATUS_SUCCESS;

}  // USBMAIN_StopDataThreads

VOID USBMAIN_UpdateXferStats(PDEVICE_EXTENSION pDevExt, ULONG PktLen, BOOLEAN IsRx)
{
    // init stats
    if (PktLen == 0xFFFFFFFF)
    {
        RtlZeroMemory((PVOID) & (pDevExt->QcXferStats), sizeof(QC_XFER_STATISTICS));
        return;
    }

    if (IsRx == TRUE)
    {
        if (PktLen >= XFER_30K)
        {
            (pDevExt->QcXferStats.RxPktsMoreThan30k)++;
        }
        else if (PktLen >= XFER_20K)
        {
            (pDevExt->QcXferStats.RxPkts20kTo30k)++;
        }
        else if (PktLen >= XFER_10K)
        {
            (pDevExt->QcXferStats.RxPkts10kTo20k)++;
        }
        else
        {
            (pDevExt->QcXferStats.RxPktsLessThan10k)++;
        }
    }
    else
    {
        if (PktLen >= XFER_30K)
        {
            (pDevExt->QcXferStats.TxPktsMoreThan30k)++;
        }
        else if (PktLen >= XFER_20K)
        {
            (pDevExt->QcXferStats.TxPkts20kTo30k)++;
        }
        else if (PktLen >= XFER_10K)
        {
            (pDevExt->QcXferStats.TxPkts10kTo20k)++;
        }
        else
        {
            (pDevExt->QcXferStats.TxPktsLessThan10k)++;
        }
    }
}  // USBMAIN_UpdateXferStats

