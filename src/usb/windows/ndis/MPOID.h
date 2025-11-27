/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P O I D . H

GENERAL DESCRIPTION
  This module contains forward references to the OID module.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef MPOID_H
#define MPOID_H

#include "MPMain.h"

#define OID_GOBI_DEVICE_INFO     0xf0000001

#define SIZEOF_OID_RESPONSE 4096

#ifdef NDIS60_MINIPORT
#define INTERRUPT_MODERATION
#endif

PUCHAR MPOID_OidToNameStr(ULONG Oid);

VOID MPOID_GetSupportedOidListSize(VOID);
VOID MPOID_GetSupportedOidGuidsSize(VOID);

NDIS_STATUS MPOID_QueryInformation
(
   NDIS_HANDLE MiniportAdapterContext,
   NDIS_OID Oid,
   PVOID InformationBuffer,
   ULONG InformationBufferLength,
   PULONG BytesWritten,
   PULONG BytesNeeded
);

NDIS_STATUS MPOID_SetInformation
(
   NDIS_HANDLE MiniportAdapterContext,
   NDIS_OID Oid,
   PVOID InformationBuffer,
   ULONG InformationBufferLength,
   PULONG BytesRead,
   PULONG BytesNeeded
);

NDIS_STATUS MPOID_SetPacketFilter(PMP_ADAPTER pAdapter, ULONG PacketFilter);

NDIS_STATUS MPOID_SetMulticastList
(
   PMP_ADAPTER Adapter,
   PVOID       InformationBuffer,
   ULONG       InformationBufferLength,
   PULONG      pBytesRead,
   PULONG      pBytesNeeded
);

VOID MPOID_CompleteOid
(
   PMP_ADAPTER pAdapter,
   NDIS_STATUS OIDStatus,
   PVOID       OidReference,
   ULONG       OidType,
   BOOLEAN     SpinlockHeld
);

VOID MPOID_ResponseIndication
(
   PMP_ADAPTER pAdapter,
   NDIS_STATUS OIDStatus,
   PVOID       OidReference
);

VOID MPOID_CleanupOidCopy
(
   PMP_ADAPTER       pAdapter,
   PMP_OID_WRITE     OidWrite
);

#ifdef NDIS60_MINIPORT

typedef struct _QCMP_OID_REQUEST
{
   LIST_ENTRY        List;
   PNDIS_OID_REQUEST OidRequest;
} QCMP_OID_REQUEST, *PQCMP_OID_REQUEST;

NDIS_STATUS MPOID_MiniportOidRequest
(
   IN NDIS_HANDLE       MiniportAdapterContext,
   IN PNDIS_OID_REQUEST NdisRequest
);

NDIS_STATUS MPMethod
(
   IN NDIS_HANDLE       MiniportAdapterContext,
   IN PNDIS_OID_REQUEST NdisRequest
);

VOID MPOID_CancelAllPendingOids(PMP_ADAPTER pAdapter);

VOID MPOID_MiniportCancelOidRequest
(
   NDIS_HANDLE MiniportAdapterContext,
   PVOID       RequestId
);

NDIS_STATUS MPOID_FindAndCompleteOidRequest
(
   PMP_ADAPTER       pAdapter,
   PVOID             OidRequest,
   PVOID             RequestId,
   NDIS_STATUS       NdisStatus,
   BOOLEAN           SpinlockHeld
);

NDIS_STATUS MPDirectOidRequest
(
   IN NDIS_HANDLE       MiniportAdapterContext,
   IN PNDIS_OID_REQUEST NdisRequest
);

VOID MPCancelDirectOidRequest
(
   NDIS_HANDLE MiniportAdapterContext,
   PVOID       RequestId
);

NDIS_STATUS MPOID_CreateOidCopy
(
   PMP_ADAPTER       pAdapter,
   PNDIS_OID_REQUEST NdisOidRequest,
   PMP_OID_WRITE     OidWrite
);

#endif // NDIS60_MINIPORT

#ifdef NDIS620_MINIPORT

VOID MPOID_IndicationReadyInfo
   (
      PMP_ADAPTER pAdapter,
      WWAN_READY_STATE ReadyState
   );

#endif

VOID MPOID_GetInformation
(
   PVOID *DestBuffer,
   PULONG DestLength,
   PVOID SrcBuffer,
   ULONG SrcLength
);

VOID MPOID_GetInformationUL
(
   PULONG DestBuffer,
   PULONG DestLength,
   ULONG  SrcValue
);

#endif // MPOID_H
