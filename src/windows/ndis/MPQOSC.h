/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             M P Q O S C . C

GENERAL DESCRIPTION
    This module contains forward references to the QOS module.
    requests.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef MPQOSC_H
#define MPQOSC_H

#include "MPMain.h"

#pragma pack(push, 1)

typedef struct _MPIOC_DEV_INFO MPIOC_DEV_INFO, *PMPIOC_DEV_INFO;
typedef struct _QMI_FILTER QMI_FILTER, *PQMI_FILTER;

#pragma pack(pop)

NTSTATUS MPQOSC_StartQmiQosClient(PMP_ADAPTER pAdapter);

NTSTATUS MPQOSC_CancelQmiQosClient(PMP_ADAPTER pAdapter);

VOID MPQOSC_QmiQosThread(PVOID Context);

VOID MPQOSC_ProcessInboundMessage
(
   PMPIOC_DEV_INFO IocDevice,
   PVOID           Message,
   ULONG           Length
);

VOID MPQOSC_SendQosSetEventReportReq
(
   PMP_ADAPTER     pAdapter,
   PMPIOC_DEV_INFO pIocDev
);

VOID ProcessSetQosEventReportRsp(PMPIOC_DEV_INFO IocDevice, PQMUX_MSG Message);

VOID ProcessQosEventReportInd(PMPIOC_DEV_INFO IocDevice, PQMUX_MSG Message);

VOID ProcessQosFlowReport
(
   PMPIOC_DEV_INFO IocDevice,
   PCHAR           FlowReport,
   USHORT          ReportLength
);

VOID MPQOSC_PurgeFilterList(PMP_ADAPTER pAdapter, PLIST_ENTRY FilterList);

PCHAR ParseQmiQosFilter
(
   PMPIOC_DEV_INFO IocDevice,
   PQMI_FILTER     QmiFilter,
   PVOID           TLVs,
   USHORT          TLVLength,
   BOOLEAN         IsTx
);

PMPQOS_FLOW_ENTRY FindQosFlow
(
   PMP_ADAPTER         pAdapter,
   QOS_FLOW_QUEUE_TYPE StartQueueType,
   QOS_FLOW_QUEUE_TYPE EndQueueType,
   ULONG               FlowId
);

VOID AddQosFilter
(
   PMP_ADAPTER       pAdapter,
   PMPQOS_FLOW_ENTRY QosFlow,
   PLIST_ENTRY       QmiFilterList
);

VOID AddQosFilterToPrecedenceList
(
   PMP_ADAPTER pAdapter,
   PLIST_ENTRY QmiFilterList
);

VOID RemoveQosFilterFromPrecedenceList
(
   PMP_ADAPTER pAdapter,
   PLIST_ENTRY QmiFilterList
);

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QMI_OVER_DATA
VOID MPQOSC_SendQosEnableFlowDisableAckReq
(
   PMP_ADAPTER     pAdapter,
   PMPIOC_DEV_INFO pIocDev,
   BOOLEAN         Ack
);
#endif
#endif

VOID MPQOSC_ComposeQosSetClientIpPrefReq
(
   PMP_ADAPTER     pAdapter,
   PMPIOC_DEV_INFO pIocDev,
   UCHAR           IPVersion
);

VOID MPQOS_ProcessQosSetClientIpPrefResp
(
   PMP_ADAPTER     pAdapter,
   PQMUX_MSG       Message,
   PMPIOC_DEV_INFO pIocDev,
   USHORT          Tid
);

#endif // MPQOSC_H
