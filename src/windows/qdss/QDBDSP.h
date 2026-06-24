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

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL QDBDSP_IoDeviceControl;
EVT_WDF_IO_QUEUE_IO_STOP QDBDSP_IoStop;
EVT_WDF_IO_QUEUE_IO_RESUME QDBDSP_IoResume;

NTSTATUS QDBDSP_GetDPLStats
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request
);

NTSTATUS QDBDSP_GetParentId
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request
);

#endif // QDBDSP_H
