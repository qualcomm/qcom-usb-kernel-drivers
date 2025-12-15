/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                            U S B R D . H

GENERAL DESCRIPTION
    This file contains definitions for reading data over USB.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBRD_H
#define USBRD_H

#include "USBMAIN.h"

NTSTATUS USBRD_Read(IN PDEVICE_OBJECT DeviceObject, IN PIRP pIrp);
NTSTATUS USBRD_CancelReadThread (PDEVICE_EXTENSION pDevExt, UCHAR Cookie);
NTSTATUS USBRD_Enqueue(PDEVICE_EXTENSION pDevExt, PIRP pIrp, UCHAR Cookie);
NTSTATUS ReadCompletionRoutine
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
);
void USBRD_ReadThread(PVOID pContext);
void QCUSB_ReadThread(PDEVICE_EXTENSION pDevExt);
VOID CancelReadRoutine(PDEVICE_OBJECT DeviceObject, PIRP pIrp);
NTSTATUS USBRD_ReadIrpCompletion(PDEVICE_EXTENSION pDevExt, UCHAR Cookie);
ULONG CountL1ReadQueue(PDEVICE_EXTENSION pDevExt);
ULONG CountReadQueue(PDEVICE_EXTENSION pDevExt);
NTSTATUS USBRD_InitializeL2Buffers(PDEVICE_EXTENSION pDevExt);
VOID USBRD_FillReadRequest(PDEVICE_EXTENSION pDevExt);
VOID USBRD_SetStopState(PDEVICE_EXTENSION pDevExt, BOOLEAN StopState, UCHAR Cookie);
BOOLEAN USBRD_IsL1Terminated(PDEVICE_EXTENSION pDevExt, UCHAR Cookie);
NTSTATUS USBRD_L2Suspend(PDEVICE_EXTENSION pDevExt);

// for multi-reads
VOID USBMRD_L1MultiReadThread(PVOID pContext);
NTSTATUS MultiReadCompletionRoutine
(
   PDEVICE_OBJECT DO,
   PIRP           Irp,
   PVOID          Context
);
VOID USBMRD_L2MultiReadThread(PDEVICE_EXTENSION pDevExt);
VOID USBMRD_ResetL2Buffers(PDEVICE_EXTENSION pDevExt);
PUSBMRD_L2BUFFER USBMRD_L2NextActive(PDEVICE_EXTENSION pDevExt);
int USBMRD_L2ActiveBuffers(PDEVICE_EXTENSION pDevExt);
VOID USBMRD_FillReadRequest(PDEVICE_EXTENSION pDevExt, UCHAR Cookie);
VOID USBMRD_RecycleL2FillBuffer(PDEVICE_EXTENSION pDevExt, LONG FillIdx, UCHAR Cookie);
VOID USBMRD_RecycleFrameBuffer(PDEVICE_EXTENSION pDevExt);
NTSTATUS USBMRD_ReadIrpCompletion(PDEVICE_EXTENSION pDevExt, UCHAR Cookie, KIRQL *levelOrHandle);
BOOLEAN USBRD_InPipeOk(PDEVICE_EXTENSION pDevExt);
NTSTATUS QCMRD_FillIrpData
(
   PDEVICE_EXTENSION pDevExt,
   PCHAR             IrpBuffer,
   ULONG             BufferSize,
   PCHAR             DataBuffer,
   ULONG             DataLength
);

#ifdef QC_IP_MODE
NTSTATUS USBRD_LoopbackPacket
(
   PDEVICE_OBJECT DeviceObject,
   PVOID          DataPacket,
   ULONG          Length
);
#endif // QC_IP_MODE

#endif // USBRD_H
