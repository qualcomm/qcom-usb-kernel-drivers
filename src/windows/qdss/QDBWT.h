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

VOID QDBWT_IoWrite
(
    WDFQUEUE   Queue,
    WDFREQUEST Request,
    size_t     Length
);

VOID QDBWT_WriteUSB
(
    IN WDFQUEUE         Queue,
    IN WDFREQUEST       Request,
    IN ULONG            Length
);

VOID QDBWT_WriteUSBCompletion
(
    WDFREQUEST                  Request,
    WDFIOTARGET                 Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    WDFCONTEXT                  Context
);

#endif // QDBWT_H
