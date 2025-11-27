/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             M P Q O S C . C

GENERAL DESCRIPTION
  This module contains QMI QOS Client functions.
  requests.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdio.h>
#include "MPMain.h"
#include "MPUsb.h"
#include "MPQCTL.h"
#include "MPIOC.h"
#include "MPQMUX.h"
#include "MPQOS.h"
#include "MPQOSC.h"

#ifdef EVENT_TRACING
#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc
#include "MPQOSC.tmh"
#endif  // EVENT_TRACING

NTSTATUS MPQOSC_StartQmiQosClient(PMP_ADAPTER pAdapter)
{
   NTSTATUS          ntStatus;
   OBJECT_ATTRIBUTES objAttr;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> StartQmiQosClient - wrong IRQL::%d\n", pAdapter->PortName, KeGetCurrentIrql())
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((pAdapter->pQmiQosThread != NULL) || (pAdapter->hQmiQosThreadHandle != NULL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QmiQosClient up/resumed\n", pAdapter->PortName)
      );
      // MPQOSC_ResumeThread(pDevExt, 0);
      return STATUS_SUCCESS;
   }

   KeClearEvent(&pAdapter->QmiQosThreadStartedEvent);
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   ntStatus = PsCreateSystemThread
              (
                 OUT &pAdapter->hQmiQosThreadHandle,
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)MPQOSC_QmiQosThread,
                 IN (PVOID)pAdapter
              );
   if ((!NT_SUCCESS(ntStatus)) || (pAdapter->hQmiQosThreadHandle == NULL))
   {
      pAdapter->hQmiQosThreadHandle = NULL;
      pAdapter->pQmiQosThread = NULL;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QMIQOS th failure\n", pAdapter->PortName)
      );
      return ntStatus;
   }
   ntStatus = KeWaitForSingleObject
              (
                 &pAdapter->QmiQosThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );

   ntStatus = ObReferenceObjectByHandle
              (
                 pAdapter->hQmiQosThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&pAdapter->pQmiQosThread,
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QMIQOS: ObReferenceObjectByHandle failed 0x%x\n", pAdapter->PortName, ntStatus)
      );
      pAdapter->pQmiQosThread = NULL;
   }
   else
   {
      if (STATUS_SUCCESS != (ntStatus = ZwClose(pAdapter->hQmiQosThreadHandle)))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> QMIQOS ZwClose failed - 0x%x\n", pAdapter->PortName, ntStatus)
         );
      }
      else
      {
         pAdapter->hQmiQosThreadHandle = NULL;
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> QMIQOS handle=0x%p thOb=0x%p\n", pAdapter->PortName,
       pAdapter->hQmiQosThreadHandle, pAdapter->pQmiQosThread)
   );

   return ntStatus;

}  // MPQOSC_StartQmiQosClient

NTSTATUS MPQOSC_CancelQmiQosClient(PMP_ADAPTER pAdapter)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER delayValue;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QMIQOS Cxl\n", pAdapter->PortName)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QMIQOS Cxl: wrong IRQL\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (InterlockedIncrement(&pAdapter->QosClientInCancellation) > 1)
   {
      while ((pAdapter->hQmiQosThreadHandle != NULL) || (pAdapter->pQmiQosThread != NULL))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMIQOS cxl in pro\n", pAdapter->PortName)
         );
         MPMAIN_Wait(-(3 * 1000 * 1000));
      }
      InterlockedDecrement(&pAdapter->QosClientInCancellation);
      return STATUS_SUCCESS;
   }

   if ((pAdapter->hQmiQosThreadHandle == NULL) && (pAdapter->pQmiQosThread == NULL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QOSC already cancelled\n", pAdapter->PortName)
      );
      InterlockedDecrement(&pAdapter->QosClientInCancellation);
      return STATUS_SUCCESS;
   }

   if ((pAdapter->hQmiQosThreadHandle != NULL) || (pAdapter->pQmiQosThread != NULL))
   {
      KeClearEvent(&pAdapter->QmiQosThreadClosedEvent);
      KeSetEvent
      (
         &pAdapter->QmiQosThreadCancelEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pAdapter->pQmiQosThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pAdapter->pQmiQosThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pAdapter->pQmiQosThread);
         KeClearEvent(&pAdapter->QmiQosThreadClosedEvent);
         ZwClose(pAdapter->hQmiQosThreadHandle);
         pAdapter->pQmiQosThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pAdapter->QmiQosThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pAdapter->QmiQosThreadClosedEvent);
         ZwClose(pAdapter->hQmiQosThreadHandle);
      }
   }
   InterlockedDecrement(&pAdapter->QosClientInCancellation);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--QMIQOS Cxl\n", pAdapter->PortName)
   );
   return ntStatus;
}  // MPQOSC_CancelQmiQosClient

VOID MPQOSC_QmiQosThread(PVOID Context)
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
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QMIQOS Mth: wrong IRQL\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hQmiQosThreadHandle);
      pAdapter->hQmiQosThreadHandle = NULL;
      KeSetEvent(&pAdapter->QmiQosThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
   }

   // allocate a wait block array for the multiple wait
   pwbArray = ExAllocatePool
              (
                 NonPagedPool,
                 (QMIQOS_THREAD_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK)
              );
   if (pwbArray == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOSC Mth: NO_MEM\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hQmiQosThreadHandle);
      pAdapter->hQmiQosThreadHandle = NULL;
      KeSetEvent(&pAdapter->QmiQosThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   ndisStatus = MPIOC_AddDevice
                (
                   &pIocDev,
                   MP_DEV_TYPE_INTERNAL_QOS,
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
      ZwClose(pAdapter->hQmiQosThreadHandle);
      pAdapter->hQmiQosThreadHandle = NULL;
      KeSetEvent(&pAdapter->QmiQosThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   ndisStatus = MPQCTL_GetClientId
                (
                   pAdapter,
                   QMUX_TYPE_QOS,
                   pIocDev  // Context
                );
   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS, MP_DBG_LEVEL_ERROR,
      ("<%s> QOSC: CID %u (0x%x) for IPV6\n", pAdapter->PortName, pIocDev->ClientId, ndisStatus)
   );
   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      ExFreePool(pwbArray);
      ZwClose(pAdapter->hQmiQosThreadHandle);
      pAdapter->hQmiQosThreadHandle = NULL;
      KeSetEvent(&pAdapter->QmiQosThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   // Call QMI_WDS_BIND_MUX_DATA_PORT
   MPQMUX_SetQMUXBindMuxDataPort( pAdapter, pIocDev, QMUX_TYPE_QOS, QMI_QOS_BIND_DATA_PORT_REQ);

   // START_DUAL_IP_SUPPORT

   // Set IP v6 preference
   pIocDev->ClientIdV6 = pIocDev->ClientId;
   MPQOSC_ComposeQosSetClientIpPrefReq(pAdapter, pIocDev, 6);

   // Set up QOS event report
   MPQOSC_SendQosSetEventReportReq(pAdapter, pIocDev);
#ifdef QCUSB_MUX_PROTOCOL
#ifdef QMI_OVER_DATA
   MPQOSC_SendQosEnableFlowDisableAckReq(pAdapter, pIocDev, FALSE);
#endif
#endif

   // Now, get client ID for V4
   pIocDev->ClientId = 0;
   ndisStatus = MPQCTL_GetClientId
                (
                   pAdapter,
                   QMUX_TYPE_QOS,
                   pIocDev  // Context
                );
   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS, MP_DBG_LEVEL_ERROR,
      ("<%s> QOSC: CID %u (0x%x) for IPV4\n", pAdapter->PortName, pIocDev->ClientId, ndisStatus)
   ); 

   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      // rollback to single IP support, at this ppoint, ClientId bound to IPV6
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS, MP_DBG_LEVEL_ERROR,
         ("<%s> QOSC: failed to get client for IPV4\n", pAdapter->PortName)
      ); 

      pIocDev->ClientId = pIocDev->ClientIdV6;
      pIocDev->ClientIdV6 = 0;
   }
   else
   {
      // Call QMI_WDS_BIND_MUX_DATA_PORT again for V4
      MPQMUX_SetQMUXBindMuxDataPort( pAdapter, pIocDev, QMUX_TYPE_QOS, QMI_QOS_BIND_DATA_PORT_REQ);

      // Set IP preference for V4
      MPQOSC_ComposeQosSetClientIpPrefReq(pAdapter, pIocDev, 4);

      // Set up QOS event report
      MPQOSC_SendQosSetEventReportReq(pAdapter, pIocDev);
#ifdef QCUSB_MUX_PROTOCOL
#ifdef QMI_OVER_DATA
      MPQOSC_SendQosEnableFlowDisableAckReq(pAdapter, pIocDev, FALSE);
#endif
#endif

   }
   // END_DUAL_IP_SUPPORT

   KeClearEvent(&pAdapter->QmiQosThreadCancelEvent);
   KeClearEvent(&pAdapter->QmiQosThreadClosedEvent);

   pAdapter->QmiQosThreadEvent[QMIQOS_CANCEL_EVENT_INDEX] = &pAdapter->QmiQosThreadCancelEvent;
   pAdapter->QmiQosThreadEvent[QMIQOS_READ_EVENT_INDEX] = &pIocDev->ReadDataAvailableEvent;

   KeSetPriorityThread(KeGetCurrentThread(), 27);

   while (bKeepRunning == TRUE)
   {
      if (bThreadStarted == FALSE)
      {
         bThreadStarted = TRUE;
         KeSetEvent(&pAdapter->QmiQosThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      }

      // self-check the RX queue
      NdisAcquireSpinLock(&pIocDev->IoLock);
      if (!IsListEmpty(&pIocDev->ReadDataQueue))
      {
         ntStatus = QMIQOS_READ_EVENT_INDEX;
         NdisReleaseSpinLock(&pIocDev->IoLock);
      }
      else
      {
         NdisReleaseSpinLock(&pIocDev->IoLock);

         ntStatus = KeWaitForMultipleObjects
                    (
                       QMIQOS_THREAD_EVENT_COUNT,
                       (VOID **)&pAdapter->QmiQosThreadEvent,
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
         case QMIQOS_CANCEL_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> QOSC th: CANCEL\n", pAdapter->PortName)
            );

            KeClearEvent(&pAdapter->QmiQosThreadCancelEvent);
            bKeepRunning = FALSE;
            break;
         }

         case QMIQOS_READ_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> QOSC th: READ\n", pAdapter->PortName)
            );
            KeClearEvent(&pIocDev->ReadDataAvailableEvent);

            // process QMI QOS messages
            NdisAcquireSpinLock(&pIocDev->IoLock);
            if (!IsListEmpty(&pIocDev->ReadDataQueue))
            {
               PLIST_ENTRY headOfList;
               PMPIOC_READ_ITEM rdItem;

               headOfList = RemoveHeadList(&pIocDev->ReadDataQueue);
               rdItem = CONTAINING_RECORD(headOfList, MPIOC_READ_ITEM, List);
               NdisReleaseSpinLock(&pIocDev->IoLock);

               MPQOSC_ProcessInboundMessage(pIocDev, rdItem->Buffer, rdItem->Length);
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
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_ERROR,
               ("<%s> QOSC th: unexpected evt-0x%x\n", pAdapter->PortName, ntStatus)
            );
            break;
         }
      }
   }  // while

   if (pwbArray != NULL)
   {
      ExFreePool(pwbArray);
   }

   // Again, try to cleanup the Rx pending queue
   // ...

   MPQCTL_ReleaseClientId
   (
      pAdapter,
      pIocDev->QMIType,
      (PVOID)pIocDev   // Context
   );

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
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_FORCE,
      ("<%s> QOSC: OUT (0x%x)\n", pAdapter->PortName, ntStatus)
   );

   KeSetEvent(&pAdapter->QmiQosThreadClosedEvent, IO_NO_INCREMENT, FALSE);

   PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}  // MPQOSC_QmiQosThread

VOID MPQOSC_SendQosSetEventReportReq
(
   PMP_ADAPTER     pAdapter,
   PMPIOC_DEV_INFO pIocDev
)
{
   UCHAR     qmux[128];
   PQCQMUX   qmuxPtr;
   PQMUX_MSG qmux_msg;
   UCHAR     qmi[512];
   ULONG     qmiLength = sizeof(QCQMI_HDR)+QCQMUX_HDR_SIZE+sizeof(QMI_QOS_SET_EVENT_REPORT_REQ_MSG);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS, MP_DBG_LEVEL_TRACE,
      ("<%s> -->SendQosSetEventReportReq CID %u\n", pAdapter->PortName, pIocDev->ClientId)
   );
   if (pIocDev->ClientId == 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS, MP_DBG_LEVEL_ERROR,
         ("<%s> <--SendQosSetEventReportReq: err - Invalid CID\n", pAdapter->PortName)
      );
      return;
   }

   // PackQosSetEventReportReq(qmux, &length);
   qmuxPtr = (PQCQMUX)qmux;
   qmuxPtr->CtlFlags = QMUX_CTL_FLAG_SINGLE_MSG | QMUX_CTL_FLAG_TYPE_CMD;
   qmuxPtr->TransactionId = GetQMUXTransactionId(pAdapter);
   qmux_msg = (PQMUX_MSG)&(qmuxPtr->Message);

   qmux_msg->QosSetEventReportReq.Type = QMI_QOS_SET_EVENT_REPORT_REQ;
   // qmux_msg->QosSetEventReportReq.TLVType = 0x01;
   // qmux_msg->QosSetEventReportReq.TLVLength = 1;
   // qmux_msg->QosSetEventReportReq.PhyLinkStatusRpt = 1;  // disable
   qmux_msg->QosSetEventReportReq.TLVType2 = 0x10;
   qmux_msg->QosSetEventReportReq.TLVLength2 = 1;
   qmux_msg->QosSetEventReportReq.GlobalFlowRpt = 1;  // enable

   // calculate total msg length
   qmux_msg->QosSetEventReportReq.Length = (sizeof(QMI_QOS_SET_EVENT_REPORT_REQ_MSG)
                                       - QMUX_MSG_OVERHEAD_BYTES); // TLV length

   MPQMI_QMUXtoQMI
   (
      pAdapter,
      QMUX_TYPE_QOS,
      pIocDev->ClientId,
      (PVOID)&qmux,
      (QCQMUX_HDR_SIZE+sizeof(QMI_QOS_SET_EVENT_REPORT_REQ_MSG)),
      (PVOID)qmi
   );

   // send QMI to device
   MP_USBSendControl(pAdapter, (PVOID)qmi, qmiLength);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS, MP_DBG_LEVEL_TRACE,
      ("<%s> <--SendQosSetEventReportReq CID %u\n", pAdapter->PortName, pIocDev->ClientId)
   );
   return;
} // MPQOSC_SendQosSetEventReportReq

VOID MPQOSC_ProcessInboundMessage
(
   PMPIOC_DEV_INFO IocDev,
   PVOID           Message,
   ULONG           Length
)
{
   PMP_ADAPTER pAdapter = IocDev->Adapter;
   PQCQMUX    qmux = (PQCQMUX)Message;
   PQMUX_MSG  qmux_msg = (PQMUX_MSG)&qmux->Message;
   UCHAR      msgType;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS, MP_DBG_LEVEL_TRACE,
      ("<%s> -->ProcessInboundMessage: Flags 0x%x TID 0x%x Type 0x%x Len %d\n\n",
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
            case QMI_QOS_SET_EVENT_REPORT_RESP:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS, MP_DBG_LEVEL_DETAIL,
                  ("<%s> QOS_SET_EVENT_REPORT_RESP\n", pAdapter->PortName)
               );
               ProcessSetQosEventReportRsp(IocDev, qmux_msg);
               break;
            }

            case QMI_QOS_SET_CLIENT_IP_PREF_RESP:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS, MP_DBG_LEVEL_DETAIL,
                  ("<%s> QOS_SET_CLIENT_IP_PREF_RESP\n", pAdapter->PortName)
               );
               MPQOS_ProcessQosSetClientIpPrefResp(pAdapter, qmux_msg, IocDev, qmux->TransactionId);
               break;
            }

            default:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS, MP_DBG_LEVEL_ERROR,
                  ("<%s> Unknown RSP type 0x%x\n", pAdapter->PortName, qmux_msg->QMUXMsgHdr.Type)
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
            case QMI_QOS_EVENT_REPORT_IND:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS, MP_DBG_LEVEL_DETAIL,
                  ("<%s> QOS_EVENT_REPORT_IND\n", pAdapter->PortName)
               );
               ProcessQosEventReportInd(IocDev, qmux_msg);
               break;
            }
            default:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS, MP_DBG_LEVEL_ERROR,
                  ("<%s> Unknown IND type 0x%x\n", pAdapter->PortName, qmux_msg->QMUXMsgHdr.Type)
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
            MP_DBG_MASK_QOS, MP_DBG_LEVEL_ERROR,
            ("<%s> Unknown message flag 0x%x\n", pAdapter->PortName, qmux->CtlFlags)
         );
         break;
      }
   }
   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS, MP_DBG_LEVEL_TRACE,
      ("<%s> <--ProcessInboundMessage\n", pAdapter->PortName)
   );
}  // MPQOSC_ProcessInboundMessage

VOID ProcessSetQosEventReportRsp(PMPIOC_DEV_INFO IocDevice, PQMUX_MSG Message)
{
   PMP_ADAPTER pAdapter = IocDevice->Adapter;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS, MP_DBG_LEVEL_TRACE,
      ("<%s> QMI_QOS_SET_EVENT_REPORT_RESP: result 0x%x Error 0x%x\n",
         pAdapter->PortName, Message->QosSetEventReportRsp.QMUXResult,
         Message->QosSetEventReportRsp.QMUXError)
   );
}  // ProcessSetQosEventReportRsp

VOID ProcessQosEventReportInd(PMPIOC_DEV_INFO IocDevice, PQMUX_MSG Message)
{
   PQMI_QOS_EVENT_REPORT_IND_MSG ind = &(Message->QosEventReportInd);
   PQMI_TLV_HDR  tlv;
   PCHAR         position, endPosition;
   PMP_ADAPTER   pAdapter = IocDevice->Adapter;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> QOS_EVENT_REPORT_IND -  %u bytes\n", pAdapter->PortName, ind->Length)
   );

   if (ind->Length == 0)
   {
      return;
   }

   position = (PCHAR)&(ind->TLVs);
   endPosition = position + ind->Length;
   tlv = (PQMI_TLV_HDR)&(ind->TLVs);

   while ((position != NULL) && (position < endPosition))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_TRACE,
         ("<%s> IND tlv; 0x%x (%u)\n", pAdapter->PortName, tlv->TLVType, tlv->TLVLength)
      );

      switch (tlv->TLVType)
      {
         case QOS_EVENT_RPT_IND_TLV_GLOBAL_FL_RPT_STATE:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> QOS_IND: TLV_GLOBAL_FL_RPT_STATE (%uB)\n",
                 pAdapter->PortName, tlv->TLVLength)
            );
            position += sizeof(QMI_TLV_HDR); // point to TLV value part
            ProcessQosFlowReport(IocDevice, position, tlv->TLVLength);

            if (tlv->TLVLength == 0)
            {
               position = NULL;
            }
            else
            {
               position += tlv->TLVLength;
            }
            break;
         }

         default:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> QOS_IND: TLV_UNKNOWN 0x%x, skip\n", pAdapter->PortName, tlv->TLVType)
            );
            position += sizeof(QMI_TLV_HDR); // point to TLV value part
            if (tlv->TLVLength == 0)
            {
               position = NULL;
            }
            else
            {
               position += tlv->TLVLength;
            }

            break;
         }
      }
   }

}  // ProcessQosEventReportInd

VOID ProcessQosFlowReport
(
   PMPIOC_DEV_INFO IocDevice,
   PCHAR           FlowReport,
   USHORT          ReportLength
)
{
   PMPQOS_FLOW_ENTRY qmiFlow = NULL;
   PQMI_FILTER   qmiFilter = NULL;
   PQMI_TLV_HDR  tlv;
   PCHAR         position, endPosition;
   int           action = -1, newFlow = -1;
   USHORT        profileId;
   LIST_ENTRY    qmiFilterList;
   PMP_ADAPTER   pAdapter = IocDevice->Adapter;

   qmiFlow = (PMPQOS_FLOW_ENTRY)ExAllocatePool(NonPagedPool, sizeof(MPQOS_FLOW_ENTRY));
   if (qmiFlow == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QOS_EVENT_REPORT_IND - Error - no MEM\n", pAdapter->PortName)
      );
      return;
   }

   InitializeListHead(&qmiFilterList);
   RtlZeroMemory(qmiFlow, sizeof(MPQOS_FLOW_ENTRY));
   InitializeListHead(&(qmiFlow->FilterList));
   InitializeListHead(&(qmiFlow->Packets));

   position = FlowReport;
   endPosition = position + ReportLength;
   tlv = (PQMI_TLV_HDR)FlowReport;

   // DbgPrint("IND position 0x%p tlv: 0x%x (%u)\n", position, tlv->TLVType, tlv->TLVLength);

   while ((position != NULL) && (position < endPosition))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_TRACE,
         ("<%s> IND tlv; 0x%x (%u)\n", pAdapter->PortName, tlv->TLVType, tlv->TLVLength)
      );

      switch (tlv->TLVType)
      {
         case QOS_EVENT_RPT_IND_TLV_PHY_LINK_STATE_TYPE:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> TLV_PHY_LINK_STATE\n", pAdapter->PortName)
            );
            position += sizeof(QOS_EVENT_RPT_IND_TLV_PHY_LINK_STATE);
            break;
         }

         case QOS_EVENT_RPT_IND_TLV_GLOBAL_FL_RPT_TYPE:
         {
            PQOS_EVENT_RPT_IND_TLV_GLOBAL_FL_RPT globalFlowRpt;

            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> TLV_GLOBAL_FL_RPT_TYPE\n", pAdapter->PortName)
            );
            globalFlowRpt = (PQOS_EVENT_RPT_IND_TLV_GLOBAL_FL_RPT)tlv;
            qmiFlow->FlowId = globalFlowRpt->QosId;
            action  = globalFlowRpt->StateChange;
            newFlow = globalFlowRpt->NewFlow;
            position += sizeof(QOS_EVENT_RPT_IND_TLV_GLOBAL_FL_RPT);
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> TLV_GLOBAL_FL_RPT_TYPE: flowId 0x%x newFlow %u action %u\n",
                 pAdapter->PortName, qmiFlow->FlowId, newFlow, action)
            );
            break;
         }

         case QOS_EVENT_RPT_IND_TLV_TX_FLOW_TYPE:
         {
            PQMI_TLV_HDR flowTlv;
            int flowTlvLength;
            
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> TLV_TX_FLOW\n", pAdapter->PortName)
            );

            // skip 
            position += sizeof(QMI_TLV_HDR); // point to TLV value part
            if (tlv->TLVLength == 0)
            {
               position = NULL;
            }
            else
            {
               position += tlv->TLVLength;
            }

            /**********************************
            // process embedded TLVs
            position += sizeof(QMI_TLV_HDR); // point to TLV value part
            flowTlv = (PQMI_TLV_HDR)position;
            flowTlvLength = flowTlv->TLVLength;
            if (flowTlv->TLVType != QOS_EVENT_RPT_IND_TLV_FLOW_SPEC)
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> Unexpected TX flow TLV type 0x%x\n", pAdapter->PortName, flowTlv->TLVType)
               );
               position = NULL;
               break;
            }

            position += sizeof(QMI_TLV_HDR); // skip the flow_spec TLV header (type 0x14)
            position = ParseQmiQosFlow(IocDev, qmiFlow, position, flowTlv->TLVLength, TRUE);
            **********************************/
            break;
         }

         case QOS_EVENT_RPT_IND_TLV_RX_FLOW_TYPE:
         {
            PQMI_TLV_HDR flowTlv;
            
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> TLV_RX_FLOW\n", pAdapter->PortName)
            );

            // skip 
            position += sizeof(QMI_TLV_HDR); // point to TLV value part
            if (tlv->TLVLength == 0)
            {
               position = NULL;
            }
            else
            {
               position += tlv->TLVLength;
            }

            /***************************************
            // process embedded TLVs
            position += sizeof(QMI_TLV_HDR); // point to TLV value part
            flowTlv = (PQMI_TLV_HDR)position;

            // skip RX flow
            position += flowTlv->TLVLength;

            if (flowTlv->TLVType != QOS_EVENT_RPT_IND_TLV_FLOW_SPEC)
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> Unexpected RX flow TLV type 0x%x\n", pAdapter->PortName, flowTlv->TLVType)
               );
               position = NULL;
               break;
            }

            position += sizeof(QMI_TLV_HDR); // skip the flow_spec TLV header (type 0x14)
            position = ParseQmiQosFlow(Context, qmiFlow, position, flowTlv->TLVLength, FALSE);
            ****************************************/

            break;
         }

         case QOS_EVENT_RPT_IND_TLV_TX_FILTER_TYPE:
         {
            PQMI_TLV_HDR filterTlv;
            int filterTlvTotalLength = tlv->TLVLength;  // could include multiple filter TLVs
            int filterTlvLength;
            
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> TLV_TX_FILTER\n", pAdapter->PortName)
            );

            // process embedded TLVs
            position += sizeof(QMI_TLV_HDR); // point to TLV value part

            while (filterTlvTotalLength > 0)
            {
               filterTlv = (PQMI_TLV_HDR)position;
               filterTlvLength = filterTlv->TLVLength;
               if (filterTlv->TLVType != QOS_EVENT_RPT_IND_TLV_FILTER_SPEC)
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_ERROR,
                     ("<%s> Unexpected filter TLV type 0x%x\n", pAdapter->PortName, filterTlv->TLVType)
                  );
                  position = NULL;
                  break;
               }

               // alloc filter TLV
               qmiFilter = (PQMI_FILTER)ExAllocatePool(NonPagedPool, sizeof(QMI_FILTER));
               if (qmiFilter == NULL)
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_ERROR,
                     ("<%s> QOS_EVENT_REPORT_IND: Error - no MEM -2\n", pAdapter->PortName)
                  );
                  position = NULL;
                  break;
               }
               RtlZeroMemory(qmiFilter, sizeof(QMI_FILTER));
               qmiFilter->Precedence = QOS_FILTER_PRECEDENCE_INVALID;
               InitializeListHead(&(qmiFilter->PacketInfoList));

               position += sizeof(QMI_TLV_HDR); // skip the filter_spec TLV header (type 0x15)
               filterTlvTotalLength -= sizeof(QMI_TLV_HDR);
               position = ParseQmiQosFilter(IocDevice, qmiFilter, position, filterTlv->TLVLength, TRUE);
               if (position != NULL)
               {
                  filterTlvTotalLength -= filterTlv->TLVLength;  // total of filter-fields (TLVs)
                  InsertTailList(&qmiFilterList, &qmiFilter->List);
               }
            }  // while

            // sanity checking
            if (filterTlvTotalLength < 0)
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> Error: filter TLV parse failure: %d - 0x%p\n", pAdapter->PortName, filterTlvTotalLength, position)
               );
               position = NULL;
            }
            break;
         }

         case QOS_EVENT_RPT_IND_TLV_RX_FILTER_TYPE:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> TLV_RX_FILTER\n", pAdapter->PortName)
            );

            // skip RX filters
            position += sizeof(QMI_TLV_HDR); // point to TLV value part
            if (tlv->TLVLength == 0)
            {
               position = NULL;
            }
            else
            {
               position += tlv->TLVLength;
            }

            break;
         }

         default:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> TLV_UNKNOWN 0x%x, skip\n", pAdapter->PortName, tlv->TLVType)
            );
            position += sizeof(QMI_TLV_HDR); // point to TLV value part
            if (tlv->TLVLength == 0)
            {
               position = NULL;
            }
            else
            {
               position += tlv->TLVLength;
            }
            break;
         }
      }

      if (position != NULL)
      {
         tlv = (PQMI_TLV_HDR)position;
      }
   }  // while

   switch (action)
   {
      case QOS_EVENT_RPT_IND_FLOW_ACTIVATED:
      case QOS_EVENT_RPT_IND_FLOW_SUSPENDED:
      {
         PMPQOS_FLOW_ENTRY flow;

         DbgPrint("<%u> Activated/Suspended flow, newFlow=%u action=%u\n", IocDevice->ClientId, newFlow, action);

         NdisAcquireSpinLock(&pAdapter->TxLock);
         flow = FindQosFlow
                (
                   pAdapter,
                   FLOW_SEND_WITH_DATA,
                   FLOW_QUEUED,
                   qmiFlow->FlowId
                );
         if (flow == NULL)
         {
            // add new flow to FLOW_SEND_WITHOUT_DATA queue
            qmiFlow->FlowState = FLOW_SEND_WITHOUT_DATA;
            InsertTailList
            (
               &(pAdapter->FlowDataQueue[FLOW_SEND_WITHOUT_DATA]), 
               &qmiFlow->List
            );
            DbgPrint("<%u> add filter successfully\n", IocDevice->ClientId);

            // Add filter list
            AddQosFilter(pAdapter, qmiFlow, &qmiFilterList);
         }
         else
         {
            // error
            DbgPrint("<%u> Error: add flow failure\n", IocDevice->ClientId);
            if (qmiFlow != NULL)
            {
               ExFreePool(qmiFlow);
               MPQOSC_PurgeFilterList(pAdapter, &qmiFilterList);
            }
         }
         NdisReleaseSpinLock(&pAdapter->TxLock);

         break;
      }
      case QOS_EVENT_RPT_IND_FLOW_MODIFIED:
      {
         PMPQOS_FLOW_ENTRY flow;

         DbgPrint("<%u> Modify flow\n", IocDevice->ClientId);

         NdisAcquireSpinLock(&pAdapter->TxLock);
         flow = FindQosFlow
                (
                   pAdapter,
                   FLOW_SEND_WITH_DATA,
                   FLOW_QUEUED,
                   qmiFlow->FlowId
                );
         if (flow == NULL)
         {
            DbgPrint("<%u> flow modification failure\n", IocDevice->ClientId);

            MPQOSC_PurgeFilterList(pAdapter, &qmiFilterList);
         }
         else
         {
            MPQOSC_PurgeFilterList(pAdapter, &flow->FilterList);

            // add modified flow's filter
            AddQosFilter(pAdapter, flow, &qmiFilterList);

            // TODO: do we need to copy modified flow contents?
         }

         ExFreePool(qmiFlow);
         qmiFlow = NULL;
         NdisReleaseSpinLock(&pAdapter->TxLock);         

         break;
      }
      /***** SUSPENDED is treated same way as ACTIVATED on host *****
      case QOS_EVENT_RPT_IND_FLOW_SUSPENDED:
      {
         DbgPrint("<%u> suspend flow, ignored\n", IocDevice->ClientId);
         if (qmiFlow != NULL)
         {
            ExFreePool(qmiFlow);
         }
         MPQOSC_PurgeFilterList(pAdapter, &qmiFilterList);
         break;
      }
      *****/
      case QOS_EVENT_RPT_IND_FLOW_DELETED:
      {
         PMPQOS_FLOW_ENTRY flow;

         DbgPrint("<%u> Delete flow\n", IocDevice->ClientId);

         NdisAcquireSpinLock(&pAdapter->TxLock);
         flow = FindQosFlow
                (
                   pAdapter,
                   FLOW_SEND_WITH_DATA,
                   FLOW_QUEUED,
                   qmiFlow->FlowId
                );
         if (flow != NULL)
         {
            RemoveEntryList(&flow->List);
            MPQOSC_PurgeFilterList(pAdapter, &flow->FilterList);

            // move flow/packets to default flow table
            flow->FlowState = FLOW_DEFAULT;
            InsertTailList
            (
               &(pAdapter->FlowDataQueue[FLOW_DEFAULT]), 
               &flow->List
            );
            if (!IsListEmpty(&flow->Packets))
            {
               KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT,FALSE);
            }
         }
         else
         {
            // error
            DbgPrint("<%u> Error: delete flow failure\n", IocDevice->ClientId);
         }
         if (qmiFlow != NULL)
         {
            ExFreePool(qmiFlow);
            MPQOSC_PurgeFilterList(pAdapter, &qmiFilterList);
         }
         NdisReleaseSpinLock(&pAdapter->TxLock);

         break;
      }

      case QOS_EVENT_RPT_IND_FLOW_ENABLED:
      {
         PMPQOS_FLOW_ENTRY flow;

         DbgPrint("<%u> Enable flow\n", IocDevice->ClientId);

         NdisAcquireSpinLock(&pAdapter->TxLock);
         if (qmiFlow->FlowId == 0)
         {
            pAdapter->DefaultFlowEnabled = TRUE;

            // signal QOS dispatch thread
            KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT, FALSE);
            DbgPrint("<%u> Default flow enabled\n", IocDevice->ClientId);
         }
         else
         {
            flow = FindQosFlow
                   (
                      pAdapter,
                      FLOW_QUEUED,
                      FLOW_QUEUED,
                      qmiFlow->FlowId
                   );
            if (flow != NULL)
            {
               RemoveEntryList(&flow->List);
               if (IsListEmpty(&flow->Packets))
               {
                  flow->FlowState = FLOW_SEND_WITHOUT_DATA;
                  InsertTailList
                  (
                     &(pAdapter->FlowDataQueue[FLOW_SEND_WITHOUT_DATA]), 
                     &flow->List
                  );
               }
               else
               {
                  flow->FlowState = FLOW_SEND_WITH_DATA;
                  InsertTailList
                  (
                     &(pAdapter->FlowDataQueue[FLOW_SEND_WITH_DATA]), 
                     &flow->List
                  );

                  // signal QOS dispatch thread
                  KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT, FALSE);
               }
               DbgPrint("<%u> flow 0x%x enabled\n", IocDevice->ClientId, flow->FlowId);
            }
            else
            {
               // error
               DbgPrint("<%u> Error: enable flow failure\n", IocDevice->ClientId);
            }
         }
         if (qmiFlow != NULL)
         {
            ExFreePool(qmiFlow);
            MPQOSC_PurgeFilterList(pAdapter, &qmiFilterList);
         }
         NdisReleaseSpinLock(&pAdapter->TxLock);
         break;
      }

      case QOS_EVENT_RPT_IND_FLOW_DISABLED:
      {
         PMPQOS_FLOW_ENTRY flow;
         BOOLEAN bDisabled = FALSE;

         DbgPrint("<%u> Disable flow\n", IocDevice->ClientId);

         NdisAcquireSpinLock(&pAdapter->TxLock);
         if (qmiFlow->FlowId == 0)
         {
            pAdapter->DefaultFlowEnabled = FALSE;
            DbgPrint("<%u> Default flow disabled\n", IocDevice->ClientId);
            bDisabled = TRUE;
         }
         else
         {
            flow = FindQosFlow
                   (
                      pAdapter,
                      FLOW_SEND_WITH_DATA,
                      FLOW_SEND_WITHOUT_DATA,
                      qmiFlow->FlowId
                   );
            if (flow != NULL)
            {
               flow->FlowState = FLOW_QUEUED;
               RemoveEntryList(&flow->List);
               InsertTailList
               (
                  &(pAdapter->FlowDataQueue[FLOW_QUEUED]), 
                  &flow->List
               );
               DbgPrint("<%u> flow disabled 0x%x\n", IocDevice->ClientId, flow->FlowId);
               bDisabled = TRUE;
            }
            else
            {
               // error
               DbgPrint("<%u> Error: disable flow failure\n", IocDevice->ClientId);
            }
         }
         if (qmiFlow != NULL)
         {
            ExFreePool(qmiFlow);
            MPQOSC_PurgeFilterList(pAdapter, &qmiFilterList);
         }
         NdisReleaseSpinLock(&pAdapter->TxLock);

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QMI_OVER_DATA
         if (bDisabled == TRUE)
         {
            MPQOSC_SendQosEnableFlowDisableAckReq(pAdapter, IocDevice, TRUE);
         }
#endif
#endif

         break;
      }

      default:
      {
         DbgPrint("<%u> Unknown action 0x%x, ignored\n", IocDevice->ClientId, action);
         if (qmiFlow != NULL)
         {
            ExFreePool(qmiFlow);
         }
         MPQOSC_PurgeFilterList(pAdapter, &qmiFilterList);
         break;
      }
   }
}  // ProcessQosFlowReport

VOID MPQOSC_PurgeFilterList(PMP_ADAPTER pAdapter, PLIST_ENTRY FilterList)
{
   PQMI_FILTER   qmiFilter;
   PLIST_ENTRY   headOfList;
   int           numFilters = 0;

   RemoveQosFilterFromPrecedenceList(pAdapter, FilterList);

   while (!IsListEmpty(FilterList))
   {
      headOfList = RemoveHeadList(FilterList);
      qmiFilter = CONTAINING_RECORD
                  (
                     headOfList,
                     QMI_FILTER,
                     List
                  );
      MPQOS_PurgePktInfoList(&qmiFilter->PacketInfoList);

      ExFreePool(qmiFilter);
      ++numFilters;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPQOSC_PurgeFilterList - %d filters\n", pAdapter->PortName, numFilters)
   );
}  // MPQOSC_PurgeFilterList

PCHAR ParseQmiQosFilter
(
   PMPIOC_DEV_INFO IocDevice,
   PQMI_FILTER     QmiFilter,
   PVOID           TLVs,
   USHORT          TLVLength,
   BOOLEAN         IsTx
)
{
   PQMI_TLV_HDR   tlv;
   PCHAR          position;
   int            remainingLength = TLVLength;
   PMP_ADAPTER    pAdapter = IocDevice->Adapter;


   tlv = (PQMI_TLV_HDR)TLVs;
   position = (PCHAR)tlv;

   DbgPrint("Parsing %s QOS filter\n", ((IsTx == TRUE)? "TX" : "RX"));

   while (remainingLength > 0)
   {
      switch (tlv->TLVType)
      {
         case QOS_FILTER_TLV_IP_FILTER_IDX_TYPE:
         {
            PQOS_FILTER_TLV_IP_FILTER_IDX ipIdx = (PQOS_FILTER_TLV_IP_FILTER_IDX)tlv;

            QmiFilter->Index = ipIdx->IpFilterIndex;

            DbgPrint("QOS_FILTER_TLV_IP_FILTER_IDX 0x%x\n", QmiFilter->Index);

            position += sizeof(QOS_FILTER_TLV_IP_FILTER_IDX);
            remainingLength -= sizeof(QOS_FILTER_TLV_IP_FILTER_IDX);
            break;
         }
         case QOS_FILTER_TLV_IP_VERSION_TYPE:
         {
            PQOS_FILTER_TLV_IP_VERSION ipVer = (PQOS_FILTER_TLV_IP_VERSION)tlv;

            QmiFilter->IpVersion = ipVer->IpVersion; // TODO: IPV4/IPV6

            DbgPrint("QOS_FILTER_TLV_IP_VERSION = %u\n", QmiFilter->IpVersion);

            position += sizeof(QOS_FILTER_TLV_IP_VERSION);
            remainingLength -= sizeof(QOS_FILTER_TLV_IP_VERSION);
            break;
         }
         case QOS_FILTER_TLV_IPV4_SRC_ADDR_TYPE:
         {
            PQOS_FILTER_TLV_IPV4_SRC_ADDR ipv4Addr = (PQOS_FILTER_TLV_IPV4_SRC_ADDR)tlv;

            QmiFilter->Ipv4SrcAddr = ipv4Addr->IpSrcAddr;
            QmiFilter->Ipv4SrcAddrSubnetMask = ipv4Addr->IpSrcSubnetMask;

            DbgPrint("QOS_FILTER_TLV_IPV4_SRC_ADDR src_addr 0x%x subnet_mask 0x%x\n",
                      QmiFilter->Ipv4SrcAddr, QmiFilter->Ipv4SrcAddrSubnetMask);

            position += sizeof(QOS_FILTER_TLV_IPV4_SRC_ADDR);
            remainingLength -= sizeof(QOS_FILTER_TLV_IPV4_SRC_ADDR);
            break;
         }
         case QOS_FILTER_TLV_IPV4_DEST_ADDR_TYPE:
         {
            PQOS_FILTER_TLV_IPV4_DEST_ADDR ipv4Addr = (PQOS_FILTER_TLV_IPV4_DEST_ADDR)tlv;

            QmiFilter->Ipv4DestAddr = ipv4Addr->IpDestAddr;
            QmiFilter->Ipv4DestAddrSubnetMask = ipv4Addr->IpDestSubnetMask;

            DbgPrint("QOS_FILTER_TLV_IPV4_DEST_ADDR: dest_addr 0x%x subnet_addr 0x%x\n",
                      QmiFilter->Ipv4DestAddr, QmiFilter->Ipv4DestAddrSubnetMask);

            position += sizeof(QOS_FILTER_TLV_IPV4_DEST_ADDR);
            remainingLength -= sizeof(QOS_FILTER_TLV_IPV4_DEST_ADDR);
            break;
         }
         case QOS_FILTER_TLV_NEXT_HDR_PROTOCOL_TYPE:
         {
            PQOS_FILTER_TLV_NEXT_HDR_PROTOCOL proto = 
               (PQOS_FILTER_TLV_NEXT_HDR_PROTOCOL)tlv;

            QmiFilter->Ipv4NextHdrProtocol = proto->NextHdrProtocol;
            QmiFilter->Ipv6NextHdrProtocol = proto->NextHdrProtocol;
            DbgPrint("QOS_FILTER_TLV_NEXT_HDR_PROTOCOL 0x%x\n", QmiFilter->Ipv4NextHdrProtocol);

            position += sizeof(QOS_FILTER_TLV_NEXT_HDR_PROTOCOL);
            remainingLength -= sizeof(QOS_FILTER_TLV_NEXT_HDR_PROTOCOL);
            break;
         }
         case QOS_FILTER_TLV_IPV4_TYPE_OF_SERVICE_TYPE:
         {
            PQOS_FILTER_TLV_IPV4_TYPE_OF_SERVICE svc = 
               (PQOS_FILTER_TLV_IPV4_TYPE_OF_SERVICE)tlv;

            QmiFilter->Ipv4TypeOfService =  svc->Ipv4TypeOfService;
            QmiFilter->Ipv4TypeOfServiceMask = svc->Ipv4TypeOfServiceMask;
 
            DbgPrint("QOS_FILTER_TLV_IPV4_TYPE_OF_SERVICE: 0x%x, mask 0x%x\n",
                      QmiFilter->Ipv4TypeOfService, QmiFilter->Ipv4TypeOfServiceMask);

            position += sizeof(QOS_FILTER_TLV_IPV4_TYPE_OF_SERVICE);
            remainingLength -= sizeof(QOS_FILTER_TLV_IPV4_TYPE_OF_SERVICE);
            break;
         }
         case QOS_FILTER_TLV_TCP_UDP_PORT_SRC_TCP_TYPE:
         {
            PQOS_FILTER_TLV_TCP_UDP_PORT port = (PQOS_FILTER_TLV_TCP_UDP_PORT)tlv;

            QmiFilter->TcpSrcPortSpecified = TRUE;
            QmiFilter->TcpSrcPort      = port->FilterPort;
            QmiFilter->TcpSrcPortRange = port->FilterPortRange;

            DbgPrint("QOS_FILTER_TLV_PORT_SRC_TCP: port %u, range %u\n",
                      QmiFilter->TcpSrcPort, QmiFilter->TcpSrcPortRange);

            position += sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            remainingLength -= sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            break;
         }
         case QOS_FILTER_TLV_TCP_UDP_PORT_DEST_TCP_TYPE:
         {
            PQOS_FILTER_TLV_TCP_UDP_PORT port = (PQOS_FILTER_TLV_TCP_UDP_PORT)tlv;

            QmiFilter->TcpDestPortSpecified = TRUE;
            QmiFilter->TcpDestPort      = port->FilterPort;
            QmiFilter->TcpDestPortRange = port->FilterPortRange;

            DbgPrint("QOS_FILTER_TLV_PORT_DEST_TCP: port %u, range %u\n",
                      QmiFilter->TcpDestPort, QmiFilter->TcpDestPortRange);

            position += sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            remainingLength -= sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            break;
         }
         case QOS_FILTER_TLV_TCP_UDP_PORT_SRC_UDP_TYPE:
         {
            PQOS_FILTER_TLV_TCP_UDP_PORT port = (PQOS_FILTER_TLV_TCP_UDP_PORT)tlv;

            QmiFilter->UdpSrcPortSpecified = TRUE;
            QmiFilter->UdpSrcPort = port->FilterPort;
            QmiFilter->UdpSrcPortRange = port->FilterPortRange;

            DbgPrint("QOS_FILTER_TLV_PORT_SRC_UDP: port %u, range %u\n",
                      QmiFilter->UdpSrcPort, QmiFilter->UdpSrcPortRange);

            position += sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            remainingLength -= sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            break;
         }
         case QOS_FILTER_TLV_TCP_UDP_PORT_DEST_UDP_TYPE:
         {
            PQOS_FILTER_TLV_TCP_UDP_PORT port = (PQOS_FILTER_TLV_TCP_UDP_PORT)tlv;

            QmiFilter->UdpDestPortSpecified = TRUE;
            QmiFilter->UdpDestPort = port->FilterPort;
            QmiFilter->UdpDestPortRange = port->FilterPortRange;

            DbgPrint("QOS_FILTER_TLV_PORT_DEST_UDP: port %u, range %u\n",
                      QmiFilter->UdpDestPort, QmiFilter->UdpDestPortRange);

            position += sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            remainingLength -= sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            break;
         }
         case QOS_FILTER_TLV_TCP_UDP_PORT_SRC_TYPE:
         {
            PQOS_FILTER_TLV_TCP_UDP_PORT port = (PQOS_FILTER_TLV_TCP_UDP_PORT)tlv;

            QmiFilter->TcpUdpSrcPortSpecified = TRUE;
            QmiFilter->TcpUdpSrcPort = port->FilterPort;
            QmiFilter->TcpUdpSrcPortRange = port->FilterPortRange;

            DbgPrint("QOS_FILTER_TLV_PORT_SRC: port %u, range %u\n",
                      QmiFilter->TcpUdpSrcPort, QmiFilter->TcpUdpSrcPortRange);

            position += sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            remainingLength -= sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            break;
         }
         case QOS_FILTER_TLV_TCP_UDP_PORT_DEST_TYPE:
         {
            PQOS_FILTER_TLV_TCP_UDP_PORT port = (PQOS_FILTER_TLV_TCP_UDP_PORT)tlv;

            QmiFilter->TcpUdpDestPortSpecified = TRUE;
            QmiFilter->TcpUdpDestPort = port->FilterPort;
            QmiFilter->TcpUdpDestPortRange = port->FilterPortRange;

            DbgPrint("QOS_FILTER_TLV_PORT_DEST: port %u, range %u\n",
                      QmiFilter->TcpUdpDestPort, QmiFilter->TcpUdpDestPortRange);

            position += sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            remainingLength -= sizeof(QOS_FILTER_TLV_TCP_UDP_PORT);
            break;
         }
         case QOS_FILTER_TLV_ICMP_FILTER_MSG_TYPE_TYPE:
         {
            PQOS_FILTER_TLV_ICMP_FILTER_MSG_TYPE icmp =
               (PQOS_FILTER_TLV_ICMP_FILTER_MSG_TYPE)tlv;

            QmiFilter->IcmpFilterMsgTypeSpecified = TRUE;
            QmiFilter->IcmpFilterMsgType = icmp->IcmpFilterMsgType;

            DbgPrint("QOS_FILTER_TLV_ICMP_FILTER_MSG_TYPE: 0x%x\n", QmiFilter->IcmpFilterMsgType);

            position += sizeof(QOS_FILTER_TLV_ICMP_FILTER_MSG_TYPE);
            remainingLength -= sizeof(QOS_FILTER_TLV_ICMP_FILTER_MSG_TYPE);
            break;
         }
         case QOS_FILTER_TLV_ICMP_FILTER_MSG_CODE_TYPE:
         {
            PQOS_FILTER_TLV_ICMP_FILTER_MSG_CODE icmp =
               (PQOS_FILTER_TLV_ICMP_FILTER_MSG_CODE)tlv;

            QmiFilter->IcmpFilterCodeFieldSpecified = TRUE;
            QmiFilter->IcmpFilterCodeField = icmp->IcmpFilterMsgCode;

            DbgPrint("QOS_FILTER_TLV_ICMP_FILTER_MSG_CODE: 0x%x\n", QmiFilter->IcmpFilterCodeField);

            position += sizeof(QOS_FILTER_TLV_ICMP_FILTER_MSG_CODE);
            remainingLength -= sizeof(QOS_FILTER_TLV_ICMP_FILTER_MSG_CODE);
            break;
         }
         case QOS_FILTER_TLV_PRECEDENCE_TYPE:
         {
            PQOS_FILTER_TLV_PRECEDENCE precedence =
               (PQOS_FILTER_TLV_PRECEDENCE)tlv;

            QmiFilter->Precedence = precedence->Precedence;
            DbgPrint("QOS_FILTER_TLV_PRECEDENCE: %d\n", QmiFilter->Precedence);

            position += sizeof(QOS_FILTER_TLV_PRECEDENCE);
            remainingLength -= sizeof(QOS_FILTER_TLV_PRECEDENCE);
            break;
         }
         case QOS_FILTER_TLV_ID_TYPE:
         {
            PQOS_FILTER_TLV_ID id = (PQOS_FILTER_TLV_ID)tlv;

            QmiFilter->FilterId = id->FilterId;
            DbgPrint("QOS_FILTER_TLV_ID: 0x%x\n", QmiFilter->FilterId);
            position += sizeof(QOS_FILTER_TLV_ID);
            remainingLength -= sizeof(QOS_FILTER_TLV_ID);
            break;
         }
   
         #ifdef QCQOS_IPV6

         case QOS_FILTER_TLV_IPV6_SRC_ADDR_TYPE:
         {
            PQOS_FILTER_TLV_IPV6_SRC_ADDR ipAddr =
               (PQOS_FILTER_TLV_IPV6_SRC_ADDR)tlv;

            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> [QOSV6]: ParseQmiQosFilter: IPV6_SRC_ADDR\n", pAdapter->PortName)
            );

            if (ipAddr->TLVLength > 16)
            {
               RtlCopyMemory
               (
                  QmiFilter->Ipv6SrcAddr,
                  ipAddr->IpSrcAddr,
                  16
               );
               QmiFilter->Ipv6SrcAddrPrefixLen = ipAddr->IpSrcAddrPrefixLen;
               if (QmiFilter->Ipv6SrcAddrPrefixLen <= 128)
               {
                  QCQOS_ConstructIPPrefixMask
                  (
                     pAdapter, 
                     QmiFilter->Ipv6SrcAddrPrefixLen,
                     &(QmiFilter->Ipv6SrcAddrMask)
                  );
               }
               else
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_ERROR,
                     ("<%s> [QOSV6]: ParseQmiQosFilter: invalid SrcAddrPrefixLen %d\n",
                       pAdapter->PortName, QmiFilter->Ipv6SrcAddrPrefixLen)
                  );
                  QmiFilter->Ipv6SrcAddrPrefixLen = 0;
               }
            }
            else
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> [QOSV6]: ParseQmiQosFilter: IPV6_SRC_ADDR too short %d\n",
                    pAdapter->PortName, ipAddr->TLVLength)
               );
            }
            position += sizeof(QOS_FILTER_TLV_IPV6_SRC_ADDR);
            remainingLength -= sizeof(QOS_FILTER_TLV_IPV6_SRC_ADDR);
            break;
         }

         case QOS_FILTER_TLV_IPV6_DEST_ADDR_TYPE:
         {
            PQOS_FILTER_TLV_IPV6_DEST_ADDR ipAddr =
               (PQOS_FILTER_TLV_IPV6_DEST_ADDR)tlv;

            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> [QOSV6]: ParseQmiQosFilter: IPV6_DEST_ADDR\n", pAdapter->PortName)
            );

            if (ipAddr->TLVLength > 16)
            {
               RtlCopyMemory
               (
                  QmiFilter->Ipv6DestAddr,
                  ipAddr->IpDestAddr,
                  16
               );
               QmiFilter->Ipv6DestAddrPrefixLen = ipAddr->IpDestAddrPrefixLen;
               if (QmiFilter->Ipv6DestAddrPrefixLen <= 128)
               {
                  QCQOS_ConstructIPPrefixMask
                  (
                     pAdapter,
                     QmiFilter->Ipv6DestAddrPrefixLen,
                     &(QmiFilter->Ipv6DestAddrMask)
                  );
               }
               else
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_ERROR,
                     ("<%s> [QOSV6]: ParseQmiQosFilter: invalid DestAddrPrefixLen %d\n",
                       pAdapter->PortName, QmiFilter->Ipv6DestAddrPrefixLen)
                  );
                  QmiFilter->Ipv6DestAddrPrefixLen = 0;
               }
            }
            else
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> [QOSV6]: ParseQmiQosFilter: IPV6_DEST_ADDR too short %d\n",
                    pAdapter->PortName, ipAddr->TLVLength)
               );
            }
            position += sizeof(QOS_FILTER_TLV_IPV6_DEST_ADDR);
            remainingLength -= sizeof(QOS_FILTER_TLV_IPV6_DEST_ADDR);
            break;
         }

         case QOS_FILTER_TLV_IPV6_TRAFFIC_CLASS_TYPE:
         {
            PQOS_FILTER_TLV_IPV6_TRAFFIC_CLASS trafficClass =
               (PQOS_FILTER_TLV_IPV6_TRAFFIC_CLASS)tlv;

            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> [QOSV6]: ParseQmiQosFilter: IPV6_TRAFFIC_CLASS\n", pAdapter->PortName)
            );

            QmiFilter->Ipv6TrafficClass = trafficClass->TrafficClass;
            QmiFilter->Ipv6TrafficClassMask = trafficClass->TrafficClassMask;

            position += sizeof(QOS_FILTER_TLV_IPV6_TRAFFIC_CLASS);
            remainingLength -= sizeof(QOS_FILTER_TLV_IPV6_TRAFFIC_CLASS);
            break;
         }

         case QOS_FILTER_TLV_IPV6_FLOW_LABEL_TYPE:
         {
            PQOS_FILTER_TLV_IPV6_FLOW_LABEL flowLabel =
               (PQOS_FILTER_TLV_IPV6_FLOW_LABEL)tlv;

            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> [QOSV6]: ParseQmiQosFilter: IPV6_FLOW_LABEL\n", pAdapter->PortName)
            );

            QmiFilter->Ipv6FlowLabel = flowLabel->FlowLabel;

            position += sizeof(QOS_FILTER_TLV_IPV6_FLOW_LABEL);
            remainingLength -= sizeof(QOS_FILTER_TLV_IPV6_FLOW_LABEL);
            break;
         }

         #endif // QCQOS_IPV6

         default:
         {
            DbgPrint("Error: unknown TLV type 0x%x, skip\n", tlv->TLVType);
            position = (PCHAR)tlv + sizeof(QMI_TLV_HDR) + tlv->TLVLength;
            remainingLength -= (sizeof(QMI_TLV_HDR) + tlv->TLVLength);
            break;
         }
      } // switch
      if ((remainingLength > 0) && (position != NULL))
      {
         tlv = (PQMI_TLV_HDR)position;
      }
   } // while

   return position;
}  // ParseQmiQosFilter

// This function must be called within TX spin lock
PMPQOS_FLOW_ENTRY FindQosFlow
(
   PMP_ADAPTER         pAdapter,
   QOS_FLOW_QUEUE_TYPE StartQueueType,
   QOS_FLOW_QUEUE_TYPE EndQueueType,
   ULONG               FlowId
)
{
   PLIST_ENTRY       headOfList, peekEntry;
   PMPQOS_FLOW_ENTRY flow, foundFlow = NULL;
   QOS_FLOW_QUEUE_TYPE i;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOSC: FindQosFlow - 0x%x\n", pAdapter->PortName, FlowId)
   );

   for (i = StartQueueType; ((i <= EndQueueType) && (foundFlow == NULL)); i++)
   {
      if (!IsListEmpty(&pAdapter->FlowDataQueue[i]))
      {
         headOfList = &pAdapter->FlowDataQueue[i];
         peekEntry = headOfList->Flink;

         while ((peekEntry != headOfList) && (foundFlow == NULL))
         {
            flow = CONTAINING_RECORD
                   (
                      peekEntry,
                      MPQOS_FLOW_ENTRY,
                      List
                   );
            if (flow->FlowId == FlowId)
            {
               foundFlow = flow;
               break;
            }
            peekEntry = peekEntry->Flink;
         }
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOSC: FindQosFlow - (flow 0x%p, ID 0x%x)\n",
        pAdapter->PortName, foundFlow, FlowId)
   );

   return foundFlow;

}  // FindQosFlow

// Must be called within TX spin lock
VOID AddQosFilter
(
   PMP_ADAPTER       pAdapter,
   PMPQOS_FLOW_ENTRY QosFlow,
   PLIST_ENTRY       QmiFilterList
)
{
   PLIST_ENTRY headOfList;
   PQMI_FILTER filter;
   int         numFilters = 0;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOSC: AddQosFilter to flow 0x%x\n", pAdapter->PortName, QosFlow->FlowId)
   );

   AddQosFilterToPrecedenceList(pAdapter, QmiFilterList);

   while (!IsListEmpty(QmiFilterList))
   {
      headOfList = RemoveHeadList(QmiFilterList);
      filter = CONTAINING_RECORD
               (
                  headOfList,
                  QMI_FILTER,
                  List
               );
      filter->Flow = QosFlow;
      InsertTailList(&QosFlow->FilterList, &filter->List);
      ++numFilters;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--QOSC: AddQosFilter to flow 0x%x (%d filters added)\n",
        pAdapter->PortName, QosFlow->FlowId, numFilters)
   );
}  // AddQosFilter

// -construct an ordered list based on precedence
// -must be called within spinlock
VOID AddQosFilterToPrecedenceList
(
   PMP_ADAPTER pAdapter,
   PLIST_ENTRY QmiFilterList
)
{
   PQMI_FILTER orderedFilter, newFilter;
   PLIST_ENTRY headOfList, peekEntry, orderedHead, orderedEntry, newEntry;
   int numFilters = 0;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->AddQosFilterToPrecedenceList\n", pAdapter->PortName)
   );

   if (IsListEmpty(QmiFilterList))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> <--AddQosFilterToPrecedenceList: no filter to add\n", pAdapter->PortName)
      );
      return;
   }

   headOfList = QmiFilterList;
   peekEntry = headOfList->Flink;
   while (peekEntry != headOfList)
   {
      // get a new filter
      newFilter = CONTAINING_RECORD(peekEntry, QMI_FILTER, List);

      // do not add filter with invalid precedence
      if (newFilter->Precedence >= QOS_FILTER_PRECEDENCE_INVALID)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_ERROR,
            ("<%s> AddQosFilterToPrecedenceList: skip filter %d with invalid precedence %d\n",
              pAdapter->PortName, newFilter->Index, newFilter->Precedence)
         );
         peekEntry = peekEntry->Flink;
         continue;
      }

      // if FilterPrecedenceList is empty, just add
      if (IsListEmpty(&pAdapter->FilterPrecedenceList))
      {
         InsertHeadList(&pAdapter->FilterPrecedenceList, &newFilter->PrecedenceEntry);
      }
      else
      {
         BOOLEAN bAdded;
         int     index;

         // go through the ordered list
         orderedHead  = &pAdapter->FilterPrecedenceList;
         orderedEntry = orderedHead->Flink;
         bAdded = FALSE;
         index  = 0;

         while (orderedEntry != orderedHead)
         {
            orderedFilter = CONTAINING_RECORD(orderedEntry, QMI_FILTER, PrecedenceEntry);
            if (newFilter->Precedence < orderedFilter->Precedence)
            {
               newEntry = &newFilter->PrecedenceEntry;

               // add in front of orderedFilter
               if (index == 0)
               {
                  InsertHeadList(&pAdapter->FilterPrecedenceList, &newFilter->PrecedenceEntry);
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_DETAIL,
                     ("<%s> AddQosFilterToPrecedenceList: added idx %d to head\n", pAdapter->PortName,
                            newFilter->Index)
                  );
               }
               else
               {
                  newEntry->Flink = orderedEntry;
                  orderedEntry->Blink->Flink = newEntry;
                  newEntry->Blink = orderedEntry->Blink;
                  orderedEntry->Blink = newEntry;

                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_DETAIL,
                     ("<%s> AddQosFilterToPrecedenceList: added idx %d(preced %d) before idx %d (preced %d)\n", pAdapter->PortName,
                            newFilter->Index, newFilter->Precedence, orderedFilter->Index, orderedFilter->Precedence)
                  );
               }
               bAdded = TRUE;
               break;
            }
            orderedEntry = orderedEntry->Flink;
            index++;
         }
         if (bAdded == FALSE)
         {
            // biggest precedence, add to tail
            InsertTailList(&pAdapter->FilterPrecedenceList, &newFilter->PrecedenceEntry);
         }
      }
      peekEntry = peekEntry->Flink;
      ++numFilters;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--AddQosFilterToPrecedenceList: added %d\n", pAdapter->PortName, numFilters)
   );
}  // AddQosFilterToPrecedenceList

// -delete filter(s) from the precedence-ordered list
// -must be called within spinlock
VOID RemoveQosFilterFromPrecedenceList
(
   PMP_ADAPTER pAdapter,
   PLIST_ENTRY QmiFilterList
)
{
   PMPQOS_FLOW_ENTRY orderedFlow, removalFilterFlow;
   PQMI_FILTER orderedFilter, removalFilter;
   PLIST_ENTRY headOfList, peekEntry, orderedHead, orderedEntry, removalEntry;
   int numFilters = 0;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->RemoveQosFilterFromPrecedenceList\n", pAdapter->PortName)
   );

   if (IsListEmpty(QmiFilterList))
   {
      return;
   }

   headOfList = QmiFilterList;
   peekEntry = headOfList->Flink;
   while (peekEntry != headOfList)
   {
      // get a new filter
      removalFilter = CONTAINING_RECORD(peekEntry, QMI_FILTER, List);
      // removalFilterFlow = (PMPQOS_FLOW_ENTRY)removalFilter->Flow;

      if (IsListEmpty(&pAdapter->FilterPrecedenceList))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_ERROR,
            ("<%s> <--RemoveQosFilterFromPrecedenceList: empty precedence list\n", pAdapter->PortName)
         );
         return;
      }
      else
      {
         // go through the ordered list
         orderedHead  = &pAdapter->FilterPrecedenceList;
         orderedEntry = orderedHead->Flink;

         while (orderedEntry != orderedHead)
         {
            orderedFilter = CONTAINING_RECORD(orderedEntry, QMI_FILTER, PrecedenceEntry);
            // orderedFlow   = (PMPQOS_FLOW_ENTRY)orderedFilter->Flow;

            // if ((orderedFlow->FlowId == removalFilterFlow->FlowId) &&
            //     (orderedFilter->Index == removalFilter->Index))
            if (orderedFilter == removalFilter)
            {
               RemoveEntryList(orderedEntry);
               ++numFilters;
            }
            orderedEntry = orderedEntry->Flink;
         }
      }
      peekEntry = peekEntry->Flink;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--RemoveQosFilterFromPrecedenceList: removed %d\n", pAdapter->PortName, numFilters)
   );

}  // RemoveQosFilterFromPrecedenceList

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QMI_OVER_DATA
VOID MPQOSC_SendQosEnableFlowDisableAckReq
(
   PMP_ADAPTER     pAdapter,
   PMPIOC_DEV_INFO pIocDev,
   BOOLEAN         Ack
)
{
   UCHAR     qmux[128];
   PQCQMUX   qmuxPtr;
   PQMUX_MSG qmux_msg;
   UCHAR     qmi[512];
   ULONG     qmiLength = sizeof(QCQMI_HDR)+QCQMUX_HDR_SIZE+sizeof(QMI_QOS_ENABLE_FLOW_DISABLE_ACK_MSG);
   UCHAR     tlvType;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS, MP_DBG_LEVEL_TRACE,
      ("<%s> -->SendQosEnableFlowDisableAckReq CID %u\n", pAdapter->PortName, pIocDev->ClientId)
   );
   if (pIocDev->ClientId == 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS, MP_DBG_LEVEL_ERROR,
         ("<%s> <--SendQosEnableFlowDisableAckReq: err - Invalid CID\n", pAdapter->PortName)
      );
      return;
   }
   if (Ack == TRUE)
   {
      tlvType = 0x11;
   }
   else
   {
      tlvType = 0x10;
   }

   qmuxPtr = (PQCQMUX)qmux;
   qmuxPtr->CtlFlags = QMUX_CTL_FLAG_SINGLE_MSG | QMUX_CTL_FLAG_TYPE_CMD;
   qmuxPtr->TransactionId = GetQMUXTransactionId(pAdapter);
   qmux_msg = (PQMUX_MSG)&(qmuxPtr->Message);

   qmux_msg->QosEnableFlowDisableAckReq.Type = QMI_QOS_ENABLE_FLOW_DISABLE_ACK;
   qmux_msg->QosEnableFlowDisableAckReq.TLVType = tlvType;
   qmux_msg->QosEnableFlowDisableAckReq.TLVLength = 1;
   qmux_msg->QosEnableFlowDisableAckReq.Parameter = 1;

   // calculate total msg length
   qmux_msg->QosEnableFlowDisableAckReq.Length = (sizeof(QMI_QOS_ENABLE_FLOW_DISABLE_ACK_MSG)
                                       - QMUX_MSG_OVERHEAD_BYTES);

   MPQMI_QMUXtoQMI
   (
      pAdapter,
      QMUX_TYPE_QOS,
      pIocDev->ClientId,
      (PVOID)&qmux,
      (QCQMUX_HDR_SIZE+sizeof(QMI_QOS_SET_EVENT_REPORT_REQ_MSG)),
      (PVOID)qmi
   );

   // send QMI to device
   MP_USBSendControl(pAdapter, (PVOID)qmi, qmiLength);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS, MP_DBG_LEVEL_TRACE,
      ("<%s> <--SendQosEnableFlowDisableAckReq CID %u\n", pAdapter->PortName, pIocDev->ClientId)
   );
   return;
} // MPQOSC_SendQosEnableFlowDisableAckReq
#endif // QCUSB_MUX_PROTOCOL
#endif // QMI_OVER_DATA

VOID MPQOSC_ComposeQosSetClientIpPrefReq
(
   PMP_ADAPTER     pAdapter,
   PMPIOC_DEV_INFO pIocDev,
   UCHAR           IPVersion
)
{
   UCHAR     qmux[128];
   PQCQMUX   qmuxPtr;
   PQMUX_MSG qmux_msg;
   UCHAR     qmi[512];
   ULONG     qmiLength = sizeof(QCQMI_HDR)+QCQMUX_HDR_SIZE+sizeof(QMI_QOS_SET_CLIENT_IP_PREF_REQ_MSG);
   UCHAR     clientId;

   if (QCMAIN_IsDualIPSupported(pAdapter) == FALSE)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> QOSC_ComposeQosSetClientIpPrefReq - no dual IP support\n", pAdapter->PortName)
      );
      return;
   }

   if (IPVersion == 4)
   {
      clientId = pIocDev->ClientId;
   }
   else if (IPVersion == 6)
   {
      clientId = pIocDev->ClientIdV6;
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> QOSC_ComposeQosSetClientIpPrefReq - invalid IP ver\n", pAdapter->PortName)
      );
      return;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOSC_ComposeQosSetClientIpPrefReq CID %u IPV%d\n", pAdapter->PortName, clientId, IPVersion)
   );

   qmuxPtr = (PQCQMUX)qmux;
   qmuxPtr->CtlFlags = QMUX_CTL_FLAG_SINGLE_MSG | QMUX_CTL_FLAG_TYPE_CMD;
   qmuxPtr->TransactionId = GetQMUXTransactionId(pAdapter);
   qmux_msg = (PQMUX_MSG)&(qmuxPtr->Message);

   qmux_msg->QosSetClientIpPrefReq.Type = QMI_QOS_SET_CLIENT_IP_PREF_REQ;
   qmux_msg->QosSetClientIpPrefReq.TLVType = 0x01;
   qmux_msg->QosSetClientIpPrefReq.TLVLength = 1;
   qmux_msg->QosSetClientIpPrefReq.IpPreference = IPVersion;

   // calculate total msg length
   qmux_msg->QosSetClientIpPrefReq.Length = (sizeof(QMI_QOS_SET_CLIENT_IP_PREF_REQ_MSG)
                                               - QMUX_MSG_OVERHEAD_BYTES); // TLV length

   MPQMI_QMUXtoQMI
   (
      pAdapter,
      QMUX_TYPE_QOS,
      clientId,
      (PVOID)&qmux,
      (QCQMUX_HDR_SIZE+sizeof(QMI_QOS_SET_CLIENT_IP_PREF_REQ_MSG)),
      (PVOID)qmi
   );

   // send QMI to device
   MP_USBSendControl(pAdapter, (PVOID)qmi, qmiLength);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_TRACE,
      ("<%s> <--QOSC_ComposeQosSetClientIpPrefReq CID %u\n", pAdapter->PortName, clientId)
   );
   return;
} // MPQOSC_ComposeQosSetClientIpPrefReq

VOID MPQOS_ProcessQosSetClientIpPrefResp
(
   PMP_ADAPTER     pAdapter,
   PQMUX_MSG       Message,
   PMPIOC_DEV_INFO pIocDev,
   USHORT          Tid
)
{
   if ((Message->QosSetClientIpPrefRsp.QMUXResult == QMI_RESULT_SUCCESS) ||
       (Message->QosSetClientIpPrefRsp.QMUXError == QMI_ERR_NO_EFFECT))
   {
      pIocDev->IpFamilySet = TRUE;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> QOS_SET_CLIENT_IP_PREF_RESP: result 0x%x Error 0x%x TID 0x%X\n",
        pAdapter->PortName, Message->QosSetClientIpPrefRsp.QMUXResult,
        Message->QosSetClientIpPrefRsp.QMUXError, Tid)
   );
}  // MPQOS_ProcessQosSetClientIpPrefResp
