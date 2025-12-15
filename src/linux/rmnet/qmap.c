/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include <linux/if_ether.h>
#include "qmap.h"

extern int debug_g;
/************************************************************
 Function: qmap_mux()

 Inserts qmap header at front of packet with the correct
 Mux ID. skb length values and pointers are adjusted to
 compensate
************************************************************/
void qmap_mux(struct sk_buff *skb, sGobiUSBNet *pGobiNet, int data)
{
   qmap_p qhdr;
   u8 padding_count = 0;
   int ippacket_len = 0;
   char *tmp;
#ifndef VIRTUAL_USB_CODE
   int i;
   unsigned char src_address[16];
   unsigned short MuxId = pGobiNet->mQMIDev.MuxId;
#endif

#ifndef VIRTUAL_USB_CODE
   memset(src_address, 0, sizeof(src_address));
   switch (skb->data[0] & 0xf0) 
   {
      case 0x40:
     {
         if (((qmap_ipv4_header_t)(skb->data))->src_address != 0)
         {
            //printk("GobiNet src IP address : %x\n", htonl(pGobiNet->mQMIDev.IPv4Addr));
            if (((qmap_ipv4_header_t)(skb->data))->src_address == htonl(pGobiNet->mQMIDev.IPv4Addr))
          {
             MuxId = pGobiNet->mQMIDev.MuxId;
          }
        for (i=0;i<MAX_MUX_DEVICES;i++)
        {
            //printk("GobiNet MUX src IP address : %x\n", htonl(pGobiNet->mQMIMUXDev[i].IPv4Addr));
               if (((qmap_ipv4_header_t)(skb->data))->src_address == htonl(pGobiNet->mQMIMUXDev[i].IPv4Addr))
           {
             MuxId = pGobiNet->mQMIMUXDev[i].MuxId;
           }
        }
         }
        command_packet = 0;
        break;
      }
      case 0x60:
      {
         PrintIPV6Addr("Packet src IP address : ", (ipv6_addr *)(((qmap_ipv6_header_t)(skb->data))->src_address));
           if (memcmp(((qmap_ipv6_header_t)(skb->data))->src_address, src_address, 16) != 0)
           {
            PrintIPV6Addr("GobiNet src IP address : ", &pGobiNet->mQMIDev.ipv6_address);
            if (memcmp(((qmap_ipv6_header_t)(skb->data))->src_address, pGobiNet->mQMIDev.ipv6_address.ipv6addr, 16) == 0)
            {
               MuxId = pGobiNet->mQMIDev.MuxId;
            }
          for (i=0;i<MAX_MUX_DEVICES;i++)
          {
               PrintIPV6Addr("GobiNet MUX src IP address : ", &pGobiNet->mQMIMUXDev[i].ipv6_address);
             if (memcmp(((qmap_ipv6_header_t)(skb->data))->src_address, pGobiNet->mQMIMUXDev[i].ipv6_address.ipv6addr, 16) == 0)
             {
               MuxId = pGobiNet->mQMIMUXDev[i].MuxId;
             }
          }
         }          
         command_packet = 0;
         break;
      }
      default :
      {
         return;
      }
      //if (memcmp(skb->data, BroadcastAddr, ETH_ALEN) == 0)
      //{
      //   skb_pull(skb, ETH_HLEN);
      //}
      //else
      //{
         /* ignoring QMAP command packets, handling only data packets */
      //   
      //}
   }
#endif

   ippacket_len = skb->len;
   qhdr = (qmap_p)skb_push(skb, sizeof(qmap_t));
   memset(qhdr, 0, sizeof(qmap_t));
#ifdef VIRTUAL_USB_CODE
   qhdr->mux_id = data;
#else
   qhdr->mux_id = MuxId;
#endif
   qhdr->cd_rsvd_pad = 0;

   if ((ippacket_len % 4) > 0) 
   {
      padding_count = 4 - (ippacket_len % 4);
   }
   
   qhdr->pkt_len = htons(ippacket_len + padding_count);
   qhdr->cd_rsvd_pad |= padding_count;

   if (padding_count > 0)
   {
      tmp = skb_put(skb, padding_count);
      memset(tmp , 0, padding_count);
   }
}

unsigned short qmap_ip_ethertype(struct sk_buff *skb)
{
        switch (skb->data[0] & 0xf0) {
        case 0x40:
                skb->protocol = __cpu_to_be16(ETH_P_IP);
                break;
        case 0x60:
                skb->protocol = __cpu_to_be16(ETH_P_IPV6);
                break;
        default:
                //printk("L3 protocol decode error: 0x%02x, len %d\n",
                //       skb->data[0] & 0xf0, skb->len);
                QC_LOG_GLOBAL("L3 protocol decode error: 0x%02x, len %d\n",
                       skb->data[0] & 0xf0, skb->len);
        }

        return skb->protocol;
}

void qmap_demux(struct usbnet *dev, struct sk_buff *skb)
{
   qmap_p qhdr;

   if (skb->data[0] & 0x80) {
      printk("command packet\n");
      return;
   }
   qhdr = (qmap_p)skb_pull(skb, sizeof(qmap_t));
   qmap_ip_ethertype(skb);
   usbnet_skb_return(dev, skb);
}

