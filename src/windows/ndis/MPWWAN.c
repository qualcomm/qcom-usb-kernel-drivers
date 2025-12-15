/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             M P W W A N . C

GENERAL DESCRIPTION
  This module contains Miniport function for handling WWAN OIDs

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdio.h>
#ifdef NDIS620_MINIPORT
#endif // NDIS620_MINIPORT
#include "MPMain.h"
#include <netioapi.h>
#include "MPUsb.h"
#include "MPQMUX.h"
#include "MPOID.h"
#include "MPWWAN.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPWWAN.tmh"

#endif   // EVENT_TRACING

/* ---------------------------------------------
          structures for reference

typedef union _SOCKADDR_INET
{
   SOCKADDR_IN Ipv4;
   SOCKADDR_IN6 Ipv6;
   ADDRESS_FAMILY si_family;
}  SOCKADDR_INET, *PSOCKADDR_INET;

SOCKADDR_IN defined as: (winsock.h)
struct sockaddr_in
{
   short   sin_family;
   u_short sin_port;
   struct  in_addr sin_addr;
   char    sin_zero[8];
};

in_addr defined as: (inaddr.h)
typedef struct in_addr
{
   union
   {
      struct { UCHAR s_b1,s_b2,s_b3,s_b4; } S_un_b;
      struct { USHORT s_w1,s_w2; } S_un_w;
      ULONG S_addr;
   }  S_un;
   #define s_addr  S_un.S_addr      // can be used for most tcp & ip code
   #define s_host  S_un.S_un_b.s_b2 // host on imp
   #define s_net   S_un.S_un_b.s_b1 // network
   #define s_imp   S_un.S_un_w.s_w2 // imp
   #define s_impno S_un.S_un_b.s_b4 // imp #
   #define s_lh    S_un.S_un_b.s_b3 // logical host
}  IN_ADDR, *PIN_ADDR, FAR *LPIN_ADDR;

typedef struct _IP_ADDRESS_PREFIX
{
   SOCKADDR_INET  Prefix;
   UINT8  PrefixLength;
}  IP_ADDRESS_PREFIX, *PIP_ADDRESS_PREFIX;

------------------------------------------------*/

BOOLEAN MPWWAN_IsWwanOid
(
   IN NDIS_HANDLE  MiniportAdapterContext,
   IN NDIS_OID     Oid
)
{
   #ifdef NDIS620_MINIPORT

   switch (Oid)
   {
      case OID_WWAN_DRIVER_CAPS:
      case OID_WWAN_DEVICE_CAPS:
      case OID_WWAN_READY_INFO:
      case OID_WWAN_HOME_PROVIDER:
      case OID_WWAN_REGISTER_STATE:
      case OID_WWAN_PACKET_SERVICE:
      case OID_WWAN_CONNECT:
      {
         return TRUE;
      }
      default:
      {
         break;
      }
   }

   #endif // NDIS620_MINIPORT

   return FALSE;
}  // MPWWAN_IsWwanOid

#ifdef NDIS620_MINIPORT
#ifdef QC_IP_MODE

NETIO_STATUS MPWWAN_CreateNetLuid(PMP_ADAPTER pAdapter)
{
   NETIO_STATUS status = STATUS_SUCCESS;

   // NDIS_MAKE_NET_LUID(&(pAdapter->NetLuid), pAdapter->NetIfType, pAdapter->NetLuidIndex);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> IPO: CreateNetLuid = 0x%I64x (0x%x)\n", pAdapter->PortName, pAdapter->NetLuid.Value, status)
   );

   return status;
}  // MPWWAN_CreateNetLuid

NETIO_STATUS MPWWAN_SetIPAddress(PMP_ADAPTER pAdapter)
{
   NETIO_STATUS status = STATUS_UNSUCCESSFUL;
   PMIB_UNICASTIPADDRESS_TABLE table = NULL;
   PMIB_UNICASTIPADDRESS_ROW   row, previousAddr = NULL;
   MIB_UNICASTIPADDRESS_ROW    newAddr;
   BOOLEAN bPreviousAddrFound = FALSE, bMatchFound = FALSE;
   int i;

   status = GetUnicastIpAddressTable(AF_INET, &table);
   if (status != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: GetUnicastIpAddressTable failure: 0x%x\n", pAdapter->PortName, status)
      );
   }

   for (i = 0; i < table->NumEntries; i++)
   {
      row = &(table->Table[i]);

      if (row->InterfaceLuid.Value == pAdapter->NetLuid.Value)
      {
         PUCHAR pHostAddr   = (PUCHAR)&(row->Address.Ipv4.sin_addr.s_addr);
         PUCHAR pDeviceAddr = (PUCHAR)&(pAdapter->IPSettings.IPV4.Address);

         bPreviousAddrFound = TRUE;
         previousAddr       = row;

         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_DETAIL,
            ("<%s> IPO: inspecting LUID 0x%I64x/0x%I64x - IPV4 %d.%d.%d.%d/%d.%d.%d.%d\n", pAdapter->PortName,
              row->InterfaceLuid.Value, pAdapter->NetLuid.Value,
              pHostAddr[0], pHostAddr[1], pHostAddr[2], pHostAddr[3],
              pDeviceAddr[0], pDeviceAddr[1], pDeviceAddr[2], pDeviceAddr[3])
         );

         if ((row->Address.Ipv4.sin_addr.s_addr == pAdapter->IPSettings.IPV4.Address) &&
             (bMatchFound == FALSE))
         {
            bMatchFound = TRUE;
         }
         else
         {
            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> IPO: removing IPV4 addr %d.%d.%d.%d\n", pAdapter->PortName,
                 pHostAddr[0], pHostAddr[1], pHostAddr[2], pHostAddr[3])
            );
            // remove
            DeleteUnicastIpAddressEntry(previousAddr);
            previousAddr = NULL;
         }
      }
   }  // for

   // create new IP address entry if necessary
   if (bMatchFound == FALSE && (pAdapter->IPSettings.IPV4.Flags != 0))
   {
      ULONG        subnetMask;
      int          i;
      PUCHAR       pHostAddr, pSubnetMask;

      InitializeUnicastIpAddressEntry(&newAddr);
      newAddr.InterfaceLuid.Value = pAdapter->NetLuid.Value;

      subnetMask = pAdapter->IPSettings.IPV4.SubnetMask;
      newAddr.OnLinkPrefixLength = 0;
      for (i = 0; i < 32; i++)
      {
         newAddr.OnLinkPrefixLength += (subnetMask & 0x1);
         subnetMask >>= 1;
      }
      newAddr.Address.Ipv4.sin_family = AF_INET;  // IPV4 for now
      newAddr.Address.Ipv4.sin_addr.s_addr = pAdapter->IPSettings.IPV4.Address;
      newAddr.PrefixOrigin = IpPrefixOriginManual;
      newAddr.SuffixOrigin = IpSuffixOriginManual;
      pHostAddr = (PUCHAR)&(newAddr.Address.Ipv4.sin_addr.s_addr);
      pSubnetMask = (PUCHAR)&(pAdapter->IPSettings.IPV4.SubnetMask);

      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_DETAIL,
         ("<%s> IPO: Creating IP addr entry - IPV4 %d.%d.%d.%d SubnetMask %d.%d.%d.%d(%d)\n",
           pAdapter->PortName, pHostAddr[0], pHostAddr[1], pHostAddr[2], pHostAddr[3],
           pSubnetMask[0], pSubnetMask[1], pSubnetMask[2], pSubnetMask[3], newAddr.OnLinkPrefixLength)
      );

      status = CreateUnicastIpAddressEntry(&newAddr);
      if (status != STATUS_SUCCESS)
      {
         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPO: new IP addr entry creation failure 0x%x\n", pAdapter->PortName, status)
         );
      }
      else
      {
         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_DETAIL,
            ("<%s> IPO: new IP addr added: %d.%d.%d.%d SubnetMask %d.%d.%d.%d PrefixLen=%d\n",
              pAdapter->PortName, pHostAddr[0], pHostAddr[1], pHostAddr[2], pHostAddr[3],
              pSubnetMask[0], pSubnetMask[1], pSubnetMask[2], pSubnetMask[3],
              newAddr.OnLinkPrefixLength)
         );
      }
   }

   if (table != NULL)
   {
      FreeMibTable(table);
   }

SETIPV6ADDR:

   MPWWAN_SetIPAddressV6(pAdapter);

   return status;

}  // MPWWAN_SetIPAddress

NETIO_STATUS MPWWAN_SetDefaultGateway(PMP_ADAPTER pAdapter)
{
   NETIO_STATUS          status;
   PMIB_IPFORWARD_TABLE2 table = NULL;
   PMIB_IPFORWARD_ROW2   row, previousEntry = NULL;
   MIB_IPFORWARD_ROW2    newEntry;
   BOOLEAN bPreviousEntryFound = FALSE, bMatchFound = FALSE;
   int i;
   PUCHAR pGateway, pGateway2;

   pGateway2 = (PUCHAR)&(pAdapter->IPSettings.IPV4.Gateway);

   status = GetIpForwardTable2(AF_INET, &table);
   if (status != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: GetIpForwardTable2 failure: 0x%x\n", pAdapter->PortName, status)
      );
   }

   for (i = 0; i < table->NumEntries; i++)
   {
      row = &(table->Table[i]);
      pGateway = (PUCHAR)&(row->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr);

      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_DETAIL,
         ("<%s> IPO: inspecting LUID 0x%I64x/0x%I64x - IPV4 Gateway %d.%d.%d.%d/%d.%d.%d.%d\n",
           pAdapter->PortName, row->InterfaceLuid.Value, pAdapter->NetLuid.Value,
           pGateway[0], pGateway[1], pGateway[2], pGateway[3],
           pGateway2[0], pGateway2[1], pGateway2[2], pGateway2[3])
      );

      if ((row->InterfaceLuid.Value == pAdapter->NetLuid.Value) &&
          (row->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr == 0))
      {
         bPreviousEntryFound = TRUE;
         previousEntry      = row;

         if ((row->NextHop.Ipv4.sin_addr.s_addr == pAdapter->IPSettings.IPV4.Gateway) &&
             (bMatchFound == FALSE))
         {
            bMatchFound = TRUE;
            // break;
         }
         else
         {
            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> IPO: removing gateway %d.%d.%d.%d\n", pAdapter->PortName,
                 pGateway[0], pGateway[1], pGateway[2], pGateway[3])
            );
            // delete
            DeleteIpForwardEntry2(previousEntry);
            previousEntry = NULL;
         }
      }
   }  // for

   if (bMatchFound == FALSE && (pAdapter->IPSettings.IPV4.Gateway != 0))
   {
      InitializeIpForwardEntry(&newEntry);
      newEntry.InterfaceLuid = pAdapter->NetLuid;
      newEntry.DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET; // IPV4 for now
      newEntry.DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr = 0; // (0.0.0.0)
      newEntry.DestinationPrefix.PrefixLength = 0;
      newEntry.NextHop.Ipv4.sin_addr.s_addr = pAdapter->IPSettings.IPV4.Gateway;
      newEntry.NextHop.Ipv4.sin_family = AF_INET;  // IPV4 for now

      // create new entry
      status = CreateIpForwardEntry2(&newEntry);
      if (status != STATUS_SUCCESS)
      {
         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPO: new gateway creation failure 0x%x\n", pAdapter->PortName, status)
         );
      }
      else
      {
         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_DETAIL,
            ("<%s> IPO: new gateway created: %d.%d.%d.%d\n", pAdapter->PortName,
              pGateway2[0], pGateway2[1], pGateway2[2], pGateway2[3])
         );
      }
   }

   if (table != NULL)
   {
      FreeMibTable(table);
   }

   MPWWAN_SetDefaultGatewayV6(pAdapter);

   return status;

}  // MPWWAN_SetDefaultGateway

NETIO_STATUS MPWWAN_SetDNSAddressV4(PMP_ADAPTER pAdapter)
{
   UNICODE_STRING ucsDNS;
   ANSI_STRING    ansDNS;
   CHAR           *addrBuf, *addrPtr;
   NTSTATUS       ntStatus;
   PUCHAR         pDnsP = (PUCHAR)&(pAdapter->IPSettings.IPV4.DnsPrimary);
   PUCHAR         pDnsS = (PUCHAR)&(pAdapter->IPSettings.IPV4.DnsSecondary);

   if (pAdapter->DnsRegPathV4.Buffer == NULL)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: SetDNSAddressV4: not available\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }
   
   addrBuf = pAdapter->DnsInfoV4;
   RtlZeroMemory(addrBuf, 256);

   // primary DNS
   if (pAdapter->IPSettings.IPV4.DnsPrimary == 0)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: SetDNSAddressV4: no PrimaryDNS\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }
   addrPtr = addrBuf;
   sprintf(addrPtr, "%d.%d.%d.%d", pDnsP[0], pDnsP[1], pDnsP[2], pDnsP[3]);
   addrPtr += strlen(addrBuf);

   // secondary DNS
   if (pAdapter->IPSettings.IPV4.DnsSecondary != 0)
   {
      sprintf(addrPtr, " %d.%d.%d.%d", pDnsS[0], pDnsS[1], pDnsS[2], pDnsS[3]);
   }

   ansDNS.Length        = strlen(addrBuf);
   ansDNS.MaximumLength = 256;
   ansDNS.Buffer        = addrBuf;
   ntStatus = RtlAnsiStringToUnicodeString(&ucsDNS, &ansDNS, TRUE);
   if (ntStatus != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: SetDNSAddressV4: failure 0x%x\n", pAdapter->PortName, ntStatus)
      );
      return STATUS_UNSUCCESSFUL;
   }

   ntStatus = RtlWriteRegistryValue
              (
                 RTL_REGISTRY_SERVICES,
                 pAdapter->DnsRegPathV4.Buffer,
                 L"NameServer",
                 REG_SZ,
                 ucsDNS.Buffer,
                 ucsDNS.Length + sizeof(WCHAR)
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: SetDNSAddressV4 FAILURE: %s\n", pAdapter->PortName, addrBuf)
      );
   }
   else
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_DETAIL,
         ("<%s> IPO: SetDNSAddressV4: %s\n", pAdapter->PortName, addrBuf)
      );
   }

   RtlFreeUnicodeString(&ucsDNS);

   return ntStatus;

}  // MPWWAN_SetDNSAddressV4

NETIO_STATUS MPWWAN_SetIPAddressV6(PMP_ADAPTER pAdapter)
{
   NETIO_STATUS status = STATUS_UNSUCCESSFUL;
   PMIB_UNICASTIPADDRESS_TABLE table = NULL;
   PMIB_UNICASTIPADDRESS_ROW   row, previousAddr = NULL;
   MIB_UNICASTIPADDRESS_ROW    newAddr;
   BOOLEAN bPreviousAddrFound = FALSE, bMatchFound = FALSE;
   int i, n;

   status = GetUnicastIpAddressTable(AF_INET6, &table);
   if (status != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPV6: SetIPAddr: GetUnicastIpAddressTable failure: 0x%x\n", pAdapter->PortName, status)
      );
   }

   if (status == STATUS_SUCCESS)
   {
      for (i = 0; i < table->NumEntries; i++)
      {
         row = &(table->Table[i]);

         if (row->InterfaceLuid.Value == pAdapter->NetLuid.Value)
         {
            PUSHORT pHostAddr   = (PUSHORT)(row->Address.Ipv6.sin6_addr.u.Word);
            PUSHORT pDeviceAddr = (PUSHORT)(pAdapter->IPSettings.IPV6.Address.Word);

            bPreviousAddrFound = TRUE;
            previousAddr       = row;

            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> IPV6: SetIPAddr: inspecting LUID 0x%I64x/0x%I64x\n",
                 pAdapter->PortName, row->InterfaceLuid.Value, pAdapter->NetLuid.Value)
            );

            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> IPV6: SetIPAddr: %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x vs %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
                 pAdapter->PortName,
                 ntohs(pHostAddr[0]), ntohs(pHostAddr[1]), ntohs(pHostAddr[2]), ntohs(pHostAddr[3]),
                 ntohs(pHostAddr[4]), ntohs(pHostAddr[5]), ntohs(pHostAddr[6]), ntohs(pHostAddr[7]),
                 ntohs(pDeviceAddr[0]), ntohs(pDeviceAddr[1]), ntohs(pDeviceAddr[2]), ntohs(pDeviceAddr[3]),
                 ntohs(pDeviceAddr[4]), ntohs(pDeviceAddr[5]), ntohs(pDeviceAddr[6]), ntohs(pDeviceAddr[7]))
            );

            n = RtlCompareMemory
                (
                   row->Address.Ipv6.sin6_addr.u.Byte,
                   pAdapter->IPSettings.IPV6.Address.Byte,
                   16
                );
 
            if ((n == 16) && (bMatchFound == FALSE))
            {
               bMatchFound = TRUE;
            }
            else
            {
               ULONG err;

               // remove
               err = DeleteUnicastIpAddressEntry(previousAddr);
               QCNET_DbgPrint
               (
                  (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
                  MP_DBG_LEVEL_DETAIL,
                  ("<%s> IPV6: SetIPAddr: remove IPV6 addr: 0x%x\n", pAdapter->PortName, err)
               );
               previousAddr = NULL;
            }
         }
      }  // for
   }

   // create new IP address entry if necessary
   if (bMatchFound == FALSE && (pAdapter->IPSettings.IPV6.Flags != 0))
   {
      int          i;
      PUSHORT      pHostAddr;

      InitializeUnicastIpAddressEntry(&newAddr);
      newAddr.InterfaceLuid.Value = pAdapter->NetLuid.Value;

      newAddr.OnLinkPrefixLength = pAdapter->IPSettings.IPV6.PrefixLengthIPAddr;
      if (newAddr.OnLinkPrefixLength == 0)
      {
         newAddr.OnLinkPrefixLength = 64; // default for WWAN
      }
      newAddr.Address.Ipv6.sin6_family = AF_INET6;  // IPV6
      RtlCopyMemory
      (
         newAddr.Address.Ipv6.sin6_addr.u.Byte,
         pAdapter->IPSettings.IPV6.Address.Byte,
         16
      );
      newAddr.PrefixOrigin = IpPrefixOriginManual;
      newAddr.SuffixOrigin = IpSuffixOriginManual;
      pHostAddr = (PUSHORT)(newAddr.Address.Ipv6.sin6_addr.u.Word);

      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_DETAIL,
         ("<%s> IPV6: SetIPAddr: Creating IP addr entry - %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x (%d)\n",
           pAdapter->PortName,
           ntohs(pHostAddr[0]), ntohs(pHostAddr[1]), ntohs(pHostAddr[2]), ntohs(pHostAddr[3]),
           ntohs(pHostAddr[4]), ntohs(pHostAddr[5]), ntohs(pHostAddr[6]), ntohs(pHostAddr[7]),
           newAddr.OnLinkPrefixLength)
      );

      status = CreateUnicastIpAddressEntry(&newAddr);
      if (status != STATUS_SUCCESS)
      {
         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPV6: SetIPAddr: new IP addr entry creation failure 0x%x\n", pAdapter->PortName, status)
         );
      }
      else
      {
         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_DETAIL,
            ("<%s> IPV6: SetIPAddr: new IP addr added: %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
              pAdapter->PortName,
              ntohs(pHostAddr[0]), ntohs(pHostAddr[1]), ntohs(pHostAddr[2]), ntohs(pHostAddr[3]),
              ntohs(pHostAddr[4]), ntohs(pHostAddr[5]), ntohs(pHostAddr[6]), ntohs(pHostAddr[7]))
         );
      }
   }

   if (table != NULL)
   {
      FreeMibTable(table);
   }

   return status;

}  // MPWWAN_SetIPAddressV6

NETIO_STATUS MPWWAN_SetDefaultGatewayV6(PMP_ADAPTER pAdapter)
{
   NETIO_STATUS          status;
   PMIB_IPFORWARD_TABLE2 table = NULL;
   PMIB_IPFORWARD_ROW2   row, previousEntry = NULL;
   MIB_IPFORWARD_ROW2    newEntry;
   BOOLEAN bPreviousEntryFound = FALSE, bMatchFound = FALSE;
   int i, n;
   PUSHORT pGateway, pGateway2;

   pGateway2 = (PUSHORT)(pAdapter->IPSettings.IPV6.Gateway.Word);

   status = GetIpForwardTable2(AF_INET6, &table);
   if (status != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPV6: SetGateway: GetIpForwardTable2 failure: 0x%x\n", pAdapter->PortName, status)
      );
   }

   if (status == STATUS_SUCCESS)
   {
      for (i = 0; i < table->NumEntries; i++)
      {
         row = &(table->Table[i]);
         pGateway = (PUSHORT)(row->DestinationPrefix.Prefix.Ipv6.sin6_addr.u.Word);

         if (row->InterfaceLuid.Value == pAdapter->NetLuid.Value)
         {
            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> IPV6: SetGateway: %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x vs %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
                 pAdapter->PortName,
                 ntohs(pGateway[0]),  ntohs(pGateway[1]),  ntohs(pGateway[2]),  ntohs(pGateway[3]),
                 ntohs(pGateway[4]),  ntohs(pGateway[5]),  ntohs(pGateway[6]),  ntohs(pGateway[7]),
                 ntohs(pGateway2[0]), ntohs(pGateway2[1]), ntohs(pGateway2[2]), ntohs(pGateway2[3]),
                 ntohs(pGateway2[4]), ntohs(pGateway2[5]), ntohs(pGateway2[6]), ntohs(pGateway2[7]))
            );

            bPreviousEntryFound = TRUE;
            previousEntry      = row;

            n = RtlCompareMemory
                (
                   row->DestinationPrefix.Prefix.Ipv6.sin6_addr.u.Byte,
                   pAdapter->IPSettings.IPV6.Gateway.Byte,
                   16
                );
            if ((n == 16) && (bMatchFound == FALSE))
            {
               bMatchFound = TRUE;
            }
            else
            {
               QCNET_DbgPrint
               (
                  (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
                  MP_DBG_LEVEL_DETAIL,
                  ("<%s> IPV6: SetGateway: remove the gateway\n", pAdapter->PortName)
               );
               // delete
               DeleteIpForwardEntry2(previousEntry);
               previousEntry = NULL;
            }
         }
      }  // for
   }

   if (bMatchFound == FALSE && (pAdapter->IPSettings.IPV6.Flags != 0))
   {
      InitializeIpForwardEntry(&newEntry);
      newEntry.InterfaceLuid = pAdapter->NetLuid;
      newEntry.DestinationPrefix.Prefix.Ipv6.sin6_family = AF_INET6; // IPV6
      RtlZeroMemory
      (
         newEntry.DestinationPrefix.Prefix.Ipv6.sin6_addr.u.Byte,
         16
      );
      newEntry.DestinationPrefix.PrefixLength = 0;
      RtlCopyMemory
      (
         newEntry.NextHop.Ipv6.sin6_addr.u.Byte,
         pAdapter->IPSettings.IPV6.Gateway.Byte,
         16
      );
      newEntry.NextHop.Ipv6.sin6_family = AF_INET6;  // IPV6
      // newEntry.SitePrefixLength = pAdapter->IPSettings.IPV6.PrefixLengthGateway;
      // if (newEntry.SitePrefixLength == 0)
      // {
      //    newEntry.SitePrefixLength = 64;
      // }

      // create new entry
      status = CreateIpForwardEntry2(&newEntry);
      if (status != STATUS_SUCCESS)
      {
         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPV6: SetGateway: new gateway creation failure 0x%x\n", pAdapter->PortName, status)
         );
      }
      else
      {
         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_DETAIL,
            ("<%s> IPV6: SetGateway: new gateway created: %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n", pAdapter->PortName,
              ntohs(pGateway2[0]), ntohs(pGateway2[1]), ntohs(pGateway2[2]), ntohs(pGateway2[3]),
              ntohs(pGateway2[4]), ntohs(pGateway2[5]), ntohs(pGateway2[6]), ntohs(pGateway2[7]))
         );
      }
   }

   if (table != NULL)
   {
      FreeMibTable(table);
   }

   return status;

}  // MPWWAN_SetDefaultGateway6

NETIO_STATUS MPWWAN_SetDNSAddressV6(PMP_ADAPTER pAdapter)
{
   UNICODE_STRING ucsDNS;
   ANSI_STRING    ansDNS;
   CHAR           *addrBuf, *addrPtr;
   NTSTATUS       ntStatus;
   PUSHORT        pDnsP = (PUSHORT)&(pAdapter->IPSettings.IPV6.DnsPrimary.Word);
   PUSHORT        pDnsS = (PUSHORT)&(pAdapter->IPSettings.IPV6.DnsSecondary.Word);

   if (pAdapter->DnsRegPathV6.Buffer == NULL)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPV6: SetDNSAddressV6: not available\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   addrBuf = pAdapter->DnsInfoV6;
   RtlZeroMemory(addrBuf, 256);

   // primary DNS
   if (pAdapter->IPSettings.IPV6.Flags == 0)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPV6: SetDNSAddressV6: no info available\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   addrPtr = addrBuf;

/*****************************************
   // Place IPV4 DNS first
   if (strlen(pAdapter->DnsInfoV4) > 0)
   {
      sprintf(addrPtr, "%s", pAdapter->DnsInfoV4);
      addrPtr += strlen(addrBuf);
   }
*******************************************/

   // IPV6 primary DNS
   sprintf(addrPtr, "%x:%x:%x:%x:%x:%x:%x:%x",
           ntohs(pDnsP[0]), ntohs(pDnsP[1]), ntohs(pDnsP[2]), ntohs(pDnsP[3]),
           ntohs(pDnsP[4]), ntohs(pDnsP[5]), ntohs(pDnsP[6]), ntohs(pDnsP[7]));
   addrPtr += strlen(addrBuf);

   // IPV6 secondary DNS
   if (pAdapter->IPSettings.IPV6.DnsSecondary.Word[0] != 0)
   {
      sprintf(addrPtr, " %x:%x:%x:%x:%x:%x:%x:%x", 
              ntohs(pDnsS[0]), ntohs(pDnsS[1]), ntohs(pDnsS[2]), ntohs(pDnsS[3]),
              ntohs(pDnsS[4]), ntohs(pDnsS[5]), ntohs(pDnsS[6]), ntohs(pDnsS[7]));
   }

   ansDNS.Length        = strlen(addrBuf);
   ansDNS.MaximumLength = 256;
   ansDNS.Buffer        = addrBuf;
   ntStatus = RtlAnsiStringToUnicodeString(&ucsDNS, &ansDNS, TRUE);
   if (ntStatus != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPV6: SetDNSAddressV6: failure 0x%x\n", pAdapter->PortName, ntStatus)
      );
      return STATUS_UNSUCCESSFUL;
   }

   ntStatus = RtlWriteRegistryValue
              (
                 RTL_REGISTRY_SERVICES,
                 pAdapter->DnsRegPathV6.Buffer,
                 L"NameServer",
                 REG_SZ,
                 ucsDNS.Buffer,
                 ucsDNS.Length + sizeof(WCHAR)
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPV6: SetDNSAddressV6 FAILURE: %s\n", pAdapter->PortName, addrBuf)
      );
   }
   else
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_DETAIL,
         ("<%s> IPV6: SetDNSAddressV6: %s\n", pAdapter->PortName, addrBuf)
      );
   }

   RtlFreeUnicodeString(&ucsDNS);

   return ntStatus;

}  // MPWWAN_SetDNSAddressV6

NETIO_STATUS MPWWAN_ClearDNSAddress(PMP_ADAPTER pAdapter, UCHAR IPVer)
{
   UCHAR    dummy[8];
   NTSTATUS ntStatus;

   RtlZeroMemory(dummy, 8);

   if ((IPVer == 0) || (IPVer == 4))
   {
      if ((pAdapter->DnsRegPathV4.Buffer != NULL))
      {
         ntStatus = RtlWriteRegistryValue
                    (
                       RTL_REGISTRY_SERVICES,
                       pAdapter->DnsRegPathV4.Buffer,
                       L"NameServer",
                       REG_SZ,
                       (PVOID)dummy,
                       sizeof(WCHAR)
                    );
         if (ntStatus != STATUS_SUCCESS)
         {
            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_ERROR,
               ("<%s> IPO: ClearDNSAddress V4 FAILURE 0x%x\n", pAdapter->PortName, ntStatus)
            );
         }
         else
         {
            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> IPO: ClearDNSAddress V4 OK\n", pAdapter->PortName)
            );
         }
      }
   }

   if ((IPVer == 0) || (IPVer == 6))
   {
      if ((pAdapter->DnsRegPathV6.Buffer != NULL))
      {
         ntStatus = RtlWriteRegistryValue
                    (
                       RTL_REGISTRY_SERVICES,
                       pAdapter->DnsRegPathV6.Buffer,
                       L"NameServer",
                       REG_SZ,
                       (PVOID)dummy,
                       sizeof(WCHAR)
                    );
         if (ntStatus != STATUS_SUCCESS)
         {
            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_ERROR,
               ("<%s> IPO: ClearDNSAddress V6 FAILURE 0x%x\n", pAdapter->PortName, ntStatus)
            );
         }
         else
         {
            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> IPO: ClearDNSAddress V6 OK\n", pAdapter->PortName)
            );
         }
      }
   }

   return ntStatus;

}  // MPWWAN_ClearDNSAddress

NETIO_STATUS MPWWAN_SetDNSAddress(PMP_ADAPTER pAdapter)
{
   if ((QCMP_NDIS620_Ok == FALSE) || (pAdapter->IPModeEnabled == FALSE))
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: SetDNSAddress: no act (NDIS620 %d IPMode %d)\n", pAdapter->PortName,
            QCMP_NDIS620_Ok, pAdapter->IPModeEnabled)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (pAdapter->IPSettings.IPV4.Flags != 0)
   {
      MPWWAN_SetDNSAddressV4(pAdapter);
   }

   if (pAdapter->IPSettings.IPV6.Flags != 0)
   {
      MPWWAN_SetDNSAddressV6(pAdapter);
   }

   return STATUS_SUCCESS;;
}  // MPWWAN_SetDNSAddress

VOID MPWWAN_AdvertiseIPSettings(PMP_ADAPTER pAdapter)
{
   if (QCMP_NDIS620_Ok == FALSE)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: AdvertiseIPSettings: no action(NDIS620 not available)\n", pAdapter->PortName)
      );
      return;
   }

   if (pAdapter->NdisMediumType != NdisMediumWirelessWan)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: AdvertiseIPSettings: no action(medium not wwan)\n", pAdapter->PortName)
      );
      return;
   }

   if (STATUS_SUCCESS != MPWWAN_CreateNetLuid(pAdapter))
   {
      return;
   }
   if (STATUS_SUCCESS != MPWWAN_SetIPAddress(pAdapter))
   {
      return;
   }

   MPWWAN_SetDefaultGateway(pAdapter);

}  // MPWWAN_AdvertiseIPSettings

VOID MPWWAN_ClearIPSettings(PMP_ADAPTER pAdapter)
{
   if (QCMP_NDIS620_Ok == FALSE)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: ClearIPSettings: no action(NDIS620 not available)\n", pAdapter->PortName)
      );
      return;
   }

   if (pAdapter->NdisMediumType != NdisMediumWirelessWan)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: ClearIPSettings: no action(medium not wwan)\n", pAdapter->PortName)
      );
      return;
   }

   MPWWAN_ClearIPAddress(pAdapter);
   MPWWAN_ClearDefaultGateway(pAdapter);

}  // MPWWAN_ClearIPSettings

NETIO_STATUS MPWWAN_ClearIPAddress(PMP_ADAPTER pAdapter)
{
   NETIO_STATUS status = STATUS_UNSUCCESSFUL;
   PMIB_UNICASTIPADDRESS_TABLE table = NULL;
   PMIB_UNICASTIPADDRESS_ROW   row, previousAddr = NULL;
   int i;

   status = GetUnicastIpAddressTable(AF_INET, &table);
   if (status != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: _ClearIPAddress: GetUnicastIpAddressTable failure: 0x%x\n", pAdapter->PortName, status)
      );

      return status;
   }

   for (i = 0; i < table->NumEntries; i++)
   {
      row = &(table->Table[i]);

      if (row->InterfaceLuid.Value == pAdapter->NetLuid.Value)
      {
         PUCHAR pHostAddr   = (PUCHAR)&(row->Address.Ipv4.sin_addr.s_addr);
         previousAddr       = row;

         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_DETAIL,
            ("<%s> IPO: purging LUID 0x%I64x - IPV4 %d.%d.%d.%d\n", pAdapter->PortName,
              row->InterfaceLuid.Value, pHostAddr[0], pHostAddr[1], pHostAddr[2], pHostAddr[3])
         );

         // if (row->Address.Ipv4.sin_addr.s_addr != 0)
         {
            DeleteUnicastIpAddressEntry(previousAddr);
         }
      }
   }  // for

   if (table != NULL)
   {
      FreeMibTable(table);
   }

   MPWWAN_ClearIPAddressV6(pAdapter);

   return status;

}  // MPWWAN_ClearIPAddress

NETIO_STATUS MPWWAN_ClearDefaultGateway(PMP_ADAPTER pAdapter)
{
   NETIO_STATUS          status;
   PMIB_IPFORWARD_TABLE2 table = NULL;
   PMIB_IPFORWARD_ROW2   row, previousEntry = NULL;
   int i;
   PUCHAR pGateway;

   status = GetIpForwardTable2(AF_INET, &table);
   if (status != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPO: _ClearDefaultGateway: GetIpForwardTable2 failure: 0x%x\n", pAdapter->PortName, status)
      );
      return status;
   }

   for (i = 0; i < table->NumEntries; i++)
   {
      row = &(table->Table[i]);
      pGateway = (PUCHAR)&(row->DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr);

      if (row->InterfaceLuid.Value == pAdapter->NetLuid.Value)
      {
         previousEntry = row;

         QCNET_DbgPrint
         (
            (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_DETAIL,
            ("<%s> IPO: LUID 0x%I64x removing gateway %d.%d.%d.%d\n", pAdapter->PortName,
              row->InterfaceLuid.Value, pGateway[0], pGateway[1], pGateway[2], pGateway[3])
         );

         DeleteIpForwardEntry2(previousEntry);
      }
   }  // for

   if (table != NULL)
   {
      FreeMibTable(table);
   }

   MPWWAN_ClearDefaultGatewayV6(pAdapter);

   return status;

}  // MPWWAN_ClearDefaultGateway

NETIO_STATUS MPWWAN_ClearIPAddressV6(PMP_ADAPTER pAdapter)
{
   NETIO_STATUS status = STATUS_UNSUCCESSFUL;
   PMIB_UNICASTIPADDRESS_TABLE table = NULL;
   PMIB_UNICASTIPADDRESS_ROW   row, previousAddr = NULL;
   int i;

   status = GetUnicastIpAddressTable(AF_INET6, &table);
   if (status != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPV6: CLearIPAddrV6: GetUnicastIpAddressTable failure: 0x%x\n", pAdapter->PortName, status)
      );
   }

   if (status == STATUS_SUCCESS)
   {
      for (i = 0; i < table->NumEntries; i++)
      {
         row = &(table->Table[i]);

         if (row->InterfaceLuid.Value == pAdapter->NetLuid.Value)
         {
            PUSHORT pHostAddr   = (PUSHORT)(row->Address.Ipv6.sin6_addr.u.Word);
            ULONG err;

            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> IPV6: LUID 0x%I64x purging %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
                 pAdapter->PortName, row->InterfaceLuid.Value,
                 ntohs(pHostAddr[0]), ntohs(pHostAddr[1]), ntohs(pHostAddr[2]), ntohs(pHostAddr[3]),
                 ntohs(pHostAddr[4]), ntohs(pHostAddr[5]), ntohs(pHostAddr[6]), ntohs(pHostAddr[7]))
            );

            previousAddr = row;
            err = DeleteUnicastIpAddressEntry(previousAddr);
            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> IPV6: ClearIPAddrV6: status 0x%x\n", pAdapter->PortName, err)
            );
         }
      }  // for
   }

   if (table != NULL)
   {
      FreeMibTable(table);
   }

   return status;

}  // MPWWAN_ClearIPAddressV6

NETIO_STATUS MPWWAN_ClearDefaultGatewayV6(PMP_ADAPTER pAdapter)
{
   NETIO_STATUS          status;
   PMIB_IPFORWARD_TABLE2 table = NULL;
   PMIB_IPFORWARD_ROW2   row, previousEntry;
   int i;
   PUSHORT pGateway;

   status = GetIpForwardTable2(AF_INET6, &table);
   if (status != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_ERROR,
         ("<%s> IPV6: ClearGatewayV6: GetIpForwardTable2 failure: 0x%x\n", pAdapter->PortName, status)
      );
   }

   if (status == STATUS_SUCCESS)
   {
      for (i = 0; i < table->NumEntries; i++)
      {
         row = &(table->Table[i]);
         pGateway = (PUSHORT)(row->DestinationPrefix.Prefix.Ipv6.sin6_addr.u.Word);

         if (row->InterfaceLuid.Value == pAdapter->NetLuid.Value)
         {
            QCNET_DbgPrint
            (
               (MP_DBG_MASK_CONTROL | MP_DBG_MASK_READ | MP_DBG_MASK_WRITE),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> IPV6: ClearGateway: LUID 0x%I64x purging %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
                 pAdapter->PortName, pAdapter->NetLuid.Value, 
                 ntohs(pGateway[0]),  ntohs(pGateway[1]),  ntohs(pGateway[2]),  ntohs(pGateway[3]),
                 ntohs(pGateway[4]),  ntohs(pGateway[5]),  ntohs(pGateway[6]),  ntohs(pGateway[7]))
            );

            previousEntry = row;
            DeleteIpForwardEntry2(previousEntry);
         }
      }  // for
   }

   if (table != NULL)
   {
      FreeMibTable(table);
   }

   return status;

}  // MPWWAN_ClearDefaultGateway6

#endif // QC_IP_MODE
#endif // NDIS620_MINIPORT
