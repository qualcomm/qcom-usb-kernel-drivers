/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B I N T . H

GENERAL DESCRIPTION
    This file contains definitions for USB interrupt operations.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBINT_H
#define USBINT_H

#include "USBMAIN.h"

#define CDC_NOTIFICATION_NETWORK_CONNECTION 0x00
#define CDC_NOTIFICATION_RESPONSE_AVAILABLE 0x01
#define CDC_NOTIFICATION_SERIAL_STATE       0x20
#define CDC_NOTIFICATION_CONNECTION_SPD_CHG 0x2A

#pragma pack(push, 1)

typedef struct _USB_NOTIFICATION_STATUS
{
    UCHAR   bmRequestType;
    UCHAR   bNotification;
    USHORT  wValue;
    USHORT  wIndex;  // interface #
    USHORT  wLength; // number of data bytes
    USHORT  usValue; // serial status, etc.
} USB_NOTIFICATION_STATUS, *PUSB_NOTIFICATION_STATUS;

typedef struct _USB_NOTIFICATION_CONNECTION_SPEED
{
    ULONG ulUSBitRate;
    ULONG ulDSBitRate;
} USB_NOTIFICATION_CONNECTION_SPEED, *PUSB_NOTIFICATION_CONNECTION_SPEED;

#pragma pack(pop)

NTSTATUS USBINT_InitInterruptPipe(IN PDEVICE_OBJECT pDevObj);
VOID ReadInterruptPipe(IN PVOID pContext);
NTSTATUS InterruptPipeCompletion
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           pIrp,
    IN PVOID          pContext
);
NTSTATUS USBINT_CancelInterruptThread(PDEVICE_EXTENSION pDevExt, UCHAR cookie);
NTSTATUS USBINT_StopInterruptService
(
    PDEVICE_EXTENSION pDevExt,
    BOOLEAN           bWait,
    BOOLEAN           CancelWaitWake,
    UCHAR             cookie
);
NTSTATUS USBINT_ResumeInterruptService(PDEVICE_EXTENSION pDevExt, UCHAR cookie);

VOID USBINT_HandleSerialStateNotification(PDEVICE_EXTENSION pDevExt);
VOID USBINT_HandleNetworkConnectionNotification(PDEVICE_EXTENSION pDevExt);
VOID USBINT_HandleResponseAvailableNotification(PDEVICE_EXTENSION pDevExt);
VOID USBINT_HandleConnectionSpeedChangeNotification(PDEVICE_EXTENSION pDevExt);

#endif // USBINT_H
