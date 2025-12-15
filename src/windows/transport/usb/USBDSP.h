/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B D S P . H

GENERAL DESCRIPTION
    This file contains definitions for dispatch routines.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBDSP_H
#define USBDSP_H

#include "USBMAIN.h"

VOID DispatchThread(PVOID pContext);
VOID DispatchCancelQueued(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS USBDSP_DirectDispatch(IN PDEVICE_OBJECT CalledDO, IN PIRP Irp);
NTSTATUS USBDSP_QueuedDispatch(IN PDEVICE_OBJECT CalledDO, IN PIRP Irp);
NTSTATUS USBDSP_Enqueue(PDEVICE_OBJECT DeviceObject, PIRP Irp, KIRQL Irql);
NTSTATUS USBDSP_InitDispatchThread(IN PDEVICE_OBJECT pDevObj);
NTSTATUS USBDSP_CancelDispatchThread(PDEVICE_EXTENSION pDevExt, UCHAR cookie);
NTSTATUS USBDSP_Dispatch
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PDEVICE_OBJECT FDO,
   IN PIRP Irp,
   IN OUT BOOLEAN *Removed
);
NTSTATUS USBDSP_CleanUp
(
   IN PDEVICE_OBJECT DeviceObject,
   PIRP              pIrp,
   BOOLEAN           bIsCleanupIrp
);
BOOLEAN QCDSP_ToProcessIrp
(
   PDEVICE_EXTENSION pDevExt,
   PIRP              Irp
);
void USBDSP_GetMUXInterface(PDEVICE_EXTENSION  pDevExt, UCHAR InterfaceNumber);

#endif // USBDSP_H
