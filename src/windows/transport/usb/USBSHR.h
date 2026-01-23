/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B S H R . H

GENERAL DESCRIPTION
    This file contains definitions for USB resource sharing.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBSHR_H
#define USBSHR_H

#ifdef QCUSB_TARGET_XP
#include <../../wxp/ntddk.h>
#else
#include <ndis.h>
#endif

#define DEVICE_ID_LEN 16
#define MAX_INTERFACE 0xFF

typedef struct _DeviceReadControlEvent
{
    LIST_ENTRY List;
#ifdef QCUSB_SHARE_INTERRUPT_OLD   
    UCHAR      DeviceId[DEVICE_ID_LEN];
#else
    PDEVICE_OBJECT DeviceId;
#endif
    KEVENT     ReadControlEvent[MAX_INTERFACE];
    LONG       RspAvailableCount[MAX_INTERFACE];
} DeviceReadControlEvent, *PDeviceReadControlEvent;

VOID USBSHR_Initialize(VOID);

PDeviceReadControlEvent USBSHR_AddReadControlElement
(
#ifdef QCUSB_SHARE_INTERRUPT_OLD   
    UCHAR DeviceId[DEVICE_ID_LEN]
#else
    PDEVICE_OBJECT DeviceId
#endif
);

PKEVENT USBSHR_GetReadControlEvent
(
    PDEVICE_OBJECT DeviceObject,
    USHORT         Interface
);
PLONG USBSHR_GetRspAvailableCount
(
    PDEVICE_OBJECT DeviceObject,
    USHORT         Interface
);
VOID USBSHR_FreeReadControlElement
(
    PDeviceReadControlEvent pElement
);

PDeviceReadControlEvent USBSHR_GetReadCtlEventElement
(
#ifdef QCUSB_SHARE_INTERRUPT_OLD   
    UCHAR  DeviceId[DEVICE_ID_LEN]
#else
    PDEVICE_OBJECT DeviceId
#endif
);

VOID USBSHR_ResetRspAvailableCount
(
    PDEVICE_OBJECT DeviceObject,
    USHORT Interface
);
#endif // USBSHR_H
