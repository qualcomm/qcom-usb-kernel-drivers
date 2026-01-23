/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             M P Q O S . H

GENERAL DESCRIPTION
    This module contains forward references to the QOS module.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef MPQOS_H
#define MPQOS_H

#include "MPMain.h"

#pragma pack(push, 1)

#ifdef QCQOS_IPV6

typedef struct _REF_IPV6_HDR
{
    UCHAR  Version;       // 4 bits
    UCHAR  TrafficClass;  // 8 bits
    ULONG  FlowLabel;     // 20 bits
    USHORT PayloadLength; // 16 bits
    UCHAR  NextHeader;    // 8 bits
    UCHAR  HopLimit;      // 8 bits
    UCHAR  SrcAddr[16];   // 16 bytes
    UCHAR  DestAddr[16];  // 16 bytes
    USHORT SrcPort;       // not part of IP hdr
    USHORT DestPort;      // not part of IP hdr
    UCHAR  MFlag;
    USHORT FragmentOffset;
    ULONG  Identification;
    UCHAR  Protocol;      // transportation protocol
} REF_IPV6_HDR, *PREF_IPV6_HDR;

typedef struct _IPV6_ADDR_PREFIX_MASK
{
    ULONGLONG High64;
    ULONGLONG Low64;
} IPV6_ADDR_PREFIX_MASK, *PIPV6_ADDR_PREFIX_MASK;

// borrow from AMSS

/*---------------------------------------------------------------------------
  Routing Header type.
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |  Next Header  |  Hdr Ext Len  |  Routing Type | Segments Left |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    :                      type-specific data                       :
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

---------------------------------------------------------------------------*/
typedef struct _ip6_routing_hdr_type
{
    UCHAR   next_hdr;                 /* Next header after the routing header */
    UCHAR   hdrlen;/* Routing hdr len in 8 octet units excluding 1st 8 octets */
    UCHAR   type;                                   /* Type of routing header */
    UCHAR   segments_left;              /* Number of route segments remaining */
    ULONG   reserved;/* Reserved field. Initialized to 0.Ignored on reception */
    /*      Type specific data comes here. Format determined by routing type */
} ip6_routing_hdr_type;

/*---------------------------------------------------------------------------
  Hop By Hop Header type for router-alert option.
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |  Next Header  |  Hdr Ext Len  |                               |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    :                      options                                  :
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


  Router Alert Option :
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |0 0 0|0 0 1 0 1|0 0 0 0 0 0 1 0| Value (2 octets) |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                          length = 2

---------------------------------------------------------------------------*/
typedef struct _ip6_hopbyhop_hdr_type
{
    UCHAR     next_hdr;                /* Next header after hop by hop header */
    UCHAR     hdrlen;
    UCHAR     opt_type_0;                /* Option Type of the options header */
    UCHAR     opt_type_1;                /* Option Type of the options header */
    USHORT    opt_value;            /* Value field for the router alert option*/
    USHORT    pad_rtr_alert;        /* 2 byte padding for the router alert option*/
} ip6_hopbyhop_hdr_type;

# define IP6_HOPBYHOP_HDR_TYPE_0 5
# define IP6_HOPBYHOP_RTR_ALERT_TYPE_1 2

#define IP6_ROUTING_HDR_TYPE_2               2
#define IP6_ROUTING_HDR_TYPE_2_HDR_EXT_LEN   2
#define IP6_ROUTING_HDR_TYPE_2_SEG_LEFT      1

/*---------------------------------------------------------------------------
  Fragment Header type.
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |  Next Header  |  Reserved     |  Fragment Offset        |Res|M|
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                      Identification                           |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

---------------------------------------------------------------------------*/
typedef struct _ip6_frag_hdr_type
{
    UCHAR  next_hdr;           /* Header after the fragment header           */
    UCHAR  reserved;           /* Reserved field. 0 on TX, ignored on RX     */
    USHORT offset_flags;       /* Fragment offset and flags field            */
    ULONG  id;                 /* Fragment identification value              */
} ip6_frag_hdr_type;

#define IP6_FRAG_HDR_OFFSET_FIELD_OFFSET 2

/*---------------------------------------------------------------------------
  Destination Options Header Type
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |  Next Header  |  Hdr Ext Len  |                               |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
    |                                                               |
    .                                                               .
    .                            Options                            .
    .                                                               .
    |                                                               |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
---------------------------------------------------------------------------*/
typedef struct _ip6_dest_opt_hdr_type
{
    UCHAR   next_hdr;                        /* Next header after this header */
    UCHAR   hdrlen;        /* hdr len in 8 octet units excluding 1st 8 octets */
    /* Options come here: in TLV format                                      */
} ip6_dest_opt_hdr_type;

#endif // QCQOS_IPV6

typedef struct _MPQOS_HDR8
{
    UCHAR Version;
    UCHAR Reserved;
    ULONG FlowId;
    UCHAR Rsvd[2];
} MPQOS_HDR8, *PMPQOS_HDR8;

typedef struct _MPQOS_HDR
{
    UCHAR Version;
    UCHAR Reserved;
    ULONG FlowId;
} MPQOS_HDR, *PMPQOS_HDR;

typedef struct _REF_IPV4_HDR
{
    UCHAR  Version;        // 4 bits
    UCHAR  HdrLen;         // header length
    UCHAR  TOS;            // type of service
    USHORT TotalLength;    // 16 bits
    USHORT Identification; // 16 bits
    UCHAR  Flags;          // 3 bits
    USHORT FragmentOffset; // 13 bits
    UCHAR  Protocol;       // 8 bits
    ULONG  SrcAddr;        // 32 bits
    ULONG  DestAddr;       // 32 bits

    // below is not part of IP hdr
    USHORT SrcPort;    // TCP/UDP
    USHORT DestPort;   // TCP/UDP
    struct
    {
        UCHAR Type;
        UCHAR Code;
    } icmp;

} REF_IPV4_HDR, *PREF_IPV4_HDR;

typedef struct _MPQOS_PKT_INFO
{
    LIST_ENTRY List;
    UCHAR      IPVersion; // 4-IPV4; 6-IPV6
    union
    {
        struct
        {
            ULONG  SrcAddr;
            ULONG  DestAddr;
            UCHAR  ULProtocol; // upper layer protocol
            USHORT Identification;
        } V4;
        struct
        {
            union
            {
                UCHAR  Byte[16];
                USHORT Word[8];
            } SrcAddr;
            union
            {
                UCHAR  Byte[16];
                USHORT Word[8];
            } DestAddr;
            ULONG Identification;
            UCHAR ULProtocol; // upper-layer protocol
        } V6;
    } IP;
} MPQOS_PKT_INFO, *PMPQOS_PKT_INFO;

typedef struct _QMI_FILTER
{
    LIST_ENTRY List;
    UCHAR      Index;                   // 0x10
    UCHAR      IpVersion;               // 0x11 - 0:IPv4; 1:IPv6
    ULONG      Ipv4SrcAddr;             // 0x12
    ULONG      Ipv4SrcAddrSubnetMask;
    ULONG      Ipv4DestAddr;            // 0x13
    ULONG      Ipv4DestAddrSubnetMask;
    UCHAR      Ipv4NextHdrProtocol;     // 0x14
    UCHAR      Ipv4TypeOfService;       // 0x15
    UCHAR      Ipv4TypeOfServiceMask;
    BOOLEAN    TcpSrcPortSpecified;     // specified or not
    USHORT     TcpSrcPort;              // 0x1B
    USHORT     TcpSrcPortRange;
    BOOLEAN    TcpDestPortSpecified;    // specified or not
    USHORT     TcpDestPort;             // 0x1C
    USHORT     TcpDestPortRange;
    BOOLEAN    UdpSrcPortSpecified;
    USHORT     UdpSrcPort;              // 0x1D
    USHORT     UdpSrcPortRange;
    BOOLEAN    UdpDestPortSpecified;
    USHORT     UdpDestPort;             // 0x1E
    USHORT     UdpDestPortRange;
    BOOLEAN    IcmpFilterMsgTypeSpecified;
    UCHAR      IcmpFilterMsgType;       // 0x1F
    BOOLEAN    IcmpFilterCodeFieldSpecified;
    UCHAR      IcmpFilterCodeField;     // 0x20
    BOOLEAN    TcpUdpSrcPortSpecified;  // specified or not
    USHORT     TcpUdpSrcPort;           // 0x24
    USHORT     TcpUdpSrcPortRange;
    BOOLEAN    TcpUdpDestPortSpecified; // specified or not
    USHORT     TcpUdpDestPort;          // 0x25
    USHORT     TcpUdpDestPortRange;
    PVOID      Flow;                    // PMPQOS_FLOW_ENTRY
    USHORT     Precedence;
    USHORT     FilterId;
    LIST_ENTRY PrecedenceEntry;

#ifdef QCQOS_IPV6

    UCHAR      Ipv6SrcAddr[16];
    UCHAR      Ipv6SrcAddrPrefixLen;
    UCHAR      Ipv6DestAddr[16];
    UCHAR      Ipv6DestAddrPrefixLen;
    UCHAR      Ipv6NextHdrProtocol;
    UCHAR      Ipv6TrafficClass;
    UCHAR      Ipv6TrafficClassMask;
    ULONG      Ipv6FlowLabel;
    IPV6_ADDR_PREFIX_MASK Ipv6SrcAddrMask;
    IPV6_ADDR_PREFIX_MASK Ipv6DestAddrMask;

#endif // QCQOS_IPV6

    LIST_ENTRY PacketInfoList;  // list of MPQOS_PKT_INFO
} QMI_FILTER, *PQMI_FILTER;

typedef struct _QMI_FLOWSPEC
{
    LIST_ENTRY List;
    ULONG  QosId;                  // not belong to the flow
    BOOLEAN IsTx;
    UCHAR  Index;                  // 0x10
    UCHAR  TrafficClass;           // 0x11
    ULONG  DataRateMax;            // 0x12
    ULONG  GuaranteedRate;
    ULONG  PeakRate;               // 0x13
    ULONG  TokenRate;
    ULONG  BucketSize;
    ULONG  Latency;                // 0x14
    ULONG  Jitter;                 // 0x15
    USHORT PktErrorRateMultiplier; // 0x16
    USHORT PktErrorRateExponent;
    ULONG  MinPolicedPktSize;      // 0x17
    ULONG  MaxAllowedPktSize;      // 0x18
    UCHAR  ResidualBitErrorRate;   // 0x19 - 3GPP
    UCHAR  TrafficPriority;        // 0x1A - 3GPP
    USHORT ProfileId;              // 0x1B - 3GPP2
    BOOLEAN Is3GPP2;                // TRUE: 3GPP; FALSE: 3GPP2
    LIST_ENTRY FilterList;         // filter list
} QMI_FLOWSPEC, *PQMI_FLOWSPEC;

#pragma pack(pop)

NTSTATUS MPQOS_StartQosThread(PMP_ADAPTER pAdapter);

NTSTATUS MPQOS_StartQosDispatchThread(PMP_ADAPTER pAdapter);

NTSTATUS MPQOS_CancelQosThread(PMP_ADAPTER pAdapter);

NTSTATUS MPQOS_CancelQosDispatchThread(PMP_ADAPTER pAdapter);

VOID MPQOS_MainTxThread(PVOID Context);

VOID MPQOS_DispatchThread(PVOID Context);

VOID MPQOS_TransmitPackets(PMP_ADAPTER pAdapter);

BOOLEAN MPQOS_CheckDefaultQueue(PMP_ADAPTER pAdapter, BOOLEAN UseSpinlock);

PLIST_ENTRY MPQOS_DequeuePacket
(
    PMP_ADAPTER pAdapter,
    PLIST_ENTRY PacketQueue,
    PNDIS_STATUS Status
);

VOID MPQOS_CategorizePackets(IN PMP_ADAPTER pAdapter);

VOID MPQOS_CategorizePacketsEx(IN PMP_ADAPTER pAdapter);

VOID MPQOS_Enqueue
(
    PMP_ADAPTER  pAdapter,
    PLIST_ENTRY  Packet,
    PCHAR        Data,
    ULONG        DataLength
);

BOOLEAN  MPQOS_DoesPktMatchThisFilter
(
    PMP_ADAPTER  pAdapter,
    PQMI_FILTER  filter,
    PUCHAR       pkt,
    ULONG        PacketLength
);

BOOLEAN  MPQOS_DoesPktMatchThisFilterEx
(
    PMP_ADAPTER  pAdapter,
    PQMI_FILTER  filter,
    PUCHAR       pkt,
    ULONG        PacketLength
);

VOID MPQOS_GetIPHeaderV4
(
    PMP_ADAPTER   pAdapter,
    PUCHAR        PacketData,
    PREF_IPV4_HDR Header
);

VOID MPQOS_CachePacketInfo
(
    PMP_ADAPTER pAdapter,
    PVOID       IpHdr,
    PQMI_FILTER Filter
);

PMPQOS_PKT_INFO MPQOS_IpIdMatch
(
    PMP_ADAPTER pAdapter,
    PVOID       IpHdr,
    PLIST_ENTRY PktInfoList
);

VOID MPQOS_PurgePktInfoList(PLIST_ENTRY PktInfoList);

VOID MPQOS_PurgeQueues(PMP_ADAPTER  pAdapter);

#ifdef QCQOS_IPV6

VOID QCQOS_GetIPHeaderV6
(
    PMP_ADAPTER   pAdapter,
    PUCHAR        PacketData,
    PREF_IPV6_HDR Header
);

VOID QCQOS_ConstructIPPrefixMask
(
    PMP_ADAPTER   pAdapter,
    UCHAR         PrefixLength,
    PIPV6_ADDR_PREFIX_MASK Mask
);

BOOLEAN QCQOS_AddressesMatchV6
(
    PMP_ADAPTER   pAdapter,
    UCHAR         Addr1[16],
    UCHAR         Addr2[16],
    PIPV6_ADDR_PREFIX_MASK Mask
);

BOOLEAN  MPQOS_DoesPktMatchThisFilterV6
(
    PMP_ADAPTER  pAdapter,
    PQMI_FILTER  QmiFilter,
    PUCHAR       pkt,
    ULONG        PacketLength
);

#endif // QCQOS_IPV6

BOOLEAN MPQOS_EnqueueByPrecedence
(
    PMP_ADAPTER  pAdapter,
    PLIST_ENTRY  Packet,
    PCHAR        Data,
    ULONG        DataLength
);

VOID MPQOS_EnableDefaultFlow(PMP_ADAPTER pAdapter, BOOLEAN Enable);

#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT)

PLIST_ENTRY MPQOS_TLPDequeuePacket
(
    PMP_ADAPTER pAdapter,
    PLIST_ENTRY PacketQueue,
    PNDIS_STATUS Status
);

VOID MPQOS_TLPTransmitPackets(PMP_ADAPTER pAdapter);

#endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT

#endif // MPQOS_H
