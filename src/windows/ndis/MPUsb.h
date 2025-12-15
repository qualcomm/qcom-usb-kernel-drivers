/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             M P U S B . H

GENERAL DESCRIPTION
    This module contains forward references to the MPusb module.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef _MPUSB_H
#define _MPUSB_H

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QMI_OVER_DATA

#define QMI_PREAMBLE_LENGTH 14

#pragma pack(push, 1)

typedef struct _QMI_PREAMBLE
{
   ULONG Reserved0;
   ULONG Reserved1;
   ULONG Reserved2;
   ULONG Reserved3;
} QMI_PREAMBLE, *PQMI_PREAMBLE;

#pragma pack(pop)

BOOLEAN MPUSB_DispatchQMI
(
   PMP_ADAPTER pAdapter,
   PVOID       DataBuffer,
   ULONG       DataLength,
   NTSTATUS    Status
);

#endif // QMI_OVER_DATA
#endif // QCUSB_MUX_PROTOCOL

#ifdef QCMP_SUPPORT_CTRL_QMIC
#define QCMP_QCTL_INTERNAL_MAX_TID 0x7F
#endif // QCMP_SUPPORT_CTRL_QMIC

void MP_USBTxPacket(PMP_ADAPTER, ULONG, BOOLEAN, PLIST_ENTRY);
void MP_USBPostPacketRead( PMP_ADAPTER, PLIST_ENTRY );
void MP_USBPostControlRead
(
   PMP_ADAPTER,
#ifndef QCQMI_SUPPORT
   pControlRx
#else
   PQCQMI
#endif
);
NDIS_STATUS MP_USBRequest( PMP_ADAPTER, PVOID, ULONG, PLIST_ENTRY );
void MP_USBCleanUp( PMP_ADAPTER );

NDIS_STATUS MP_USBSendControl
(
   PMP_ADAPTER pAdapter,
   PVOID       Buffer,
   ULONG       BufferLength
);

NDIS_STATUS MP_USBSendControlRequest
(
   PMP_ADAPTER pAdapter,
   PVOID       Buffer,
   ULONG       BufferLength
);

NTSTATUS MP_SendControlCompletion
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
);

NDIS_STATUS MP_USBSendCustomCommand
(
   PMP_ADAPTER pAdapter,
   ULONG       IoControlCode,
   PVOID       Buffer,
   ULONG       BufferLength,
   ULONG       *pReturnBufferLength
);

NTSTATUS MP_SendCustomCommandCompletion
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
);

ULONG MPUSB_CopyNdisPacketToBuffer
(
   IN PNDIS_PACKET pPacket,
   IN PUCHAR       pBuffer,
   IN ULONG        dwLeadingBytesToSkip
);

#ifdef QC_IP_MODE
BOOLEAN MPUSB_PreparePacket
(
   PMP_ADAPTER pAdapter,
   PCHAR       EthPkt,
   PULONG      Length  // include QOS hdr when QOS enabled
);

VOID MPUSB_ProcessARP
(
   PMP_ADAPTER pAdapter,  // MP adapter
   PCHAR       EthPkt,    // ETH pkt containing ARP
   ULONG       Length     // length of the ETH pkt
);

VOID MPUSB_ArpResponse
(
   PMP_ADAPTER pAdapter,  // MP adapter
   PCHAR       EthPkt,    // ETH pkt containing ARP
   ULONG       Length     // length of the ETH pkt
);
#endif // QC_IP_MODE

#ifdef NDIS60_MINIPORT

VOID MP_USBPostPacketReadEx(PMP_ADAPTER pAdapter, PLIST_ENTRY pList);

NTSTATUS MP_RxIRPCompletionEx(PDEVICE_OBJECT pDO, PIRP pIrp, PVOID pContext);

PMPUSB_RX_NBL MPUSB_FindRxRequest(PMP_ADAPTER pAdapter, PIRP Irp);

VOID MP_USBTxPacketEx
(
   PMP_ADAPTER  pAdapter,
   PLIST_ENTRY  pList,
   ULONG        QosFlowId,
   BOOLEAN      IsQosPacket
);

VOID MPUSB_TryToCompleteNBL
(
   PMP_ADAPTER           pAdapter,
   PMPUSB_TX_CONTEXT_NBL TxNbl,
   NDIS_STATUS           NdisStatus,
   BOOLEAN               ToAcquireSpinLock,
   BOOLEAN               ToUseBusyList
);

PUCHAR MPUSB_RetriveDataPacket
(
   PMP_ADAPTER pAdapter,
   PNET_BUFFER NetBuffer,
   PULONG      Length,
   PVOID       PktBuffer
);

VOID MPUSB_CompleteNetBufferList
(
   PMP_ADAPTER      pAdapter,
   PNET_BUFFER_LIST NetBufferList,
   NDIS_STATUS      NdisStatus,
   ULONG            SendCompleteFlags,
   PVOID            NBLContext
);

NTSTATUS MP_TxIRPCompleteEx
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext,
   BOOLEAN        acquire
);

VOID MPUSB_SetIPType
(
   PUCHAR Packet,
   ULONG  Length,
   PVOID  NetBufferList
);

INT MPUSB_GetRxStreamIndex(PMP_ADAPTER pAdapter, PUCHAR Packet, ULONG Length);

#endif  // NDIS60_MINIPORT

#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT)

NDIS_STATUS MPUSB_InitializeTLP(PMP_ADAPTER pAdapter);

VOID MPUSB_PurgeTLPQueues(PMP_ADAPTER pAdapter, BOOLEAN FreeMemory);

INT MPUSB_AggregationAvailable(PMP_ADAPTER pAdapter, BOOLEAN UseSpinLock, ULONG SizeofPacket);

NTSTATUS MPUSB_TLPTxIRPComplete
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
);

VOID MPUSB_TLPTxPacket
(
   PMP_ADAPTER  pAdapter,
   ULONG        QosFlowId,
   BOOLEAN      IsQosPacket,
   PLIST_ENTRY  pList
);

BOOLEAN MPUSB_TLPProcessPendingTxQueue(PMP_ADAPTER pAdapter);

BOOLEAN MPUSB_TLPIndicateCompleteTx(PMP_ADAPTER pAdapter, PNDIS_PACKET pNdisPacket, UCHAR Cookie);

VOID CancelTransmitTimer( PMP_ADAPTER pAdapter);

#ifdef QCUSB_SHARE_INTERRUPT
NDIS_STATUS MP_RequestDeviceID
(
   PMP_ADAPTER pAdapter
);
#endif

#ifdef NDIS620_MINIPORT

VOID MPUSB_PurgeTLPQueuesEx(PMP_ADAPTER pAdapter, BOOLEAN FreeMemory);

NTSTATUS MPUSB_TLPTxIRPCompleteEx
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
);

VOID MPUSB_TLPTxPacketEx
(
   PMP_ADAPTER  pAdapter, 
   ULONG        QosFlowId,
   BOOLEAN      IsQosPacket,
   PLIST_ENTRY  pList
);

BOOLEAN MPUSB_TLPProcessPendingTxQueueEx(PMP_ADAPTER pAdapter);

#endif

#endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT

PMP_ADAPTER FindStackDeviceObject(PMP_ADAPTER pAdapter);

LONG GetPhysicalAdapterQCTLTransactionId(PMP_ADAPTER pAdapter);

LONG GetQCTLTransactionId(PMP_ADAPTER pAdapter);

LONG GetQMUXTransactionId(PMP_ADAPTER pAdapter);

BOOLEAN DisconnectedAllAdapters(PMP_ADAPTER pAdapter, PMP_ADAPTER* returnAdapter);

ULONG GetNextTxPacketSize(PMP_ADAPTER pAdapter, PLIST_ENTRY listEntry);

VOID IncrementAllQmiSync(PMP_ADAPTER pAdapter);

#endif // _MPUSB_H
