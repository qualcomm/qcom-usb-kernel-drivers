/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef __QMAP_H
#define __QMAP_H

#include "Structs.h"
#include "QMI.h"

#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_IPV6 0x86DD

typedef struct _QTI_ETH_HDR
{
    unsigned char DstMacAddress[ETH_ALEN];
    unsigned char SrcMacAddress[ETH_ALEN];
    unsigned short EtherType;
}__attribute__ ((aligned (1))) QTI_ETH_HDR, *PQTI_ETH_HDR;

typedef struct _QTI_ARP_HDR
{
   unsigned short HardwareType;
   unsigned short ProtocolType;
   unsigned char HLEN;        // length of HA  (6)
   unsigned char PLEN;        // length of IP  (4)
   unsigned short Operation;
   unsigned char SenderHA[ETH_ALEN];  // 6
   unsigned long SenderIP;
   unsigned char TargetHA[ETH_ALEN];  // 6
   unsigned long TargetIP;
}__attribute__ ((aligned (1))) QTI_ARP_HDR, *PQTI_ARP_HDR;

typedef struct qmap_hdr
{
#if 0
#ifndef LITTLE_ENDIAN
        unsigned char pad_len:6;
        unsigned char reserved_bit:1;
        unsigned char cd_bit:1;
#else
        unsigned char cd_bit:1;
        unsigned char reserved_bit:1;
        unsigned char pad_len:6;
#endif
#endif
   unsigned char cd_rsvd_pad;
   unsigned char mux_id;
   unsigned short pkt_len;
}  __attribute__ ((aligned (1))) qmap_t, __attribute__ ((aligned (1)))*qmap_p;

typedef struct qmap_ipv4_header
{
#ifdef LITTLE_ENDIAN
   unsigned char ihl:4;
   unsigned char version:4;
   unsigned char ecn:2;
   unsigned char dscp:6;
#else
   unsigned char version:4;
   unsigned char ihl:4;
   unsigned char dscp:6;
   unsigned char ecn:2;
#endif
   unsigned short total_length;
   unsigned short identification;
   unsigned short fragment_offset;
   unsigned char ttl;
   unsigned char protocol;
   unsigned short header_checksum;
   unsigned int src_address;
   unsigned int dst_address;
} __attribute__ ((aligned (1))) *qmap_ipv4_header_t;

typedef struct qmap_ipv6_header
{
   unsigned int version:4;
   unsigned int traffic_class:8;
   unsigned int flow_label:20;
   unsigned short length;
   unsigned char next_header;
   unsigned char hop_limit;
   unsigned char src_address[16];
   unsigned char dst_address[16];
} __attribute__ ((aligned (1))) *qmap_ipv6_header_t;

void qmap_mux(struct sk_buff *skb, sGobiUSBNet *pGobiNet, int data_offset);

void qmap_demux(struct usbnet *dev, struct sk_buff *skb);

unsigned short qmap_ip_ethertype(struct sk_buff *skb);

enum qmap_mux_errors
{
        QMAP_MUX_SUCCESS,
        QMAP_MUX_INVALID_MUX_ID,
        QMAP_MUX_INVALID_PAD_LENGTH,
        QMAP_MUX_INVALID_PKT_LENGTH,
        QMAP_MUX_ENUM_LENGTH /* This should always be the last element */

};

#endif /* __QMAP_H */

