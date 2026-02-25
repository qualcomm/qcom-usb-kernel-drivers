/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             Q C I N T . H

GENERAL DESCRIPTION
    CDC interrupt pipe management function and worker thread declarations.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef QCINT_H
#define QCINT_H

#include "QCMAIN.h"

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

EVT_WDF_REQUEST_COMPLETION_ROUTINE    QCINT_InterruptPipeCompletion;

#pragma pack(pop)

NTSTATUS StopInterruptService
(
    PDEVICE_CONTEXT pDevContext,
    BOOLEAN           bWait
);

NTSTATUS QCINT_InitInterruptPipe
(
    PDEVICE_CONTEXT pDevContext
);

VOID QCINT_HandleSerialStateNotification
(
    PDEVICE_CONTEXT pDevContext
);

VOID QCINT_ReadInterruptPipe
(
    PVOID pContext
);

#endif // QCINT_H
