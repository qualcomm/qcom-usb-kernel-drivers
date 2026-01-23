/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P R X . H

GENERAL DESCRIPTION
    This module contains definitions for miniport data RX

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef MPRX_H
#define MPRX_H

#include "MPMain.h"

VOID MPRX_MiniportReturnPacket
(
    NDIS_HANDLE  MiniportAdapterContext,
    PNDIS_PACKET Packet
);

#ifdef NDIS60_MINIPORT

VOID MPRX_MiniportReturnNetBufferLists
(
    IN NDIS_HANDLE       MiniportAdapterContext,
    IN PNET_BUFFER_LIST  NetBufferLists,
    IN ULONG             ReturnFlags
);

NTSTATUS MPRX_StartRxThread(PMP_ADAPTER pAdapter, INT Index);

NTSTATUS MPRX_CancelRxThread(PMP_ADAPTER pAdapter, INT Index);

VOID MPRX_RxThread(PVOID Context);

VOID MPRX_RxChainCleanup(PMP_ADAPTER pAdapter, INT Index);

VOID MPRX_ProcessRxChain(PMP_ADAPTER pAdapter, INT Index);

#endif // NDIS60_MINIPORT

#endif // MPRX_H
