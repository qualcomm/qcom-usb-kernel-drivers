/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P O I D . C

GENERAL DESCRIPTION
  This module contains Miniport function for handling OID requests.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "MPMain.h"
#include "MPOID.h"
#include "MPUsb.h"
#include "MPQMUX.h"
#include "MPQOS.h"
//#include "qcusbnet_wmi.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPOID.tmh"

#endif   // EVENT_TRACING

NDIS_GUID    QCMPSupportedOidGuids[] = 
{
   {
      {0xDE81837E, 0xBD78, 0x4EC8, 0xAB, 0xA8, 0x89, 0xB7, 0xD0, 0x26, 0xC1, 0xCF},
      OID_GOBI_DEVICE_INFO,
      256,
      fNDIS_GUID_TO_OID | fNDIS_GUID_ANSI_STRING | fNDIS_GUID_ALLOW_READ | fNDIS_GUID_ARRAY
   }
};

NDIS_OID QCMPSupportedOidList[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_PNP_CAPABILITIES,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_SUPPORTED_GUIDS,
    OID_GEN_VENDOR_ID,
    OID_PNP_QUERY_POWER,
    OID_PNP_SET_POWER,

    OID_GEN_LINK_SPEED,  // cache from MSM
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_PROTOCOL_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    // OID_GEN_NETWORK_LAYER_ADDRESSES,
    OID_GEN_PHYSICAL_MEDIUM,

    // -- QMUX_GET_PKT_STATISTICS_REQ
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_BROADCAST_FRAMES_XMIT,
    OID_GEN_BROADCAST_FRAMES_RCV,

    // return 0 for now
    OID_802_3_MAC_OPTIONS,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,

    // WWAN OIDs
    #ifdef NDIS620_MINIPORT
    OID_WWAN_DRIVER_CAPS,
    OID_WWAN_DEVICE_CAPS,
    OID_WWAN_READY_INFO,
    //OID_WWAN_SERVICE_ACTIVATION,
    OID_WWAN_RADIO_STATE,
    OID_WWAN_PIN,
    OID_WWAN_PIN_LIST,
    OID_WWAN_HOME_PROVIDER,
    //OID_WWAN_PREFERRED_PROVIDERS,
    OID_WWAN_VISIBLE_PROVIDERS,
    OID_WWAN_REGISTER_STATE,
    OID_WWAN_SIGNAL_STATE,
    OID_WWAN_PACKET_SERVICE,
    OID_WWAN_PROVISIONED_CONTEXTS,
    OID_WWAN_CONNECT,
    OID_WWAN_SMS_CONFIGURATION,
    OID_WWAN_SMS_READ,
    OID_WWAN_SMS_SEND,
    OID_WWAN_SMS_DELETE,
    OID_WWAN_SMS_STATUS,

#ifdef INTERRUPT_MODERATION    
    OID_GEN_INTERRUPT_MODERATION,
#endif    

    #endif // NDIS620_MINIPORT

#ifdef NDIS60_MINIPORT
    // 6.0 OIDs
    OID_GEN_LINK_PARAMETERS,
    OID_GEN_STATISTICS,
    OID_GEN_LINK_STATE,
#endif

    //CUSTOM OIDS
    OID_GOBI_DEVICE_INFO
};

VOID MPOID_GetSupportedOidListSize(VOID)
{
   QCMP_SupportedOidListSize = sizeof(QCMPSupportedOidList);
}  // MPOID_GetSupportedOidListSize

VOID MPOID_GetSupportedOidGuidsSize(VOID)
{
   QCMP_SupportedOidGuidsSize = sizeof(QCMPSupportedOidGuids);
}

VOID MPOID_ResponseIndication
   (
      PMP_ADAPTER pAdapter,
      NDIS_STATUS OIDStatus,
      PVOID       OidReference
   )
{
   #ifdef NDIS620_MINIPORT

   PNDIS_STATUS_INDICATION statusIndication;
   PMP_OID_WRITE pOid = (PMP_OID_WRITE)OidReference; 

   statusIndication = ExAllocatePool( NonPagedPool, sizeof(NDIS_STATUS_INDICATION) );
   if( statusIndication == NULL )
   {
     QCNET_DbgPrint
     (
       MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
       MP_DBG_LEVEL_ERROR,
       ("<%s> error: Failed to allocate memory for return buffer\n", pAdapter->PortName)
     );
      return;
   }

   QCNET_DbgPrint
   (
     MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
     MP_DBG_LEVEL_DETAIL,
     ("<%s> OID Response OID 0x%x, Status : 0x%x: \n", pAdapter->PortName, 
     pOid->Oid, OIDStatus)
   );
   
   NdisZeroMemory( statusIndication, sizeof(NDIS_STATUS_INDICATION) );
   statusIndication->Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
   statusIndication->Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
   statusIndication->Header.Size = sizeof(NDIS_STATUS_INDICATION);
   statusIndication->DestinationHandle = pOid->OidReqCopy.RequestHandle;
   statusIndication->RequestId         = pOid->OidReqCopy.RequestId;
   statusIndication->StatusCode = pOid->IndicationType;
   statusIndication->StatusBuffer = pOid->pOIDResp;
   statusIndication->StatusBufferSize = pOid->OIDRespLen;
   NdisMIndicateStatusEx(pAdapter->AdapterHandle, statusIndication);
   ExFreePool(statusIndication);

   #else

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> MPOID_ResponseIndication: ERROR - WRONG call\n", pAdapter->PortName)
   );

   #endif // NDIS620_MINIPORT
}

#ifdef NDIS620_MINIPORT

VOID MPOID_IndicationReadyInfo
   (
      PMP_ADAPTER pAdapter,
      WWAN_READY_STATE ReadyState
   )
{
   pAdapter->ReadyInfo.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
   pAdapter->ReadyInfo.Header.Revision  = NDIS_WWAN_READY_INFO_REVISION_1;
   pAdapter->ReadyInfo.Header.Size  = sizeof(NDIS_WWAN_READY_INFO);
   pAdapter->ReadyInfo.ReadyInfo.EmergencyMode = WwanEmergencyModeOff;
   pAdapter->ReadyInfo.ReadyInfo.ReadyState = ReadyState;
   
   MyNdisMIndicateStatus
   (
      pAdapter->AdapterHandle,
      NDIS_STATUS_WWAN_READY_INFO,
      (PVOID)&pAdapter->ReadyInfo,
      sizeof(NDIS_WWAN_READY_INFO) + pAdapter->ReadyInfo.ReadyInfo.TNListHeader.ElementCount*WWAN_TN_LEN*sizeof(WCHAR)
   );
}

#endif

VOID MPOID_CompleteOid
(
   PMP_ADAPTER pAdapter,
   NDIS_STATUS OIDStatus,
   PVOID       OidReference,
   ULONG       OidType,
   BOOLEAN     SpinlockHeld
)
{
   #ifdef NDIS60_MINIPORT
   if (QCMP_NDIS6_Ok == TRUE)
   {
      MPOID_FindAndCompleteOidRequest
      (
         pAdapter,
         OidReference,
         0,
         OIDStatus,
         SpinlockHeld
      );
   }
   // else
   #else
   {
      if (OidType == fMP_QUERY_OID)
      {
         NdisMQueryInformationComplete(pAdapter->AdapterHandle, OIDStatus);
      }
      else
      {
         NdisMSetInformationComplete(pAdapter->AdapterHandle, OIDStatus);
      }
   }
   #endif  // NDIS60_MINIPORT
}  // MPOID_CompleteOid

#ifdef NDIS60_MINIPORT

NDIS_STATUS MPOID_MiniportOidRequest
(
   IN NDIS_HANDLE       MiniportAdapterContext,
   IN PNDIS_OID_REQUEST NdisRequest
)
{
   PMP_ADAPTER       pAdapter = (PMP_ADAPTER)MiniportAdapterContext;
   NDIS_STATUS       oidStatus, ndisStatus = NDIS_STATUS_PENDING;
   PQCMP_OID_REQUEST req;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->MPOidRequest: 0x%p\n", pAdapter->PortName, NdisRequest)
   );

   if ((pAdapter->Flags & fMP_ANY_FLAGS) != 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_TRACE,
         ("<%s> <--MPOidRequest: 0x%p ABORTED (MP_Flas 0x%x)\n", pAdapter->PortName,
           NdisRequest, pAdapter->Flags)
      );
      return NDIS_STATUS_REQUEST_ABORTED;
   }

   // Enqueue the OID request:
   // Due to incompatibility between NDIS5.1 and NDIS6, we need to enqueue
   // the OID request to provide extensibility and flexibility. This appears
   // to be redundant because the old-fashioned OID is enqueued again in
   // MPOID_SetInformation() and MPOID_QueryInformation(). But do we have a better
   // choice to eliminate such redundancy while maintaining compatibility?

   req = ExAllocatePoolWithTag
         (
            NonPagedPool,
            sizeof(QCMP_OID_REQUEST),
            (ULONG)'DIOQ'
         );

   if (req == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_ERROR,
         ("<%s> MPOidRequest: no_mem(NDIS_STATUS_NOT_ACCEPTED)\n", pAdapter->PortName)
      );
      return NDIS_STATUS_NOT_ACCEPTED;
   }

   req->OidRequest = NdisRequest;

   switch (NdisRequest->RequestType)
   {
       case NdisRequestMethod:
           oidStatus = MPMethod(pAdapter, NdisRequest);
           break;

       case NdisRequestSetInformation:
       {
           NdisRequest->DATA.SET_INFORMATION.BytesRead   = 0;
           NdisRequest->DATA.SET_INFORMATION.BytesNeeded = 0;
           oidStatus = MPOID_SetInformation
                       (
                          pAdapter,
                          NdisRequest->DATA.SET_INFORMATION.Oid,
                          req,  // to extract InformationBuffer later on
                          NdisRequest->DATA.SET_INFORMATION.InformationBufferLength,
                          &(NdisRequest->DATA.SET_INFORMATION.BytesRead),
                          &(NdisRequest->DATA.SET_INFORMATION.BytesNeeded)
                       );
           break;
       }
       case NdisRequestQueryInformation:
       case NdisRequestQueryStatistics:
       {
          NdisRequest->DATA.QUERY_INFORMATION.BytesWritten = 0;
          NdisRequest->DATA.QUERY_INFORMATION.BytesNeeded  = 0;
          oidStatus = MPOID_QueryInformation
                      (
                         pAdapter,
                         NdisRequest->DATA.QUERY_INFORMATION.Oid,
                         req,  // to extract InformationBuffer later on
                         NdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength,
                         &(NdisRequest->DATA.QUERY_INFORMATION.BytesWritten),
                         &(NdisRequest->DATA.QUERY_INFORMATION.BytesNeeded)
                      );
           break;
       }
       default:
       {
           QCNET_DbgPrint
           (
              MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
              MP_DBG_LEVEL_DETAIL,
              ("<%s> MPOidRequest: NOT_SUPPORTED\n", pAdapter->PortName)
           );
           oidStatus = NDIS_STATUS_NOT_SUPPORTED;
           break;
       }
   }

   ndisStatus = oidStatus;
   if (oidStatus != NDIS_STATUS_PENDING && oidStatus != NDIS_STATUS_INDICATION_REQUIRED)
   {
      // direct return, req is not enqueued
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_TRACE,
         ("<%s> OID (Cd 0x%p) ST 0x%x\n", pAdapter->PortName, NdisRequest, ndisStatus)
      );
      ExFreePool(req);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--MPOidRequest: 0x%p (0x%x) P%d\n", pAdapter->PortName, NdisRequest, ndisStatus, pAdapter->nPendingOidReq)
   );

   return ndisStatus;

}  // MPOidRequest

NDIS_STATUS MPMethod
(
   IN NDIS_HANDLE       MiniportAdapterContext,
   IN PNDIS_OID_REQUEST NdisRequest
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)MiniportAdapterContext;
   NDIS_OID oid = NdisRequest->DATA.METHOD_INFORMATION.Oid;
   PVOID informationBuffer = (PVOID)(NdisRequest->DATA.METHOD_INFORMATION.InformationBuffer);
   ULONG inputBufferLength = NdisRequest->DATA.METHOD_INFORMATION.InputBufferLength;
   ULONG outputBufferLength = NdisRequest->DATA.METHOD_INFORMATION.OutputBufferLength;
   ULONG methodId = NdisRequest->DATA.METHOD_INFORMATION.MethodId;
   ULONG bytesNeeded = 0, bytesWritten = 0, bytesRead = 0;
   NDIS_STATUS status = NDIS_STATUS_SUCCESS;
 
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->MPMethod: 0x%p\n", pAdapter->PortName, NdisRequest)
   );

   switch (oid)
   {
      default:
      {
         status = NDIS_STATUS_NOT_SUPPORTED;
         break;
      }
   }

   if (status != NDIS_STATUS_SUCCESS)
   {
      NdisRequest->DATA.METHOD_INFORMATION.BytesNeeded = bytesNeeded;
   }
   else
   {
      NdisRequest->DATA.METHOD_INFORMATION.BytesWritten = bytesWritten;
      NdisRequest->DATA.METHOD_INFORMATION.BytesRead = bytesRead;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--MPMethod: 0x%p\n", pAdapter->PortName, NdisRequest)
   );

   return status;
}  // MPMethod

VOID MPOID_NdisMOidRequestComplete(
                   PMP_ADAPTER pAdapter,
                   PQCMP_OID_REQUEST req,
                   NDIS_STATUS NdisStatus)
{
   NdisMOidRequestComplete
   (
      pAdapter->AdapterHandle,
      req->OidRequest,
      NdisStatus
   );
}

VOID MPOID_CancelAllPendingOids(PMP_ADAPTER pAdapter)
{
   PLIST_ENTRY       tailOfList;
   PQCMP_OID_REQUEST req;
   int               numCompleted = 0;
   NDIS_STATUS       ndisStatus = NDIS_STATUS_REQUEST_ABORTED;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->_CancelAllPendingOids: C%d/P%d\n", pAdapter->PortName,
        numCompleted, pAdapter->nPendingOidReq)
   );

   NdisAcquireSpinLock(&pAdapter->OIDLock);

   while (!IsListEmpty(&pAdapter->OidRequestList))
   {
      tailOfList = RemoveTailList(&pAdapter->OidRequestList);
      req = CONTAINING_RECORD(tailOfList, QCMP_OID_REQUEST, List);

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_ERROR,
         ("<%s> OID (Cx 0x%p) ST 0x%x\n", pAdapter->PortName, req->OidRequest, ndisStatus)
      );
      NdisReleaseSpinLock(&pAdapter->OIDLock);

      MPOID_NdisMOidRequestComplete
      (
         pAdapter,
         req,
         ndisStatus
      );

      ExFreePool(req);
      
      InterlockedDecrement(&pAdapter->nPendingOidReq);
      ++numCompleted;
      NdisAcquireSpinLock(&pAdapter->OIDLock);
   }

   NdisReleaseSpinLock(&pAdapter->OIDLock);


   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--_CancelAllPendingOids: C%d/P%d\n", pAdapter->PortName,
        numCompleted, pAdapter->nPendingOidReq)
   );
}  // MPOID_CancelAllPendingOids

NDIS_STATUS MPOID_FindAndCompleteOidRequest
(
   PMP_ADAPTER       pAdapter,
   PVOID             OidRequest,
   PVOID             RequestId,
   NDIS_STATUS       NdisStatus,
   BOOLEAN           SpinlockHeld
)
{
   PLIST_ENTRY       tailOfList;
   LIST_ENTRY        matchingList, tmpList;
   PQCMP_OID_REQUEST req, foundReq = NULL;
   int               numCompleted  = 0;
   PNDIS_OID_REQUEST NdisRequest   = NULL;
   PMP_OID_WRITE     oidReq        = NULL;
   NDIS_STATUS       retST         = NDIS_STATUS_SUCCESS;

   if (OidRequest != NULL)
   {
      oidReq = (PMP_OID_WRITE)OidRequest;
      if (oidReq->OidReference != NULL)
      {
         NdisRequest = (PNDIS_OID_REQUEST)(oidReq->OidReference);
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->_CompleteOidRequest: 0x%p/0x%p(ST 0x%x)-P%d\n", pAdapter->PortName,
        NdisRequest, RequestId, NdisStatus, pAdapter->nPendingOidReq)
   );

   InitializeListHead(&matchingList);
   InitializeListHead(&tmpList);

   // Examine OidRequestList to find the NdisRequest or RequestId

   if (SpinlockHeld == FALSE)
   {
      NdisAcquireSpinLock(&pAdapter->OIDLock);
   }

   if (IsListEmpty(&pAdapter->OidRequestList))
   {
      if (SpinlockHeld == FALSE)
      {
         NdisReleaseSpinLock(&pAdapter->OIDLock);
      }
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_ERROR,
         ("<%s> <--_CompleteOidRequest: OID queued empty\n", pAdapter->PortName)
      );

      retST = NDIS_STATUS_FAILURE;
      goto CleanupAndExit;
   }

   while (!IsListEmpty(&pAdapter->OidRequestList))
   {
      tailOfList = RemoveTailList(&pAdapter->OidRequestList);
      req = CONTAINING_RECORD(tailOfList, QCMP_OID_REQUEST, List);
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_ERROR,
         ("<%s> OID (Find and complete 0x%p) \n", pAdapter->PortName, req->OidRequest)
      );

      if (NdisRequest != NULL)
      {
         if (req->OidRequest == NdisRequest)
         {
            InsertHeadList(&matchingList, tailOfList);
         }
         else
         {
            InsertHeadList(&tmpList, tailOfList);
         }
      }
      else if (RequestId != 0)
      {
         if (req->OidRequest->RequestId == RequestId)
         {
            InsertHeadList(&matchingList, tailOfList);
         }
         else
         {
            InsertHeadList(&tmpList, tailOfList);
         }
      }
   }

   while (!IsListEmpty(&tmpList))
   {
      PQCMP_OID_REQUEST req;
      tailOfList = RemoveTailList(&tmpList);
      req = CONTAINING_RECORD(tailOfList, QCMP_OID_REQUEST, List);
      InsertHeadList(&pAdapter->OidRequestList, tailOfList);
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_ERROR,
         ("<%s> OID (Temp enqueue 0x%p) \n", pAdapter->PortName, req->OidRequest)
      );
      
   }

   if (SpinlockHeld == FALSE)
   {
      NdisReleaseSpinLock(&pAdapter->OIDLock);
   }

   while (!IsListEmpty(&matchingList))
   {
      tailOfList = RemoveTailList(&matchingList);
      req = CONTAINING_RECORD(tailOfList, QCMP_OID_REQUEST, List);

      if (NdisStatus == NDIS_STATUS_SUCCESS)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> OID (C 0x%p) ST 0x%x\n", pAdapter->PortName, req->OidRequest, NdisStatus)
         );
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
            MP_DBG_LEVEL_ERROR,
            ("<%s> OID (Ce 0x%p) ST 0x%x\n", pAdapter->PortName, req->OidRequest, NdisStatus)
         );
      }

      // fill NDIS request
      if ((oidReq != NULL) && (NdisRequest != NULL) && (req->OidRequest == NdisRequest))
      {
         switch (NdisRequest->RequestType)
         {
            case NdisRequestSetInformation:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                  MP_DBG_LEVEL_TRACE,
                  ("<%s> filling SET OID 0x%p BUF(0x%p, 0x%p)\n", pAdapter->PortName, req->OidRequest,
                    oidReq->RequestInfoBuffer,
                    oidReq->OidReqCopy.DATA.QUERY_INFORMATION.InformationBuffer)
               );

               if (SpinlockHeld == FALSE)
               {
                  NdisAcquireSpinLock(&pAdapter->OIDLock);
               }
               if (oidReq->RequestBytesUsed != NULL)  // should check RequestBytesNeeded too
               {
                  NdisRequest->DATA.SET_INFORMATION.BytesRead   = *(oidReq->RequestBytesUsed);
                  NdisRequest->DATA.SET_INFORMATION.BytesNeeded = *(oidReq->RequestBytesNeeded);
               }
               if (SpinlockHeld == FALSE)
               {
                  NdisReleaseSpinLock(&pAdapter->OIDLock);
               }

               break;
            }

            case NdisRequestQueryInformation:
            case NdisRequestQueryStatistics:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                  MP_DBG_LEVEL_TRACE,
                  ("<%s> filling QUERY OID 0x%p BUF(0x%p, 0x%p)\n", pAdapter->PortName, req->OidRequest,
                    oidReq->RequestInfoBuffer,
                    oidReq->OidReqCopy.DATA.SET_INFORMATION.InformationBuffer)
               );

               if (SpinlockHeld == FALSE)
               {
                  NdisAcquireSpinLock(&pAdapter->OIDLock);
               }
               if (oidReq->RequestBytesUsed != NULL)  // should check RequestBytesNeeded too
               {
                  NdisRequest->DATA.QUERY_INFORMATION.BytesWritten = *(oidReq->RequestBytesUsed);
                  NdisRequest->DATA.QUERY_INFORMATION.BytesNeeded  = *(oidReq->RequestBytesNeeded);
               }
               if (SpinlockHeld == FALSE)
               {
                  NdisReleaseSpinLock(&pAdapter->OIDLock);
               }

               if ((NdisStatus == NDIS_STATUS_SUCCESS) &&
                   (oidReq->RequestInfoBuffer != NULL) &&
                   (*(oidReq->RequestBytesUsed) > 0))
               {
                  // double check
                  if (*(oidReq->RequestBytesUsed) <= NdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength)
                  {
                     RtlCopyMemory
                     (
                        NdisRequest->DATA.QUERY_INFORMATION.InformationBuffer,
                        oidReq->RequestInfoBuffer,
                        *(oidReq->RequestBytesUsed)
                     );
                  }
                  else
                  {
                     NdisStatus = NDIS_STATUS_BUFFER_TOO_SHORT;
                  }
               }
               break;
            }

            default:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> filling OID error: 0x%p\n", pAdapter->PortName, req->OidRequest)
               );

               NdisStatus = NDIS_STATUS_NOT_SUPPORTED;
               break;
            }
         }

         if (oidReq->RequestInfoBuffer != NULL)
         {
            oidReq->RequestInfoBuffer = NULL;
         }
         else
         {
            // error msg???
         }
      }  // if ((oidReq != NULL) && (NdisRequest != NULL) && (req->OidRequest == NdisRequest))

      NdisMOidRequestComplete
      (
         pAdapter->AdapterHandle,
         req->OidRequest,
         NdisStatus
      );
      ExFreePool(req);
      InterlockedDecrement(&pAdapter->nPendingOidReq);
      ++numCompleted;
   }

   if (numCompleted == 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_ERROR,
         ("<%s> _CompleteOidRequest: already completed 0x%p/0x%p, recycle 0x%p\n",
           pAdapter->PortName, NdisRequest, RequestId, oidReq)
      );
   }

CleanupAndExit:

   if (SpinlockHeld == FALSE)
   {
      NdisAcquireSpinLock(&pAdapter->OIDLock);
   }
   MPOID_CleanupOidCopy(pAdapter, oidReq);
   if (SpinlockHeld == FALSE)
   {
      NdisReleaseSpinLock(&pAdapter->OIDLock);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--_CompleteOidRequest: 0x%p/0x%p-C%d/P%d (0x%x)\n", pAdapter->PortName,
        NdisRequest, RequestId, numCompleted, pAdapter->nPendingOidReq, retST)
   );

   return retST;
}  // MPOID_FindAndCompleteOidRequest

VOID MPOID_MiniportCancelOidRequest
(
   NDIS_HANDLE MiniportAdapterContext,
   PVOID       RequestId
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)MiniportAdapterContext;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->MPOID_MiniportCancelOidRequest: 0x%p\n", pAdapter->PortName, RequestId)
   );

   MPOID_FindAndCompleteOidRequest
   (
      pAdapter,
      NULL,
      RequestId,
      NDIS_STATUS_REQUEST_ABORTED,
      FALSE
   );

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--MPOID_MiniportCancelOidRequest: 0x%p\n", pAdapter->PortName, RequestId)
   );
} // MPOID_MiniportCancelOidRequest

NDIS_STATUS MPDirectOidRequest
(
   IN NDIS_HANDLE       MiniportAdapterContext,
   IN PNDIS_OID_REQUEST NdisRequest
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)MiniportAdapterContext;
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("\n<%s> -->MPDirectOidRequest", gDeviceName) 
   );
   // ...
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("\n<%s> <--MPDirectOidRequest", gDeviceName)
   );

   return NDIS_STATUS_SUCCESS;
}

VOID MPCancelDirectOidRequest
(
   NDIS_HANDLE MiniportAdapterContext,
   PVOID       RequestId
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)MiniportAdapterContext;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("\n<%s> -->MPCancelDirectOidRequest", gDeviceName) 
   );
   // ...
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("\n<%s> <--MPCancelDirectOidRequest", gDeviceName) 
   );
}

NDIS_STATUS MPOID_CreateOidCopy
(
   PMP_ADAPTER       pAdapter,
   PNDIS_OID_REQUEST NdisOidRequest,
   PMP_OID_WRITE     OidWrite
)
{
   NDIS_STATUS ndisStatus = NDIS_STATUS_SUCCESS;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPOID_CreateOidCopy: 0x%p/0x%p-P%d\n", pAdapter->PortName,
        NdisOidRequest, OidWrite, pAdapter->nPendingOidReq)
   );

   OidWrite->OidReference             = NdisOidRequest;
   OidWrite->OidReqCopy.RequestType   = NdisOidRequest->RequestType;
   OidWrite->OidReqCopy.RequestId     = NdisOidRequest->RequestId;
   OidWrite->OidReqCopy.RequestHandle = NdisOidRequest->RequestHandle;

   switch (NdisOidRequest->RequestType)
   {
       case NdisRequestSetInformation:
       {
          PVOID buf = NULL;
          UINT  length;

          OidWrite->OidReqCopy.DATA.SET_INFORMATION.Oid =
             NdisOidRequest->DATA.SET_INFORMATION.Oid;
          OidWrite->OidReqCopy.DATA.SET_INFORMATION.InformationBufferLength =
             length = NdisOidRequest->DATA.SET_INFORMATION.InformationBufferLength;
          OidWrite->OidReqCopy.DATA.SET_INFORMATION.BytesRead =
             NdisOidRequest->DATA.SET_INFORMATION.BytesRead;
          OidWrite->OidReqCopy.DATA.SET_INFORMATION.BytesNeeded =
             NdisOidRequest->DATA.SET_INFORMATION.BytesNeeded;

          if (length > 0)
          {
             // allocate information buffer
             buf = ExAllocatePoolWithTag(NonPagedPool, length, (ULONG)'1DIO');
             if (buf == NULL)
             {
                ndisStatus = NDIS_STATUS_FAILURE;
                OidWrite->OidReqCopy.DATA.SET_INFORMATION.InformationBufferLength = 0;
             }
             else
             {
                RtlCopyMemory(buf, NdisOidRequest->DATA.SET_INFORMATION.InformationBuffer, length);
             }
          }
          OidWrite->OidReqCopy.DATA.SET_INFORMATION.InformationBuffer = buf;

          // hand over
          OidWrite->RequestInfoBuffer   = buf;
          OidWrite->RequestBytesUsed    = &(OidWrite->OidReqCopy.DATA.SET_INFORMATION.BytesRead);
          OidWrite->RequestBytesNeeded  = &(OidWrite->OidReqCopy.DATA.SET_INFORMATION.BytesNeeded);

          break;
       }
       case NdisRequestQueryInformation:
       case NdisRequestQueryStatistics:
       {
          PVOID buf = NULL;
          UINT  length;

          OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.Oid =
             NdisOidRequest->DATA.QUERY_INFORMATION.Oid;
          OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.InformationBufferLength =
             length = NdisOidRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
          OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.BytesWritten =
             NdisOidRequest->DATA.QUERY_INFORMATION.BytesWritten;
          OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.BytesNeeded =
             NdisOidRequest->DATA.QUERY_INFORMATION.BytesNeeded;

          if (length > 0)
          {
             // allocate information buffer
             buf = ExAllocatePoolWithTag(NonPagedPool, length, (ULONG)'2DIO');
             if (buf == NULL)
             {
                ndisStatus = NDIS_STATUS_FAILURE;
                OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.InformationBufferLength = 0;
             }
             else
             {
                RtlCopyMemory(buf, NdisOidRequest->DATA.QUERY_INFORMATION.InformationBuffer, length);
             }
          }
          OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.InformationBuffer = buf;

          // hand over
          OidWrite->RequestInfoBuffer   = buf;
          OidWrite->RequestBytesUsed    = &(OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.BytesWritten);
          OidWrite->RequestBytesNeeded  = &(OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.BytesNeeded);

          break;
       }

       default:
       {
          NdisZeroMemory(&(OidWrite->OidReqCopy), sizeof(QCMP_OID_COPY));
          ndisStatus = NDIS_STATUS_FAILURE;
          break;
       }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPOID_CreateOidCopy: 0x%p-P%d (ST 0x%x)\n", pAdapter->PortName,
        NdisOidRequest, pAdapter->nPendingOidReq, ndisStatus)
   );

   return ndisStatus;
}  // MPOID_CreateOidCopy

#endif // NDIS60_MINIPORT

// This function needs to be called with OIDLock held
VOID MPOID_CleanupOidCopy
(
   PMP_ADAPTER       pAdapter,
   PMP_OID_WRITE     OidWrite
)
{
   ULONG cleaned = 0;  // for tracking purpose

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->MPOID_CleanupOidCopy: 0x%p/0x%p-P%d\n", pAdapter->PortName,
        OidWrite->OidReference, OidWrite, pAdapter->nPendingOidReq)
   );

   if (OidWrite == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_TRACE,
         ("<%s> <--MPOID_CleanupOidCopy: NULL OID-P%d\n", pAdapter->PortName,
           pAdapter->nPendingOidReq)
      );
      return;
   }

   switch (OidWrite->OidReqCopy.RequestType)
   {
       case NdisRequestSetInformation:
       {
          if (NULL != OidWrite->OidReqCopy.DATA.SET_INFORMATION.InformationBuffer)
          {
             ExFreePool(OidWrite->OidReqCopy.DATA.SET_INFORMATION.InformationBuffer);
             OidWrite->OidReqCopy.DATA.SET_INFORMATION.InformationBuffer = NULL;
             cleaned |= 0x01;
          }
          break;
       }

       case NdisRequestQueryInformation:
       case NdisRequestQueryStatistics:
       {
          if (NULL != OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.InformationBuffer)
          {
             ExFreePool(OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.InformationBuffer);
             OidWrite->OidReqCopy.DATA.QUERY_INFORMATION.InformationBuffer = NULL;
             cleaned |= 0x02;
          }
          break;
       }
   }

   /******
   if (!IsListEmpty(&(OidWrite->QMIQueue)))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> MPOID_CleanupOidCopy: ERROR-QMIQ not empty!(0x%p)\n", pAdapter->PortName,
           OidWrite->OidReference)
      );
      DbgBreakPoint();
   }
   ******/

   NdisZeroMemory(&(OidWrite->OidReqCopy), sizeof(QCMP_OID_COPY));
   OidWrite->RequestInfoBuffer   = (PUCHAR) NULL;
   OidWrite->RequestBytesUsed    = (PULONG) NULL;
   OidWrite->RequestBytesNeeded  = (PULONG) NULL;
   OidWrite->RequestBufferLength = 0;
   OidWrite->Oid                 = 0;
   OidWrite->OidType             = 0;
   OidWrite->OidReference        = 0;

   if (OidWrite->pOIDResp != NULL)
   {
      ExFreePool(OidWrite->pOIDResp);
      OidWrite->pOIDResp = NULL;
      OidWrite->OIDRespLen = 0;
      cleaned |= 0x04;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--MPOID_CleanupOidCopy: 0x%p-P%d C0x%X\n", pAdapter->PortName,
        OidWrite->OidReference, pAdapter->nPendingOidReq, cleaned)
   );
}  // MPOID_CleanupOidCopy


/*** MiniportQueryInformation ***/

VOID MPOID_GetInformation
(
   PVOID *DestBuffer,
   PULONG DestLength,
   PVOID SrcBuffer,
   ULONG SrcLength
)
{
   if (SrcBuffer != NULL)
   {
      *DestBuffer = SrcBuffer;
   }
   *DestLength = SrcLength;
}  // MPOID_GetInformation

VOID MPOID_GetInformationUL
(
   PULONG DestBuffer,
   PULONG DestLength,
   ULONG  SrcValue
)
{
   *DestBuffer = SrcValue;
   *DestLength = sizeof(ULONG);
}  // MPOID_GetInformationUL

NDIS_STATUS MPOID_QueryInformation
(
   IN NDIS_HANDLE  MiniportAdapterContext,
   IN NDIS_OID     Oid,
   IN PVOID        InformationBuffer,
   IN ULONG        InformationBufferLength,
   OUT PULONG      BytesWritten,
   OUT PULONG      BytesNeeded
)
{
    PMP_ADAPTER             pAdapter;
    NDIS_HARDWARE_STATUS    HardwareStatus = NdisHardwareStatusReady;
    NDIS_MEDIUM             Medium = QCMP_MEDIA_TYPE;
    NDIS_PHYSICAL_MEDIUM    myPhysicalMedium = QCMP_PHYSICAL_MEDIUM_TYPE;
    ULONG                   ulInfo;
    USHORT                  usInfo;                                                              
    ULONG64                 ulInfo64;
    PVOID                   pInfo = (PVOID) &ulInfo;
    ULONG                   ulInfoLen = sizeof(ulInfo);   
    UCHAR                   VendorDesc[] = "QUALCOMM INC WWAN";
    CHAR                    VendorDesc2[256];
    NDIS_STATUS             OidStatus = NDIS_STATUS_SUCCESS, Status = NDIS_STATUS_SUCCESS;

    BOOLEAN                forwardQuery = FALSE;
    PLIST_ENTRY            pList;
    PMP_OID_WRITE          pOID;
    NDIS_PNP_CAPABILITIES  cap;
    KIRQL                  irql = KeGetCurrentIrql();
    #ifdef NDIS620_MINIPORT
    NDIS_WWAN_DRIVER_CAPS driverCaps;
    #endif // NDIS620_MINIPORT

    #ifdef NDIS60_MINIPORT
    PQCMP_OID_REQUEST qcmpReq;
    PNDIS_OID_REQUEST oidRequest = NULL;
    NDIS_STATISTICS_INFO StatisticsInfo;
#ifdef INTERRUPT_MODERATION    
    NDIS_INTERRUPT_MODERATION_PARAMETERS InterruptModeration;    
#endif
    #endif // NDIS60_MINIPORT

    pAdapter = (PMP_ADAPTER) MiniportAdapterContext;

    #ifdef NDIS60_MINIPORT
    qcmpReq = (PQCMP_OID_REQUEST)InformationBuffer;
    oidRequest = qcmpReq->OidRequest;
    InformationBuffer = oidRequest->DATA.QUERY_INFORMATION.InformationBuffer;
    #endif // NDIS60_MINIPORT

#ifdef NDIS620_MINIPORT
    if (QCMP_NDIS620_Ok == TRUE)
    {
#ifdef QC_IP_MODE      
       if (pAdapter->IPModeEnabled == TRUE)
       {
          Medium = QCMP_MEDIA_TYPE_WWAN;
       }
#endif // QC_IP_MODE
       myPhysicalMedium = QCMP_PHYSICAL_MEDIUM_TYPEWWAN;
    }
#endif

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
       MP_DBG_LEVEL_TRACE,
       ("<%s> ---> MPOID_QueryInformation %u/%u <S%u, R%u> QueryOID: %s (0x%x) IRQL%u\n", pAdapter->PortName,
         pAdapter->nBusyOID, pAdapter->nBusyTx,
         pAdapter->ulMaxBusySends, pAdapter->ulMaxBusyRecvs, MPOID_OidToNameStr(Oid), Oid, irql)
    );

    // Initialize OUT values
    if (BytesWritten == NULL || BytesNeeded == NULL)
    {
        return NDIS_STATUS_INVALID_DATA;
    }
    else
    {
       *BytesWritten = *BytesNeeded = 0;
    }

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
        {
           MPOID_GetInformation(&pInfo, &ulInfoLen, QCMPSupportedOidList, QCMP_SupportedOidListSize);
           break;
        }
        
        case OID_GEN_HARDWARE_STATUS:
        {
           MPOID_GetInformation(&pInfo, &ulInfoLen, &HardwareStatus, sizeof(NDIS_HARDWARE_STATUS));
           break;
        }

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
        {
           MPOID_GetInformation(&pInfo, &ulInfoLen, &Medium, sizeof(NDIS_MEDIUM));
           break;
        }

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:
        {
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, MAX_LOOKAHEAD);
           break;            
        }

        case OID_GEN_MAXIMUM_FRAME_SIZE:
        {
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, pAdapter->MTUSize);
           break;
        }

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
        {
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, (ULONG)ETH_MAX_PACKET_SIZE);
           break;
        }

        case OID_GEN_MAC_OPTIONS:
        {
           ULONG val = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA | 
                       NDIS_MAC_OPTION_TRANSFERS_NOT_PEND  |
                       NDIS_MAC_OPTION_NO_LOOPBACK         |
                       NDIS_MAC_OPTION_8021P_PRIORITY      |
                       NDIS_MAC_OPTION_8021Q_VLAN;
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, val);
           break;
        }

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
        {
           // If the data pipe is occupied, we return no capacity
           if (listDepth(&pAdapter->TxPendingList, &pAdapter->TxLock) ||
               listDepth(&pAdapter->TxBusyList, &pAdapter->TxLock))
           {
              MPOID_GetInformationUL(&ulInfo, &ulInfoLen, 0);
           }
           else
           {
              MPOID_GetInformationUL(&ulInfo, &ulInfoLen, ETH_MAX_PACKET_SIZE);
           }
           break;
        }
        case OID_GEN_RECEIVE_BUFFER_SPACE:
        {
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, ETH_MAX_PACKET_SIZE);
           break;
        }

        case OID_GEN_VENDOR_DRIVER_VERSION:
        {
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, VENDOR_DRIVER_VERSION);
           break;
        }
        case OID_GEN_DRIVER_VERSION:
            usInfo = (USHORT) (MP_NDIS_MAJOR_VERSION<<8) + MP_NDIS_MINOR_VERSION;
#ifdef NDIS620_MINIPORT
            if (QCMP_NDIS620_Ok == TRUE)
            {
               usInfo = (USHORT) (MP_NDIS_MAJOR_VERSION620<<8) + MP_NDIS_MINOR_VERSION620;
            }
#endif
            pInfo = (PVOID) &usInfo;
            ulInfoLen = sizeof(USHORT);
            break;

        case OID_GEN_MAXIMUM_SEND_PACKETS:
        {
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, pAdapter->MaxDataSends);
           break;
        }

        case OID_PNP_CAPABILITIES:
        {
           if (InformationBufferLength < sizeof(NDIS_PNP_CAPABILITIES))
           {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_CONTROL,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> OID_PNP_CAPABILITIES: BUF SHORT %d\n", pAdapter->PortName, InformationBufferLength)
              );

              *BytesNeeded = sizeof(NDIS_PNP_CAPABILITIES);
              *BytesWritten = 0;
              return NDIS_STATUS_BUFFER_TOO_SHORT;
           }
           if (InformationBuffer == NULL)
           {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_CONTROL,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> OID_PNP_CAPABILITIES: NIL BUF\n", pAdapter->PortName)
              );
              *BytesNeeded = sizeof(NDIS_PNP_CAPABILITIES);
              *BytesWritten = 0;
              return NDIS_STATUS_BUFFER_TOO_SHORT;
           }
           else
           {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_CONTROL,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> OID_PNP_CAPABILITIES: all Unspecified (len %d)\n", pAdapter->PortName, InformationBufferLength)
              );
              Status = NDIS_STATUS_SUCCESS;
              cap.WakeUpCapabilities.MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
              cap.WakeUpCapabilities.MinPatternWakeUp     = NdisDeviceStateUnspecified;
              cap.WakeUpCapabilities.MinLinkChangeWakeUp  = NdisDeviceStateUnspecified;
              MPOID_GetInformation(&pInfo, &ulInfoLen, &cap, sizeof(NDIS_PNP_CAPABILITIES));
           }

           break;
        }

        case OID_802_3_PERMANENT_ADDRESS:
        {
            MPOID_GetInformation(&pInfo, &ulInfoLen, pAdapter->PermanentAddress, ETH_LENGTH_OF_ADDRESS);
            break;
        }
        case OID_802_3_CURRENT_ADDRESS:
        {
            MPOID_GetInformation(&pInfo, &ulInfoLen, pAdapter->CurrentAddress, ETH_LENGTH_OF_ADDRESS);
            break;
        }
        case OID_802_3_MULTICAST_LIST:
        {
            MPOID_GetInformation(&pInfo, &ulInfoLen, pAdapter->MCList, (pAdapter->ulMCListSize * ETH_LENGTH_OF_ADDRESS));
            break;
        }
        case OID_802_3_MAXIMUM_LIST_SIZE:
        {
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, MAX_MCAST_LIST_LEN);
           break;
        }

        case OID_GEN_CURRENT_PACKET_FILTER:
        {
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, pAdapter->PacketFilter);
           break;
        }

        case OID_GEN_SUPPORTED_GUIDS:
        {
           MPOID_GetInformation(&pInfo, &ulInfoLen, QCMPSupportedOidGuids, QCMP_SupportedOidGuidsSize);
           break;
        }
        case OID_GEN_PHYSICAL_MEDIUM:
        {
           MPOID_GetInformation(&pInfo, &ulInfoLen, &myPhysicalMedium, sizeof(NDIS_PHYSICAL_MEDIUM));
           break;
        }
#ifdef NDIS620_MINIPORT

      case OID_WWAN_DRIVER_CAPS:
         {
            
           NdisZeroMemory(&driverCaps, sizeof(NDIS_WWAN_DRIVER_CAPS));
           driverCaps.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
           driverCaps.Header.Revision = NDIS_WWAN_DRIVER_CAPS_REVISION_1; 
           driverCaps.Header.Size = sizeof(NDIS_WWAN_DRIVER_CAPS);
           driverCaps.DriverCaps.ulMajorVersion = WWAN_MAJOR_VERSION;
           driverCaps.DriverCaps.ulMinorVersion = WWAN_MINOR_VERSION;
           MPOID_GetInformation(&pInfo, &ulInfoLen, &driverCaps, sizeof(NDIS_WWAN_DRIVER_CAPS));
           break;
         }
#ifdef INTERRUPT_MODERATION

      case OID_GEN_INTERRUPT_MODERATION:
         {
            NdisZeroMemory(&InterruptModeration, sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS));
            InterruptModeration.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            InterruptModeration.Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1; 
            InterruptModeration.Header.Size = sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS);
            InterruptModeration.InterruptModeration = NdisInterruptModerationNotSupported;
            MPOID_GetInformation(&pInfo, &ulInfoLen, &InterruptModeration, sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS));
            break;
         }      
#endif      
#endif /* supporting NDIS 6.20 */

        /* eventually this need to be read from the registry 
        ** - hard coded for now 
        */
        case OID_GEN_VENDOR_ID:
        {
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, VENDOR_ID);
           break;
        }
        case OID_PNP_QUERY_POWER:
        {
           NDIS_DEVICE_POWER_STATE pwrState;

           if (InformationBufferLength < sizeof(NDIS_DEVICE_POWER_STATE))
           {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_CONTROL,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> OID_PNP_QUERY_POWER: BUF SHORT %d\n", pAdapter->PortName, InformationBufferLength)
              );
              *BytesNeeded = sizeof(NDIS_DEVICE_POWER_STATE);
              *BytesWritten = 0;
              return NDIS_STATUS_BUFFER_TOO_SHORT;
           }

           if (InformationBuffer != NULL)
           {
              pwrState = *((PNDIS_DEVICE_POWER_STATE)InformationBuffer);

              switch (pwrState)
              {
                 case NdisDeviceStateUnspecified:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> OID_PNP_QUERY_POWER: UNSPECIFIED\n", pAdapter->PortName)
                    );
                    break;
                 case NdisDeviceStateD0:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> OID_PNP_QUERY_POWER: D0\n", pAdapter->PortName)
                    );
                    break;
                 case NdisDeviceStateD1:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> OID_PNP_QUERY_POWER: D1\n", pAdapter->PortName)
                    );
                    break;
                 case NdisDeviceStateD2:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> OID_PNP_QUERY_POWER: D2\n", pAdapter->PortName)
                    );
                    break;
                 case NdisDeviceStateD3:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> OID_PNP_QUERY_POWER: D3\n", pAdapter->PortName)
                    );
                    break;
                 default:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> OID_PNP_QUERY_POWER: UNKNOWN %u\n", pAdapter->PortName, pwrState)
                    );
                    break;
              }
              // early indication to stop TX data
              // DTR will be lowered with SET-D3
              if ((pAdapter->ulMediaConnectStatus != NdisMediaStateDisconnected) &&
                  (pAdapter->ToPowerDown == TRUE) &&  // system powering down
                  (pwrState > NdisDeviceStateD0))     // double check
              {
                 QCNET_DbgPrint
                 (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_ERROR,
                    ("<%s> ToPowerDown: ConnStat: DISCONNECT\n", pAdapter->PortName)
                 );

                 // Reset Timer Resolution here
                 //MPMAIN_MediaDisconnect(pAdapter, TRUE);
                 KeSetEvent(&pAdapter->MPThreadMediaDisconnectEvent, IO_NO_INCREMENT, FALSE);
                 
                 #ifdef MP_QCQOS_ENABLED
                 MPQOS_PurgeQueues(pAdapter);
                 #endif // MP_QCQOS_ENABLED
                 MPMAIN_ResetPacketCount(pAdapter);
                 //USBIF_CancelWriteThread(pAdapter->USBDo);
              }
              return NDIS_STATUS_SUCCESS;
           }
           else
           {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_CONTROL,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> OID_PNP_QUERY_POWER: NIL BUF\n", pAdapter->PortName)
              );
              *BytesNeeded = sizeof(NDIS_DEVICE_POWER_STATE);
              *BytesWritten = 0;
              return NDIS_STATUS_BUFFER_TOO_SHORT;
           }
        }  // OID_PNP_QUERY_POWER


       case OID_GEN_VENDOR_DESCRIPTION:
       {
          MPOID_GetInformation(&pInfo, &ulInfoLen, pAdapter->FriendlyName, strlen(pAdapter->FriendlyName));
          break;
       }

       case OID_GEN_PROTOCOL_OPTIONS:
       {
          MPOID_GetInformationUL
          (
             &ulInfo, &ulInfoLen,
             (NDIS_PROT_OPTION_ESTIMATED_LENGTH | NDIS_PROT_OPTION_NO_LOOPBACK)
          );
          break;
       }

       case OID_GEN_MEDIA_CONNECT_STATUS:
       {
          MPOID_GetInformationUL(&ulInfo, &ulInfoLen, pAdapter->ulMediaConnectStatus);
          QCNET_DbgPrint
          (
             MP_DBG_MASK_CONTROL,
             MP_DBG_LEVEL_DETAIL,
             ("<%s> OID_GEN_MEDIA_CONNECT_STATUS: %u\n", pAdapter->PortName, ulInfo)
          );
          break;
       }

       case OID_GEN_XMIT_OK:
       case OID_GEN_BROADCAST_FRAMES_XMIT:
       {
          ulInfo64 = pAdapter->GoodTransmits;
          if (InformationBufferLength >= sizeof(ULONG64) ||
              InformationBufferLength == 0)
          {
              MPOID_GetInformation(&pInfo, &ulInfoLen, &ulInfo64, sizeof(ULONG64));
          }
          else
          {
              MPOID_GetInformation(&pInfo, &ulInfoLen, &ulInfo64, sizeof(ULONG));
          }
          QCNET_DbgPrint
          (
             MP_DBG_MASK_CONTROL,
             MP_DBG_LEVEL_DETAIL,
             ("<%s> GoodTransmits: %I64u\n", pAdapter->PortName, pAdapter->GoodTransmits)
          );
          break;
       }

       case OID_GEN_RCV_OK:
       case OID_GEN_BROADCAST_FRAMES_RCV:
           ulInfo64 = pAdapter->GoodReceives;
           if (InformationBufferLength >= sizeof(ULONG64) ||
               InformationBufferLength == 0)
           {
              MPOID_GetInformation(&pInfo, &ulInfoLen, &ulInfo64, sizeof(ULONG64));
           }
           else
           {
              MPOID_GetInformation(&pInfo, &ulInfoLen, &ulInfo64, sizeof(ULONG));
           }
           QCNET_DbgPrint
           (
              MP_DBG_MASK_CONTROL,
              MP_DBG_LEVEL_DETAIL,
              ("<%s> GoodReceives: %I64u\n", pAdapter->PortName, pAdapter->GoodReceives)
           );
           break;

       case OID_GEN_XMIT_ERROR:
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, pAdapter->BadTransmits);
           QCNET_DbgPrint
           (
              MP_DBG_MASK_CONTROL,
              MP_DBG_LEVEL_DETAIL,
              ("<%s> XMIT_ERROR: %I64u\n", pAdapter->PortName, pAdapter->BadTransmits)
           );
           break;

       case OID_GEN_RCV_ERROR:
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, (pAdapter->BadReceives + pAdapter->DroppedReceives));
           QCNET_DbgPrint
           (
              MP_DBG_MASK_CONTROL,
              MP_DBG_LEVEL_DETAIL,
              ("<%s> RCV_ERROR: %I64u\n", pAdapter->PortName, pAdapter->DroppedReceives)
           );
           break;

       case OID_GEN_RCV_NO_BUFFER:
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, 0);
           break;

       // return 0 for now unless WHQL expects otherwise
       case OID_802_3_MAC_OPTIONS:
       case OID_802_3_RCV_ERROR_ALIGNMENT:
       case OID_802_3_XMIT_ONE_COLLISION:
       case OID_802_3_XMIT_MORE_COLLISIONS:
           MPOID_GetInformationUL(&ulInfo, &ulInfoLen, 0);
           break;

       // QoS
       case OID_GEN_VLAN_ID:
          MPOID_GetInformationUL(&ulInfo, &ulInfoLen, pAdapter->VlanId);
          break;

       #ifdef NDIS60_MINIPORT

       case OID_GEN_STATISTICS:
       {
          if (InformationBufferLength < NDIS_SIZEOF_STATISTICS_INFO_REVISION_1)
          {
             QCNET_DbgPrint
             (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> NDIS_STATISTICS_INFO: BUF SHORT %d\n", pAdapter->PortName, InformationBufferLength)
             );
             *BytesNeeded = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
             *BytesWritten = 0;
             return NDIS_STATUS_BUFFER_TOO_SHORT;
          }

          NdisZeroMemory(&StatisticsInfo, NDIS_SIZEOF_STATISTICS_INFO_REVISION_1);
          StatisticsInfo.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
          StatisticsInfo.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
          StatisticsInfo.Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
          StatisticsInfo.SupportedStatistics  = NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                                                NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
                                                NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                                                NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                                                NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR |
                                                NDIS_STATISTICS_FLAGS_VALID_XMIT_DISCARDS |
                                                
                                                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                                                NDIS_STATISTICS_FLAGS_VALID_MULTICAST_FRAMES_RCV |
                                                NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_RCV |
                                                
                                                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                                                NDIS_STATISTICS_FLAGS_VALID_MULTICAST_FRAMES_XMIT |
                                                NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_XMIT |

                                                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_BYTES_RCV |
                                                NDIS_STATISTICS_FLAGS_VALID_MULTICAST_BYTES_RCV |
                                                NDIS_STATISTICS_FLAGS_VALID_BROADCAST_BYTES_RCV |

                                                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_BYTES_XMIT |
                                                NDIS_STATISTICS_FLAGS_VALID_MULTICAST_BYTES_XMIT |
                                                NDIS_STATISTICS_FLAGS_VALID_BROADCAST_BYTES_XMIT;


          StatisticsInfo.ifHCInUcastPkts =  pAdapter->GoodReceives;
          StatisticsInfo.ifHCInMulticastPkts =  pAdapter->GoodReceives;
          StatisticsInfo.ifHCInBroadcastPkts =  pAdapter->GoodReceives;

          StatisticsInfo.ifHCInOctets  = pAdapter->RxBytesGood;
          StatisticsInfo.ifInDiscards  = pAdapter->BadReceives + pAdapter->DroppedReceives;
          StatisticsInfo.ifInErrors    = pAdapter->BadReceives;

          StatisticsInfo.ifHCOutUcastPkts = pAdapter->GoodTransmits;
          StatisticsInfo.ifHCOutMulticastPkts = pAdapter->GoodTransmits;
          StatisticsInfo.ifHCOutBroadcastPkts = pAdapter->GoodTransmits;

          StatisticsInfo.ifHCOutOctets = pAdapter->TxBytesGood;
          StatisticsInfo.ifOutErrors   = pAdapter->TxFramesFailed;
          StatisticsInfo.ifOutDiscards = pAdapter->TxFramesDropped;

          StatisticsInfo.ifHCInUcastOctets = pAdapter->RxBytesGood;
          StatisticsInfo.ifHCInMulticastOctets = pAdapter->RxBytesGood;
          StatisticsInfo.ifHCInBroadcastOctets = pAdapter->RxBytesGood;

          StatisticsInfo.ifHCOutUcastOctets = pAdapter->TxBytesGood; 
          StatisticsInfo.ifHCOutMulticastOctets = pAdapter->TxBytesGood; 
          StatisticsInfo.ifHCOutBroadcastOctets = pAdapter->TxBytesGood; 

          MPOID_GetInformation(&pInfo, &ulInfoLen, &StatisticsInfo, NDIS_SIZEOF_STATISTICS_INFO_REVISION_1);

          QCNET_DbgPrint
          (
             MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_TRACE,
             ("<%s> OID_GEN_STATISTICS: Rg %I64uB Re %I64uF Rd %I64uF Tg %I64uB Te %I64uF Td %I64uF\n",
               pAdapter->PortName,
               StatisticsInfo.ifHCInOctets, StatisticsInfo.ifInErrors, StatisticsInfo.ifInDiscards,
               StatisticsInfo.ifHCOutOctets, StatisticsInfo.ifOutErrors, StatisticsInfo.ifOutDiscards)
          );

          break;
       }

       #endif // NDIS60_MINIPORT
       
       case OID_GOBI_DEVICE_INFO:
       {
          ulInfoLen = pAdapter->DeviceStaticInfoLen;
          pInfo = &(pAdapter->DeviceStaticInfo);
          break;
       }
       
/*
 *************************************************************
 *************************************************************
*/   

        default:
        
#ifdef NDIS51_MINIPORT

            if (Oid != OID_GEN_LINK_SPEED)
            {
               Status = NDIS_STATUS_NOT_SUPPORTED;
               break;
            }
#endif
            if (Oid == OID_GEN_LINK_SPEED)
            {
               if ((pAdapter->EventReportMask & QWDS_EVENT_REPORT_MASK_RATES) &&
                   ((pAdapter->ulServingSystemRxRate != MP_INVALID_LINK_SPEED) &&
                    (pAdapter->ulServingSystemTxRate != MP_INVALID_LINK_SPEED))) 
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL,
                     MP_DBG_LEVEL_DETAIL,
                     ("<%s> OID_GEN_LINK_SPEED: cached max rx %u tx %u\n", pAdapter->PortName, 
                      pAdapter->ulServingSystemRxRate, pAdapter->ulServingSystemTxRate)
                  );
                  if (pAdapter->ulServingSystemRxRate > pAdapter->ulServingSystemTxRate)
                  {
                     ulInfo = pAdapter->ulServingSystemRxRate / 100;
                  }
                  else
                  {
                     ulInfo = pAdapter->ulServingSystemTxRate / 100;
                  }
                  break;
               }
               else if ((pAdapter->EventReportMask & QWDS_EVENT_REPORT_MASK_RATES) == 0)
               {
                  if (pAdapter->ulMediaConnectStatus == NdisMediaStateConnected)
                  {
#ifndef NDIS620_MINIPORT                  
                     MPQWDS_SendEventReportReq(pAdapter, NULL, TRUE);
#endif
                  }
                  else
                  {
                     ulInfo = 0;
                  }
               }
#ifdef QCMP_DISABLE_QMI
               if (pAdapter->DisableQMI == 1)
               {
                  ulInfo = 150*10000;
                  break;
               }
#endif
            }

            if( (pAdapter->Flags & fMP_ANY_FLAGS) == 0 )
            {
                // If there is a free OID to do the work (there should always be one
                // if we ever fail this there's something wrong (OIDs not getting freed
                // for some reason) or we need to bump the number we start with up some
                NdisAcquireSpinLock( &pAdapter->OIDLock );
                if( !IsListEmpty( &pAdapter->OIDFreeList ) )
                {
                    // Get an "OID Blank" off the free list
                    pList = RemoveHeadList( &pAdapter->OIDFreeList );
                    InterlockedIncrement(&(pAdapter->nBusyOID));

                    // Put the OID we are about to use on the pending list
                    // InsertTailList( &pAdapter->OIDPendingList, pList );

                    // Don't allow a 0 transaction ID, that's the flag for a notification
                    if( ++(pAdapter->NextTxID) == 0 ) ++(pAdapter->NextTxID);
                    NdisReleaseSpinLock( &pAdapter->OIDLock );

                    // Point to the OID structure proper
                    pOID = CONTAINING_RECORD(pList, MP_OID_WRITE, List);


                    // Save away the address in the request so we can deposit the data there later
                    pOID->RequestInfoBuffer   = InformationBuffer;
                    pOID->RequestBytesUsed    = BytesWritten;
                    pOID->RequestBytesNeeded  = BytesNeeded;
                    pOID->RequestBufferLength = InformationBufferLength;
                    pOID->Oid                 = Oid;
                    pOID->OidType             = fMP_QUERY_OID;
                    pOID->pOIDResp            = NULL;
                    #ifdef NDIS60_MINIPORT
                    MPOID_CreateOidCopy(pAdapter, oidRequest, pOID);
                    #endif // NDIS60_MINIPORT

                    // keep things working, to clean up later -- TODO
                    pOID->OID.TransactionID = pAdapter->NextTxID;
                    pOID->OID.Oid = Oid;
                    pOID->OID.OidType = fMP_QUERY_OID;
                    pOID->OID.BufferLength = InformationBufferLength;

                    if (!IsListEmpty(&pOID->QMIQueue))
                    {
                       QCNET_DbgPrint
                       (
                          MP_DBG_MASK_CONTROL,
                          MP_DBG_LEVEL_CRITICAL,
                          ("<%s> QMIQueue not empty! 0x%p\n", pAdapter->PortName, pOID)
                       );
                    }

                    // Setup the OID
                    #ifndef QCQMI_SUPPORT

                    pOID->OID.TransactionID = pAdapter->NextTxID;
                    pOID->OID.Oid = Oid;
                    pOID->OID.OidType = fMP_QUERY_OID;
                    pOID->OID.BufferLength = InformationBufferLength;

                    // If data is passed in via InfoBuffer, copy it to request
                    if( ulInfoLen > 0 )
                    {
                        if ( ulInfoLen <= pAdapter->CtrlSendSize )
                        {
                           NdisMoveMemory( pOID->OID.Buffer,
                                           InformationBuffer,
                                           ulInfoLen );
                           QCNET_DbgPrint
                           (
                              MP_DBG_MASK_CONTROL,
                              MP_DBG_LEVEL_DETAIL,
                              ("<%s> QueryOID(0x%x) passed to QC USB with %d bytes of data\n",
                                pAdapter->PortName, Oid, ulInfoLen)
                           );

                           // Call IOCallDriver to pass this down (via an IRP) to QC USB
                           Status = MP_USBRequest( pAdapter, &pOID->OID, ulInfoLen, pList );
                        }
                        else
                        {
                           ulInfoLen = 0;
                           Status = NDIS_STATUS_INVALID_DATA;
                        }
                    }
                    else
                    {
                       QCNET_DbgPrint
                       (
                          MP_DBG_MASK_CONTROL,
                          MP_DBG_LEVEL_DETAIL,
                          ("<%s> QueryOID(0x%x) passed to QC USB with %d bytes of data\n",
                            pAdapter->PortName, Oid, ulInfoLen)
                       );

                       // Call IOCallDriver to pass this down (via an IRP) to QC USB
                       Status = MP_USBRequest( pAdapter, &pOID->OID, ulInfoLen, pList );
                    }

                    #else  // QCQMI_SUPPORT

                    ulInfoLen = MPQMI_OIDtoQMI(pAdapter, Oid, pOID);
                    OidStatus = pOID->OIDAsyncType;
                    if ((ulInfoLen != 0) 
#ifdef NDIS51_MINIPORT                     
                        &&
                        (pAdapter->ulMediaConnectStatus == NdisMediaStateConnected))
#else
                        )
#endif                        
                    {
                       int i;
                       PLIST_ENTRY   listHead, thisEntry;
                       PQCQMIQUEUE currentQMIQueueEle = NULL;

                       // Aquire the OID LOCK sending all the qmi messages 
                       NdisAcquireSpinLock( &pAdapter->OIDLock );

                       if (!IsListEmpty(&pOID->QMIQueue))
                       {
                          // Start at the head of the list
                          listHead = &pOID->QMIQueue;
                          thisEntry = listHead->Flink;
                          while (thisEntry != listHead)
                          {
                             currentQMIQueueEle = CONTAINING_RECORD(thisEntry, QCQMIQUEUE, QCQMIRequest);
                             Status = MP_USBRequest(pAdapter, &currentQMIQueueEle->QMI, currentQMIQueueEle->QMILength, pList);
                             if (Status != NDIS_STATUS_PENDING)
                             {
                                QCNET_DbgPrint
                                (
                                   MP_DBG_MASK_CONTROL,
                                   MP_DBG_LEVEL_DETAIL,
                                   ("<%s> Query: OID back to free-0\n", pAdapter->PortName)
                                );
                                MPQMI_FreeQMIRequestQueue(pOID);
                                break;
                             }
                             thisEntry = thisEntry->Flink;
                          }
                       }
                       else
                       {
                          Status = NDIS_STATUS_RESOURCES;
                       }
                       if (Status == NDIS_STATUS_PENDING)
                       {
                          //InsertTailList( &pAdapter->OIDPendingList, &pOID->List );

                          #ifdef NDIS60_MINIPORT
                          QCNET_DbgPrint
                          (
                             MP_DBG_MASK_CONTROL,
                             MP_DBG_LEVEL_ERROR,
                             ("<%s> OID (Query Enqueue 0x%p)\n", pAdapter->PortName, qcmpReq->OidRequest)
                          );
                          
                          if (OidStatus != NDIS_STATUS_INDICATION_REQUIRED)
                          {
                             InsertTailList(&pAdapter->OidRequestList, &(qcmpReq->List));
                             InterlockedIncrement(&pAdapter->nPendingOidReq);
                          }
                          else
                          {
                             ExFreePool( qcmpReq );
                          }
                          #endif // NDIS60_MINIPORT
                       }
                       else
                       {
                          MPOID_CleanupOidCopy(pAdapter, pOID);
                          InsertTailList(&pAdapter->OIDFreeList, &pOID->List);
                       }
                       NdisReleaseSpinLock(&pAdapter->OIDLock);
                    }
                    else
                    {
                       // return 0 for link speed instead of failure
                       if (Oid == OID_GEN_LINK_SPEED)
                       {
                          QCNET_DbgPrint
                          (
                             MP_DBG_MASK_CONTROL,
                             MP_DBG_LEVEL_ERROR,
                             ("<%s> QMI failure (%uB) or no connect, OID_GEN_LINK_SPEED=0\n",
                               pAdapter->PortName, ulInfoLen)
                          );
                          Status = NDIS_STATUS_SUCCESS;
                          ulInfo = 0;
                          ulInfoLen = sizeof(ulInfo);
                          pInfo = (PVOID) &ulInfo;
                       }
                       else
                       {
                          Status = NDIS_STATUS_NOT_SUPPORTED;
                       }

                       NdisAcquireSpinLock(&pAdapter->OIDLock);
                       QCNET_DbgPrint
                       (
                          MP_DBG_MASK_CONTROL,
                          MP_DBG_LEVEL_DETAIL,
                          ("<%s> Query: OID back to free-1\n", pAdapter->PortName)
                       );
                       MPQMI_FreeQMIRequestQueue(pOID);
                       MPOID_CleanupOidCopy(pAdapter, pOID);
                       InsertTailList(&pAdapter->OIDFreeList, &pOID->List);
                       NdisReleaseSpinLock(&pAdapter->OIDLock);
                    }

                    #endif // QCQMI_SUPPORT

                    if (Status != NDIS_STATUS_PENDING)
                    {
                       NdisAcquireSpinLock( &pAdapter->OIDLock );
                       InterlockedDecrement(&(pAdapter->nBusyOID));
                       NdisReleaseSpinLock( &pAdapter->OIDLock );
                    }
                }
                else
                {
                    NdisReleaseSpinLock( &pAdapter->OIDLock );

                    // This is a problem we should never see
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_ERROR,
                       ("<%s> Query: MPQuery could not find a write OID\n", pAdapter->PortName)
                    );
                    Status = NDIS_STATUS_RESOURCES;
                }

            }
            else
            {
                Status = NDIS_STATUS_NOT_ACCEPTED;
            }
            break;
    }

    if( Status == NDIS_STATUS_SUCCESS )
    {
      // If the OID is one of the ones we need to forward to QC USB
      // This is just a normal Query handled by the miniport
      if( ulInfoLen <= InformationBufferLength )
      {
        // Copy result into InformationBuffer
        *BytesWritten = ulInfoLen;
        if( ulInfoLen )
        {
          NdisMoveMemory(InformationBuffer, pInfo, ulInfoLen);
        }
        else
        {
           *BytesNeeded = 0;
        }
      }
      else
      {
        // too short
        *BytesNeeded = ulInfoLen;
        Status = NDIS_STATUS_BUFFER_TOO_SHORT;
      }
    }

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> <--- MPOID_QueryInformation Status = 0x%x\n", pAdapter->PortName, Status)
    );

    if( Status == NDIS_STATUS_PENDING && OidStatus == NDIS_STATUS_INDICATION_REQUIRED)
    {
       return OidStatus;
    }
    return(Status);
}  // MPOID_QueryInformation


/*** MiniportSetInformation ***/

NDIS_STATUS MPOID_SetInformation
(
   NDIS_HANDLE MiniportAdapterContext,
   NDIS_OID    Oid,
   PVOID       InformationBuffer,
   ULONG       InformationBufferLength,
   PULONG      BytesRead,
   PULONG      BytesNeeded
)
{
    PMP_ADAPTER   pAdapter = (PMP_ADAPTER) MiniportAdapterContext;
    PLIST_ENTRY   pList;
    PMP_OID_WRITE pOID;
    PUSHORT       pTemp;
    ULONG         ulInfoLen = 0;
    NDIS_STATUS   OidStatus = NDIS_STATUS_SUCCESS, Status = NDIS_STATUS_SUCCESS;

    #ifdef NDIS60_MINIPORT
    PQCMP_OID_REQUEST      qcmpReq;
    PNDIS_OID_REQUEST      oidRequest = NULL;
    qcmpReq    = (PQCMP_OID_REQUEST)InformationBuffer;
    oidRequest = qcmpReq->OidRequest;
    InformationBuffer = oidRequest->DATA.SET_INFORMATION.InformationBuffer;
    #endif // NDIS60_MINIPORT

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL | MP_DBG_MASK_OID_QMI,
       MP_DBG_LEVEL_TRACE,
       ("<%s> ---> MPOID_SetInformation %u/%u <S%u, R%u> SetOID: %s (0x%x)\n", pAdapter->PortName,
         pAdapter->nBusyOID, pAdapter->nBusyTx,
         pAdapter->ulMaxBusySends, pAdapter->ulMaxBusyRecvs, MPOID_OidToNameStr(Oid), Oid )
    );

    switch (Oid)
    {
        
        case OID_GEN_TRANSPORT_HEADER_OFFSET:
            if( InformationBufferLength != (2 * sizeof(USHORT)) )
                {
                *BytesNeeded = (2 * sizeof(USHORT));
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
                }

            *BytesRead = InformationBufferLength;
            NdisMoveMemory( &pAdapter->ProtocolType, InformationBuffer, sizeof(USHORT) );
            NdisMoveMemory( &pAdapter->HeaderOffset, ((PUCHAR)InformationBuffer + sizeof(USHORT)), sizeof(USHORT) );
            Status = NDIS_STATUS_SUCCESS;
            break;

        case OID_GEN_MACHINE_NAME:
            Status = NDIS_STATUS_INVALID_OID;
            break;

        case OID_802_3_MULTICAST_LIST:
            Status = MPOID_SetMulticastList
                     (
                        pAdapter,
                        InformationBuffer,
                        InformationBufferLength,
                        BytesRead,
                        BytesNeeded
                     );

            break;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if( InformationBufferLength != sizeof(ULONG) )
                {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
                }
            pAdapter->ulLookAhead = *(PULONG)InformationBuffer;                

            *BytesRead = sizeof(ULONG);
            Status = NDIS_STATUS_SUCCESS;
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            if( InformationBufferLength != sizeof(ULONG) )
                {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
                }

            *BytesRead = InformationBufferLength;

            Status = MPOID_SetPacketFilter(pAdapter, *((PULONG)InformationBuffer));

            break;

        case OID_PNP_SET_POWER:
        {
           NDIS_DEVICE_POWER_STATE pwrState;

           if (InformationBufferLength < sizeof(NDIS_DEVICE_POWER_STATE))
           {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_CONTROL,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> MPOID_SetInformation: OID_PNP_SET_POWER: BUF SHORT %d\n", pAdapter->PortName, InformationBufferLength)
              );
           }
           else if (InformationBuffer != NULL)
           {
              pwrState = *((PNDIS_DEVICE_POWER_STATE)InformationBuffer);

              switch (pwrState)
              {
                 case NdisDeviceStateUnspecified:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> MPOID_SetInformation: OID_PNP_SET_POWER: UNSPECIFIED\n", pAdapter->PortName)
                    );
                    break;
                 case NdisDeviceStateD0:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> MPOID_SetInformation: OID_PNP_SET_POWER: D0\n", pAdapter->PortName)
                    );
                    break;
                 case NdisDeviceStateD1:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> MPOID_SetInformation: OID_PNP_SET_POWER: D1\n", pAdapter->PortName)
                    );
                    break;
                 case NdisDeviceStateD2:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> MPOID_SetInformation: OID_PNP_SET_POWER: D2\n", pAdapter->PortName)
                    );
                    break;
                 case NdisDeviceStateD3:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> MPOID_SetInformation: OID_PNP_SET_POWER: D3\n", pAdapter->PortName)
                    );
                    break;
                 default:
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_ERROR,
                       ("<%s> MPOID_SetInformation: OID_PNP_SET_POWER: UNKNOWN\n", pAdapter->PortName)
                    );
                    break;
              }

              USBIF_SetPowerState(pAdapter->USBDo, pwrState);
           }
           else
           {
              QCNET_DbgPrint
              (
                 MP_DBG_MASK_CONTROL,
                 MP_DBG_LEVEL_ERROR,
                 ("<%s> MPOID_SetInformation: OID_PNP_SET_POWER: NIL BUF\n", pAdapter->PortName)
              );
           }

           return NDIS_STATUS_SUCCESS;
        }  // OID_PNP_SET_POWER

        case OID_GEN_PROTOCOL_OPTIONS:
           // ignore this
           break;

            // The missing break here is intentional, we want to set our
            // packet filter flag and then let the MSM know how the flag is set also

        case OID_GEN_VLAN_ID:
            if( InformationBufferLength != sizeof(NDIS_VLAN_ID) )
            {
                *BytesNeeded = sizeof(NDIS_VLAN_ID);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }
            pAdapter->VlanId = *(PULONG)InformationBuffer;

            *BytesRead = sizeof(NDIS_VLAN_ID);
            Status = NDIS_STATUS_SUCCESS;
           break;

         case OID_GEN_LINK_PARAMETERS:
            if( InformationBufferLength != sizeof(NDIS_LINK_PARAMETERS) )
            {
                *BytesNeeded = sizeof(NDIS_LINK_PARAMETERS);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }
            *BytesRead = sizeof(NDIS_LINK_PARAMETERS);
            Status = NDIS_STATUS_SUCCESS;
           break;
           
#ifdef NDIS60_MINIPORT
#ifdef INTERRUPT_MODERATION           
         case OID_GEN_INTERRUPT_MODERATION:
         {
            Status = NDIS_STATUS_INVALID_DATA;
            break;
         }      
#endif                 
#endif

        default:

#ifdef NDIS51_MINIPORT        
           // nothing sent to the device in this version
           // the following code segment is not executed
           Status = NDIS_STATUS_NOT_SUPPORTED;
           break;
#endif
            if( (pAdapter->Flags & fMP_ANY_FLAGS) == 0 )
            {
                NdisAcquireSpinLock( &pAdapter->OIDLock );
                if( !IsListEmpty( &pAdapter->OIDFreeList ) )
                {
                    // Get an "OID Blank" off the free list
                    pList = RemoveHeadList( &pAdapter->OIDFreeList );
                    InterlockedIncrement(&(pAdapter->nBusyOID));

                    // Put the OID we are about to use on the pending list
                    // InsertTailList( &pAdapter->OIDPendingList, pList );

                    if( ++(pAdapter->NextTxID) == 0 ) ++(pAdapter->NextTxID);
                    NdisReleaseSpinLock( &pAdapter->OIDLock );

                    // Point to the OID structure proper
                    pOID = CONTAINING_RECORD(pList, MP_OID_WRITE, List);

                    pOID->RequestInfoBuffer = NULL;
                    pOID->RequestBytesUsed = BytesRead;
                    pOID->RequestBytesNeeded = BytesNeeded;
                    pOID->RequestBufferLength = InformationBufferLength;
                    pOID->Oid = Oid;
                    pOID->OidType = fMP_SET_OID;
                    pOID->pOIDResp = NULL;

                    #ifdef NDIS60_MINIPORT
                    MPOID_CreateOidCopy(pAdapter, oidRequest, pOID);
                    #endif // NDIS60_MINIPORT

                    // keep things working, to clean up later -- TODO
                    pOID->OID.TransactionID = pAdapter->NextTxID;
                    pOID->OID.Oid = Oid;
                    pOID->OID.OidType = fMP_SET_OID;
                    pOID->OID.BufferLength = InformationBufferLength;

                    if (!IsListEmpty(&pOID->QMIQueue))
                    {
                       QCNET_DbgPrint
                       (
                          MP_DBG_MASK_CONTROL,
                          MP_DBG_LEVEL_ERROR,
                          ("<%s> The QMIQueue is no empty - error!\n", pAdapter->PortName)
                       );
                    }

                    #ifndef QCQMI_SUPPORT

                    // Setup the OID
                    pOID->OID.TransactionID = pAdapter->NextTxID;
                    pOID->OID.Oid = Oid;
                    pOID->OID.OidType = fMP_SET_OID;
                    pOID->OID.BufferLength = InformationBufferLength;
                    NdisMoveMemory( pOID->OID.Buffer, InformationBuffer, InformationBufferLength );

                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_DETAIL,
                       ("<%s> MPOID_SetInformation: SetOID(0x%p) passed to QC USB\n", pAdapter->PortName, Oid)
                    );

                    // Call IOCallDriver to pass this down (via an IRP) to QC USB
                    Status = MP_USBRequest( pAdapter, &pOID->OID, InformationBufferLength, pList );

                    #else  // QCQMI_SUPPORT

                    ulInfoLen = MPQMI_OIDtoQMI(pAdapter, Oid, pOID);
                    OidStatus = pOID->OIDAsyncType;
                    if (ulInfoLen != 0) 
                        //  &&
                        //(pAdapter->ulMediaConnectStatus == NdisMediaStateConnected))
                        //TODO: How to make it a valod condiftion
                    {
                       int i;
                       PLIST_ENTRY   listHead, thisEntry;
                       PQCQMIQUEUE currentQMIQueueEle = NULL;

                       // Aquire the OID LOCK sending all the qmi messages 
                       NdisAcquireSpinLock( &pAdapter->OIDLock );

                       if (!IsListEmpty(&pOID->QMIQueue))
                       {
                          // Start at the head of the list
                          listHead = &pOID->QMIQueue;
                          thisEntry = listHead->Flink;
                          while (thisEntry != listHead)
                          {
                             currentQMIQueueEle = CONTAINING_RECORD(thisEntry, QCQMIQUEUE, QCQMIRequest);
                             Status = MP_USBRequest(pAdapter, &currentQMIQueueEle->QMI, currentQMIQueueEle->QMILength, pList);
                             if (Status != NDIS_STATUS_PENDING)
                             {
                                QCNET_DbgPrint
                                (
                                   MP_DBG_MASK_CONTROL,
                                   MP_DBG_LEVEL_DETAIL,
                                   ("<%s> Set: OID back to free-0\n", pAdapter->PortName)
                                );
                                MPQMI_FreeQMIRequestQueue(pOID);
                                break;
                             }
                             thisEntry = thisEntry->Flink;
                          }
                       }
                       else
                       {
                          Status = NDIS_STATUS_RESOURCES;
                       }
                       if (Status == NDIS_STATUS_PENDING)
                       {
                          //InsertTailList( &pAdapter->OIDPendingList, &pOID->List );

                          #ifdef NDIS60_MINIPORT

                          QCNET_DbgPrint
                          (
                             MP_DBG_MASK_CONTROL,
                             MP_DBG_LEVEL_ERROR,
                             ("<%s> OID (Set Enqueue 0x%p) \n", pAdapter->PortName, qcmpReq->OidRequest)
                          );

                          if (OidStatus != NDIS_STATUS_INDICATION_REQUIRED)
                          {
                             InsertTailList(&pAdapter->OidRequestList, &(qcmpReq->List));
                             InterlockedIncrement(&pAdapter->nPendingOidReq);
                          }
                          else
                          {
                             ExFreePool( qcmpReq );
                          }
                          #endif // NDIS60_MINIPORT
                       }
                       else
                       {
                          MPOID_CleanupOidCopy(pAdapter, pOID);
                          InsertTailList(&pAdapter->OIDFreeList, &pOID->List);
                       }
                       NdisReleaseSpinLock(&pAdapter->OIDLock);
                    }
                    else
                    {
                       Status = NDIS_STATUS_NOT_SUPPORTED;

                       NdisAcquireSpinLock(&pAdapter->OIDLock);
                       QCNET_DbgPrint
                       (
                          MP_DBG_MASK_CONTROL,
                          MP_DBG_LEVEL_DETAIL,
                          ("<%s> Set: OID back to free-1\n", pAdapter->PortName)
                       );
                       MPQMI_FreeQMIRequestQueue(pOID);
                       MPOID_CleanupOidCopy(pAdapter, pOID);
                       InsertTailList(&pAdapter->OIDFreeList, &pOID->List);
                       NdisReleaseSpinLock(&pAdapter->OIDLock);
                    }

                    #endif // QCQMI_SUPPORT
                    if (Status != NDIS_STATUS_PENDING)
                    {
                       NdisAcquireSpinLock( &pAdapter->OIDLock );
                       InterlockedDecrement(&(pAdapter->nBusyOID));
                       NdisReleaseSpinLock( &pAdapter->OIDLock );
                    }
                }
                else
                {
                    QCNET_DbgPrint
                    (
                       MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_ERROR,
                       ("<%s> MPOID_SetInformation: MPSet could not find a write OID for Set 0x%p 0x%p\n",
                         pAdapter->PortName, &pAdapter->OIDFreeList, pAdapter->OIDFreeList.Flink)
                    );
                    NdisReleaseSpinLock( &pAdapter->OIDLock );

                    // This is a problem we should never see
                    Status = NDIS_STATUS_RESOURCES;
                }

            }
            else
            {
                Status = NDIS_STATUS_NOT_ACCEPTED;
            }
            break;
    }

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> <--- MPOID_SetInformation: Status = 0x%x\n", pAdapter->PortName, Status)
    );

    if( Status == NDIS_STATUS_PENDING && OidStatus == NDIS_STATUS_INDICATION_REQUIRED)
    {
       return OidStatus;
    }
    return(Status);
}  // MPOID_SetInformation

NDIS_STATUS MPOID_SetPacketFilter(PMP_ADAPTER pAdapter, ULONG PacketFilter)
{
    NDIS_STATUS      Status = NDIS_STATUS_SUCCESS;

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> ---> MPOID_SetPacketFilter\n", pAdapter->PortName)
    );

    // any bits not supported?
    if( PacketFilter & ~SUPPORTED_PACKET_FILTERS )
        {
        return(NDIS_STATUS_NOT_SUPPORTED);
        }

    // any filtering changes?
    if( PacketFilter != pAdapter->PacketFilter )
    {
        //
        // Change the filtering modes on hardware
        // TODO 


        // Save the new packet filter value                                                               
        pAdapter->PacketFilter = PacketFilter;
    }

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> <--- MPOID_SetPacketFilter\n", pAdapter->PortName)
    );

    return(Status);
}  // MPOID_SetPacketFilter

NDIS_STATUS MPOID_SetMulticastList
(
   PMP_ADAPTER pAdapter,
   PVOID       InformationBuffer,
   ULONG       InformationBufferLength,
   PULONG      pBytesRead,
   PULONG      pBytesNeeded
)
{
    NDIS_STATUS            Status = NDIS_STATUS_SUCCESS;
    ULONG                  index;

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> ---> MPOID_SetMulticastList\n", pAdapter->PortName)
    );

    //
    // Initialize.
    //
    *pBytesNeeded = ETH_LENGTH_OF_ADDRESS;
    *pBytesRead = InformationBufferLength;

    do
        {
        if( InformationBufferLength % ETH_LENGTH_OF_ADDRESS )
            {
            Status = NDIS_STATUS_INVALID_LENGTH;
            break;
            }

        if( InformationBufferLength > (MAX_MCAST_LIST_LEN * ETH_LENGTH_OF_ADDRESS) )
            {
            Status = NDIS_STATUS_MULTICAST_FULL;
            *pBytesNeeded = MAX_MCAST_LIST_LEN * ETH_LENGTH_OF_ADDRESS;
            break;
            }

        //
        // Protect the list update with a lock if it can be updated by
        // another thread simultaneously.
        //

        NdisZeroMemory(pAdapter->MCList,
                       MAX_MCAST_LIST_LEN * ETH_LENGTH_OF_ADDRESS);

        NdisMoveMemory(pAdapter->MCList,
                       InformationBuffer,
                       InformationBufferLength);

        pAdapter->ulMCListSize =    InformationBufferLength / ETH_LENGTH_OF_ADDRESS;

#if DBG
        // display the multicast list
        for( index = 0; index < pAdapter->ulMCListSize; index++ )
        {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MC(%d) = %02x-%02x-%02x-%02x-%02x-%02x\n", pAdapter->PortName,
                                         index,
                                         pAdapter->MCList[index][0],
                                         pAdapter->MCList[index][1],
                                         pAdapter->MCList[index][2],
                                         pAdapter->MCList[index][3],
                                         pAdapter->MCList[index][4],
                                         pAdapter->MCList[index][5])
            );
        }
#endif        
        }
    while( FALSE );    

    //
    // Program the hardware to add suport for these muticast addresses
    // 

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> <--- MPOID_SetMulticastList\n", pAdapter->PortName)
    );

    return(Status);

}  // MPOID_SetMulticastList

