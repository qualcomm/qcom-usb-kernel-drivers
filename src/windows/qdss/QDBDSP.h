/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B D S P . H

GENERAL DESCRIPTION
    Function declarations for the QDSS I/O dispatch and completion module.
    Covers IOCTL handling, queue callbacks, and IRP completion routines.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef QDBDSP_H
#define QDBDSP_H

VOID QDBDSP_IoDeviceControl
(
    WDFQUEUE   Queue,
    WDFREQUEST Request,
    size_t     OutputBufferLength,
    size_t     InputBufferLength,
    ULONG      IoControlCode
);

VOID QDBDSP_IoStop
(
    WDFQUEUE   Queue,
    WDFREQUEST Request,
    ULONG      ActionFlags
);

VOID QDBDSP_IoResume
(
    WDFQUEUE   Queue,
    WDFREQUEST Request
);

NTSTATUS QDBDSP_IrpIoCompletion
(
    PDEVICE_OBJECT DeviceObject,
    PIRP           Irp,
    PVOID          Context
);

NTSTATUS QDBDSP_GetParentId
(
    PDEVICE_CONTEXT pDevContext,
    PVOID           IoBuffer,
    ULONG           BufferLen
);

#endif // QDBDSP_H
