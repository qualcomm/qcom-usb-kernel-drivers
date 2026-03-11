/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B D E V . H

GENERAL DESCRIPTION
    Function declarations for the QDSS file-object callbacks.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef QDBDEV_H
#define QDBDEV_H

#define QDSS_TRACE_FILE L"\\TRACE"
#define QDSS_DEBUG_FILE L"\\DEBUG"
#define QDSS_DPL_FILE   L"\\DPL"

EVT_WDF_DEVICE_FILE_CREATE QDBDEV_EvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE QDBDEV_EvtDeviceFileClose;
EVT_WDF_FILE_CLEANUP QDBDEV_EvtDeviceFileCleanup;

#endif // QDBDEV_H
