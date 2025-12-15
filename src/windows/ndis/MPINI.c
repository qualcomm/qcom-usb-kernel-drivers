/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P I N I . C

GENERAL DESCRIPTION
    This module contains initialization helper routines called during
    MiniportInitialize.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdio.h>
#include "MPMain.h"
#include "MPIOC.h"
#include "MPINI.h"
#include "MPQCTL.h"
#include "MPWork.h"
#include "USBMAIN.h"
#ifdef MP_QCQOS_ENABLED
#include "MPQOS.h"
#endif

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPINI.tmh"

#endif   // EVENT_TRACING

#pragma NDIS_PAGEABLE_FUNCTION(MPINI_AllocAdapter)
#pragma NDIS_PAGEABLE_FUNCTION(MPINI_InitializeAdapter)

NDIS_STATUS MPINI_MiniportInitialize
(
   PNDIS_STATUS OpenErrorStatus,
   PUINT SelectedMediumIndex,
   PNDIS_MEDIUM MediumArray,
   UINT MediumArraySize,
   NDIS_HANDLE MiniportAdapterHandle,
   NDIS_HANDLE WrapperConfigurationContext
)
{
   NDIS_STATUS          Status = NDIS_STATUS_SUCCESS;
   PMP_ADAPTER          pAdapter=NULL;
   NDIS_HANDLE          ConfigurationHandle;
   UINT                 index;
   INT                  temp;

   QCNET_DbgPrintG(("<%s> ---> MPINI_MiniportInitialize\n", gDeviceName));

   do
   {
      //
      // Check to see if our media type exists in an array of supported 
      // media types provided by NDIS.
      //
      for ( index = 0; index < MediumArraySize; ++index )
         {
         if ( MediumArray[index] == QCMP_MEDIA_TYPE )
            {
            break;
            }
         }

      if ( index == MediumArraySize )
         {
         QCNET_DbgPrintG( ("<%s> Expected media is not in MediumArray.\n", gDeviceName) );
         Status = NDIS_STATUS_UNSUPPORTED_MEDIA;
         break;
         }

      //
      // Set the index value as the selected medium for our device.
      //
      *SelectedMediumIndex = index;

      //
      // Allocate adapter context structure and initialize all the 
      // memory resources for sending and receiving packets.
      //
      Status =  MPINI_AllocAdapter(&pAdapter);
      if (Status != NDIS_STATUS_SUCCESS)
      {
         break;
      }

      NdisInterlockedIncrement( &pAdapter->RefCount );  
      pAdapter->InstanceIdx = NdisInterlockedIncrement(&GlobalData.InstanceIndex);
      sprintf(pAdapter->PortName, "qnet%04d", pAdapter->InstanceIdx);

#ifdef NDIS_WDM
      KeInitializeEvent(&pAdapter->USBPnpCompletionEvent, NotificationEvent,FALSE);
#endif
      NdisMGetDeviceProperty
      (
         MiniportAdapterHandle,
         &pAdapter->Pdo,
         &pAdapter->Fdo,
         &pAdapter->NextDeviceObject,
         NULL,
         NULL
      );
      pAdapter->AdapterHandle = MiniportAdapterHandle;

      // QCNET_DbgPrintG( ("<%s> MP: pdo 0x%p fdo 0x%p ndo 0x%p\n", gDeviceName,
      //             pAdapter->Pdo, pAdapter->Fdo, pAdapter->NextDeviceObject) );

      // default MAC addresses or custom MAC addresses from registry
      Status = MPINI_ReadNetworkAddress(pAdapter, WrapperConfigurationContext);
      if ( Status != NDIS_STATUS_SUCCESS )
      {
         break;
      }

      // Read Advanced configuration information from the registry.
      Status = MPParam_GetConfigValues
               (
                  MiniportAdapterHandle, 
                  WrapperConfigurationContext,
                  pAdapter
               );
      if ( Status != NDIS_STATUS_SUCCESS )
      {
         break;
      }

#ifndef EVENT_TRACING
      pAdapter->DebugLevel = (pAdapter->DebugMask & MP_DBG_LEVEL_MASK);
      pAdapter->DebugMask  = (pAdapter->DebugMask & MP_DBG_MASK_MASK);
#endif

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPMaxDataSends: %u\n", pAdapter->PortName, pAdapter->MaxDataSends)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPMaxDataReceives: %u\n", pAdapter->PortName, pAdapter->MaxDataReceives)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPMaxCtrlSends: %u\n", pAdapter->PortName, pAdapter->MaxCtrlSends)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPCtrlSendSize: %u\n", pAdapter->PortName, pAdapter->CtrlSendSize)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPMaxCtrlReceives: %u\n", pAdapter->PortName, pAdapter->MaxCtrlReceives)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPCtrlReceiveSize: %u\n", pAdapter->PortName, pAdapter->CtrlReceiveSize)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPNumClients: %u\n", pAdapter->PortName, pAdapter->NumClients)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPReconfigDelay: %u\n", pAdapter->PortName, pAdapter->ReconfigDelay)
      );
      #ifdef QCMP_TEST_MODE
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPTestConnectDelay: %u\n", pAdapter->PortName, pAdapter->TestConnectDelay)
      );
      #endif // QCMP_TEST_MODE

      #ifdef QCMP_UL_TLP
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> UplinkTLPSize: %d\n", pAdapter->PortName, pAdapter->UplinkTLPSize)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPEnableTLP: %d\n", pAdapter->PortName, pAdapter->MPEnableTLP)
      );
      /*****************
      if (pAdapter->MPEnableTLP == 0)
      {
         // quick TX is enabled for TLP
         pAdapter->TLPEnabled = FALSE;
         pAdapter->MPQuickTx = 0;
      }
      else
      {
         pAdapter->TLPEnabled = TRUE;
         pAdapter->MPQuickTx = 1;  // force quick TX, remove this for reg config to take effect
      }
      *********************/
      #endif // QCMP_UL_TLP

#ifdef QCMP_DL_TLP
    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_DETAIL,
       ("<%s> MPEnableDLTLP: %d\n", pAdapter->PortName, pAdapter->MPEnableDLTLP)
    );
#endif // QCMP_DL_TLP
  
#if defined(QCMP_MBIM_UL_SUPPORT)
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPEnableMBIMUL: %d\n", pAdapter->PortName, pAdapter->MPEnableMBIMUL)
      );
#endif
#if defined(QCMP_MBIM_DL_SUPPORT)
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPEnableMBIMDL: %d\n", pAdapter->PortName, pAdapter->MPEnableMBIMDL)
      );
#endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPEnableQMAPV2: %d\n", pAdapter->PortName, pAdapter->MPEnableQMAPV2)
            );

            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPEnableQMAPV3: %d\n", pAdapter->PortName, pAdapter->MPEnableQMAPV3)
            );
#endif
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
             QCNET_DbgPrint
             (
                 MP_DBG_MASK_CONTROL,
                 MP_DBG_LEVEL_DETAIL,
                 ("<%s> MPEnableQMAPV1: %d\n", pAdapter->PortName, pAdapter->MPEnableQMAPV1)
              );

              QCNET_DbgPrint
              (
                  MP_DBG_MASK_CONTROL,
                  MP_DBG_LEVEL_DETAIL,
                  ("<%s> MPEnableQMAPV4: %d\n", pAdapter->PortName, pAdapter->MPEnableQMAPV4)
              );
#endif

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> ndpSignature: 0x%x\n", pAdapter->PortName, pAdapter->ndpSignature)
      );

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MuxId: 0x%x\n", pAdapter->PortName, pAdapter->MuxId)
      );

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> QCDriverMTUSize: %d\n", pAdapter->PortName, pAdapter->MTUSize)
      );

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> QCDriverTransmitTimer: %d\n", pAdapter->PortName, pAdapter->TransmitTimerValue)
      );

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPDisableQoS: %d\n", pAdapter->PortName, pAdapter->MPDisableQoS)
      );

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMaxPendingQMIReqs: %d\n", pAdapter->PortName, pAdapter->MaxPendingQMIReqs)
      );

#ifdef QCUSB_MUX_PROTOCOL
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> FakeIMSI : %d and FakeICCID : %d\n", pAdapter->PortName, pAdapter->FakeIMSI, pAdapter->FakeICCID)
      );
#endif
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> IndClusterSize : %lu\n", pAdapter->PortName, pAdapter->RxIndClusterSize)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> RxStreams : %lu\n", pAdapter->PortName, pAdapter->RxStreams)
      );
      // We can't call alloc buffers until after the parse registry because some of
      // the buffer counts are setup from registry values.
      Status = MPINI_AllocAdapterBuffers(pAdapter);
      if ( Status != NDIS_STATUS_SUCCESS )
      {
         break;
      }

#ifndef NDIS60_MINIPORT
      // Set miniport attributes
      NdisMSetAttributesEx
      (
         MiniportAdapterHandle,
         (NDIS_HANDLE) pAdapter,
         4,
#ifdef NDIS51_MINIPORT            
         NDIS_ATTRIBUTE_DESERIALIZE |      // NDIS does not maintain a send-packet
         NDIS_ATTRIBUTE_SURPRISE_REMOVE_OK,
         NdisInterfaceInternal);
#else
         NDIS_ATTRIBUTE_DESERIALIZE|
         NDIS_ATTRIBUTE_USES_SAFE_BUFFER_APIS, 
         NdisInterfacePci
      );
#endif               
#endif
      // 
      // Get the Adapter Resources & Initialize the hardware.
      //
      Status = MPINI_InitializeAdapter(pAdapter, WrapperConfigurationContext);
      if ( Status != NDIS_STATUS_SUCCESS )
      {
         Status = NDIS_STATUS_FAILURE;
         break;
      }

      // Set Device Instance
      Status = MPMAIN_SetDeviceId(pAdapter, TRUE);

      // Get device friendly name
      Status = MPMAIN_GetDeviceFriendlyName( pAdapter );

   } while ( FALSE );

   if ( Status == NDIS_STATUS_SUCCESS )
   {
      // Initialize USB component
      pAdapter->UsbRemoved = FALSE;
      pAdapter->USBDo = USBIF_InitializeUSB
                        (
                           (PVOID)pAdapter,
                           gDriverObject,
                           pAdapter->Pdo,
                           pAdapter->Fdo,
                           pAdapter->NextDeviceObject,
                           WrapperConfigurationContext,
                           pAdapter->PortName
                        );
      if ( pAdapter->USBDo != NULL )
      {
         USBIF_AddFdoMap(pAdapter->Fdo, (PVOID)pAdapter);
         #ifdef QCUSB_MUX_PROTOCOL
         USBIF_SetDLCI(pAdapter->USBDo);
         #endif // QCUSB_MUX_PROTOCOL

         //
         // Attach this Adapter to the global list of adapters managed by
         // this driver.
         //
         MPINI_Attach(pAdapter);

         //
         // Create an IOCTL interface
         //
         MPIOC_RegisterDevice(pAdapter, WrapperConfigurationContext);

         // Indicate initial media ststus
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_INFO,
            ("<%s> MPINI_MiniportInitialize: Indicate initial Status (DISCONNECT)\n", gDeviceName)
         );

         MPMAIN_MediaDisconnect(pAdapter, TRUE);

         MPMAIN_StartMPThread(pAdapter);

         // KeClearEvent(&pAdapter->ControlReadPostedEvent);

         MPWork_ScheduleWorkItem( pAdapter );

#ifdef QCMP_DISABLE_QMI
         if (pAdapter->DisableQMI == 0)
         {
#endif
         KeWaitForSingleObject
         (
            &pAdapter->ControlReadPostedEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
         );
#ifdef QCMP_DISABLE_QMI
         }
#endif         
         KeClearEvent(&pAdapter->ControlReadPostedEvent);

         // In order to receive the response, this is issued only after the
         // control reads are posted

         MPMAIN_InitializeQMI(pAdapter, 1);


         // Init again if the above fails
         KeSetEvent(&pAdapter->MPThreadInitQmiEvent, IO_NO_INCREMENT, FALSE);
         // if (FALSE == MPMAIN_InitializeQMI(pAdapter, 1))
         // {
         //    MPMAIN_MiniportHalt(pAdapter);
         //    Status = NDIS_STATUS_FAILURE;
         // }

         // Not a good idea to call Halt here. Not sure if
         // at this point the MP is fully initialized yet, so there
         // is a good chance to get a dead lock by calling Halt
         // within the Init function.
         if (Status != NDIS_STATUS_SUCCESS)
         {
            MPMAIN_MiniportHalt(pAdapter);
            Status = NDIS_STATUS_FAILURE;
         }
         else
         {
            pAdapter->Flags |= fMP_STATE_INITIALIZED;
         }
      }
      else
      {
         NdisInterlockedDecrement( &pAdapter->RefCount );
         if ( pAdapter->RefCount == 0 )
         {
            NdisSetEvent( &pAdapter->RemoveEvent );
         }
         MPINI_FreeAdapter(pAdapter);
         Status = NDIS_STATUS_FAILURE;
      }
   }
   else
   {
      if ( pAdapter )
      {
         NdisInterlockedDecrement( &pAdapter->RefCount );
         if ( pAdapter->RefCount == 0 )
         {
            NdisSetEvent( &pAdapter->RemoveEvent );
         }
         MPINI_FreeAdapter(pAdapter);
      }
   }

   QCNET_DbgPrintG( ("<%s> <--- MPINI_MiniportInitialize Status = 0x%x\n", gDeviceName, Status) );

   return Status;

}  // MPINI_MiniportInitialize

#ifdef NDIS60_MINIPORT

NDIS_STATUS MPINI_MiniportInitializeEx
(
   NDIS_HANDLE                    MiniportAdapterHandle,
   NDIS_HANDLE                    MiniportDriverContext,
   PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters
)
{
   NDIS_STATUS          Status = NDIS_STATUS_SUCCESS;
   PMP_ADAPTER          pAdapter=NULL;
   NDIS_HANDLE          ConfigurationHandle;
   NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RegistrationAttributes;
   NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES      GeneralAttributes;

   QCNET_DbgPrintG(("<%s> ---> MPINI_MiniportInitializeEx\n", gDeviceName));

   do
   {
      Status = MPINI_AllocAdapter(&pAdapter);
      if ( Status != NDIS_STATUS_SUCCESS )
      {
         break;
      }

      NdisInterlockedIncrement( &pAdapter->RefCount );  
      pAdapter->InstanceIdx = NdisInterlockedIncrement(&GlobalData.InstanceIndex);
      sprintf(pAdapter->PortName, "qnet%04d", pAdapter->InstanceIdx);

#ifdef NDIS_WDM
      KeInitializeEvent(&pAdapter->USBPnpCompletionEvent, NotificationEvent,FALSE);
#endif
      NdisMGetDeviceProperty
      (
         MiniportAdapterHandle,
         &pAdapter->Pdo,
         &pAdapter->Fdo,
         &pAdapter->NextDeviceObject,
         NULL,
         NULL
      );
      pAdapter->AdapterHandle = MiniportAdapterHandle;

      // QCNET_DbgPrintG( ("<%s> MP: pdo 0x%p fdo 0x%p ndo 0x%p\n", gDeviceName,
      //             pAdapter->Pdo, pAdapter->Fdo, pAdapter->NextDeviceObject) );

      //
      // Initialise the MAC address to a default then attempt
      // to replace the defalut by reading it from the registry
      //
      Status = MPINI_ReadNetworkAddress(pAdapter, 0);
      if (Status != NDIS_STATUS_SUCCESS)
      {
         break;
      }
      //
      // Read Advanced configuration information from the registry
      // After this MPDebugLevel is set so we can start using it to select
      // debug output
      //
      Status = MPParam_GetConfigValues
               (
                  MiniportAdapterHandle, 
                  0, // WrapperConfigurationContext,
                  pAdapter
               );
      if ( Status != NDIS_STATUS_SUCCESS )
      {
         break;
      }

#ifdef NDIS620_MINIPORT
      pAdapter->NetLuid.Value = MiniportInitParameters->NetLuid.Value;
#endif

#ifndef EVENT_TRACING
      pAdapter->DebugLevel = (pAdapter->DebugMask & MP_DBG_LEVEL_MASK);
      pAdapter->DebugMask  = (pAdapter->DebugMask & MP_DBG_MASK_MASK);
#endif

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPMaxDataSends: %u\n", pAdapter->PortName, pAdapter->MaxDataSends)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPMaxDataReceives: %u\n", pAdapter->PortName, pAdapter->MaxDataReceives)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPMaxCtrlSends: %u\n", pAdapter->PortName, pAdapter->MaxCtrlSends)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPCtrlSendSize: %u\n", pAdapter->PortName, pAdapter->CtrlSendSize)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPMaxCtrlReceives: %u\n", pAdapter->PortName, pAdapter->MaxCtrlReceives)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPCtrlReceiveSize: %u\n", pAdapter->PortName, pAdapter->CtrlReceiveSize)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPNumClients: %u\n", pAdapter->PortName, pAdapter->NumClients)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPReconfigDelay: %u\n", pAdapter->PortName, pAdapter->ReconfigDelay)
      );

      #ifdef NDIS620_MINIPORT
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPNetLuidIndex: %u\n", pAdapter->PortName, pAdapter->NetLuidIndex)
      );
      #endif // NDIS620_MINIPORT

      #ifdef QCMP_TEST_MODE
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_TRACE,
         ("<%s> MPTestConnectDelay: %u\n", pAdapter->PortName, pAdapter->TestConnectDelay)
      );
      #endif // QCMP_TEST_MODE
      
      #ifdef QCMP_UL_TLP
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> UplinkTLPSize: %d\n", pAdapter->PortName, pAdapter->UplinkTLPSize)
            );
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPEnableTLP: %d\n", pAdapter->PortName, pAdapter->MPEnableTLP)
            );
      #endif // QCMP_UL_TLP
#ifdef QCMP_DL_TLP
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPEnableDLTLP: %d\n", pAdapter->PortName, pAdapter->MPEnableDLTLP)
            );
#endif // QCMP_DL_TLP
            
#if defined(QCMP_MBIM_UL_SUPPORT)
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPEnableMBIMUL: %d\n", pAdapter->PortName, pAdapter->MPEnableMBIMUL)
            );
#endif
#if defined(QCMP_MBIM_DL_SUPPORT)
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPEnableMBIMDL: %d\n", pAdapter->PortName, pAdapter->MPEnableMBIMDL)
            );
#endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPEnableQMAPV2 : %d\n", pAdapter->PortName, pAdapter->MPEnableQMAPV2)
            );
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPEnableQMAPV3 : %d\n", pAdapter->PortName, pAdapter->MPEnableQMAPV3)
            );
#endif
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPEnableQMAPV1 : %d\n", pAdapter->PortName, pAdapter->MPEnableQMAPV1)
            );
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPEnableQMAPV4 : %d\n", pAdapter->PortName, pAdapter->MPEnableQMAPV4)
            );
#endif

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> QCDriverMTUSize: %d\n", pAdapter->PortName, pAdapter->MTUSize)
      );

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> QCDriverTransmitTimer: %d\n", pAdapter->PortName, pAdapter->TransmitTimerValue)
      );


      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> Deregister: %d\n", pAdapter->PortName, pAdapter->Deregister)
      );

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> ndpSignature: 0x%x\n", pAdapter->PortName, pAdapter->ndpSignature)
      );

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MuxId: 0x%x\n", pAdapter->PortName, pAdapter->MuxId)
      );

#ifdef QCUSB_MUX_PROTOCOL
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> FakeIMSI : %d and FakeICCID : %d\n", pAdapter->PortName, pAdapter->FakeIMSI, pAdapter->FakeICCID)
      );
#endif
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> IndClusterSize : %lu\n", pAdapter->PortName, pAdapter->RxIndClusterSize)
      );
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> RxStreams : %lu\n", pAdapter->PortName, pAdapter->RxStreams)
      );

      // We can't call alloc buffers until after the parse registry because some of
      // the buffer counts are setup from registry values.
      Status = MPINI_AllocAdapterBuffers(pAdapter);
      if ( Status != NDIS_STATUS_SUCCESS )
      {
         break;
      }

      NdisZeroMemory
      (
         &RegistrationAttributes,
         sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES)
      );
      NdisZeroMemory
      (
         &GeneralAttributes,
         sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES)
      );

      // set REGISTRATION_ATTRIBUTES
      RegistrationAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
      RegistrationAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
      RegistrationAttributes.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES);
      RegistrationAttributes.MiniportAdapterContext = (NDIS_HANDLE)pAdapter;
      RegistrationAttributes.AttributeFlags = (NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM |
                                               NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND |
                                               NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK);
      RegistrationAttributes.CheckForHangTimeInSeconds = 4;
      RegistrationAttributes.InterfaceType = NdisInterfacePci;

      Status = NdisMSetMiniportAttributes
               (
                  MiniportAdapterHandle,
                  (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RegistrationAttributes
               );

      // 
      // Get the Adapter Resources & Initialize the hardware.
      //
      Status = MPINI_InitializeAdapter(pAdapter, 0);
      if ( Status != NDIS_STATUS_SUCCESS )
      {
         Status = NDIS_STATUS_FAILURE;
         break;
      }

      // Set Device Instance
      Status = MPMAIN_SetDeviceId(pAdapter, TRUE);

      // Get device friendly name
      Status = MPMAIN_GetDeviceFriendlyName( pAdapter );

   } while ( FALSE );

   if ( Status == NDIS_STATUS_SUCCESS )
   {
      // Initialize USB component
      pAdapter->UsbRemoved = FALSE;
      pAdapter->USBDo = USBIF_InitializeUSB
                        (
                           (PVOID)pAdapter,
                           gDriverObject,
                           pAdapter->Pdo,
                           pAdapter->Fdo,
                           pAdapter->NextDeviceObject,
                           0, // WrapperConfigurationContext,
                           pAdapter->PortName
                        );
      if ( pAdapter->USBDo != NULL )
      {
         USBIF_AddFdoMap(pAdapter->Fdo, (PVOID)pAdapter);

         //
         // Attach this Adapter to the global list of adapters managed by
         // this driver.
         //
         MPINI_Attach(pAdapter);

         //
         // Create an IOCTL interface
         //
         MPIOC_RegisterDevice(pAdapter, 0);

         // Indicate initial media ststus
         QCNET_DbgPrint
         (
             MP_DBG_MASK_CONTROL,
             MP_DBG_LEVEL_INFO,
             ("<%s> MPInit: Indicate initial Status (DISCONNECT)\n", gDeviceName)
         );
         pAdapter->ulMediaConnectStatus = NdisMediaStateDisconnected;
#ifdef NDIS620_MINIPORT
         {
            NDIS_LINK_STATE LinkState;
            
            pAdapter->DeviceContextState = DeviceWWanContextDetached;
            NdisZeroMemory( &LinkState, sizeof(NDIS_LINK_STATE) );
            LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            LinkState.Header.Revision  = NDIS_LINK_STATE_REVISION_1;
            LinkState.Header.Size  = sizeof(NDIS_LINK_STATE);
            LinkState.MediaConnectState = MediaConnectStateDisconnected;
            LinkState.MediaDuplexState = MediaDuplexStateUnknown;
            LinkState.PauseFunctions = NdisPauseFunctionsUnknown;
            LinkState.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
            LinkState.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
            MyNdisMIndicateStatus
            (
               pAdapter->AdapterHandle,
               NDIS_STATUS_LINK_STATE,
               (PVOID)&LinkState,
               sizeof(NDIS_LINK_STATE)
            );
         }
#else

         MyNdisMIndicateStatus
         (
            pAdapter->AdapterHandle,
            NDIS_STATUS_MEDIA_DISCONNECT,
            (PVOID)0,
            0
         );
#endif
         MPMAIN_StartMPThread(pAdapter);

         KeClearEvent(&pAdapter->ControlReadPostedEvent);

         MPWork_ScheduleWorkItem( pAdapter );

#ifdef QCMP_DISABLE_QMI
         if (pAdapter->DisableQMI == 0)
         {
#endif         
         KeWaitForSingleObject
         (
            &pAdapter->ControlReadPostedEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
         );
#ifdef QCMP_DISABLE_QMI
         }
#endif         
         KeClearEvent(&pAdapter->ControlReadPostedEvent);

#ifdef NDIS620_MINIPORT
            if (QCMP_NDIS620_Ok == TRUE)
            {
#ifdef QC_IP_MODE      
               pAdapter->NdisMediumType = NdisMediumWirelessWan;
               QCNET_DbgPrintG(("<%s setting media type to WWAN, IPMode %d\n", gDeviceName, 
                                pAdapter->IPModeEnabled));
#endif //QC_IP_MODE      
         
            }
#endif

         // In order to receive the response, this is issued only after the
         // control reads are posted
         {
            PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;
            if(pDevExt->MuxInterface.MuxEnabled == 0)
            {
               MPMAIN_InitializeQMI(pAdapter, 1);
            }
         }

         // Init again if the above fails
         KeSetEvent(&pAdapter->MPThreadInitQmiEvent, IO_NO_INCREMENT, FALSE);
         
         // Not a good idea to call Halt here. Not sure if
         // at this point the MP is fully initialized yet, so there
         // is a good chance to get a dead lock by calling Halt
         // within the Init function.
         // if (Status != NDIS_STATUS_SUCCESS)
         // {
         //    MPMAIN_MiniportHalt(pAdapter);
         //    Status = NDIS_STATUS_FAILURE;
         // }
         // else
         {
            pAdapter->Flags |= fMP_STATE_INITIALIZED;

            MPINI_SetNdisAttributes(MiniportAdapterHandle, pAdapter);
         }
      }
      else
      {
         NdisInterlockedDecrement( &pAdapter->RefCount );
         if ( pAdapter->RefCount == 0 )
         {
            NdisSetEvent( &pAdapter->RemoveEvent );
         }
         MPINI_FreeAdapter(pAdapter);
         Status = NDIS_STATUS_FAILURE;
      }
   }
   else
   {
      if ( pAdapter )
      {
         NdisInterlockedDecrement( &pAdapter->RefCount );
         if ( pAdapter->RefCount == 0 )
         {
            NdisSetEvent( &pAdapter->RemoveEvent );
         }
         MPINI_FreeAdapter(pAdapter);
      }
   }

   QCNET_DbgPrintG( ("<%s> <--- MPINI_MiniportInitializeEx Status = 0x%x\n", gDeviceName, Status) );

   return Status;

}  // MPINI_MiniportInitializeEx

#endif // NDIS60_MINIPORT

NDIS_STATUS MPINI_AllocAdapter(PMP_ADAPTER *pAdapter)
{
    PMP_ADAPTER Adapter = NULL;
    NDIS_STATUS Status;
    QOS_FLOW_QUEUE_TYPE i;
    NTSTATUS nts;

    QCNET_DbgPrintG(("<%s> --> MPINI_AllocAdapter\n", gDeviceName));

    *pAdapter = NULL;

    //
    // Allocate memory for adapter context
    //
    Status = NdisAllocateMemoryWithTag(
                                      &Adapter, 
                                      sizeof(MP_ADAPTER), 
                                      QUALCOMM_TAG);
    if( Status != NDIS_STATUS_SUCCESS )
    {
       QCNET_DbgPrintG(("<%s> Failed to allocate memory for adapter context\n", gDeviceName));
       goto allocAFail;
    }
    //
    // Zero the memory block
    //
    NdisZeroMemory(Adapter, sizeof(MP_ADAPTER));

    // Remove lock
    Adapter->pMPRmLock = &(Adapter->MPRmLock);
    IoInitializeRemoveLock(Adapter->pMPRmLock, 0, 0, 0);
    nts = IoAcquireRemoveLock(Adapter->pMPRmLock, NULL);
    if (!NT_SUCCESS(nts))
    {
       QCNET_DbgPrintG(("<%s> MPInit: rml error 0x%x\n", gDeviceName, nts));
       NdisFreeMemory(Adapter, sizeof(MP_ADAPTER), 0);
       Status = NDIS_STATUS_FAILURE;
       Adapter = NULL;
       goto allocAFail;
    }
    else
    {
       QcStatsIncrement(Adapter, MP_RML_CTL, 100);
    }

    Adapter->DeviceId = MP_DEVICE_ID_NONE;
    Adapter->QMI_ID   = MP_INVALID_QMI_ID;

    Adapter->QMIInitInProgress = 0;

    NdisInitializeListHead(&Adapter->List);

    //
    // Initialize Send & Recv listheads and corresponding 
    // spinlocks.
    //

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QC_DUAL_IP_FC
    NdisInitializeListHead(&Adapter->FlowControlListV4);
    NdisInitializeListHead(&Adapter->FlowControlListV6);
    Adapter->FlowControlV4 = FALSE;
    Adapter->FlowControlV6 = FALSE;
#endif // QC_DUAL_IP_FC
#endif // QCUSB_MUX_PROTOCOL

#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
        NdisInitializeListHead(&Adapter->QMAPFlowControlList);
        Adapter->QMAPFlowControl = FALSE;
#endif // defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)  

    #ifdef NDIS60_MINIPORT
    NdisInitializeListHead(&Adapter->OidRequestList);
    #endif // NDIS60_MINIPORT

    NdisInitializeListHead(&Adapter->TxPendingList);
    NdisInitializeListHead(&Adapter->TxBusyList);
    NdisInitializeListHead(&Adapter->TxCompleteList);
    NdisInitializeListHead(&Adapter->LHFreeList);
    NdisAllocateSpinLock(&Adapter->TxLock);
    NdisAllocateSpinLock(&Adapter->UsbLock);


    NdisInitializeListHead(&Adapter->RxFreeList);
    NdisInitializeListHead(&Adapter->RxPendingList);
    NdisAllocateSpinLock(&Adapter->RxLock);  

    NdisInitializeListHead(&Adapter->CtrlReadFreeList);
    NdisInitializeListHead(&Adapter->CtrlReadPendingList);
    NdisInitializeListHead(&Adapter->CtrlReadCompleteList);
    NdisAllocateSpinLock(&Adapter->CtrlReadLock);  

    NdisInitializeListHead(&Adapter->OIDFreeList);
    NdisInitializeListHead(&Adapter->OIDPendingList);
    NdisInitializeListHead(&Adapter->OIDWaitingList);
    NdisInitializeListHead(&Adapter->OIDAsyncList);
    NdisAllocateSpinLock(&Adapter->OIDLock);  

    NdisInitializeListHead(&Adapter->CtrlWriteList);
    NdisInitializeListHead(&Adapter->CtrlWritePendingList);
    NdisAllocateSpinLock(&Adapter->CtrlWriteLock);  

    #if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT)
    NdisInitializeListHead(&Adapter->UplinkFreeBufferQueue);
    
    // Initialize the timer DPC for transmit.
    KeInitializeTimer(&Adapter->TransmitTimer);
    KeInitializeDpc(&Adapter->TransmitTimerDPC, TransmitTimerDpc, Adapter);
    KeSetImportanceDpc(&Adapter->TransmitTimerDPC, HighImportance);
    Adapter->TransmitTimerValue = TRANSMIT_TIMER_VALUE;
    Adapter->TransmitTimerLaunched = FALSE;

    // Adapter->ndpSignature = 0x50444E51; //QNDP
    // Adapter->ndpSignature = 0x344D434E; //NCM4
    Adapter->ndpSignature = 0x00535049; //IPS0
        
    #endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT

    NdisInitializeTimer
    (
       &Adapter->ResetTimer,
       (PNDIS_TIMER_FUNCTION)ResetCompleteTimerDpc,
       (PVOID)Adapter
    );

    NdisInitializeTimer
    (
       &Adapter->ReconfigTimer,
       (PNDIS_TIMER_FUNCTION)ReconfigTimerDpc,
       (PVOID)Adapter
    );
    Adapter->ReconfigTimerState = RECONF_TIMER_IDLE;

   NdisInitializeTimer
   (
      &Adapter->ReconfigTimerIPv6,
      (PNDIS_TIMER_FUNCTION)ReconfigTimerDpcIPv6,
      (PVOID)Adapter
   );
   Adapter->ReconfigTimerStateIPv6 = RECONF_TIMER_IDLE;

#ifdef NDIS620_MINIPORT
   NdisInitializeTimer
   (
      &Adapter->SignalStateTimer,
      (PNDIS_TIMER_FUNCTION)SignalStateTimerDpc,
      (PVOID)Adapter
   );

   NdisInitializeTimer   
   (
      &Adapter->MsisdnTimer,
      (PNDIS_TIMER_FUNCTION)MsisdnTimerDpc,
      (PVOID)Adapter
   );

   NdisInitializeTimer
   (
      &Adapter->RegisterPacketTimer,
      (PNDIS_TIMER_FUNCTION)RegisterPacketTimerDpc,
      (PVOID)Adapter
   );

   NdisInitializeTimer
      (
      &Adapter->SignalStateDisconnectTimer,
      (PNDIS_TIMER_FUNCTION)SignalStateDisconnectTimerDpc,
      (PVOID)Adapter
      );

   // Other initialization //Start the register mode with Automatic always
   Adapter->RegisterMode = DeviceWWanRegisteredAutomatic;

#endif

    NdisInitializeEvent(&Adapter->RemoveEvent);
    // NdisInitializeEvent(&Adapter->RemoveEvent);

    // Adapter->ClientId = 0;
    Adapter->NdisMediumType  = QCMP_MEDIA_TYPE;
    Adapter->IsQMIOutOfService = FALSE;
    Adapter->QMIType  = 0;  // useless
    Adapter->ulCurrentRxRate = MP_INVALID_LINK_SPEED;
    Adapter->ulCurrentTxRate = MP_INVALID_LINK_SPEED;
    NdisInitializeListHead(&Adapter->QMICTLTransactionList);
    NdisAllocateSpinLock(&Adapter->QMICTLLock);  
    Adapter->QMICTLTransactionId = 0;
    Adapter->QMUXTransactionId = 0;
    Adapter->DeviceManufacturer = NULL;
    Adapter->DeviceModelID      = NULL;
    Adapter->DeviceRevisionID   = NULL;
    Adapter->HardwareRevisionID = NULL;
    Adapter->DevicePriID        = NULL;
    Adapter->DeviceMSISDN       = NULL;
    Adapter->DeviceMIN          = NULL;
    Adapter->DeviceIMSI         = NULL;
    Adapter->ulMediaConnectStatus = NdisMediaStateDisconnected;
    Adapter->PreviousDevicePowerState = NdisDeviceStateD0;


    KeInitializeEvent(&Adapter->QMICTLInstanceIdReceivedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->QMICTLVersionReceivedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->DeviceInfoReceivedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->ClientIdReceivedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->ClientIdReleasedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->DataFormatSetEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->CtlSyncEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->ControlReadPostedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->ReconfigCompletionEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->ReconfigCompletionEventIPv6, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->QMICTLSyncReceivedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MainAdapterQmiInitSuccessful, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MainAdapterSetDataFormatSuccessful, NotificationEvent, FALSE);
    #ifdef QC_IP_MODE
    Adapter->IsLinkProtocolSupported = FALSE;
    Adapter->IPModeEnabled           = FALSE;
    Adapter->IPV4Address             = 0;
    Adapter->IPV6Address             = 0;
    KeInitializeEvent(&Adapter->QMIWDSIPAddrReceivedEvent, NotificationEvent, FALSE);
    #endif // QC_IP_MODE
    Adapter->WdsEnableUlParams       = FALSE;

    Adapter->IsWdsAdminPresent = FALSE;

    Adapter->MPThreadEvent[MP_INIT_QMI_EVENT_INDEX] = &Adapter->MPThreadInitQmiEvent;
    Adapter->MPThreadEvent[MP_CANCEL_EVENT_INDEX] = &Adapter->MPThreadCancelEvent;
    Adapter->MPThreadEvent[MP_TX_TIMER_INDEX] = &Adapter->MPThreadTxTimerEvent;
    Adapter->MPThreadEvent[MP_TX_EVENT_INDEX] = &Adapter->MPThreadTxEvent;
    Adapter->MPThreadEvent[MP_MEDIA_CONN_EVENT_INDEX] = &Adapter->MPThreadMediaConnectEvent;
    Adapter->MPThreadEvent[MP_MEDIA_CONN_EVENT_INDEX_IPV6] = &Adapter->MPThreadMediaConnectEventIPv6;
    Adapter->MPThreadEvent[MP_MEDIA_DISCONN_EVENT_INDEX] = &Adapter->MPThreadMediaDisconnectEvent;
    Adapter->MPThreadEvent[MP_IODEV_ACT_EVENT_INDEX] = &Adapter->MPThreadIODevActivityEvent;
    Adapter->MPThreadEvent[MP_IPV4_MTU_EVENT_INDEX] = &Adapter->IPV4MtuReceivedEvent;
    Adapter->MPThreadEvent[MP_IPV6_MTU_EVENT_INDEX] = &Adapter->IPV6MtuReceivedEvent;

#ifdef QCUSB_MUX_PROTOCOL
    Adapter->MPThreadEvent[MP_DL_PAUSE_EVENT_INDEX] = &Adapter->MPThreadDLPauseEvent;
    Adapter->MPThreadEvent[MP_DL_RESUME_EVENT_INDEX] = &Adapter->MPThreadDLResumeEvent;
    KeInitializeEvent(&Adapter->MPThreadDLPauseEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadDLResumeEvent, NotificationEvent, FALSE);
    #ifdef QC_DUAL_IP_FC
    Adapter->MPThreadEvent[MP_PAUSE_V4_EVENT_INDEX] = &Adapter->MPThreadPauseV4Event;
    Adapter->MPThreadEvent[MP_PAUSE_V6_EVENT_INDEX] = &Adapter->MPThreadPauseV6Event;
    Adapter->MPThreadEvent[MP_RESUME_V4_EVENT_INDEX] = &Adapter->MPThreadResumeV4Event;
    Adapter->MPThreadEvent[MP_RESUME_V6_EVENT_INDEX] = &Adapter->MPThreadResumeV6Event;
    KeInitializeEvent(&Adapter->MPThreadPauseV4Event, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadPauseV6Event, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadResumeV4Event, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadResumeV6Event, NotificationEvent, FALSE);
    #endif // QC_DUAL_IP_FC    
#endif // #ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
    NdisInitializeListHead(&Adapter->QMAPControlList);
    Adapter->MPThreadEvent[MP_PAUSE_DL_QMAP_EVENT_INDEX] = &Adapter->MPThreadQMAPDLPauseEvent;
    Adapter->MPThreadEvent[MP_RESUME_DL_QMAP_EVENT_INDEX] = &Adapter->MPThreadQMAPDLResumeEvent;
    KeInitializeEvent(&Adapter->MPThreadQMAPDLPauseEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadQMAPDLResumeEvent, NotificationEvent, FALSE);
    Adapter->MPThreadEvent[MP_QMAP_CONTROL_EVENT_INDEX] = &Adapter->MPThreadQMAPControlEvent;
    KeInitializeEvent(&Adapter->MPThreadQMAPControlEvent, NotificationEvent, FALSE);
#endif    

    KeInitializeEvent(&Adapter->MPThreadInitQmiEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadCancelEvent,  NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadTxTimerEvent,  NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadTxEvent,  NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadClosedEvent,  NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadStartedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadMediaConnectEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadMediaConnectEventIPv6, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadMediaDisconnectEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MPThreadIODevActivityEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->IPV4MtuReceivedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->IPV6MtuReceivedEvent, NotificationEvent, FALSE);


    // QOS
    Adapter->IsQosPresent = FALSE;
    Adapter->QosEnabled = FALSE;
    Adapter->QosThreadEvent[QOS_CANCEL_EVENT_INDEX] = &Adapter->QosThreadCancelEvent;
    Adapter->QosThreadEvent[QOS_TX_EVENT_INDEX] = &Adapter->QosThreadTxEvent;
    KeInitializeEvent(&Adapter->QosThreadCancelEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->QosThreadTxEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->QosThreadStartedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->QosThreadClosedEvent, NotificationEvent, FALSE);

    Adapter->QosDispThreadEvent[QOS_DISP_CANCEL_EVENT_INDEX] = &Adapter->QosDispThreadCancelEvent;
    Adapter->QosDispThreadEvent[QOS_DISP_TX_EVENT_INDEX] = &Adapter->QosDispThreadTxEvent;
    KeInitializeEvent(&Adapter->QosDispThreadCancelEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->QosDispThreadTxEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->QosDispThreadStartedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->QosDispThreadClosedEvent, NotificationEvent, FALSE);
    
    KeInitializeEvent(&Adapter->QmiQosThreadCancelEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->QmiQosThreadStartedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->QmiQosThreadClosedEvent, NotificationEvent, FALSE);

    NdisInitializeListHead(&Adapter->FilterPrecedenceList);
    for (i = FLOW_SEND_WITH_DATA; i < FLOW_QUEUE_MAX; i++)
    {
       NdisInitializeListHead(&Adapter->FlowDataQueue[i]);
    }
    NdisInitializeListHead(&(Adapter->DefaultQosFlow.FilterList));
    NdisInitializeListHead(&(Adapter->DefaultQosFlow.Packets));
    InsertHeadList(&(Adapter->FlowDataQueue[FLOW_DEFAULT]), &(Adapter->DefaultQosFlow.List));
    Adapter->DefaultFlowEnabled = TRUE;
    Adapter->ToPowerDown = FALSE;

    // Work Thread
    KeInitializeEvent(&Adapter->WorkThreadStartedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->WorkThreadClosedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->WorkThreadCancelEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->WorkThreadProcessEvent, NotificationEvent, FALSE);
    Adapter->WorkThreadEvent[WORK_CANCEL_EVENT_INDEX] = &Adapter->WorkThreadCancelEvent;
    Adapter->WorkThreadEvent[WORK_PROCESS_EVENT_INDEX] = &Adapter->WorkThreadProcessEvent;

    // RX Thread
    if (1)
    {
       int i;

       for (i = 0; i < RX_THREAD_COUNT; i++)
       {
          #ifdef NDIS60_MINIPORT
          NdisInitializeListHead(&(Adapter->RxNblChain[i]));
          #endif
          KeInitializeEvent(&Adapter->RxThreadStartedEvent[i], NotificationEvent, FALSE);
          KeInitializeEvent(&Adapter->RxThreadClosedEvent[i], NotificationEvent, FALSE);
          KeInitializeEvent(&Adapter->RxThreadCancelEvent[i], NotificationEvent, FALSE);
          KeInitializeEvent(&Adapter->RxThreadProcessEvent[i], NotificationEvent, FALSE);
          Adapter->RxThreadEvent[i][RX_CANCEL_EVENT_INDEX] = &Adapter->RxThreadCancelEvent[i];
          Adapter->RxThreadEvent[i][RX_PROCESS_EVENT_INDEX] = &Adapter->RxThreadProcessEvent[i];
          Adapter->RxThreadContext[i].AdapterContext = (PVOID)Adapter;
          Adapter->RxThreadContext[i].Index = i;
          NdisAllocateSpinLock(&Adapter->RxIndLock[i]);  
          NdisAllocateSpinLock(&Adapter->RxProtocolLock[i]);  
       }
    }

    // WDS IP thread
    KeInitializeEvent(&Adapter->WdsIpThreadStartedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->WdsIpThreadClosedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->WdsIpThreadCancelEvent, NotificationEvent, FALSE);

    // QMI Initialization
    Adapter->QmiInitialized = FALSE;
    Adapter->IsQmiWorking = FALSE;
    Adapter->WdsMediaConnected = FALSE;

    Adapter->IsMipderegSent = FALSE;

    //Initialization all thread synchronization
    Adapter->MPThreadCancelStarted = 0;
    Adapter->WorkThreadCancelStarted = 0;
    Adapter->IpThreadCancelStarted = 0;

    // Mux Id
    Adapter->MuxId = 0x00;
    
    allocAFail:
    *pAdapter = Adapter;
    QCNET_DbgPrintG(("<%s> <-- MPINI_AllocAdapter\n", gDeviceName));
    return(Status);
}  // MPINI_AllocAdapter

NDIS_STATUS MPINI_AllocAdapterBuffers( PMP_ADAPTER pAdapter )
{
    PNDIS_PACKET Packet;
    PNDIS_BUFFER Buffer;
    PUCHAR       pAllocMem;
    NDIS_STATUS  Status;
    PVOID        pOID;
    pLLHeader    pHead;
    LONG         temp;
    LONG         index;

    // Allocate packet pool for receives
    //
    #ifdef NDIS60_MINIPORT
    Status = MPINI_AllocateReceiveNBL(pAdapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
       return Status;
    }
    goto InitCtrlAndTx;
    #else // NDIS60_MINIPORT

    NdisAllocatePacketPool(
                          &Status,
                          &pAdapter->RxPacketPoolHandle,
                          pAdapter->MaxDataReceives,
                          PROTOCOL_RESERVED_SIZE_IN_PACKET);

    if( Status != NDIS_STATUS_SUCCESS )
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_ERROR,
          ("<%s> error: NdisAllocatePacketPool failed\n", pAdapter->PortName)
       );
       goto allocFail;
    }

    NdisAllocateBufferPool(
                          &Status,
                          &pAdapter->RxBufferPool,
                          pAdapter->MaxDataReceives);
    if( Status != NDIS_STATUS_SUCCESS )
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_ERROR,
          ("<%s> error: NdisAllocateBufferPool for Recv buffer pool failed\n", pAdapter->PortName)
       );
        goto allocFail;
    }

    //
    // Initialize receive packets
    //
    for( index=0; index < pAdapter->MaxDataReceives; index++ )
    {
        //
        // Allocate a packet descriptor for receive packets
        // from a preallocated pool.
        //
        NdisAllocatePacket(
                          &Status,
                          &Packet,
                          pAdapter->RxPacketPoolHandle);
        if( Status != NDIS_STATUS_SUCCESS )
        {
           QCNET_DbgPrint
           (
              MP_DBG_MASK_CONTROL,
              MP_DBG_LEVEL_ERROR,
              ("<%s> error: NdisAllocatePacket failed\n", pAdapter->PortName)
           );

           goto allocFail;
        }
        NDIS_SET_PACKET_HEADER_SIZE(Packet, ETH_HEADER_SIZE);

        Status = NdisAllocateMemoryWithTag(
                                          &pAllocMem, 
                                          MAX_RX_BUFFER_SIZE,
                                          QUALCOMM_TAG);

        if( Status != NDIS_STATUS_SUCCESS )
        {
           QCNET_DbgPrint
           (
              MP_DBG_MASK_CONTROL,
              MP_DBG_LEVEL_ERROR,
              ("<%s> error: Failed to allocate Recv Buffer[%d]\n", pAdapter->PortName, index)
           );
           goto allocFail;
        }
        NdisAllocateBuffer(
                          &Status,
                          &Buffer,
                          pAdapter->RxBufferPool,
                          (PVOID)pAllocMem,
                          MAX_RX_BUFFER_SIZE );
        if( Status != NDIS_STATUS_SUCCESS )
        {
           QCNET_DbgPrint
           (
              MP_DBG_MASK_CONTROL,
              MP_DBG_LEVEL_ERROR,
              ("<%s> error: NdisAllocateBuffer failed\n", pAdapter->PortName)
           );
           goto allocFail;
        }
        NdisChainBufferAtBack(Packet, Buffer);

        //
        // Insert it into the list of free receive packets.
        //
        NdisInterlockedInsertTailList(
                                     &pAdapter->RxFreeList, 
                                     (PLIST_ENTRY)&(Packet->MiniportReserved[0]), 
                                     &pAdapter->RxLock);
        NdisInterlockedIncrement(&pAdapter->nRxFreeInMP);
        }
#endif
    InitCtrlAndTx:

    // Setup the structures to read data from the control channel of the QC SUB
    // (unsolicited indications and responses to query requests)
    Status = NdisAllocateMemoryWithTag(
                                      &pAllocMem, 
                                      ((sizeof(MP_OID_READ)+pAdapter->CtrlReceiveSize) * pAdapter->MaxCtrlReceives), 
                                      QUALCOMM_TAG);

    if( Status != NDIS_STATUS_SUCCESS )
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_ERROR,
          ("<%s> error: Failed to allocate memory for CtrlReads\n", pAdapter->PortName)
       );
       goto allocFail;
    }
    pAdapter->CtrlReadBufMem = pAllocMem;

    for( index = 0; index < pAdapter->MaxCtrlReceives; index++ )
        {

        pOID = (pAdapter->CtrlReadBufMem + (index * ((sizeof(MP_OID_READ) + pAdapter->CtrlReceiveSize ))) );
        NdisInterlockedInsertTailList(
                                     &pAdapter->CtrlReadFreeList, 
                                     &(((PMP_OID_READ)pOID)->List), 
                                     &pAdapter->CtrlReadLock);

        }


    // Setup the structures to write the control channel of the QC USB
    // (Querys)
    Status = NdisAllocateMemoryWithTag(
                                      &pAllocMem, 
                                      ((sizeof(MP_OID_WRITE) + pAdapter->CtrlSendSize) * pAdapter->MaxCtrlSends), 
                                      QUALCOMM_TAG
                                      );

    if( Status != NDIS_STATUS_SUCCESS )
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_ERROR,
          ("<%s> error: Failed to allocate memory for OID Writes\n", pAdapter->PortName)
       );
        goto allocFail;
    }
    NdisZeroMemory
    (
       pAllocMem,
       ((sizeof(MP_OID_WRITE) + pAdapter->CtrlSendSize) * pAdapter->MaxCtrlSends)
    );
       
    pAdapter->OIDBufMem = pAllocMem;

    for( index = 0; index < pAdapter->MaxCtrlSends; index++ )
        {

        pOID = (pAdapter->OIDBufMem + (index * ((sizeof(MP_OID_WRITE) + pAdapter->CtrlSendSize))) );
        NdisInitializeListHead(&((PMP_OID_WRITE)pOID)->QMIQueue);

        NdisInterlockedInsertTailList(
                                     &pAdapter->OIDFreeList, 
                                     &(((PMP_OID_WRITE)pOID)->List), 
                                     &pAdapter->OIDLock);

        }

    if( pAdapter->LLHeaderBytes > 0 )
        {
        // Setup the structures to write the control channel of the QC USB
        // (Querys)
        Status = NdisAllocateMemoryWithTag(
                                          &pAllocMem, 
                                          ((sizeof(LLHeader) + pAdapter->LLHeaderBytes) * pAdapter->MaxDataSends), 
                                          QUALCOMM_TAG
                                          );
        if( Status != NDIS_STATUS_SUCCESS )
        {
           QCNET_DbgPrint
           (
              MP_DBG_MASK_CONTROL,
              MP_DBG_LEVEL_ERROR,
              ("<%s> error: Failed to allocate memory for OID Writes 2\n", pAdapter->PortName)
           );
           goto allocFail;
        }
        pAdapter->LLHeaderMem = pAllocMem;

        for( index = 0; index < pAdapter->MaxDataSends; index++ )
            {
            pHead = (pLLHeader)(pAdapter->LLHeaderMem + (index * ((sizeof(LLHeader) + pAdapter->LLHeaderBytes ))) );
            NdisInterlockedInsertTailList(
                                         &pAdapter->LHFreeList, 
                                         &pHead->List, 
                                         &pAdapter->TxLock);

            }
        }

    allocFail:
    return Status;
}  // MPINI_AllocAdapterBuffers

VOID MPINI_FreeAdapter(PMP_ADAPTER pAdapter)
{
    NDIS_STATUS    Status;
    PNDIS_PACKET   pPacket;
    PNDIS_BUFFER   Buffer;
    PUCHAR         pMem;
    PLIST_ENTRY    pEntry;

    PNDIS_BUFFER pCurrentNdisBuffer, pPreviousNdisBuffer;
    PUCHAR pCurrentBuffer;
    ULONG dwCurrentBufferLength;
    ULONG dwTotalNdisBuffer;
    ULONG dwTotalPacketLength;

    if (pAdapter == NULL)
    {
       QCNET_DbgPrintG(("<%s> MPINI_FreeAdapter: NULL adapter\n", gDeviceName));
       return;
    }

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> --> MPINI_FreeAdapter\n", pAdapter->PortName)
    );

    MPMAIN_SetDeviceId(pAdapter, FALSE);

    NdisFreeSpinLock(&pAdapter->TxLock);

    // free RX buffers
    #ifdef NDIS60_MINIPORT
    if (QCMP_NDIS6_Ok == TRUE)
    {
       while (!IsListEmpty(&pAdapter->RxFreeList))
       {
          pEntry = (PLIST_ENTRY)NdisInterlockedRemoveHeadList
                                (
                                   &pAdapter->RxFreeList, 
                                   &pAdapter->RxLock
                                ); 
          InterlockedDecrement(&pAdapter->nRxFreeInMP);
       }

       MPINI_FreeReceiveNBL(pAdapter);
    }
    #else // NDIS60_MINIPORT
    {
       while( !IsListEmpty(&pAdapter->RxFreeList) )
       {
           pEntry = (PLIST_ENTRY) NdisInterlockedRemoveHeadList
                                  (
                                     &pAdapter->RxFreeList, 
                                     &pAdapter->RxLock
                                  );
           InterlockedDecrement(&pAdapter->nRxFreeInMP);

           if ( pEntry )
           {
               pPacket = CONTAINING_RECORD(pEntry, NDIS_PACKET, MiniportReserved);
   
               NdisQueryPacket
               (
                  pPacket, 
                  NULL, 
                  &dwTotalNdisBuffer, 
                  &pCurrentNdisBuffer, 
                  &dwTotalPacketLength
               );

               do
               {
                   pPreviousNdisBuffer = pCurrentNdisBuffer;
                   NdisGetNextBuffer (pPreviousNdisBuffer, &pCurrentNdisBuffer);
   
                   NdisQueryBufferSafe
                   (
                      pPreviousNdisBuffer, 
                      &pCurrentBuffer, 
                      &dwCurrentBufferLength,
                      NormalPagePriority
                   );

                   if( pCurrentBuffer != NULL )
                   {
                       NdisFreeMemory(pCurrentBuffer, dwCurrentBufferLength, 0);
                   }
                   NdisFreeBuffer( pPreviousNdisBuffer );

               } while( --dwTotalNdisBuffer && (pCurrentNdisBuffer != NULL) );

               NdisFreePacket(pPacket);
           }  // if
       }  // while

       if( pAdapter->RxBufferPool )
       {
           NdisFreeBufferPool( pAdapter->RxBufferPool );
           pAdapter->RxBufferPool = NULL;
       }

       if( pAdapter->RxPacketPoolHandle )
       {
           NdisFreePacketPool( pAdapter->RxPacketPoolHandle );
           pAdapter->RxPacketPoolHandle = NULL;
       }

    }
    #endif
    ASSERT(IsListEmpty(&pAdapter->RxFreeList));                  
    NdisFreeSpinLock( &pAdapter->RxLock );
    if (1)
    {
       INT i;

       for (i = 0; i < RX_THREAD_COUNT; i++)
       {
          NdisFreeSpinLock(&pAdapter->RxIndLock[i]);
          NdisFreeSpinLock(&pAdapter->RxProtocolLock[i]);
       }
    }

    // Free the memory alloced for the control reads
    if( pAdapter->CtrlReadBufMem )
    {
        NdisFreeMemory( pAdapter->CtrlReadBufMem,
                        ((sizeof(MP_OID_READ)+pAdapter->CtrlReceiveSize) * pAdapter->MaxCtrlReceives),
                        0 );
        pAdapter->CtrlReadBufMem = NULL;
    }
    NdisFreeSpinLock( &pAdapter->CtrlReadLock );

    // Free the memory alloced for the OIDs
    if( pAdapter->OIDBufMem )
    {
        NdisFreeMemory( pAdapter->OIDBufMem,
                        ((sizeof(MP_OID_WRITE) + pAdapter->CtrlSendSize) * pAdapter->MaxCtrlSends),
                        0 );
        pAdapter->OIDBufMem = NULL;
    }
    NdisFreeSpinLock( &pAdapter->OIDLock );

    //Free write lock
    NdisFreeSpinLock( &pAdapter->CtrlWriteLock );

    // Lastly, release the USB DO
    #ifdef NDIS_WDM
    MPMAIN_PnpEventWithRemoval(pAdapter, NdisDevicePnPEventQueryRemoved, TRUE);
    NdisAcquireSpinLock(MP_CtlLock);
    pAdapter->UsbRemoved = TRUE;
    NdisReleaseSpinLock(MP_CtlLock);

    // to removes USB device object incl. extension
    // not to check pAdapter->UsbRemoved
    USBIF_ShutdownUSB(pAdapter->USBDo);

    NdisAcquireSpinLock(MP_CtlLock);
    pAdapter->USBDo = NULL;
    NdisReleaseSpinLock(MP_CtlLock);

    NdisFreeSpinLock(&pAdapter->UsbLock);
    #endif

    // Free the memory allocated for the device ID
    if (pAdapter->DeviceManufacturer != NULL)
    {
       ExFreePool(pAdapter->DeviceManufacturer);
    }
    if (pAdapter->DeviceModelID != NULL)
    {
       ExFreePool(pAdapter->DeviceModelID);
    }
    if (pAdapter->DeviceRevisionID != NULL)
    {
       ExFreePool(pAdapter->DeviceRevisionID);
    }
    if (pAdapter->HardwareRevisionID != NULL)
    {
       ExFreePool(pAdapter->HardwareRevisionID);
    }
    if (pAdapter->DevicePriID != NULL)
    {
       ExFreePool(pAdapter->DevicePriID);
    }
    if (pAdapter->DeviceMSISDN != NULL)
    {
       ExFreePool(pAdapter->DeviceMSISDN);
    }
    if (pAdapter->DeviceMIN != NULL)
    {
       ExFreePool(pAdapter->DeviceMIN);
    }
    if (pAdapter->DeviceIMSI != NULL)
    {
       ExFreePool(pAdapter->DeviceIMSI);
    }

    // MPQCTL_CleanupQMICTLTransactionList(pAdapter);
    NdisFreeSpinLock(&pAdapter->QMICTLLock);

    if (pAdapter->InterfaceGUID.Buffer != NULL)
    {
       RtlFreeUnicodeString(&pAdapter->InterfaceGUID);
       pAdapter->InterfaceGUID.Buffer = NULL;
    }
    if (pAdapter->DnsRegPathV4.Buffer != NULL)
    {
       RtlFreeUnicodeString(&pAdapter->DnsRegPathV4);
       pAdapter->DnsRegPathV4.Buffer = NULL;
    }
    if (pAdapter->DnsRegPathV6.Buffer != NULL)
    {
       RtlFreeUnicodeString(&pAdapter->DnsRegPathV6);
       pAdapter->DnsRegPathV6.Buffer = NULL;
    }
    if (pAdapter->NetCfgInstanceId.Buffer != NULL)
    {
       RtlFreeAnsiString(&pAdapter->NetCfgInstanceId);
    }

    if (pAdapter->ICCID != NULL)
    {
       ExFreePool(pAdapter->ICCID);
    }

#ifdef NDIS620_MINIPORT

    if (pAdapter->RegModelId.Buffer != NULL)
    {
       RtlFreeUnicodeString(&pAdapter->RegModelId);
       pAdapter->RegModelId.Buffer = NULL;
    }

#endif

    // Statistics summary
    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_FORCE,
       ("<%s> MPINI_FreeAdapter: ST [%d, %d, %d, %d, %d, %d, %d, %d]\n", pAdapter->PortName,
         pAdapter->Stats[MP_RML_CTL], pAdapter->Stats[MP_RML_RD], pAdapter->Stats[MP_RML_WT],
         pAdapter->Stats[MP_RML_TH], pAdapter->Stats[MP_CNT_TIMER], pAdapter->Stats[MP_MEM_CTL],
         pAdapter->Stats[MP_MEM_RD],    pAdapter->Stats[MP_MEM_WT])
    );

    if (pAdapter->pMPRmLock != NULL)
    {
       IoReleaseRemoveLockAndWait(pAdapter->pMPRmLock, NULL);
    }

    //
    // Finally free the memory for pAdapter context.
    //
    NdisFreeMemory(pAdapter, sizeof(MP_ADAPTER), 0);  

    QCNET_DbgPrintG(("<%s> <-- MPINI_FreeAdapter\n", gDeviceName));
}  // MPINI_FreeAdapter

VOID MPINI_Attach(PMP_ADAPTER pAdapter)
{
    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> --> MPINI_Attach\n", pAdapter->PortName)
    );

    NdisInterlockedInsertTailList(
                                 &GlobalData.AdapterList, 
                                 &pAdapter->List, 
                                 &GlobalData.Lock);

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> <-- MPINI_Attach\n", pAdapter->PortName)
    );
}  // MPINI_Attach

VOID MPINI_Detach(PMP_ADAPTER pAdapter)
{
    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> --> MPINI_Detach\n", pAdapter->PortName)
    );

    NdisAcquireSpinLock(&GlobalData.Lock);
    RemoveEntryList(&pAdapter->List);
    NdisReleaseSpinLock(&GlobalData.Lock);

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL,
       MP_DBG_LEVEL_TRACE,
       ("<%s> <-- MPINI_Detach\n", pAdapter->PortName)
    );
}  // MPINI_Detach

NDIS_STATUS MPINI_ReadNetworkAddress
(
    PMP_ADAPTER pAdapter,
    NDIS_HANDLE WrapperConfigurationContext
)
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
    NDIS_HANDLE     ConfigurationHandle = NULL;
    PUCHAR          NetworkAddress = NULL;
    UINT            Length = 0;
    PUCHAR          pAddr;

    #ifdef NDIS60_MINIPORT

    NDIS_CONFIGURATION_OBJECT ConfigObject;

    ConfigObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    ConfigObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    ConfigObject.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
    ConfigObject.NdisHandle = pAdapter->AdapterHandle;
    ConfigObject.Flags = 0;

    #endif // NDIS60_MINIPORT

    QCNET_DbgPrint(MP_DBG_MASK_CONTROL,
                   MP_DBG_LEVEL_TRACE,
                   ("<%s> --> MPINI_ReadNetworkAddress\n", gDeviceName));

    pAdapter->NetworkAddressReadOk = FALSE;

    #ifdef NDIS60_MINIPORT
    if (QCMP_NDIS6_Ok == TRUE)
    {
       Status = NdisOpenConfigurationEx
                (
                   &ConfigObject,
                   &ConfigurationHandle
                );
    }
    #else
    NdisOpenConfiguration
    (
       &Status,
       &ConfigurationHandle,
       WrapperConfigurationContext
    );
    #endif // NDIS60_MINIPORT

    if( Status != NDIS_STATUS_SUCCESS )
    {
        QCNET_DbgPrint(MP_DBG_MASK_CONTROL,
                       MP_DBG_LEVEL_ERROR,
                       ("<%s> NdisOpenConfiguration failed 0x%x\n", gDeviceName, Status));
        return NDIS_STATUS_FAILURE;
    }

    pAdapter->PermanentAddress[0] = QUALCOMM_MAC_0;
    pAdapter->PermanentAddress[1] = QUALCOMM_MAC_1;   
    pAdapter->PermanentAddress[2] = QUALCOMM_MAC_2;   
    pAdapter->PermanentAddress[3] = QUALCOMM_MAC_3_HOST_BYTE;
    pAdapter->PermanentAddress[4] = 0x00;
    pAdapter->PermanentAddress[5] = (UCHAR) GlobalData.MACAddressByte;

    NdisInterlockedIncrement( &GlobalData.MACAddressByte );

    
    /* do not read MAC address since it is crashing  in some particular cases on Win 7 */
#ifdef NDIS51_MINIPORT 
    NdisReadNetworkAddress(
                          &Status,
                          &NetworkAddress,
                          &Length,
                          ConfigurationHandle);
#else
    Status = NDIS_STATUS_FAILURE;
#endif

    if( (Status == NDIS_STATUS_SUCCESS) && (Length == ETH_LENGTH_OF_ADDRESS) )
    {
        pAdapter->NetworkAddressReadOk = TRUE;
        ETH_COPY_NETWORK_ADDRESS(pAdapter->CurrentAddress, NetworkAddress);
        ETH_COPY_NETWORK_ADDRESS(pAdapter->PermanentAddress, NetworkAddress);
    }
    else
    {
        ETH_COPY_NETWORK_ADDRESS(pAdapter->CurrentAddress, pAdapter->PermanentAddress);
    }

    QCNET_DbgPrint(MP_DBG_MASK_CONTROL,
                   MP_DBG_LEVEL_INFO,
                   ("<%s> Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n", 
                   gDeviceName,
                   pAdapter->PermanentAddress[0],
                   pAdapter->PermanentAddress[1],
                   pAdapter->PermanentAddress[2],
                   pAdapter->PermanentAddress[3],
                   pAdapter->PermanentAddress[4],
                   pAdapter->PermanentAddress[5]));

    QCNET_DbgPrint(MP_DBG_MASK_CONTROL,
                   MP_DBG_LEVEL_INFO,
                   ("<%s> Current Address = %02x-%02x-%02x-%02x-%02x-%02x\n", 
                   gDeviceName,
                   pAdapter->CurrentAddress[0],
                   pAdapter->CurrentAddress[1],
                   pAdapter->CurrentAddress[2],
                   pAdapter->CurrentAddress[3],
                   pAdapter->CurrentAddress[4],
                   pAdapter->CurrentAddress[5]));

    //
    // Close the configuration registry
    //
    NdisCloseConfiguration(ConfigurationHandle);
    QCNET_DbgPrint(MP_DBG_MASK_CONTROL,
                   MP_DBG_LEVEL_TRACE,
                   ("<%s> <-- MPINI_ReadNetworkAddress\n", gDeviceName));

    return NDIS_STATUS_SUCCESS;
}  // MPINI_ReadNetworkAddress

NDIS_STATUS MPINI_InitializeAdapter
(
   PMP_ADAPTER pAdapter,
   NDIS_HANDLE WrapperConfigurationContext
)
{


   NDIS_STATUS Status = NDIS_STATUS_ADAPTER_NOT_FOUND;      

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> --> MPINI_InitializeAdapter\n", pAdapter->PortName)
   );

   Status = NDIS_STATUS_SUCCESS;

   #ifdef NDIS50_MINIPORT
   NdisMRegisterAdapterShutdownHandler
   (
      pAdapter->AdapterHandle,
      (PVOID)pAdapter,
      (ADAPTER_SHUTDOWN_HANDLER)MPMAIN_MiniportShutdown
   );
   #endif         

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <-- MPINI_InitializeAdapter, Status=%x\n", pAdapter->PortName, Status)
   );
   return Status;
}  // MPINI_InitializeAdapter

#ifdef NDIS60_MINIPORT

NDIS_STATUS MPINI_SetNdisAttributes
(
   NDIS_HANDLE MiniportAdapterHandle,
   PMP_ADAPTER pAdapter
)
{
   NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GeneralAttributes;
   NDIS_STATUS status;
#ifdef NDIS620_MINIPORT
   NDIS_PM_CAPABILITIES                     pmCap;
#endif

   NdisZeroMemory(&GeneralAttributes, sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES));

#ifdef NDIS620_MINIPORT
   NdisZeroMemory(&pmCap, sizeof(NDIS_PM_CAPABILITIES));
   pmCap.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
   pmCap.Header.Revision = NDIS_PM_CAPABILITIES_REVISION_1;
   pmCap.Header.Size = NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_1;
   if (QCMP_NDIS630_Ok == TRUE)
   {
#ifdef NDIS630_MINIPORT
       pmCap.Header.Revision = NDIS_PM_CAPABILITIES_REVISION_2;
       pmCap.Header.Size = NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_2;
       //pmCap.Flags = NDIS_DEVICE_WAKE_UP_ENABLE;
       //pmCap.Flags = NDIS_PM_SELECTIVE_SUSPEND_SUPPORTED;
#endif       
   }    
   // pmCap.Flags = NDIS_DEVICE_WAKE_UP_ENABLE;
   pmCap.MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
   pmCap.MinPatternWakeUp     = NdisDeviceStateUnspecified;
   pmCap.MinLinkChangeWakeUp  = NdisDeviceStateUnspecified;
   GeneralAttributes.PowerManagementCapabilitiesEx = &pmCap;

#else
   NDIS_PNP_CAPABILITIES                    pmCap;

   NdisZeroMemory(&pmCap, sizeof(NDIS_PNP_CAPABILITIES));
   // pmCap.Flags = NDIS_DEVICE_WAKE_UP_ENABLE;
   pmCap.WakeUpCapabilities.MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
   pmCap.WakeUpCapabilities.MinPatternWakeUp     = NdisDeviceStateUnspecified;
   pmCap.WakeUpCapabilities.MinLinkChangeWakeUp  = NdisDeviceStateUnspecified;
   GeneralAttributes.PowerManagementCapabilities = &pmCap;
#endif


   GeneralAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
   GeneralAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
   GeneralAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;

#ifdef NDIS620_MINIPORT
   if (QCMP_NDIS620_Ok == TRUE)
   {
      QCNET_DbgPrintG(("<%s setting attributes for NDIS6.20 and ndis 6.30\n", gDeviceName));
      GeneralAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
      GeneralAttributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
   }
#endif
   GeneralAttributes.IfType = IF_TYPE_ETHERNET_CSMACD;
   GeneralAttributes.MediaType = QCMP_MEDIA_TYPE;
   GeneralAttributes.PhysicalMediumType = QCMP_PHYSICAL_MEDIUM_TYPE;

#ifdef NDIS620_MINIPORT
   if (QCMP_NDIS620_Ok == TRUE)
   {
      GeneralAttributes.IfType = 243;
      GeneralAttributes.MediaType = QCMP_MEDIA_TYPE;
      GeneralAttributes.PhysicalMediumType = QCMP_PHYSICAL_MEDIUM_TYPEWWAN;
      
#ifdef QC_IP_MODE      
      GeneralAttributes.MediaType = QCMP_MEDIA_TYPE_WWAN;
      pAdapter->NdisMediumType = NdisMediumWirelessWan;
      QCNET_DbgPrintG(("<%s setting media type to WWAN, IPMode %d\n", gDeviceName, 
                       pAdapter->IPModeEnabled));
#endif //QC_IP_MODE      

   }
   pAdapter->NetIfType = GeneralAttributes.IfType;
#endif

   QCNET_DbgPrintG(("\n<%s Iftype %d MediaType %d PhysicalMediaType %d NdisMT %d \n", gDeviceName,
                    GeneralAttributes.IfType, GeneralAttributes.MediaType, 
                    GeneralAttributes.PhysicalMediumType, pAdapter->NdisMediumType ));

   //GeneralAttributes.MtuSize = ETH_MAX_DATA_SIZE;
   GeneralAttributes.MtuSize = QC_DATA_MTU_MAX; // pAdapter->MTUSize;
   GeneralAttributes.MaxXmitLinkSpeed = MP_MEDIA_MAX_SPEED;
   GeneralAttributes.MaxRcvLinkSpeed = MP_MEDIA_MAX_SPEED;

   GeneralAttributes.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
   GeneralAttributes.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
   GeneralAttributes.MediaConnectState = MediaConnectStateUnknown;
   GeneralAttributes.MediaDuplexState = MediaDuplexStateUnknown;
   GeneralAttributes.LookaheadSize = ETH_MAX_DATA_SIZE;

   #ifdef NDIS620_MINIPORT
   if (pAdapter->NdisMediumType == NdisMediumWirelessWan)
   {
      GeneralAttributes.MacOptions = 0;
   }
   else
   #endif // NDIS620_MINIPORT
   {
      GeneralAttributes.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                                     NDIS_MAC_OPTION_TRANSFERS_NOT_PEND  |
                                     NDIS_MAC_OPTION_NO_LOOPBACK         |
                                     NDIS_MAC_OPTION_8021P_PRIORITY      |
                                     NDIS_MAC_OPTION_8021Q_VLAN;
   }

   GeneralAttributes.SupportedPacketFilters = SUPPORTED_PACKET_FILTERS;
   pAdapter->PacketFilter                   = SUPPORTED_PACKET_FILTERS;

   GeneralAttributes.MaxMulticastListSize = MAX_MCAST_LIST_LEN;
   GeneralAttributes.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
   NdisMoveMemory
   (
      GeneralAttributes.PermanentMacAddress,
      pAdapter->PermanentAddress,
      ETH_LENGTH_OF_ADDRESS
   );

   NdisMoveMemory
   (
      GeneralAttributes.CurrentMacAddress,
      pAdapter->CurrentAddress,
      ETH_LENGTH_OF_ADDRESS
   );

   GeneralAttributes.RecvScaleCapabilities = NULL;

   #ifdef NDIS620_MINIPORT
   if (pAdapter->NdisMediumType == NdisMediumWirelessWan)
   {
      GeneralAttributes.AccessType = NET_IF_ACCESS_POINT_TO_POINT; // for WWAN
   }
   else
   #endif // NDIS620_MINIPORT
   {
      GeneralAttributes.AccessType = NET_IF_ACCESS_BROADCAST; // for ETH
   }
   GeneralAttributes.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
   GeneralAttributes.ConnectionType = NET_IF_CONNECTION_DEDICATED; // for ETH
   GeneralAttributes.IfConnectorPresent = TRUE;

#ifdef NDIS620_MINIPORT
   GeneralAttributes.SupportedStatistics = NDIS_STATISTICS_DIRECTED_FRAMES_RCV_SUPPORTED |
                                           NDIS_STATISTICS_MULTICAST_FRAMES_RCV_SUPPORTED |
                                           NDIS_STATISTICS_BROADCAST_FRAMES_RCV_SUPPORTED | 
                                           NDIS_STATISTICS_BYTES_RCV_SUPPORTED |
                                           NDIS_STATISTICS_RCV_DISCARDS_SUPPORTED |
                                           NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
                                           NDIS_STATISTICS_DIRECTED_FRAMES_XMIT_SUPPORTED |
                                           NDIS_STATISTICS_MULTICAST_FRAMES_XMIT_SUPPORTED |
                                           NDIS_STATISTICS_BROADCAST_FRAMES_XMIT_SUPPORTED |
                                           NDIS_STATISTICS_BYTES_XMIT_SUPPORTED |
                                           NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                                           NDIS_STATISTICS_XMIT_DISCARDS_SUPPORTED |
                                           NDIS_STATISTICS_DIRECTED_BYTES_RCV_SUPPORTED |
                                           NDIS_STATISTICS_MULTICAST_BYTES_RCV_SUPPORTED |
                                           NDIS_STATISTICS_BROADCAST_BYTES_RCV_SUPPORTED |
                                           NDIS_STATISTICS_DIRECTED_BYTES_XMIT_SUPPORTED |
                                           NDIS_STATISTICS_MULTICAST_BYTES_XMIT_SUPPORTED |
                                           NDIS_STATISTICS_BROADCAST_BYTES_XMIT_SUPPORTED;   
                                           
#else
   GeneralAttributes.SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
                                           NDIS_STATISTICS_RCV_OK_SUPPORTED |
                                           NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                                           NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
                                           NDIS_STATISTICS_RCV_CRC_ERROR_SUPPORTED |
                                           NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED |
                                           NDIS_STATISTICS_TRANSMIT_QUEUE_LENGTH_SUPPORTED |
                                           NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED;
#endif
   GeneralAttributes.SupportedOidList = QCMPSupportedOidList;
   GeneralAttributes.SupportedOidListLength = QCMP_SupportedOidListSize;

   status = NdisMSetMiniportAttributes
            (
               MiniportAdapterHandle,
               (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&GeneralAttributes
            );

   QCNET_DbgPrintG(("<%s> <--- NdisMSetMiniportAttributes Status = 0x%x\n", gDeviceName, status));

   return status;
}  // MPINI_SetNdisAttributes

NDIS_STATUS MPINI_AllocateReceiveNBL(PMP_ADAPTER pAdapter)
{
   NET_BUFFER_LIST_POOL_PARAMETERS poolParams;
   PNET_BUFFER                     nb;
   int i;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPINI_AllocateReceiveNBL\n", pAdapter->PortName)
   );

   NdisZeroMemory(&poolParams, sizeof(NET_BUFFER_LIST_POOL_PARAMETERS));
   poolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
   poolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
   poolParams.Header.Size = sizeof(NET_BUFFER_LIST_POOL_PARAMETERS);
   poolParams.ProtocolId = 0;
   poolParams.ContextSize = 0;
   poolParams.fAllocateNetBuffer = TRUE;
   poolParams.DataSize = 0;
   poolParams.PoolTag = QUALCOMM_TAG;

   pAdapter->RxPacketPoolHandle = NdisAllocateNetBufferListPool
                                  (
                                     pAdapter->AdapterHandle,
                                     &poolParams
                                  );


   if (pAdapter->RxPacketPoolHandle == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> MPINI_AllocateReceiveNBL: alloc failure-0\n", pAdapter->PortName)
      );
      return NDIS_STATUS_FAILURE;
   }

   pAdapter->RxBufferMem = ExAllocatePoolWithTag
                           (
                              NonPagedPool,
                              (pAdapter->MaxDataReceives * sizeof(MPUSB_RX_NBL)),
                              QUALCOMM_TAG
                           );
   if (pAdapter->RxBufferMem == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> MPINI_AllocateReceiveNBL: alloc failure-1 %dB\n", pAdapter->PortName,
            (pAdapter->MaxDataReceives * sizeof(MPUSB_RX_NBL)))
      );
      MPINI_FreeReceiveNBL(pAdapter);
      return NDIS_STATUS_FAILURE;
   }
   NdisZeroMemory(pAdapter->RxBufferMem, (pAdapter->MaxDataReceives * sizeof(MPUSB_RX_NBL)));

   for (i = 0; i < pAdapter->MaxDataReceives; i++)
   {
      pAdapter->RxBufferMem[i].BvaPtr = pAdapter->RxBufferMem[i].BVA;
      pAdapter->RxBufferMem[i].RxMdl = NdisAllocateMdl
                                       (
                                          pAdapter->AdapterHandle,
                                          pAdapter->RxBufferMem[i].BvaPtr,
                                          QCMP_MAX_DATA_PKT_SIZE
                                       );
      if (pAdapter->RxBufferMem[i].RxMdl == NULL)
      {
         if (i == 0)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_ERROR,
               ("<%s> MPINI_AllocateReceiveNBL: alloc failure-2 %dB[%d]\n", pAdapter->PortName,
                 QCMP_MAX_DATA_PKT_SIZE, i)
            );
            MPINI_FreeReceiveNBL(pAdapter);
            return NDIS_STATUS_FAILURE;
         }
         else
         {
            pAdapter->MaxDataReceives = i;
            break;
         }
      }
      else
      {
         NDIS_MDL_LINKAGE(pAdapter->RxBufferMem[i].RxMdl) = NULL;
         pAdapter->RxBufferMem[i].RxNBL = NdisAllocateNetBufferAndNetBufferList
                                          (
                                             pAdapter->RxPacketPoolHandle,
                                             0,           // ContextSize
                                             0,           // ContextBackFillSize
                                             pAdapter->RxBufferMem[i].RxMdl, // MdlChain
                                             0,           // DataOffset
                                             QCMP_MAX_DATA_PKT_SIZE          // DataLength
                                          );
         if (pAdapter->RxBufferMem[i].RxNBL == NULL)
         {
            if (i == 0)
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> MPINI_AllocateReceiveNBL: alloc failure-3 %dB[%d]\n", pAdapter->PortName,
                    QCMP_MAX_DATA_PKT_SIZE, i)
               );
               MPINI_FreeReceiveNBL(pAdapter);
               return NDIS_STATUS_FAILURE;
            }
            else
            {
               pAdapter->MaxDataReceives = i;
               break;
            }
         }
         else
         {
            nb = NET_BUFFER_LIST_FIRST_NB(pAdapter->RxBufferMem[i].RxNBL);

            ASSERT(NET_BUFFER_FIRST_MDL(nb) == pAdapter->RxBufferMem[i].RxMdl);

            NET_BUFFER_DATA_LENGTH(nb) = 0;

            pAdapter->RxBufferMem[i].RxNBL->MiniportReserved[0] = &(pAdapter->RxBufferMem[i]);
            pAdapter->RxBufferMem[i].RxNBL->SourceHandle = pAdapter->AdapterHandle;
            pAdapter->RxBufferMem[i].Irp   = NULL;
            pAdapter->RxBufferMem[i].Index = i;

            // En-Q receive NBL
            InterlockedIncrement(&pAdapter->nRxFreeInMP);
            NdisInterlockedInsertTailList
            (
               &pAdapter->RxFreeList, 
               &(pAdapter->RxBufferMem[i].List),
               &pAdapter->RxLock
            );

            // MPRCV_QnblInfo(pAdapter, &(pAdapter->RxBufferMem[i]), i, "Created:");
         }
      }
   } // for

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPINI_AllocateReceiveNBL\n", pAdapter->PortName)
   );

   return NDIS_STATUS_SUCCESS;

}  // MPINI_AllocateReceiveNBL  

VOID MPINI_FreeReceiveNBL(PMP_ADAPTER pAdapter)
{
   int i;

   if (pAdapter->RxBufferMem == NULL)
   {
      return;
   }

   for (i = 0; i < pAdapter->MaxDataReceives; i++)
   {
      if (pAdapter->RxBufferMem[i].RxMdl != NULL)
      {
         NdisFreeMdl(pAdapter->RxBufferMem[i].RxMdl);
         pAdapter->RxBufferMem[i].RxMdl = NULL;
      }
      if (pAdapter->RxBufferMem[i].RxNBL != NULL)
      {
         NdisFreeNetBufferList(pAdapter->RxBufferMem[i].RxNBL);
         pAdapter->RxBufferMem[i].RxNBL = NULL;
      }
      if (pAdapter->RxBufferMem[i].Irp != NULL)
      {
         IoFreeIrp(pAdapter->RxBufferMem[i].Irp);
         pAdapter->RxBufferMem[i].Irp = NULL;
      }
   }

   ExFreePool(pAdapter->RxBufferMem);
   pAdapter->RxBufferMem = NULL;

   if (pAdapter->RxPacketPoolHandle != NULL)
   {
      NdisFreeNetBufferListPool(pAdapter->RxPacketPoolHandle);
      pAdapter->RxPacketPoolHandle = NULL;
   }
}  // MPINI_FreeReceiveNBL

#endif // NDIS60_MINIPORT
