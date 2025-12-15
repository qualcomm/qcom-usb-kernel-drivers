/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P I N I . H

GENERAL DESCRIPTION
    This module contains definitions for miniport initialization

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef MPINI_H
#define MPINI_H

#include "MPMain.h"

NDIS_STATUS MPINI_MiniportInitialize
(
   PNDIS_STATUS OpenErrorStatus,
   PUINT        SelectedMediumIndex,
   PNDIS_MEDIUM MediumArray,
   UINT         MediumArraySize,
   NDIS_HANDLE  MiniportAdapterHandle,
   NDIS_HANDLE  WrapperConfigurationContext
);

NDIS_STATUS MPINI_ReadNetworkAddress
(
   PMP_ADAPTER Adapter,
   NDIS_HANDLE WrapperConfigurationContext
);

VOID MPINI_Attach(PMP_ADAPTER Adapter);

VOID MPINI_Detach(PMP_ADAPTER Adapter);

VOID MPINI_FreeAdapter(PMP_ADAPTER Adapter);

NDIS_STATUS MPINI_AllocAdapter(PMP_ADAPTER *Adapter);

NDIS_STATUS MPINI_AllocAdapterBuffers(PMP_ADAPTER Adapter);

NDIS_STATUS MPINI_InitializeAdapter
(
   PMP_ADAPTER Adapter,
   NDIS_HANDLE WrapperConfigurationContext
);

#ifdef NDIS60_MINIPORT
NDIS_STATUS MPINI_MiniportInitializeEx
(
   NDIS_HANDLE                    MiniportAdapterHandle,
   NDIS_HANDLE                    MiniportDriverContext,
   PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters
);

NDIS_STATUS MPINI_SetNdisAttributes
(
   NDIS_HANDLE MiniportAdapterHandle,
   PMP_ADAPTER pAdapter
);

NDIS_STATUS MPINI_AllocateReceiveNBL(PMP_ADAPTER pAdapter);

VOID MPINI_FreeReceiveNBL(PMP_ADAPTER pAdapter);

#endif

#endif // MPINI_H
