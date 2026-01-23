/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef QCRD_H
#define QCRD_H

#include "QCMAIN.h"

KSTART_ROUTINE                     QCRD_ReadRequestHandlerThread;
EVT_WDF_REQUEST_COMPLETION_ROUTINE QCRD_EvtIoReadCompletionAsync;

VOID QCRD_ScanForWaitMask
(
    PDEVICE_CONTEXT pDevContext,
    UCHAR ucEventChar,
    ULONG ulWaitMask
);

NTSTATUS QCRD_CreateReadUrb
(
    PDEVICE_CONTEXT pDevContext,
    size_t          urbBufferSize,
    ULONG           readBufferParamTag,
    WDFREQUEST     *outRequest
);

NTSTATUS QCRD_ResetReadUrb
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST request
);

NTSTATUS QCRD_SendReadUrb
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      request,
    WDFIOTARGET     ioTarget
);

SIZE_T QCRD_SendReadUrbs
(
    PDEVICE_CONTEXT pDevContext,
    WDFIOTARGET     ioTarget
);

VOID QCRD_CleanupReadQueues
(
    PDEVICE_CONTEXT pDevContext
);

VOID QCRD_ClearBuffer
(
    PDEVICE_CONTEXT pDevContext
);

BOOLEAN QCRD_StartReadTimeout
(
    PDEVICE_CONTEXT pDevContext,
    ULONG readLength
);

VOID QCRD_ReadTimeoutDpc
(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
);

#endif
