/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P Q M I . C

GENERAL DESCRIPTION
  This file provides support for QMI.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "MPQMI.h"
#include "MPQCTL.h"
#include "MPQMUX.h"
#include "MPMain.h"
#include "MPIOC.h"
#include "MPOID.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPQMI.tmh"

#endif   // EVENT_TRACING

// To retrieve the next TLV
PQCQMUX_TLV MPQMI_GetNextTLV
(
   PMP_ADAPTER pAdapter,
   PQCQMUX_TLV CurrentTLV,
   PLONG       RemainingLength // length including CurrentTLV
)
{
   PQCQMUX_TLV nextTlv = NULL;
   PCHAR dataPtr;

   // skip current TLV header
   if (*RemainingLength > sizeof(QMI_TLV_HDR))
   {
      *RemainingLength -= sizeof(QMI_TLV_HDR);
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> _GetNextTLV: no more TLV\n", pAdapter->PortName)
      );
      return NULL;
   }

   if (*RemainingLength > CurrentTLV->Length)
   {
      // skip current TLV body
      *RemainingLength -= CurrentTLV->Length;
      dataPtr = (PCHAR)&(CurrentTLV->Value);
      dataPtr += CurrentTLV->Length;
      nextTlv = (PQCQMUX_TLV)dataPtr;
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> _GetNextTLV: no more TLV %d/%d\n", pAdapter->PortName,
           *RemainingLength, CurrentTLV->Length)
      );
   }

   if (nextTlv != NULL)
   {
   }
   else
   {
   }

   return nextTlv;
}  // MPQMI_GetNextTLV

// ======================== QMI =========================
VOID MPQMI_ProcessInboundQMI
(

   PMP_ADAPTER  pAdapter,
   PMP_OID_READ pOID
)
{
   PQCQMI qmi = (PQCQMI)&(pOID->QMI);
   UCHAR  ucChannel = USBIF_GetDataInterfaceNumber(pAdapter->USBDo);

   // MPQMI_MakeQMIHdr(qmi);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> got QMI <=%u: IFType 0x%x Length %d Flags 0x%x Type 0x%x CID %d IRPst 0x%x(%uB)\n",
        pAdapter->PortName,
        ucChannel,
        qmi->IFType,
        qmi->Length,
        qmi->CtlFlags,
        qmi->QMIType,
        qmi->ClientId,
        pOID->IrpStatus,
        pOID->IrpDataLength
      )
   );

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> MPth: Main Adapter sync needed value %d\n", pAdapter->PortName, pAdapter->QmiSyncNeeded)
   );
   
   // sanity checking
   if (qmi->CtlFlags != QCQMI_CTL_FLAG_SERVICE)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QMI error: unexpected QMI control flags 0x%x\n",
           pAdapter->PortName, qmi->CtlFlags)
      );
   }

   if ((!NT_SUCCESS(pOID->IrpStatus)) ||
       (pOID->IrpDataLength < (qmi->Length + QCUSB_CTL_MSG_HDR_SIZE)))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_ERROR,
         ("<%s> QMI: invalid returned data from target/phone (IRPst 0x%x), dropped\n",
           pAdapter->PortName,
           pOID->IrpStatus)
      );
      return;
   }

   switch (qmi->QMIType)
   {
      case QMUX_TYPE_CTL:
      {
         MPQCTL_ProcessInboundQMICTL(pAdapter, qmi, pOID->IrpDataLength);
         break;
      }

      // case QMUX_TYPE_WDS:
      // case QMUX_TYPE_DMS:
      // case QMUX_TYPE_NAS:
      default:
      {
         MPQMI_ProcessInboundQMUX(pAdapter, qmi, pOID->IrpDataLength);
         break;
      }

      // default:
      // {
      //    QCNET_DbgPrint
      //    (
      //       MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      //       MP_DBG_LEVEL_ERROR,
      //       ("<%s> ProcessQMI: invalid QMI type %d, IRP data dropped.\n", pAdapter->PortName, qmi->QMIType)
      //    );
      //    break;
      // }
   }  // switch

   // MPWork put the associated OID back to CtrlReadFreeList
   // so we don't need to do it here.

}  // MPQMI_ProcessInboundQMI

// This function must be called with OIDLock held
VOID MPQMI_FreeQMIRequestQueue
(
   PMP_OID_WRITE pOID
)
{
   PLIST_ENTRY thisEntry;
   PQCQMIQUEUE currentQMIElement = NULL;

   while (!IsListEmpty(&pOID->QMIQueue))
   {
      thisEntry = RemoveHeadList(&pOID->QMIQueue);
      currentQMIElement = CONTAINING_RECORD(thisEntry, QCQMIQUEUE, QCQMIRequest);
      NdisFreeMemory(currentQMIElement, sizeof(QCQMIQUEUE) + sizeof(QCQMUX) + sizeof(QMUX_MSG), 0 );
   }
}  // MPQMI_FreeQMIRequestQueue


BOOLEAN MPQMI_FindQMIRequest
(
   PMP_ADAPTER pAdapter,
   PMP_OID_WRITE pOID,
   PQCQMI      qmi
)
{
    PLIST_ENTRY   listHead, thisEntry;
    PQCQMICTL_MSG qmictl,  curr_qmictl;
    PQCQMUX       qmux = NULL, curr_qmux;
    BOOLEAN       bMatchFound = FALSE;
    PQCQMI        currentQMI = NULL;
    PQCQMIQUEUE   currentQMIElement = NULL;

    // Be sure you own the OID_LOCK before you get here!!!!
   if (!IsListEmpty(&pOID->QMIQueue))
   {
      // Start at the head of the list
      listHead = &pOID->QMIQueue;
      thisEntry = listHead->Flink;

      while (thisEntry != listHead)
      {
         currentQMIElement = CONTAINING_RECORD(thisEntry, QCQMIQUEUE, QCQMIRequest);
         currentQMI = &currentQMIElement->QMI;
         if ((currentQMI->QMIType == qmi->QMIType) &&
             (((currentQMI->ClientId == 0x00) && (qmi->ClientId == pAdapter->ClientId[qmi->QMIType])) ||
             (currentQMI->ClientId == qmi->ClientId)))
         {
            switch (qmi->QMIType)
            {
              case QMUX_TYPE_CTL:
              {
                curr_qmictl = (PQCQMICTL_MSG)&(currentQMI->SDU);
                qmictl = (PQCQMICTL_MSG)&qmi->SDU;
                if (curr_qmictl->TransactionId == qmictl->TransactionId)
                {
                  bMatchFound = TRUE;
                }
                break;
              }

              // Since QMUX has the same structure
              // we just process them together
              // Check only QMI types supported internally
              case QMUX_TYPE_WDS:
              case QMUX_TYPE_DMS:
              case QMUX_TYPE_WMS:
              case QMUX_TYPE_NAS:
              case QMUX_TYPE_UIM:
              {
                curr_qmux = (PQCQMUX)&(currentQMI->SDU);
                qmux = (PQCQMUX)&qmi->SDU;
                if (curr_qmux->TransactionId == qmux->TransactionId)
                {
                  bMatchFound = TRUE;
                }
                break;
              }
            }

            if (bMatchFound == TRUE)
            {
              break;
            }
         }  // if

         // Advance to the next OID in the completed list
         thisEntry = thisEntry->Flink;
      }

      // If we are here and have exhaused the list we didn't find a match
      if( thisEntry == listHead )
      {
         currentQMI = NULL;
         bMatchFound = FALSE;
      }
      else
      {
         if (bMatchFound == TRUE)
         {
            if (qmux != NULL)
            {
               // DbgPrint("<qnetxxx> FindQMIRequest: match found, removed 0x%p (QMI%X CID%X TID%d)\n",
               //      currentQMIElement, qmi->QMIType,
               //      qmi->ClientId, qmux->TransactionId);
            }
            RemoveEntryList( thisEntry );
            NdisFreeMemory(currentQMIElement, sizeof(QCQMIQUEUE) + sizeof(QCQMUX) + sizeof(QMUX_MSG), 0 );
         }
      }   
   }
    return bMatchFound;
}  // MPQMI_FindQMIRequest


PMP_OID_WRITE MPQMI_FindRequest
(
   PMP_ADAPTER pAdapter,
   PQCQMI      qmi
)
{
    PMP_OID_WRITE currentOID = NULL;
    PLIST_ENTRY   listHead, thisEntry;
    PQCQMICTL_MSG   qmictl,  curr_qmictl;
    PQCQMUX      qmux, curr_qmux;
    BOOLEAN       bMatchFound = FALSE;
   PQCQMI currentQMI = NULL;

   // Be sure you own the OIDLock before you get here!!!!
   // Aquire the OID LOCK sending all the qmi messages 
   NdisAcquireSpinLock( &pAdapter->OIDLock );

   if (!IsListEmpty(&pAdapter->OIDWaitingList))
   {
      // Start at the head of the list
      listHead = &pAdapter->OIDWaitingList;
      thisEntry = listHead->Flink;

      while (thisEntry != listHead)
      {
         currentOID = CONTAINING_RECORD(thisEntry, MP_OID_WRITE, List);
         bMatchFound = MPQMI_FindQMIRequest(pAdapter, currentOID, qmi);
         if (bMatchFound == TRUE)
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

   NdisReleaseSpinLock(&pAdapter->OIDLock);

   return currentOID;
}  // MPQMI_FindRequest

/* ---------------------------------------------------------------
   This function fills the pending OID and completes the OID.
-----------------------------------------------------------------*/
VOID MPQMI_FillOID
(
   PMP_ADAPTER   pAdapter,
   PMP_OID_WRITE pMatchOid,  // not NULL when called
   PVOID         pData,
   ULONG         Length,
   NDIS_STATUS   OIDStatus
)
{
   // pMatchOid is not NULL
   // pMatchOid->RequestInfoBuffer
   // pMatchOid->RequestInfoBufferLength
   // pMatchOid->RequestBytesUsed
   // pMatchOid->RequestBytesNeeded

   NdisAcquireSpinLock(&pAdapter->OIDLock);
   RemoveEntryList(&pMatchOid->List);
   NdisReleaseSpinLock(&pAdapter->OIDLock);

   if (pMatchOid->OidType == fMP_QUERY_OID)
   {
      if (pMatchOid->RequestBufferLength >= Length)
      {
         if (pMatchOid->RequestInfoBuffer != NULL)
         {
            if ((pData != NULL) && (Length > 0))
            {
               NdisMoveMemory(pMatchOid->RequestInfoBuffer, pData, Length);
            }
            else
            {
               OIDStatus = NDIS_STATUS_FAILURE;

               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> MPQMI_FillOID: failure for OID 0x%x status 0x%x\n",
                    pAdapter->PortName,
                    pMatchOid->Oid, OIDStatus)
               );
            }

            if ( pMatchOid->RequestBytesUsed != NULL )
            {
               *(pMatchOid->RequestBytesUsed) = Length;
            }
            if ( pMatchOid->RequestBytesNeeded != NULL )
            {
               *(pMatchOid->RequestBytesNeeded) = 0;
            }
         }
         else
         {
            if ( pMatchOid->RequestBytesUsed != NULL )
            {
               *(pMatchOid->RequestBytesUsed) = 0;
            }
            if ( pMatchOid->RequestBytesNeeded != NULL )
            {
               *(pMatchOid->RequestBytesNeeded) = Length;
            }
            OIDStatus = NDIS_STATUS_BUFFER_TOO_SHORT; 
         }
      }
      else
      {
         if ( pMatchOid->RequestBytesUsed != NULL )
         {
            *(pMatchOid->RequestBytesUsed) = 0;
         }
         if ( pMatchOid->RequestBytesNeeded != NULL )
         {
            *(pMatchOid->RequestBytesNeeded) = Length;
         }
         OIDStatus = NDIS_STATUS_BUFFER_TOO_SHORT; 
      }

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> QMI: Indicate Query Complete (Oid: 0x%x, Status: 0x%x)\n",
           pAdapter->PortName, pMatchOid->Oid, OIDStatus)
      );
      MPOID_CompleteOid(pAdapter, OIDStatus, pMatchOid, pMatchOid->OidType, FALSE);
   }
   else  // SET OID
   {
      if ( pMatchOid->RequestBytesUsed != NULL )
      {
         *(pMatchOid->RequestBytesUsed) = pMatchOid->RequestBufferLength;
      }

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> QMI: Indicate Set Complete (Oid: 0x%x, Status: 0x%x)\n",
          pAdapter->PortName, pMatchOid->Oid, OIDStatus)
      );
      MPOID_CompleteOid(pAdapter, OIDStatus, pMatchOid, pMatchOid->OidType, FALSE);
   }


   NdisAcquireSpinLock( &pAdapter->OIDLock );

   MPOID_CleanupOidCopy(pAdapter, pMatchOid);
   InsertTailList( &pAdapter->OIDFreeList, &pMatchOid->List );
   InterlockedDecrement(&(pAdapter->nBusyOID));

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> _ResolveRequests ok: OID=0x%x nBusyOID=%d\n",
        pAdapter->PortName, pMatchOid->Oid, pAdapter->nBusyOID)
   );

   NdisReleaseSpinLock( &pAdapter->OIDLock );

   return;
}  // MPQMI_FillOID


/* ---------------------------------------------------------------
   This function fills the pending OID and completes the OID.
-----------------------------------------------------------------*/
NDIS_STATUS  MPQMI_FillPendingOID
(
   PMP_ADAPTER   pAdapter,
   PMP_OID_WRITE pMatchOid,  // not NULL when called
   PVOID         pData,
   ULONG         Length
)
{
   NDIS_STATUS   OIDStatus = NDIS_STATUS_SUCCESS;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->MPQMI_FillPendingOID: OID (0x%p, 0x%x) P%d\n", pAdapter->PortName,
        pMatchOid->OidReference, pMatchOid->Oid, pAdapter->nBusyOID)
   );

   if (pMatchOid->OidType == fMP_QUERY_OID)
   {
      if (pMatchOid->RequestBufferLength >= Length)
      {
         if (pMatchOid->RequestInfoBuffer != NULL)
         {
            if ((pData != NULL) && (Length > 0))
            {
               NdisMoveMemory(pMatchOid->RequestInfoBuffer, pData, Length);
            }
            else
            {
               OIDStatus = NDIS_STATUS_FAILURE;

               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> MPQMI_FillOID: failure for OID 0x%x status 0x%x\n",
                    pAdapter->PortName,
                    pMatchOid->Oid, OIDStatus)
               );
               return OIDStatus;
            }

            if ( pMatchOid->RequestBytesUsed != NULL )
            {
               *(pMatchOid->RequestBytesUsed) = Length;
            }
            if ( pMatchOid->RequestBytesNeeded != NULL )
            {
               *(pMatchOid->RequestBytesNeeded) = 0;
            }
         }
         else
         {
            if ( pMatchOid->RequestBytesUsed != NULL )
            {
               *(pMatchOid->RequestBytesUsed) = 0;
            }
            if ( pMatchOid->RequestBytesNeeded != NULL )
            {
               *(pMatchOid->RequestBytesNeeded) = Length;
            }
            OIDStatus = NDIS_STATUS_BUFFER_TOO_SHORT; 
         }
      }
      else
      {
         if ( pMatchOid->RequestBytesUsed != NULL )
         {
            *(pMatchOid->RequestBytesUsed) = 0;
         }
         if ( pMatchOid->RequestBytesNeeded != NULL )
         {
            *(pMatchOid->RequestBytesNeeded) = Length;
         }
         OIDStatus = NDIS_STATUS_BUFFER_TOO_SHORT; 
      }

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPQMI_FillPendingOID: filled QUERY Oid (0x%p, 0x%x) ST 0x%x)\n",
           pAdapter->PortName, pMatchOid->OidReference, pMatchOid->Oid, OIDStatus)
      );
   }
   else  // SET OID
   {
      if ( pMatchOid->RequestBytesUsed != NULL )
      {
         *(pMatchOid->RequestBytesUsed) = pMatchOid->RequestBufferLength;
      }

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPQMI_FillPendingOID: filled SET Oid (0x%p, 0x%x) ST 0x%x)\n",
           pAdapter->PortName, pMatchOid->OidReference, pMatchOid->Oid, OIDStatus)
      );
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--MPQMI_FillPendingOID: OID (0x%p, 0x%x) P%d\n", pAdapter->PortName,
        pMatchOid->OidReference, pMatchOid->Oid, pAdapter->nBusyOID)
   );
   return OIDStatus;
}  // MPQMI_FillPendingOID

#ifdef NDIS620_MINIPORT

VOID SetDefaultSignalStengthTimer( PMP_ADAPTER pAdapter)
{
   NTSTATUS nts = STATUS_SUCCESS;

   NdisAcquireSpinLock(&pAdapter->QMICTLLock);
   
   if (pAdapter->nSignalStateTimerCount == 0)
   {
      nts = IoAcquireRemoveLock(pAdapter->pMPRmLock, NULL);
      QcStatsIncrement(pAdapter, MP_CNT_TIMER, 222);
      pAdapter->nSignalStateTimerCount = 30000;
      NdisSetTimer( &pAdapter->SignalStateTimer, pAdapter->nSignalStateTimerCount );
   }
   pAdapter->RssiThreshold = 0x05;

   NdisReleaseSpinLock(&pAdapter->QMICTLLock);
}  

#endif

/* ---------------------------------------------------------------
   This function convert the previous OID into QMI
   The result is placed in the QMI member in the OID structure.
-----------------------------------------------------------------*/
ULONG MPQMI_OIDtoQMI
(
   PMP_ADAPTER   pAdapter,
   NDIS_OID      Oid,
   PMP_OID_WRITE pOID
)
{
   NDIS_STATUS  status = NDIS_STATUS_SUCCESS;
   ULONG        qmiLen = 0;

   pOID->OIDAsyncType= NDIS_STATUS_PENDING;

   // pOID->QMI.ClientId = pAdapter->ClientId[QMUX_TYPE_WDS];  // TODO: how to set it correctly???

   // if (pOID->QMI.ClientId == 0)
   // {
   //    QCNET_DbgPrint
   //    (
   //       MP_DBG_MASK_CONTROL,
   //       MP_DBG_LEVEL_ERROR,
   //       ("<%s> MPQMI_OIDtoQMI: no client id assigned, rejected\n", pAdapter->PortName)
   //    );
   //    return 0;
   // }

   switch (Oid)
   {
      case OID_GEN_LINK_SPEED:
      case OID_GEN_LINK_STATE:
      {
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_WDS, 
                                         QMIWDS_GET_CURRENT_CHANNEL_RATE_REQ, NULL, FALSE );
         
         break;
      }  // OID_GEN_LINK_SPEED

      case OID_GEN_VENDOR_DESCRIPTION:
      {
        do
        {
           qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                           QMIDMS_GET_DEVICE_MFR_REQ, NULL, FALSE );
        }while(0);
         break;
      }  // OID_GEN_VENDOR_DESCRIPTION

      case OID_GEN_XMIT_OK:
      case OID_GEN_RCV_OK:
      case OID_GEN_XMIT_ERROR:
      case OID_GEN_RCV_ERROR:
      case OID_GEN_RCV_NO_BUFFER:
      case OID_GEN_STATISTICS:
      {
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_WDS, 
                                         QMIWDS_GET_PKT_STATISTICS_REQ, MPQMUX_ComposeWdsGetPktStatisticsReq, FALSE );
         
         break;
      }  // statistics OID's

#ifdef NDIS620_MINIPORT

      case OID_WWAN_DEVICE_CAPS:
      {
         pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
         do
         {
            qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                            QMIDMS_GET_DEVICE_CAP_REQ, NULL, FALSE );
            
         }while(0);

         if(qmiLen != 0 )
         {
            PNDIS_WWAN_DEVICE_CAPS pNdisDeviceCaps;
            // Fill the basic response OID
            pOID->pOIDResp = ExAllocatePool( NonPagedPool, sizeof(NDIS_WWAN_DEVICE_CAPS) );
            if( pOID->pOIDResp == NULL )
            {
              QCNET_DbgPrint
              (
                MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                MP_DBG_LEVEL_ERROR,
                ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
              );
               return 0;
            }
            pOID->OIDRespLen = sizeof(NDIS_WWAN_DEVICE_CAPS);
            NdisZeroMemory( pOID->pOIDResp, sizeof(NDIS_WWAN_DEVICE_CAPS) );

            pNdisDeviceCaps = (PNDIS_WWAN_DEVICE_CAPS)pOID->pOIDResp;
            pNdisDeviceCaps->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            pNdisDeviceCaps->Header.Revision = NDIS_WWAN_DEVICE_CAPS_REVISION_1;
            pNdisDeviceCaps->Header.Size = sizeof(NDIS_WWAN_DEVICE_CAPS);
            pNdisDeviceCaps->DeviceCaps.WwanDeviceType = WwanDeviceTypeEmbedded;
            pNdisDeviceCaps->DeviceCaps.WwanVoiceClass = WwanVoiceClassNoVoice;
            pNdisDeviceCaps->DeviceCaps.MaxActivatedContexts = 1;

            pOID->IndicationType = NDIS_STATUS_WWAN_DEVICE_CAPS;

            //Start the Signal Strenght timer
            SetDefaultSignalStengthTimer( pAdapter );

            // Set the wds event report indications to get the connect/disconnect states
            MPQWDS_SendEventReportReq(pAdapter, NULL, TRUE);

            //Send DUN Call indication request
            MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WDS, 
                                      QMIWDS_DUN_CALL_INFO_REQ, MPQWDS_SendWdsDunCallInfoReq, TRUE );

            if (pAdapter->IsNASSysInfoPresent == TRUE)
            {
                MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_NAS, 
                                          QMINAS_INDICATION_REGISTER_REQ, MPQMUX_SendNasIndicationRegisterReq, TRUE );
            }
         }
         break;
      }  // Device CAP OID's

      case OID_WWAN_READY_INFO:
      {
         pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
         do
         {
            qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                            QMIDMS_GET_OPERATING_MODE_REQ, NULL, FALSE );
            
         }while(0);

         if(qmiLen != 0 )
         {
            PNDIS_WWAN_READY_INFO pNdisReadyInfo;
            // Fill the basic response OID
            pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
            if( pOID->pOIDResp == NULL )
            {
              QCNET_DbgPrint
              (
                MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                MP_DBG_LEVEL_ERROR,
                ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
              );
               return 0;
            }
            NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
            pOID->OIDRespLen = sizeof(NDIS_WWAN_READY_INFO);
            pNdisReadyInfo = (PNDIS_WWAN_READY_INFO)pOID->pOIDResp;
            pNdisReadyInfo->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            pNdisReadyInfo->Header.Revision = NDIS_WWAN_READY_INFO_REVISION_1;
            pNdisReadyInfo->Header.Size = sizeof(NDIS_WWAN_READY_INFO);
            pNdisReadyInfo->ReadyInfo.EmergencyMode = WwanEmergencyModeOff;

            pOID->IndicationType = NDIS_STATUS_WWAN_READY_INFO;
         }
         break;
      }
      case OID_WWAN_SERVICE_ACTIVATION:
      {

         status = NDIS_STATUS_NOT_SUPPORTED;
         break;
      }
      case OID_WWAN_RADIO_STATE:
      {
         pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
         if (pOID->OidType == fMP_SET_OID)
         {
           qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                           QMIDMS_SET_OPERATING_MODE_REQ, MPQMUX_ComposeDmsSetOperatingModeReq, FALSE );            
         }
         else
         {
            qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                            QMIDMS_GET_OPERATING_MODE_REQ, NULL, FALSE );
         }
         if(qmiLen != 0 )
         {
            PNDIS_WWAN_RADIO_STATE pNdisRadioState;
            
            pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
            if( pOID->pOIDResp == NULL )
            {
              QCNET_DbgPrint
              (
                MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                MP_DBG_LEVEL_ERROR,
                ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
              );
               return 0;
            }
            NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
            pOID->OIDRespLen = sizeof(NDIS_WWAN_RADIO_STATE);
            pNdisRadioState = (PNDIS_WWAN_RADIO_STATE)pOID->pOIDResp;
            pNdisRadioState->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            pNdisRadioState->Header.Revision = NDIS_WWAN_RADIO_STATE_REVISION_1;
            pNdisRadioState->Header.Size = sizeof(NDIS_WWAN_RADIO_STATE);

            pOID->IndicationType = NDIS_STATUS_WWAN_RADIO_STATE;
         }
         break;
      }

      case OID_WWAN_PIN:
      {
         pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
         if (pOID->OidType == fMP_SET_OID)
         {

            PNDIS_OID_REQUEST pOIDReq = pOID->OidReference;
            PNDIS_WWAN_SET_PIN  pSetPin = (PNDIS_WWAN_SET_PIN)pOIDReq->DATA.SET_INFORMATION.InformationBuffer;
            switch(pSetPin->PinAction.PinOperation)
            {
               case WwanPinOperationEnter:
               {
                  if (pSetPin->PinAction.PinType == WwanPinTypePuk1 || 
                      pSetPin->PinAction.PinType == WwanPinTypePuk2)
                  {
                    if (pAdapter->ClientId[QMUX_TYPE_UIM] == 0)
                    {
                    qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                                    QMIDMS_UIM_GET_PIN_STATUS_REQ, NULL, FALSE );                     
                  }
                    else
                    {
                        qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_UIM, 
                                                    QMIUIM_GET_CARD_STATUS_REQ, NULL, FALSE );                     
                    }
                  }
                  else if (pSetPin->PinAction.PinType == WwanPinTypePin1 || 
                           pSetPin->PinAction.PinType == WwanPinTypePin2)
                  {
                     if (pAdapter->ClientId[QMUX_TYPE_UIM] == 0)
                     {
                     qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                                     QMIDMS_UIM_GET_PIN_STATUS_REQ, NULL, FALSE );                     
                  }
                     else
                     {
                         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_UIM, 
                                                     QMIUIM_GET_CARD_STATUS_REQ, NULL, FALSE );                     
                     }
                  }
                  else if ((pSetPin->PinAction.PinType == WwanPinTypeDeviceSimPin) || 
                           ((pSetPin->PinAction.PinType >= WwanPinTypeNetworkPin) && 
                            (pSetPin->PinAction.PinType <= WwanPinTypeCorporatePin)))
                  {
                     if (pAdapter->ClientId[QMUX_TYPE_UIM] == 0)
                     {
                     qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                                     QMIDMS_UIM_SET_CK_PROTECTION_REQ, MPQMUX_ComposeDmsUIMSetCkProtectionReq, FALSE );
                  }
                     else
                     {
                        qmiLen = 0;
                     }
                  }
                  else if (pSetPin->PinAction.PinType >= WwanPinTypeNetworkPuk || 
                           pSetPin->PinAction.PinType <= WwanPinTypeCorporatePuk)
                  {
                     if (pAdapter->ClientId[QMUX_TYPE_UIM] == 0)
                     {
                     qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                                     QMIDMS_UIM_UNBLOCK_CK_REQ, MPQMUX_ComposeDmsUIMUnblockCkReq, FALSE );
                  }
                  else
                  {
                     qmiLen = 0;
                  }
                  }
                  else
                  {
                     qmiLen = 0;
                  }
                  break;
               }
               case WwanPinOperationEnable:
               case WwanPinOperationDisable:
               {
                  if (pSetPin->PinAction.PinType == WwanPinTypePin1 || 
                      pSetPin->PinAction.PinType == WwanPinTypePin2)
                  {
                      if (pAdapter->ClientId[QMUX_TYPE_UIM] == 0)
                      {
                     qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                                     QMIDMS_UIM_GET_PIN_STATUS_REQ, NULL, FALSE );                     
                  }
                  else
                  {
                          qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_UIM, 
                                                      QMIUIM_GET_CARD_STATUS_REQ, NULL, FALSE );                     
                      }
                  }
                  else
                  {
                     qmiLen = 0;
                  }
                  break;
               }
               case WwanPinOperationChange:
               {
                  if (pSetPin->PinAction.PinType == WwanPinTypePin1 || 
                      pSetPin->PinAction.PinType == WwanPinTypePin2)
                  {
                      if (pAdapter->ClientId[QMUX_TYPE_UIM] == 0)
                      {
                     qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                                     QMIDMS_UIM_GET_PIN_STATUS_REQ, NULL, FALSE );                     
                  }
                  else
                  {
                          qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_UIM, 
                                                      QMIUIM_GET_CARD_STATUS_REQ, NULL, FALSE );                     
                      }
                  }
                  else
                  {
                     qmiLen = 0;
                  }
                  break;
               }
            }
         }         
         else
         {
             if (pAdapter->ClientId[QMUX_TYPE_UIM] == 0)
             {
            qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                            QMIDMS_UIM_GET_PIN_STATUS_REQ, NULL, FALSE );                     
         }
             else
             {
                 qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_UIM, 
                                             QMIUIM_GET_CARD_STATUS_REQ, NULL, FALSE );                     
             }
         }
         if(qmiLen != 0 )
         {
            PNDIS_WWAN_PIN_INFO  pNdisPinInfo;
            // Fill the basic response OID
            pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
            if( pOID->pOIDResp == NULL )
            {
              QCNET_DbgPrint
              (
                MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                MP_DBG_LEVEL_ERROR,
                ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
              );
               return 0;
            }
            NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
            pOID->OIDRespLen = sizeof(NDIS_WWAN_PIN_INFO);
            pNdisPinInfo = (PNDIS_WWAN_PIN_INFO)pOID->pOIDResp;
            pNdisPinInfo->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            pNdisPinInfo->Header.Revision = NDIS_WWAN_PIN_INFO_REVISION_1;
            pNdisPinInfo->Header.Size = sizeof(NDIS_WWAN_PIN_INFO);

            pOID->IndicationType = NDIS_STATUS_WWAN_PIN_INFO;
         }
         break;
      }

      case OID_WWAN_PIN_LIST:
      {
         pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
         if (pAdapter->ClientId[QMUX_TYPE_UIM] == 0)
         {
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_DMS, 
                                         QMIDMS_UIM_GET_PIN_STATUS_REQ, NULL, FALSE );                     
         }
         else
         {
             qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_UIM, 
                                         QMIUIM_GET_CARD_STATUS_REQ, NULL, FALSE );                     
         }
         if(qmiLen != 0 )
         {
            PNDIS_WWAN_PIN_LIST  pNdisPinList;
            // Fill the basic response OID
            pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
            if( pOID->pOIDResp == NULL )
            {
              QCNET_DbgPrint
              (
                MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                MP_DBG_LEVEL_ERROR,
                ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
              );
               return 0;
            }
            NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
            pOID->OIDRespLen = sizeof(NDIS_WWAN_PIN_LIST);
            pNdisPinList = (PNDIS_WWAN_PIN_LIST)pOID->pOIDResp;
            pNdisPinList->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            pNdisPinList->Header.Revision = NDIS_WWAN_PIN_LIST_REVISION_1;
            pNdisPinList->Header.Size = sizeof(NDIS_WWAN_PIN_LIST);

            pOID->IndicationType = NDIS_STATUS_WWAN_PIN_LIST;
         }
         break;
      }
      case OID_WWAN_HOME_PROVIDER:
      {
         pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
         if (pAdapter->IsNASSysInfoPresent == FALSE)
         {
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                         QMINAS_GET_SERVING_SYSTEM_REQ, NULL, FALSE );
         }
         else
         {
             qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                             QMINAS_GET_SYS_INFO_REQ, NULL, FALSE );
         }
         if(qmiLen != 0 )
         {
            PNDIS_WWAN_HOME_PROVIDER  pNdisHomeProvider;
            // Fill the basic response OID
            pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
            if( pOID->pOIDResp == NULL )
            {
              QCNET_DbgPrint
              (
                MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                MP_DBG_LEVEL_ERROR,
                ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
              );
               return 0;
            }
            NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
            pOID->OIDRespLen = sizeof(NDIS_WWAN_HOME_PROVIDER);
            pNdisHomeProvider = (PNDIS_WWAN_HOME_PROVIDER)pOID->pOIDResp;
            pNdisHomeProvider->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            pNdisHomeProvider->Header.Revision = NDIS_WWAN_HOME_PROVIDER_REVISION_1;
            pNdisHomeProvider->Header.Size = sizeof(NDIS_WWAN_HOME_PROVIDER);

            pOID->IndicationType = NDIS_STATUS_WWAN_HOME_PROVIDER;
         }
         break;
      }

      case OID_WWAN_PREFERRED_PROVIDERS:
      {
         status = NDIS_STATUS_NOT_SUPPORTED;
         break;
      }

   case OID_WWAN_VISIBLE_PROVIDERS:
   {
      pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
      if (pAdapter->DeviceClass == DEVICE_CLASS_GSM)
      {
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                QMINAS_PERFORM_NETWORK_SCAN_REQ, NULL, FALSE );
      }
      else
      {
         if (pAdapter->IsNASSysInfoPresent == FALSE)
         {
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                QMINAS_GET_SERVING_SYSTEM_REQ, NULL, FALSE );
         }
         else
         {
             qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                             QMINAS_GET_SYS_INFO_REQ, NULL, FALSE );
         }
      }
      if(qmiLen != 0 )
      {
         PNDIS_WWAN_VISIBLE_PROVIDERS  pNdisVisibleProviders;
         // Fill the basic response OID
         pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
         if( pOID->pOIDResp == NULL )
         {
           QCNET_DbgPrint
           (
             MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
             MP_DBG_LEVEL_ERROR,
             ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
           );
            return 0;
         }
         NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
         pOID->OIDRespLen = sizeof(NDIS_WWAN_VISIBLE_PROVIDERS);
         pNdisVisibleProviders = (PNDIS_WWAN_VISIBLE_PROVIDERS)pOID->pOIDResp;
         pNdisVisibleProviders->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
         pNdisVisibleProviders->Header.Revision = NDIS_WWAN_VISIBLE_PROVIDERS_REVISION_1;
         pNdisVisibleProviders->Header.Size = sizeof(NDIS_WWAN_VISIBLE_PROVIDERS);
         pNdisVisibleProviders->VisibleListHeader.ElementType = WwanStructProvider;
         pNdisVisibleProviders->VisibleListHeader.ElementCount = 0;

         pOID->IndicationType = NDIS_STATUS_WWAN_VISIBLE_PROVIDERS;
      }
      break;
   }
   case OID_WWAN_REGISTER_STATE:
   {
      
      if (pOID->OidType == fMP_SET_OID)
      {
         pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                QMINAS_GET_RF_BAND_INFO_REQ, NULL, FALSE );
      }
      else
      {
         pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
         if (pAdapter->IsNASSysInfoPresent == FALSE)
         {
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                QMINAS_GET_SERVING_SYSTEM_REQ, NULL, FALSE );
      }
         else
         {
             qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                             QMINAS_GET_SYS_INFO_REQ, NULL, FALSE );
         }
      }
      if(qmiLen != 0 )
      {

         PNDIS_WWAN_REGISTRATION_STATE  pNdisRegisterState;
         // Fill the basic response OID
         pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
         if( pOID->pOIDResp == NULL )
         {
           QCNET_DbgPrint
           (
             MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
             MP_DBG_LEVEL_ERROR,
             ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
           );
            return 0;
         }
         NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
         pOID->OIDRespLen = sizeof(NDIS_WWAN_REGISTRATION_STATE);
         pNdisRegisterState = (PNDIS_WWAN_REGISTRATION_STATE)pOID->pOIDResp;
         pNdisRegisterState->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
         pNdisRegisterState->Header.Revision = NDIS_WWAN_REGISTRATION_STATE_REVISION_1;
         pNdisRegisterState->Header.Size = sizeof(NDIS_WWAN_REGISTRATION_STATE);

         pOID->IndicationType = NDIS_STATUS_WWAN_REGISTER_STATE;
      }
      break;
   }
   case OID_WWAN_SIGNAL_STATE:
   {
      pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
      qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                             QMINAS_GET_SIGNAL_STRENGTH_REQ, NULL, FALSE );
      if (qmiLen != 0)
      {
         NTSTATUS nts = STATUS_SUCCESS;
         LONG nSignalStateTimerCount = 0;
         PNDIS_OID_REQUEST pOIDReq = pOID->OidReference;
         if (pOID->OidType == fMP_SET_OID)
         {
         
            PNDIS_WWAN_SET_SIGNAL_INDICATION pNdisSetSignalState = 
                                  (PNDIS_WWAN_SET_SIGNAL_INDICATION)pOIDReq->DATA.SET_INFORMATION.InformationBuffer;

            if (pNdisSetSignalState->SignalIndication.RssiInterval == WWAN_RSSI_DEFAULT)
            {
               nSignalStateTimerCount = 30000;
            }
            else if (pNdisSetSignalState->SignalIndication.RssiInterval == WWAN_RSSI_DISABLE)
            {
               nSignalStateTimerCount = 0;
            }
            else
            {
               nSignalStateTimerCount = pNdisSetSignalState->SignalIndication.RssiInterval*1000;
            }

            NdisAcquireSpinLock(&pAdapter->QMICTLLock);

            if (nSignalStateTimerCount > 0 )
            {

               if (pAdapter->nSignalStateTimerCount == 0)
               {
                  nts = IoAcquireRemoveLock(pAdapter->pMPRmLock, NULL);
                  QcStatsIncrement(pAdapter, MP_CNT_TIMER, 222);
               }
               if (!NT_SUCCESS(nts))
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_OID_QMI, MP_DBG_LEVEL_ERROR,
                     ("<%s> SignalStateTimer setsignal: rm lock err\n", pAdapter->PortName)
                  );
               }
               else
               {
                  pAdapter->nSignalStateTimerCount = nSignalStateTimerCount;
                  NdisSetTimer( &pAdapter->SignalStateTimer, pAdapter->nSignalStateTimerCount );
               }
            }
            else
            {
               BOOLEAN TimerCancel;
               pAdapter->nSignalStateTimerCount = 0;
               NdisCancelTimer( &pAdapter->SignalStateTimer, &TimerCancel );
               if (TimerCancel == FALSE)  // timer DPC is running
               {
                   QCNET_DbgPrint
                  (
                     MP_DBG_MASK_OID_QMI, MP_DBG_LEVEL_DETAIL,
                     ("<%s> SignalStateTimer: WAIT\n", pAdapter->PortName)
                  );
               }
               else
               {
                  // timer cancelled, release remove lock
                  QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 222);
               }
             }
         
             NdisReleaseSpinLock(&pAdapter->QMICTLLock);
         }          
         
      }
      if(qmiLen != 0 )
      {

         PNDIS_WWAN_SIGNAL_STATE   pNdisSignalState;
         // Fill the basic response OID
         pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
         if( pOID->pOIDResp == NULL )
         {
           QCNET_DbgPrint
           (
             MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
             MP_DBG_LEVEL_ERROR,
             ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
           );
            return 0;
         }
         NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
         pOID->OIDRespLen = sizeof(NDIS_WWAN_SIGNAL_STATE);
         pNdisSignalState = (PNDIS_WWAN_SIGNAL_STATE)pOID->pOIDResp;
         pNdisSignalState->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
         pNdisSignalState->Header.Revision = NDIS_WWAN_SIGNAL_STATE_REVISION_1;
         pNdisSignalState->Header.Size = sizeof(NDIS_WWAN_SIGNAL_STATE);

         pOID->IndicationType = NDIS_STATUS_WWAN_SIGNAL_STATE;
      }
      break;
   }

   case OID_WWAN_PACKET_SERVICE:
   {
      BOOLEAN bSendInitiateAttach = FALSE;
      PNDIS_OID_REQUEST pOIDReq = pOID->OidReference;
      PNDIS_WWAN_SET_PACKET_SERVICE pNdisSetPacketService = (PNDIS_WWAN_SET_PACKET_SERVICE)
         pOIDReq->DATA.SET_INFORMATION.InformationBuffer;

      pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;

      /* Do the PS Detach only when the CS is attached to get around PS only networks */
      if (pOID->OidType == fMP_SET_OID && pAdapter->DeviceClass == DEVICE_CLASS_GSM)
      {
         if (pNdisSetPacketService->PacketServiceAction == WwanPacketServiceActionAttach)
         {
            bSendInitiateAttach = TRUE;
#if SPOOF_PS_ONLY_DETACH
            if (pAdapter->IsLTE == TRUE)
            {
                bSendInitiateAttach = FALSE;            
            }
#endif
         }
         if (pNdisSetPacketService->PacketServiceAction == WwanPacketServiceActionDetach)
         {
            bSendInitiateAttach = TRUE;
#if SPOOF_PS_ONLY_DETACH
            if (pAdapter->DeviceCircuitState != DeviceWWanPacketAttached)
            {
               bSendInitiateAttach = FALSE;            
            }
            if (pAdapter->IsLTE == TRUE)
            {
               bSendInitiateAttach = FALSE;            
            }
#endif
         }
      }

      if (bSendInitiateAttach == TRUE)
      {
          pAdapter->SetSelDomain = 0;
         if (pAdapter->IsNASSysInfoPresent == FALSE)
         {
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                QMINAS_INITIATE_ATTACH_REQ, MPQMUX_ComposeNasInitiateAttachReq, FALSE );
      }
      else
      {
            qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                            QMINAS_GET_SYS_INFO_REQ, NULL, FALSE );
         }
      }
      else
      {
          pAdapter->SetSelDomain = 1;
         if (pAdapter->IsNASSysInfoPresent == FALSE)
         {
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                         QMINAS_GET_SERVING_SYSTEM_REQ, NULL, FALSE );
      }
         else
         {
             qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                             QMINAS_GET_SYS_INFO_REQ, NULL, FALSE );
         }
      }
      if (qmiLen != 0)
      {

         PNDIS_WWAN_PACKET_SERVICE_STATE pNdisPacketServiceState;
         // Fill the basic response OID
         pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
         if( pOID->pOIDResp == NULL )
         {
           QCNET_DbgPrint
           (
             MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
             MP_DBG_LEVEL_ERROR,
             ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
           );
            return 0;
         }
         NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
         pOID->OIDRespLen = sizeof(NDIS_WWAN_PACKET_SERVICE_STATE);
         pNdisPacketServiceState = (PNDIS_WWAN_PACKET_SERVICE_STATE)pOID->pOIDResp;
         pNdisPacketServiceState->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
         pNdisPacketServiceState->Header.Revision = NDIS_WWAN_PACKET_SERVICE_STATE_REVISION_1;
         pNdisPacketServiceState->Header.Size = sizeof(NDIS_WWAN_PACKET_SERVICE_STATE);

         pOID->IndicationType = NDIS_STATUS_WWAN_PACKET_SERVICE;
      }
      break;
   }

   case OID_WWAN_PROVISIONED_CONTEXTS:
   {
      pOID->OIDAsyncType = NDIS_STATUS_INDICATION_REQUIRED;

      if (pAdapter->IsNASSysInfoPresent == FALSE)
      {
      qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                             QMINAS_GET_SERVING_SYSTEM_REQ, NULL, FALSE );
      }
      else
      {
          qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                          QMINAS_GET_SYS_INFO_REQ, NULL, FALSE );
      }
      if (qmiLen != 0)
      {

         PNDIS_WWAN_PROVISIONED_CONTEXTS pNdisContextState;
         // Fill the basic response OID
         pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
         if( pOID->pOIDResp == NULL )
         {
           QCNET_DbgPrint
           (
             MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
             MP_DBG_LEVEL_ERROR,
             ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
           );
            return 0;
         }
         NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
         pOID->OIDRespLen = sizeof(NDIS_WWAN_PROVISIONED_CONTEXTS);
         pNdisContextState = (PNDIS_WWAN_PROVISIONED_CONTEXTS)pOID->pOIDResp;
         pNdisContextState->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
         pNdisContextState->Header.Revision = NDIS_WWAN_PROVISIONED_CONTEXTS_REVISION_1;
         pNdisContextState->Header.Size = sizeof(NDIS_WWAN_PROVISIONED_CONTEXTS);

         pNdisContextState->ContextListHeader.ElementType = WwanStructContext;
         pOID->IndicationType = NDIS_STATUS_WWAN_PROVISIONED_CONTEXTS;
      }
      break;
   }

   case OID_WWAN_CONNECT:
   {

      pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
      if (pAdapter->IsNASSysInfoPresent == FALSE)
      {
      qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                             QMINAS_GET_SERVING_SYSTEM_REQ, NULL, FALSE );
      }
      else
      {
          qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                                          QMINAS_GET_SYS_INFO_REQ, NULL, FALSE );
      }
      if (qmiLen != 0)
      {

         PNDIS_WWAN_CONTEXT_STATE pNdisContextState;
         // Fill the basic response OID
         pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
         if( pOID->pOIDResp == NULL )
         {
           QCNET_DbgPrint
           (
             MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
             MP_DBG_LEVEL_ERROR,
             ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
           );
            return 0;
         }

         // Start with IPv4
         pAdapter->IPType = 0x04;
         NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
         pOID->OIDRespLen = sizeof(NDIS_WWAN_CONTEXT_STATE);
         pNdisContextState = (PNDIS_WWAN_CONTEXT_STATE)pOID->pOIDResp;
         pNdisContextState->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
         pNdisContextState->Header.Revision = NDIS_WWAN_CONTEXT_STATE_REVISION_1;
         pNdisContextState->Header.Size = sizeof(NDIS_WWAN_CONTEXT_STATE);
         pNdisContextState->ContextState.VoiceCallState = WwanVoiceCallStateNone;
         pOID->IndicationType = NDIS_STATUS_WWAN_CONTEXT_STATE;
      }
      break;
   }

   case OID_WWAN_SMS_CONFIGURATION:
   {

      if (pAdapter->DeviceClass == DEVICE_CLASS_GSM ||
         (pAdapter->DeviceClass == DEVICE_CLASS_CDMA && pOID->OidType == fMP_QUERY_OID))
      {
         pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
         qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_WMS, 
                                         QMIWMS_GET_SMSC_ADDRESS_REQ, NULL, FALSE );
         if (qmiLen != 0)
         {

            PNDIS_WWAN_SMS_CONFIGURATION pNdisSMSConfiguration;
            // Fill the basic response OID
            pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
            if( pOID->pOIDResp == NULL )
            {
              QCNET_DbgPrint
              (
                MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                MP_DBG_LEVEL_ERROR,
                ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
              );
               return 0;
            }
            NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
            pOID->OIDRespLen = sizeof(NDIS_WWAN_SMS_CONFIGURATION);
            pNdisSMSConfiguration = (PNDIS_WWAN_SMS_CONFIGURATION)pOID->pOIDResp;
            pNdisSMSConfiguration->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            pNdisSMSConfiguration->Header.Revision = NDIS_WWAN_SMS_CONFIGURATION_REVISION_1;
            pNdisSMSConfiguration->Header.Size = sizeof(NDIS_WWAN_SMS_CONFIGURATION);
            pOID->IndicationType = NDIS_STATUS_WWAN_SMS_CONFIGURATION;

            qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WMS, 
                                            QMIWMS_SET_EVENT_REPORT_REQ, MPQMUX_SendWmsSetEventReportReqSend, TRUE );
            
         }
      }
      else
      {
         status = NDIS_STATUS_NOT_SUPPORTED;      
      }
      break;
   }
   case OID_WWAN_SMS_READ:
   {

      PNDIS_OID_REQUEST pOIDReq = pOID->OidReference;
      pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
      qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_WMS, 
                                      QMIWMS_GET_SMSC_ADDRESS_REQ, NULL, FALSE );
      if (qmiLen != 0)
      {

         PNDIS_WWAN_SMS_RECEIVE pSMSReceive;
         // Fill the basic response OID
         pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
         if( pOID->pOIDResp == NULL )
         {
           QCNET_DbgPrint
           (
             MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
             MP_DBG_LEVEL_ERROR,
             ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
           );
            return 0;
         }
         NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
         pOID->OIDRespLen = sizeof(NDIS_WWAN_SMS_RECEIVE);
         pSMSReceive = (PNDIS_WWAN_SMS_RECEIVE)pOID->pOIDResp;
         pSMSReceive->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
         pSMSReceive->Header.Revision = NDIS_WWAN_SMS_RECEIVE_REVISION_1;
         pSMSReceive->Header.Size = sizeof(NDIS_WWAN_SMS_RECEIVE);
         pSMSReceive->SmsListHeader.ElementType = WwanStructSmsPdu;
         pOID->IndicationType = NDIS_STATUS_WWAN_SMS_RECEIVE;
      }
      break;
   }

   case OID_WWAN_SMS_STATUS:
   {
      PNDIS_OID_REQUEST pOIDReq = pOID->OidReference;
      pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
      qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_WMS, 
                                      QMIWMS_GET_SMSC_ADDRESS_REQ, NULL, FALSE );
      if (qmiLen != 0)
      {

         PNDIS_WWAN_SMS_STATUS pSMSStatus;
         // Fill the basic response OID
         pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
         if( pOID->pOIDResp == NULL )
         {
           QCNET_DbgPrint
           (
             MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
             MP_DBG_LEVEL_ERROR,
             ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
           );
            return 0;
         }
         NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
         pOID->OIDRespLen = sizeof(NDIS_WWAN_SMS_STATUS);
         pSMSStatus = (PNDIS_WWAN_SMS_STATUS)pOID->pOIDResp;
         pSMSStatus->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
         pSMSStatus->Header.Revision = NDIS_WWAN_SMS_STATUS_REVISION_1;
         pSMSStatus->Header.Size = sizeof(NDIS_WWAN_SMS_STATUS);
         pOID->IndicationType = NDIS_STATUS_WWAN_SMS_STATUS;
      }
      break;
   }

   case OID_WWAN_SMS_DELETE:
   {

      PNDIS_OID_REQUEST pOIDReq = pOID->OidReference;
      pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
      qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_WMS, 
                                      QMIWMS_GET_SMSC_ADDRESS_REQ, NULL, FALSE );
      if (qmiLen != 0)
      {

         PNDIS_WWAN_SMS_DELETE_STATUS pSMSDeleteStatus;
         // Fill the basic response OID
         pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
         if( pOID->pOIDResp == NULL )
         {
           QCNET_DbgPrint
           (
             MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
             MP_DBG_LEVEL_ERROR,
             ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
           );
            return 0;
         }
         NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
         pOID->OIDRespLen = sizeof(NDIS_WWAN_SMS_DELETE_STATUS);
         pSMSDeleteStatus = (PNDIS_WWAN_SMS_DELETE_STATUS)pOID->pOIDResp;
         pSMSDeleteStatus->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
         pSMSDeleteStatus->Header.Revision = NDIS_WWAN_SMS_DELETE_STATUS_REVISION_1;
         pSMSDeleteStatus->Header.Size = sizeof(NDIS_WWAN_SMS_DELETE_STATUS);
         pOID->IndicationType = NDIS_STATUS_WWAN_SMS_DELETE;
      }
      break;
   }
   case OID_WWAN_SMS_SEND:
   {

      PNDIS_OID_REQUEST pOIDReq = pOID->OidReference;
      pOID->OIDAsyncType= NDIS_STATUS_INDICATION_REQUIRED;
      qmiLen = MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_WMS, 
                                      QMIWMS_GET_SMSC_ADDRESS_REQ, NULL, FALSE );

      if (qmiLen != 0)
      {

         PNDIS_WWAN_SMS_SEND_STATUS pSMSSendStatus;
         // Fill the basic response OID
         pOID->pOIDResp = ExAllocatePool( NonPagedPool, SIZEOF_OID_RESPONSE );
         if( pOID->pOIDResp == NULL )
         {
           QCNET_DbgPrint
           (
             MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
             MP_DBG_LEVEL_ERROR,
             ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
           );
            return 0;
         }
         NdisZeroMemory( pOID->pOIDResp, SIZEOF_OID_RESPONSE);
         pOID->OIDRespLen = sizeof(NDIS_WWAN_SMS_SEND_STATUS);
         pSMSSendStatus = (PNDIS_WWAN_SMS_SEND_STATUS)pOID->pOIDResp;
         pSMSSendStatus->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
         pSMSSendStatus->Header.Revision = NDIS_WWAN_SMS_SEND_STATUS_REVISION_1;
         pSMSSendStatus->Header.Size = sizeof(NDIS_WWAN_SMS_SEND_STATUS);
         pOID->IndicationType = NDIS_STATUS_WWAN_SMS_SEND;
      }
      break;
   }
#endif //NDIS620_MINIPORT


      // case QMUX_SET_POWER_STATE_*:
      // {
      //    break;
      // }

      default:
      {
         status = NDIS_STATUS_NOT_SUPPORTED;
         break;
      }
   }  // switch

   return qmiLen;

}  // MPQMI_OIDtoQMI

VOID MPQMI_ComposeOIDData
(
   PMP_ADAPTER   pAdapter,
   PMP_OID_WRITE pMatchOid,
   PVOID         pDataBuffer, // generic buffer
   PULONG        pLength,     // generic length
   NDIS_STATUS   NdisStatus
)
{
   ULONG dataLength = 0;
   PCHAR pChar;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> --> MPQMI_ComposeOIDData: pMatchOID 0x%p(st 0x%x)\n",
        pAdapter->PortName, pMatchOid, NdisStatus)
   );

   // fill the generic data buffer and data length
   if ((pMatchOid != NULL) && (NdisStatus == NDIS_STATUS_SUCCESS))
   {
      dataLength = sizeof(ULONG);
      NdisZeroMemory(pDataBuffer, pAdapter->CtrlReceiveSize);

      switch (pMatchOid->Oid)
      {
         case OID_GEN_LINK_SPEED:
         {
            ULONG linkSpeed;
            if (pAdapter->ulServingSystemRxRate > pAdapter->ulServingSystemTxRate)
            {
               linkSpeed = pAdapter->ulServingSystemRxRate / 100;
            }
            else
            {
               linkSpeed = pAdapter->ulServingSystemTxRate / 100;
            }
            NdisMoveMemory(pDataBuffer, (PVOID)&linkSpeed, dataLength);
            break;
         }
         case OID_GEN_XMIT_OK:
         {
            NdisMoveMemory(pDataBuffer, (PVOID)&pAdapter->TxGoodPkts, dataLength);
            break;
         }
         case OID_GEN_RCV_OK:
         {
            NdisMoveMemory(pDataBuffer, (PVOID)&pAdapter->RxGoodPkts, dataLength);
            break;
         }
         case OID_GEN_XMIT_ERROR:
         {
            NdisMoveMemory(pDataBuffer, (PVOID)&pAdapter->BadTransmits, dataLength);
            break;
         }
         case OID_GEN_RCV_ERROR:
         {
            ULONG totalCount;

            totalCount = pAdapter->TxErrorPkts + pAdapter->RxErrorPkts;
            NdisMoveMemory(pDataBuffer, (PVOID)&totalCount, dataLength);
            break;
         }
         case OID_GEN_RCV_NO_BUFFER:
         {
            NdisZeroMemory(pDataBuffer, dataLength);
            break;
         }
         case OID_GEN_VENDOR_DESCRIPTION:
         {
            ULONG len = 0;
            PCHAR p = (PCHAR)pDataBuffer;

            if (pAdapter->DeviceManufacturer != NULL)
            {
               len = strlen(pAdapter->DeviceManufacturer);
               dataLength += (len+1);
               if (dataLength < pAdapter->CtrlReceiveSize)
               {
                  NdisMoveMemory(p, pAdapter->DeviceManufacturer, len);
                  p += (len+1);
               }
               else
               {
                  dataLength -= (len+1);
               }
            }

            if (pAdapter->DeviceMSISDN != NULL)
            {
               len = strlen(pAdapter->DeviceMSISDN);
               dataLength += (len+1);
               if (dataLength < pAdapter->CtrlReceiveSize)
               {
                  if (dataLength > 0)
                  {
                     *p = ':';     // separater
                     p++;
                  }
                  NdisMoveMemory(p, pAdapter->DeviceMSISDN, len);
                  p += (len+1);
               }
               else
               {
                  dataLength -= (len+1);
               }
            }

            break;
         }

         default:
         {
            dataLength = 0;
            break;
         }
      }  // switch (pMatchOID->Oid)
   }

   *pLength = dataLength;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <-- MPQMI_ComposeOIDData: pMatchOID 0x%p(st 0x%x) Len %u\n",
        pAdapter->PortName, pMatchOid, NdisStatus, dataLength)
   );

}  // MPQMI_ComposeOIDData

VOID MPQMI_QMUXtoQMI
(
   PMP_ADAPTER pAdapter,
   UCHAR       QMIType,
   UCHAR       ClientId,
   PVOID       QMUXBuffer,
   ULONG       QMUXBufferLength,
   PVOID       QMIBuffer
)
{
   PQCQMI           qmi = (PQCQMI)QMIBuffer;

   // length checking is conducted in _Write, so we have good msg len here.

   qmi->IFType   = USB_CTL_MSG_TYPE_QMI;
   qmi->Length   = QCQMI_HDR_SIZE + QMUXBufferLength;
   qmi->CtlFlags = 0; // reserved;
   qmi->QMIType  = QMIType;
   qmi->ClientId = ClientId;

   NdisMoveMemory((PVOID)&qmi->SDU, QMUXBuffer, QMUXBufferLength);
}  // MPQMI_QMUXtoQMI

