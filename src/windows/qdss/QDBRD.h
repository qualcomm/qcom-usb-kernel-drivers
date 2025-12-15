/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B R D . C

GENERAL DESCRIPTION
  This is the file which contains RX function definitions for QDSS driver.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef QDBRD_H
#define QDBRD_H

VOID QDBRD_IoRead
(
   WDFQUEUE   Queue,
   WDFREQUEST Request,
   size_t     Length
);

VOID QDBRD_ReadUSB
(
   IN WDFQUEUE         Queue,
   IN WDFREQUEST       Request,
   IN ULONG            Length
);

VOID QDBRD_ReadUSBCompletion
(
   WDFREQUEST                  Request,
   WDFIOTARGET                 Target,
   PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
   WDFCONTEXT                  Context
);

NTSTATUS QDBRD_AllocateRequestsRx(PDEVICE_CONTEXT pDevContext);

VOID QDBRD_FreeIoBuffer(PDEVICE_CONTEXT pDevContext);

VOID QDBRD_SendIOBlock(PDEVICE_CONTEXT pDevContext, INT Index);

NTSTATUS QDBRD_PipeDrainStart(PDEVICE_CONTEXT pDevContext);

VOID QDBRD_SendIOBlock(PDEVICE_CONTEXT pDevContext, INT Index);

NTSTATUS QDBRD_PipeDrainStop(PDEVICE_CONTEXT pDevContext);

VOID QDBRD_PipeDrainCompletion
(
   WDFREQUEST                  Request,
   WDFIOTARGET                 Target,
   PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
   WDFCONTEXT                  Context
);

VOID QDBRD_ProcessDrainedDPLBlock(PDEVICE_CONTEXT pDevContext, PVOID Buffer, ULONG Length);

PVOID QDBRD_RetrievePacket(PVOID* DataPtr, PULONG DataLength, PULONG PacketLength);

#endif // QDBRD_H
