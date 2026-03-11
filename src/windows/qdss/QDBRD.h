/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B R D . H

GENERAL DESCRIPTION
    Function declarations for the QDSS USB pipe read module.
    Covers application read dispatch, USB URB submission, completion
    handling, and autonomous DPL pipe drain lifecycle management.

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

NTSTATUS QDBRD_AllocateRequestsRx(PDEVICE_CONTEXT pDevContext);

VOID QDBRD_FreeIoBuffer(PDEVICE_CONTEXT pDevContext);

VOID QDBRD_SendIOBlock(PDEVICE_CONTEXT pDevContext, INT Index);

NTSTATUS QDBRD_PipeDrainStart(PDEVICE_CONTEXT pDevContext);

VOID QDBRD_SendIOBlock(PDEVICE_CONTEXT pDevContext, INT Index);

NTSTATUS QDBRD_PipeDrainStop(PDEVICE_CONTEXT pDevContext);

EVT_WDF_REQUEST_COMPLETION_ROUTINE QDBRD_PipeDrainCompletion;

VOID QDBRD_ProcessDrainedDPLBlock(PDEVICE_CONTEXT pDevContext, PVOID Buffer, ULONG Length);

PVOID QDBRD_RetrievePacket(PVOID *DataPtr, PULONG DataLength, PULONG PacketLength);

#endif // QDBRD_H
