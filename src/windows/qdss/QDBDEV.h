/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B D E V . H

GENERAL DESCRIPTION
    This is the file which contains definitions for QDSS device I/O.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef QDBDEV_H
#define QDBDEV_H

#define QDSS_TRACE_FILE L"\\TRACE"
#define QDSS_DEBUG_FILE L"\\DEBUG"
#define QDSS_DPL_FILE   L"\\DPL"

VOID QDBDEV_EvtDeviceFileCreate
(
    WDFDEVICE     Device,    // handle to a framework device object
    WDFREQUEST    Request,   // handle to request object representing file creation req
    WDFFILEOBJECT FileObject // handle to a framework file obj describing a file
);

VOID QDBDEV_EvtDeviceFileClose(WDFFILEOBJECT FileObject);

VOID QDBDEV_EvtDeviceFileCleanup(WDFFILEOBJECT FileObject);

#endif // QDBDEV_H
