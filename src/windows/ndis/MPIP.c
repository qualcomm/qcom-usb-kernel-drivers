/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P I P . C

GENERAL DESCRIPTION
    This module contains WDS Client functions for IP(V6).
    requests.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdio.h>
#include "MPMain.h"
#include "MPUsb.h"
#include "MPQCTL.h"
#include "MPIOC.h"
#include "MPQMUX.h"
#include "MPIP.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPIP.tmh"

#endif   // EVENT_TRACING


NTSTATUS MPIP_StartWdsIpClient(PMP_ADAPTER pAdapter)
{
   NTSTATUS          ntStatus;
   OBJECT_ATTRIBUTES objAttr;

   if (QCMAIN_IsDualIPSupported(pAdapter) == FALSE)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> StartWdsIpClient - no dual IP support\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> StartWdsIpClient - wrong IRQL::%d\n", pAdapter->PortName, KeGetCurrentIrql())
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((pAdapter->pWdsIpThread != NULL) || (pAdapter->hWdsIpThreadHandle != NULL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> WdsIpClient up/resumed\n", pAdapter->PortName)
      );
      return STATUS_SUCCESS;
   }

   KeClearEvent(&pAdapter->WdsIpThreadStartedEvent);
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   ntStatus = PsCreateSystemThread
              (
                 OUT &pAdapter->hWdsIpThreadHandle,
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)MPIP_WdsIpThread,
                 IN (PVOID)pAdapter
              );
   if ((!NT_SUCCESS(ntStatus)) || (pAdapter->hWdsIpThreadHandle == NULL))
   {
      pAdapter->hWdsIpThreadHandle = NULL;
      pAdapter->pWdsIpThread = NULL;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> WdsIp th failure\n", pAdapter->PortName)
      );
      return ntStatus;
   }

   ntStatus = KeWaitForSingleObject
              (
                 &pAdapter->WdsIpThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );


   if (pAdapter->hWdsIpThreadHandle != NULL )
   {
   ntStatus = ObReferenceObjectByHandle
              (
                 pAdapter->hWdsIpThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&pAdapter->pWdsIpThread,
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> WdsIp: ObReferenceObjectByHandle failed 0x%x\n", pAdapter->PortName, ntStatus)
      );
      pAdapter->pWdsIpThread = NULL;
   }
   else
   {
      if (STATUS_SUCCESS != (ntStatus = ZwClose(pAdapter->hWdsIpThreadHandle)))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> WdsIp ZwClose failed - 0x%x\n", pAdapter->PortName, ntStatus)
         );
      }
      else
      {
         pAdapter->hWdsIpThreadHandle = NULL;
      }
   }
   }
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> WdsIp handle=0x%p thOb=0x%p\n", pAdapter->PortName,
       pAdapter->hWdsIpThreadHandle, pAdapter->pWdsIpThread)
   );

   return ntStatus;

}  // MPIP_StartWdsIpClient

NTSTATUS MPIP_CancelWdsIpClient(PMP_ADAPTER pAdapter)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER delayValue;
   LARGE_INTEGER timeoutValue;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->WdsIp Cxl\n", pAdapter->PortName)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> WdsIp Cxl: wrong IRQL\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (InterlockedIncrement(&pAdapter->IpThreadCancelStarted) > 1)
   {
      while ((pAdapter->hWdsIpThreadHandle != NULL) || (pAdapter->pWdsIpThread != NULL))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> WdsIp cxl in pro\n", pAdapter->PortName)
         );
         MPMAIN_Wait(-(3 * 1000 * 1000));
      }
      InterlockedDecrement(&pAdapter->IpThreadCancelStarted);
      return STATUS_SUCCESS;
   }

   if ((pAdapter->hWdsIpThreadHandle != NULL) || (pAdapter->pWdsIpThread != NULL))
   {
      KeClearEvent(&pAdapter->WdsIpThreadClosedEvent);
      KeSetEvent
      (
         &pAdapter->WdsIpThreadCancelEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pAdapter->pWdsIpThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pAdapter->pWdsIpThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pAdapter->pWdsIpThread);
         KeClearEvent(&pAdapter->WdsIpThreadClosedEvent);
         ZwClose(pAdapter->hWdsIpThreadHandle);
         pAdapter->pWdsIpThread = NULL;
      }
      else  // best effort
      {
         timeoutValue.QuadPart = -(30 * 1000 * 1000);   // 3.0 sec
         ntStatus = KeWaitForSingleObject
                    (
                       &pAdapter->WdsIpThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       &timeoutValue
                    );
         KeClearEvent(&pAdapter->WdsIpThreadClosedEvent);
         ZwClose(pAdapter->hWdsIpThreadHandle);
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--WdsIp Cxl\n", pAdapter->PortName)
   );
   InterlockedDecrement(&pAdapter->IpThreadCancelStarted);
   return ntStatus;
}  // MPIP_CancelWdsIpClient

VOID MPIP_WdsIpThread(PVOID Context)
{
   PMP_ADAPTER  pAdapter = (PMP_ADAPTER)Context;
   NTSTATUS     ntStatus;
   NDIS_STATUS  ndisStatus;
   BOOLEAN      bKeepRunning = TRUE;
   BOOLEAN      bThreadStarted = FALSE;
   PKWAIT_BLOCK pwbArray = NULL;
   PMPIOC_DEV_INFO pIocDev = NULL;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> WdsIp th: wrong IRQL\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hWdsIpThreadHandle);
      pAdapter->hWdsIpThreadHandle = NULL;
      KeSetEvent(&pAdapter->WdsIpThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
   }

   // allocate a wait block array for the multiple wait
   pwbArray = ExAllocatePool
              (
                 NonPagedPool,
                 (WDSIP_THREAD_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK)
              );
   if (pwbArray == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> WdsIp Mth: NO_MEM\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hWdsIpThreadHandle);
      pAdapter->hWdsIpThreadHandle = NULL;
      KeSetEvent(&pAdapter->WdsIpThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   ndisStatus = MPIOC_AddDevice
                (
                   &pIocDev,
                   MP_DEV_TYPE_WDS_IPV6,
                   pAdapter,
                   0,     // NdisDeviceHandle
                   NULL,  // ControlDeviceObject
                   NULL,  // LinkName (UNICODE)
                   NULL,  // DeviceName
                   NULL   // LinkName (ANSI)
                );
   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      ExFreePool(pwbArray);
      ZwClose(pAdapter->hWdsIpThreadHandle);
      pAdapter->hWdsIpThreadHandle = NULL;
      KeSetEvent(&pAdapter->WdsIpThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   ndisStatus = MPQCTL_GetClientId
                (
                   pAdapter,
                   QMUX_TYPE_WDS,
                   pIocDev  // Context
                );
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
      ("<%s> WdsIp: CID %u (0x%x)\n", pAdapter->PortName, pIocDev->ClientId, ndisStatus)
   );
   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      ExFreePool(pwbArray);
      ZwClose(pAdapter->hWdsIpThreadHandle);
      pAdapter->hWdsIpThreadHandle = NULL;
      KeSetEvent(&pAdapter->WdsIpThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }
   pAdapter->WdsIpClientContext = pIocDev;
   pAdapter->WdsIpClientId = pIocDev->ClientId;

   // Do the bind
   MPQMUX_SetQMUXBindMuxDataPort( pAdapter, pIocDev, QMUX_TYPE_WDS, QMIWDS_BIND_MUX_DATA_PORT_REQ);

   // set IPV6
   if ((QCMAIN_IsDualIPSupported(pAdapter) == TRUE) && (pAdapter->WdsIpClientContext != NULL))
   {
      MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WDS, 
                             QMIWDS_SET_CLIENT_IP_FAMILY_PREF_REQ, (CUSTOMQMUX)MPQMUX_SetIPv6ClientIPFamilyPref, TRUE );
   }

   // Register for extended IP config change
   MPQWDS_SendIndicationRegisterReq(pAdapter, pIocDev, TRUE);

   #ifdef QCUSB_MUX_PROTOCOL
   #ifdef QC_DUAL_IP_FC
   MPQWDS_SendEventReportReq(pAdapter, pIocDev, TRUE);
   #endif // QC_DUAL_IP_FC
   #endif // QCUSB_MUX_PROTOCOL   

   // 3/13/2018: follwing could cause potential race condition
   // 3/13/2018: remove following, they are initialized in MPINI_AllocAdapter
   // KeClearEvent(&pAdapter->WdsIpThreadCancelEvent);
   // KeClearEvent(&pAdapter->WdsIpThreadClosedEvent);

   pAdapter->WdsIpThreadEvent[WDSIP_CANCEL_EVENT_INDEX] = &pAdapter->WdsIpThreadCancelEvent;
   pAdapter->WdsIpThreadEvent[WDSIP_READ_EVENT_INDEX] = &pIocDev->ReadDataAvailableEvent;

   // KeSetPriorityThread(KeGetCurrentThread(), 27);

   while (bKeepRunning == TRUE)
   {
      if (bThreadStarted == FALSE)
      {
         bThreadStarted = TRUE;
         KeSetEvent(&pAdapter->WdsIpThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      }

      // self-check the RX queue
      NdisAcquireSpinLock(&pIocDev->IoLock);
      if (!IsListEmpty(&pIocDev->ReadDataQueue))
      {
         ntStatus = WDSIP_READ_EVENT_INDEX;
         NdisReleaseSpinLock(&pIocDev->IoLock);
      }
      else
      {
         NdisReleaseSpinLock(&pIocDev->IoLock);

         ntStatus = KeWaitForMultipleObjects
                    (
                       WDSIP_THREAD_EVENT_COUNT,
                       (VOID **)&pAdapter->WdsIpThreadEvent,
                       WaitAny,
                       Executive,
                       KernelMode,
                       FALSE,             // non-alertable
                       NULL,
                       pwbArray
                    );
      }

      switch (ntStatus)
      {
         case WDSIP_CANCEL_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> WDSIP th: CANCEL\n", pAdapter->PortName)
            );

            KeClearEvent(&pAdapter->WdsIpThreadCancelEvent);
            bKeepRunning = FALSE;
            break;
         }

         case WDSIP_READ_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> WDSIP th: READ\n", pAdapter->PortName)
            );
            KeClearEvent(&pIocDev->ReadDataAvailableEvent);

            // process WDS messages
            NdisAcquireSpinLock(&pIocDev->IoLock);
            if (!IsListEmpty(&pIocDev->ReadDataQueue))
            {
               PLIST_ENTRY headOfList;
               PMPIOC_READ_ITEM rdItem;

               headOfList = RemoveHeadList(&pIocDev->ReadDataQueue);
               rdItem = CONTAINING_RECORD(headOfList, MPIOC_READ_ITEM, List);
               NdisReleaseSpinLock(&pIocDev->IoLock);

               MPIP_ProcessInboundMessage(pIocDev, rdItem->Buffer, rdItem->Length, NULL);
               ExFreePool(rdItem->Buffer);
               ExFreePool(rdItem);
            }
            else
            {
               NdisReleaseSpinLock(&pIocDev->IoLock);
            }
            break;
         }

         default:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_ERROR,
               ("<%s> WDSIP th: unexpected evt-0x%x\n", pAdapter->PortName, ntStatus)
            );
            break;
         }
      }
   }  // while

   pAdapter->WdsIpClientContext = NULL;
   if (pwbArray != NULL)
   {
      ExFreePool(pwbArray);
   }

   // Again, try to cleanup the Rx pending queue

   MPQCTL_ReleaseClientId
   (
      pAdapter,
      pIocDev->QMIType,
      (PVOID)pIocDev   // Context
   );
   pAdapter->WdsIpClientId = 0;

   if (pIocDev != NULL)
   {
      MPIOC_DeleteDevice
      (
         pAdapter,
         pIocDev,
         pAdapter->USBDo  // trick: this arg will never be matched so it forces
                          // the function to match the second argument and remove
                          // the desired instance.
      );
   }
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_FORCE,
      ("<%s> WDSIP: OUT (0x%x)\n", pAdapter->PortName, ntStatus)
   );

   KeSetEvent(&pAdapter->WdsIpThreadClosedEvent, IO_NO_INCREMENT, FALSE);

   PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}  // MPIP_WdsIpThread

/* NOTE: no need to do any processing here since the processing is taken care in the MPQMUX.c */
VOID MPIP_ProcessInboundMessage
(
   PMPIOC_DEV_INFO IocDev,
   PVOID           Message,
   ULONG           Length,
   PMP_OID_WRITE   pMatchOID
)
{
   PMP_ADAPTER pAdapter = IocDev->Adapter;
   PQCQMUX    qmux = (PQCQMUX)Message;
   PQMUX_MSG  qmux_msg = (PQMUX_MSG)&qmux->Message;
   UCHAR      msgType;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_TRACE,
      ("<%s> -->MPIP_ProcessInboundMessage: Flags 0x%x TID 0x%x Type 0x%x Len %d\n\n",
         pAdapter->PortName, qmux->CtlFlags, qmux->TransactionId,
         qmux_msg->QMUXMsgHdr.Type, qmux_msg->QMUXMsgHdr.Length)
   );

   msgType = qmux->CtlFlags & QMUX_CTL_FLAG_MASK_TYPE;

   switch (msgType)
   {
      case QMUX_CTL_FLAG_TYPE_RSP:
      {
         switch (qmux_msg->QMUXMsgHdr.Type)
         {
            #ifdef QCUSB_MUX_PROTOCOL
            #ifdef QC_DUAL_IP_FC
            case QMIWDS_SET_EVENT_REPORT_RESP:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL,
                  MP_DBG_LEVEL_DETAIL,
                  ("<%s> MPIP: SET_EVENT_REPORT_RESP\n", pAdapter->PortName)
               );

               if (qmux_msg->EventReportRsp.QMUXResult != QMI_RESULT_SUCCESS)
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL,
                     MP_DBG_LEVEL_ERROR,
                     ("<%s> MPIP: EventReportRsp result 0x%x err 0x%x\n", pAdapter->PortName,
                       qmux_msg->EventReportRsp.QMUXResult,
                       qmux_msg->EventReportRsp.QMUXError)
                  );
               }
               break;
            }
            #endif // QC_DUAL_IP_FC
            #endif // QCUSB_MUX_PROTOCOL

            case QMIWDS_INDICATION_REGISTER_RESP:
            {
               MPQMUX_ProcessIndicationRegisterResp(pAdapter, qmux_msg, IocDev);
               break;
            }
            
            case QMIWDS_START_NETWORK_INTERFACE_RESP:
            {
               break;
            }
            case QMIWDS_GET_PKT_SRVC_STATUS_RESP:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                  ("<%s> MPIP: WDS_GET_PKT_SRVC_STATUS_RESP\n", pAdapter->PortName)
               );
               //MPQMUX_ProcessWdsPktSrvcStatusResp(pAdapter, qmux_msg, NULL, IocDev);
               break;
            }
            case QMIWDS_SET_CLIENT_IP_FAMILY_PREF_RESP:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                  ("<%s> MPIP: WDS_SET_CLIENT_IP_FAMILY_PREF_RESP\n", pAdapter->PortName)
               );
               //MPQMUX_ProcessWdsSetClientIpFamilyPrefResp(pAdapter, qmux_msg, NULL, IocDev);
               break;
            }
            #ifdef QC_IP_MODE
            case QMIWDS_GET_RUNTIME_SETTINGS_RESP:
            {
               //MPQMUX_ProcessWdsGetRuntimeSettingResp(pAdapter, qmux_msg, NULL, IocDev);
               break;
            }
            #endif // QC_IP_MODE

            default:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                  ("<%s> MPIP: Unknown RSP type 0x%x\n", pAdapter->PortName, qmux_msg->QMUXMsgHdr.Type)
               );
               break;
            }
         }

         break;
      }

      case QMUX_CTL_FLAG_TYPE_IND:
      {
         switch (qmux_msg->QMUXMsgHdr.Type)
         {
#ifdef QCUSB_MUX_PROTOCOL
#ifdef QC_DUAL_IP_FC
             case QMIWDS_EVENT_REPORT_IND:
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                   ("<%s> MPIP: WDS_EVENT_REPORT_IND\n", pAdapter->PortName)
                );
                //MPQMUX_ProcessWdsEventReportInd(pAdapter, IocDev, qmux_msg);
                break;
             }
#endif // QC_DUAL_IP_FC
#endif // QCUSB_MUX_PROTOCOL
        
            case QMIWDS_GET_PKT_SRVC_STATUS_IND:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                  ("<%s> MPIP: WDS_GET_PKT_SRVC_STATUS_IND\n", pAdapter->PortName)
               );
               //ProcessWdsPktSrvcStatusInd(IocDev, qmux_msg);
               break;
            }
            default:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                  ("<%s> MPIP: Unknown IND type 0x%x\n", pAdapter->PortName, qmux_msg->QMUXMsgHdr.Type)
               );
               break;
            }
         }
         break;
      }

      case QMUX_CTL_FLAG_TYPE_CMD:
      default:
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<%s> MPIP: Unknown message flag 0x%x\n", pAdapter->PortName, qmux->CtlFlags)
         );
         break;
      }
   }
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_TRACE,
      ("<%s> <--MPIP_ProcessInboundMessage\n", pAdapter->PortName)
   );
}  // MPIP_ProcessInboundMessage

VOID ProcessWdsPktSrvcStatusInd(PMPIOC_DEV_INFO IocDevice, PQMUX_MSG Message)
{
   PQMIWDS_GET_PKT_SRVC_STATUS_IND_MSG ind = &(Message->PacketServiceStatusInd);
   PMP_ADAPTER returnAdapter;
   PMP_ADAPTER   pAdapter = IocDevice->Adapter;
   PQCQMUX_TLV   tlv;
   LONG          len = 0;
   BOOLEAN       bIPV6 = FALSE;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> MPIP: WDS_PKT_SRVC_STATUS_IND -  %u bytes ConnStat 0x%x Reconf 0x%x\n", pAdapter->PortName,
        ind->Length, ind->ConnectionStatus, ind->ReconfigRequired)
   );

   if (ind->Length == 0)
   {
      return;
   }

   // try to init QMI if it's not initialized
   KeSetEvent(&pAdapter->MPThreadInitQmiEvent, IO_NO_INCREMENT, FALSE);

   // Check to see if we have an armed timer, if yes we need to
   // discard the action because new indication has arrived.
   MPIP_CancelReconfigTimer(pAdapter);

   // get to the IP family TLV
   tlv = MPQMUX_GetTLV(pAdapter, Message, 0, &len); // point to the first TLV

   while (tlv != NULL)
   {
      if (tlv->Type == 0x12)  // IP family
      {
         PQMIWDS_IP_FAMILY_TLV pIpFamily = (PQMIWDS_IP_FAMILY_TLV)tlv;

         if (pIpFamily->IpFamily == 0x06)
         {
            bIPV6 = TRUE;
         }
         break;
      }
      tlv = MPQMUX_GetNextTLV(pAdapter, tlv, &len);
   }

   if (bIPV6 == FALSE)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPIP: WDS_PKT_SRVC_STATUS_IND -  unexpected IP family 0x%x\n", pAdapter->PortName, bIPV6)
      );
      return;
   }

   if (ind->ConnectionStatus == QWDS_PKT_DATA_CONNECTED)
   {
      if (ind->ReconfigRequired == 0)
      {
         KeSetEvent(&pAdapter->MPThreadMediaConnectEvent, IO_NO_INCREMENT, FALSE);
      }
      else
      {
         NTSTATUS nts;

         MPMAIN_MediaDisconnect(pAdapter, TRUE);

         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPIP: WDS_PKT_SRVC_STATUS_IND: to arm ReconfigTimer\n", pAdapter->PortName)
         );
         nts = IoAcquireRemoveLock(pAdapter->pMPRmLock, NULL);
         if (!NT_SUCCESS(nts))
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
               ("<%s> MPIP: WDS_PKT_SRVC_STATUS_IND: ReconfigTimer rm lock err\n", pAdapter->PortName)
            );
         }
         else
         {
            MPMAIN_ResetPacketCount(pAdapter);
            if ( TRUE == DisconnectedAllAdapters(pAdapter, &returnAdapter))
            {
                USBIF_PollDevice(returnAdapter->USBDo, FALSE);
                // USBIF_CancelWriteThread(returnAdapter->USBDo);
            }
            // set timer
            NdisAcquireSpinLock(&pAdapter->QMICTLLock);
            QcStatsIncrement(pAdapter, MP_CNT_TIMER, 12);
            pAdapter->ReconfigTimerState = RECONF_TIMER_ARMED;
            KeClearEvent(&pAdapter->ReconfigCompletionEvent);
            NdisSetTimer(&pAdapter->ReconfigTimer, pAdapter->ReconfigDelay);
            NdisReleaseSpinLock(&pAdapter->QMICTLLock);
         }
      }
   }
   else if (ind->ConnectionStatus != QWDS_PKT_DATA_AUTHENTICATING)
   {
#ifdef NDIS620_MINIPORT
                 {
                   // Send Context disconnect indication
                   NDIS_WWAN_CONTEXT_STATE ContextState;
      
                   pAdapter->DeviceContextState = DeviceWWanContextDetached;
                   NdisZeroMemory( &ContextState, sizeof(NDIS_WWAN_CONTEXT_STATE) );
                   ContextState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
                   ContextState.Header.Revision = NDIS_WWAN_CONTEXT_STATE_REVISION_1;
                   ContextState.Header.Size = sizeof(NDIS_WWAN_CONTEXT_STATE);
                   ContextState.ContextState.VoiceCallState = WwanVoiceCallStateNone;
                   ContextState.ContextState.ConnectionId = pAdapter->WWanServiceConnectHandle;
                   ContextState.ContextState.ActivationState = WwanActivationStateDeactivated;
      
                   MyNdisMIndicateStatus
                   (
                     pAdapter->AdapterHandle,
                     NDIS_STATUS_WWAN_CONTEXT_STATE,
                     (PVOID)&ContextState,
                     sizeof(NDIS_WWAN_CONTEXT_STATE)
                   );
                   
                   if (pAdapter->DeregisterIndication == 1)
                   {
                     if (pAdapter->IsNASSysInfoPresent == FALSE)
                     {
                     MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_NAS, 
                                       QMINAS_GET_SERVING_SYSTEM_REQ, NULL, TRUE );
                     }
                     else
                     {
                         MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_NAS, 
                                        QMINAS_GET_SYS_INFO_REQ, NULL, TRUE );
                     }
                   }
                 }
#endif
      MPMAIN_MediaDisconnect(pAdapter, TRUE);
      MPMAIN_ResetPacketCount(pAdapter);
      if ( TRUE == DisconnectedAllAdapters(pAdapter, &returnAdapter))
      {
         USBIF_PollDevice(returnAdapter->USBDo, FALSE);
         // USBIF_CancelWriteThread(returnAdapter->USBDo);
      }
      // Query IPV4 status
      MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WDS, 
                             QMIWDS_GET_PKT_SRVC_STATUS_REQ, NULL, TRUE );
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPIP: ProcessWdsPktSrvcStatusInd: ConnStat: 0x%x\n", pAdapter->PortName, pAdapter->ulMediaConnectStatus)
   );

}  // ProcessWdsPktSrvcStatusInd

VOID MPIP_CancelReconfigTimer(PMP_ADAPTER pAdapter)
{
   BOOLEAN       bCancelled;
   LARGE_INTEGER timeoutValue;

   NdisAcquireSpinLock(&pAdapter->QMICTLLock);
   if (pAdapter->ReconfigTimerState == RECONF_TIMER_ARMED)
   {

      NdisCancelTimer(&pAdapter->ReconfigTimer, &bCancelled);
      NdisReleaseSpinLock(&pAdapter->QMICTLLock);

      if (bCancelled == FALSE)
      {
         NTSTATUS nts;

         timeoutValue.QuadPart = -((LONG)pAdapter->ReconfigDelay * 10 * 1000);
         nts = KeWaitForSingleObject
               (
                  &pAdapter->ReconfigCompletionEvent,
                  Executive, KernelMode, FALSE, &timeoutValue
               );
         KeClearEvent(&pAdapter->ReconfigCompletionEvent);
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPIP_CancelReconfigTimer: ReconfigTimer stopped 0x%x\n", pAdapter->PortName, nts)
         );
      }
      else
      {
         // reconfig timer cancelled, release remove lock
         QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 11);
      }
      pAdapter->ReconfigTimerState = RECONF_TIMER_IDLE;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPIP_CancelReconfigTimer: ReconfigTimer cancelled\n", pAdapter->PortName)
      );
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPIP_CancelReconfigTimer: ReconfigTimer idling\n", pAdapter->PortName)
      );
      NdisReleaseSpinLock(&pAdapter->QMICTLLock);
   }

   return;
}  // MPIP_CancelReconfigTimer
