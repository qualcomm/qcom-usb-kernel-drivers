/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                            M P W W A N. H

GENERAL DESCRIPTION
    This module contains forward references to the WWAN OID module.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef MPWWAN_H
#define MPWWAN_H

#include "MPMain.h"
#include <netioapi.h>

#ifdef NDIS60_MINIPORT

BOOLEAN MPWWAN_IsWwanOid
(
   IN NDIS_HANDLE  MiniportAdapterContext,
   IN NDIS_OID     Oid
);

#ifdef NDIS620_MINIPORT
#ifdef QC_IP_MODE

NETIO_STATUS MPWWAN_CreateNetLuid(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_SetIPAddress(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_SetDefaultGateway(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_SetDNSAddressV4(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_SetIPAddressV6(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_SetDefaultGatewayV6(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_SetDNSAddressV6(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_SetDNSAddress(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_ClearDNSAddress(PMP_ADAPTER pAdapter, UCHAR IPVer);

VOID MPWWAN_AdvertiseIPSettings(PMP_ADAPTER pAdapter);

VOID MPWWAN_ClearIPSettings(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_ClearDNSAddress(PMP_ADAPTER pAdapter, UCHAR IPVer);

NETIO_STATUS MPWWAN_ClearIPAddress(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_ClearDefaultGateway(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_ClearIPAddressV6(PMP_ADAPTER pAdapter);

NETIO_STATUS MPWWAN_ClearDefaultGatewayV6(PMP_ADAPTER pAdapter);

#endif // QC_IP_MODE
#endif // NDIS620_MINIPORT

#endif // NDIS60_MINIPORT

#endif // MPWWAN_H
