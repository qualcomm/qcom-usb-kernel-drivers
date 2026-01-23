/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                            M P I P. H

GENERAL DESCRIPTION
    This module contains references to the IP(V6) module.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef MPIP_H
#define MPIP_H

#include "MPMain.h"

#pragma pack(push, 1)

typedef struct _MPIOC_DEV_INFO MPIOC_DEV_INFO, *PMPIOC_DEV_INFO;

#pragma pack(pop)

NTSTATUS MPIP_StartWdsIpClient(PMP_ADAPTER pAdapter);

NTSTATUS MPIP_CancelWdsIpClient(PMP_ADAPTER pAdapter);

VOID MPIP_WdsIpThread(PVOID Context);

VOID MPIP_ProcessInboundMessage
(
    PMPIOC_DEV_INFO IocDevice,
    PVOID           Message,
    ULONG           Length,
    PMP_OID_WRITE   pMatchOID
);

VOID ProcessWdsPktSrvcStatusInd(PMPIOC_DEV_INFO IocDevice, PQMUX_MSG Message);

VOID MPIP_CancelReconfigTimer(PMP_ADAPTER pAdapter);

#endif // MPIP_H
