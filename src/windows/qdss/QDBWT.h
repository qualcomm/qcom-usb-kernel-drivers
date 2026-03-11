/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B W T . H

GENERAL DESCRIPTION
    Function declarations for the QDSS USB write module. Covers
    application write dispatch, USB URB submission, and write
    completion handling for the DEBUG channel.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef QDBWT_H
#define QDBWT_H

EVT_WDF_IO_QUEUE_IO_WRITE QDBWT_IoWrite;

VOID QDBWT_WriteUSB
(
    IN WDFQUEUE         Queue,
    IN WDFREQUEST       Request,
    IN ULONG            Length
);

EVT_WDF_REQUEST_COMPLETION_ROUTINE QDBWT_WriteUSBCompletion;

#endif // QDBWT_H
