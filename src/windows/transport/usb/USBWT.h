/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B W T . H

GENERAL DESCRIPTION
    This file contains definitions for USB write operations.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBWT_H
#define USBWT_H

#include "USBMAIN.h"

#ifdef QCNDIS_ERROR_TEST
typedef struct _QCQOS_HDR
{
    UCHAR Version;
    UCHAR Reserved;
    ULONG FlowId;
} QCQOS_HDR, *PQCQOS_HDR;
#endif  // QCNDIS_ERROR_TEST

NTSTATUS USBWT_Write(IN PDEVICE_OBJECT CalledDO, IN PIRP pIrp);
VOID     CancelWriteRoutine(PDEVICE_OBJECT DeviceObject, PIRP pIrp);

NTSTATUS USBWT_WriteIrpCompletion
(
    PDEVICE_EXTENSION pDevExt,
    ULONG             ulRequestedBytes,
    ULONG             ulActiveBytes,
    NTSTATUS          ntStatus,
    UCHAR             cookie
);

NTSTATUS USBWT_CancelWriteThread(PDEVICE_EXTENSION pDevExt, UCHAR cookie);
NTSTATUS USBWT_Enqueue(PDEVICE_EXTENSION pDevExt, PIRP pIrp, UCHAR cookie);
NTSTATUS WriteCompletionRoutine
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
);
VOID USBWT_WriteThread(PVOID pContext);
VOID USBWT_SetStopState(PDEVICE_EXTENSION pDevExt, BOOLEAN StopState);
NTSTATUS USBWT_Suspend(PDEVICE_EXTENSION pDevExt);
NTSTATUS USBWT_Resume(PDEVICE_EXTENSION pDevExt);
BOOLEAN USBWT_OutPipeOk(PDEVICE_EXTENSION pDevExt);

#ifdef QCUSB_MULTI_WRITES

typedef struct _QCUSB_MWT_SENT_IRP
{
    PIRP          SentIrp;
    ULONG         TotalLength; // for debugging
    PQCMWT_BUFFER MWTBuf;
    BOOLEAN       IrpReturned;
    LIST_ENTRY    List;
} QCUSB_MWT_SENT_IRP, *PQCUSB_MWT_SENT_IRP;

NTSTATUS USBMWT_WriteCompletionRoutine
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
);
void USBMWT_WriteThread(PVOID pContext);
NTSTATUS USBMWT_InitializeMultiWriteElements(PDEVICE_EXTENSION pDevExt);
NTSTATUS USBMWT_WriteIrpCompletion
(
    PDEVICE_EXTENSION DeviceExtension,
    PQCMWT_BUFFER     WriteItem,
    ULONG             TransferredBytes,
    NTSTATUS          Status,
    UCHAR             Cookie
);
VOID USBMWT_CancelWriteRoutine(PDEVICE_OBJECT DeviceObject, PIRP pIrp);
PQCMWT_BUFFER USBMWT_IsIrpPending
(
    PDEVICE_EXTENSION pDevExt,
    PIRP              Irp
);

BOOLEAN USBMWT_MarkAndCheckReturnedIrp
(
    PDEVICE_EXTENSION pDevExt,
    PQCMWT_BUFFER     MWTBuf,
    BOOLEAN           RemoveIrp,
    MWT_STATE         OperationState,
    NTSTATUS          Status,
    PURB              Urb
);

#ifdef QCUSB_MUX_PROTOCOL
// BURST_SUPPORT
#define BURST_SEND       0
#define BURST_COMPLETION 1
VOID USBMWT_InitializeBurstSettings(PDEVICE_EXTENSION pDevExt);

VOID USBMWT_BurstCheck(PDEVICE_EXTENSION pDevExt, ULONG TransferSize, INT SendOrCompletion);

VOID USBMWT_BurstDpcRoutine
(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2
);

VOID USBMWT_CancelBurstTimer(PDEVICE_EXTENSION pDevExt);

#endif // QCUSB_MUX_PROTOCOL

#endif // QCUSB_MULTI_WRITES

#endif // USBWT_H
