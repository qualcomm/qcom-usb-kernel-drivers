/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef QCUSB_H
#define QCUSB_H

#include "QCMAIN.h"

#pragma pack(push, 1)
typedef struct
{
    UCHAR  bmRequestType;
    UCHAR  bRequest;
    USHORT wValue;
    USHORT wIndex;
    USHORT wLength;
    char   Data[1];
}  USB_DEFAULT_PIPE_REQUEST, *PUSB_DEFAULT_PIPE_REQUEST;
#pragma pack(pop)

#ifdef QCUSB_MUX_PROTOCOL
NTSTATUS QCUSB_SVEFlowEntry
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCUSB_VICommand
(
    PDEVICE_CONTEXT pDevContext,
    PVI_CONFIG      pViConfig,
    USHORT          Command
);

NTSTATUS QCUSB_VIConfig
(
    PDEVICE_CONTEXT pDevContext,
    PVI_CONFIG      pViConfig
);
#endif

#endif // QCUSB_H
