/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B E N C . C

GENERAL DESCRIPTION
    This file contains functions for handling USB CDC encapsulated commands.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "USBMAIN.h"
#include "USBENC.h"
#include "USBUTL.h"
#include "USBIOC.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBENC.tmh"

#endif   // EVENT_TRACING

NTSTATUS USBENC_Enqueue(PDEVICE_OBJECT DeviceObject, PIRP Irp, KIRQL Irql)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    NTSTATUS ntStatus = STATUS_SUCCESS;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    // We assume the dispatch thread has been created and running.
    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CIRP,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> ENC_Enqueue: 0x%p\n", pDevExt->PortName, Irp)
    );

    ntStatus = STATUS_PENDING;

    QcAcquireSpinLockWithLevel(&pDevExt->ControlSpinLock, &levelOrHandle, Irql);
    if (Irp->Cancel)
    {
        PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> USBENC_Enqueue: CANCELLED\n", pDevExt->PortName)
        );
        ntStatus = Irp->IoStatus.Status = STATUS_CANCELLED;
        QcReleaseSpinLockWithLevel(&pDevExt->ControlSpinLock, levelOrHandle, Irql);
        goto enc_enq_exit;
    }


    _IoMarkIrpPending(Irp);
    IoSetCancelRoutine(Irp, USBENC_CancelQueued);
    InsertTailList(&pDevExt->EncapsulatedDataQueue, &Irp->Tail.Overlay.ListEntry);

    QcReleaseSpinLockWithLevel(&pDevExt->ControlSpinLock, levelOrHandle, Irql);

enc_enq_exit:

    if (ntStatus != STATUS_PENDING)
    {
        IoSetCancelRoutine(Irp, NULL);
        Irp->IoStatus.Status = ntStatus;
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> CIRP (Cx 0x%x) 0x%p\n", pDevExt->PortName, ntStatus, Irp)
        );
        QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return ntStatus;
}  // USBENC_Enqueue

NTSTATUS USBENC_CDC_SendEncapsulatedCommand
(
    PDEVICE_OBJECT DeviceObject,
    USHORT         Interface,
    PIRP           pIrp
)
{
    PUSB_DEFAULT_PIPE_REQUEST pRequest;
    NTSTATUS nts = STATUS_SUCCESS;
    ULONG ulRetSize;
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(pIrp);
    PVOID ioBuffer = pIrp->AssociatedIrp.SystemBuffer;
    ULONG Length = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    PVOID pBuf;
    ULONG bufSize;

    bufSize = sizeof(USB_DEFAULT_PIPE_REQUEST) + Length + 64; // enough for mux
    pBuf = ExAllocatePool(NonPagedPool, bufSize);
    if (pBuf == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(pBuf, bufSize);

    pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;
    pRequest->bmRequestType = CLASS_HOST_TO_INTERFACE;
    pRequest->bRequest = CDC_SEND_ENCAPSULATED_CMD;
    pRequest->wIndex = Interface;
    pRequest->wValue = 0;
    pRequest->wLength = Length;

#ifdef QCUSB_MUX_PROTOCOL

    if (pDevExt->MuxEnabled == TRUE)
    {
        ULONG frameLen = 0;

        USBIF_EncodeMuxFrame
        (
            pDevExt->MyDeviceObject,
            ioBuffer, Length,
            &pRequest->Data[0],
            &frameLen,  // returned mux frame length
            FALSE       // not data frame
        );
        pRequest->wLength = frameLen;  // USB payload length

        // adjust USB buffer length for send
        bufSize = sizeof(USB_DEFAULT_PIPE_REQUEST) + frameLen;
    }
    else
    {
        RtlCopyMemory(&pRequest->Data[0], ioBuffer, Length);
    }

#else

    RtlCopyMemory(&pRequest->Data[0], ioBuffer, Length);

#endif // QCUSB_MUX_PROTOCOL

    USBUTL_PrintBytes
    (
        &pRequest->Data[0],
        pRequest->wLength, pRequest->wLength,
        "SendEncap", pDevExt,
        QCUSB_DBG_MASK_ENDAT,
        QCUSB_DBG_LEVEL_VERBOSE
    );

    nts = QCUSB_PassThrough
    (
        DeviceObject, pBuf, bufSize, &ulRetSize
    );

    ExFreePool(pBuf);

    return nts;
} // QCUSB_CDC_SendEncapsulatedCommand

NTSTATUS USBENC_CDC_GetEncapsulatedResponse
(
    PDEVICE_OBJECT DeviceObject,
    USHORT         Interface
)
{
    PIRP pIrp;
    PUSB_DEFAULT_PIPE_REQUEST pRequest;
    NTSTATUS nts = STATUS_SUCCESS;
    ULONG ulRetSize = 0;
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpStack;
    PVOID ioBuffer;
    ULONG Length = USBENC_MAX_ENCAPSULATED_DATA_LENGTH, reqLen, cpLen;
    PVOID pBuf;
    ULONG bufSize;
    PLIST_ENTRY headOfList;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif
    PCHAR pDataPtr;

    /* bmRequestType:
     * 1 01 00001:
     *    D7: direction
              (0=host->device, 1=device->host)
     *    D6..5: type
              (0=standard, 1=class, 2-vendor, 3=other)
     *    D4..0: Recipient
              (0=device, 1=interface, 2=endpoint, 3=other, 4..31=reserved)
     */

    QCUSB_DbgPrint2
    (
        QCUSB_DBG_MASK_ENC,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> GetEncapsulatedResponse -- 0\n", pDevExt->PortName)
    );
    bufSize = sizeof(USB_DEFAULT_PIPE_REQUEST) + Length;
    pBuf = ExAllocatePool(NonPagedPool, bufSize);
    if (pBuf == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(pBuf, bufSize);

    pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;
    pRequest->bmRequestType = CLASS_INTERFACE_TO_HOST;
    pRequest->bRequest = CDC_GET_ENCAPSULATED_RSP;  // 0x01
    pRequest->wValue = 0;
    pRequest->wIndex = Interface;
    pRequest->wLength = Length;

    nts = QCUSB_PassThrough
    (
        DeviceObject, pBuf, bufSize, &ulRetSize
    );

    if ((ulRetSize > Length) || (!NT_SUCCESS(nts)))
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_ENC,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> enc get failure %u/%u (0x%x)\n", pDevExt->PortName,
            ulRetSize, Length, nts)
        );
        ExFreePool(pBuf);
        return STATUS_UNSUCCESSFUL; // nts; // could be STATUS_SUCCESS
    }
    else
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_ENC,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> ENC: got %uB (0x%x)\n", pDevExt->PortName, ulRetSize, nts)
        );
        USBUTL_PrintBytes
        (
            &(pRequest->Data[0]),
            ulRetSize,
            ulRetSize,
            "GetEncap",
            pDevExt,
            QCUSB_DBG_MASK_ENDAT,
            QCUSB_DBG_LEVEL_VERBOSE
        );
    }

    pDataPtr = &(pRequest->Data[0]);

#ifdef QCUSB_MUX_PROTOCOL

    if (pDevExt->MuxEnabled == TRUE)
    {
        ULONG  muxPayloadLength = 0;
        PUCHAR p;

        p = USBIF_DecodeMuxFrame
        (
            pDevExt->MyDeviceObject,
            pDataPtr,
            ulRetSize,
            &muxPayloadLength,
            FALSE
        );

        // adjust buffer ptr and length to reflect mux payload
        if ((p != NULL) && (muxPayloadLength != 0))
        {
            pDataPtr = p;
            ulRetSize = muxPayloadLength;
        }

        // drop the frame on error
        if ((p == NULL) && (muxPayloadLength == 0))
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_ENC,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> ENC: [MUX] frame dropped\n", pDevExt->PortName)
            );
            ExFreePool(pBuf);
            return STATUS_UNSUCCESSFUL;
        }
    }

#endif // QCUSB_MUX_PROTOCOL

    // dequeue read control irp
    QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
    if (!IsListEmpty(&pDevExt->EncapsulatedDataQueue))
    {
        headOfList = RemoveHeadList(&pDevExt->EncapsulatedDataQueue);
        pIrp = CONTAINING_RECORD
        (
            headOfList,
            IRP,
            Tail.Overlay.ListEntry
        );

        if (pIrp != NULL)
        {
            if (IoSetCancelRoutine(pIrp, NULL) == NULL)
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_CONTROL,
                    QCUSB_DBG_LEVEL_CRITICAL,
                    ("<%s> ENC: Cxled IRP 0x%p\n", pDevExt->PortName, pIrp)
                );
            }
            else
            {
                irpStack = IoGetCurrentIrpStackLocation(pIrp);
                ioBuffer = pIrp->AssociatedIrp.SystemBuffer;
                reqLen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
                if (ulRetSize > reqLen)
                {
                    cpLen = reqLen;
                    pIrp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
                }
                else
                {
                    cpLen = ulRetSize;
                    pIrp->IoStatus.Status = STATUS_SUCCESS;
                }
                RtlCopyMemory(ioBuffer, pDataPtr, cpLen);
                pIrp->IoStatus.Information = cpLen;
                InsertTailList(&pDevExt->CtlCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
                KeSetEvent(&pDevExt->InterruptEmptyCtlQueueEvent, IO_NO_INCREMENT, FALSE);
            }
        }
        else
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_ENC,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> ENC: de-Q failure\n", pDevExt->PortName)
            );
        }
    }
    else
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_ENC,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> ENC: no pending read, drop data (%uB)\n", pDevExt->PortName, ulRetSize)
        );
    }
    QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

    ExFreePool(pBuf);

    QCUSB_DbgPrint2
    (
        QCUSB_DBG_MASK_ENC,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> GetEncapsulatedResponse -- 1(%u)\n", pDevExt->PortName,
        *(pDevExt->pRspAvailableCount))
    );

    return nts;

}  // USBENC_CDC_GetEncapsulatedResponse

VOID USBENC_PurgeQueue
(
    PDEVICE_EXTENSION pDevExt,
    PLIST_ENTRY       queue,
    BOOLEAN           ctlItemOnly,
    UCHAR             cookie
)
{
    ULONG                  ioControlCode;
    PIO_STACK_LOCATION     irpStack;
    PLIST_ENTRY headOfList, tailOfList;
    LIST_ENTRY  tmpQueue;
    PIRP pIrp;
    BOOLEAN bDone = FALSE;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif
    int rstItem = 0;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> ENC: PurgeQueue - %d\n", pDevExt->PortName, cookie)
    );

    InitializeListHead(&tmpQueue);

    QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);

    while (bDone == FALSE)
    {
        if (!IsListEmpty(queue))
        {
            headOfList = RemoveHeadList(queue);
            pIrp = CONTAINING_RECORD
            (
                headOfList,
                IRP,
                Tail.Overlay.ListEntry
            );
            if (pIrp == NULL)
            {
                continue;
            }

            // examine the pIrp to see if it needs to be cancelled
            if (ctlItemOnly == TRUE)
            {
                // if pIrp is not ENC I/O, queue to tmpQueue and go back to input queue
                irpStack = IoGetCurrentIrpStackLocation(pIrp);
                ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;
                if (irpStack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
                {
                    if ((ioControlCode == IOCTL_QCDEV_READ_CONTROL) ||
                        (ioControlCode == IOCTL_QCDEV_SEND_CONTROL))
                    {
                        // Cancel the IRP
                        goto cancel_irp;
                    }
                }

                InsertTailList(&tmpQueue, &pIrp->Tail.Overlay.ListEntry);
                continue;
            }

cancel_irp:

            if (IoSetCancelRoutine(pIrp, NULL) != NULL)
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_CIRP,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> CIRP: (Cx 0x%p ENC RmlCount[0]=%u)\n", pDevExt->PortName, pIrp,
                    pDevExt->Sts.lRmlCount[0])
                );
                pIrp->IoStatus.Status = STATUS_CANCELLED;
                pIrp->IoStatus.Information = 0;
                InsertTailList(&pDevExt->CtlCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
                KeSetEvent(&pDevExt->InterruptEmptyCtlQueueEvent, IO_NO_INCREMENT, FALSE);
            }
            else
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_CONTROL,
                    QCUSB_DBG_LEVEL_ERROR,
                    ("<%s> ENC: Cxled IRP 0x%p\n", pDevExt->PortName, pIrp)
                );
            }
        }
        else
        {
            bDone = TRUE;
        }
    }  // while

    // put tmp items back to queue
    while (!IsListEmpty(&tmpQueue))
    {
        tailOfList = RemoveTailList(&tmpQueue);
        pIrp = CONTAINING_RECORD
        (
            tailOfList,
            IRP,
            Tail.Overlay.ListEntry
        );
        InsertHeadList(queue, &pIrp->Tail.Overlay.ListEntry);
        rstItem++;
    }

    QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> ENC: PurgeQ: restored %d items, Rml[0]=%u\n", pDevExt->PortName, rstItem,
        pDevExt->Sts.lRmlCount[0])
    );

    return;
}  // USBENC_PurgeQueue

VOID USBENC_CancelQueued(PDEVICE_OBJECT CalledDO, PIRP Irp)
{
    KIRQL irql = KeGetCurrentIrql();
    PDEVICE_OBJECT DeviceObject = CalledDO;
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    BOOLEAN bIrpInQueue;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE levelOrHandle;
#else
    KIRQL levelOrHandle;
#endif

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> ENC CQed: 0x%p\n", pDevExt->PortName, Irp)
    );
    IoReleaseCancelSpinLock(Irp->CancelIrql);

    // De-queue
    QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
    bIrpInQueue = USBUTL_IsIrpInQueue
    (
        pDevExt,
        &pDevExt->EncapsulatedDataQueue,
        Irp,
        QCUSB_IRP_TYPE_CIRP,
        4
    );

    if (bIrpInQueue == TRUE)
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        InsertTailList(&pDevExt->CtlCompletionQueue, &Irp->Tail.Overlay.ListEntry);
        KeSetEvent(&pDevExt->InterruptEmptyCtlQueueEvent, IO_NO_INCREMENT, FALSE);
    }
    else
    {
        // The read IRP is in processing, we just re-install the cancel routine here.
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> ENC_Cxl: no action to active 0x%p\n", pDevExt->PortName, Irp)
        );
        IoSetCancelRoutine(Irp, USBENC_CancelQueued);
    }

    QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
}  // USBENC_CancelQueued

