/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B E N C . H

GENERAL DESCRIPTION
    This file contains definitions for handling USB CDC encapsulated commands.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBENC_H
#define USBENC_H

#include "USBMAIN.h"

#define USBENC_MAX_ENCAPSULATED_DATA_LENGTH 4096

NTSTATUS USBENC_Enqueue
(
   PDEVICE_OBJECT DeviceObject,
   PIRP Irp,
   KIRQL Irql
);

NTSTATUS USBENC_CDC_SendEncapsulatedCommand
(
   PDEVICE_OBJECT DeviceObject,
   USHORT Interface,
   PIRP   pIrp
);

NTSTATUS USBENC_CDC_GetEncapsulatedResponse
(
   PDEVICE_OBJECT DeviceObject,
   USHORT         Interface
);

VOID USBENC_PurgeQueue
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY    queue,
   BOOLEAN        ctlItemOnly,
   UCHAR          cookie
);

VOID USBENC_CancelQueued(PDEVICE_OBJECT CalledDO, PIRP Irp);

#endif // USBENC_H
