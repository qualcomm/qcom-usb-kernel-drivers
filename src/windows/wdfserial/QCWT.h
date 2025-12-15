/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef QCWT_H
#define QCWT_H

#include "QCMAIN.h"

KSTART_ROUTINE                     QCWT_WriteRequestHandlerThread;
EVT_WDF_IO_QUEUE_IO_WRITE          QCWT_EvtIoWrite;
EVT_WDF_REQUEST_COMPLETION_ROUTINE QCWT_EvtIoWriteCompletion;

VOID QCWT_CleanupWriteQueue
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCWT_SendUsbShortPacket
(
    PDEVICE_CONTEXT pDevContext
);

#endif