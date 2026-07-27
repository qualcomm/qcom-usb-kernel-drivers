/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B R D . H

GENERAL DESCRIPTION
    Function declarations for the QDSS USB pipe read module.
    Covers application read dispatch, USB URB submission, completion
    handling, and drain management through usb continuous reader.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef QDBRD_H
#define QDBRD_H

EVT_WDF_IO_QUEUE_IO_READ QDBRD_IoRead;

VOID QDBRD_ReadUSB
(
    IN WDFQUEUE         Queue,
    IN WDFREQUEST       Request,
    IN ULONG            Length
);

EVT_WDF_REQUEST_COMPLETION_ROUTINE QDBRD_ReadUSBCompletion;

NTSTATUS QDBRD_ConfigureContinuousReader(PDEVICE_CONTEXT pDevContext);

EVT_WDF_USB_READER_COMPLETION_ROUTINE QDBRD_DrainReadComplete;

EVT_WDF_USB_READERS_FAILED QDBRD_DrainReadFailed;

VOID QDBRD_StartDraining(PDEVICE_CONTEXT pDevContext);

VOID QDBRD_StopDraining(PDEVICE_CONTEXT pDevContext);

#endif // QDBRD_H
