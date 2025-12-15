/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P T R A N S M I T . C

GENERAL DESCRIPTION
  This module contains Miniport function for handling send packets.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "MPMain.h"
#include "MPTX.h"
#include "MPusb.h"
#include "MPWork.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPTX.tmh"

#endif   // EVENT_TRACING

VOID MPTX_MiniportTxPackets
(
   NDIS_HANDLE             MiniportAdapterContext,
   PPNDIS_PACKET           PacketArray,
   UINT                    NumberOfPackets)
{
   PMP_ADAPTER       pAdapter;
   UINT              packetCounter;
   BOOLEAN           startTx;

   pAdapter = (PMP_ADAPTER)MiniportAdapterContext;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> ---> MPTX_MiniportTxPackets: %d::%d packets\n", pAdapter->PortName,
       NumberOfPackets, pAdapter->nBusyTx)
   );

   ASSERT(pAdapter);
   ASSERT(PacketArray);

   // Set a flag if the transmit queue is empty when we start all this
   NdisAcquireSpinLock( &pAdapter->TxLock );

   // Capture the starting state of the Pending list (for the bump start if necessary)
   startTx = IsListEmpty( &pAdapter->TxPendingList );

   // For every packet in the list passed down from NDIS
   for ( packetCounter=0; packetCounter < NumberOfPackets; packetCounter++ )
   {

      // Check for a zero pointer
      ASSERT(PacketArray[packetCounter]);

      #ifdef QCUSB_MUX_PROTOCOL
      #ifdef QC_DUAL_IP_FC
      if (MPMAIN_IsFlowControlledData(pAdapter, (PLIST_ENTRY)(PacketArray[packetCounter])->MiniportReservedEx) == TRUE)
      {
         InterlockedIncrement(&(pAdapter->nBusyTx));
         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> MPTX_MiniportTxPackets=>0x%p (flow controlled, nBusyTx %d)\n", pAdapter->PortName,
              PacketArray[packetCounter], pAdapter->nBusyTx)
         );
         continue;
      }
      #endif // QC_DUAL_IP_FC
      #endif // QCUSB_MUX_PROTOCOL

#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
      if (MPMAIN_QMAPIsFlowControlledData(pAdapter, (PLIST_ENTRY)(PacketArray[packetCounter])->MiniportReservedEx) == TRUE)
      {
         InterlockedIncrement(&(pAdapter->nBusyTx));
         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> MPTX_MiniportTxPackets=>0x%p (flow controlled, nBusyTx %d)\n", pAdapter->PortName,
              PacketArray[packetCounter], pAdapter->nBusyTx)
         );
         continue;
      }
#endif // defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)

      // Place the new pakcet on the Pending List
      InsertTailList(&pAdapter->TxPendingList, (PLIST_ENTRY) (PacketArray[packetCounter])->MiniportReservedEx  );
      InterlockedIncrement(&(pAdapter->nBusyTx));
      QCNET_DbgPrint
      (
         MP_DBG_MASK_WRITE,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPTX_MiniportTxPackets=>0x%p\n", pAdapter->PortName, PacketArray[packetCounter])
      );      
    }
   NdisReleaseSpinLock( &pAdapter->TxLock );

   // If the transmit queue was empty when we started
   // Give it a kick from here, otherwise we'd be waiting for the
   // work item to run
   if ( startTx )
   {
      KeSetEvent(&pAdapter->MPThreadTxEvent, IO_NO_INCREMENT, FALSE);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--- MPTX_MiniportTxPackets (packetCounter: %d::%d)\n", pAdapter->PortName,
        packetCounter, pAdapter->nBusyTx)
   );
   return;
}


BOOLEAN MPTX_ProcessPendingTxQueue(IN PMP_ADAPTER pAdapter)
{
   PLIST_ENTRY  pList;
   UINT processed;
   NDIS_STATUS    status;
   BOOLEAN     kicker = FALSE;

   #ifdef NDIS60_MINIPORT
   return MPTX_ProcessPendingTxQueueEx(pAdapter);
   #endif  // NDIS60_MINIPORT

   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> ---> MPTX_ProcessPendingTxQueue\n", pAdapter->PortName)
   );

   processed = 0;

   NdisAcquireSpinLock( &pAdapter->TxLock );

   while ( !IsListEmpty( &pAdapter->TxPendingList) )
   {
      // Move the packet from the Pending list to the Busy list
      pList = RemoveHeadList( &pAdapter->TxPendingList );

      if (((pAdapter->Flags & fMP_ANY_FLAGS) == 0) &&
          (pAdapter->ToPowerDown == FALSE))
      {
         InsertTailList( &pAdapter->TxBusyList,  pList );

         NdisReleaseSpinLock( &pAdapter->TxLock );

         MP_USBTxPacket( pAdapter, 0, FALSE, pList );
         ++processed;
      }
      else
      {
         kicker = TRUE;
         // If the Busy list is empty we can just complete this packet
         // This is to complete the TX pkt in order?
         if ( IsListEmpty( &pAdapter->TxBusyList ) )
         {
            PNDIS_PACKET pNdisPacket;

            pNdisPacket = ListItemToPacket(pList);
            NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
            InsertTailList( &pAdapter->TxCompleteList,  pList );
            // InterlockedDecrement(&(pAdapter->nBusyTx)); // decremented when processing TxCompleteList
            NdisReleaseSpinLock( &pAdapter->TxLock );
         }
         else
         {
            // But if the BusyList is not empty we need to wait it out and try again next time.
            InsertHeadList( &pAdapter->TxPendingList, pList );

            // shall we wait a bit here?
            NdisReleaseSpinLock( &pAdapter->TxLock );
            MPMAIN_Wait(-(100 * 1000L)); // 10ms
         }
      }

      NdisAcquireSpinLock( &pAdapter->TxLock );
   }  // while

   NdisReleaseSpinLock( &pAdapter->TxLock );

   if ( kicker )
   {
      MPWork_ScheduleWorkItem( pAdapter );
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--- MPTX_ProcessPendingTxQueue (processed: %d)\n", pAdapter->PortName, processed)
   );

   return ( processed > 0 );
} // MPTX_ProcessPendingTxQueue

#ifdef NDIS60_MINIPORT

VOID MPTX_MiniportSendNetBufferListsEx
(
   IN NDIS_HANDLE       MiniportAdapterContext,
   IN PNET_BUFFER_LIST  NetBufferLists,
   IN NDIS_PORT_NUMBER  PortNumber,
   IN ULONG             SendFlags
)
{
   PMP_ADAPTER      pAdapter;
   UINT             packetCounter = 0;
   BOOLEAN          startTx = FALSE;
   NDIS_STATUS      Status = NDIS_STATUS_PENDING;
   UINT             numNBs = 0, i;
   PNET_BUFFER      currentNB, nextNB;
   PNET_BUFFER_LIST currentNBL, nextNBL;
   BOOLEAN          bAtDispatchLevel;
   PMPUSB_TX_CONTEXT_NBL txContext = NULL;
   PMPUSB_TX_CONTEXT_NB  nbContext = NULL;
   ULONG                 txContextSize;

   pAdapter = (PMP_ADAPTER)MiniportAdapterContext;

   // to be used for acquiring spin lock
   bAtDispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);

   // 1) Enqueue currentNBL to TxPendingList
   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> ---> MPTX_MiniportSendNetBufferLists: 0x%p FLG 0x%x nBusyTx %d (DISP %d)\n", pAdapter->PortName,
       NetBufferLists, SendFlags, pAdapter->nBusyTx, bAtDispatchLevel)
   );

   // For every packet in the list passed down from NDIS
   for (currentNBL = NetBufferLists; currentNBL != NULL; currentNBL = nextNBL)
   {
      nextNBL = NET_BUFFER_LIST_NEXT_NBL(currentNBL);
      NET_BUFFER_LIST_NEXT_NBL(currentNBL) = NULL;

      // Count number of NET buffers
      numNBs = 0;
      for (currentNB = NET_BUFFER_LIST_FIRST_NB(currentNBL);
           currentNB != NULL;
           currentNB = NET_BUFFER_NEXT_NB(currentNB))
      {
          ++numNBs;
      }

      if (numNBs == 0)
      {
         // error
         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPTX_MiniportSendNetBufferLists: no NET buf in 0x%p\n", pAdapter->PortName,
             currentNBL)
         );

         // fail current NBL
         MPUSB_CompleteNetBufferList
         (
            pAdapter,
            currentNBL,
            NDIS_STATUS_FAILURE,
            ((bAtDispatchLevel == TRUE)? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0),
            NULL
         );
         continue;
      }

      txContextSize = sizeof(MPUSB_TX_CONTEXT_NBL)+(sizeof(MPUSB_TX_CONTEXT_NB)*numNBs);
      txContext = ExAllocatePoolWithTag
                  (
                     NonPagedPool,
                     txContextSize,
                     (ULONG)'006N'
                  );

      if (txContext == NULL)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPTX_MiniportSendNetBufferLists: NO_MEM\n", pAdapter->PortName)
         );

         // fail current NBL
         MPUSB_CompleteNetBufferList
         (
            pAdapter,
            currentNBL,
            NDIS_STATUS_RESOURCES,
            ((bAtDispatchLevel == TRUE)? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0),
            NULL
         );
         continue;
      }
      nbContext = (PMPUSB_TX_CONTEXT_NB)((PCHAR)txContext + sizeof(MPUSB_TX_CONTEXT_NBL));

      QCNET_DbgPrint
      (
         MP_DBG_MASK_WRITE,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPTX_MiniportSendNetBufferLists: NBL 0x%p QNBL 0x%p QNB 0x%p (%d) totalSz %dB Flags 0x%x\n",
           pAdapter->PortName, currentNBL, txContext, nbContext, numNBs, txContextSize,
           NET_BUFFER_LIST_FLAGS(currentNBL))
      );

      /******** For dual IP, this does not apply **********
      #ifdef NDIS620_MINIPORT
      #ifdef QC_IP_MODE

      // Verify NBL flag for raw-IP mode
      if ((pAdapter->NdisMediumType == NdisMediumWirelessWan) &&
          (pAdapter->IPModeEnabled == TRUE))
      {
         BOOLEAN bRawIP = TRUE;

         // ntohs() does the same thing as ConvertToNetworkByteOrder()
         if (pAdapter->IPSettings.IPV4.Flags != 0)
         {
            bRawIP = NBL_TEST_FLAG(currentNBL, NDIS_NBL_FLAGS_IS_IPV4);
         }
         else if (pAdapter->IPSettings.IPV6.Flags != 0)
         {
            bRawIP = NBL_TEST_FLAG(currentNBL, NDIS_NBL_FLAGS_IS_IPV6);
         }
         else
         {
            bRawIP = FALSE;
         }

         if (bRawIP == FALSE)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_WRITE,
               MP_DBG_LEVEL_ERROR,
               ("<%s> MPTX_MiniportSendNetBufferLists: IPO error: wrong NBL flag 0x%x V4(%d) V6(%d)\n",
                 pAdapter->PortName, NET_BUFFER_LIST_FLAGS(currentNBL),
                 pAdapter->IPSettings.IPV4.Flags, pAdapter->IPSettings.IPV6.Flags)
            );
         }
      }

      #endif // QC_IP_MODE
      #endif // NDIS620_MINIPORT
      *********************************************************/

      txContext->Adapter      = pAdapter;
      txContext->NBL          = currentNBL;
      txContext->TotalNBs     = numNBs;
      txContext->NumProcessed = 0;
      txContext->NumCompleted = 0;
      txContext->AbortFlag    = 0;
      txContext->TxBytesSucceeded = 0;
      txContext->TxBytesFailed    = 0;
      txContext->TxFramesFailed   = 0;
      txContext->TxFramesDropped  = 0;

      // euqueue the packet chain (currentNBL)
      NET_BUFFER_LIST_STATUS(currentNBL) = NDIS_STATUS_SUCCESS;

      NdisAcquireSpinLock(&pAdapter->TxLock);
      startTx = IsListEmpty(&pAdapter->TxPendingList);
      NdisReleaseSpinLock(&pAdapter->TxLock);

      i = 0;
      for (currentNB = NET_BUFFER_LIST_FIRST_NB(currentNBL);
           currentNB != NULL;
           currentNB = nextNB)
      {
         nextNB = NET_BUFFER_NEXT_NB(currentNB);

         nbContext[i].NB         = currentNB;   // for information purpose
         nbContext[i].NBL        = txContext;
         nbContext[i].Index      = i;  // NB index
         nbContext[i].Completed  = 0;
         nbContext[i].DataLength = 0;
         i++;
      }

      
      QCNET_DbgPrint
      (
         MP_DBG_MASK_WRITE,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPTX_MiniportSendNetBufferLists: EnQ 0x%p (%d NBs)\n", pAdapter->PortName,
          currentNBL, numNBs)
      );

         NdisAcquireSpinLock(&pAdapter->TxLock);

      // set up NBs
      for (i = 0; i < numNBs; i++)
      {

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QC_DUAL_IP_FC
         if (MPMAIN_IsFlowControlledData(pAdapter, (PLIST_ENTRY)(&(nbContext[i].List))) == TRUE)
         {
            InterlockedIncrement(&(pAdapter->nBusyTx));
            QCNET_DbgPrint
            (
               MP_DBG_MASK_WRITE,
               MP_DBG_LEVEL_TRACE,
               ("<%s> MPTX_MiniportTxPackets=>0x%p (flow controlled, nBusyTx %d)\n", pAdapter->PortName,
                 &(nbContext[i].List), pAdapter->nBusyTx)
            );
            continue;
         }
#endif // QC_DUAL_IP_FC
#endif // QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
         if (MPMAIN_QMAPIsFlowControlledData(pAdapter, (PLIST_ENTRY)(&(nbContext[i].List))) == TRUE)
         {
            InterlockedIncrement(&(pAdapter->nBusyTx));
            QCNET_DbgPrint
            (
               MP_DBG_MASK_WRITE,
               MP_DBG_LEVEL_TRACE,
               ("<%s> MPTX_MiniportTxPackets=>0x%p (flow controlled, nBusyTx %d)\n", pAdapter->PortName,
                 &(nbContext[i].List), pAdapter->nBusyTx)
            );
            continue;
         }
#endif
         
         InsertTailList(&pAdapter->TxPendingList, &(nbContext[i].List));
         InterlockedIncrement(&(pAdapter->nBusyTx));  // num of NB
         
      }

      NdisReleaseSpinLock(&pAdapter->TxLock);
      
      // NOTE: From this point on, access to the NBL might be invalid because
      //       the it could be processed and completed by other thread.

      packetCounter += i;  // total packets/NBs

      if (startTx)
      {
         KeSetEvent(&pAdapter->MPThreadTxEvent, IO_NO_INCREMENT, FALSE);
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--- MPTX_MiniportSendNetBufferLists (TotalPkts: %d::%d)\n", pAdapter->PortName,
        packetCounter, pAdapter->nBusyTx)
   );

}  // MPTX_MiniportSendNetBufferListsEx

VOID MPTX_MiniportCancelSend
(
    NDIS_HANDLE MiniportAdapterContext,
    PVOID       CancelId
)
{ 
   PMP_ADAPTER           pAdapter = (PMP_ADAPTER)MiniportAdapterContext;
   PNET_BUFFER_LIST      netBufferList;
   PVOID                 netBufListCancelId;
   PLIST_ENTRY           headOfList, peekEntry, nextEntry;
   PMPUSB_TX_CONTEXT_NBL txNbl;
   LIST_ENTRY            cancelQueue;
   int                   numCancelled = 0;

   
   if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| 
          (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0) || (pAdapter->QMAPEnabledV4 == TRUE) 

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
        || (pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE)
#endif
#endif
   )
   {
       
       PMPUSB_TX_CONTEXT_NB txNb;
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_TRACE,
          ("<%s> -->CancelSendNetBufferLists: 0x%p/%d\n",
            pAdapter->PortName, CancelId, (int)MaxNetBufferListInfo)
       );

       InitializeListHead(&cancelQueue);

       NdisAcquireSpinLock(&pAdapter->TxLock);

       if (!IsListEmpty(&pAdapter->TxPendingList))
       {
          headOfList = &pAdapter->TxPendingList;
          peekEntry  = headOfList->Flink;

          while (peekEntry != headOfList)
          {
             nextEntry = peekEntry->Flink;
             txNb = CONTAINING_RECORD
                  (
                     peekEntry,
                     MPUSB_TX_CONTEXT_NB,
                     List
                  );
             if (((PMPUSB_TX_CONTEXT_NBL)(txNb->NBL))->Canceld != 1)
             {
                 netBufferList = ((PMPUSB_TX_CONTEXT_NBL)(txNb->NBL))->NBL;
                 netBufListCancelId = NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(netBufferList);

                 if (netBufListCancelId == CancelId)
                 {
                    // place the NBL onto cancelQueue
                    RemoveEntryList(&txNb->List);
                    InsertTailList(&cancelQueue, &txNb->List);
                    ((PMPUSB_TX_CONTEXT_NBL)(txNb->NBL))->Canceld = 1;
                 }
                 peekEntry = nextEntry;
             }
             else
             {
                // place the NBL onto cancelQueue
                RemoveEntryList(&txNb->List);
                peekEntry = nextEntry;
             }
          }
       }

       NdisReleaseSpinLock(&pAdapter->TxLock);

       // now, cancel the items in cancelQueue
       while (!IsListEmpty(&cancelQueue))
       {
          headOfList = RemoveHeadList(&cancelQueue);
          txNb = CONTAINING_RECORD
                  (
                     headOfList,
                     MPUSB_TX_CONTEXT_NB,
                     List
                  );
#if 0
          MPUSB_CompleteNetBufferList
          (
             pAdapter,
             ((PMPUSB_TX_CONTEXT_NBL)(txNb->NBL))->NBL,
             NDIS_STATUS_REQUEST_ABORTED,
             0,  // unknown IRQL
             (PMPUSB_TX_CONTEXT_NBL)txNb->NBL
          );
#endif
          ((PMPUSB_TX_CONTEXT_NBL)txNb->NBL)->NdisStatus = NDIS_STATUS_REQUEST_ABORTED;
          MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, txNb, TRUE);

          ++numCancelled;
       }

       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_TRACE,
          ("<%s> <--CancelSendNetBufferLists: 0x%p/%d(%d)\n",
            pAdapter->PortName, CancelId, (int)MaxNetBufferListInfo, numCancelled)
       );   
   }
   else
   {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_TRACE,
          ("<%s> -->CancelSendNetBufferLists: 0x%p/%d\n",
            pAdapter->PortName, CancelId, (int)MaxNetBufferListInfo)
       );

       InitializeListHead(&cancelQueue);

       NdisAcquireSpinLock(&pAdapter->TxLock);

       if (!IsListEmpty(&pAdapter->TxPendingList))
       {
          headOfList = &pAdapter->TxPendingList;
          peekEntry  = headOfList->Flink;

          while (peekEntry != headOfList)
          {
             nextEntry = peekEntry->Flink;
             txNbl = CONTAINING_RECORD
                     (
                        peekEntry,
                        MPUSB_TX_CONTEXT_NBL,
                        List
                     );
             netBufferList = txNbl->NBL;
             netBufListCancelId = NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(netBufferList);

             if (netBufListCancelId == CancelId)
             {
                // place the NBL onto cancelQueue
                RemoveEntryList(&txNbl->List);
                InsertTailList(&cancelQueue, &txNbl->List);
             }
             peekEntry = nextEntry;
          }
       }

       NdisReleaseSpinLock(&pAdapter->TxLock);

       // now, cancel the items in cancelQueue
       while (!IsListEmpty(&cancelQueue))
       {
          headOfList = RemoveHeadList(&cancelQueue);
          txNbl = CONTAINING_RECORD
                  (
                     headOfList,
                     MPUSB_TX_CONTEXT_NBL,
                     List
                  );

          MPUSB_CompleteNetBufferList
          (
             pAdapter,
             txNbl->NBL,
             NDIS_STATUS_REQUEST_ABORTED,
             0,  // unknown IRQL
             txNbl
          );

          ++numCancelled;
       }

       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_TRACE,
          ("<%s> <--CancelSendNetBufferLists: 0x%p/%d(%d)\n",
            pAdapter->PortName, CancelId, (int)MaxNetBufferListInfo, numCancelled)
       );
   }
}  // MPTX_MiniportCancelSend

BOOLEAN MPTX_ProcessPendingTxQueueTlpEx(IN PMP_ADAPTER pAdapter)
{
   PLIST_ENTRY  pList;
   NDIS_STATUS  status;
   BOOLEAN      kicker = FALSE;
   UINT         processed;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->_ProcessPendingTxQueueTlpEx\n", pAdapter->PortName)
   );

   processed = 0;

   NdisAcquireSpinLock(&pAdapter->TxLock);

   while (!IsListEmpty(&pAdapter->TxPendingList))
   {
      // Move the buf list from the PendingList to the BusyList
      pList = RemoveHeadList(&pAdapter->TxPendingList);

      if (((pAdapter->Flags & fMP_ANY_FLAGS) == 0) &&
          (pAdapter->ToPowerDown == FALSE)         &&
          (MPMAIN_InPauseState(pAdapter) == FALSE))
      {
         InsertTailList(&pAdapter->TxBusyList, pList); // TxPendingList => TxBusyList

         NdisReleaseSpinLock( &pAdapter->TxLock );

         MP_USBTxPacketEx(pAdapter, pList, 0, FALSE);
         ++processed;
      }
      else
      {
         PMPUSB_TX_CONTEXT_NB qcNb;

         // get NetBufferList
         qcNb = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NB, List);

         kicker = TRUE;

         // If the Busy list is empty we can just complete this packet
         // This is to complete the TX pkt in order?
         if (IsListEmpty(&pAdapter->TxBusyList))
         {

            QCNET_DbgPrint
            (
               MP_DBG_MASK_WRITE,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> ProcessPendingTxQueueEx: MP not ready, comp NBL 0x%p\n",
                 pAdapter->PortName, ((PMPUSB_TX_CONTEXT_NBL)(qcNb->NBL))->NBL)
            );
            MPUSB_CompleteNetBufferList
            (
               pAdapter,
               ((PMPUSB_TX_CONTEXT_NBL)(qcNb->NBL))->NBL,
               NDIS_STATUS_FAILURE,
               NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
               (PMPUSB_TX_CONTEXT_NBL)qcNb->NBL
            );

            NdisReleaseSpinLock(&pAdapter->TxLock);
         }
         else
         {
            // But if the BusyList is not empty we need to wait it out and try again next time.
            QCNET_DbgPrint
            (
               MP_DBG_MASK_WRITE,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> ProcessPendingTxQueueEx: MP not ready, NBL 0x%p back to P\n",
                 pAdapter->PortName, ((PMPUSB_TX_CONTEXT_NBL)(qcNb->NBL))->NBL)
            );
            InsertHeadList(&pAdapter->TxPendingList, pList);

            // shall we wait a bit here?
            NdisReleaseSpinLock( &pAdapter->TxLock );
            MPMAIN_Wait(-(100 * 1000L)); // 10ms
         }
      }

      NdisAcquireSpinLock( &pAdapter->TxLock );
   }  // while

   NdisReleaseSpinLock( &pAdapter->TxLock );

   if (kicker)
   {
      MPWork_ScheduleWorkItem(pAdapter);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--_ProcessPendingTxQueueEx (processed: %d)\n", pAdapter->PortName, processed)
   );

   return (processed > 0);
} // MPTX_ProcessPendingTxQueueTlpEx


VOID MPTX_MiniportSendNetBufferLists
(
   IN NDIS_HANDLE       MiniportAdapterContext,
   IN PNET_BUFFER_LIST  NetBufferLists,
   IN NDIS_PORT_NUMBER  PortNumber,
   IN ULONG             SendFlags
)
{
   PMP_ADAPTER      pAdapter;
   UINT             packetCounter = 0;
   BOOLEAN          startTx = FALSE;
   NDIS_STATUS      Status = NDIS_STATUS_PENDING;
   UINT             numNBs = 0, i;
   PNET_BUFFER      currentNB, nextNB;
   PNET_BUFFER_LIST currentNBL, nextNBL;
   BOOLEAN          bAtDispatchLevel;
   PMPUSB_TX_CONTEXT_NBL txContext = NULL;
   PMPUSB_TX_CONTEXT_NB  nbContext = NULL;
   ULONG                 txContextSize;

   pAdapter = (PMP_ADAPTER)MiniportAdapterContext;
   
   if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| 
       (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0) || (pAdapter->QMAPEnabledV4 == TRUE) 
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
       || (pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE)  
#endif
#endif
      )
       
   {
      MPTX_MiniportSendNetBufferListsEx( MiniportAdapterContext, NetBufferLists, PortNumber,SendFlags );
      return;
   }
   
   pAdapter = (PMP_ADAPTER)MiniportAdapterContext;

   // to be used for acquiring spin lock
   bAtDispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);

   // 1) Enqueue currentNBL to TxPendingList
   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> ---> MPTX_MiniportSendNetBufferLists: 0x%p FLG 0x%x nBusyTx %d (DISP %d)\n", pAdapter->PortName,
       NetBufferLists, SendFlags, pAdapter->nBusyTx, bAtDispatchLevel)
   );

   // For every packet in the list passed down from NDIS
   for (currentNBL = NetBufferLists; currentNBL != NULL; currentNBL = nextNBL)
   {
      nextNBL = NET_BUFFER_LIST_NEXT_NBL(currentNBL);
      NET_BUFFER_LIST_NEXT_NBL(currentNBL) = NULL;

      // Count number of NET buffers
      numNBs = 0;
      for (currentNB = NET_BUFFER_LIST_FIRST_NB(currentNBL);
           currentNB != NULL;
           currentNB = NET_BUFFER_NEXT_NB(currentNB))
      {
          ++numNBs;
      }

      if (numNBs == 0)
      {
         // error
         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPTX_MiniportSendNetBufferLists: no NET buf in 0x%p\n", pAdapter->PortName,
             currentNBL)
         );

         // fail current NBL
         MPUSB_CompleteNetBufferList
         (
            pAdapter,
            currentNBL,
            NDIS_STATUS_FAILURE,
            ((bAtDispatchLevel == TRUE)? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0),
            NULL
         );
         continue;
      }

      txContextSize = sizeof(MPUSB_TX_CONTEXT_NBL)+(sizeof(MPUSB_TX_CONTEXT_NB)*numNBs);
      txContext = ExAllocatePoolWithTag
                  (
                     NonPagedPool,
                     txContextSize,
                     (ULONG)'006N'
                  );

      if (txContext == NULL)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPTX_MiniportSendNetBufferLists: NO_MEM\n", pAdapter->PortName)
         );

         // fail current NBL
         MPUSB_CompleteNetBufferList
         (
            pAdapter,
            currentNBL,
            NDIS_STATUS_RESOURCES,
            ((bAtDispatchLevel == TRUE)? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0),
            NULL
         );
         continue;
      }
      nbContext = (PMPUSB_TX_CONTEXT_NB)((PCHAR)txContext + sizeof(MPUSB_TX_CONTEXT_NBL));

      QCNET_DbgPrint
      (
         MP_DBG_MASK_WRITE,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPTX_MiniportSendNetBufferLists: NBL 0x%p QNBL 0x%p QNB 0x%p (%d) totalSz %dB Flags 0x%x\n",
           pAdapter->PortName, currentNBL, txContext, nbContext, numNBs, txContextSize,
           NET_BUFFER_LIST_FLAGS(currentNBL))
      );

      /******** For dual IP, this does not apply **********
      #ifdef NDIS620_MINIPORT
      #ifdef QC_IP_MODE

      // Verify NBL flag for raw-IP mode
      if ((pAdapter->NdisMediumType == NdisMediumWirelessWan) &&
          (pAdapter->IPModeEnabled == TRUE))
      {
         BOOLEAN bRawIP = TRUE;

         // ntohs() does the same thing as ConvertToNetworkByteOrder()
         if (pAdapter->IPSettings.IPV4.Flags != 0)
         {
            bRawIP = NBL_TEST_FLAG(currentNBL, NDIS_NBL_FLAGS_IS_IPV4);
         }
         else if (pAdapter->IPSettings.IPV6.Flags != 0)
         {
            bRawIP = NBL_TEST_FLAG(currentNBL, NDIS_NBL_FLAGS_IS_IPV6);
         }
         else
         {
            bRawIP = FALSE;
         }

         if (bRawIP == FALSE)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_WRITE,
               MP_DBG_LEVEL_ERROR,
               ("<%s> MPTX_MiniportSendNetBufferLists: IPO error: wrong NBL flag 0x%x V4(%d) V6(%d)\n",
                 pAdapter->PortName, NET_BUFFER_LIST_FLAGS(currentNBL),
                 pAdapter->IPSettings.IPV4.Flags, pAdapter->IPSettings.IPV6.Flags)
            );
         }
      }

      #endif // QC_IP_MODE
      #endif // NDIS620_MINIPORT
      *********************************************************/

      txContext->Adapter      = pAdapter;
      txContext->NBL          = currentNBL;
      txContext->TotalNBs     = numNBs;
      txContext->NumProcessed = 0;
      txContext->NumCompleted = 0;
      txContext->AbortFlag    = 0;
      txContext->TxBytesSucceeded = 0;
      txContext->TxBytesFailed    = 0;
      txContext->TxFramesFailed   = 0;
      txContext->TxFramesDropped  = 0;

      // euqueue the packet chain (currentNBL)
      NET_BUFFER_LIST_STATUS(currentNBL) = NDIS_STATUS_SUCCESS;

      // set up NBs
      i = 0;
      for (currentNB = NET_BUFFER_LIST_FIRST_NB(currentNBL);
           currentNB != NULL;
           currentNB = nextNB)
      {
         nextNB = NET_BUFFER_NEXT_NB(currentNB);

         nbContext[i].NB         = currentNB;   // for information purpose
         nbContext[i].NBL        = txContext;
         nbContext[i].Index      = i;  // NB index
         nbContext[i].Completed  = 0;
         nbContext[i].DataLength = 0;
         ++i;
      }

      QCNET_DbgPrint
      (
         MP_DBG_MASK_WRITE,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPTX_MiniportSendNetBufferLists: EnQ 0x%p (%d NBs)\n", pAdapter->PortName,
          currentNBL, numNBs)
      );

      NdisAcquireSpinLock(&pAdapter->TxLock);
      startTx = IsListEmpty(&pAdapter->TxPendingList);

      #ifdef QCUSB_MUX_PROTOCOL
      #ifdef QC_DUAL_IP_FC
      if (MPMAIN_IsFlowControlledData(pAdapter, (PLIST_ENTRY)(&(txContext->List))) == TRUE)
      {
         InterlockedIncrement(&(pAdapter->nBusyTx));
         NdisReleaseSpinLock(&pAdapter->TxLock);
         packetCounter += i;  // total packets/NBs
         if (startTx)
         {
            KeSetEvent(&pAdapter->MPThreadTxEvent, IO_NO_INCREMENT, FALSE);
         }
         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> MPTX_MiniportTxPackets=>0x%p (flow controlled, nBusyTx %d)\n", pAdapter->PortName,
              &(txContext->List), pAdapter->nBusyTx)
         );
         continue;
      }
      #endif // QC_DUAL_IP_FC
      #endif // QCUSB_MUX_PROTOCOL

#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
      if (MPMAIN_QMAPIsFlowControlledData(pAdapter, (PLIST_ENTRY)(&(txContext->List))) == TRUE)
      {
         InterlockedIncrement(&(pAdapter->nBusyTx));
         NdisReleaseSpinLock(&pAdapter->TxLock);
         packetCounter += i;  // total packets/NBs
         if (startTx)
         {
            KeSetEvent(&pAdapter->MPThreadTxEvent, IO_NO_INCREMENT, FALSE);
         }
         QCNET_DbgPrint
         (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> MPTX_MiniportTxPackets=>0x%p (flow controlled, nBusyTx %d)\n", pAdapter->PortName,
              &(txContext->List), pAdapter->nBusyTx)
         );
         continue;
      }
#endif
      
      InsertTailList(&pAdapter->TxPendingList, &(txContext->List));
      InterlockedIncrement(&(pAdapter->nBusyTx));  // num of NBLs
      NdisReleaseSpinLock(&pAdapter->TxLock);

      // NOTE: From this point on, access to the NBL might be invalid because
      //       the it could be processed and completed by other thread.

      packetCounter += i;  // total packets/NBs

      if (startTx)
      {
         KeSetEvent(&pAdapter->MPThreadTxEvent, IO_NO_INCREMENT, FALSE);
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--- MPTX_MiniportSendNetBufferLists (TotalPkts: %d::%d)\n", pAdapter->PortName,
        packetCounter, pAdapter->nBusyTx)
   );

}  // MPTX_MiniportSendNetBufferLists

BOOLEAN MPTX_ProcessPendingTxQueueEx(IN PMP_ADAPTER pAdapter)
{
   PLIST_ENTRY  pList;
   NDIS_STATUS  status;
   BOOLEAN      kicker = FALSE;
   UINT         processed;

   if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| 
       (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0) || (pAdapter->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
           || (pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE)   
#endif
#endif
   )
   {
      return MPTX_ProcessPendingTxQueueTlpEx( pAdapter );
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->_ProcessPendingTxQueueEx\n", pAdapter->PortName)
   );

   processed = 0;

   NdisAcquireSpinLock(&pAdapter->TxLock);

   while (!IsListEmpty(&pAdapter->TxPendingList))
   {
      // Move the buf list from the PendingList to the BusyList
      pList = RemoveHeadList(&pAdapter->TxPendingList);

      if (((pAdapter->Flags & fMP_ANY_FLAGS) == 0) &&
          (pAdapter->ToPowerDown == FALSE)         &&
          (MPMAIN_InPauseState(pAdapter) == FALSE))
      {
         InsertTailList(&pAdapter->TxBusyList, pList); // TxPendingList => TxBusyList

         NdisReleaseSpinLock( &pAdapter->TxLock );

         MP_USBTxPacketEx(pAdapter, pList, 0, FALSE);
         ++processed;
      }
      else
      {
         PMPUSB_TX_CONTEXT_NBL qcNbl;

         // get NetBufferList
         qcNbl = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NBL, List);

         kicker = TRUE;

         // If the Busy list is empty we can just complete this packet
         // This is to complete the TX pkt in order?
         if (IsListEmpty(&pAdapter->TxBusyList))
         {

            QCNET_DbgPrint
            (
               MP_DBG_MASK_WRITE,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> ProcessPendingTxQueueEx: MP not ready, comp NBL 0x%p\n",
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

            NdisReleaseSpinLock(&pAdapter->TxLock);
         }
         else
         {
            // But if the BusyList is not empty we need to wait it out and try again next time.
            QCNET_DbgPrint
            (
               MP_DBG_MASK_WRITE,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> ProcessPendingTxQueueEx: MP not ready, NBL 0x%p back to P\n",
                 pAdapter->PortName, qcNbl->NBL)
            );
            InsertHeadList(&pAdapter->TxPendingList, pList);

            // shall we wait a bit here?
            NdisReleaseSpinLock( &pAdapter->TxLock );
            MPMAIN_Wait(-(100 * 1000L)); // 10ms
         }
      }

      NdisAcquireSpinLock( &pAdapter->TxLock );
   }  // while

   NdisReleaseSpinLock( &pAdapter->TxLock );

   if (kicker)
   {
      MPWork_ScheduleWorkItem(pAdapter);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_WRITE,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--_ProcessPendingTxQueueEx (processed: %d)\n", pAdapter->PortName, processed)
   );

   return (processed > 0);
} // MPTX_ProcessPendingTxQueueEx

#endif // NDIS60_MINIPORT
