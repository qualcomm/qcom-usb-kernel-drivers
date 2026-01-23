/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B S H R . C

GENERAL DESCRIPTION
    This file contains implementations for USB resource sharing

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "USBMAIN.h"
#include "USBSHR.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBSHR.tmh"

#endif   // EVENT_TRACING

static LIST_ENTRY ReadCtlEventList;
static KSPIN_LOCK ReadCtlListSpinLock;

VOID USBSHR_Initialize(VOID)
{
    InitializeListHead(&ReadCtlEventList);
    KeInitializeSpinLock(&ReadCtlListSpinLock);

}  // USBSHR_Initialize

PDeviceReadControlEvent USBSHR_AddReadControlElement
(
#ifdef QCUSB_SHARE_INTERRUPT_OLD
    UCHAR DeviceId[DEVICE_ID_LEN]
#else
    PDEVICE_OBJECT DeviceId
#endif
)
{
    PDeviceReadControlEvent element;
    int i;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE      levelOrHandle;
#else
    KIRQL                   levelOrHandle;
#endif

    element = USBSHR_GetReadCtlEventElement(DeviceId);

    if (element != NULL)
    {
        return element;
    }

    // Create new element
    element = ExAllocatePool(NonPagedPool, sizeof(DeviceReadControlEvent));

    if (element == NULL)
    {
        return NULL;
    }

#ifdef QCUSB_SHARE_INTERRUPT_OLD
    RtlCopyMemory(element->DeviceId, DeviceId, DEVICE_ID_LEN);
#else
    element->DeviceId = DeviceId;
#endif
    for (i = 0; i < MAX_INTERFACE; i++)
    {
        KeInitializeEvent
        (
            &(element->ReadControlEvent[i]),
            NotificationEvent,
            FALSE
        );
        element->RspAvailableCount[i] = 0;
    }

    QcAcquireSpinLock(&ReadCtlListSpinLock, &levelOrHandle);
    InsertTailList(&ReadCtlEventList, &element->List);
    QcReleaseSpinLock(&ReadCtlListSpinLock, levelOrHandle);

    return element;

}  // USBSHR_InitializeReadControlElement

PKEVENT USBSHR_GetReadControlEvent
(
    PDEVICE_OBJECT DeviceObject,
    USHORT         Interface
)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    PDeviceReadControlEvent element;
    PKEVENT                 eventFound = NULL;

#ifdef QCUSB_SHARE_INTERRUPT

    if (Interface >= MAX_INTERFACE)
    {
        return NULL;
    }

    if (pDevExt->bEnableResourceSharing == FALSE)
    {
        return pDevExt->pReadControlEvent;
    }

    element = USBSHR_GetReadCtlEventElement(pDevExt->DeviceId);

    if (element != NULL)
    {
        eventFound = &(element->ReadControlEvent[Interface]);
    }

#endif // QCUSB_SHARE_INTERRUPT

    return eventFound;
}  // USBSHR_GetReadControlEvent

PLONG USBSHR_GetRspAvailableCount
(
    PDEVICE_OBJECT DeviceObject,
    USHORT         Interface
)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    PDeviceReadControlEvent element;
    PLONG                   countFound = NULL;

#ifdef QCUSB_SHARE_INTERRUPT

    if (Interface >= MAX_INTERFACE)
    {
        return NULL;
    }

    if (pDevExt->bEnableResourceSharing == FALSE)
    {
        return pDevExt->pRspAvailableCount;
    }

    element = USBSHR_GetReadCtlEventElement(pDevExt->DeviceId);

    if (element != NULL)
    {
        countFound = &(element->RspAvailableCount[Interface]);
    }

#endif // QCUSB_SHARE_INTERRUPT

    return countFound;
}  // USBSHR_GetRspAvailableCount

PDeviceReadControlEvent USBSHR_GetReadCtlEventElement
(
#ifdef QCUSB_SHARE_INTERRUPT_OLD   
    UCHAR  DeviceId[DEVICE_ID_LEN]
#else
    PDEVICE_OBJECT DeviceId
#endif   
)
{
    PDeviceReadControlEvent element, elementFound = NULL;
    PLIST_ENTRY             headOfList, tailOfList;
    LIST_ENTRY              tempList;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE      levelOrHandle;
#else
    KIRQL                   levelOrHandle;
#endif

    InitializeListHead(&tempList);

    QcAcquireSpinLock(&ReadCtlListSpinLock, &levelOrHandle);
    // check to see if an instance exists
    while (!IsListEmpty(&ReadCtlEventList))
    {
        headOfList = RemoveHeadList(&ReadCtlEventList);
        element = CONTAINING_RECORD
        (
            headOfList,
            DeviceReadControlEvent,
            List
        );
        InsertTailList(&tempList, &element->List);

#ifdef QCUSB_SHARE_INTERRUPT_OLD   
        if (DEVICE_ID_LEN == RtlCompareMemory
        (
            element->DeviceId,
            DeviceId,
            DEVICE_ID_LEN
            ))
#else
        if (element->DeviceId == DeviceId)
#endif          
        {
            elementFound = element;
            break;
        }
    }

    // restore global list
    while (!IsListEmpty(&tempList))
    {
        tailOfList = RemoveTailList(&tempList);
        element = CONTAINING_RECORD
        (
            tailOfList,
            DeviceReadControlEvent,
            List
        );
        InsertHeadList(&ReadCtlEventList, &element->List);
    }

    QcReleaseSpinLock(&ReadCtlListSpinLock, levelOrHandle);

    return elementFound;

}  // USBSHR_GetReadCtlEventElement

VOID USBSHR_FreeReadControlElement
(
    PDeviceReadControlEvent pElement
)
{
    PDeviceReadControlEvent element;
    PLIST_ENTRY             headOfList;
#ifdef QCUSB_TARGET_XP
    KLOCK_QUEUE_HANDLE      levelOrHandle;
#else
    KIRQL                   levelOrHandle;
#endif

    if (pElement != NULL)
    {
        QcAcquireSpinLock(&ReadCtlListSpinLock, &levelOrHandle);
        RemoveEntryList(&pElement->List);
        ExFreePool(pElement);
        QcReleaseSpinLock(&ReadCtlListSpinLock, levelOrHandle);
        return;
    }

    // free the whole list
    QcAcquireSpinLock(&ReadCtlListSpinLock, &levelOrHandle);
    while (!IsListEmpty(&ReadCtlEventList))
    {
        headOfList = RemoveHeadList(&ReadCtlEventList);
        element = CONTAINING_RECORD
        (
            headOfList,
            DeviceReadControlEvent,
            List
        );
        ExFreePool(element);
    }
    QcReleaseSpinLock(&ReadCtlListSpinLock, levelOrHandle);

}  // USBSHR_FreeReadControlElement

VOID USBSHR_ResetRspAvailableCount
(
    PDEVICE_OBJECT DeviceObject,
    USHORT         Interface
)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    PDeviceReadControlEvent element;
    int i;

#ifdef QCUSB_SHARE_INTERRUPT

    if (pDevExt == NULL)
    {
        return;
    }

    if (pDevExt->bEnableResourceSharing == FALSE)
    {
        InterlockedExchange(pDevExt->pRspAvailableCount, 0);
        return;
    }

    element = USBSHR_GetReadCtlEventElement(pDevExt->DeviceId);
    if (element == NULL)
    {
        return;
    }

    if (Interface >= MAX_INTERFACE)
    {
        for (i = 0; i < MAX_INTERFACE; i++)
        {
            InterlockedExchange(&(element->RspAvailableCount[i]), 0);
        }
    }
    else
    {
        InterlockedExchange(&(element->RspAvailableCount[Interface]), 0);
    }

#endif // QCUSB_SHARE_INTERRUPT

}  // USBSHR_ResetRspAvailableCount
