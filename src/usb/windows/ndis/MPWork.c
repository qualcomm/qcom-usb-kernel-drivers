/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             M P W O R K . C

GENERAL DESCRIPTION
  This module contains the NDIS_WORK_ITEMS used to process items off of
  and onto the work queues used to effect asyncronization between NDIS and
  the USB devices

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "MPMain.h"
#include "MPWork.h"
#include "MPUsb.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPWork.tmh"

#endif  // EVENT_TRACING

/*********************************************************************
 *  Returns TRUE if any packets were completed, else FALSE
 **********************************************************************/
BOOLEAN MPWork_IndicateCompleteTx(PMP_ADAPTER pAdapter)
{
    PNDIS_PACKET pNdisPacket;
    PLIST_ENTRY  pList;
    ULONG        completed = 0;
    NDIS_STATUS  ndisStatus;

    // Get ownership of the Tx Lists
    NdisAcquireSpinLock(&pAdapter->TxLock);

    // For each packet on the complete list
    while (!IsListEmpty(&pAdapter->TxCompleteList))
    {
        // get the packet off the Tx Complete list
        pList = RemoveHeadList( &pAdapter->TxCompleteList );
        InterlockedDecrement(&(pAdapter->nBusyTx));
        pNdisPacket = ListItemToPacket( pList );
        ndisStatus = NDIS_GET_PACKET_STATUS(pNdisPacket);

        if (ndisStatus == NDIS_STATUS_SUCCESS)
        {
            ++(pAdapter->GoodTransmits);
        }
        else
        {
            ++(pAdapter->BadTransmits);
        }

        NdisReleaseSpinLock( &pAdapter->TxLock );

        QCNET_DbgPrint
        (
           MP_DBG_MASK_WRITE,
           MP_DBG_LEVEL_TRACE,
           ("<%s> TNDISP: 0x%p (0x%x)\n", pAdapter->PortName,
             pNdisPacket, ndisStatus)
        );

        NdisMSendComplete(pAdapter->AdapterHandle, pNdisPacket, ndisStatus);

        // Take note that we did some work this pass so we will attempt 
        // another pass before leaving the thread.
        ++completed;

        // Get the lock back before looking at the transmit lists again
        NdisAcquireSpinLock( &pAdapter->TxLock );
    }
    // Be sure to let go
    NdisReleaseSpinLock( &pAdapter->TxLock );

    if( completed > 0 )
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_WRITE,
          MP_DBG_LEVEL_TRACE,
          ("<%s> MPWork_IndicateCompleteTx completed: %d\n", pAdapter->PortName, completed)
       );
    }
    return( completed > 0 );
}  // MPWork_IndicateCompleteTx


BOOLEAN MPWork_PostDataReads(PMP_ADAPTER  pAdapter)
{
    PLIST_ENTRY  pList;
    ULONG posted = 0;

    // Don't read data if media is not connected
    if (pAdapter->ulMediaConnectStatus != NdisMediaStateConnected)
    {
       /****
       QCNET_DbgPrint
       (
          MP_DBG_MASK_READ,
          MP_DBG_LEVEL_TRACE,
          ("<%s> MPWork_PostDataReads: disconnected, no posting\n", pAdapter->PortName)
       );
       ****/
       return FALSE;
    }

    // Get ownership of the receive lists
    NdisAcquireSpinLock( &pAdapter->RxLock );

    // For each receive in the free list
    while( ((pAdapter->Flags & fMP_ANY_FLAGS) == 0) &&
           (!IsListEmpty( &pAdapter->RxFreeList ))  &&
           (MPMAIN_InPauseState(pAdapter) == FALSE) )
    {
        // Get the packet off the RxFreeList
        pList = RemoveHeadList( &pAdapter->RxFreeList );

        // Move the packet to the RxPending completed list
        InsertTailList( &pAdapter->RxPendingList, pList );
        InterlockedIncrement(&(pAdapter->nBusyRx));
        InterlockedDecrement(&(pAdapter->nRxFreeInMP));

        // Still try to minimize the ownership period
        NdisReleaseSpinLock( &pAdapter->RxLock );

        // Now actually call down the stack with this empty packet.
        // PostPacket will do a IOCallDriver to QC USB, we don't
        // expect the IRP to complete until a packet is available
        InterlockedIncrement(&(pAdapter->nRxPendingInUsb));
        MP_USBPostPacketRead(pAdapter, pList);

        // Take note that we did some work this pass so we will attempt 
        // another pass before leaving the thread.
        ++posted;

        // Get control again as we loop back to top
        NdisAcquireSpinLock( &pAdapter->RxLock );
    }

    NdisReleaseSpinLock( &pAdapter->RxLock );

    if (posted > 0)
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_READ,
          MP_DBG_LEVEL_TRACE,
          ("<%s> MPWork_PostDataReads posted: %d\n", pAdapter->PortName, posted)
       );
    }

    return( posted > 0 );
}  // MPWork_PostDataReads


BOOLEAN MPWork_PostControlReads(PMP_ADAPTER  pAdapter)
{
    PMP_OID_READ pOID;
    PLIST_ENTRY  pList;
    ULONG posted = 0;

    // Take control of the Ctrl Read lists
    NdisAcquireSpinLock( &pAdapter->CtrlReadLock );
    // While there are free reads to send down
    while( ((pAdapter->Flags & fMP_ANY_FLAGS) == 0) && (!IsListEmpty( &pAdapter->CtrlReadFreeList )) )
    {

        // Get the packet off the CtrlReadFreeList
        pList = RemoveHeadList( &pAdapter->CtrlReadFreeList );
        // Move the packet to the RxPending completed list
        InsertTailList( &pAdapter->CtrlReadPendingList, pList );
        InterlockedIncrement(&(pAdapter->nBusyRCtrl));

        // Give others a chance at the lists while we are processing
        NdisReleaseSpinLock( &pAdapter->CtrlReadLock );

        pOID = CONTAINING_RECORD(pList, MP_OID_READ, List);

#ifndef QCQMI_SUPPORT
        // TxID will be set by QC USB to tell us what/if a request is
        // beining satisified
        pOID->OID.TransactionID = 0;

        // Use IoCallDriver to post and IRP down to QC USB
        MP_USBPostControlRead( pAdapter, &pOID->OID );
#else
        MP_USBPostControlRead( pAdapter, &pOID->QMI );
#endif

        // Take note that we did some work this pass so we will attempt 
        // another pass before leaving the thread.
        ++posted;

        // Get control again before we loop
        NdisAcquireSpinLock( &pAdapter->CtrlReadLock );
    }

    // Be sure to let go
    NdisReleaseSpinLock( &pAdapter->CtrlReadLock );

    if ( posted > 0 )
    {
       KeSetEvent(&pAdapter->ControlReadPostedEvent, IO_NO_INCREMENT, FALSE);
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_TRACE,
          ("<%s> MP_PostCtrlReads posted: %d\n", pAdapter->PortName, posted)
       );
    }

    return posted > 0;
}

PMP_OID_WRITE MPWork_FindRequest(PMP_ADAPTER pAdapter, ULONG ulTxID)
{
    PMP_OID_WRITE currentOID = NULL;
    PLIST_ENTRY    listHead, thisEntry;

    // Be sure you own the OIDLock before you get here!!!!

    // Only if there are any completed OIDS to look at
    if( !IsListEmpty( &pAdapter->OIDWaitingList ) )
    {

        // Start at the head of the list
        listHead = &pAdapter->OIDWaitingList;
        thisEntry = listHead->Flink;

        // And while we haven't reached the end and the TxIDs do not match...
        while (thisEntry != listHead)
        {
            currentOID = CONTAINING_RECORD( thisEntry, MP_OID_WRITE, List );
            if (currentOID->OID.TransactionID == ulTxID)
            {
               break;
            }

            // Advance to the next OID in the completed list
            thisEntry = thisEntry->Flink;
        }

        // If we are here and have exhaused the list we didn't find a match
        if( thisEntry == listHead )
        {
            currentOID = NULL;
        }
    }

    // Return what we found to the caller
    return currentOID;
}

#ifdef DBG
VOID dumpOIDList( PMP_ADAPTER pAdapter )
{
    PMP_OID_WRITE currentOID = NULL;
    PLIST_ENTRY    listHead, thisEntry;
    INT oidIndx = 0;

    // Be sure you own the OIDLock before you get here!!!!

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> dumpOIDList: Pending OIDS:\n", pAdapter->PortName)
    );
    // Only if there are any incomplete OIDS to look at
    if( !IsListEmpty( &pAdapter->OIDWaitingList ) )
    {

       // Start at the head of the list
       listHead = &pAdapter->OIDWaitingList;
       thisEntry = listHead->Flink;
       currentOID = CONTAINING_RECORD( thisEntry, MP_OID_WRITE, List );
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_TRACE,
          ("<%s> dumpOIDList: [%d] %d\n", pAdapter->PortName, oidIndx++, currentOID->OID.TransactionID)
       );

       // And while we haven't reached the end
       while( (thisEntry != listHead) )
       {
           // Advance to the next OID in the completed list
           thisEntry = thisEntry->Flink;
           currentOID = CONTAINING_RECORD( thisEntry, MP_OID_WRITE, List );
           QCNET_DbgPrint
           (
              MP_DBG_MASK_CONTROL,
              MP_DBG_LEVEL_TRACE,
              ("<%s> dumpOIDList: [%d] %d\n", pAdapter->PortName, oidIndx++, currentOID->OID.TransactionID)
           );
       }
    }
    else
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_TRACE,
          ("<%s> dumpOIDList: empty list\n", pAdapter->PortName)
       );
    }
    return;
}
#endif

BOOLEAN MPWork_ResolveRequests(PMP_ADAPTER pAdapter)
{
    BOOLEAN     processed = FALSE;
    PLIST_ENTRY pList;
    PMP_OID_WRITE pMatchOid;
    PMP_OID_READ pOID;
    int  OIDStatus;
    int listSize;

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> -->MPWork_ResolveRequests\n", pAdapter->PortName)
    );
    // Get control of the Control lists
    NdisAcquireSpinLock( &pAdapter->CtrlReadLock );
    // While there are items in the Completed list
    while( !IsListEmpty( &pAdapter->CtrlReadCompleteList ) )
    {

        // Remove the itme form the list and point to the pOID record
        pList = RemoveHeadList( &pAdapter->CtrlReadCompleteList );

        // For now we can release the CtrlRead list
        NdisReleaseSpinLock( &pAdapter->CtrlReadLock );

        pOID = CONTAINING_RECORD( pList, MP_OID_READ, List );

#ifdef QCQMI_SUPPORT
        MPQMI_ProcessInboundQMI(pAdapter, pOID);  // Adapter, received IRP buffer
#else
        // Check the TxID of the completes ctrl read  we are about to process
        if( pOID->OID.TransactionID == 0 )
        {
            // This is a notification event so send it on to NDIS
            MyNdisMIndicateStatus(
                               pAdapter->AdapterHandle,
                               pOID->OID.Status,
                               ((pOID->OID.indication.Num_bytes > 0)? (PVOID)pOID->OID.Buffer : NULL),
                               pOID->OID.indication.Num_bytes
                               );

            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_TRACE,
               ("<%s> MPWork_ResolveRequests: Notification status: 0x%x\n", pAdapter->PortName, pOID->OID.Status)
            );
        }
        else
        {
            // This is a response to some earlier OID
            // find the oid in the OIDWaiting list by matching the transactionIDs
            // Since we'll be traversing the list we best own the spin lock
            NdisAcquireSpinLock( &pAdapter->OIDLock );
            pMatchOid = MPWork_FindRequest( pAdapter, pOID->OID.TransactionID );

            // If we found the OID we were looking for
            if( pMatchOid != NULL ) 
            {
                // If we found a matching request OID remove it from the OID Waiting list
                RemoveEntryList( &pMatchOid->List );
                // Let others in while we are doing the hard work

                NdisReleaseSpinLock( &pAdapter->OIDLock );

                // O.K, now we have the OID that requested the data (pMatchOID) and the
                // CtrlRead that returned the data (pList/pOID) 
                // copy the data as necessary and complete the pended OID

                OIDStatus = pOID->OID.Status;
/**********
                if( pOID->OID.response.Oid == OID_GEN_SUPPORTED_LIST )
                {
                    listSize = QCMP_SupportedOidListSize;
                    if ( pOID->OID.response.Bytes_needed != 0 )
                    {
                       if ( pMatchOid->RequestBytesNeeded != NULL )
                       {
                          listSize += pOID->OID.response.Bytes_needed;
                          *(pMatchOid->RequestBytesNeeded) = listSize;
                       }
                       OIDStatus = NDIS_STATUS_BUFFER_TOO_SHORT;
                    }
                    else
                    {
                       if ( pMatchOid->OID.BufferLength >= pOID->OID.response.Num_bytes + listSize )
                       {
                          if( pMatchOid->RequestInfoBuffer != NULL )
                          {
                             NdisMoveMemory( pMatchOid->RequestInfoBuffer, QCMPSupportedOidList, listSize );
                             NdisMoveMemory( (pMatchOid->RequestInfoBuffer + listSize), pOID->OID.Buffer, pOID->OID.response.Num_bytes );
                          }

                          if( pMatchOid->RequestBytesUsed != NULL )
                          {
                             listSize += pOID->OID.response.Num_bytes;
                             *(pMatchOid->RequestBytesUsed) = listSize;
                          }
                       } 
                       else
                       {
                          if ( pMatchOid->RequestBytesNeeded != NULL )
                          {
                             listSize += pOID->OID.response.Num_bytes;
                             *(pMatchOid->RequestBytesNeeded) = listSize;
                          }
                          OIDStatus = NDIS_STATUS_BUFFER_TOO_SHORT;
                       }
                    }
                 }
                 else
***********/
                 {
                    if ( pOID->OID.response.Bytes_needed != 0 )
                    {
                       if ( pMatchOid->RequestBytesNeeded != NULL )
                       {
                          *(pMatchOid->RequestBytesNeeded) = pOID->OID.response.Bytes_needed;
                       }
                       OIDStatus = NDIS_STATUS_BUFFER_TOO_SHORT;
                    }
                    else
                    {
                       if ( pMatchOid->OID.BufferLength >= pOID->OID.response.Num_bytes )
                       {
                          if ( pMatchOid->RequestInfoBuffer != NULL )
                          {
                             NdisMoveMemory( pMatchOid->RequestInfoBuffer, pOID->OID.Buffer, pOID->OID.response.Num_bytes );
                          }

                          if ( pMatchOid->RequestBytesUsed != NULL )
                          {
                             *(pMatchOid->RequestBytesUsed) = pOID->OID.response.Num_bytes;
                          }

                          if ( pMatchOid->RequestBytesNeeded != NULL )
                          {
                             *(pMatchOid->RequestBytesNeeded) = pOID->OID.response.Bytes_needed;
                          }
                       }
                       else
                       {
                          if ( pMatchOid->RequestBytesNeeded != NULL )
                          {
                             *(pMatchOid->RequestBytesNeeded) = pOID->OID.response.Num_bytes;
                          }
                          OIDStatus = NDIS_STATUS_BUFFER_TOO_SHORT;
                       }
                    }
                 }

                if( pMatchOid->OID.OidType == fMP_QUERY_OID )
                {
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_TRACE,
                       ("<%s> MPWork_ResolveRequests: Indicate Query Complete (Oid: 0x%x, Status: 0x%x)\n",
                         pAdapter->PortName, pOID->OID.response.Oid, OIDStatus)
                    );
                    MPOID_CompleteOid(pAdapter, OIDStatus, pMatchOid, pMatchOid->OID.OidType, FALSE);
                }
                else
                {
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_TRACE,
                       ("<%s> MPWork_ResolveRequests: Indicate Set Complete (Oid: 0x%x, Status: 0x%x)\n",
                         pAdapter->PortName, pOID->OID.response.Oid, OIDStatus)
                    );
                    MPOID_CompleteOid(pAdapter, OIDStatus, pMatchOid, pMatchOid->OID.OidType, FALSE);
                }

                // Put the OID we just processed back on the free list so it'll
                // posted down next time around in the work item
                pMatchOid->RequestInfoBuffer = (PUCHAR) NULL;
                pMatchOid->RequestBytesUsed = (PULONG) NULL;
                pMatchOid->RequestBytesNeeded = (PULONG) NULL;

                NdisAcquireSpinLock( &pAdapter->OIDLock );
                MPOID_CleanupOidCopy(pAdapter, pMatchOid);
                InsertTailList( &pAdapter->OIDFreeList, &pMatchOid->List );
                InterlockedDecrement(&(pAdapter->nBusyOID));
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL,
                   MP_DBG_LEVEL_TRACE,
                   ("<%s> MPWork_ResolveRequests: _ResolveRequests ok: OID=0x%x nBusyOID=%d\n",
                     pAdapter->PortName, pMatchOid->Oid, pAdapter->nBusyOID)
                );
                NdisReleaseSpinLock( &pAdapter->OIDLock );



                }
                else
                {
                   NdisReleaseSpinLock( &pAdapter->OIDLock );
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL,
                      MP_DBG_LEVEL_TRACE,
                      ("<%s> MPWork_ResolveRequests could not match a resp(TxID: %d) to a req\n",
                        pAdapter->PortName, pOID->OID.TransactionID)
                   );
#ifdef DBG
                   NdisAcquireSpinLock( &pAdapter->OIDLock );
                   dumpOIDList( pAdapter );
                   NdisReleaseSpinLock( &pAdapter->OIDLock );
#endif
                }
        }  // TID != 0

#endif // QCQMI_SUPPORT

        // We need to own the Ctrl Read lists again
        NdisAcquireSpinLock( &pAdapter->CtrlReadLock );

        // Put the item we just processed on the free list
        InsertTailList( &pAdapter->CtrlReadFreeList, pList );
        InterlockedDecrement(&(pAdapter->nBusyRCtrl));

        processed = TRUE;
    }  // while
    // Be sure to let go
    NdisReleaseSpinLock( &pAdapter->CtrlReadLock );

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> <--MPWork_ResolveRequests\n", pAdapter->PortName)
    );
    // No need to schedule the work item because it's running
    // returning true from here will assure everything (including reposting the
    // control reads) runs again.
    return processed;
}

BOOLEAN MPWork_PostWriteRequests( PMP_ADAPTER pAdapter )
{
    BOOLEAN     processed = FALSE;
    PLIST_ENTRY pList;
    PQCWRITEQUEUE QMIElement;    
    PQCQMI QMI;
    int listSize;

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> -->MPWork_ResolveRequests\n", pAdapter->PortName)
    );

    if ( pAdapter->IsQmiWorking == FALSE)
    {
        QCNET_DbgPrint
        (
           MP_DBG_MASK_CONTROL,
           MP_DBG_LEVEL_TRACE,
           ("<%s> QMI Not Working yet\n", pAdapter->PortName)
        );
        return processed;
    }
    // Get control of the Control lists
    NdisAcquireSpinLock( &pAdapter->CtrlWriteLock );
    // While there are items in the Completed list
    while( !IsListEmpty( &pAdapter->CtrlWriteList ) )
    {

           // Remove the itme form the list and point to the pOID record
           pList = RemoveHeadList( &pAdapter->CtrlWriteList );
        QMIElement = CONTAINING_RECORD( pList, QCWRITEQUEUE, QCWRITERequest);
        QMI = (PQCQMI)QMIElement->Buffer;
        if (pAdapter->PendingCtrlRequests[QMI->QMIType] < pAdapter->MaxPendingQMIReqs)
        {
           // For now we can release the CtrlRead list
           NdisReleaseSpinLock( &pAdapter->CtrlWriteLock );
            
           MP_USBSendControlRequest(pAdapter, QMIElement->Buffer, QMIElement->QMILength);
            QCNET_DbgPrint
            (
               (MP_DBG_MASK_OID_QMI),
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MP_USBSendControlRequest : Number of QMI requests pending for service %d is %d\n",
                 pAdapter->PortName,
                 QMI->QMIType,
                 pAdapter->PendingCtrlRequests[QMI->QMIType]
               )
            );
            NdisFreeMemory(QMIElement, sizeof(QCWRITEQUEUE) + QMIElement->QMILength, 0 );
            NdisAcquireSpinLock( &pAdapter->CtrlWriteLock );   
           InterlockedDecrement(&(pAdapter->nBusyWrite));
        }
        else
        {
            // Insert to the tail of the list back
            InsertTailList( &pAdapter->CtrlWriteList, pList );
           break;
        }
        processed = TRUE;
    }  // while
    // Be sure to let go
    NdisReleaseSpinLock( &pAdapter->CtrlWriteLock );

    return processed;
}

VOID MPWork_WorkItem(PNDIS_WORK_ITEM pWorkItem, PVOID pContext)
{
    BOOLEAN workDone = TRUE;
    PMP_ADAPTER pAdapter = (PMP_ADAPTER) pContext;
    // PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;
    LONG    loop;

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> --> MPWork_WorkItem\n", pAdapter->PortName)
    );

    loop = 0;
    // Keep looping as long as we are accomplishing something
    while( workDone )
    {
        if (QCMP_NDIS6_Ok == FALSE)
        {
           // First indicate any completed transmits
           workDone  = MPWork_IndicateCompleteTx( pAdapter ); 
        }
        else
        {
           workDone = FALSE;
        }

#ifdef QCMP_DISABLE_QMI
        if (pAdapter->DisableQMI == 0)
        {
#endif        
         // TODO:
         // if ((pDevExt->MuxInterface.MuxEnabled == 0x00) ||
         //   (pDevExt->MuxInterface.InterfaceNumber == pDevExt->MuxInterface.PhysicalInterfaceNumber))
         // {
        // And that any completed control reads are returned as well.
        workDone |= MPWork_PostControlReads( pAdapter );
         // }
#ifdef QCMP_DISABLE_QMI
        }
#endif

        // Now be sure that any completed receives are returned to QC USB
        workDone |= MPWork_PostDataReads( pAdapter );

        // Now be sure that any completed receives are returned to QC USB
        workDone |= MPWork_PostWriteRequests( pAdapter );

        // Finally check to see if any of the outstanding requests have completed
        workDone |= MPWork_ResolveRequests( pAdapter );
        ++loop;
    }

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> <-- MPWork_WorkItem: (loop: %d)\n", pAdapter->PortName, loop)
    );
    return;
}

NTSTATUS MPWork_StartWorkThread(PMP_ADAPTER pAdapter)
{
   NTSTATUS          ntStatus;
   OBJECT_ATTRIBUTES objAttr;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> StartWorkThread - wrong IRQL::%d\n", pAdapter->PortName, KeGetCurrentIrql())
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((pAdapter->pWorkThread != NULL) || (pAdapter->hWorkThreadHandle != NULL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> WorkThread up/resumed\n", pAdapter->PortName)
      );
      return STATUS_SUCCESS;
   }

   KeClearEvent(&pAdapter->WorkThreadStartedEvent);
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   ntStatus = PsCreateSystemThread
              (
                 OUT &pAdapter->hWorkThreadHandle,
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)MPWork_WorkThread,
                 IN (PVOID)pAdapter
              );
   if ((!NT_SUCCESS(ntStatus)) || (pAdapter->hWorkThreadHandle == NULL))
   {
      pAdapter->hWorkThreadHandle = NULL;
      pAdapter->pWorkThread = NULL;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> WorkTh failure\n", pAdapter->PortName)
      );
      return ntStatus;
   }
   ntStatus = KeWaitForSingleObject
              (
                 &pAdapter->WorkThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );

   ntStatus = ObReferenceObjectByHandle
              (
                 pAdapter->hWorkThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&pAdapter->pWorkThread,
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> Work: ObReferenceObjectByHandle failed 0x%x\n", pAdapter->PortName, ntStatus)
      );
      pAdapter->pWorkThread = NULL;
   }
   else
   {
      if (STATUS_SUCCESS != (ntStatus = ZwClose(pAdapter->hWorkThreadHandle)))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> Work ZwClose failed - 0x%x\n", pAdapter->PortName, ntStatus)
         );
      }
      else
      {
         pAdapter->hWorkThreadHandle = NULL;
      }
      ntStatus = STATUS_SUCCESS;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> Work handle=0x%p thOb=0x%p\n", pAdapter->PortName,
       pAdapter->hWorkThreadHandle, pAdapter->pWorkThread)
   );

   return ntStatus;

}  // MPWork_StartWorkThread

NTSTATUS MPWork_CancelWorkThread(PMP_ADAPTER pAdapter)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER delayValue;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->Work Cxl\n", pAdapter->PortName)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> Work Cxl: wrong IRQL\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (InterlockedIncrement(&pAdapter->WorkThreadCancelStarted) > 1)
   {
      while ((pAdapter->hWorkThreadHandle != NULL) || (pAdapter->pWorkThread != NULL))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> Work cxl in pro\n", pAdapter->PortName)
         );
         MPMAIN_Wait(-(3 * 1000 * 1000));
      }
      InterlockedDecrement(&pAdapter->WorkThreadCancelStarted);
      return STATUS_SUCCESS;
   }

   if ((pAdapter->hWorkThreadHandle != NULL) || (pAdapter->pWorkThread != NULL))
   {
      KeClearEvent(&pAdapter->WorkThreadClosedEvent);
      KeSetEvent
      (
         &pAdapter->WorkThreadCancelEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pAdapter->pWorkThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pAdapter->pWorkThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pAdapter->pWorkThread);
         KeClearEvent(&pAdapter->WorkThreadClosedEvent);
         ZwClose(pAdapter->hWorkThreadHandle);
         pAdapter->pWorkThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pAdapter->WorkThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pAdapter->WorkThreadClosedEvent);
         ZwClose(pAdapter->hWorkThreadHandle);
      }
      pAdapter->hWorkThreadHandle = NULL;
   }

   InterlockedDecrement(&pAdapter->WorkThreadCancelStarted);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--Work Cxl\n", pAdapter->PortName)
   );

   return ntStatus;
}  // MPWork_CancelWorkThread

VOID MPWork_WorkThread(PVOID Context)
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
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> Work th: wrong IRQL\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hWorkThreadHandle);
      pAdapter->hWorkThreadHandle = NULL;
      KeSetEvent(&pAdapter->WorkThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
   }

   // allocate a wait block array for the multiple wait
   pwbArray = ExAllocatePool
              (
                 NonPagedPool,
                 (WORK_THREAD_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK)
              );
   if (pwbArray == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> Work th: NO_MEM\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hWorkThreadHandle);
      pAdapter->hWorkThreadHandle = NULL;
      KeSetEvent(&pAdapter->WorkThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   KeSetPriorityThread(KeGetCurrentThread(), 27);

   // 3/13/2018: remove following, potential race condition
   // KeClearEvent(&pAdapter->WorkThreadCancelEvent);
   // KeClearEvent(&pAdapter->WorkThreadClosedEvent);
   // KeClearEvent(&pAdapter->WorkThreadProcessEvent);

   while (bKeepRunning == TRUE)
   {
      if (bThreadStarted == FALSE)
      {
         bThreadStarted = TRUE;
         KeSetEvent(&pAdapter->WorkThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      }

      ntStatus = KeWaitForMultipleObjects
                 (
                    WORK_THREAD_EVENT_COUNT,
                    (VOID **)&pAdapter->WorkThreadEvent,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,             // non-alertable
                    NULL,
                    pwbArray
                 );
      switch (ntStatus)
      {
         case WORK_CANCEL_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> WORK_CANCEL\n", pAdapter->PortName)
            );

            KeClearEvent(&pAdapter->WorkThreadCancelEvent);
            bKeepRunning = FALSE;
            break;
         }

         case WORK_PROCESS_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> WORK_PROCESS\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->WorkThreadProcessEvent);
            MPWork_WorkItem(NULL, pAdapter);
            break;
         }

         default:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_ERROR,
               ("<%s> Work th: unexpected evt-0x%x\n", pAdapter->PortName, ntStatus)
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
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_FORCE,
      ("<%s> Work th: OUT (0x%x)\n", pAdapter->PortName, ntStatus)
   );

   KeSetEvent(&pAdapter->WorkThreadClosedEvent, IO_NO_INCREMENT, FALSE);

   PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}  // MPWork_WorkThread

