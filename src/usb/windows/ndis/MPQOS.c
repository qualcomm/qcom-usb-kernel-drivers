/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             M P Q O S . C

GENERAL DESCRIPTION
  This module contains Miniport function for handling QOS.
  requests.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdio.h>
#include "MPMain.h"
#include "MPUsb.h"
#include "MPQMUX.h"
#include "MPQOS.h"
#include "MPQOSC.h"
#include "MPWork.h"

#ifdef EVENT_TRACING
#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc
#include "MPQOS.tmh"
#endif  // EVENT_TRACING

NTSTATUS MPQOS_StartQosThread(PMP_ADAPTER pAdapter)
{
   NTSTATUS          ntStatus;
   OBJECT_ATTRIBUTES objAttr;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> StartQosThread - wrong IRQL::%d\n", pAdapter->PortName, KeGetCurrentIrql())
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((pAdapter->pQosThread != NULL) || (pAdapter->hQosThreadHandle != NULL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QosThread up/resumed\n", pAdapter->PortName)
      );
      // MPQOS_ResumeThread(pDevExt, 0);
      return STATUS_SUCCESS;
   }

   KeClearEvent(&pAdapter->QosThreadStartedEvent);
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   ntStatus = PsCreateSystemThread
              (
                 OUT &pAdapter->hQosThreadHandle,
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)MPQOS_MainTxThread,
                 IN (PVOID)pAdapter
              );
   if ((!NT_SUCCESS(ntStatus)) || (pAdapter->hQosThreadHandle == NULL))
   {
      pAdapter->hQosThreadHandle = NULL;
      pAdapter->pQosThread = NULL;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS th failure\n", pAdapter->PortName)
      );
      return ntStatus;
   }
   ntStatus = KeWaitForSingleObject
              (
                 &pAdapter->QosThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );

   ntStatus = ObReferenceObjectByHandle
              (
                 pAdapter->hQosThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&pAdapter->pQosThread,
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS: ObReferenceObjectByHandle failed 0x%x\n", pAdapter->PortName, ntStatus)
      );
      pAdapter->pQosThread = NULL;
   }
   else
   {
      if (STATUS_SUCCESS != (ntStatus = ZwClose(pAdapter->hQosThreadHandle)))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> QOS ZwClose failed - 0x%x\n", pAdapter->PortName, ntStatus)
         );
      }
      else
      {
         pAdapter->hQosThreadHandle = NULL;
      }
      ntStatus = STATUS_SUCCESS;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> QOS handle=0x%p thOb=0x%p\n", pAdapter->PortName,
       pAdapter->hQosThreadHandle, pAdapter->pQosThread)
   );

   return ntStatus;

}  // MPQOS_StartQosThread

NTSTATUS MPQOS_CancelQosThread(PMP_ADAPTER pAdapter)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER delayValue;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->QOS Cxl\n", pAdapter->PortName)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS Cxl: wrong IRQL\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   MPQOS_CancelQosDispatchThread(pAdapter);

   if (InterlockedIncrement(&pAdapter->QosThreadInCancellation) > 1)
   {
      while ((pAdapter->hQosThreadHandle != NULL) || (pAdapter->pQosThread != NULL))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QOS cxl in pro\n", pAdapter->PortName)
         );
         MPMAIN_Wait(-(3 * 1000 * 1000));
      }
      InterlockedDecrement(&pAdapter->QosThreadInCancellation);
      return STATUS_SUCCESS;
   }

   if ((pAdapter->hQosThreadHandle == NULL) && (pAdapter->pQosThread == NULL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QOS already cancelled\n", pAdapter->PortName)
      );
      InterlockedDecrement(&pAdapter->QosThreadInCancellation);
      return STATUS_SUCCESS;
   }

   if ((pAdapter->hQosThreadHandle != NULL) || (pAdapter->pQosThread != NULL))
   {
      KeClearEvent(&pAdapter->QosThreadClosedEvent);
      KeSetEvent
      (
         &pAdapter->QosThreadCancelEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pAdapter->pQosThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pAdapter->pQosThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pAdapter->pQosThread);
         KeClearEvent(&pAdapter->QosThreadClosedEvent);
         ZwClose(pAdapter->hQosThreadHandle);
         pAdapter->pQosThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pAdapter->QosThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pAdapter->QosThreadClosedEvent);
         ZwClose(pAdapter->hQosThreadHandle);
      }
   }
   InterlockedDecrement(&pAdapter->QosThreadInCancellation);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--QOS Cxl\n", pAdapter->PortName)
   );

   return ntStatus;
}  // MPQOS_CancelQosThread

NTSTATUS MPQOS_StartQosDispatchThread(PMP_ADAPTER pAdapter)
{
   NTSTATUS          ntStatus;
   OBJECT_ATTRIBUTES objAttr;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> StartQosDispThread - wrong IRQL::%d\n", pAdapter->PortName, KeGetCurrentIrql())
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((pAdapter->pQosDispThread != NULL) || (pAdapter->hQosDispThreadHandle != NULL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QosDispThread up/resumed\n", pAdapter->PortName)
      );
      // MPQOS_ResumeThread(pDevExt, 0);
      return STATUS_SUCCESS;
   }

   KeClearEvent(&pAdapter->QosDispThreadStartedEvent);
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   ntStatus = PsCreateSystemThread
              (
                 OUT &pAdapter->hQosDispThreadHandle,
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)MPQOS_DispatchThread,
                 IN (PVOID)pAdapter
              );
   if ((!NT_SUCCESS(ntStatus)) || (pAdapter->hQosDispThreadHandle == NULL))
   {
      pAdapter->hQosDispThreadHandle = NULL;
      pAdapter->pQosDispThread = NULL;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS Dth failure\n", pAdapter->PortName)
      );
      return ntStatus;
   }
   ntStatus = KeWaitForSingleObject
              (
                 &pAdapter->QosDispThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );

   ntStatus = ObReferenceObjectByHandle
              (
                 pAdapter->hQosDispThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&pAdapter->pQosDispThread,
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS Dth: ObReferenceObjectByHandle failed 0x%x\n", pAdapter->PortName, ntStatus)
      );
      pAdapter->pQosDispThread = NULL;
   }
   else
   {
      if (STATUS_SUCCESS != (ntStatus = ZwClose(pAdapter->hQosDispThreadHandle)))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> QOS D ZwClose failed - 0x%x\n", pAdapter->PortName, ntStatus)
         );
      }
      else
      {
         pAdapter->hQosDispThreadHandle = NULL;
      }
      ntStatus = STATUS_SUCCESS;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> QOS D handle=0x%p thOb=0x%p\n", pAdapter->PortName,
       pAdapter->hQosDispThreadHandle, pAdapter->pQosDispThread)
   );

   return ntStatus;

}  // MPQOS_StartQosDispatchThread

NTSTATUS MPQOS_CancelQosDispatchThread(PMP_ADAPTER pAdapter)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER delayValue;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->QOS D Cxl\n", pAdapter->PortName)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS D Cxl: wrong IRQL\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (InterlockedIncrement(&pAdapter->QosDispatchThreadInCancellation) > 1)
   {
      while ((pAdapter->hQosDispThreadHandle != NULL) || (pAdapter->pQosDispThread != NULL))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QOS D cxl in pro\n", pAdapter->PortName)
         );
         MPMAIN_Wait(-(3 * 1000 * 1000));
      }
      InterlockedDecrement(&pAdapter->QosDispatchThreadInCancellation);
      return STATUS_SUCCESS;
   }

   if ((pAdapter->hQosDispThreadHandle == NULL) && (pAdapter->pQosDispThread == NULL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QosDispatch already cancelled\n", pAdapter->PortName)
      );
      InterlockedDecrement(&pAdapter->QosDispatchThreadInCancellation);
      return STATUS_SUCCESS;
   }

   if ((pAdapter->hQosDispThreadHandle != NULL) || (pAdapter->pQosDispThread != NULL))
   {
      KeClearEvent(&pAdapter->QosDispThreadClosedEvent);
      KeSetEvent
      (
         &pAdapter->QosDispThreadCancelEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pAdapter->pQosDispThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pAdapter->pQosDispThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pAdapter->pQosDispThread);
         KeClearEvent(&pAdapter->QosDispThreadClosedEvent);
         ZwClose(pAdapter->hQosDispThreadHandle);
         pAdapter->pQosDispThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pAdapter->QosDispThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pAdapter->QosDispThreadClosedEvent);
         ZwClose(pAdapter->hQosDispThreadHandle);
      }
   }
   InterlockedDecrement(&pAdapter->QosDispatchThreadInCancellation);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--QOS D Cxl\n", pAdapter->PortName)
   );
   return ntStatus;
}  // MPQOS_CancelQosDispatchThread

VOID MPQOS_MainTxThread(PVOID Context)
{
   PMP_ADAPTER  pAdapter = (PMP_ADAPTER)Context;
   NTSTATUS     ntStatus;
   BOOLEAN      bKeepRunning = TRUE;
   BOOLEAN      bThreadStarted = FALSE;
   PKWAIT_BLOCK pwbArray = NULL;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS Mth: wrong IRQL\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hQosThreadHandle);
      pAdapter->hQosThreadHandle = NULL;
      KeSetEvent(&pAdapter->QosThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
   }

   // allocate a wait block array for the multiple wait
   pwbArray = ExAllocatePoolWithTag
              (
                 NonPagedPool,
                 (QOS_MAIN_THREAD_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 (ULONG)'1SOQ'
              );
   if (pwbArray == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS Mth: NO_MEM\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hQosThreadHandle);
      pAdapter->hQosThreadHandle = NULL;
      KeSetEvent(&pAdapter->QosThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   // Spawn the QOS Dispatch thread
   ntStatus = MPQOS_StartQosDispatchThread(pAdapter);
   if (!NT_SUCCESS(ntStatus))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS Mth: Dth failure\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hQosThreadHandle);
      pAdapter->hQosThreadHandle = NULL;
      KeSetEvent(&pAdapter->QosThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
   }

   KeClearEvent(&pAdapter->QosThreadCancelEvent);
   KeClearEvent(&pAdapter->QosThreadClosedEvent);
   KeClearEvent(&pAdapter->MPThreadTxEvent);

   while (bKeepRunning == TRUE)
   {
      if (bThreadStarted == FALSE)
      {
         bThreadStarted = TRUE;
         KeSetEvent(&pAdapter->QosThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      }

      // self-check the TX queue
      NdisAcquireSpinLock(&pAdapter->TxLock);
      if (!IsListEmpty(&pAdapter->TxPendingList))
      {
         if (pAdapter->QosEnabled == TRUE)
         {
            KeSetEvent(&pAdapter->QosThreadTxEvent, IO_NO_INCREMENT, FALSE);
         }
         
      }
      NdisReleaseSpinLock(&pAdapter->TxLock);

      ntStatus = KeWaitForMultipleObjects
                 (
                    QOS_MAIN_THREAD_EVENT_COUNT,
                    (VOID **)&pAdapter->QosThreadEvent,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,             // non-alertable
                    NULL,
                    pwbArray
                 );
      switch (ntStatus)
      {
         case QOS_CANCEL_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> QOS th: CANCEL\n", pAdapter->PortName)
            );

            KeClearEvent(&pAdapter->QosThreadCancelEvent);
            bKeepRunning = FALSE;
            break;
         }

         case QOS_TX_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> QOS th: TX\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->QosThreadTxEvent);

            #ifdef NDIS620_MINIPORT
            MPQOS_CategorizePacketsEx(pAdapter);
            #else
            MPQOS_CategorizePackets(pAdapter);
            #endif // NDIS620_MINIPORT
            break;
         }

         default:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_ERROR,
               ("<%s> QOS th: unexpected evt-0x%x\n", pAdapter->PortName, ntStatus)
            );
            break;
         }
      }
   }  // while

   if (pwbArray != NULL)
   {
      ExFreePool(pwbArray);
   }

   MPQOS_PurgeQueues(pAdapter);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_FORCE,
      ("<%s> QOS th: OUT (0x%x)\n", pAdapter->PortName, ntStatus)
   );

   KeSetEvent(&pAdapter->QosThreadClosedEvent, IO_NO_INCREMENT, FALSE);

   PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}  // MPQOS_MainTxThread

VOID MPQOS_DispatchThread(PVOID Context)
{
   PMP_ADAPTER  pAdapter = (PMP_ADAPTER)Context;
   NTSTATUS     ntStatus;
   BOOLEAN      bKeepRunning = TRUE;
   BOOLEAN      bThreadStarted = FALSE;
   PKWAIT_BLOCK pwbArray = NULL;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS Dth: wrong IRQL\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hQosDispThreadHandle);
      pAdapter->hQosDispThreadHandle = NULL;
      KeSetEvent(&pAdapter->QosDispThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
   }

   // allocate a wait block array for the multiple wait
   pwbArray = ExAllocatePoolWithTag
              (
                 NonPagedPool,
                 (QOS_DISP_THREAD_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 (ULONG)'2SOQ'
              );
   if (pwbArray == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> QOS Dth: NO_MEM\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hQosDispThreadHandle);
      pAdapter->hQosDispThreadHandle = NULL;
      KeSetEvent(&pAdapter->QosDispThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   KeClearEvent(&pAdapter->QosDispThreadCancelEvent);
   KeClearEvent(&pAdapter->QosDispThreadClosedEvent);
   KeClearEvent(&pAdapter->QosDispThreadTxEvent);

   while (bKeepRunning == TRUE)
   {
      if (bThreadStarted == FALSE)
      {
         bThreadStarted = TRUE;
         KeSetEvent(&pAdapter->QosDispThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      }

      ntStatus = KeWaitForMultipleObjects
                 (
                    QOS_DISP_THREAD_EVENT_COUNT,
                    (VOID **)&pAdapter->QosDispThreadEvent,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,             // non-alertable
                    NULL,
                    pwbArray
                 );
      switch (ntStatus)
      {
         case QOS_DISP_CANCEL_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> QOS Dth: CANCEL\n", pAdapter->PortName)
            );

            KeClearEvent(&pAdapter->QosDispThreadCancelEvent);
            bKeepRunning = FALSE;
            break;
         }
         case QOS_DISP_TX_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> QOS Dth: TX\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->QosDispThreadTxEvent);

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
           if ((pAdapter->QMAPEnabledV2 == TRUE)||(pAdapter->QMAPEnabledV3 == TRUE))
           {
               MPQOS_TLPTransmitPackets(pAdapter);
           }
           else
           {
#endif
#endif
#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
            if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| (pAdapter->QMAPEnabledV4 == TRUE)
                || (pAdapter->QMAPEnabledV1 == TRUE)    || (pAdapter->MPQuickTx != 0))
            {
               MPQOS_TLPTransmitPackets(pAdapter);
            }
            else
            #endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT
            {
               MPQOS_TransmitPackets(pAdapter);
            }
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
           }
#endif
#endif
            break;
         }

         default:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_ERROR,
               ("<%s> QOS Dth: unexpected evt-0x%x\n", pAdapter->PortName, ntStatus)
            );
            break;
         }
      }
   }  // while

   if (pwbArray != NULL)
   {
      ExFreePool(pwbArray);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_FORCE,
      ("<%s> QOS Dth: OUT (0x%x)\n", pAdapter->PortName, ntStatus)
   );

   KeSetEvent(&pAdapter->QosDispThreadClosedEvent, IO_NO_INCREMENT, FALSE);

   PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}  // MPQOS_DispatchThread

VOID MPQOS_TransmitPackets(PMP_ADAPTER pAdapter)
{
   PLIST_ENTRY flowTable, headOfList;
   PMPQOS_FLOW_ENTRY qosFlow;
   BOOLEAN notDone = TRUE;
   int flowTableType = FLOW_SEND_WITH_DATA;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOS_TransmitPackets\n", pAdapter->PortName)
   );

   // process the SEND-state flows with data
   NdisAcquireSpinLock(&pAdapter->TxLock);
   flowTable = &(pAdapter->FlowDataQueue[flowTableType]);
   while (!IsListEmpty(flowTable))
   {
      #ifdef MPQOS_PKT_CONTROL
      if (pAdapter->QosPendingPackets > pAdapter->MPPendingPackets)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> QOS Dth: A-hold, pending %d/%u\n", pAdapter->PortName,
              pAdapter->QosPendingPackets, pAdapter->MPPendingPackets)
         );
         break;
      }
      #endif  // MPQOS_PKT_CONTROL

      headOfList = RemoveHeadList(flowTable);
      qosFlow = CONTAINING_RECORD
                (
                   headOfList,
                   MPQOS_FLOW_ENTRY,
                   List
                );
      if (!IsListEmpty(&qosFlow->Packets))
      {
         PLIST_ENTRY pList;
         NDIS_STATUS status;

         // move the flow entry to the end of the flow table
         InsertTailList(flowTable, &qosFlow->List);

         // de-queue NDIS packet
         pList = MPQOS_DequeuePacket(pAdapter, &qosFlow->Packets, &status);

         NdisReleaseSpinLock(&pAdapter->TxLock);

         // send to USB
         switch (status)
         {
            case NDIS_STATUS_SUCCESS:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_TRACE,
                  ("<%s> QOS_TX: flow 0x%x\n", pAdapter->PortName, qosFlow->FlowId)
               );
#ifdef NDIS620_MINIPORT
               MP_USBTxPacketEx(pAdapter, pList, qosFlow->FlowId, TRUE);
#else
               MP_USBTxPacket(pAdapter, qosFlow->FlowId, TRUE, pList);
#endif
               break;
            }
            case NDIS_STATUS_MEDIA_BUSY:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> QOS_TX_BUSY: flow 0x%x, drop pkt\n", pAdapter->PortName, qosFlow->FlowId)
               );
               MPMAIN_Wait(-(100 * 1000L)); // 10ms
               MPWork_ScheduleWorkItem(pAdapter);
               break;
            }
            case NDIS_STATUS_INTERFACE_DOWN:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> QOS_TX_IF_DOWN: flow 0x%x, drop pkt\n", pAdapter->PortName, qosFlow->FlowId)
               );
               MPWork_ScheduleWorkItem(pAdapter);
               break;
            }
            default:
               break;
         }
      }
      else
      {
         // move the flow to the flow table with out data
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> QOS_TX: move flow 0x%x to no-data Q\n", pAdapter->PortName, qosFlow->FlowId)
         );
         qosFlow->FlowState = FLOW_SEND_WITHOUT_DATA;
         InsertTailList(&(pAdapter->FlowDataQueue[FLOW_SEND_WITHOUT_DATA]), &qosFlow->List);
         NdisReleaseSpinLock(&pAdapter->TxLock);
      }
      NdisAcquireSpinLock(&pAdapter->TxLock);
   }
   NdisReleaseSpinLock(&pAdapter->TxLock);

   #ifdef MPQOS_PKT_CONTROL
   if (pAdapter->QosPendingPackets > pAdapter->MPPendingPackets)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> QOS Dth: B-hold, pending %d/%u\n", pAdapter->PortName,
           pAdapter->QosPendingPackets, pAdapter->MPPendingPackets)
      );
      goto Func_Exit;
   }
   #endif  // MPQOS_PKT_CONTROL

   // Process the default flow, one packet only
   NdisAcquireSpinLock(&pAdapter->TxLock);
   flowTableType = FLOW_DEFAULT;
   flowTable = &(pAdapter->FlowDataQueue[flowTableType]);
   headOfList = flowTable->Flink;

   if ((!IsListEmpty(flowTable)) && (pAdapter->DefaultFlowEnabled == TRUE))
   {
      qosFlow = CONTAINING_RECORD
                (
                   headOfList,
                   MPQOS_FLOW_ENTRY,
                   List
                );
      if (!IsListEmpty(&qosFlow->Packets))
      {
         PLIST_ENTRY pList;
         NDIS_STATUS status;

         // de-queue NDIS packet
         pList = MPQOS_DequeuePacket(pAdapter, &qosFlow->Packets, &status);

         NdisReleaseSpinLock(&pAdapter->TxLock);

         // send to USB
         switch (status)
         {
            case NDIS_STATUS_SUCCESS:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_TRACE,
                  ("<%s> QOS_TX: flow 0x%x\n", pAdapter->PortName, qosFlow->FlowId)
               );
#ifdef NDIS620_MINIPORT
               MP_USBTxPacketEx(pAdapter, pList, 0, FALSE);
#else
               MP_USBTxPacket(pAdapter, 0, FALSE, pList);
#endif
               break;
            }
            case NDIS_STATUS_MEDIA_BUSY:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> QOS_TX_BUSY: flow 0x%x, drop pkt\n", pAdapter->PortName, qosFlow->FlowId)
               );
               MPMAIN_Wait(-(100 * 1000L)); // 10ms
               MPWork_ScheduleWorkItem(pAdapter);
               break;
            }
            case NDIS_STATUS_INTERFACE_DOWN:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> QOS_TX_IF_DOWN: flow 0x%x, drop pkt\n", pAdapter->PortName, qosFlow->FlowId)
               );
               MPWork_ScheduleWorkItem(pAdapter);
               break;
            }
            default:
               break;
         }

         MPQOS_CheckDefaultQueue(pAdapter, TRUE);
      }
      else
      {
         // no more data in the flow
         MPQOS_CheckDefaultQueue(pAdapter, FALSE);

         NdisReleaseSpinLock(&pAdapter->TxLock);
      }
   }
   else
   {
      NdisReleaseSpinLock(&pAdapter->TxLock);
   }

Func_Exit:

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--QOS_TransmitPackets\n", pAdapter->PortName)
   );
}  // MPQOS_TransmitPacket

BOOLEAN MPQOS_CheckDefaultQueue(PMP_ADAPTER pAdapter, BOOLEAN UseSpinlock)
{
   PLIST_ENTRY         headOfList, peekEntry;
   PMPQOS_FLOW_ENTRY   flow;
   BOOLEAN             defaultQueueEmpty = FALSE, packetsPresent = FALSE;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOS_CheckDefaultQueue: %u\n", pAdapter->PortName, UseSpinlock)
   );

   if (UseSpinlock == TRUE)
   {
      NdisAcquireSpinLock(&pAdapter->TxLock);
   }

   headOfList = &(pAdapter->FlowDataQueue[FLOW_DEFAULT]);
   peekEntry = headOfList->Flink;

   // go through each flow entry
   while (peekEntry != headOfList)
   {
      flow = CONTAINING_RECORD(peekEntry, MPQOS_FLOW_ENTRY, List);

      if (IsListEmpty(&flow->Packets))
      {
         // double check
         if ((flow->FlowId != 0) && (flow != &pAdapter->DefaultQosFlow))
         {
            // it's a deleted QOS flow, cleanup
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> QOS_CheckDefaultQueue: cleanup flow 0x%x from default Q\n",
                 pAdapter->PortName, flow->FlowId)
            );
            peekEntry = peekEntry->Flink;
            MPQOSC_PurgeFilterList(pAdapter, &flow->FilterList);
            RemoveEntryList(&flow->List);
            ExFreePool(flow);
            continue;
         }
         else
         {
            // this is our original default flow
            defaultQueueEmpty = TRUE;
         }
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> QOS_CheckDefaultQueue: pkts present: flow 0x%x\n",
              pAdapter->PortName, flow->FlowId)
         );
         packetsPresent = TRUE;
      }

      peekEntry = peekEntry->Flink;
   }

   if ((packetsPresent == TRUE) && (defaultQueueEmpty == TRUE))
   {
      // move the default QOS entry to tail so that data pkts always at
      // the front
      pAdapter->DefaultQosFlow.FlowState = FLOW_DEFAULT;
      RemoveEntryList(&(pAdapter->DefaultQosFlow.List));
      InsertTailList(&(pAdapter->FlowDataQueue[FLOW_DEFAULT]), &(pAdapter->DefaultQosFlow.List));
   }

   if (packetsPresent == TRUE)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_TRACE,
         ("<%s> QOS_CheckDefaultQueue: sig disp th again\n", pAdapter->PortName)
      );
      KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT,FALSE);
   }

   if (UseSpinlock == TRUE)
   {
      NdisReleaseSpinLock(&pAdapter->TxLock);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--QOS_CheckDefaultQueue: %u (def_q/p: %u/%d)\n",
       pAdapter->PortName, UseSpinlock, defaultQueueEmpty, packetsPresent)
   );

   return packetsPresent;

}  // MPQOS_CheckDefaultQueue

PLIST_ENTRY MPQOS_DequeuePacket
(
   PMP_ADAPTER pAdapter,
   PLIST_ENTRY PacketQueue,
   PNDIS_STATUS Status
)
{
   PLIST_ENTRY pList;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOS_DequeuePacket\n", pAdapter->PortName)
   );

   pList = RemoveHeadList(PacketQueue);
   if ((pAdapter->Flags & fMP_ANY_FLAGS) == 0)
   {
      InsertTailList(&pAdapter->TxBusyList, pList);
      *Status = NDIS_STATUS_SUCCESS;
   }
   else
   {
      if (IsListEmpty(&pAdapter->TxBusyList))
      {
         #ifdef NDIS620_MINIPORT

         PMPUSB_TX_CONTEXT_NBL qcNbl;

         // get NetBufferList
         qcNbl = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NBL, List);

         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPQOS_DequeuePacket: MP not ready, comp NBL 0x%p\n",
              pAdapter->PortName, qcNbl->NBL)
         );
         MPUSB_CompleteNetBufferList
         (
            pAdapter,
            qcNbl->NBL,
            NDIS_STATUS_FAILURE,
            NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
            qcNbl
         );

         #else

         PNDIS_PACKET pNdisPacket;

         pNdisPacket = ListItemToPacket(pList);
         NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
         InsertTailList(&pAdapter->TxCompleteList, pList);
         *Status = NDIS_STATUS_INTERFACE_DOWN;

         #endif // NDIS620_MINIPORT

      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPQOS_DequeuePacket: MP not ready, NBL back to Pending\n",
              pAdapter->PortName)
         );
         InsertHeadList(&pAdapter->TxPendingList, pList);
         *Status = NDIS_STATUS_MEDIA_BUSY;
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--QOS_DequeuePacket: ST 0x%x\n", pAdapter->PortName, *Status)
   );
   return pList;
}  // MPQOS_DequeuePacket

VOID MPQOS_CategorizePackets(IN PMP_ADAPTER pAdapter)
{
   PLIST_ENTRY  pList;
   NDIS_STATUS  status;
   BOOLEAN      kicker = FALSE;
   int          processed = 0;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->CategorizePackets\n", pAdapter->PortName)
   );

   NdisAcquireSpinLock( &pAdapter->TxLock );

   while ( !IsListEmpty( &pAdapter->TxPendingList) )
   {
      pList = RemoveHeadList( &pAdapter->TxPendingList );

      if ( (pAdapter->Flags & fMP_ANY_FLAGS) == 0 )
      {
         ULONG dataLen;

         // categorize...
         dataLen = MPUSB_CopyNdisPacketToBuffer
                   (
                      ListItemToPacket(pList),
                      pAdapter->QosPacketBuffer,
                      0
                   );
         if (dataLen != 0)
         {
            MPQOS_Enqueue(pAdapter, pList, pAdapter->QosPacketBuffer, dataLen);

            // signal the QOS dispatch thread
            KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT,FALSE);
         }
         else
         {
            PNDIS_PACKET pNdisPacket;

            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_ERROR,
               ("<%s> CategorizePackets: ERR-copy failure\n", pAdapter->PortName)
            );
            
            kicker = TRUE;
            // TX packets could be completed out of order because of this
            pNdisPacket = ListItemToPacket(pList);
            NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
            InsertTailList(&pAdapter->TxCompleteList, pList);
         }

         NdisReleaseSpinLock( &pAdapter->TxLock );

         ++processed;
      }
      else
      {
         PNDIS_PACKET pNdisPacket;

         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_ERROR,
            ("<%s> CategorizePackets: dev flagged 0x%x, complete pkt\n",
              pAdapter->PortName, pAdapter->Flags)
         );

         // TX packets could be completed out of order because of this
         kicker = TRUE;
         pNdisPacket = ListItemToPacket(pList);
         NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
         InsertTailList(&pAdapter->TxCompleteList, pList);
         NdisReleaseSpinLock(&pAdapter->TxLock);

      }

      NdisAcquireSpinLock( &pAdapter->TxLock );
   }  // while

   NdisReleaseSpinLock( &pAdapter->TxLock );

   if ( kicker )
   {
      MPWork_ScheduleWorkItem(pAdapter);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--CategorizePackets (processed: %d)\n", pAdapter->PortName, processed)
   );

   return;
} // MPQOS_CategorizePackets


#ifdef NDIS620_MINIPORT

VOID MPQOS_CategorizePacketsEx(IN PMP_ADAPTER pAdapter)
{
   PLIST_ENTRY  pList;
   NDIS_STATUS  status;
   BOOLEAN      kicker = FALSE;
   int          processed = 0;
   PNET_BUFFER_LIST      netBufferList;
   PNET_BUFFER           netBuffer;
   PMPUSB_TX_CONTEXT_NBL txContext = NULL;
   PMPUSB_TX_CONTEXT_NB  nbContext;
   int                   numNBs;
   PUCHAR                dataPkt;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->CategorizePacketsEx\n", pAdapter->PortName)
   );

   NdisAcquireSpinLock(&pAdapter->TxLock);

   while (!IsListEmpty(&pAdapter->TxPendingList))
   {
      pList = RemoveHeadList(&pAdapter->TxPendingList);

         if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| 
             (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0) ||(pAdapter->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
             || (pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE) 
#endif
#endif
            )
      {
         nbContext = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NB, List);
         txContext = nbContext->NBL;
         netBufferList = txContext->NBL;
         numNBs = txContext->TotalNBs;
      }
      else
      {
      txContext = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NBL, List);
      netBufferList = txContext->NBL;
      nbContext = (PMPUSB_TX_CONTEXT_NB)((PCHAR)txContext + sizeof(MPUSB_TX_CONTEXT_NBL));
      numNBs = txContext->TotalNBs;
      }

      // examine only the first NB
      netBuffer = nbContext[0].NB;

      if ((pAdapter->Flags & fMP_ANY_FLAGS) == 0)
      {
         UINT dataLen;

         // categorize...
         dataPkt = MPUSB_RetriveDataPacket(pAdapter, netBuffer, &dataLen, pAdapter->QosPacketBuffer);
         if (dataLen != 0)
         {
            MPQOS_Enqueue(pAdapter, pList, pAdapter->QosPacketBuffer, dataLen);

            // signal the QOS dispatch thread
            KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT,FALSE);
         }
         else
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_ERROR,
               ("<%s> CategorizePacketsEx: ERR-copy failure\n", pAdapter->PortName)
            );
            
            kicker = TRUE;
             if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| 
                 (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0) || (pAdapter->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
                 || (pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE)  
#endif
#endif
                )
            {
                ((PMPUSB_TX_CONTEXT_NBL)txContext)->NdisStatus = NDIS_STATUS_FAILURE;
                MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, FALSE);
            }
            else
            {
            // TX packets could be completed out of order because of this
            MPUSB_CompleteNetBufferList
            (
               pAdapter,
               txContext->NBL,
               NDIS_STATUS_FAILURE,
               NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
               txContext
            );
         }
         }

         NdisReleaseSpinLock( &pAdapter->TxLock );

         ++processed;
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_ERROR,
            ("<%s> CategorizePacketsEx: dev flagged 0x%x, complete pkt\n",
              pAdapter->PortName, pAdapter->Flags)
         );

         kicker = TRUE;
          if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| 
              (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0) || (pAdapter->QMAPEnabledV4 == TRUE) 
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
              || (pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE)   
#endif
#endif
             )
         {
             ((PMPUSB_TX_CONTEXT_NBL)txContext)->NdisStatus = NDIS_STATUS_FAILURE;
             MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, FALSE);
         }
         else
         {
         // TX packets could be completed out of order because of this
         MPUSB_CompleteNetBufferList
         (
            pAdapter,
            txContext->NBL,
            NDIS_STATUS_FAILURE,
            NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
            txContext
         );
         }
         NdisReleaseSpinLock(&pAdapter->TxLock);
      }

      NdisAcquireSpinLock( &pAdapter->TxLock );
   }  // while

   NdisReleaseSpinLock( &pAdapter->TxLock );

   if ( kicker )
   {
      MPWork_ScheduleWorkItem(pAdapter);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--CategorizePackets (processed: %d)\n", pAdapter->PortName, processed)
   );

   return;
} // MPQOS_CategorizePacketsEx

#endif

// must be executed with in TX spin lock
VOID MPQOS_Enqueue
(
   PMP_ADAPTER  pAdapter,
   PLIST_ENTRY  Packet,
   PCHAR        Data,
   ULONG        DataLength
)
{
   QOS_FLOW_QUEUE_TYPE i;
   PLIST_ENTRY         headOfList, peekEntry;
   PMPQOS_FLOW_ENTRY   flow;
   BOOLEAN             filterFound = FALSE;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOS_Enqueue\n", pAdapter->PortName)
   );

   MPMAIN_PrintBytes
   (
      Data, 54, DataLength, "PacketData->D", pAdapter,
      MP_DBG_MASK_DATA_QOS, MP_DBG_LEVEL_VERBOSE
   );

   if (TRUE == MPQOS_EnqueueByPrecedence(pAdapter, Packet, Data, DataLength))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_TRACE,
         ("<%s> <--QOS_Enqueue: enqueued by filter precedence\n", pAdapter->PortName)
      );
      return;
   }

   for (i = FLOW_SEND_WITH_DATA;
        ((i <= FLOW_QUEUED) && (filterFound == FALSE) && (pAdapter->MPDisableQoS == 0));
        i++)
   {
      if (IsListEmpty(&(pAdapter->FlowDataQueue[i])))
      {
         continue;
      }
      headOfList = &(pAdapter->FlowDataQueue[i]);
      peekEntry = headOfList->Flink;

      // go through each flow entry
      while ((peekEntry != headOfList) && (filterFound == FALSE))
      {
         flow = CONTAINING_RECORD(peekEntry, MPQOS_FLOW_ENTRY, List);

         if (!IsListEmpty(&flow->FilterList))
         {
            PLIST_ENTRY headOfList, peekEntry;
            PQMI_FILTER filter;

            headOfList = &flow->FilterList;
            peekEntry = headOfList->Flink;

            // go through each filter
            while ((peekEntry != headOfList) && (filterFound == FALSE))
            {
               filter = CONTAINING_RECORD(peekEntry, QMI_FILTER, List);
               if (filter->Precedence >= QOS_FILTER_PRECEDENCE_INVALID)
               {
                  if (TRUE == MPQOS_DoesPktMatchThisFilterEx(pAdapter, filter, Data, DataLength))
                  {
                     filterFound = TRUE;
                     break;
                  }
               }

               // go to the next filter
               peekEntry = peekEntry->Flink;
            }
         }

         if (filterFound == TRUE)
         {
            // enqueue NDIS packet
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> QOS_Enqueue: enqueue pkt to flow 0x%x (table %u)\n", 
                 pAdapter->PortName, flow->FlowId, i)
            );
            InsertTailList(&flow->Packets, Packet);

            if (i == FLOW_SEND_WITHOUT_DATA)
            {
               // move the flow to FLOW_SEND_WITH_DATA table
               flow->FlowState = FLOW_SEND_WITH_DATA;
               RemoveEntryList(&flow->List);
               InsertTailList
               (
                  &(pAdapter->FlowDataQueue[FLOW_SEND_WITH_DATA]),
                  &flow->List
               );
            }
         }

         // go to the next flow
         peekEntry = peekEntry->Flink;
      }
   }

   if (filterFound == FALSE)
   {
      // put on default queue
      #ifdef NDIS51_MINIPORT
      {
         PNDIS_PACKET pNdisPacket = ListItemToPacket(Packet);

         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_TRACE,
            ("<%s> QOS_Enqueue: place in default Q 0x%p->0x%p\n", pAdapter->PortName,
              pNdisPacket, &(pAdapter->DefaultQosFlow))
         );
      }
      #else
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_TRACE,
         ("<%s> QOS_Enqueue: no filter match found, place in default Q\n", pAdapter->PortName)
      );
      #endif // NDIS51_MINIPORT
      InsertTailList(&(pAdapter->DefaultQosFlow.Packets), Packet);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--QOS_Enqueue\n", pAdapter->PortName)
   );
}  // MPQOS_Enqueue

VOID MPQOS_GetIPHeaderV4
(
   PMP_ADAPTER   pAdapter,
   PUCHAR        PacketData,
   PREF_IPV4_HDR Header
)
{
   PUCHAR  puc = PacketData;

   #define ICMP 0x01
   #define TCP  0x06
   #define UDP  0x11
 
   RtlZeroMemory(Header, sizeof(REF_IPV4_HDR));

   Header->Version = (*puc & 0xF0) >> 4;
   Header->HdrLen  = (*puc & 0x0F) * 4;

   puc++;
   Header->TOS = *puc;

   puc++;
   Header->TotalLength = ntohs(*((PUSHORT)puc));

   puc += 2;
   Header->Identification = *((PUSHORT)puc);

   puc += 2;
   Header->Flags = *puc;
   Header->Flags >>= 5; // keep high 3 bits
   Header->FragmentOffset = *puc & 0x1F; // low 5 bits
   Header->FragmentOffset <<= 8;
   puc++;
   Header->FragmentOffset |= (UCHAR)*puc;

   puc += 2;
   Header->Protocol = *puc;

   puc += 3;
   RtlCopyMemory(&Header->SrcAddr, puc, sizeof(ULONG));
   Header->SrcAddr = ntohl(Header->SrcAddr);

   puc += 4;
   RtlCopyMemory(&Header->DestAddr, puc, sizeof(ULONG));
   Header->DestAddr = ntohl(Header->DestAddr);

   // go to transportation header
   puc = PacketData + Header->HdrLen;
   switch (Header->Protocol)
   {
      case TCP:
      case UDP:
      {
         RtlCopyMemory(&Header->SrcPort, puc, sizeof(USHORT));
         Header->SrcPort = ntohs(Header->SrcPort);

         puc += sizeof(USHORT);
         RtlCopyMemory(&Header->DestPort, puc, sizeof(USHORT));
         Header->DestPort = ntohs(Header->DestPort);

         break;
      }

      case ICMP:
      {
         Header->icmp.Type = *puc++;
         Header->icmp.Code = *puc;

         break;
      }

      default:
      {
         break;
      }
   }
}  // MPQOS_GetIPHeaderV4

BOOLEAN  MPQOS_DoesPktMatchThisFilterEx
(
  PMP_ADAPTER  pAdapter,
  PQMI_FILTER  filter,
  PUCHAR       pkt,
  ULONG        PacketLength
)
{
  ULONG   ip4_addr;
  BOOLEAN bMatch = FALSE;
  BOOLEAN cacheInfoIfMatch = FALSE;
  REF_IPV4_HDR ipHdr;

  #define ICMP 0x01
  #define TCP  0x06
  #define UDP  0x11
  #define TCPUDP 0xFD

  QCNET_DbgPrint
  (
     MP_DBG_MASK_QOS,
     MP_DBG_LEVEL_TRACE,
     ("<%s> -->PktMatchFilterEx\n", pAdapter->PortName)
  );

  // Reference: 1) 802.3 Header format; 2) IP Header format
  // IP Fragmentation:
  // The first fragment will have the fragment offset zero, and the last fragment
  // will have the more-fragments flag reset to zero.

  pkt += 14; // skip the ethernet header

  MPQOS_GetIPHeaderV4(pAdapter, pkt, &ipHdr);

  if (ipHdr.Version != filter->IpVersion)
  {
    // not IPv4 packet
    QCNET_DbgPrint
    (
       MP_DBG_MASK_QOS,
       MP_DBG_LEVEL_ERROR,
       ("<%s> <--PktMatchFilterEx: no match <IpVersion> 0x%x/0x%x\n",
         pAdapter->PortName, ipHdr.Version, filter->IpVersion)
    );
    return FALSE;
  }
  else
  {
     bMatch = TRUE;
  }

  #ifdef QCQOS_IPV6

  if (filter->IpVersion == 6) // IPV6
  {
     return MPQOS_DoesPktMatchThisFilterV6
            (
               pAdapter,
               filter,
               pkt,             // pointing to IP hdr
               PacketLength-14  // informatioon only
            );
  }

  #endif // QCQOS_IPV6

  QCNET_DbgPrint
  (
     MP_DBG_MASK_QOS,
     MP_DBG_LEVEL_TRACE,
     ("<%s> PktMatchFilterEx: Flags 0x%x Offset 0x%x ID 0x%x\n",
       pAdapter->PortName, ipHdr.Flags, ipHdr.FragmentOffset, ntohs(ipHdr.Identification))
  );
  // if MF bit set
  //   0   1   2
  // +---+---+---+
  // |   | D | M |
  // | 0 | F | F |
  // +---+---+---+
  if (ipHdr.Flags & 0x01)
  {
     if (ipHdr.FragmentOffset == 0) // first fragment
     {
        // cache IP ID
        cacheInfoIfMatch = TRUE;
     }
     else
     {
        // compare
        if (MPQOS_IpIdMatch(pAdapter, &ipHdr, &filter->PacketInfoList) != NULL)
        {
           return TRUE;
        }
        else
        {
           return FALSE;
        }
     }
  }
  else if (ipHdr.FragmentOffset != 0)
  {
     PMPQOS_PKT_INFO pktInfo;

     // last fragment, compare
     pktInfo = MPQOS_IpIdMatch(pAdapter, &ipHdr, &filter->PacketInfoList);
     if (pktInfo != NULL)
     {
        RemoveEntryList(&pktInfo->List); // clear cache
        ExFreePool(pktInfo);
        return TRUE;
     }
     else
     {
        return FALSE;
     }
  }
  
  // Type of service
  if (filter->Ipv4TypeOfServiceMask)
  {
    if (ipHdr.TOS != filter->Ipv4TypeOfService)
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_QOS,
          MP_DBG_LEVEL_ERROR,
          ("<%s> <--PktMatchFilterEx: no match <Ipv4TypeOfService> 0x%x/0x%x\n",
            pAdapter->PortName, ipHdr.TOS, filter->Ipv4TypeOfService)
       );
       return FALSE;
    }
    else
    {
       bMatch = TRUE;
    }
  }

  if (filter->Ipv4NextHdrProtocol == TCPUDP)
  {
     if ((ipHdr.Protocol != TCP) && (ipHdr.Protocol != UDP))
     {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_QOS,
          MP_DBG_LEVEL_ERROR,
          ("<%s> <--PktMatchFilterEx: no match <Ipv4NextHdrProtocol> 0x%x/0x%x(TCPUDP)\n",
            pAdapter->PortName, ipHdr.Protocol, filter->Ipv4NextHdrProtocol)
       );
        return FALSE;
     }
  }
  else if (filter->Ipv4NextHdrProtocol)
  {
    if (ipHdr.Protocol != filter->Ipv4NextHdrProtocol)
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_QOS,
          MP_DBG_LEVEL_ERROR,
          ("<%s> <--PktMatchFilterEx: no match <Ipv4NextHdrProtocol> 0x%x/0x%x\n",
            pAdapter->PortName, ipHdr.Protocol, filter->Ipv4NextHdrProtocol)
       );
       return FALSE;
    }
    else
    {
       bMatch = TRUE;
    }
  }

  // compare source IP address
  if (filter->Ipv4SrcAddrSubnetMask)
  {
    ip4_addr = ipHdr.SrcAddr;
    ip4_addr &= filter->Ipv4SrcAddrSubnetMask;

    if( ip4_addr != (filter->Ipv4SrcAddr & filter->Ipv4SrcAddrSubnetMask) )
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_QOS,
          MP_DBG_LEVEL_ERROR,
          ("<%s> <--PktMatchFilterEx: no match <Ipv4SrcAddr> 0x%x/0x%x (mask 0x%x)\n",
            pAdapter->PortName, ipHdr.SrcAddr, filter->Ipv4SrcAddr, filter->Ipv4SrcAddrSubnetMask)
       );
       return FALSE;
    }
    else
    {
       bMatch = TRUE;
    }
  }

  // compare destination IP address
  if (filter->Ipv4DestAddrSubnetMask)
  {
    ip4_addr = ipHdr.DestAddr;
    ip4_addr &= filter->Ipv4DestAddrSubnetMask;

    if( ip4_addr != (filter->Ipv4DestAddr & filter->Ipv4DestAddrSubnetMask) )
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_QOS,
          MP_DBG_LEVEL_ERROR,
          ("<%s> <--PktMatchFilterEx: no match <Ipv4DestAddr> 0x%x/0x%x (mask 0x%x)\n",
            pAdapter->PortName, ipHdr.DestAddr, filter->Ipv4DestAddr, filter->Ipv4DestAddrSubnetMask)
       );
       return FALSE;
    }
    else
    {
       bMatch = TRUE;
    }
  }

  // if no next_protocol, we're done.  Otherwise, we know next_protocol
  //  matches by check earlier
  if (0 == filter->Ipv4NextHdrProtocol)
  {
    // done, all match
    QCNET_DbgPrint
    (
       MP_DBG_MASK_QOS,
       MP_DBG_LEVEL_TRACE,
       ("<%s> <--PktMatchFilterEx: all match <Ipv4NextHdrProtocol> 0\n", pAdapter->PortName)
    );
    if (cacheInfoIfMatch == TRUE)
    {
       MPQOS_CachePacketInfo(pAdapter, &ipHdr, filter);
    }
    return TRUE;
  }

  switch (ipHdr.Protocol)
  {
    case TCP:
    {
      // compare source port
      if (filter->TcpSrcPortSpecified)
      {
        if( ipHdr.SrcPort < filter->TcpSrcPort ||
            ipHdr.SrcPort > filter->TcpSrcPort + filter->TcpSrcPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilterEx: no match <TcpSrcPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, ipHdr.SrcPort, filter->TcpSrcPort, filter->TcpSrcPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      if (filter->TcpUdpSrcPortSpecified)
      {
        if( ipHdr.SrcPort < filter->TcpUdpSrcPort ||
            ipHdr.SrcPort > filter->TcpUdpSrcPort + filter->TcpUdpSrcPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilterEx: no match <TcpUdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, ipHdr.SrcPort, filter->TcpUdpSrcPort, filter->TcpUdpSrcPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }

      // compare destination port
      if (filter->TcpDestPortSpecified)
      {
        // need to add ntohs before comparison? check
        if( ipHdr.DestPort < filter->TcpDestPort ||
            ipHdr.DestPort > filter->TcpDestPort + filter->TcpDestPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilterEx: no match <TcpDestPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, ipHdr.DestPort, filter->TcpDestPort, filter->TcpDestPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      if (filter->TcpUdpDestPortSpecified)
      {
        if( ipHdr.DestPort < filter->TcpUdpDestPort ||
            ipHdr.DestPort > filter->TcpUdpDestPort + filter->TcpUdpDestPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilterEx: no match <TcpUdpDestPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, ipHdr.DestPort, filter->TcpUdpDestPort, filter->TcpUdpDestPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      break;
    }
    case UDP:
    {
      // compare source port
      if (filter->UdpSrcPortSpecified)
      {
        if( ipHdr.SrcPort < filter->UdpSrcPort ||
            ipHdr.SrcPort > filter->UdpSrcPort + filter->UdpSrcPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilterEx: no match <UdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, ipHdr.SrcPort, filter->UdpSrcPort, filter->UdpSrcPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      if (filter->TcpUdpSrcPortSpecified)
      {
        if( ipHdr.SrcPort < filter->TcpUdpSrcPort ||
            ipHdr.SrcPort > filter->TcpUdpSrcPort + filter->TcpUdpSrcPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilterEx: no match <TcpUdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, ipHdr.SrcPort, filter->TcpUdpSrcPort, filter->TcpUdpSrcPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }

      // compare destination port
      if (filter->UdpDestPortSpecified)
      {
        if( ipHdr.DestPort < filter->UdpDestPort ||
            ipHdr.DestPort > filter->UdpDestPort + filter->UdpDestPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilterEx: no match <UdpDestPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, ipHdr.DestPort, filter->UdpDestPort, filter->UdpDestPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      if (filter->TcpUdpDestPortSpecified)
      {
        if( ipHdr.DestPort < filter->TcpUdpDestPort ||
            ipHdr.DestPort > filter->TcpUdpDestPort + filter->TcpUdpDestPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilterEx: no match <TcpUdpDestPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, ipHdr.DestPort, filter->TcpUdpDestPort, filter->TcpUdpDestPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      break;
    }

    case TCPUDP:
    {
       if (filter->TcpUdpSrcPortSpecified)
       {
         if( ipHdr.SrcPort < filter->TcpUdpSrcPort ||
             ipHdr.SrcPort > filter->TcpUdpSrcPort + filter->TcpUdpSrcPortRange )
         {
           QCNET_DbgPrint
           (
              MP_DBG_MASK_QOS,
              MP_DBG_LEVEL_ERROR,
              ("<%s> <--PktMatchFilterEx: no match <TcpUdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
                pAdapter->PortName, ipHdr.SrcPort, filter->TcpUdpSrcPort, filter->TcpUdpSrcPortRange)
           );
           return FALSE;
         }
         else
         {
            bMatch = TRUE;
         }
       }
       if (filter->TcpUdpDestPortSpecified)
       {
         if( ipHdr.DestPort < filter->TcpUdpDestPort ||
             ipHdr.DestPort > filter->TcpUdpDestPort + filter->TcpUdpDestPortRange )
         {
           QCNET_DbgPrint
           (
              MP_DBG_MASK_QOS,
              MP_DBG_LEVEL_ERROR,
              ("<%s> <--PktMatchFilterEx: no match <TcpUdpDestPort> 0x%x/0x%x (range 0x%x)\n",
                pAdapter->PortName, ipHdr.DestPort, filter->TcpUdpDestPort, filter->TcpUdpDestPortRange)
           );
           return FALSE;
         }
         else
         {
            bMatch = TRUE;
         }
       }
       break;
    }

    case ICMP:
    {
      // compare ICMP type port
      if (filter->IcmpFilterMsgTypeSpecified)
      {
        if( ipHdr.icmp.Type != filter->IcmpFilterMsgType )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilterEx: no match <IcmpFilterMsgType> 0x%x/0x%x\n",
               pAdapter->PortName, ipHdr.icmp.Type, filter->IcmpFilterMsgType)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }

      // compare ICMP type port
      if (filter->IcmpFilterCodeFieldSpecified)
      {
        if (ipHdr.icmp.Code != filter->IcmpFilterCodeField)
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilterEx: no match <IcmpFilterCodeField> 0x%x/0x%x\n",
               pAdapter->PortName, ipHdr.icmp.Code, filter->IcmpFilterCodeField)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      break;
    }
    default:  // unknown prototocol
    {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> <--PktMatchFilterEx: no match <unknown prototocol> 0x%x\n",
           pAdapter->PortName, ipHdr.Protocol)
      );
      return FALSE;
    }
  } 

  if ((cacheInfoIfMatch == TRUE) && (bMatch == TRUE))
  {
     MPQOS_CachePacketInfo(pAdapter, &ipHdr, filter);
  }

  QCNET_DbgPrint
  (
     MP_DBG_MASK_QOS,
     MP_DBG_LEVEL_TRACE,
     ("<%s> <--PktMatchFilterEx: match (IPID: 0X%04X)\n", pAdapter->PortName, ipHdr.Identification)
  );

  return bMatch;
}  // MPQOS_DoesPktMatchThisFilterEx

VOID MPQOS_CachePacketInfo
(
   PMP_ADAPTER pAdapter,
   PVOID       IpHdr,
   PQMI_FILTER Filter
)
{
   PMPQOS_PKT_INFO pktInfo;
   PREF_IPV4_HDR hdrV4;
   PREF_IPV6_HDR hdrV6;

   pktInfo = (PMPQOS_PKT_INFO)ExAllocatePool(NonPagedPool, sizeof(MPQOS_PKT_INFO));
   if (pktInfo == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QOS_CachePacketInfo: Error - no MEM\n", pAdapter->PortName)
      );
      return;
   }

   RtlZeroMemory(pktInfo, sizeof(MPQOS_PKT_INFO));
   pktInfo->IPVersion = *((PUCHAR)IpHdr);

   if (pktInfo->IPVersion == 4)
   {
      hdrV4 = (PREF_IPV4_HDR)IpHdr;

      pktInfo->IP.V4.SrcAddr  = hdrV4->SrcAddr;
      pktInfo->IP.V4.DestAddr = hdrV4->DestAddr;
      pktInfo->IP.V4.ULProtocol = hdrV4->Protocol;
      pktInfo->IP.V4.Identification = hdrV4->Identification;
   }
   else if (pktInfo->IPVersion == 6)
   {
      hdrV6 = (PREF_IPV6_HDR)IpHdr;

      RtlCopyMemory(pktInfo->IP.V6.SrcAddr.Byte, hdrV6->SrcAddr, 16);
      RtlCopyMemory(pktInfo->IP.V6.DestAddr.Byte, hdrV6->DestAddr, 16);
      pktInfo->IP.V6.ULProtocol = hdrV6->Protocol;
      pktInfo->IP.V6.Identification = hdrV6->Identification;
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QOS_CachePacketInfo: unknown version %d\n", pAdapter->PortName, pktInfo->IPVersion)
      );
   }

   InsertTailList(&Filter->PacketInfoList, &pktInfo->List);

}  // MPQOS_CachePacketInfo

PMPQOS_PKT_INFO MPQOS_IpIdMatch
(
   PMP_ADAPTER pAdapter,
   PVOID       IpHdr,
   PLIST_ENTRY PktInfoList
)
{
   UCHAR           ipVer;
   PREF_IPV4_HDR   hdrV4 = NULL;
   PREF_IPV6_HDR   hdrV6 = NULL;
   PMPQOS_PKT_INFO pPktInfo = NULL, foundPkt = NULL;
   PLIST_ENTRY     headOfList, peekEntry;

   ipVer = *((PUCHAR)IpHdr);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> QOS_IpIdMatch: IPV%d\n", pAdapter->PortName, ipVer)
   );

   if (ipVer == 4)
   {
      hdrV4 = (PREF_IPV4_HDR)IpHdr;
   }
   else if (ipVer == 6)
   {
      hdrV6 = (PREF_IPV6_HDR)IpHdr;
   }
   else
   {
      // ignore
      return pPktInfo;
   }

   if (!IsListEmpty(PktInfoList))
   {
      headOfList = PktInfoList;
      peekEntry = headOfList->Flink;

      while ((peekEntry != headOfList) && (foundPkt == NULL))
      {
         pPktInfo = CONTAINING_RECORD(peekEntry, MPQOS_PKT_INFO, List);

         if (ipVer == 4)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_TRACE,
               ("<%s> QOS_IpIdMatch: ID 0x%x/0x%x DestA: 0x%x/0x%x Proto %d/%d SrcA 0x%x/0x%x\n",
                 pAdapter->PortName,
                 hdrV4->Identification, pPktInfo->IP.V4.Identification,
                 hdrV4->DestAddr, pPktInfo->IP.V4.DestAddr,
                 hdrV4->Protocol, pPktInfo->IP.V4.ULProtocol,
                 hdrV4->SrcAddr, pPktInfo->IP.V4.SrcAddr)
            );

            if ((hdrV4->Identification == pPktInfo->IP.V4.Identification) &&
                (hdrV4->DestAddr == pPktInfo->IP.V4.DestAddr) &&
                (hdrV4->Protocol == pPktInfo->IP.V4.ULProtocol) &&
                (hdrV4->SrcAddr == pPktInfo->IP.V4.SrcAddr))
            {
               foundPkt = pPktInfo;
            }
            else
            {
               peekEntry = peekEntry->Flink;
               continue;
            }
         }
         else // (ipVer == 6)
         {
            SIZE_T sz;

            if (hdrV6->Identification != pPktInfo->IP.V6.Identification)
            {
               peekEntry = peekEntry->Flink;
               continue;
            }
            sz = RtlCompareMemory(hdrV6->DestAddr, pPktInfo->IP.V6.DestAddr.Byte, 16);
            if (sz != 16)
            {
               peekEntry = peekEntry->Flink;
               continue;
            }
            if (hdrV6->Protocol != pPktInfo->IP.V6.ULProtocol)
            {
               peekEntry = peekEntry->Flink;
               continue;
            }
            sz = RtlCompareMemory(hdrV6->SrcAddr, pPktInfo->IP.V6.SrcAddr.Byte, 16);
            if (sz != 16)
            {
               peekEntry = peekEntry->Flink;
               continue;
            }

            // we have a match
            foundPkt = pPktInfo;
         }
      } // while
   } // if

   return foundPkt;

}  // MPQOS_IpIdMatch

VOID MPQOS_PurgePktInfoList(PLIST_ENTRY PktInfoList)
{
   PMPQOS_PKT_INFO pPktInfo;
   PLIST_ENTRY     headOfList;

   while (!IsListEmpty(PktInfoList))
   {
      headOfList = RemoveHeadList(PktInfoList);
      pPktInfo = CONTAINING_RECORD(headOfList, MPQOS_PKT_INFO, List);
      ExFreePool(pPktInfo);
   }
}  // MPQOS_PurgePktInfoList

// returns FLOW pointer if matches
BOOLEAN  MPQOS_DoesPktMatchThisFilter
(
  PMP_ADAPTER  pAdapter,
  PQMI_FILTER  filter,
  PUCHAR       pkt,
  ULONG        PacketLength
)
{
  ULONG  ip4_addr;
  USHORT port;
  UCHAR  next_protocol;
  PUCHAR next_hdr;
  BOOLEAN bMatch = FALSE;

  #define ICMP 0x01
  #define TCP  0x06
  #define UDP  0x11

  QCNET_DbgPrint
  (
     MP_DBG_MASK_QOS,
     MP_DBG_LEVEL_TRACE,
     ("<%s> -->PktMatchFilter\n", pAdapter->PortName)
  );

  // MPMAIN_PrintBytes
  // (
  //    pkt, 54, PacketLength, "PacketData", pAdapter,
  //    MP_DBG_MASK_DATA_QOS, MP_DBG_LEVEL_VERBOSE
  // );

  // Reference: 1) 802.3 Header format; 2) IP Header format
  // IP Fragmentation:
  // The first fragment will have the fragment offset zero, and the last fragment
  // will have the more-fragments flag reset to zero.

  pkt += 14; // skip the ethernet header

  // maybe we should add a way in the spec to "not care" about IP header, e.g. 
  //  filter on tcp without caring which network layer used
  if( (*pkt & 0xF0) >> 4 != filter->IpVersion)  // IP version field
  {
    // not IPv4 packet
    QCNET_DbgPrint
    (
       MP_DBG_MASK_QOS,
       MP_DBG_LEVEL_ERROR,
       ("<%s> <--PktMatchFilter: no match <IpVersion> 0x%x/0x%x\n",
         pAdapter->PortName, *pkt, filter->IpVersion)
    );
    return FALSE;  // quick return
  }
  else
  {
     bMatch = TRUE;
  }

  #ifdef QCQOS_IPV6

  if (filter->IpVersion == 6) // IPV6
  {
     return MPQOS_DoesPktMatchThisFilterV6
            (
               pAdapter,
               filter,
               pkt,             // pointing to IP hdr
               PacketLength-14  // informatioon only
            );
  }

  #endif // QCQOS_IPV6

  next_hdr = pkt + (*pkt++ & 0x0F) * 4;  // note transport layer hdr location

  // Type of service
  if (filter->Ipv4TypeOfServiceMask)
  {
    if (*pkt != filter->Ipv4TypeOfService)
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_QOS,
          MP_DBG_LEVEL_ERROR,
          ("<%s> <--PktMatchFilter: no match <Ipv4TypeOfService> 0x%x/0x%x\n",
            pAdapter->PortName, *pkt, filter->Ipv4TypeOfService)
       );
       return FALSE;
    }
    else
    {
       bMatch = TRUE;
    }
  }

  // done with TOS, skip pkt length, id, flags, fragment offset, and TTL
  pkt += 8;

  // save L4 protocol and check
  next_protocol = *pkt;
  if (filter->Ipv4NextHdrProtocol)
  {
    if (*pkt != filter->Ipv4NextHdrProtocol)
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_QOS,
          MP_DBG_LEVEL_ERROR,
          ("<%s> <--PktMatchFilter: no match <Ipv4NextHdrProtocol> 0x%x/0x%x\n",
            pAdapter->PortName, *pkt, filter->Ipv4NextHdrProtocol)
       );
       return FALSE;
    }
    else
    {
       bMatch = TRUE;
    }
  }

  // done with protocol field, skip checksum
  pkt += 3;

  // compare source IP address
  if (filter->Ipv4SrcAddrSubnetMask)
  {
    memcpy( &ip4_addr, pkt, sizeof(ULONG) );

    ip4_addr = ntohl(ip4_addr);
    ip4_addr &= filter->Ipv4SrcAddrSubnetMask;

    if( ip4_addr != (filter->Ipv4SrcAddr & filter->Ipv4SrcAddrSubnetMask) )
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_QOS,
          MP_DBG_LEVEL_ERROR,
          ("<%s> <--PktMatchFilter: no match <Ipv4SrcAddr> 0x%x/0x%x (mask 0x%x)\n",
            pAdapter->PortName, ip4_addr, filter->Ipv4SrcAddr, filter->Ipv4SrcAddrSubnetMask)
       );
       return FALSE;
    }
    else
    {
       bMatch = TRUE;
    }
  }
  pkt += 4;

  // compare destination IP address
  if (filter->Ipv4DestAddrSubnetMask)
  {
    memcpy( &ip4_addr, pkt, sizeof(ULONG) );

    ip4_addr = ntohl(ip4_addr);
    ip4_addr &= filter->Ipv4DestAddrSubnetMask;

    if( ip4_addr != (filter->Ipv4DestAddr & filter->Ipv4DestAddrSubnetMask) )
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_QOS,
          MP_DBG_LEVEL_ERROR,
          ("<%s> <--PktMatchFilter: no match <Ipv4DestAddr> 0x%x/0x%x (mask 0x%x)\n",
            pAdapter->PortName, ip4_addr, filter->Ipv4DestAddr, filter->Ipv4DestAddrSubnetMask)
       );
       return FALSE;
    }
    else
    {
       bMatch = TRUE;
    }
  }
  pkt += 4;

  // if no next_protocol, we're done.  Otherwise, we know next_protocol
  //  matches by check earlier
  if (0 == filter->Ipv4NextHdrProtocol)
  {
    // done, all match
    QCNET_DbgPrint
    (
       MP_DBG_MASK_QOS,
       MP_DBG_LEVEL_TRACE,
       ("<%s> <--PktMatchFilter: all match <Ipv4NextHdrProtocol> 0\n", pAdapter->PortName)
    );
    return TRUE;
  }

  switch (next_protocol)
  {
    case TCP:
    {
      // compare source port
      if (filter->TcpSrcPortSpecified)
      {
        memcpy( &port, next_hdr, sizeof(USHORT) );

        port = ntohs(port);
        if( port < filter->TcpSrcPort ||
            port > filter->TcpSrcPort + filter->TcpSrcPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilter: no match <TcpSrcPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, port, filter->TcpSrcPort, filter->TcpSrcPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      if (filter->TcpUdpSrcPortSpecified)
      {
        memcpy( &port, next_hdr, sizeof(USHORT) );

        port = ntohs(port);
        if( port < filter->TcpUdpSrcPort ||
            port > filter->TcpUdpSrcPort + filter->TcpUdpSrcPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilter: no match <TcpUdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, port, filter->TcpUdpSrcPort, filter->TcpUdpSrcPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      next_hdr += 2;

      // compare destination port
      if (filter->TcpDestPortSpecified)
      {
        memcpy( &port, next_hdr, sizeof(USHORT) );
        port = ntohs(port);

        // need to add ntohs before comparison? check
        if( port < filter->TcpDestPort ||
            port > filter->TcpDestPort + filter->TcpDestPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilter: no match <TcpDestPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, port, filter->TcpDestPort, filter->TcpDestPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      if (filter->TcpUdpDestPortSpecified)
      {
        memcpy( &port, next_hdr, sizeof(USHORT) );
        port = ntohs(port);

        // need to add ntohs before comparison? check
        if( port < filter->TcpUdpDestPort ||
            port > filter->TcpUdpDestPort + filter->TcpUdpDestPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilter: no match <TcpUdpDestPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, port, filter->TcpUdpDestPort, filter->TcpUdpDestPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      next_hdr += 2;
      break;
    }
    case UDP:
    {
      // compare source port
      if (filter->UdpSrcPortSpecified)
      {
        memcpy( &port, next_hdr, sizeof(USHORT) );
        port = ntohs(port);

        // need to add ntohs before comparison? check
        if( port < filter->UdpSrcPort ||
            port > filter->UdpSrcPort + filter->UdpSrcPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilter: no match <UdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, port, filter->UdpSrcPort, filter->UdpSrcPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      if (filter->TcpUdpSrcPortSpecified)
      {
        memcpy( &port, next_hdr, sizeof(USHORT) );

        port = ntohs(port);
        if( port < filter->TcpUdpSrcPort ||
            port > filter->TcpUdpSrcPort + filter->TcpUdpSrcPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilter: no match <TcpUdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, port, filter->TcpUdpSrcPort, filter->TcpUdpSrcPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      next_hdr += 2;

      // compare destination port
      if (filter->UdpDestPortSpecified)
      {
        memcpy( &port, next_hdr, sizeof(USHORT) );
        port = ntohs(port);

        // need to add ntohs before comparison? check
        if( port < filter->UdpDestPort ||
            port > filter->UdpDestPort + filter->UdpDestPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilter: no match <UdpDestPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, port, filter->UdpDestPort, filter->UdpDestPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      if (filter->TcpUdpDestPortSpecified)
      {
        memcpy( &port, next_hdr, sizeof(USHORT) );
        port = ntohs(port);

        // need to add ntohs before comparison? check
        if( port < filter->TcpUdpDestPort ||
            port > filter->TcpUdpDestPort + filter->TcpUdpDestPortRange )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilter: no match <TcpUdpDestPort> 0x%x/0x%x (range 0x%x)\n",
               pAdapter->PortName, port, filter->TcpUdpDestPort, filter->TcpUdpDestPortRange)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      next_hdr += 2;
      break;
    }
    case ICMP:
    {
      // compare ICMP type port
      if (filter->IcmpFilterMsgTypeSpecified)
      {
        if( *next_hdr != filter->IcmpFilterMsgType )
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilter: no match <IcmpFilterMsgType> 0x%x/0x%x\n",
               pAdapter->PortName, *next_hdr, filter->IcmpFilterMsgType)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      next_hdr++;

      // compare ICMP type port
      if (filter->IcmpFilterCodeFieldSpecified)
      {
        if (*next_hdr != filter->IcmpFilterCodeField)
        {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_QOS,
             MP_DBG_LEVEL_ERROR,
             ("<%s> <--PktMatchFilter: no match <IcmpFilterCodeField> 0x%x/0x%x\n",
               pAdapter->PortName, *next_hdr, filter->IcmpFilterCodeField)
          );
          return FALSE;
        }
        else
        {
           bMatch = TRUE;
        }
      }
      next_hdr++;
      break;
    }
    default:  // unknown prototocol
    {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_ERROR,
         ("<%s> <--PktMatchFilter: no match <unknown prototocol> 0x%x\n",
           pAdapter->PortName, next_protocol)
      );
      return FALSE;
    }
  } 

  QCNET_DbgPrint
  (
     MP_DBG_MASK_QOS,
     MP_DBG_LEVEL_TRACE,
     ("<%s> <--PktMatchFilter: match %d\n", pAdapter->PortName, bMatch)
  );

  return bMatch;
}  // MPQOS_DoesPktMatchThisFilter

VOID MPQOS_PurgeQueues(PMP_ADAPTER  pAdapter)
{
   QOS_FLOW_QUEUE_TYPE i;
   PMPQOS_FLOW_ENTRY   flow;
   BOOLEAN             kicker = FALSE;
   int                 numFlows, numFilters, numPkts;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPQOS_PurgeQueues\n", pAdapter->PortName)
   );

   numFlows = numFilters = numPkts = 0;

   NdisAcquireSpinLock(&pAdapter->TxLock);

   for (i = FLOW_SEND_WITH_DATA; i < FLOW_QUEUE_MAX; i++)
   {
      // go through each flow entry
      while (!IsListEmpty(&(pAdapter->FlowDataQueue[i])))
      {
         PLIST_ENTRY headOfList;
         PQMI_FILTER filter;

         ++numFlows;
         numFilters = numPkts = 0;

         headOfList = RemoveHeadList(&(pAdapter->FlowDataQueue[i]));
         flow = CONTAINING_RECORD(headOfList, MPQOS_FLOW_ENTRY, List);

         // purge filters
         RemoveQosFilterFromPrecedenceList(pAdapter, &flow->FilterList);
         while (!IsListEmpty(&flow->FilterList))
         {
            PLIST_ENTRY filterItem;

            filterItem = RemoveHeadList(&flow->FilterList);
            filter = CONTAINING_RECORD(filterItem, QMI_FILTER, List);
            ExFreePool(filter);
            ++numFilters;
         }

         // purge packets
         while (!IsListEmpty(&flow->Packets))
         {
            PLIST_ENTRY pktItem;
            pktItem = RemoveHeadList(&flow->Packets);

            #ifdef NDIS620_MINIPORT
            {
               PMPUSB_TX_CONTEXT_NBL qcNbl;

               if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| 
                   (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0) || (pAdapter->QMAPEnabledV4 == TRUE) 
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
                   || (pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE)  
#endif
#endif
                  )
              {
                  PMPUSB_TX_CONTEXT_NB qcNb;
                  // get NetBufferList
                  qcNb = CONTAINING_RECORD(pktItem, MPUSB_TX_CONTEXT_NB, List);
                  qcNbl = qcNb->NBL;
                  ((PMPUSB_TX_CONTEXT_NBL)qcNbl)->NdisStatus = NDIS_STATUS_FAILURE;
                  MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, qcNb, FALSE);
              }
              else
              {
                 
                 
               // get NetBufferList
               qcNbl = CONTAINING_RECORD(pktItem, MPUSB_TX_CONTEXT_NBL, List);

               QCNET_DbgPrint
               (
                  MP_DBG_MASK_WRITE,
                  MP_DBG_LEVEL_DETAIL,
                  ("<%s> MPQOS_PurgeQueues: removed NBL 0x%p\n",
                    pAdapter->PortName, qcNbl->NBL)
               );
               MPUSB_CompleteNetBufferList
               (
                  pAdapter,
                  qcNbl->NBL,
                  NDIS_STATUS_FAILURE,
                  NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
                  qcNbl
               );
            }
            }
            #else
            {
               PNDIS_PACKET pNdisPacket;

               pNdisPacket = ListItemToPacket(pktItem);
               NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
               InsertTailList(&pAdapter->TxCompleteList, pktItem);
               kicker = TRUE;
            }
            #endif // NDIS620_MINIPORT
            ++numPkts;
         }

         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPQOS_PurgeQueues: remove flow 0x%x (%d filters, %d packets)\n",
              pAdapter->PortName, flow->FlowId, numFilters, numPkts)
         );

         // the default flow is defined inside the Adapter
         // if (i != FLOW_DEFAULT)
         if (flow != &(pAdapter->DefaultQosFlow))
         {
            ExFreePool(flow);
         }
      }
   }

   // Put default flow back onto the default flow queue
   pAdapter->DefaultQosFlow.FlowState = FLOW_DEFAULT;
   InsertHeadList(&(pAdapter->FlowDataQueue[FLOW_DEFAULT]), &(pAdapter->DefaultQosFlow.List));

   NdisReleaseSpinLock(&pAdapter->TxLock);

   if (kicker == TRUE)
   {
      MPWork_ScheduleWorkItem(pAdapter);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPQOS_PurgeQueues (%d flows)\n", pAdapter->PortName, numFlows)
   );

}  // MPQOS_PurgeQueues

#ifdef QCQOS_IPV6

VOID QCQOS_GetIPHeaderV6
(
   PMP_ADAPTER   pAdapter,
   PUCHAR        PacketData,
   PREF_IPV6_HDR Header
)
{
   PUCHAR  puc = PacketData;
   UCHAR   nextHdr;
   BOOLEAN bDone = FALSE;

   #define TYPE_TCP      0x06
   #define TYPE_UDP      0x11
   #define TYPE_ICMPV6   0x3A
   #define TYPE_FRAGMENT 0x2C
   #define TYPE_ROUTING  43
   #define TYPE_HOP_BY_HOP_OPT 0
   #define TYPE_NO_NEXT_HDR 59
   #define TYPE_AH       51
   #define TYPE_ESP      50

   RtlZeroMemory(Header, sizeof(REF_IPV6_HDR));

   // parse IPV6 base header
   Header->Version = (*puc & 0xF0) >> 4;
   Header->TrafficClass = (*puc & 0x0F) << 4;
   puc++;
   Header->TrafficClass |= (*puc & 0xF0);

   Header->FlowLabel = *puc & 0x0F;
   Header->FlowLabel <<= 16;              // high 4 bits
   puc++;
   Header->FlowLabel |= *((PUSHORT)puc);  // low 16 bits
   puc += sizeof(USHORT);

   Header->PayloadLength = *((PUSHORT)puc);
   puc += sizeof(USHORT);

   Header->NextHeader = *puc;
   puc++;
   Header->HopLimit = *puc;
   puc++;

   RtlCopyMemory(Header->SrcAddr, puc, 16);
   puc += 16;
   RtlCopyMemory(Header->DestAddr, puc, 16);

   puc += 16;

   nextHdr = Header->NextHeader;

   while (nextHdr != TYPE_TCP &&
          nextHdr != TYPE_UDP &&
          nextHdr != TYPE_ICMPV6 &&
          bDone == FALSE)
   {
      switch (nextHdr)
      {
         case TYPE_ROUTING:
         {
            USHORT len;

            nextHdr = *puc;
            len = ((UCHAR)*(puc+1)) * 8;
            puc += (sizeof(ip6_routing_hdr_type) + len);
            break;
         }

         case TYPE_HOP_BY_HOP_OPT:
         {
            USHORT len;

            nextHdr = *puc;
            len = ((UCHAR)*(puc+1)) * 8;
            puc += (sizeof(ip6_hopbyhop_hdr_type) + len);
            break;
         }

         case TYPE_FRAGMENT:
         {
            nextHdr = *puc;

            puc += 2;
            Header->FragmentOffset = (UCHAR)(*puc);
            Header->FragmentOffset <<= 5;
            puc++;
            Header->MFlag = *puc;
            Header->FragmentOffset |= (Header->MFlag >> 3);
            Header->MFlag &= 0x01; // get MFlag

            puc++;
            Header->Identification = ntohl(*((PULONG)puc));

            QCNET_DbgPrint
            (
               MP_DBG_MASK_QOS,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> _GetIPHeaderV6: Fragment %d Offset %d, ID 0x%x\n", pAdapter->PortName,
                 Header->MFlag, Header->FragmentOffset, Header->Identification)
            );

            puc += 4;  // point to the next hdr

            break;
         }

         case TYPE_NO_NEXT_HDR:
         {
            bDone = TRUE;
            break;
         }

         case TYPE_AH:
         case TYPE_ESP:
         default:
         {
            bDone = TRUE;
            break;
         }
      } // switch (nextHdr)
   } // while

   switch (nextHdr)
   {
      case TYPE_TCP:
      case TYPE_UDP:
      {
         Header->Protocol = nextHdr;

         RtlCopyMemory(&Header->SrcPort, puc, sizeof(USHORT));
         Header->SrcPort = ntohs(Header->SrcPort);

         puc += sizeof(USHORT);
         RtlCopyMemory(&Header->DestPort, puc, sizeof(USHORT));
         Header->DestPort = ntohs(Header->DestPort);

         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> _GetIPHeaderV6: Transport 0x%x (%d, %d)\n", pAdapter->PortName,
              Header->Protocol, Header->SrcPort, Header->DestPort)
         );

         // we are all done
         break;
      }
      case TYPE_ICMPV6:
      {
         // partial support, without detailed ICMPV6 parsing
         Header->Protocol = nextHdr;
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> _GetIPHeaderV6: ICMPV6\n", pAdapter->PortName)
         );
         break;
      }
      case 51:  // Auth Hdr
      case 50:  // Encap Security Payload Hdr
      case 60:  // Destination Options
      case 135: // Mobility Hdr
      case 59:  // No next hdr
      {
         // ignore
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_ERROR,
            ("<%s> <--_GetIPHeaderV6: unexpected next hdr in fragment: 0x%x\n", pAdapter->PortName, nextHdr)
         );
         break;
      }
      default:
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_ERROR,
            ("<%s> <--_GetIPHeaderV6: unknown next hdr in base hdr 0x%x\n", pAdapter->PortName, Header->NextHeader)
         );
         break;
      }
   }  // switch (Header->NextHeader)

}  // QCQOS_GetIPHeaderV6

VOID QCQOS_ConstructIPPrefixMask
(
   PMP_ADAPTER   pAdapter,
   UCHAR         PrefixLength,
   PIPV6_ADDR_PREFIX_MASK Mask
)
{
   int i;

   Mask->High64 = Mask->Low64 = 0;

   for (i = 1; i <= PrefixLength; i++)
   {
      if (i <= 64)
      {
         Mask->High64 |= (1 << (64-i));
      }
      else
      {
         Mask->Low64 |= (1 << (128-i)); 
      }
   }
   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> [QOSV6] IPPrefixMask: 0x%08X%08X%08X%08X (%d)\n", pAdapter->PortName,
        (ULONG)(Mask->High64 >> 32),
        (ULONG)(Mask->High64),
        (ULONG)(Mask->Low64 >> 32),
        (ULONG)(Mask->Low64),
        PrefixLength
      )
   );

}  // QCQOS_ConstructIPPrefixMask

BOOLEAN QCQOS_AddressesMatchV6
(
   PMP_ADAPTER   pAdapter,
   UCHAR         Addr1[16],
   UCHAR         Addr2[16],
   PIPV6_ADDR_PREFIX_MASK Mask
)
{
   PULONGLONG high64_addr1, low64_addr1;
   PULONGLONG high64_addr2, low64_addr2;
   PUCHAR p;

   high64_addr1 = (PULONGLONG)Addr1;
   low64_addr1  = (PULONGLONG)(&(Addr1[8]));
   high64_addr2 = (PULONGLONG)Addr2;
   low64_addr2  = (PULONGLONG)(&(Addr2[8]));

   return ((*high64_addr1 & Mask->High64) == (*high64_addr2 & Mask->High64) &&
           (*low64_addr1  & Mask->Low64)  == (*low64_addr2  & Mask->Low64));

}  // QCQOS_AddressesMatchV6

BOOLEAN  MPQOS_DoesPktMatchThisFilterV6
(
  PMP_ADAPTER  pAdapter,
  PQMI_FILTER  QmiFilter,
  PUCHAR       pkt,
  ULONG        PacketLength
)
{
   REF_IPV6_HDR ipHdr;
   BOOLEAN bMatch = FALSE;
   BOOLEAN cacheInfoIfMatch = FALSE;

   #define ICMP 0x01
   #define TCP  0x06
   #define UDP  0x11
   #define TCPUDP 0xFD

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->PktMatchThisFilterV6: idx 0x%x\n", pAdapter->PortName, QmiFilter->Index)
   );

   QCQOS_GetIPHeaderV6(pAdapter, pkt, &ipHdr);

   // if fragmented
   if (ipHdr.MFlag == 1)
   {
      if (ipHdr.FragmentOffset == 0) // first fragment
      {
         cacheInfoIfMatch = TRUE;
      }
      else
      {
         // compare
         if (MPQOS_IpIdMatch(pAdapter, &ipHdr, &QmiFilter->PacketInfoList) != NULL)
         {
            return TRUE;
         }
         else
         {
            return FALSE;
         }
      }
   }
   else if (ipHdr.FragmentOffset != 0)
   {
      PMPQOS_PKT_INFO pktInfo;

      // last fragment, compare
      pktInfo = MPQOS_IpIdMatch(pAdapter, &ipHdr, &QmiFilter->PacketInfoList);
      if (pktInfo != NULL)
      {
         RemoveEntryList(&pktInfo->List); // clear cache
         ExFreePool(pktInfo);
         return TRUE;
      }
      else
      {
         return FALSE;
      }
   }

   // source addr
   if (QmiFilter->Ipv6SrcAddrPrefixLen != 0)
   {
      bMatch = QCQOS_AddressesMatchV6
               (
                  pAdapter,
                  ipHdr.SrcAddr,
                  QmiFilter->Ipv6SrcAddr,
                  &(QmiFilter->Ipv6SrcAddrMask)
               );
      if (bMatch == FALSE)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [QCQOSV6] PktMatchThisFilterV6: SrcAddr no match\n", pAdapter->PortName)
         );
         return bMatch;
      }
   }

   // destination address
   if (QmiFilter->Ipv6DestAddrPrefixLen != 0)
   {
      bMatch = QCQOS_AddressesMatchV6
               (
                  pAdapter,
                  ipHdr.DestAddr,
                  QmiFilter->Ipv6DestAddr,
                  &(QmiFilter->Ipv6DestAddrMask)
               );
      if (bMatch == FALSE)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [QCQOSV6] PktMatchThisFilterV6: DestAddr no match\n", pAdapter->PortName)
         );
         return bMatch;
      }
   }

   // next header protocol
   if ((QmiFilter->Ipv6NextHdrProtocol != 0) && (QmiFilter->Ipv6NextHdrProtocol != TCPUDP))
   {
      bMatch = (QmiFilter->Ipv6NextHdrProtocol == ipHdr.Protocol);
      if (bMatch == FALSE)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [QCQOSV6] PktMatchThisFilterV6: Transport no match 0x%x/0x%x\n",
              pAdapter->PortName, QmiFilter->Ipv6NextHdrProtocol,
              ipHdr.Protocol)
         );
         return FALSE;
      }
   }

   // traffic class
   if (QmiFilter->Ipv6TrafficClass != 0)
   {
      bMatch = ((QmiFilter->Ipv6TrafficClass & QmiFilter->Ipv6TrafficClassMask) ==
                (ipHdr.TrafficClass & QmiFilter->Ipv6TrafficClassMask));
      if (bMatch == FALSE)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [QCQOSV6] PktMatchThisFilterV6: TrafficClass no match 0x%x/0x%x/0x%x\n",
              pAdapter->PortName, QmiFilter->Ipv6TrafficClass,
              ipHdr.TrafficClass, QmiFilter->Ipv6TrafficClassMask)
         );
         return FALSE;
      }
   }

   // flow label
   if (QmiFilter->Ipv6FlowLabel != 0)
   {
      bMatch = (QmiFilter->Ipv6FlowLabel == ipHdr.FlowLabel);
      if (bMatch == FALSE)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [QCQOSV6] PktMatchThisFilterV6: FlowLabel no match 0x%x/0x%x\n",
              pAdapter->PortName, QmiFilter->Ipv6FlowLabel, ipHdr.FlowLabel)
         );
         return FALSE;
      }
   }

   // TCP/UDP ports
   switch (ipHdr.Protocol)
   {
      case TCP:
      {
         // compare source port
         if (QmiFilter->TcpSrcPortSpecified)
         {
            if( ipHdr.SrcPort < QmiFilter->TcpSrcPort ||
                ipHdr.SrcPort > QmiFilter->TcpSrcPort + QmiFilter->TcpSrcPortRange )
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> <--PktMatchFilter: no match <TcpSrcPort> 0x%x/0x%x (range 0x%x)\n",
                    pAdapter->PortName, ipHdr.SrcPort, QmiFilter->TcpSrcPort, QmiFilter->TcpSrcPortRange)
               );
               return FALSE;
            }
            else
            {
               bMatch = TRUE;
            }
         }
         if (QmiFilter->TcpUdpSrcPortSpecified)
         {
            if( ipHdr.SrcPort < QmiFilter->TcpUdpSrcPort ||
                ipHdr.SrcPort > QmiFilter->TcpUdpSrcPort + QmiFilter->TcpUdpSrcPortRange )
            {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_QOS,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> <--PktMatchFilter: no match <TcpUdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
                   pAdapter->PortName, ipHdr.SrcPort, QmiFilter->TcpUdpSrcPort, QmiFilter->TcpUdpSrcPortRange)
              );
              return FALSE;
            }
            else
            {
               bMatch = TRUE;
            }
         }

         // compare destination port
         if (QmiFilter->TcpDestPortSpecified)
         {
            if( ipHdr.DestPort < QmiFilter->TcpDestPort ||
                ipHdr.DestPort > QmiFilter->TcpDestPort + QmiFilter->TcpDestPortRange )
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> <--PktMatchFilter: no match <TcpDestPort> 0x%x/0x%x (range 0x%x)\n",
                    pAdapter->PortName, ipHdr.DestPort, QmiFilter->TcpDestPort, QmiFilter->TcpDestPortRange)
               );
               return FALSE;
            }
            else
            {
               bMatch = TRUE;
            }
         }
         if (QmiFilter->TcpUdpDestPortSpecified)
         {
            if( ipHdr.DestPort < QmiFilter->TcpUdpDestPort ||
                ipHdr.DestPort > QmiFilter->TcpUdpDestPort + QmiFilter->TcpUdpDestPortRange )
            {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_QOS,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> <--PktMatchFilter: no match <TcpUdpDestPort> 0x%x/0x%x (range 0x%x)\n",
                   pAdapter->PortName, ipHdr.DestPort, QmiFilter->TcpUdpDestPort, QmiFilter->TcpUdpDestPortRange)
              );
              return FALSE;
            }
            else
            {
               bMatch = TRUE;
            }
         }
         break;
      }
      case UDP:
      {
         // compare source port
         if (QmiFilter->UdpSrcPortSpecified)
         {
            if( ipHdr.SrcPort < QmiFilter->UdpSrcPort ||
                ipHdr.SrcPort > QmiFilter->UdpSrcPort + QmiFilter->UdpSrcPortRange )
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> <--PktMatchFilter: no match <UdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
                    pAdapter->PortName, ipHdr.SrcPort, QmiFilter->UdpSrcPort, QmiFilter->UdpSrcPortRange)
               );
               return FALSE;
            }
            else
            {
               bMatch = TRUE;
            }
         }
         if (QmiFilter->TcpUdpSrcPortSpecified)
         {
            if( ipHdr.SrcPort < QmiFilter->TcpUdpSrcPort ||
                ipHdr.SrcPort > QmiFilter->TcpUdpSrcPort + QmiFilter->TcpUdpSrcPortRange )
            {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_QOS,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> <--PktMatchFilter: no match <TcpUdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
                   pAdapter->PortName, ipHdr.SrcPort, QmiFilter->TcpUdpSrcPort, QmiFilter->TcpUdpSrcPortRange)
              );
              return FALSE;
            }
            else
            {
               bMatch = TRUE;
            }
         }

         // compare destination port
         if (QmiFilter->UdpDestPortSpecified)
         {
            if( ipHdr.DestPort < QmiFilter->UdpDestPort ||
                ipHdr.DestPort > QmiFilter->UdpDestPort + QmiFilter->UdpDestPortRange )
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_QOS,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> <--PktMatchFilter: no match <UdpDestPort> 0x%x/0x%x (range 0x%x)\n",
                    pAdapter->PortName, ipHdr.DestPort, QmiFilter->UdpDestPort, QmiFilter->UdpDestPortRange)
               );
               return FALSE;
            }
            else
            {
               bMatch = TRUE;
            }
         }
         if (QmiFilter->TcpUdpDestPortSpecified)
         {
            if( ipHdr.DestPort < QmiFilter->TcpUdpDestPort ||
                ipHdr.DestPort > QmiFilter->TcpUdpDestPort + QmiFilter->TcpUdpDestPortRange )
            {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_QOS,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> <--PktMatchFilter: no match <TcpUdpDestPort> 0x%x/0x%x (range 0x%x)\n",
                   pAdapter->PortName, ipHdr.DestPort, QmiFilter->TcpUdpDestPort, QmiFilter->TcpUdpDestPortRange)
              );
              return FALSE;
            }
            else
            {
               bMatch = TRUE;
            }
         }
         break;
      }
      case TCPUDP:
      {
         if (QmiFilter->TcpUdpSrcPortSpecified)
         {
            if( ipHdr.SrcPort < QmiFilter->TcpUdpSrcPort ||
                ipHdr.SrcPort > QmiFilter->TcpUdpSrcPort + QmiFilter->TcpUdpSrcPortRange )
            {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_QOS,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> <--PktMatchFilter: no match <TcpUdpSrcPort> 0x%x/0x%x (range 0x%x)\n",
                   pAdapter->PortName, ipHdr.SrcPort, QmiFilter->TcpUdpSrcPort, QmiFilter->TcpUdpSrcPortRange)
              );
              return FALSE;
            }
            else
            {
               bMatch = TRUE;
            }
         }
         if (QmiFilter->TcpUdpDestPortSpecified)
         {
            if( ipHdr.DestPort < QmiFilter->TcpUdpDestPort ||
                ipHdr.DestPort > QmiFilter->TcpUdpDestPort + QmiFilter->TcpUdpDestPortRange )
            {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_QOS,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> <--PktMatchFilter: no match <TcpUdpDestPort> 0x%x/0x%x (range 0x%x)\n",
                   pAdapter->PortName, ipHdr.DestPort, QmiFilter->TcpUdpDestPort, QmiFilter->TcpUdpDestPortRange)
              );
              return FALSE;
            }
            else
            {
               bMatch = TRUE;
            }
         }
         break;
      }
      case TYPE_ICMPV6:
      {
         // partial support with no detailed matching (based on addr matching only)
         bMatch = TRUE;
         break;
      }

      default:
      {
         break;
      }
   }  // switch (ipHdr.Protocol)

   if ((cacheInfoIfMatch == TRUE) && (bMatch == TRUE))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> PktMatchThisFilterV6: match and cache info: ID 0x%x\n",
           pAdapter->PortName, ipHdr.Identification)
      );
      MPQOS_CachePacketInfo(pAdapter, &ipHdr, QmiFilter);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--PktMatchThisFilterV6: match %d\n", pAdapter->PortName, bMatch)
   );

   return bMatch;

}  // MPQOS_DoesPktMatchThisFilterV6

#endif // QCQOS_IPV6


BOOLEAN MPQOS_EnqueueByPrecedence
(
   PMP_ADAPTER  pAdapter,
   PLIST_ENTRY  Packet,
   PCHAR        Data,
   ULONG        DataLength
)
{
   QOS_FLOW_QUEUE_TYPE i;
   PLIST_ENTRY         headOfList, peekEntry;
   PMPQOS_FLOW_ENTRY   flow = NULL;
   BOOLEAN             filterFound = FALSE;
   PQMI_FILTER         filter = NULL;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOS_EnqueueByPrecedence\n", pAdapter->PortName)
   );


   if (IsListEmpty(&pAdapter->FilterPrecedenceList))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_TRACE,
         ("<%s> <--QOS_EnqueueByPrecedence: no filter found\n", pAdapter->PortName)
      );
      return FALSE;
   }
   headOfList = &pAdapter->FilterPrecedenceList;
   peekEntry = headOfList->Flink;

   while ((peekEntry != headOfList) && (filterFound == FALSE))
   {
      filter = CONTAINING_RECORD(peekEntry, QMI_FILTER, PrecedenceEntry);
      if (TRUE == MPQOS_DoesPktMatchThisFilterEx(pAdapter, filter, Data, DataLength))
      {
         filterFound = TRUE;
         flow = (PMPQOS_FLOW_ENTRY)filter->Flow;
      }
      peekEntry = peekEntry->Flink;
   }

   if (filterFound == TRUE)
   {
      // enqueue NDIS packet
      QCNET_DbgPrint
      (
         MP_DBG_MASK_QOS,
         MP_DBG_LEVEL_TRACE,
         ("<%s> QOS_EnqueueByPrecedence: enqueue pkt to flow 0x%x (filter_precedence %d)\n",
           pAdapter->PortName, flow->FlowId, filter->Precedence)
      );
      InsertTailList(&flow->Packets, Packet);

      if (flow->FlowState == FLOW_SEND_WITHOUT_DATA)
      {
         // move the flow to FLOW_SEND_WITH_DATA table
         flow->FlowState = FLOW_SEND_WITH_DATA;
         RemoveEntryList(&flow->List);
         InsertTailList
         (
            &(pAdapter->FlowDataQueue[FLOW_SEND_WITH_DATA]),
            &flow->List
         );
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--QOS_EnqueueByPrecedence: filter found %d\n", pAdapter->PortName, filterFound)
   );

   return filterFound;
}  // MPQOS_EnqueueByPrecedence

VOID MPQOS_EnableDefaultFlow(PMP_ADAPTER pAdapter, BOOLEAN Enable)
{
   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> QOS_EnableDefaultFlow: %d\n", pAdapter->PortName, Enable)
   );
   NdisAcquireSpinLock(&pAdapter->TxLock);
   pAdapter->DefaultFlowEnabled = Enable;
   NdisReleaseSpinLock(&pAdapter->TxLock);
}  // MPQOS_EnableDefaultFlow

#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT)

PLIST_ENTRY MPQOS_TLPDequeuePacket
(
   PMP_ADAPTER pAdapter,
   PLIST_ENTRY PacketQueue,
   PNDIS_STATUS Status
)
{
   PLIST_ENTRY pList;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOS_TLPDequeuePacket\n", pAdapter->PortName)
   );

   pList = RemoveHeadList(PacketQueue);
   if ((pAdapter->Flags & fMP_ANY_FLAGS) == 0)
   {
      *Status = NDIS_STATUS_SUCCESS;
   }
   else
   {
      // for TLP, the list is not used (always empty)
      if (IsListEmpty(&pAdapter->TxBusyList))
      {
         #ifdef NDIS620_MINIPORT

         PMPUSB_TX_CONTEXT_NBL qcNbl;

            if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| 
                (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0) || (pAdapter->QMAPEnabledV4 == TRUE) 
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
                || (pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE)  
#endif
#endif
               )
           {
               PMPUSB_TX_CONTEXT_NB qcNb;
               // get NetBufferList
               qcNb = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NB, List);
               qcNbl = qcNb->NBL;
               ((PMPUSB_TX_CONTEXT_NBL)qcNbl)->NdisStatus = NDIS_STATUS_FAILURE;
               MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, qcNb, FALSE);
           }
           else
           {
              
              
         // get NetBufferList
              qcNbl = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NBL, List);

         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
                 ("<%s> MPQOS_PurgeQueues: removed NBL 0x%p\n",
              pAdapter->PortName, qcNbl->NBL)
         );
         MPUSB_CompleteNetBufferList
         (
            pAdapter,
            qcNbl->NBL,
            NDIS_STATUS_FAILURE,
            NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
            qcNbl
         );
           }

         #else

         PNDIS_PACKET pNdisPacket;

         pNdisPacket = ListItemToPacket(pList);
         NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
         InsertTailList(&pAdapter->TxCompleteList, pList);
         *Status = NDIS_STATUS_INTERFACE_DOWN;

         #endif // NDIS620_MINIPORT
      }
      else
      {
         InsertHeadList(&pAdapter->TxPendingList, pList);
         *Status = NDIS_STATUS_MEDIA_BUSY;
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--QOS_TLPDequeuePacket: ST 0x%x\n", pAdapter->PortName, *Status)
   );
   return pList;
}  // MPQOS_TLPDequeuePacket

VOID MPQOS_TLPTransmitPackets(PMP_ADAPTER pAdapter)
{
   PLIST_ENTRY flowTable, headOfList;
   PMPQOS_FLOW_ENTRY qosFlow;
   BOOLEAN notDone = TRUE, bCapacity = TRUE;
   int flowTableType = FLOW_SEND_WITH_DATA;
   BOOLEAN bNoData = FALSE, bPacketsInDefaultQueue;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->QOS_TLPTransmitPackets\n", pAdapter->PortName)
   );

   // process the SEND-state flows with data
   NdisAcquireSpinLock(&pAdapter->TxLock);
   flowTable = &(pAdapter->FlowDataQueue[flowTableType]);
   while ((!IsListEmpty(flowTable)) && (bCapacity == TRUE))
   {
      #ifdef MPQOS_PKT_CONTROL
      if (pAdapter->QosPendingPackets > pAdapter->MPPendingPackets)
      {
         QCNET_DbgPrint
         (
            (MP_DBG_MASK_QOS | MP_DBG_MASK_WRITE),
            MP_DBG_LEVEL_DETAIL,
            ("<%s> QOS_TLPTx Dth: A-hold, pending %d/%u\n", pAdapter->PortName,
              pAdapter->QosPendingPackets, pAdapter->MPPendingPackets)
         );
         break;
      }
      #endif  // MPQOS_PKT_CONTROL

      headOfList = RemoveHeadList(flowTable);
      qosFlow = CONTAINING_RECORD
                (
                   headOfList,
                   MPQOS_FLOW_ENTRY,
                   List
                );

      if (!IsListEmpty(&qosFlow->Packets))
      {
         PLIST_ENTRY pList;
         NDIS_STATUS status;
         ULONG SizeofPacket = GetNextTxPacketSize(pAdapter, &qosFlow->Packets);
         if (TLP_AGG_MORE == MPUSB_AggregationAvailable(pAdapter, FALSE, SizeofPacket))
         {
            // move the flow entry to the end of the flow table
            InsertTailList(flowTable, &qosFlow->List);

            // de-queue NDIS packet
            pList = MPQOS_TLPDequeuePacket(pAdapter, &qosFlow->Packets, &status);

            NdisReleaseSpinLock(&pAdapter->TxLock);

            // send to USB
            switch (status)
            {
               case NDIS_STATUS_SUCCESS:
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_TRACE,
                     ("<%s> QOS_TLPTX: flow 0x%x\n", pAdapter->PortName, qosFlow->FlowId)
                  );
#ifdef NDIS620_MINIPORT
                  MPUSB_TLPTxPacketEx(pAdapter, qosFlow->FlowId, TRUE, pList);
#else
                  MPUSB_TLPTxPacket(pAdapter, qosFlow->FlowId, TRUE, pList);
#endif
                  break;
               }
               case NDIS_STATUS_MEDIA_BUSY:
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_ERROR,
                     ("<%s> QOS_TLPTX_BUSY: flow 0x%x, drop pkt\n", pAdapter->PortName, qosFlow->FlowId)
                  );
                  MPMAIN_Wait(-(100 * 1000L)); // 10ms
                  MPWork_ScheduleWorkItem(pAdapter);
                  break;
               }
               case NDIS_STATUS_INTERFACE_DOWN:
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_ERROR,
                     ("<%s> QOS_TLPTX_IF_DOWN: flow 0x%x, drop pkt\n", pAdapter->PortName, qosFlow->FlowId)
                  );
                  MPWork_ScheduleWorkItem(pAdapter);
                  break;
               }
               default:
               break;
            } // switch
         }
         else if (TLP_AGG_SEND == MPUSB_AggregationAvailable(pAdapter, FALSE, SizeofPacket))
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_WRITE,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> [TLP] QOS_TLPTransmitPackets: sending\n", pAdapter->PortName)
            );
            InsertHeadList(flowTable, &qosFlow->List);
            NdisReleaseSpinLock(&pAdapter->TxLock);
#ifdef NDIS620_MINIPORT
            MPUSB_TLPTxPacketEx(pAdapter, qosFlow->FlowId, TRUE, NULL);
#else
            MPUSB_TLPTxPacket(pAdapter, qosFlow->FlowId, TRUE, NULL);
#endif
            
         }
         else
         {
            // no more TLP capacity, restore flow, exit and wait
            bCapacity = FALSE;
            InsertHeadList(flowTable, &qosFlow->List);
            NdisReleaseSpinLock(&pAdapter->TxLock);
         }
      }
      else
      {
         // move the flow to the flow table with out data
         QCNET_DbgPrint
         (
            MP_DBG_MASK_QOS,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> QOS_TLPTX: move flow 0x%x to no-data Q\n", pAdapter->PortName, qosFlow->FlowId)
         );
         qosFlow->FlowState = FLOW_SEND_WITHOUT_DATA;
         InsertTailList(&(pAdapter->FlowDataQueue[FLOW_SEND_WITHOUT_DATA]), &qosFlow->List);
         NdisReleaseSpinLock(&pAdapter->TxLock);
      }
      NdisAcquireSpinLock(&pAdapter->TxLock);
   }
   NdisReleaseSpinLock(&pAdapter->TxLock);

   #ifdef MPQOS_PKT_CONTROL
   if (pAdapter->QosPendingPackets > pAdapter->MPPendingPackets)
   {
      QCNET_DbgPrint
      (
         (MP_DBG_MASK_QOS | MP_DBG_MASK_WRITE),
         MP_DBG_LEVEL_DETAIL,
         ("<%s> QOS_TLPTx Dth: B-hold, pending %d/%u\n", pAdapter->PortName,
           pAdapter->QosPendingPackets, pAdapter->MPPendingPackets)
      );
      goto Func_Exit;
   }
   #endif  // MPQOS_PKT_CONTROL

   // Process the default flow, one packet only
   NdisAcquireSpinLock(&pAdapter->TxLock);
   flowTableType = FLOW_DEFAULT;
   flowTable = &(pAdapter->FlowDataQueue[flowTableType]);
   headOfList = flowTable->Flink;

   if ((!IsListEmpty(flowTable)) && (pAdapter->DefaultFlowEnabled == TRUE))
   {
      qosFlow = CONTAINING_RECORD
                (
                   headOfList,
                   MPQOS_FLOW_ENTRY,
                   List
                );

      if (!IsListEmpty(&qosFlow->Packets))
      {
         PLIST_ENTRY pList;
         NDIS_STATUS status;
         ULONG SizeofPacket = GetNextTxPacketSize(pAdapter, &qosFlow->Packets);
         if (TLP_AGG_MORE == MPUSB_AggregationAvailable(pAdapter, FALSE, SizeofPacket))
         {
            // de-queue NDIS packet
            pList = MPQOS_TLPDequeuePacket(pAdapter, &qosFlow->Packets, &status);

            bNoData = IsListEmpty(&qosFlow->Packets);
            NdisReleaseSpinLock(&pAdapter->TxLock);

            // send to USB
            switch (status)
            {
               case NDIS_STATUS_SUCCESS:
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_TRACE,
                     ("<%s> QOS_TLPTX: flow 0x%x\n", pAdapter->PortName, qosFlow->FlowId)
                  );
#ifdef NDIS620_MINIPORT
                  MPUSB_TLPTxPacketEx(pAdapter, 0, FALSE, pList);
#else
                  MPUSB_TLPTxPacket(pAdapter, 0, FALSE, pList);
#endif
                  
                  break;
               }
               case NDIS_STATUS_MEDIA_BUSY:
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_ERROR,
                     ("<%s> QOS_TLPTX_BUSY: flow 0x%x, drop pkt\n", pAdapter->PortName, qosFlow->FlowId)
                  );
                  MPMAIN_Wait(-(100 * 1000L)); // 10ms
                  MPWork_ScheduleWorkItem(pAdapter);
                  break;
               }
               case NDIS_STATUS_INTERFACE_DOWN:
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_QOS,
                     MP_DBG_LEVEL_ERROR,
                     ("<%s> QOS_TLPTX_IF_DOWN: flow 0x%x, drop pkt\n", pAdapter->PortName, qosFlow->FlowId)
                  );
                  MPWork_ScheduleWorkItem(pAdapter);
                  break;
               }
               default:
               break;
            }

            bPacketsInDefaultQueue = MPQOS_CheckDefaultQueue(pAdapter, TRUE);
            if (bPacketsInDefaultQueue == FALSE)
            {
               NdisAcquireSpinLock(&pAdapter->TxLock);
               bPacketsInDefaultQueue = !IsListEmpty(&pAdapter->TxPendingList);
               NdisReleaseSpinLock(&pAdapter->TxLock);
            }
         }
         else if (TLP_AGG_SEND == MPUSB_AggregationAvailable(pAdapter, FALSE, SizeofPacket))
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_WRITE,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> [TLP] QOS_TLPTransmitPackets: sending default\n", pAdapter->PortName)
            );
            NdisReleaseSpinLock(&pAdapter->TxLock);
#ifdef NDIS620_MINIPORT            
            MPUSB_TLPTxPacketEx(pAdapter, 0, FALSE, NULL);
#else
            MPUSB_TLPTxPacket(pAdapter, 0, FALSE, NULL);
#endif
         }
         else
         {
            // no more TLP capacity, exit and wait for next Tx event
            NdisReleaseSpinLock(&pAdapter->TxLock);
         }
      }
      else
      {
         // no more data in the flow
         bPacketsInDefaultQueue = MPQOS_CheckDefaultQueue(pAdapter, FALSE);

         NdisReleaseSpinLock(&pAdapter->TxLock);
      }
   }
   else
   {
      NdisReleaseSpinLock(&pAdapter->TxLock);
   }

Func_Exit:

   if (bPacketsInDefaultQueue == FALSE)
   {
      // send/flush current TLP if available if default queue is empty
#ifdef NDIS620_MINIPORT            
      MPUSB_TLPTxPacketEx(pAdapter, 0, FALSE, NULL);
#else
      MPUSB_TLPTxPacket(pAdapter, 0, FALSE, NULL);
#endif
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_QOS,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--QOS_TLPTransmitPackets\n", pAdapter->PortName)
   );
}  // MPQOS_TLPTransmitPacket

#endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT

