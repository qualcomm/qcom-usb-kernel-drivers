/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P M A I N . C

GENERAL DESCRIPTION
    Main file for miniport layer, it provides entry points to the miniport.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdio.h>
#include "MPMain.h"
#include "MPINI.h"
#include "MPRX.h"
#include "MPTX.h"
#include "MPIOC.h"
#include "MPWork.h"
#include "USBMAIN.h"
#include "USBUTL.h"
#include "MPUsb.h"
#include "MPParam.h"
#include "MPQCTL.h"
#include "MPQMUX.h"
#ifdef MP_QCQOS_ENABLED
#include "MPQOS.h"
#include "MPQOSC.h"
#endif // MP_QCQOS_ENABLED
#include "MPOID.h"
#include "MPWWAN.h"
#include "MPIP.h"

#pragma NDIS_INIT_FUNCTION(DriverEntry)

//
// EVENT_TRACING
//
// This source code has been instrumented to support WPP / ETW event tracing.
// 
// The following section of code initializes tracing support and is required
// at the beginning of every C source code file.  EVENT_TRACING is defined in
// each of the Sources file (SRCS_*.*).  To conditionally build the driver with
// tracing or without, DO NOT modify the value of EVENT_TRACING.  Instead,
// comment out line 4, "ENABLE_EVENT_TRACING=1" of the Sources file to disable
// event tracing.  If "ENABLE_EVENT_TRACING=1" is commented out, all the debug
// traces will be sent to the kernel-debugger as either DbgPrints of KdPrints.
// 
// The placement of the following block of code is very important as to not break
// the original code and to maintain the option of being able to turn tracing on
// and off.  In general, it is best to place this after all other "#includes" and
// prior to any Globals or Functions.
// 
// A major design goal of this instrumentation was to maintain the functinoality of
// the original code base and to be minimally invasive to that code.
//
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 


//
// Auto Logger and Global Logger
//
// Auto Logger and Global Logger are mechanisms to trace the driver and
// other related drivers from system boot.  For additional INFORMATION
// refer to the "Design Guide" and "Engineering User's Guide"
//
// Currently Windows 7 and Windows Vista are using Auto Logger.  Windows XP
// does not support Auto Logger and is required to use Global Logger.  However
// Global Logging is not currently functional.
//
// The following #define is required to start Global Logger tracing at boot
// time. For this to work our driver tracing control GUID has to be enabled
// in the global logger.  The inclusion of this in the code will have not
// impact in runtime performace or behavior whether the Global Logger is
// activated in the registry or not
//
#define WPP_GLOBALLOGGER 1


//
// The trace message header file must be included in a source file
// before any WPP macro calls and after defining a WPP_CONTROL_GUIDS
// macro (defined in MPWPP.h). During the compilation, WPP scans the source
// files for any macros defined in MPWPP.h or by the RUN_WPP directive in the
// Sources file and builds a .tmh file which stores a unique
// data GUID for each message, the text resource string for each message,
// and the data types of the variables passed in for each message.  This file
// is automatically generated and used during post-processing.
//
#include "MPMAIN.tmh"

#endif   // EVENT_TRACING

BOOLEAN queuesAreEmpty( PMP_ADAPTER, char * );
VOID cleanupQueues(PMP_ADAPTER );

MP_GLOBAL_DATA  GlobalData;
NDIS_HANDLE     MyNdisHandle;
LIST_ENTRY      MPUSB_FdoMap;
BOOLEAN         QCMP_NDIS6_Ok, QCMP_NDIS620_Ok, QCMP_NDIS630_Ok;
ULONG           QCMP_SupportedOidListSize = 0;
ULONG           QCMP_SupportedOidGuidsSize = 0;

/* for setting the timer resolution */
static LONG TimerResolution = 0;


#ifdef NDIS_WDM
PDRIVER_OBJECT  gDriverObject = NULL;
#endif // NDIS_WDM

#ifdef NDIS60_MINIPORT
NDIS_HANDLE  MiniportDriverContext = NULL;
NDIS_HANDLE  NdisMiniportDriverHandle = NULL;
#endif // NDIS60_MINIPORT

NDIS_STATUS DriverEntry(PVOID DriverObject, PVOID RegistryPath)
{

   #ifdef NDIS60_MINIPORT
   NDIS_MINIPORT_DRIVER_CHARACTERISTICS NdisMPChar;
   #else
   NDIS_MINIPORT_CHARACTERISTICS Ndis5Characteristics;
   #endif // NDIS60_MINIPORT
   ANSI_STRING asRegPath;
   NDIS_STATUS ndisStatus;
   NTSTATUS    ntStatus;
   int i;

#ifdef EVENT_TRACING
#if 0   
      char cDate[] = __DATE__;
      char cTime[] = __TIME__;
#endif
      //
      // include this to support WPP.
      //
   
      // This macro is required to initialize software tracing. 
      WPP_INIT_TRACING(DriverObject,RegistryPath);
   
      KdPrint(("KdPrint - WPP_INIT_TRACING\n"));
   
#if 0 
      QCNET_DbgPrintG
      (
         ("<qcnet> --->DriverEntry built on %s at %s\n", cDate, cTime)
      );
#else
      QCNET_DbgPrintG
      (
         ("<qcnet> --->DriverEntry\n")
      );
#endif
#else

   QCNET_DbgPrintG
   (
      ("<qcnet> --->DriverEntry built on "__DATE__" at "__TIME__ "\n")
   );

#endif

   //call this to make sure NonPagedPoolNx is passed to ExAllocatePool in Win10 and above
   ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

   MPMAIN_DetermineNdisVersion();
   MPOID_GetSupportedOidListSize();
   MPOID_GetSupportedOidGuidsSize();

   NdisZeroMemory(&(GlobalData.DeviceId), (MP_DEVICE_ID_MAX+1));

#ifdef NDIS_WDM
   gDriverObject = DriverObject;

   MPMAIN_InitUsbGlobal();

   ntStatus = RtlUnicodeStringToAnsiString
              (
                 &asRegPath,
                 RegistryPath,
                 TRUE
              );

   if ( !NT_SUCCESS( ntStatus ) )
   {
      QCNET_DbgPrintG(("<qcnet> Failure at DriverEntry.\n"));
#ifdef EVENT_TRACING
      // Cleanup 
      WPP_CLEANUP(DriverObject);
#endif   
      return STATUS_UNSUCCESSFUL;
   }
   else
   {
      asRegPath.Buffer[asRegPath.Length] = 0;
      QCNET_DbgPrintG(("<qcnet> RegPath: %s\n", asRegPath.Buffer));
      RtlFreeAnsiString(&asRegPath);
   }

   ntStatus = USBUTL_AllocateUnicodeString
              (
                 &gServicePath,
                 ((PUNICODE_STRING)RegistryPath)->Length,
                 "gServicePath"
              );
   if ( ntStatus != STATUS_SUCCESS )
   {
      QCNET_DbgPrintG(("\n<%s> DriverEntry: gServicePath failure", gDeviceName) );
      _zeroUnicode(gServicePath);
   }
   else
   {
       ANSI_STRING ServiceName;
       char *p;
       RtlCopyUnicodeString(&gServicePath, RegistryPath);
       RtlUnicodeStringToAnsiString(&ServiceName, &gServicePath, TRUE);
       p = ServiceName.Buffer + ServiceName.Length - 1;
       while ( p > ServiceName.Buffer )
       {
          if (*p == '\\')
          {
             p++;
             break;
          }
          p--;
       }
       
       RtlZeroMemory(gServiceName, 255);
       strcpy(gServiceName, p);
       QCNET_DbgPrintG(("<qcnet> ServiceName: %s\n", gServiceName));
       RtlFreeAnsiString( &ServiceName );
   }

#endif // NDIS_WDM

   #ifdef NDIS60_MINIPORT
   if (QCMP_NDIS6_Ok == TRUE)
   {
      NdisZeroMemory(&NdisMPChar, sizeof(NdisMPChar));

      NdisMPChar.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS,
      NdisMPChar.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
      NdisMPChar.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;

#if defined(NDIS620_MINIPORT) || defined(NDIS630_MINIPORT)
      if (QCMP_NDIS620_Ok == TRUE)
      {
         NdisMPChar.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
         NdisMPChar.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
      }
      if (QCMP_NDIS630_Ok == TRUE)
      {
          NdisMPChar.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
          NdisMPChar.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
      }
#endif

      NdisMPChar.MajorNdisVersion            = MP_NDIS_MAJOR_VERSION;
      NdisMPChar.MinorNdisVersion            = MP_NDIS_MINOR_VERSION;

   #if defined(NDIS620_MINIPORT) || defined(NDIS630_MINIPORT)
      if (QCMP_NDIS620_Ok == TRUE)
      {
         NdisMPChar.MajorNdisVersion            = MP_NDIS_MAJOR_VERSION620;
         NdisMPChar.MinorNdisVersion            = MP_NDIS_MINOR_VERSION620;
      }
      if (QCMP_NDIS630_Ok == TRUE)
      {
          NdisMPChar.MajorNdisVersion           = MP_NDIS_MAJOR_VERSION630;
          NdisMPChar.MinorNdisVersion           = MP_NDIS_MINOR_VERSION630;
      }
#endif
      NdisMPChar.MajorDriverVersion          = MP_MAJOR_DRIVER_VERSION;
      NdisMPChar.MinorDriverVersion          = MP_MINOR_DRIVER_VERSION;

      QCNET_DbgPrintG(("\n<%s> DriverEntry: NDIS %d.%d", gDeviceName,
         NdisMPChar.MajorNdisVersion, NdisMPChar.MinorNdisVersion));

      NdisMPChar.InitializeHandlerEx         = MPINI_MiniportInitializeEx;
      NdisMPChar.HaltHandlerEx               = MPMAIN_MiniportHaltEx;
      NdisMPChar.UnloadHandler               = MPMAIN_MiniportUnload,
      NdisMPChar.PauseHandler                = MPMAIN_MiniportPause;
      NdisMPChar.RestartHandler              = MPMAIN_MiniportRestart;
      NdisMPChar.OidRequestHandler           = MPOID_MiniportOidRequest;
      NdisMPChar.SendNetBufferListsHandler   = MPTX_MiniportSendNetBufferLists;
      NdisMPChar.ReturnNetBufferListsHandler = MPRX_MiniportReturnNetBufferLists;
      NdisMPChar.CancelSendHandler           = MPTX_MiniportCancelSend;
      NdisMPChar.DevicePnPEventNotifyHandler = MPMAIN_MiniportDevicePnPEventNotify;
      NdisMPChar.ShutdownHandlerEx           = MPMAIN_MiniportShutdownEx;
      NdisMPChar.CheckForHangHandlerEx       = MPMAIN_MiniportCheckForHang;
      NdisMPChar.ResetHandlerEx              = MPMAIN_MiniportResetEx;
      NdisMPChar.CancelOidRequestHandler     = MPOID_MiniportCancelOidRequest;
      NdisMPChar.SetOptionsHandler           = MPMAIN_MiniportSetOptions;

#ifdef NDIS620_MINIPORT
      if (QCMP_NDIS620_Ok == TRUE)
      {
         NdisMPChar.DirectOidRequestHandler     = MPDirectOidRequest;
         NdisMPChar.CancelDirectOidRequestHandler = MPCancelDirectOidRequest;
      }
#endif
      NdisMPChar.Flags = NDIS_WDM_DRIVER;

      // TODO: cleanup receive NBLs
   }
   // else
   #else
   {
      NdisZeroMemory(&Ndis5Characteristics, sizeof(Ndis5Characteristics));

      // Initialize miniport handlers

      NdisMInitializeWrapper(&MyNdisHandle, DriverObject, RegistryPath, NULL);

      Ndis5Characteristics.MajorNdisVersion          = MP_NDIS_MAJOR_VERSION;
      Ndis5Characteristics.MinorNdisVersion          = MP_NDIS_MINOR_VERSION;
      Ndis5Characteristics.InitializeHandler         = MPINI_MiniportInitialize;
      Ndis5Characteristics.HaltHandler               = MPMAIN_MiniportHalt;
      Ndis5Characteristics.SetInformationHandler     = MPOID_SetInformation;
      Ndis5Characteristics.QueryInformationHandler   = MPOID_QueryInformation;
      Ndis5Characteristics.SendPacketsHandler        = MPTX_MiniportTxPackets;
      Ndis5Characteristics.ReturnPacketHandler       = MPRX_MiniportReturnPacket;
      Ndis5Characteristics.ResetHandler              = MPMAIN_MiniportReset;
      Ndis5Characteristics.CheckForHangHandler       = MPMAIN_MiniportCheckForHang;

      #ifdef NDIS51_MINIPORT
      //   Ndis5Characteristics.CancelSendPacketsHandler = MPTX_MiniportCancelSendPackets;
      Ndis5Characteristics.PnPEventNotifyHandler    = MPMAIN_MiniportPnPEventNotify;
      Ndis5Characteristics.AdapterShutdownHandler   = MPMAIN_MiniportShutdown;
      #endif
   }
   #endif // NDIS60_MINIPORT

   //
   // Initialize the global variables. The ApaterList in the
   // GloablData structure is used to track the multiple instances
   // of the same adapter. Make sure you do that before registering
   // the unload handler.
   //
   ndisStatus = NdisAllocateMemoryWithTag
                (
                   &MP_CtlLock,
                   sizeof(NDIS_SPIN_LOCK),
                   (ULONG)'0000'
                );
   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      QCNET_DbgPrintG(("<qcnet> Failed to allocate memory for MP_CtlLock\n"));
      MPMAIN_DeregisterMiniport();
      MP_CtlLock = NULL;
#ifdef EVENT_TRACING
      // Cleanup 
      WPP_CLEANUP(DriverObject);
#endif   
      return ndisStatus;
   }
   NdisAllocateSpinLock(MP_CtlLock);
   InitializeListHead(&MP_DeviceList);
   InitializeListHead(&MPUSB_FdoMap);
   NdisAllocateSpinLock(&GlobalData.Lock);
   NdisInitializeListHead(&GlobalData.AdapterList);
   GlobalData.MACAddressByte = 0;
   GlobalData.LastSysPwrIrpState = PowerSystemUnspecified;
   GlobalData.MuxId = USB_MUXID_IDENTIFIER;

   #ifdef NDIS60_MINIPORT
   if (QCMP_NDIS6_Ok == TRUE)
   {
      ndisStatus = NdisMRegisterMiniportDriver
                   (
                      DriverObject,
                      RegistryPath,
                      (PNDIS_HANDLE)MiniportDriverContext,
                      &NdisMPChar,
                      &NdisMiniportDriverHandle
                   );
   }
   // else
   #else
   {
      //
      // Registers miniport's entry points with the NDIS library as the first
      // step in NIC driver initialization. The NDIS will call the 
      // MiniportInitialize when the device is actually started by the PNP
      // manager.
      //
      ndisStatus = NdisMRegisterMiniport
                   (
                      MyNdisHandle,
                      &Ndis5Characteristics,
                      sizeof(NDIS_MINIPORT_CHARACTERISTICS)
                   );
   }
   #endif // NDIS60_MINIPORT

   if ( ndisStatus != NDIS_STATUS_SUCCESS )
   {
      QCNET_DbgPrintG( ("<%s> RegMiniport Failed: ndisStatus = 0x%x\n", gDeviceName, ndisStatus) );

      MPMAIN_DeregisterMiniport();
      NdisFreeSpinLock(&GlobalData.Lock);
      NdisFreeSpinLock(MP_CtlLock);
      NdisFreeMemory(MP_CtlLock, sizeof(NDIS_SPIN_LOCK), 0);
      MP_CtlLock = NULL;
#ifdef EVENT_TRACING
      // Cleanup 
      WPP_CLEANUP(DriverObject);
#endif   
   }
   else
   {
      USBIF_SetupDispatchFilter(DriverObject);

      if (QCMP_NDIS6_Ok == FALSE)
      {
         //
         // Register an Unload handler for global data cleanup. The unload handler
         // has a more global scope, whereas the scope of the MiniportHalt function
         // is restricted to a particular miniport instance.
         //
#ifndef NDIS60_MINIPORT
         NdisMRegisterUnloadHandler(MyNdisHandle, MPMAIN_MiniportUnload);
#endif
      }
   }

   // QCNET_DbgPrintG(("<%s> <--- DriverEntry\n", gDeviceName));
   return ndisStatus;

}  // DriverEntry

VOID MPMAIN_MiniportHalt(NDIS_HANDLE MiniportAdapterContext)
{
   MPMAIN_MiniportHaltEx(MiniportAdapterContext, 0);
}

VOID MPMAIN_MiniportHaltEx(NDIS_HANDLE MiniportAdapterContext, NDIS_HALT_ACTION HaltAction)
{
   PMP_ADAPTER       pAdapter = (PMP_ADAPTER) MiniportAdapterContext;
   BOOLEAN           bCancelled, bDone;
   LONG              nHaltCount = 0, Count;
   LARGE_INTEGER     timeoutValue;

   QCNET_DbgPrintG( ("<%s> ---> MPMAIN_MiniportHalt: action 0x%x\n", gDeviceName, HaltAction) );

   pAdapter->Flags &= ~(fMP_STATE_INITIALIZED);
   pAdapter->Flags |= fMP_ADAPTER_HALT_IN_PROGRESS;

   pAdapter->IsQMIOutOfService = TRUE;  // set the flag before cancelling any QMI service

   #ifdef MP_QCQOS_ENABLED
   MPQOSC_CancelQmiQosClient(pAdapter); 
   MPQOS_CancelQosThread(pAdapter); 
   #endif // MP_QCQOS_ENABLED

   MPIP_CancelWdsIpClient(pAdapter);

   USBCTL_ClrDtrRts(pAdapter->USBDo);  // drop DTR to cleanup device
   MPIOC_SetStopState(pAdapter, TRUE);
   
   //
   // Unregister the ioctl interface.
   //
   MPIOC_DeregisterDevice(pAdapter);
   

   // Release internal client id -- DTR drop releases all in device
   RtlZeroMemory((PVOID)pAdapter->ClientId, (QMUX_TYPE_MAX+1));

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QC_DUAL_IP_FC
      MPMAIN_PurgeFlowControlledPackets(pAdapter, FALSE);
#endif
#endif
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
      MPMAIN_QMAPPurgeFlowControlledPackets(pAdapter, FALSE);
#endif

   //Raise the device removal events
   for (Count = 0; Count < pAdapter->NamedEventsIndex; Count++)
   {
      if (pAdapter->NamedEvents[Count] != NULL)
      {
         KeSetEvent(pAdapter->NamedEvents[Count], IO_NO_INCREMENT, FALSE);
         _closeHandleG(pAdapter->PortName, pAdapter->NamedEventsHandle[Count], "MPHALT");

         // Release our reference
         ObDereferenceObject(pAdapter->NamedEvents[Count]);
         pAdapter->NamedEvents[Count] = NULL;
         pAdapter->NamedEventsHandle[Count] = NULL;
      }
   }

   //Cancel the Signal StateTimer
#ifdef NDIS620_MINIPORT
   
   NdisAcquireSpinLock(&pAdapter->QMICTLLock);

   pAdapter->nSignalStateTimerCount = 0;
   NdisCancelTimer( &pAdapter->SignalStateTimer, &bCancelled );
   if (bCancelled == FALSE)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_MiniportHalt: SignalStateTimer stopped\n", pAdapter->PortName)
      );
   }
   else
   {
      // reconfig timer cancelled, release remove lock
      QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 222);
   }

   NdisCancelTimer( &pAdapter->MsisdnTimer, &bCancelled );
   if (bCancelled == FALSE)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_MiniportHalt: msisdnTimer stopped\n", pAdapter->PortName)
      );
   }
   else
   {
      // timer cancelled, release remove lock
      QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 444);
   }

   NdisCancelTimer( &pAdapter->RegisterPacketTimer, &bCancelled );
   if (bCancelled == FALSE)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_MiniportHalt: RegisterPacketTimer stopped\n", pAdapter->PortName)
      );
   }
   else
   {
      // register timer cancelled, release remove lock
      QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 222);
   }

   NdisReleaseSpinLock(&pAdapter->QMICTLLock);
   
#endif
   //
   // Call Shutdown handler to disable interrupt and turn the hardware off 
   // by issuing a full reset
   //
#if defined(NDIS50_MINIPORT)
   MPMAIN_MiniportShutdown(MiniportAdapterContext);
#elif defined(NDIS51_MINIPORT)
   if ( ((pAdapter->Flags & fMP_ADAPTER_SURPRISE_REMOVED) == 0) )
      {
      // Shutdown reason is not suprize remove so call shutdown
      MPMAIN_MiniportShutdown(MiniportAdapterContext);
      }
#endif

   //
   // Cancel the ResetTimer.
   //    
   NdisCancelTimer(&pAdapter->ResetTimer, &bCancelled);
   if (bCancelled == TRUE)
   {
      QcStatsDecrement(pAdapter, MP_CNT_TIMER, 0);
   }

   // Cancel the ReconfigTimer
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
            ("<%s> MPMAIN_MiniportHalt: ReconfigTimer stopped 0x%x\n", pAdapter->PortName, nts)
         );
      }
      else
      {
         // reconfig timer cancelled, release remove lock
         QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 1);
      }
      pAdapter->ReconfigTimerState = RECONF_TIMER_IDLE;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_MiniportHalt: ReconfigTimer cancelled\n", pAdapter->PortName)
      );
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_MiniportHalt: ReconfigTimer idling\n", pAdapter->PortName)
      );
      NdisReleaseSpinLock(&pAdapter->QMICTLLock);
   }

   // Cancel the ReconfigTimer IPv6
   NdisAcquireSpinLock(&pAdapter->QMICTLLock);
   if (pAdapter->ReconfigTimerStateIPv6 == RECONF_TIMER_ARMED)
   {

      NdisCancelTimer(&pAdapter->ReconfigTimerIPv6, &bCancelled);
      NdisReleaseSpinLock(&pAdapter->QMICTLLock);

      if (bCancelled == FALSE)
      {
         NTSTATUS nts;

         timeoutValue.QuadPart = -((LONG)pAdapter->ReconfigDelayIPv6 * 10 * 1000);
         nts = KeWaitForSingleObject
               (
                  &pAdapter->ReconfigCompletionEventIPv6,
                  Executive, KernelMode, FALSE, &timeoutValue
               );
         KeClearEvent(&pAdapter->ReconfigCompletionEventIPv6);
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPMAIN_MiniportHalt: ReconfigTimerIPv6 stopped 0x%x\n", pAdapter->PortName, nts)
         );
      }
      else
      {
         // reconfig timer cancelled, release remove lock
         QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 1);
      }
      pAdapter->ReconfigTimerStateIPv6 = RECONF_TIMER_IDLE;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_MiniportHalt: ReconfigTimerIPv6 cancelled\n", pAdapter->PortName)
      );
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_MiniportHalt: ReconfigTimerIPv6 idling\n", pAdapter->PortName)
      );
      NdisReleaseSpinLock(&pAdapter->QMICTLLock);
   }   

   /* Set the device receive evnet so that any of the MPQMUX waiting for the data comes out */
   KeSetEvent(&pAdapter->DeviceInfoReceivedEvent, IO_NO_INCREMENT, FALSE);  

#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT)
      CancelTransmitTimer( pAdapter );
#endif

   /* Cancel the work thread */
   MPMAIN_CancelMPThread(pAdapter); 


#ifdef NDIS_WDM
   USBIF_Close(pAdapter->USBDo);
   // fake surprise removal
   MPMAIN_PnpEventWithRemoval(pAdapter, NdisDevicePnPEventSurpriseRemoved, FALSE);
#endif

   /* Before cleanup check for RxPendingList empty*/
   while ( TRUE )
   {
      // test and set bRxPendingListEmpty
      NdisAcquireSpinLock( &pAdapter->RxLock );         
      if ( !IsListEmpty( &pAdapter->RxPendingList ))
      {
         NdisReleaseSpinLock( &pAdapter->RxLock );
         NdisMSleep(50);
         continue;
      }
      else
      {
         NdisReleaseSpinLock( &pAdapter->RxLock );
         break;
      }
   }

   /* Clean up other queues if they are not empty */
   cleanupQueues( pAdapter );
   
   //
   // Decrement the ref count which was incremented in MPINI_MiniportInitialize
   //

   NdisInterlockedDecrement( &pAdapter->RefCount );
   if ( pAdapter->RefCount == 0 )
      NdisSetEvent( &pAdapter->RemoveEvent );

   //
   // Possible non-zero ref counts mean one or more of the following conditions: 
   // 1) Reset DPC is still running.
   // 2) Receive Indication DPC is still running.
   //
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> MPMAIN_MiniportHalt: RefCount=%d --- waiting!\n", pAdapter->PortName, pAdapter->RefCount)
   );
   NdisWaitEvent(&pAdapter->RemoveEvent, 0);

   while ( TRUE )
   {
      //
      // Are all the packets indicated up returned?
      //
      if ( bDone = queuesAreEmpty( pAdapter, "MPMAIN_MiniportHalt" ) )
      {
         break;   
      }

      if ( ++nHaltCount % 100 )
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
            ("<%s> Halt timed out!!!\n", pAdapter->PortName)
         );
      }

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_MiniportHalt - waiting ...\n", pAdapter->PortName)
      );
      NdisMSleep(1000000);        
   }

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   if ((pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE))
   {
#ifdef NDIS620_MINIPORT
      MPUSB_PurgeTLPQueuesEx(pAdapter, TRUE);
#else
      MPUSB_PurgeTLPQueues(pAdapter, TRUE);
#endif
   }
   else
   {
#endif   
#endif
   #if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
   if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE) || 
      (pAdapter->QMAPEnabledV4 == TRUE) || (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0))
   {
#ifdef NDIS620_MINIPORT
      MPUSB_PurgeTLPQueuesEx(pAdapter, TRUE);
#else
      MPUSB_PurgeTLPQueues(pAdapter, TRUE);
#endif
   }
   #endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   }
#endif
#endif

   ASSERT(bDone);

   MPMAIN_CancelMPThread(pAdapter); 
   //Just to make sure that all the queues are empty
   cleanupQueues(pAdapter);
   
#ifdef NDIS50_MINIPORT
   //
   // Deregister shutdown handler as it's being halted
   //
   NdisMDeregisterAdapterShutdownHandler(pAdapter->AdapterHandle);
#endif  

   MPINI_Detach(pAdapter);

   USBIF_CleanupFdoMap(pAdapter->Fdo);
   MPINI_FreeAdapter(pAdapter);
   GlobalData.LastSysPwrIrpState = PowerSystemUnspecified;

   // cannot call filtered_print, the adapter has been freed.
   QCNET_DbgPrintG(("<%s> <--- MPMAIN_MiniportHalt\n", gDeviceName));
}  // MPMAIN_MiniportHaltEx

#ifdef NDIS60_MINIPORT

NDIS_STATUS MPMAIN_MiniportResetEx
(
   NDIS_HANDLE  MiniportAdapterContext,
   PBOOLEAN  AddressingReset
)
{
#ifdef NDIS620_MINIPORT
   // TODO: Need to see if we have to cancel the OIDs which excceds their time out limit
   return NDIS_STATUS_SUCCESS;
#else
   return MPMAIN_MiniportReset(AddressingReset, MiniportAdapterContext);
#endif
}  // MPMAIN_MiniportResetEx

#endif // NDIS60_MINIPORT

NDIS_STATUS MPMAIN_MiniportReset(PBOOLEAN AddressingReset, NDIS_HANDLE MiniportAdapterContext)
{
   NDIS_STATUS       Status = NDIS_STATUS_PENDING;
   PMP_ADAPTER       pAdapter = (PMP_ADAPTER) MiniportAdapterContext;
   int i;


   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> MPMAIN_MiniportReset\n", pAdapter->PortName)
   );


   do
   {
      if ( (pAdapter->Flags & fMP_RESET_IN_PROGRESS) != 0 )
      {
         // We're already reseting so say so
         Status = NDIS_STATUS_RESET_IN_PROGRESS;
         break;   
      }

      *AddressingReset = FALSE;

      #ifdef NDIS60_MINIPORT
      if (queuesAreEmpty(pAdapter, "MPMAIN_MiniportResetEx"))
      {
         pAdapter->ResetState = RST_STATE_COMPLETE;
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> <--- MPMAIN_MiniportResetEx: State: %d PendingOIDs %d TX %d RX (N%d F%d U%d)\n", pAdapter->PortName,
              pAdapter->ResetState, pAdapter->nPendingOidReq, pAdapter->nBusyTx,
              pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb)
         );
         return NDIS_STATUS_SUCCESS;
      }
      #endif // NDIS60_MINIPORT

      //
      // Check to see if all the packets indicated up are returned.
      //
      if ( !queuesAreEmpty( pAdapter, "MPMAIN_MiniportReset" ) )
      {
         pAdapter->ResetState = RST_STATE_START_CLEANUP;

         pAdapter->nResetTimerCount = 0;
         QcStatsIncrement(pAdapter, MP_CNT_TIMER, 0);
         NdisSetTimer(&pAdapter->ResetTimer, 500 );

         break;
      }

      pAdapter->Flags |= fMP_RESET_IN_PROGRESS;
      pAdapter->ResetState = RST_STATE_QUEUES_EMPTY;
      QcStatsIncrement(pAdapter, MP_CNT_TIMER, 1);
      NdisSetTimer(&pAdapter->ResetTimer, 1);

   } while ( FALSE );

   // MPQCTL_CleanupQMICTLTransactionList(pAdapter);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- MPMAIN_MiniportResetEx Status: %x State: %d\n", pAdapter->PortName, Status, pAdapter->ResetState )
   );

   return Status;
}  // MPMAIN_MiniportResetEx


VOID ResetCompleteTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)FunctionContext;
   BOOLEAN bDone = TRUE;
   NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
   int i;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> ResetCompleteTimerDpc\n", pAdapter->PortName)
   );

   //
   // Increment the ref count on the adapter to prevent the driver from
   // unloding while the DPC is running. The Halt handler waits for the
   // ref count to drop to zero before returning. 
   //
   NdisInterlockedIncrement( &pAdapter->RefCount );  

   switch ( pAdapter->ResetState )
      {
      case RST_STATE_START_CLEANUP:
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> Dpc State: START_CLEANUP\n", pAdapter->PortName)
         );
         pAdapter->Flags |= fMP_RESET_IN_PROGRESS;

         MP_USBCleanUp( pAdapter );
         pAdapter->ResetState = RST_STATE_WAIT_FOR_EMPTY;
         QcStatsIncrement(pAdapter, MP_CNT_TIMER, 2);
         NdisSetTimer(&pAdapter->ResetTimer, 500);
         break;

      case RST_STATE_WAIT_FOR_EMPTY: // waiting for queues to drain
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> Dpc State: WAIT_FOR_EMPTY\n", pAdapter->PortName)
         );

        

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   if ((pAdapter->QMAPEnabledV2 == TRUE)|| (pAdapter->QMAPEnabledV3 == TRUE))
   {
#ifdef NDIS620_MINIPORT
      MPUSB_PurgeTLPQueuesEx(pAdapter, TRUE);
#else
      MPUSB_PurgeTLPQueues(pAdapter, TRUE);
#endif
   }
   else
   {
#endif   
#endif
   #if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
   if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE) || (pAdapter->QMAPEnabledV4 == TRUE)
        || (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0))
         {
#ifdef NDIS620_MINIPORT
            MPUSB_PurgeTLPQueuesEx(pAdapter, TRUE);
#else
            MPUSB_PurgeTLPQueues(pAdapter, TRUE);
#endif
         }
         #endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   }
#endif
#endif

         if ( bDone = queuesAreEmpty( pAdapter, "Dpc"  ) )
            {
            pAdapter->ResetState = RST_STATE_QUEUES_EMPTY;
            QcStatsIncrement(pAdapter, MP_CNT_TIMER, 3);
            NdisSetTimer(&pAdapter->ResetTimer, 1);
            }
         else
            {
            if ( ++pAdapter->nResetTimerCount <= RESET_TIMEOUT )
            {
               if ( pAdapter->nBusyWrite)
               {
                  // Empty the pending OIDs list
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                     ("<%s> Dpc: Clean out Write Queue \n", pAdapter->PortName)
                  );

                  NdisAcquireSpinLock( &pAdapter->CtrlWriteLock);
                  while ( !IsListEmpty( &pAdapter->CtrlWriteList ) )
                  {
                     PLIST_ENTRY listEntry;
                     PQCWRITEQUEUE QMIElement;

                     listEntry = RemoveHeadList( &pAdapter->CtrlWriteList );
                     
                     QMIElement = CONTAINING_RECORD( listEntry, QCWRITEQUEUE, QCWRITERequest);
                     NdisFreeMemory(QMIElement, sizeof(QCWRITEQUEUE) + QMIElement->QMILength, 0 );
                     InterlockedDecrement(&(pAdapter->nBusyWrite));
                  }

                  while ( !IsListEmpty( &pAdapter->CtrlWritePendingList ) )
                  {
                     PLIST_ENTRY listEntry;
                     PQCWRITEQUEUE QMIElement;

                     listEntry = RemoveHeadList( &pAdapter->CtrlWritePendingList );
                     
                     QMIElement = CONTAINING_RECORD( listEntry, QCWRITEQUEUE, QCWRITERequest);
                     NdisFreeMemory(QMIElement, sizeof(QCWRITEQUEUE) + QMIElement->QMILength, 0 );
                     InterlockedDecrement(&(pAdapter->nBusyWrite));
                  }

                  while ( !IsListEmpty( &pAdapter->QMAPControlList) )
                  {
                        PLIST_ENTRY listEntry;
                        PQCQMAPCONTROLQUEUE currentQMAPElement = NULL;
                        
                        listEntry = RemoveHeadList( &pAdapter->QMAPControlList );
                        
                        currentQMAPElement = CONTAINING_RECORD(listEntry, QCQMAPCONTROLQUEUE, QMAPListEntry);
                        NdisFreeMemory(currentQMAPElement, sizeof(QCQMAPCONTROLQUEUE), 0 );
                  }

                  NdisZeroMemory(&(pAdapter->PendingCtrlRequests), sizeof(pAdapter->PendingCtrlRequests));

                  NdisReleaseSpinLock( &pAdapter->CtrlWriteLock );
                  ASSERT( pAdapter->nBusyWrite == 0 );
               }

               if ( pAdapter->nBusyOID )
               {
                  // Empty the pending OIDs list
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                     ("<%s> Dpc: Clean out OID waiting\n", pAdapter->PortName)
                  );

                  NdisAcquireSpinLock( &pAdapter->OIDLock );
                  while ( !IsListEmpty( &pAdapter->OIDWaitingList ) )
                  {
                     PLIST_ENTRY listEntry;
                     PMP_OID_WRITE pListOID;

                     listEntry = RemoveHeadList( &pAdapter->OIDWaitingList );
                     
                     pListOID = CONTAINING_RECORD(listEntry, MP_OID_WRITE, List);
                     
                     MPQMI_FreeQMIRequestQueue(pListOID);
                     MPOID_CleanupOidCopy(pAdapter, pListOID);
                     InsertTailList( &pAdapter->OIDFreeList, listEntry ); 
                     InterlockedDecrement(&(pAdapter->nBusyOID));
                  }

                  while ( !IsListEmpty( &pAdapter->OIDPendingList ) )
                  {
                     PLIST_ENTRY listEntry;
                     PMP_OID_WRITE pListOID;

                     listEntry = RemoveHeadList( &pAdapter->OIDPendingList );
                     
                     pListOID = CONTAINING_RECORD(listEntry, MP_OID_WRITE, List);
                     
                     MPQMI_FreeQMIRequestQueue(pListOID);
                     MPOID_CleanupOidCopy(pAdapter, pListOID);
                     InsertTailList( &pAdapter->OIDFreeList, listEntry ); 
                     InterlockedDecrement(&(pAdapter->nBusyOID));
                  }

                  NdisReleaseSpinLock( &pAdapter->OIDLock );
                  ASSERT( pAdapter->nBusyOID == 0 );
               }

               #ifdef NDIS60_MINIPORT
               MPOID_CancelAllPendingOids(pAdapter);
               #endif // NDIS60_MINIPORT

               //
               // Let us try again.
               //
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                  ("<%s> Dpc Again\n", pAdapter->PortName)
               );
               QcStatsIncrement(pAdapter, MP_CNT_TIMER, 4);
               NdisSetTimer(&pAdapter->ResetTimer, 500);
               }
            else
               {
               // Timed out, setup to return failure
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                  ("<%s> Reset timed out waiting for queues to empty\n", pAdapter->PortName)
               );

               pAdapter->ResetState = RST_STATE_TIMEOUT;
               QcStatsIncrement(pAdapter, MP_CNT_TIMER, 5);
               NdisSetTimer(&pAdapter->ResetTimer, 1);
               }
            }
         break;

      case RST_STATE_QUEUES_EMPTY: // Queues are empty, kick the worker thread
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
            ("<%s> Dpc State: QUEUES_EMPTY\n", pAdapter->PortName)
         );

         #ifdef NDIS60_MINIPORT
         {
            MPOID_CancelAllPendingOids(pAdapter);
            pAdapter->Flags &= ~fMP_RESET_IN_PROGRESS;
            pAdapter->ResetState = RST_STATE_COMPLETE;
            QcStatsIncrement(pAdapter, MP_CNT_TIMER, 9);
            NdisSetTimer(&pAdapter->ResetTimer, 1);
            break;
         }
         #endif // NDIS60_MINIPORT

         pAdapter->Flags &= ~fMP_RESET_IN_PROGRESS;

         if ( ++pAdapter->nResetTimerCount <= RESET_TIMEOUT )
            {
            if ( MPWork_ScheduleWorkItem( pAdapter ) )
               {
               pAdapter->ResetState = RST_STATE_WAIT_USB;
               QcStatsIncrement(pAdapter, MP_CNT_TIMER, 6);
               NdisSetTimer(&pAdapter->ResetTimer, 500 );
               }
            else
               { 
               QcStatsIncrement(pAdapter, MP_CNT_TIMER, 7);
               NdisSetTimer(&pAdapter->ResetTimer, 100 );
               }
            }
         else
            {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
               ("<%s> Reset timed out trying to start the WorkItem\n", pAdapter->PortName)
            );
            pAdapter->ResetState = RST_STATE_TIMEOUT;
            QcStatsIncrement(pAdapter, MP_CNT_TIMER, 8);
            NdisSetTimer(&pAdapter->ResetTimer, 1);
            }
         break;

      case RST_STATE_TIMEOUT: // Reset failed, indicate it up
         {
            int i,j;

            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> Dpc State: TIMEOUT - %d\n", pAdapter->PortName, pAdapter->nResetTimerCount)
            );

            cleanupQueues( pAdapter );

            // Now set the adapter state and indicate the failure
            pAdapter->Flags &= ~fMP_RESET_IN_PROGRESS;
            Status = NDIS_STATUS_FAILURE;
            NdisMResetComplete(
                              pAdapter->AdapterHandle,
                              Status,
                              FALSE);
         }
         break;

      case RST_STATE_WAIT_USB:  // Make sure we are reconnected
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> Dpc State: WAIT_USB\n", pAdapter->PortName)
         );

         // Wait until all the reads have been posted
         if ( ++pAdapter->nResetTimerCount <= RESET_TIMEOUT )
            {
            if ( IsListEmpty( &pAdapter->RxFreeList ) &&
                 IsListEmpty( &pAdapter->CtrlReadFreeList ) )
               {
               pAdapter->ResetState = RST_STATE_COMPLETE;
               QcStatsIncrement(pAdapter, MP_CNT_TIMER, 9);
               NdisSetTimer(&pAdapter->ResetTimer, 1);
               }
            else
               {
               QcStatsIncrement(pAdapter, MP_CNT_TIMER, 10);
               NdisSetTimer(&pAdapter->ResetTimer, 500);
               }
            }
         else
            {
            // Timed out, setup to return failure
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
               ("<%s> Reset timed out waiting for queues to re-fill\n", pAdapter->PortName)
            );

            pAdapter->ResetState = RST_STATE_TIMEOUT;
            QcStatsIncrement(pAdapter, MP_CNT_TIMER, 11);
            NdisSetTimer(&pAdapter->ResetTimer, 1);
            }
         break;

      case RST_STATE_COMPLETE: // Reset completed successfully, indicate it up
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> Dpc State COMPLETE - %d\n", pAdapter->PortName, pAdapter->nResetTimerCount)
         );

         // Now set the adapter state and indicate success
         Status = NDIS_STATUS_SUCCESS;
         NdisMResetComplete(
                           pAdapter->AdapterHandle,
                           Status,
                           FALSE);

         break;
      }

   NdisInterlockedDecrement( &pAdapter->RefCount );
   if ( pAdapter->RefCount == 0 )
      NdisSetEvent( &pAdapter->RemoveEvent );

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- ResetCompleteTimerDpc Status = 0x%x\n", pAdapter->PortName, Status)
   );
   QcStatsDecrement(pAdapter, MP_CNT_TIMER, 2);
}  // ResetCompleteTimerDpc

VOID ReconfigTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)FunctionContext;
   NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> ReconfigTimerDpc\n", pAdapter->PortName)
   );

   NdisAcquireSpinLock(&pAdapter->QMICTLLock);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ReconfigTimerDpc: indicating ConnStat CONNECT\n", pAdapter->PortName)
   );
   // Set indication
   KeSetEvent(&pAdapter->MPThreadMediaConnectEvent, IO_NO_INCREMENT, FALSE);

   // Set event
   KeSetEvent
   (
      &pAdapter->ReconfigCompletionEvent,
      IO_NO_INCREMENT,
      FALSE
   );
   pAdapter->ReconfigTimerState = RECONF_TIMER_IDLE;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- ReconfigTimerDpc\n", pAdapter->PortName)
   );

   NdisReleaseSpinLock(&pAdapter->QMICTLLock);

   QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 3)

}  // ReconfigTimerDpc

VOID ReconfigTimerDpcIPv6
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)FunctionContext;
   NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> ReconfigTimerDpcIPv6\n", pAdapter->PortName)
   );

   NdisAcquireSpinLock(&pAdapter->QMICTLLock);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ReconfigTimerDpcIPv6: indicating ConnStat CONNECT\n", pAdapter->PortName)
   );
   // Set indication
   KeSetEvent(&pAdapter->MPThreadMediaConnectEventIPv6, IO_NO_INCREMENT, FALSE);

   // Set event
   KeSetEvent
   (
      &pAdapter->ReconfigCompletionEventIPv6,
      IO_NO_INCREMENT,
      FALSE
   );
   pAdapter->ReconfigTimerStateIPv6 = RECONF_TIMER_IDLE;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- ReconfigTimerDpcIPv6\n", pAdapter->PortName)
   );

   NdisReleaseSpinLock(&pAdapter->QMICTLLock);

   QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 3)

}  // ReconfigTimerDpcIPv6

BOOLEAN queuesAreEmpty(PMP_ADAPTER pAdapter, char *caller)
{
   BOOLEAN allQueuesClear = TRUE;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> -->queuesAreEmpty\n", pAdapter->PortName)
   );

   if ( pAdapter->nBusyRx > 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> %s: nBusyRx = %d F%d P%d (N%d F%d U%d)\n", pAdapter->PortName,
           caller, pAdapter->nBusyRx,
           listDepth(&pAdapter->RxFreeList, &pAdapter->RxLock),
           listDepth(&pAdapter->RxPendingList, &pAdapter->RxLock),
           pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb
         )
      );
      allQueuesClear = FALSE;
   }

   //
   // Are there any outstanding send packets?                                                         
   //
   if ( pAdapter->nBusyTx > 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> %s: nBusyTx = %d P%d B%d C%d\n", pAdapter->PortName,
           caller, pAdapter->nBusyTx,
           listDepth(&pAdapter->TxPendingList, &pAdapter->TxLock),
           listDepth(&pAdapter->TxBusyList, &pAdapter->TxLock),
           listDepth(&pAdapter->TxCompleteList, &pAdapter->TxLock)
         )
      );
      allQueuesClear = FALSE;
   }

#if 0
   // Are there any outstanding write packets?                                                         
   //
   if ( pAdapter->nBusyWrite > 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> %s: nBusyWrite = %d P%d \n", pAdapter->PortName,
           caller, pAdapter->nBusyWrite,
           listDepth(&pAdapter->CtrlWriteList, &pAdapter->CtrlWriteLock)
         )
      );
      allQueuesClear = FALSE;
   }
#endif

   //
   // Are there any outstanding control reads?                                                         
   //
   if ( pAdapter->nBusyRCtrl > 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> %s: nBusyRCtrl = %d F%d P%d C%d\n", pAdapter->PortName,
           caller, pAdapter->nBusyRCtrl,
           listDepth(&pAdapter->CtrlReadFreeList, &pAdapter->CtrlReadLock),
           listDepth(&pAdapter->CtrlReadPendingList, &pAdapter->CtrlReadLock),
           listDepth(&pAdapter->CtrlReadCompleteList, &pAdapter->CtrlReadLock)
         )
      );
      allQueuesClear = FALSE;
   }

   //
   // Are there any outstanding OIDs?                                                         
   //
   if ( pAdapter->nBusyOID > 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> %s: nBusyOID = %d F%d P%d W%d A%d\n", pAdapter->PortName,
           caller, pAdapter->nBusyOID,
           listDepth(&pAdapter->OIDFreeList,    &pAdapter->OIDLock),
           listDepth(&pAdapter->OIDPendingList, &pAdapter->OIDLock),
           listDepth(&pAdapter->OIDWaitingList, &pAdapter->OIDLock),
           listDepth(&pAdapter->OIDAsyncList, &pAdapter->OIDLock)
         )
      );
      allQueuesClear = FALSE;
   }

   //
   // Are there any QMI QCTL IRP's?                                                         
   //
   NdisAcquireSpinLock(&pAdapter->QMICTLLock);
   if (!IsListEmpty( &pAdapter->QMICTLTransactionList))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> %s: QCTL IRP's still pending\n", pAdapter->PortName, caller)
      );
      allQueuesClear = FALSE;
   }
   NdisReleaseSpinLock(&pAdapter->QMICTLLock);

   if (allQueuesClear == TRUE)
   {
      pAdapter->nBusyRx = pAdapter->nBusyTx = pAdapter->nBusyRCtrl = pAdapter->nBusyOID = 0;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--queuesAreEmpty: %d\n", pAdapter->PortName, allQueuesClear)
   );
   return allQueuesClear;
}

cleanupOIDWaitingQueue(PMP_ADAPTER pAdapter)
{
   NdisAcquireSpinLock( &pAdapter->OIDLock );
   while ( !IsListEmpty( &pAdapter->OIDWaitingList ) )
   {
      PLIST_ENTRY listEntry;
      PMP_OID_WRITE pListOID;

      listEntry = RemoveHeadList( &pAdapter->OIDWaitingList );
      
      pListOID = CONTAINING_RECORD(listEntry, MP_OID_WRITE, List);
      
      MPQMI_FreeQMIRequestQueue(pListOID);
      MPOID_CleanupOidCopy(pAdapter, pListOID);
      InsertTailList( &pAdapter->OIDFreeList, listEntry ); 
      InterlockedDecrement(&(pAdapter->nBusyOID));
   }
   NdisReleaseSpinLock( &pAdapter->OIDLock );
}

VOID CleanupTxQueues(PMP_ADAPTER pAdapter)
{
    PLIST_ENTRY pList;
#ifdef NDIS620_MINIPORT
    PMPUSB_TX_CONTEXT_NB txNb;
#else
    PNDIS_PACKET pNdisPacket;
#endif

    // Clean up all Flow control queues
#ifdef QCUSB_MUX_PROTOCOL
#ifdef QC_DUAL_IP_FC
    MPMAIN_PurgeFlowControlledPackets(pAdapter, FALSE);
#endif
#endif
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
    MPMAIN_QMAPPurgeFlowControlledPackets(pAdapter, FALSE);
#endif

    if ( pAdapter->nBusyTx > 0)
    {
       NdisAcquireSpinLock( &pAdapter->TxLock );
       while ( !IsListEmpty( &pAdapter->TxPendingList ) )
       {
          pList = RemoveHeadList( &pAdapter->TxPendingList ); 
#ifdef NDIS620_MINIPORT
         txNb = CONTAINING_RECORD
                 (
                    pList,
                    MPUSB_TX_CONTEXT_NB,
                    List
                 );
         ((PMPUSB_TX_CONTEXT_NBL)txNb->NBL)->NdisStatus = NDIS_STATUS_FAILURE;
         MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, txNb, FALSE); 
#else               
         pNdisPacket = ListItemToPacket(pList);
         NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
         NdisMSendComplete(pAdapter->AdapterHandle, pNdisPacket, NDIS_STATUS_FAILURE);         
#endif 
       }
       while ( !IsListEmpty( &pAdapter->TxBusyList ) )
       {
          pList = RemoveHeadList( &pAdapter->TxBusyList ); 
#ifdef NDIS620_MINIPORT
           txNb = CONTAINING_RECORD
                   (
                      pList,
                      MPUSB_TX_CONTEXT_NB,
                      List
                   );
           ((PMPUSB_TX_CONTEXT_NBL)txNb->NBL)->NdisStatus = NDIS_STATUS_FAILURE;
           MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, txNb, FALSE); 
#else               
           pNdisPacket = ListItemToPacket(pList);
           NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
           NdisMSendComplete(pAdapter->AdapterHandle, pNdisPacket, NDIS_STATUS_FAILURE);         
#endif 
       }
       while ( !IsListEmpty( &pAdapter->TxCompleteList ) )
       {
          pList = RemoveHeadList( &pAdapter->TxCompleteList ); 
#ifdef NDIS620_MINIPORT
           txNb = CONTAINING_RECORD
                   (
                      pList,
                      MPUSB_TX_CONTEXT_NB,
                      List
                   );
           ((PMPUSB_TX_CONTEXT_NBL)txNb->NBL)->NdisStatus = NDIS_STATUS_FAILURE;
           MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, txNb, FALSE); 
#else               
           pNdisPacket = ListItemToPacket(pList);
           NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
           NdisMSendComplete(pAdapter->AdapterHandle, pNdisPacket, NDIS_STATUS_FAILURE);         
#endif 
       }
       pAdapter->nBusyTx = 0;
       NdisReleaseSpinLock( &pAdapter->TxLock );
    }

}  // CleanupTxQueues
VOID cleanupQueues(PMP_ADAPTER pAdapter)
{
   if ( pAdapter->nBusyRx > 0)
   {
      NdisAcquireSpinLock( &pAdapter->RxLock );
      while ( !IsListEmpty( &pAdapter->RxPendingList ) )
      {
         InterlockedIncrement(&pAdapter->nRxFreeInMP);
         InterlockedDecrement(&pAdapter->nRxPendingInUsb);
         InsertTailList
         (
            &pAdapter->RxFreeList, 
            RemoveHeadList(&pAdapter->RxPendingList)
         ); 
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<qnetxxx> Error: RxPendingList not empty!\n")
         );
         DbgBreakPoint();
      }
      pAdapter->nBusyRx = 0;
      NdisReleaseSpinLock( &pAdapter->RxLock );

   }

   CleanupTxQueues(pAdapter);
   #ifdef NDIS60_MINIPORT
   if (1)
   {
      INT i;

      for (i = 0; i < pAdapter->RxStreams; i++)
      {
         MPRX_RxChainCleanup(pAdapter, i);
      }
   }
   #endif

   if ( pAdapter->nBusyRCtrl > 0)
   {
      NdisAcquireSpinLock( &pAdapter->CtrlReadLock );
      while ( !IsListEmpty( &pAdapter->CtrlReadCompleteList ) )
      {
         InsertTailList( &pAdapter->CtrlReadFreeList, 
                         RemoveHeadList( &pAdapter->CtrlReadCompleteList ) ); 
      }
      while ( !IsListEmpty( &pAdapter->CtrlReadPendingList ) )
      {
         InsertTailList( &pAdapter->CtrlReadFreeList, 
                         RemoveHeadList( &pAdapter->CtrlReadPendingList ) ); 
      }
      pAdapter->nBusyRCtrl = 0;
      NdisReleaseSpinLock( &pAdapter->CtrlReadLock );
   }

   if ( pAdapter->nBusyWrite > 0)
   {
      NdisAcquireSpinLock( &pAdapter->CtrlWriteLock );
      while ( !IsListEmpty( &pAdapter->CtrlWriteList ) )
      {
            PLIST_ENTRY listEntry;
            PQCWRITEQUEUE currentQMIElement = NULL;
            
            listEntry = RemoveHeadList( &pAdapter->CtrlWriteList );
            
            currentQMIElement = CONTAINING_RECORD(listEntry, QCWRITEQUEUE, QCWRITERequest);
            NdisFreeMemory(currentQMIElement, sizeof(QCWRITEQUEUE) + currentQMIElement->QMILength, 0 );
            InterlockedDecrement(&(pAdapter->nBusyWrite));
      }
      while ( !IsListEmpty( &pAdapter->CtrlWritePendingList ) )
      {
            PLIST_ENTRY listEntry;
            PQCWRITEQUEUE currentQMIElement = NULL;
            
            listEntry = RemoveHeadList( &pAdapter->CtrlWritePendingList );
            
            currentQMIElement = CONTAINING_RECORD(listEntry, QCWRITEQUEUE, QCWRITERequest);
            NdisFreeMemory(currentQMIElement, sizeof(QCWRITEQUEUE) + currentQMIElement->QMILength, 0 );
            InterlockedDecrement(&(pAdapter->nBusyWrite));
      }
      
      while ( !IsListEmpty( &pAdapter->QMAPControlList) )
      {
            PLIST_ENTRY listEntry;
            PQCQMAPCONTROLQUEUE currentQMAPElement = NULL;
            
            listEntry = RemoveHeadList( &pAdapter->QMAPControlList );
            
            currentQMAPElement = CONTAINING_RECORD(listEntry, QCQMAPCONTROLQUEUE, QMAPListEntry);
            NdisFreeMemory(currentQMAPElement, sizeof(QCQMAPCONTROLQUEUE), 0 );
      }
      
      pAdapter->nBusyWrite = 0;
      NdisZeroMemory(&(pAdapter->PendingCtrlRequests), sizeof(pAdapter->PendingCtrlRequests));
      NdisReleaseSpinLock( &pAdapter->CtrlWriteLock );
   }

   if ( pAdapter->nBusyOID > 0)
   {
      NdisAcquireSpinLock( &pAdapter->OIDLock );
      while ( !IsListEmpty( &pAdapter->OIDWaitingList ) )
      {
            PLIST_ENTRY listEntry;
            PMP_OID_WRITE pListOID;
            
            listEntry = RemoveHeadList( &pAdapter->OIDWaitingList );
            
            pListOID = CONTAINING_RECORD(listEntry, MP_OID_WRITE, List);
            
            MPQMI_FreeQMIRequestQueue(pListOID);
            MPOID_CleanupOidCopy(pAdapter, pListOID);
            InsertTailList( &pAdapter->OIDFreeList, listEntry ); 
            InterlockedDecrement(&(pAdapter->nBusyOID));
      }
      while ( !IsListEmpty( &pAdapter->OIDPendingList ) )
      {
            PLIST_ENTRY listEntry;
            PMP_OID_WRITE pListOID;
            
            listEntry = RemoveHeadList( &pAdapter->OIDPendingList );
            
            pListOID = CONTAINING_RECORD(listEntry, MP_OID_WRITE, List);
            
            MPQMI_FreeQMIRequestQueue(pListOID);
            MPOID_CleanupOidCopy(pAdapter, pListOID);
            InsertTailList( &pAdapter->OIDFreeList, listEntry ); 
            InterlockedDecrement(&(pAdapter->nBusyOID));
      }
      while ( !IsListEmpty( &pAdapter->OIDAsyncList ) )
      {
            PLIST_ENTRY listEntry;
            PMP_OID_WRITE pListOID;
            
            listEntry = RemoveHeadList( &pAdapter->OIDAsyncList );
            
            pListOID = CONTAINING_RECORD(listEntry, MP_OID_WRITE, List);
            
            MPOID_CleanupOidCopy(pAdapter, pListOID);
            InsertTailList( &pAdapter->OIDFreeList, listEntry ); 
            InterlockedDecrement(&(pAdapter->nBusyOID));
      }
      pAdapter->nBusyOID = 0;
      NdisReleaseSpinLock( &pAdapter->OIDLock );
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> cleanupQueues results - \n", pAdapter->PortName)
   );

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s>     nBusyRx = %d F%d P%d (N%d U%d F%d)\n", 
        pAdapter->PortName, pAdapter->nBusyRx,
        listDepth(&pAdapter->RxFreeList, &pAdapter->RxLock),
        listDepth(&pAdapter->RxPendingList, &pAdapter->RxLock),
        pAdapter->nRxHeldByNdis, pAdapter->nRxPendingInUsb,
        pAdapter->nRxFreeInMP)
   );

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> nBusyTx = %d P%d B%d C%d\n", 
        pAdapter->PortName,
        pAdapter->nBusyTx,
        listDepth(&pAdapter->TxPendingList, &pAdapter->TxLock),
        listDepth(&pAdapter->TxBusyList, &pAdapter->TxLock),
        listDepth(&pAdapter->TxCompleteList, &pAdapter->TxLock)
       )
   );
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> nBusyRCtrl = %d F%d P%d C%d\n", pAdapter->PortName,
        pAdapter->nBusyRCtrl,
        listDepth(&pAdapter->CtrlReadFreeList, &pAdapter->CtrlReadLock),
        listDepth(&pAdapter->CtrlReadPendingList, &pAdapter->CtrlReadLock),
        listDepth(&pAdapter->CtrlReadCompleteList, &pAdapter->CtrlReadLock)
      )
   );
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
      ("<%s> nBusyOID = %d F%d P%d W%d A%d\n", pAdapter->PortName,
        pAdapter->nBusyOID,
        listDepth(&pAdapter->OIDFreeList, &pAdapter->OIDLock),
        listDepth(&pAdapter->OIDPendingList, &pAdapter->OIDLock),
        listDepth(&pAdapter->OIDWaitingList, &pAdapter->OIDLock),
        listDepth(&pAdapter->OIDAsyncList, &pAdapter->OIDLock)
      )
   );
}

VOID MPMAIN_MiniportUnload(PDRIVER_OBJECT  DriverObject)
{
   USBMAIN_DriverUnload(DriverObject);
   
   // do this before freeing GlobalData
   USBIF_CleanupFdoMap(NULL);

   ASSERT(IsListEmpty(&GlobalData.AdapterList));
   NdisFreeSpinLock(&GlobalData.Lock);

   if (MP_CtlLock != NULL)
   {
      NdisFreeSpinLock(MP_CtlLock);
      NdisFreeMemory(MP_CtlLock, sizeof(NDIS_SPIN_LOCK), 0);
      MP_CtlLock = NULL;
   }

   MPMAIN_DeregisterMiniport();

#ifdef EVENT_TRACING
   // Cleanup 
   WPP_CLEANUP(DriverObject);
#endif   

}  // MPMAIN_MiniportUnload

VOID MPMAIN_MiniportShutdown(NDIS_HANDLE MiniportAdapterContext)
{
   MPMAIN_MiniportShutdownEx(MiniportAdapterContext, 0);
}

VOID MPMAIN_MiniportShutdownEx(NDIS_HANDLE MiniportAdapterContext, NDIS_SHUTDOWN_ACTION ShutdownAction)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER) MiniportAdapterContext;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> MPMAIN_MiniportShutdownEx: action 0x%x\n", pAdapter->PortName, ShutdownAction)
   );

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- MPMAIN_MiniportShutdownEx\n", pAdapter->PortName)
   );
}  // MPMAIN_MiniportShutdownEx

BOOLEAN MPMAIN_MiniportCheckForHang(NDIS_HANDLE MiniportAdapterContext)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)MiniportAdapterContext;

   KeSetEvent(&pAdapter->MPThreadIODevActivityEvent, IO_NO_INCREMENT, FALSE);

   return(FALSE);
}

int listDepth(PLIST_ENTRY listHead, PNDIS_SPIN_LOCK lock)
{
   int depth;
   PLIST_ENTRY    thisEntry;

   NdisAcquireSpinLock(lock);

   thisEntry = listHead->Flink;

   depth = 0;
   while ( thisEntry != listHead )
   {
      depth++;
      thisEntry = thisEntry->Flink;
   }

   NdisReleaseSpinLock(lock);

   return depth;
}

BOOLEAN MPWork_ScheduleWorkItem( PMP_ADAPTER pAdapter )
{

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_TRACE,
      ("<%s> MPWork_ScheduleWorkItem\n", pAdapter->PortName)
   );

   KeSetEvent(&pAdapter->WorkThreadProcessEvent, IO_NO_INCREMENT, FALSE);

   return TRUE;
}

VOID MPMAIN_MiniportPnPEventNotify
(
   NDIS_HANDLE             MiniportAdapterContext,
   NDIS_DEVICE_PNP_EVENT   PnPEvent,
   PVOID                   InformationBuffer,
   ULONG                   InformationBufferLength
)
{
   PMP_ADAPTER     pAdapter = (PMP_ADAPTER)MiniportAdapterContext;

   //
   // Turn off the warings.
   //
   // UNREFERENCED_PARAMETER(pAdapter);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> MPMAIN_MiniportPnPEventNotify\n", pAdapter->PortName)
   );

   switch ( PnPEvent )
      {
      case NdisDevicePnPEventQueryRemoved:
         // IRP_MN_QUERY_REMOVE_DEVICE.
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPMAIN_MiniportPnPEventNotify: NdisDevicePnPEventQueryRemoved\n", pAdapter->PortName)
         );
         break;

      case NdisDevicePnPEventRemoved:
         // IRP_MN_REMOVE_DEVICE.
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPMAIN_MiniportPnPEventNotify: NdisDevicePnPEventRemoved\n", pAdapter->PortName)
         );
         break;       

      case NdisDevicePnPEventSurpriseRemoved:
         // IRP_MN_SURPRISE_REMOVAL. 
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPMAIN_MiniportPnPEventNotify: NdisDevicePnPEventSurpriseRemoved\n", pAdapter->PortName)
         );
         pAdapter->Flags &= ~(fMP_STATE_INITIALIZED);
         pAdapter->Flags |= fMP_ADAPTER_SURPRISE_REMOVED;
         MPIOC_SetStopState(pAdapter, FALSE);  // stop all IOCDEV

#ifdef NDIS_WDM
         MPMAIN_MediaDisconnect(pAdapter, TRUE);

         /* Set the device receive event so that any of the MPQMUX waiting for the data comes out */
         KeSetEvent(&pAdapter->DeviceInfoReceivedEvent, IO_NO_INCREMENT, FALSE);   
         
         /* Cancel the work thread */
         MPMAIN_CancelMPThread(pAdapter);

         MPMAIN_PnpEventWithRemoval(pAdapter, PnPEvent, FALSE);
         MPMAIN_ResetOIDWaitingQueue(pAdapter);

         #ifdef NDIS60_MINIPORT
         MPOID_CancelAllPendingOids(pAdapter);
         #endif // NDIS60_MINIPORT
#endif
         // need to cleanup pending TX packets related to a disabled flow,
         // otherwise MPMAIN_MiniportHalt will not be called
         #ifdef MP_QCQOS_ENABLED
         MPQOSC_CancelQmiQosClient(pAdapter);
         MPQOS_CancelQosThread(pAdapter);
         #endif // MP_QCQOS_ENABLED

         CleanupTxQueues(pAdapter);

         // Clean up all Flow control queues
#ifdef QCUSB_MUX_PROTOCOL
#ifdef QC_DUAL_IP_FC
         MPMAIN_PurgeFlowControlledPackets(pAdapter, FALSE);
#endif
#endif
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
         MPMAIN_QMAPPurgeFlowControlledPackets(pAdapter, FALSE);
#endif
         break;

      case NdisDevicePnPEventPowerProfileChanged:
      {
         PNDIS_POWER_PROFILE pwrProfile;

         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPMAIN_MiniportPnPEventNotify: NdisDevicePnPEventPowerProfileChanged\n", pAdapter->PortName)
         );

         if (InformationBufferLength != sizeof(NDIS_POWER_PROFILE) )
         {
            break;
         }

         pwrProfile = (PNDIS_POWER_PROFILE)InformationBuffer;

         QCNET_DbgPrint
         (
             MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
             ("<%s> The host system power profile (Battery-0 / AC-1) reported: %d\n", pAdapter->PortName, *pwrProfile)
         );

         break;      
      }

      default:
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<%s> MPMAIN_MiniportPnPEventNotify: unknown PnP event %x \n", pAdapter->PortName, PnPEvent)
         );
         break;         
      }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- MPMAIN_MiniportPnPEventNotify\n", pAdapter->PortName)
   );
}

#ifdef NDIS_WDM

NTSTATUS MPMAIN_PnpCompletionRoutine
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)pContext;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> MPMAIN_PnpCompletionRoutine-IRP\n", pAdapter->PortName)
   );

   KeSetEvent
   (
   &pAdapter->USBPnpCompletionEvent,
   IO_NO_INCREMENT,
   FALSE
   );

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- MPMAIN_PnpCompletionRoutine-IRP\n", pAdapter->PortName)
   );
   return STATUS_MORE_PROCESSING_REQUIRED;
}

BOOLEAN MPMAIN_SetupPnpIrp
(
   PMP_ADAPTER           pAdapter,
   NDIS_DEVICE_PNP_EVENT PnPEvent,
   PIRP                  pIrp
)
{
   PIO_STACK_LOCATION pNextStack = IoGetNextIrpStackLocation(pIrp);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> SetupPnpIrp\n", pAdapter->PortName)
   );

   IoSetCompletionRoutine
   (
      pIrp,
      MPMAIN_PnpCompletionRoutine,
      pAdapter,
      TRUE,
      TRUE,
      TRUE
   );
   pNextStack->MajorFunction = IRP_MJ_PNP;

   switch ( PnPEvent )
      {
      case NdisDevicePnPEventQueryRemoved:
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPMAIN_MiniportPnPEventNotify-IRP: NdisDevicePnPEventQueryRemoved\n", pAdapter->PortName)
         );
         pNextStack->MinorFunction = IRP_MN_QUERY_REMOVE_DEVICE;
         break;

      case NdisDevicePnPEventRemoved:
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPMAIN_MiniportPnPEventNotify-IRP: NdisDevicePnPEventRemoved\n", pAdapter->PortName)
         );
         pNextStack->MinorFunction = IRP_MN_REMOVE_DEVICE;
         break;

      case NdisDevicePnPEventSurpriseRemoved:
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPMAIN_MiniportPnPEventNotify-IRP: NdisDevicePnPEventSurpriseRemove\n", pAdapter->PortName)
         );
         pNextStack->MinorFunction = IRP_MN_SURPRISE_REMOVAL;
         break;

      case NdisDevicePnPEventQueryStopped:
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPMAIN_MiniportPnPEventNotify-IRP: NdisDevicePnPEventQueryStopped\n", pAdapter->PortName)
         );
         pNextStack->MinorFunction = IRP_MN_QUERY_STOP_DEVICE;
         break;

      case NdisDevicePnPEventStopped:
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPMAIN_MiniportPnPEventNotify-IRP: NdisDevicePnPEventStopped\n", pAdapter->PortName)
         );
         pNextStack->MinorFunction = IRP_MN_STOP_DEVICE;
         break;

      default:
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<%s> MPMAIN_MiniportPnPEventNotify-IRP: unknown PnP event %x \n", pAdapter->PortName, PnPEvent)
         );
         pNextStack->MinorFunction = 0xFF;
         return FALSE;
      }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- SetupPnpIrp\n", pAdapter->PortName)
   );
   return TRUE;
}

// Initialize the global variables for USB device object.
// This function is naturally replaced if the USB component becomes
// a standalone bus enumerator.
VOID MPMAIN_InitUsbGlobal(void)
{
   bAddNewDevice    = FALSE;
   gVendorConfig.DriverResident = 0;

   KeInitializeEvent(&gSyncEntryEvent, SynchronizationEvent, TRUE);
   KeSetEvent(&gSyncEntryEvent,IO_NO_INCREMENT,FALSE);

#ifdef QCUSB_SHARE_INTERRUPT
   USBSHR_Initialize();
#endif // QCUSB_SHARE_INTERRUPT

   RtlZeroMemory(gDeviceName, 255);
   RtlZeroMemory((PVOID)(&gVendorConfig), sizeof(QCUSB_VENDOR_CONFIG));
   strcpy(gDeviceName, "qcusbnet");
}

/* -----------------------------------------------------
 * FUNCTION: MPMAIN_PnpEventWithRemoval
 * 
 *    This function processes a PNP event with the option
 *    of removing the USB device object.
 *
 * ARGUMENTS:
 *    pAdapter: the miniport context
 *    PnPEvent: the PnP event to be processed
 *    bRemove:  if TRUE, the function will remove the USB
 *              device object after processing the PnPEvent.
 *
 * COMMENTS:
 *    This function maintains the removal state of the USB
 *    device object. If the USB device object is removed, this
 *    function will do nothing. This function assumes that
 *    the removal event of the USB device object does not happen
 *    alone, so it does nothing if the PnPEvent is the removal'
 *    event.
 * ------------------------------------------------------*/
NTSTATUS MPMAIN_PnpEventWithRemoval
(
   PMP_ADAPTER           pAdapter,
   NDIS_DEVICE_PNP_EVENT PnPEvent,
   BOOLEAN               bRemove
)
{
   PIRP pIrp = NULL;
   PIO_STACK_LOCATION pNextStack = NULL;
   LARGE_INTEGER delayValue;
   NTSTATUS nts = STATUS_SUCCESS;
   int concurrentCalls;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> MPMAIN_PnpEventWithRemoval\n", pAdapter->PortName)
   );

   // To make the function reentrant
   while ((concurrentCalls = InterlockedIncrement(&pAdapter->RemovalInProgress)) > 1)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> _PnpEventWithRemoval: concurrent calls %d, wait\n", pAdapter->PortName, concurrentCalls)
      );
      InterlockedDecrement(&pAdapter->RemovalInProgress);
      MPMAIN_Wait(-(5 * 1000 * 1000));  // 500ms
   }

   // Don't process NdisDevicePnPEventRemoved alone!!!
   NdisAcquireSpinLock(&pAdapter->UsbLock);
   if ( (pAdapter->UsbRemoved == TRUE)          ||
        (PnPEvent == NdisDevicePnPEventRemoved) ||
        (pAdapter->USBDo == NULL) )
   {
      nts = STATUS_UNSUCCESSFUL;
      NdisReleaseSpinLock(&pAdapter->UsbLock);
      goto Exit;
   }
   NdisReleaseSpinLock(&pAdapter->UsbLock);

   pIrp = IoAllocateIrp
          (
             // (CCHAR)(pAdapter->NextDeviceObject->StackSize+2),
             (CCHAR)(pAdapter->USBDo->StackSize+2),
             FALSE
          );
   if ( pIrp == NULL )
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> IRP creation failure!\n", pAdapter->PortName)
      );
      nts = STATUS_NO_MEMORY;
      goto Exit;
   }
   else
   {
      IoReuseIrp(pIrp, STATUS_NOT_SUPPORTED);
      if ( MPMAIN_SetupPnpIrp(pAdapter, PnPEvent, pIrp) == TRUE )
      {
         NTSTATUS ntStatus;

         KeClearEvent(&pAdapter->USBPnpCompletionEvent);
         ntStatus = QCIoCallDriver(pAdapter->USBDo, pIrp);
         delayValue.QuadPart = -(20 * 1000 * 1000); // 2.0 sec

         if (ntStatus == STATUS_PENDING)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> Wait for USB completion...\n", pAdapter->PortName)
            );
            KeWaitForSingleObject
            (
               &pAdapter->USBPnpCompletionEvent,
               Executive,
               KernelMode,
               FALSE,
               NULL    //  &delayValue
            );
            KeClearEvent(&pAdapter->USBPnpCompletionEvent);
         }
         else
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> QcIoCallDriver-0 returned 0x%x \n", pAdapter->PortName, ntStatus)
            );
         }

         NdisAcquireSpinLock(&pAdapter->UsbLock);

         if ( (bRemove == TRUE) && (pAdapter->UsbRemoved == FALSE) )
         {
            IoReuseIrp(pIrp, STATUS_NOT_SUPPORTED);
            if ( MPMAIN_SetupPnpIrp(pAdapter, NdisDevicePnPEventRemoved, pIrp) == TRUE )
            {
               pAdapter->UsbRemoved = TRUE;
               NdisReleaseSpinLock(&pAdapter->UsbLock);

               KeClearEvent(&pAdapter->USBPnpCompletionEvent);
               ntStatus = QCIoCallDriver(pAdapter->USBDo, pIrp);

               if (ntStatus == STATUS_PENDING)
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                     ("<%s> Wait for USB completion 2...\n", pAdapter->PortName)
                  );
                  delayValue.QuadPart = -(20 * 1000 * 1000); // 2.0 sec
                  KeWaitForSingleObject
                  (
                     &pAdapter->USBPnpCompletionEvent,
                     Executive,
                     KernelMode,
                     FALSE,
                     NULL  // &delayValue
                  );
                  KeClearEvent(&pAdapter->USBPnpCompletionEvent);
               }
               else
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                     ("<%s> QcIoCallDriver-1 returned 0x%x \n", pAdapter->PortName, ntStatus)
                  );
               }
            }
            else
            {
               NdisReleaseSpinLock(&pAdapter->UsbLock);
            }
         }
         else
         {
            NdisReleaseSpinLock(&pAdapter->UsbLock);
         }
      }
      else
      {
         nts = STATUS_UNSUCCESSFUL;
         goto Exit;
      }
   }

   Exit:

   if ( pIrp != NULL )
   {
      IoFreeIrp(pIrp);
   }

   cleanupOIDWaitingQueue( pAdapter );

   InterlockedDecrement(&pAdapter->RemovalInProgress);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- MPMAIN_PnpEventWithRemoval\n", pAdapter->PortName)
   );
   return nts;
} // MPMAIN_PnpEventWithRemoval 

VOID MPMAIN_ResetOIDWaitingQueue(PMP_ADAPTER pAdapter)
{
   NDIS_STATUS OIDStatus = NDIS_STATUS_REQUEST_ABORTED; // NDIS_STATUS_NOT_ACCEPTED;
   PMP_OID_WRITE pWaitingOid;
   PLIST_ENTRY pList;

   // We fail the OIDWaitingList only. The OIDPendingList should get processed
   // when the USB layer gets cancelled.

   NdisAcquireSpinLock( &pAdapter->OIDLock );
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> -->ResetOIDWaitingQueue, nBusyOID = %d\n", pAdapter->PortName, pAdapter->nBusyOID)
   );

   if (pAdapter->nBusyOID > 0)
   {
      while ( !IsListEmpty( &pAdapter->OIDWaitingList ) )
      {
         pList = RemoveHeadList(&pAdapter->OIDWaitingList);
         pWaitingOid = CONTAINING_RECORD(pList, MP_OID_WRITE, List);

         pWaitingOid->RequestBytesUsed = (PULONG) NULL;
         pWaitingOid->RequestBytesNeeded = (PULONG) NULL;
         // #ifdef NDIS620_MINIPORT_QCOM
         MPQMI_FreeQMIRequestQueue(pWaitingOid);
         // #endif

         if( pWaitingOid->OID.OidType == fMP_QUERY_OID )
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> Failed Query Complete (Oid: 0x%x)\n", pAdapter->PortName, pWaitingOid->OID.Oid)
            );
            MPOID_CompleteOid(pAdapter, OIDStatus, pWaitingOid, pWaitingOid->OID.OidType, TRUE);
         }
         else
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> Failed Set Complete (Oid: 0x%x)\n", pAdapter->PortName, pWaitingOid->OID.Oid)
            );
            MPOID_CompleteOid(pAdapter, OIDStatus, pWaitingOid, pWaitingOid->OID.OidType, TRUE);
         }
         MPOID_CleanupOidCopy(pAdapter, pWaitingOid);
         InsertTailList(&pAdapter->OIDFreeList, pList);
         InterlockedDecrement(&(pAdapter->nBusyOID));
      }
      while ( !IsListEmpty( &pAdapter->OIDPendingList ) )
      {
         pList = RemoveHeadList(&pAdapter->OIDPendingList);
         pWaitingOid = CONTAINING_RECORD(pList, MP_OID_WRITE, List);

         pWaitingOid->RequestBytesUsed = (PULONG) NULL;
         pWaitingOid->RequestBytesNeeded = (PULONG) NULL;
         // #ifdef NDIS620_MINIPORT_QCOM
         MPQMI_FreeQMIRequestQueue(pWaitingOid);
         // #endif

         if( pWaitingOid->OID.OidType == fMP_QUERY_OID )
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> Failed Query Complete (Oid: 0x%x)\n", pAdapter->PortName, pWaitingOid->OID.Oid)
            );
            MPOID_CompleteOid(pAdapter, OIDStatus, pWaitingOid, pWaitingOid->OID.OidType, TRUE);
         }
         else
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> Failed Set Complete (Oid: 0x%x)\n", pAdapter->PortName, pWaitingOid->OID.Oid)
            );
            MPOID_CompleteOid(pAdapter, OIDStatus, pWaitingOid, pWaitingOid->OID.OidType, TRUE);
         }
         MPOID_CleanupOidCopy(pAdapter, pWaitingOid);
         InsertTailList(&pAdapter->OIDFreeList, pList);
         InterlockedDecrement(&(pAdapter->nBusyOID));
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--ResetOIDWaitingQueue, nBusyOID = %d\n", pAdapter->PortName, pAdapter->nBusyOID)
   );
   NdisReleaseSpinLock( &pAdapter->OIDLock );
}  // MPMAIN_ResetOIDWaitingQueue

#endif // NDIS_WDM

// #endif

NDIS_STATUS MPMAIN_SetDeviceId(PMP_ADAPTER pAdapter, BOOLEAN state)
{
   NDIS_STATUS ndisStatus = NDIS_STATUS_RESOURCES;
   SHORT       i;

   // Get device instance
   if (state == TRUE)
   {
      NTSTATUS nts;

      nts = MPMAIN_GetDeviceInstance(pAdapter);

      if (nts == STATUS_SUCCESS)
      {
         PUSHORT psv;

         if (pAdapter->NetworkAddressReadOk == FALSE)
         {
            pAdapter->PermanentAddress[0] = QUALCOMM_MAC_0;
            pAdapter->PermanentAddress[1] = QUALCOMM_MAC_1;
            pAdapter->PermanentAddress[2] = QUALCOMM_MAC_2;
            pAdapter->PermanentAddress[3] = QUALCOMM_MAC_3_HOST_BYTE;
            psv = (PUSHORT)&(pAdapter->PermanentAddress[4]);
            *psv = ntohs(pAdapter->DeviceInstance);

            ETH_COPY_NETWORK_ADDRESS(pAdapter->CurrentAddress, pAdapter->PermanentAddress);

            QCNET_DbgPrintG(("<%s> MAC Addr set to: %02x-%02x-%02x-%02x-%02x-%02x\n",
                  pAdapter->PortName,
                  pAdapter->CurrentAddress[0], pAdapter->CurrentAddress[1],
                  pAdapter->CurrentAddress[2], pAdapter->CurrentAddress[3],
                  pAdapter->CurrentAddress[4], pAdapter->CurrentAddress[5]));
         }

         #ifdef QC_IP_MODE

         pAdapter->MacAddress2[0] = QUALCOMM_MAC2_0;
         pAdapter->MacAddress2[1] = QUALCOMM_MAC2_1;
         pAdapter->MacAddress2[2] = QUALCOMM_MAC2_2;
         pAdapter->MacAddress2[3] = QUALCOMM_MAC2_3_HOST_BYTE;
         psv = (PUSHORT)&(pAdapter->MacAddress2[4]);
         *psv = ntohs(pAdapter->DeviceInstance);

         QCNET_DbgPrintG(("<%s> MAC Addr2 set to: %02x-%02x-%02x-%02x-%02x-%02x\n",
               pAdapter->PortName,
               pAdapter->MacAddress2[0], pAdapter->MacAddress2[1],
               pAdapter->MacAddress2[2], pAdapter->MacAddress2[3],
               pAdapter->MacAddress2[4], pAdapter->MacAddress2[5]));

         #endif // QC_IP_MODE

         return NDIS_STATUS_SUCCESS;
      }
   }

   NdisAcquireSpinLock(&GlobalData.Lock);

   if (state == FALSE)  // reset 
   {
      if (pAdapter->DeviceId <= MP_DEVICE_ID_MAX)
      {
         GlobalData.DeviceId[pAdapter->DeviceId] = 0;
         pAdapter->DeviceId = MP_DEVICE_ID_NONE;
         ndisStatus = NDIS_STATUS_SUCCESS;
      }

      #ifdef QC_IP_MODE
      if (pAdapter->DeviceId2 <= MP_DEVICE_ID_MAX)
      {
         GlobalData.DeviceId[pAdapter->DeviceId2] = 0;
         pAdapter->DeviceId2 = MP_DEVICE_ID_NONE;
      }
      #endif // QC_IP_MODE
   }
   else                // set
   {
      for (i = 0; i <= MP_DEVICE_ID_MAX; i++)
      {
         if (GlobalData.DeviceId[i] == 0)
         {
            #ifdef QC_IP_MODE
            // if ->DeviceId has been set
            if (ndisStatus == NDIS_STATUS_SUCCESS)
            {
               GlobalData.DeviceId[i] = 1;
               pAdapter->DeviceId2 = i;
               break;
            }
            else
            #endif // QC_IP_MODE
            {
               GlobalData.DeviceId[i] = 1;
               pAdapter->DeviceId = i;
               ndisStatus = NDIS_STATUS_SUCCESS;
               #ifndef QC_IP_MODE
               break;
               #endif // QC_IP_MODE
            }
         }
      }

   }

   NdisReleaseSpinLock(&GlobalData.Lock);

   #ifdef QC_IP_MODE
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> IPO: _SetDeviceId: (%d, %d)\n", pAdapter->PortName,
        pAdapter->DeviceId, pAdapter->DeviceId2)
   );
   #else
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> _SetDeviceId: (%d)\n", pAdapter->PortName,
        pAdapter->DeviceId)
   );
   #endif // QC_IP_MODE
   return ndisStatus;
}  // MPMAIN_SetDeviceId

NDIS_STATUS MPMAIN_SetDeviceDataFormat(PMP_ADAPTER pAdapter)
{
   NDIS_STATUS   ndisStatus;
   LARGE_INTEGER timeoutValue;
   PKEVENT       pClientIdReceivedEvent;
   UCHAR         currentClientId;
   BOOLEAN       sendAgain = FALSE;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> MPMAIN_SetDeviceDataFormat\n", pAdapter->PortName)
   );

   ndisStatus = MPQCTL_GetClientId(pAdapter, QMUX_TYPE_WDS_ADMIN, pAdapter);

   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      return ndisStatus;
   }

   MPQMUX_SetDeviceDataFormat(pAdapter);

#if defined(QCMP_QMAP_V1_SUPPORT)
if ((pAdapter->MPEnableQMAPV4 != 0) && (pAdapter->QMAPEnabledV4 == 0) &&
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
    ((pAdapter->MPEnableQMAPV3 != 0) || (pAdapter->MPEnableQMAPV1 != 0))
#endif
#else
      (pAdapter->MPEnableQMAPV1 != 0)
#endif
      )
   {
       pAdapter->MPEnableQMAPV4 = 0;
       MPQMUX_SetDeviceDataFormat(pAdapter);
   }
   if (pAdapter->QMAPEnabledV4 != 0)
   {
      goto exit0;
   }
#endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   if ((pAdapter->MPEnableQMAPV3 != 0) && (pAdapter->QMAPEnabledV3 == 0) &&
          (pAdapter->MPEnableQMAPV2 != 0))
   {
       pAdapter->MPEnableQMAPV3 = 0;
       MPQMUX_SetDeviceDataFormat(pAdapter);
   }
   if (pAdapter->QMAPEnabledV3 != 0)
   {
      goto exit0;
   }

   if ((pAdapter->MPEnableQMAPV2 != 0) && (pAdapter->QMAPEnabledV2 == 0) &&
          (pAdapter->MPEnableQMAPV1 != 0))
   {
       pAdapter->MPEnableQMAPV2 = 0;
       MPQMUX_SetDeviceDataFormat(pAdapter);
   }
   if (pAdapter->QMAPEnabledV2 != 0)
   {
      goto exit0;
   }
#endif        
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
   if ((pAdapter->MPEnableQMAPV1 != 0) && (pAdapter->QMAPEnabledV1 == 0) &&
          (pAdapter->MPEnableMBIMUL != 0))
   {
      pAdapter->MPEnableQMAPV1 = 0;
      MPQMUX_SetDeviceDataFormat(pAdapter);
   }
   if (pAdapter->QMAPEnabledV1 != 0)
   {
      goto exit0;
   }
#endif                              

#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_MBIM_DL_SUPPORT)
   if ( (pAdapter->MPEnableMBIMUL == 0) && (pAdapter->MPEnableMBIMDL == 0) )
   {
      sendAgain = TRUE;
   }
#endif

#if defined(QCMP_UL_TLP)
   if ((pAdapter->MPEnableMBIMUL != 0) && (pAdapter->MBIMULEnabled == 0) &&
          (pAdapter->MPEnableTLP != 0) )
   {
      pAdapter->MPEnableMBIMUL = 0;
      sendAgain = TRUE;
   }
#endif
   
#if defined(QCMP_DL_TLP)
    if ((pAdapter->MPEnableMBIMDL != 0) && (pAdapter->MBIMDLEnabled == 0) &&
        (pAdapter->MPEnableDLTLP != 0) )
    {
       pAdapter->MPEnableMBIMDL = 0;
       sendAgain = TRUE;
    }
#endif

   if (sendAgain == TRUE)
   {
      MPQMUX_SetDeviceDataFormat(pAdapter);   
   }

exit0:

   if ((pAdapter->QMAPEnabledV1 == TRUE)
       || (pAdapter->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
      || (pAdapter->QMAPEnabledV2 == TRUE)
      || (pAdapter->QMAPEnabledV3 == TRUE)
#endif
#endif                         
   )
   {
      if (pAdapter->DisableQMAPFC == 0)
      {
         MPQMUX_SetDeviceQMAPSettings(pAdapter);
      }
   }
   
   MPQCTL_ReleaseClientId
   (
      pAdapter,
      pAdapter->QMIType,
      (PVOID)pAdapter   // Context
   );

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- MPMAIN_SetDeviceDataFormat\n", pAdapter->PortName)
   );

   return ndisStatus;
}  // MPMAIN_SetDeviceDataFormat

NDIS_STATUS MPMAIN_GetDeviceDescription(PMP_ADAPTER pAdapter)
{
   NDIS_STATUS   ndisStatus = NDIS_STATUS_SUCCESS;
   LARGE_INTEGER timeoutValue;
   PKEVENT       pClientIdReceivedEvent;
   UCHAR         currentClientId;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> MPMAIN_GetDeviceDescription\n", pAdapter->PortName)
   );

   if (pAdapter->ClientId[QMUX_TYPE_DMS] == 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> <-- MPMAIN_GetDeviceDescription: no DMS client\n", pAdapter->PortName)
      );
      return NDIS_STATUS_FAILURE;
   }

   MPQMUX_GetDeviceInfo(pAdapter);

   if (pAdapter->DeviceManufacturer != NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_GetDeviceDescription: MFR<%s>\n",
           pAdapter->PortName, pAdapter->DeviceManufacturer));
   }
   if (pAdapter->DeviceMSISDN != NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_GetDeviceDescription: Voice#<%s>\n",
           pAdapter->PortName, pAdapter->DeviceMSISDN)
      );
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- MPMAIN_GetDeviceDescription\n", pAdapter->PortName)
   );

   MPQMUX_FillDeviceWMIInformation(pAdapter);

   return ndisStatus;
}  // MPMAIN_GetDeviceDescription

VOID MPMAIN_DevicePowerStateChange
(
   PMP_ADAPTER             pAdapter,
   NDIS_DEVICE_POWER_STATE PowerState
)
{
   NDIS_STATUS ns;

   // This function is likely called at IRQL_DISPATCH_LEVEL

   if (PowerState >= NdisDeviceStateD3)
   {
      SYSTEM_POWER_STATE sysPwr;

      // sysPwr = USBIF_GetSystemPowerState(pAdapter->USBDo);
      sysPwr = pAdapter->MiniportSystemPower;
      if (sysPwr > PowerSystemWorking)
      {
         if (pAdapter->ulMediaConnectStatus != NdisMediaStateDisconnected)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> _DevicePowerStateChange: ConnStat - MEDIA_DISCONNECT\n",
                 pAdapter->PortName)
            );

            // Reset Timer Resolution here...
            //MPMAIN_MediaDisconnect(pAdapter, TRUE);
            KeSetEvent(&pAdapter->MPThreadMediaDisconnectEvent, IO_NO_INCREMENT, FALSE);
            
            #ifdef MP_QCQOS_ENABLED
            MPQOS_PurgeQueues(pAdapter);
            #endif // MP_QCQOS_ENABLED
            MPMAIN_ResetPacketCount(pAdapter);
            //USBIF_CancelWriteThread(pAdapter->USBDo);
         }
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> _DevicePowerStateChange: in S0, keep ConnStat\n",
              pAdapter->PortName)
         );
      }
   }

   if (PowerState == NdisDeviceStateD3)
   {
      // Release internal client id
      // But we do not need to call MPQCTL_ReleaseClientId because
      // the device treats this as cable disconnection and thus
      // cleans itself up. All we need to do is to do some reset on
      // the driver side.
      // pAdapter->QMIType  = 0;
      // pAdapter->ClientId = 0;
      RtlZeroMemory((PVOID)pAdapter->ClientId, (QMUX_TYPE_MAX+1));

      // reset QMI initialization
      pAdapter->QmiInitialized = FALSE;
      pAdapter->IsQmiWorking = FALSE;

      // Check to see if we have to stop the WDS IP thread here
      MPIP_CancelWdsIpClient(pAdapter);
   }
   else if ((PowerState == NdisDeviceStateD0) &&
            (pAdapter->PreviousDevicePowerState >= NdisDeviceStateD3))
   {
      KeSetEvent(&pAdapter->MPThreadInitQmiEvent, IO_NO_INCREMENT, FALSE);
   }
   pAdapter->PreviousDevicePowerState = PowerState;
}  // MPMAIN_DevicePowerStateChange

BOOLEAN MPMAIN_InitializeQMI(PMP_ADAPTER pAdapter, INT QmiRetries)
{
   BOOLEAN retVal = TRUE;
   KIRQL irql = KeGetCurrentIrql();
   LONG numInits;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> --> InitializeQMI: retry %d\n", pAdapter->PortName, QmiRetries)
   );

#ifdef QCMP_DISABLE_QMI
   if (pAdapter->DisableQMI == 0)
   {
#endif   
   if ((numInits = InterlockedIncrement(&pAdapter->QmiInInitialization)) > 1)
   {
      int i = 0;
      BOOLEAN bContinueToInit = FALSE;

      if (numInits > 2)  // limit the number of calls
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> <-- InitializeQMI: redundant init %d\n", pAdapter->PortName, QmiRetries)
         );
         InterlockedDecrement(&pAdapter->QmiInInitialization);
         return retVal;
      }

      // Initialized or timed out?
      while ((pAdapter->ClientId[QMUX_TYPE_WDS] == 0) && (i++ < 400))
      {
          if ((pAdapter->Flags & fMP_ANY_FLAGS) == 0)
          {
             QCNET_DbgPrint
             (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> QMI_Init in progress - %d\n", pAdapter->PortName, i)
             );
             MPMAIN_Wait(-(3 * 1000 * 1000));  // 300ms
             if (pAdapter->QmiInInitialization == 1)  // other party finished init
             {
                if (pAdapter->ClientId[QMUX_TYPE_WDS] == 0)
                {
                   bContinueToInit = TRUE;
                   break;  // out of loop
                }
             } 
          }
          else
          {
             QCNET_DbgPrint
             (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> QMI_Init abort: %d\n", pAdapter->PortName, i)
             );
             break;
          }
      }
      
      if (bContinueToInit == FALSE)
      {
         InterlockedDecrement(&pAdapter->QmiInInitialization);
         return (pAdapter->ClientId[QMUX_TYPE_WDS] != 0);
      }
   }


   pAdapter->QmiInitRetries = QmiRetries;

   #ifdef QCQMI_SUPPORT
   if (pAdapter->ClientId[QMUX_TYPE_WDS] == 0)  // test WDS is sufficient
   {
      NDIS_STATUS status;
      PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;

      if (irql == PASSIVE_LEVEL)
      {
         status = MPQCTL_GetQMICTLVersion(pAdapter, TRUE);
         if (status == NDIS_STATUS_SUCCESS)
         {
            // Is QMI working
            pAdapter->IsQmiWorking = TRUE;

            // Set the data format using WDS_ADMIN if present
            if (pAdapter->IsWdsAdminPresent == TRUE)
            {
                if ((pDevExt->MuxInterface.MuxEnabled == 0) ||
                    (pDevExt->MuxInterface.InterfaceNumber == pDevExt->MuxInterface.PhysicalInterfaceNumber))
               {
#if 1            
               MPMAIN_SetDeviceDataFormat(pAdapter);
#else
               pAdapter->QosEnabled = FALSE;
               pAdapter->IPModeEnabled = TRUE;
               pAdapter->QMAPEnabledV2 = TRUE;
               pAdapter->MPQuickTx = 1;                           
#endif               
               USBIF_SetAggregationMode(pAdapter->USBDo);
                   KeSetEvent(&pAdapter->MainAdapterSetDataFormatSuccessful, IO_NO_INCREMENT, FALSE);

                }
                else
                {
                     NTSTATUS nts;
                     PMP_ADAPTER returnAdapter = NULL;
                     
                     DisconnectedAllAdapters(pAdapter, &returnAdapter);
                     // wait for signal
                     nts = KeWaitForSingleObject
                           (
                              &returnAdapter->MainAdapterSetDataFormatSuccessful,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL
                           );
#ifdef MP_QCQOS_ENABLED    
                     pAdapter->QosEnabled = returnAdapter->QosEnabled;
#endif
#ifdef QC_IP_MODE
                     pAdapter->IPModeEnabled = returnAdapter->IPModeEnabled;
#endif
#if defined(QCMP_QMAP_V1_SUPPORT)
                     pAdapter->QMAPEnabledV4 = returnAdapter->QMAPEnabledV4;
#endif
#ifdef QCUSB_MUX_PROTOCOL                        
#if defined(QCMP_QMAP_V2_SUPPORT)
                     pAdapter->QMAPEnabledV3 = returnAdapter->QMAPEnabledV3;
                     pAdapter->QMAPEnabledV2 = returnAdapter->QMAPEnabledV2;
#endif
#endif
#if defined(QCMP_QMAP_V1_SUPPORT)
                     pAdapter->QMAPEnabledV1 = returnAdapter->QMAPEnabledV1;
#endif
#if defined(QCMP_UL_TLP)
                     pAdapter->TLPEnabled = returnAdapter->TLPEnabled;
#endif
#if defined(QCMP_MBIM_UL_SUPPORT)
                     pAdapter->MBIMULEnabled = returnAdapter->MBIMULEnabled;
#endif
                     pAdapter->MPQuickTx = returnAdapter->MPQuickTx;

#if defined(QCMP_QMAP_V1_SUPPORT)
                     pAdapter->QMAPEnabledV4 = returnAdapter->QMAPEnabledV4;
#endif
#ifdef QCUSB_MUX_PROTOCOL                        
#if defined(QCMP_QMAP_V2_SUPPORT)
                     pAdapter->QMAPEnabledV3 = returnAdapter->QMAPEnabledV3;
                     pAdapter->QMAPEnabledV2 = returnAdapter->QMAPEnabledV2;
#endif
#endif
#if defined(QCMP_QMAP_V1_SUPPORT)
                     pAdapter->QMAPEnabledV1 = returnAdapter->QMAPEnabledV1;
#endif
#if defined(QCMP_MBIM_DL_SUPPORT)
                     pAdapter->MBIMDLEnabled = returnAdapter->MBIMDLEnabled;
#endif
#if defined(QCMP_DL_TLP)
                     pAdapter->TLPDLEnabled = returnAdapter->TLPDLEnabled;
#endif
#if defined(QCMP_MBIM_UL_SUPPORT)
                     pAdapter->ndpSignature = returnAdapter->ndpSignature;
                     pAdapter->MaxTLPPackets = returnAdapter->MaxTLPPackets;
                     pAdapter->UplinkTLPSize = returnAdapter->UplinkTLPSize;
                     pAdapter->QMAPDLMinPadding = returnAdapter->QMAPDLMinPadding;             
#endif
                     USBIF_SetAggregationMode(pAdapter->USBDo);
                  }                
            }
            

            status = MPMAIN_OpenQMIClients(pAdapter);

            // Get device description from DMS temp DMS service
            MPMAIN_GetDeviceDescription(pAdapter);

            if (status == NDIS_STATUS_SUCCESS)
            {
               // Call QMI_WDS_BIND_MUX_DATA_PORT
               MPQMUX_SetQMUXBindMuxDataPort( pAdapter, pAdapter, QMUX_TYPE_WDS, QMIWDS_BIND_MUX_DATA_PORT_REQ);

               if (QCMP_NDIS620_Ok == FALSE)
               {
                  MPQCTL_SetInstanceId(pAdapter);
               }

               #ifdef MP_QCQOS_ENABLED
               if ((pAdapter->IsQosPresent == TRUE)
                  #ifdef QC_IP_MODE
                   || (pAdapter->IsLinkProtocolSupported == TRUE))
                  #else
                  )
                  #endif // QC_IP_MODE
               {
                  if (pAdapter->IsWdsAdminPresent != TRUE)
                  {
                     NDIS_STATUS ns;
                     ns = MPQCTL_SetDataFormat(pAdapter, pAdapter);
                     USBIF_SetAggregationMode(pAdapter->USBDo);
                  }

                  #ifdef QC_IP_MODE
                  USBIF_SetDataMode(pAdapter->USBDo);
                  #endif // QC_IP_MODE
                  if (pAdapter->QosEnabled == TRUE)
                  {
                     // spawn QOS threads
                     MPQOS_StartQosThread(pAdapter);
                     MPQOSC_StartQmiQosClient(pAdapter);
                  }
               }

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
               if ((pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE))
               {
                  MPUSB_InitializeTLP(pAdapter);
               }
               else
               {
#endif
#endif
#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
               if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)||(pAdapter->QMAPEnabledV4 == TRUE)
                   || (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0))
                   
               {
                  MPUSB_InitializeTLP(pAdapter);
               }
               #endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
               }
#endif
#endif

               #endif // MP_QCQOS_ENABLED

               // Query IPV4
               if (QCMAIN_IsDualIPSupported(pAdapter) == TRUE)
               {
                  MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WDS, 
                                         QMIWDS_SET_CLIENT_IP_FAMILY_PREF_REQ, (CUSTOMQMUX)MPQMUX_SetIPv4ClientIPFamilyPref, TRUE );
               }
               
               // Register event for extended-IP-config
               MPQWDS_SendIndicationRegisterReq(pAdapter, NULL, TRUE);

               MPIP_StartWdsIpClient(pAdapter);

               // See if there is any connection indication
               // Query IPV4/IPV6 tp confirm media still connected
               MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WDS, 
                           QMIWDS_GET_PKT_SRVC_STATUS_REQ, NULL, TRUE );
               if ((QCMAIN_IsDualIPSupported(pAdapter) == TRUE) && (pAdapter->WdsIpClientContext != NULL))
               {
                  MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WDS, 
                                         QMIWDS_GET_PKT_SRVC_STATUS_REQ, (CUSTOMQMUX)MPQMUX_ChkIPv6PktSrvcStatus, TRUE );
               }
            }
            else
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> InitializeQMI: failed to get client Id\n", pAdapter->PortName)
               );
            }
         }
         else if (status == NDIS_STATUS_ADAPTER_REMOVED)
         {
            retVal = FALSE;
         }
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> InitializeQMI: failed to get CID (IRQL too high %u)\n", pAdapter->PortName, irql)
         );
      }
   }

   if (retVal == TRUE)
   {
      pAdapter->QmiInitialized = TRUE;
      MoveQMIRequests(pAdapter);
      USBIF_UpdateSSR(pAdapter->USBDo, 0);
   }
   
   #endif // QCQMI_SUPPORT

   InterlockedDecrement(&pAdapter->QmiInInitialization);
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <-- InitializeQMI: %d(ST %d)\n", pAdapter->PortName, QmiRetries, retVal)
   );
#ifdef QCMP_DISABLE_QMI
   }
   else
   {
   if (pAdapter->QmiInitialized != TRUE)
   {
      pAdapter->QosEnabled = FALSE;
      pAdapter->IPModeEnabled = TRUE; // FALSE;
      pAdapter->MPQuickTx = 0;

#if defined(QCMP_QMAP_V1_SUPPORT)
   if (pAdapter->MPEnableQMAPV4 != 0)
   {
      pAdapter->QMAPEnabledV4 = 1;
      pAdapter->MPQuickTx = 1;
   }
else
#endif    
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   if (pAdapter->MPEnableQMAPV3 != 0)
   {
       pAdapter->QMAPEnabledV3 = 1;
       pAdapter->MPQuickTx = 1;
   }
   else
   if (pAdapter->MPEnableQMAPV2 != 0)
   {
       pAdapter->QMAPEnabledV2 = 1;
       pAdapter->MPQuickTx = 1;
   }
   else
#endif        
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
   if (pAdapter->MPEnableQMAPV1 != 0)
   {
      pAdapter->QMAPEnabledV1 = 1;
      pAdapter->MPQuickTx = 1;
   }
   else
#endif                              

#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_MBIM_DL_SUPPORT)
   if (pAdapter->MPEnableMBIMUL != 0)
   {
      pAdapter->MBIMULEnabled = 1;
      pAdapter->MPQuickTx = 1;
   }
   else
#endif
#if defined(QCMP_UL_TLP)
   if (pAdapter->MPEnableTLP != 0)
   {
      pAdapter->TLPEnabled = 1;
      pAdapter->MPQuickTx = 1;
   }
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
   if (pAdapter->MPEnableQMAPV4 != 0)
   {
      pAdapter->QMAPEnabledV4 = 1;
      pAdapter->MPQuickTx = 1;
   }
else
#endif    
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   if (pAdapter->MPEnableQMAPV3 != 0)
   {
       pAdapter->QMAPEnabledV3 = 1;
      pAdapter->MPQuickTx = 1;
   }
   else
   if (pAdapter->MPEnableQMAPV2 != 0)
   {
       pAdapter->QMAPEnabledV2 = 1;
      pAdapter->MPQuickTx = 1;
   }
   else
#endif        
#endif
#if defined(QCMP_QMAP_V1_SUPPORT)
   if (pAdapter->MPEnableQMAPV1 != 0)
   {
      pAdapter->QMAPEnabledV1 = 1;
      pAdapter->MPQuickTx = 1;
   }
   else
#endif                              
#if defined(QCMP_MBIM_DL_SUPPORT)
    if (pAdapter->MPEnableMBIMDL != 0)
    {
       pAdapter->MBIMDLEnabled = 1;
       pAdapter->MPQuickTx = 1;
    }
    else
#endif
#if defined(QCMP_DL_TLP)
    if (pAdapter->MPEnableDLTLP != 0)
    {
        pAdapter->TLPDLEnabled = 1;
        pAdapter->MPQuickTx = 1;
    }
#endif

      USBIF_SetAggregationMode(pAdapter->USBDo);

      #ifdef QC_IP_MODE
         USBIF_SetDataMode(pAdapter->USBDo);
      #endif // QC_IP_MODE

      if (pAdapter->QosEnabled == TRUE)
      {
         // spawn QOS threads
         MPQOS_StartQosThread(pAdapter);
         MPQOSC_StartQmiQosClient(pAdapter);
      }
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
      if ((pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE))
      {
         MPUSB_InitializeTLP(pAdapter);
      }
      else
      {
#endif
#endif
#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
      if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| (pAdapter->QMAPEnabledV4 == TRUE)
          || (pAdapter->QMAPEnabledV1 == TRUE)  || (pAdapter->MPQuickTx != 0))
                          
      {
         MPUSB_InitializeTLP(pAdapter);
      }
#endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
      }
#endif
#endif
      // Is QMI working
      pAdapter->IsQmiWorking = TRUE;
      pAdapter->QmiInitialized = TRUE;
      USBIF_UpdateSSR(pAdapter->USBDo, 0);
   }   
   }
#endif /* QCMP_DISABLE_QMI */              
   return retVal;
}  // MPMAIN_InitializeQMI

NTSTATUS MPMAIN_StartMPThread(PMP_ADAPTER pAdapter)
{
   NTSTATUS          ntStatus;
   OBJECT_ATTRIBUTES objAttr;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> StartMPThread - wrong IRQL::%d\n", pAdapter->PortName, KeGetCurrentIrql())
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((pAdapter->pMPThread != NULL) || (pAdapter->hMPThreadHandle != NULL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> MPThread up/resumed\n", pAdapter->PortName)
      );
      // MPMAIN_ResumeThread(pDevExt, 0);
      return STATUS_SUCCESS;
   }

   KeClearEvent(&pAdapter->MPThreadStartedEvent);
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   ntStatus = PsCreateSystemThread
              (
                 OUT &pAdapter->hMPThreadHandle,
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)MPMAIN_MPThread,
                 IN (PVOID)pAdapter
              );
   if ((!NT_SUCCESS(ntStatus)) || (pAdapter->hMPThreadHandle == NULL))
   {
      pAdapter->hMPThreadHandle = NULL;
      pAdapter->pMPThread = NULL;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> MP th failure\n", pAdapter->PortName)
      );
      return ntStatus;
   }
   ntStatus = KeWaitForSingleObject
              (
                 &pAdapter->MPThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );

   ntStatus = ObReferenceObjectByHandle
              (
                 pAdapter->hMPThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&pAdapter->pMPThread,
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> MP: ObReferenceObjectByHandle failed 0x%x\n", pAdapter->PortName, ntStatus)
      );
      pAdapter->pMPThread = NULL;
   }
   else
   {
      if (STATUS_SUCCESS != (ntStatus = ZwClose(pAdapter->hMPThreadHandle)))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MP ZwClose failed - 0x%x\n", pAdapter->PortName, ntStatus)
         );
      }
      else
      {
         pAdapter->hMPThreadHandle = NULL;
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> MP handle=0x%p thOb=0x%p\n", pAdapter->PortName,
       pAdapter->hMPThreadHandle, pAdapter->pMPThread)
   );

   return ntStatus;

}  // MPMAIN_StartMPThread

NTSTATUS MPMAIN_CancelMPThread(PMP_ADAPTER pAdapter)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER delayValue;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> MP Cxl\n", pAdapter->PortName)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> MP Cxl: wrong IRQL\n", pAdapter->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (InterlockedIncrement(&pAdapter->MPMainThreadInCancellation) > 1)
   {
      while ((pAdapter->hMPThreadHandle != NULL) || (pAdapter->pMPThread != NULL))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP cxl in pro\n", pAdapter->PortName)
         );
         MPMAIN_Wait(-(3 * 1000 * 1000));  // 300ms
      }
      InterlockedDecrement(&pAdapter->MPMainThreadInCancellation);
      return STATUS_SUCCESS;
   }

   if ((pAdapter->hMPThreadHandle == NULL) && (pAdapter->pMPThread == NULL))
   {
     QCNET_DbgPrint
     (
        MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
        ("<%s> MP: already cancelled\n", pAdapter->PortName)
     );
     InterlockedDecrement(&pAdapter->MPMainThreadInCancellation);
     return STATUS_SUCCESS;
   }

   if ((pAdapter->hMPThreadHandle != NULL) || (pAdapter->pMPThread != NULL))
   {
      KeClearEvent(&pAdapter->MPThreadClosedEvent);
      KeSetEvent
      (
         &pAdapter->MPThreadCancelEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pAdapter->pMPThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pAdapter->pMPThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pAdapter->pMPThread);
         KeClearEvent(&pAdapter->MPThreadClosedEvent);
         ZwClose(pAdapter->hMPThreadHandle);
         pAdapter->pMPThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pAdapter->MPThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pAdapter->MPThreadClosedEvent);
         ZwClose(pAdapter->hMPThreadHandle);
      }
      pAdapter->hMPThreadHandle = NULL;
   }

   MPWork_CancelWorkThread(pAdapter);
   #ifdef NDIS60_MINIPORT
   if (1)
   {
      INT i;

      for (i = 0; i < pAdapter->RxStreams; i++)
      {
         MPRX_CancelRxThread(pAdapter, i);
      }
   }
   #endif
   InterlockedDecrement(&pAdapter->MPMainThreadInCancellation);

   return ntStatus;
}  // MPMAIN_CancelMPThread

VOID MPMAIN_MPThread(PVOID Context)
{
   PMP_ADAPTER  pAdapter = (PMP_ADAPTER)Context;
   NTSTATUS     ntStatus;
   BOOLEAN      bKeepRunning = TRUE;
   BOOLEAN      bThreadStarted = FALSE;
   PKWAIT_BLOCK pwbArray = NULL;
   LARGE_INTEGER waitInterval, firstCallTime;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> MPth: wrong IRQL\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hMPThreadHandle);
      pAdapter->hMPThreadHandle = NULL;
      KeSetEvent(&pAdapter->MPThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
   }

   // allocate a wait block array for the multiple wait
   pwbArray = ExAllocatePool
              (
                 NonPagedPool,
                 (MP_THREAD_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK)
              );
   if (pwbArray == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> MPth: NO_MEM\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hMPThreadHandle);
      pAdapter->hMPThreadHandle = NULL;
      KeSetEvent(&pAdapter->MPThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   // Spawn the work thread
   ntStatus = MPWork_StartWorkThread(pAdapter);
   if (!NT_SUCCESS(ntStatus))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> MPth: Work failure\n", pAdapter->PortName)
      );
      ZwClose(pAdapter->hMPThreadHandle);
      pAdapter->hMPThreadHandle = NULL;
      KeSetEvent(&pAdapter->MPThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
   }

   #ifdef NDIS60_MINIPORT
   // Start RX thread
   if (1)
   {
      INT i;

      for (i = 0; i < pAdapter->RxStreams; i++)
      {
         MPRX_StartRxThread(pAdapter, i);
      }
   }
   #endif

   KeClearEvent(&pAdapter->MPThreadCancelEvent);
   KeClearEvent(&pAdapter->MPThreadClosedEvent);
   // KeClearEvent(&pAdapter->MPThreadInitQmiEvent);
   KeClearEvent(&pAdapter->MPThreadTxEvent);
   KeClearEvent(&pAdapter->MPThreadTxTimerEvent);

   while (bKeepRunning == TRUE)
   {
      int retries = 0;
      UCHAR numTimeouts = 0;

      if (bThreadStarted == FALSE)
      {
         bThreadStarted = TRUE;
         KeSetEvent(&pAdapter->MPThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      }

      waitInterval.QuadPart = -(5 * 1000 * 1000); // 0.5 sec
      ntStatus = KeWaitForMultipleObjects
                 (
                    MP_THREAD_EVENT_COUNT,
                    (VOID **)&pAdapter->MPThreadEvent,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,             // non-alertable
                    &waitInterval,
                    pwbArray
                 );
      switch (ntStatus)
      {
         case MP_INIT_QMI_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: init QMI\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->MPThreadInitQmiEvent);
            MPWork_WorkItem(NULL, pAdapter);
            MPMAIN_InitializeQMI(pAdapter, QMICTL_RETRIES);
            break;
         }

         case MP_CANCEL_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: CANCEL\n", pAdapter->PortName)
            );

            KeClearEvent(&pAdapter->MPThreadCancelEvent);
            bKeepRunning = FALSE;
            break;
         }

         case MP_TX_TIMER_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_TRACE,
               ("<%s> MPth: TX Timer\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->MPThreadTxTimerEvent);

            // Reset the resolution
            // ExSetTimerResolution(0, FALSE);
            
            /* Put the code for flush here */
#ifdef NDIS620_MINIPORT
            MPUSB_TLPTxPacketEx(pAdapter, 0, FALSE, NULL);
#else
            MPUSB_TLPTxPacket(pAdapter, 0, FALSE, NULL);
#endif
            
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> ---> TransmitTimerDpc Flush due to timer count %d\n", pAdapter->PortName, pAdapter->FlushBufferinTimer++)
            );
            
            break;
         }

         case MP_TX_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_TRACE,
               ("<%s> MPth: TX\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->MPThreadTxEvent);
            #ifdef MP_QCQOS_ENABLED
            if (pAdapter->QosEnabled == TRUE)
            {
               KeSetEvent(&pAdapter->QosThreadTxEvent, IO_NO_INCREMENT, FALSE);
            }
            else
            #endif // MP_QCQOS_ENABLED
            {
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
            if ((pAdapter->QMAPEnabledV2 == TRUE)|| (pAdapter->QMAPEnabledV3 == TRUE))
            {
                   MPUSB_TLPProcessPendingTxQueue(pAdapter);
            }            
            else
            {
#endif
#endif
#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
                if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)|| (pAdapter->QMAPEnabledV4 == TRUE)
                    || (pAdapter->QMAPEnabledV1 == TRUE)    || (pAdapter->MPQuickTx != 0))
               {
                  MPUSB_TLPProcessPendingTxQueue(pAdapter);
               }
               else
               #endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT
               {
                  MPTX_ProcessPendingTxQueue(pAdapter);
               }
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
            }
#endif
#endif
               
            }
            break;
         }

         case MP_MEDIA_CONN_EVENT_INDEX:
         case MP_MEDIA_CONN_EVENT_INDEX_IPV6:
         {
            PMP_ADAPTER returnAdapter = NULL;
            NDIS_STATUS ndisStatus = NDIS_STATUS_SUCCESS;

            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: MEDIA_CONN\n", pAdapter->PortName)
            );

            pAdapter->WdsMediaConnected = TRUE;
            if (pAdapter->QmiInitialized == FALSE)
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> MPth: MEDIA_CONN: wait for QMI init...\n", pAdapter->PortName)
               );
               MPMAIN_Wait(-(50 * 10 * 1000L)); // 50ms
               break;
            }
            pAdapter->WdsMediaConnected = FALSE; // reset

            if (ntStatus == MP_MEDIA_CONN_EVENT_INDEX)
               KeClearEvent(&pAdapter->MPThreadMediaConnectEvent);
            else
               KeClearEvent(&pAdapter->MPThreadMediaConnectEventIPv6);

#ifdef QC_IP_MODE
            if (pAdapter->IPModeEnabled == TRUE)
            {
               /* Clear DNS address */
#ifdef NDIS620_MINIPORT
               if (pAdapter->IPv4DataCall == 0)
               {
                  MPWWAN_ClearDNSAddress(pAdapter, 0x04);
               }
               if (pAdapter->IPv6DataCall == 0)
               {
                  MPWWAN_ClearDNSAddress(pAdapter, 0x06);
               }
#endif   

#ifdef QCMP_DISABLE_QMI
               if (pAdapter->DisableQMI == 0)
               {
#endif               
               // Get device IP address
               if (pAdapter->IPv4DataCall == 1)
               {
                  ndisStatus = MPQMUX_GetIPAddress(pAdapter, pAdapter);
               }
               else
               {
                  RtlZeroMemory((PVOID)&(pAdapter->IPSettings.IPV4), sizeof(pAdapter->IPSettings.IPV4));
               }
               if ((QCMAIN_IsDualIPSupported(pAdapter) == TRUE) && (pAdapter->WdsIpClientContext != NULL))
               {
                  if (pAdapter->IPv6DataCall == 1)
                  {
                     // Get device IPV6 address
                     ndisStatus = MPQMUX_GetIPAddress(pAdapter, pAdapter->WdsIpClientContext);
                  }
                  else
                  {
                     RtlZeroMemory((PVOID)&(pAdapter->IPSettings.IPV6), sizeof(pAdapter->IPSettings.IPV6));
                  }
               }
#ifdef QCMP_DISABLE_QMI
               }
               else
               {
                  ndisStatus = NDIS_STATUS_SUCCESS;
               }
#endif               
               USBIF_SetDataMode(pAdapter->USBDo);
            }
#endif // QC_IP_MODE

            if (ndisStatus != NDIS_STATUS_SUCCESS)
            {
               // cannot connect if IP mode enbaled without IP address
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> MPth: MEDIA_CONN: IP failure\n", pAdapter->PortName)
               );
               break;
            }

#ifdef QC_IP_MODE
#ifdef NDIS620_MINIPORT

            MPWWAN_SetDNSAddress(pAdapter);

            #endif // NDIS620_MINIPORT
            #endif // QC_IP_MODE

            // If running on version higher than XP
            if (IoIsWdmVersionAvailable(0x06, 0x00))
            {
               if (pAdapter->ulMediaConnectStatus == NdisMediaStateConnected)
               {
                  LARGE_INTEGER callTime;
   
                  KeQuerySystemTime(&callTime);
                  if ((callTime.QuadPart - firstCallTime.QuadPart) >= 100000000L)  // 10s
                  {

#ifndef NDIS60_MINIPORT
                     // revive IP settings -- toggling connection state
                     NdisMIndicateStatus
                     (
                        pAdapter->AdapterHandle,
                        NDIS_STATUS_MEDIA_DISCONNECT,
                        (PVOID)0,
                        0
                     );
                     NdisMIndicateStatusComplete(pAdapter->AdapterHandle);
#endif
                     firstCallTime.QuadPart = callTime.QuadPart;
                     MPMAIN_Wait(-(5 * 1000 * 1000L)); // 500ms
                  }
               }
               else
               {
                  pAdapter->ulMediaConnectStatus = NdisMediaStateConnected;
                  KeQuerySystemTime(&firstCallTime);
               }
            }
            else
            {
               pAdapter->ulMediaConnectStatus = NdisMediaStateConnected;
            }
            MPWork_ScheduleWorkItem(pAdapter); // post data-read IRPs
            
            DisconnectedAllAdapters(pAdapter, &returnAdapter);
            USBIF_PollDevice(returnAdapter->USBDo, TRUE);
            // USBIF_PollDevice(returnAdapter->USBDo, TRUE);

#ifdef QCMP_TEST_MODE
            if (pAdapter->TestConnectDelay == 0)
            {
               MPMAIN_Wait(-(50 * 1000L)); // 5ms
            }
            else
            {
               // Test Delay: (pAdapter->TestConnectDelay * 1)ms
               MPMAIN_Wait(-(pAdapter->TestConnectDelay * 10 * 1000L));
            }
#else
            MPMAIN_Wait(-(50 * 1000L)); // 5ms
#endif // QCMP_TEST_MODE

            NdisAcquireSpinLock(&pAdapter->QMICTLLock);
            pAdapter->NumberOfConnections++;
            NdisReleaseSpinLock(&pAdapter->QMICTLLock);

            if (pAdapter->DisableTimerResolution == 0)
            {
               SetTimerResolution( pAdapter, TRUE );
            }
            
#ifdef NDIS620_MINIPORT

            {
            // Send Context connect indication
            NDIS_WWAN_CONTEXT_STATE ContextState;

            pAdapter->DeviceContextState = DeviceWWanContextAttached;
            NdisZeroMemory( &ContextState, sizeof(NDIS_WWAN_CONTEXT_STATE) );
            ContextState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            ContextState.Header.Revision = NDIS_WWAN_CONTEXT_STATE_REVISION_1;
            ContextState.Header.Size = sizeof(NDIS_WWAN_CONTEXT_STATE);
            ContextState.ContextState.VoiceCallState = WwanVoiceCallStateNone;
            ContextState.ContextState.ConnectionId = pAdapter->WWanServiceConnectHandle;
            ContextState.ContextState.ActivationState = WwanActivationStateActivated;
            MyNdisMIndicateStatus
            (
               pAdapter->AdapterHandle,
               NDIS_STATUS_WWAN_CONTEXT_STATE,
               (PVOID)&ContextState,
               sizeof(NDIS_WWAN_CONTEXT_STATE)
            );
            }

            {
            NDIS_LINK_STATE LinkState;
            pAdapter->DeviceContextState = DeviceWWanContextAttached;
            NdisZeroMemory( &LinkState, sizeof(NDIS_LINK_STATE) );
            LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            LinkState.Header.Revision  = NDIS_LINK_STATE_REVISION_1;
            LinkState.Header.Size  = sizeof(NDIS_LINK_STATE);
            LinkState.MediaConnectState = MediaConnectStateConnected;
            LinkState.MediaDuplexState = MediaDuplexStateUnknown;
            LinkState.PauseFunctions = NdisPauseFunctionsUnknown;
            LinkState.AutoNegotiationFlags = NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED |
                                             NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED;
            //LinkState.XmitLinkSpeed = pAdapter->ulCurrentTxRate;
            //LinkState.RcvLinkSpeed = pAdapter->ulCurrentRxRate;

            //Spoof xmit link speed
            //LinkState.XmitLinkSpeed = pAdapter->ulServingSystemTxRate;
            LinkState.XmitLinkSpeed = pAdapter->ulServingSystemRxRate;
            LinkState.RcvLinkSpeed = pAdapter->ulServingSystemRxRate;

            MyNdisMIndicateStatus
            (
               pAdapter->AdapterHandle,
               NDIS_STATUS_LINK_STATE,
               (PVOID)&LinkState,
               sizeof(NDIS_LINK_STATE)
            );
            }

            MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WDS, 
                                QMIWDS_GET_CURRENT_CHANNEL_RATE_REQ, NULL, TRUE );
            // MPQWDS_SendEventReportReq(pAdapter, TRUE);  //Murali TODO

            /* Get the current data bearer req */
            MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WDS, 
                                QMIWDS_GET_DATA_BEARER_REQ, NULL, TRUE );
                     
#else
            MyNdisMIndicateStatus
            (
               pAdapter->AdapterHandle,
               NDIS_STATUS_MEDIA_CONNECT,
               (PVOID)0,
               0
            );
#endif
            #ifdef QC_IP_MODE
            #ifdef NDIS620_MINIPORT

            MPWWAN_AdvertiseIPSettings(pAdapter);

            #endif // NDIS620_MINIPORT
            #endif // QC_IP_MODE

            break;
         }

         case MP_MEDIA_DISCONN_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: MEDIA_DISCONN\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->MPThreadMediaDisconnectEvent);

            // Currently reset the resolution here
            MPMAIN_MediaDisconnect(pAdapter, TRUE);

            break;
         }

         case MP_IODEV_ACT_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_TRACE,
               ("<%s> MPth: IODEV_ACT\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->MPThreadIODevActivityEvent);

            MPIOC_CheckForAquiredDeviceIdleTime(pAdapter);

            break;
         }

         case MP_IPV4_MTU_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: IPV4_MTU_EVENT\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->IPV4MtuReceivedEvent);
            MPIOC_AdvertiseMTU(pAdapter, 4, pAdapter->IPV4Mtu);
            break;
         }

         case MP_IPV6_MTU_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: IPV6_MTU_EVENT\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->IPV6MtuReceivedEvent);
            MPIOC_AdvertiseMTU(pAdapter, 6, pAdapter->IPV6Mtu);
            break;
         }

#ifdef QCUSB_MUX_PROTOCOL

         case MP_DL_PAUSE_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: DL_PAUSE, pktCount %d\n", pAdapter->PortName, pAdapter->DLPktCount)
            );
            KeClearEvent(&pAdapter->MPThreadDLPauseEvent);
            if (pAdapter->DLPauseEnabled == TRUE)
            {
               USBIF_PollDevice(pAdapter->USBDo, FALSE);
            }

            break;
         }

         case MP_DL_RESUME_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: DL_RESUME\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->MPThreadDLResumeEvent);
            if (pAdapter->DLPauseEnabled == TRUE)
            {
               InterlockedExchange(&pAdapter->DLPktCount, 0);
               USBIF_PollDevice(pAdapter->USBDo, TRUE);
            }

            break;
         }
         
#ifdef QC_DUAL_IP_FC

         case MP_PAUSE_V4_EVENT_INDEX:
         {
            KeClearEvent(&pAdapter->MPThreadPauseV4Event);
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: PAUSE_V4\n", pAdapter->PortName)
            );
            // flow off
            MPMAIN_FlowControlCall(pAdapter, 4, TRUE);
            break;
         }
         case MP_PAUSE_V6_EVENT_INDEX:
         {
             KeClearEvent(&pAdapter->MPThreadPauseV6Event);
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: PAUSE_V6\n", pAdapter->PortName)
            );
            // flow off
            MPMAIN_FlowControlCall(pAdapter, 6, TRUE);
            break;
         }
         case MP_RESUME_V4_EVENT_INDEX:
         {
             KeClearEvent(&pAdapter->MPThreadResumeV4Event);
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: RESUME_V4\n", pAdapter->PortName)
            );
            // flow on
            MPMAIN_FlowControlCall(pAdapter, 4, FALSE);
            break;
         }
         case MP_RESUME_V6_EVENT_INDEX:
         {
             KeClearEvent(&pAdapter->MPThreadResumeV6Event);
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: RESUME_V6\n", pAdapter->PortName)
            );
            // flow on
            MPMAIN_FlowControlCall(pAdapter, 6, FALSE);
            break;
         }

#endif // QC_DUAL_IP_FC

#endif // ifdef QCUSB_MUX_PROTOCOL

#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
         case MP_QMAP_CONTROL_EVENT_INDEX:
         {
            KeClearEvent(&pAdapter->MPThreadQMAPControlEvent);
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: QMAP FLOW CONTROL COMMAND\n", pAdapter->PortName)
            );
            // flow off
            USBIF_QMAPProcessControlQueue(pAdapter->USBDo);
            break;
         }

         case MP_PAUSE_DL_QMAP_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: QMAP_DL_PAUSE\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->MPThreadQMAPDLPauseEvent);
            // if (pAdapter->QMAPDLPauseEnabled != TRUE)
            {
               USBIF_QMAPSendFlowControlCommand(pAdapter->USBDo, 0, QMAP_FLOWCONTROL_COMMAND_PAUSE, NULL);
               // USBIF_PollDevice(pAdapter->USBDo, FALSE);
               pAdapter->QMAPDLPauseEnabled = TRUE;
            }

            break;
         }

         case MP_RESUME_DL_QMAP_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
               ("<%s> MPth: QMAP_DL_RESUME\n", pAdapter->PortName)
            );
            KeClearEvent(&pAdapter->MPThreadQMAPDLResumeEvent);
            // if (pAdapter->QMAPDLPauseEnabled == TRUE)
            {
               USBIF_QMAPSendFlowControlCommand(pAdapter->USBDo, 0, QMAP_FLOWCONTROL_COMMAND_RESUME, NULL);
               // USBIF_PollDevice(pAdapter->USBDo, TRUE);
               pAdapter->QMAPDLPauseEnabled = FALSE;
            }
            break;
         }

#endif         

         default:
         {
            PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;
            if (ntStatus != STATUS_TIMEOUT)
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> MPth: unexpected evt-0x%x\n", pAdapter->PortName, ntStatus)
               );
            }
            else
            {
               ++numTimeouts;

               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL,
                  MP_DBG_LEVEL_DETAIL,
                  ("<%s> MPth: TO %d Flags 0x%x\n", pAdapter->PortName, numTimeouts, pAdapter->Flags)
               );
            }
            if ((pAdapter->Flags & fMP_ANY_FLAGS) == 0)
            {
               if ((pAdapter->ClientId[QMUX_TYPE_WDS] == 0) && (numTimeouts > 1))
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL,
                     MP_DBG_LEVEL_DETAIL,
                     ("<%s> MPth: retry QMI init %d\n", pAdapter->PortName, numTimeouts)
                  );
                  KeSetEvent(&pAdapter->MPThreadInitQmiEvent, IO_NO_INCREMENT, FALSE);
                  break;
               }
               else // if (pAdapter->ClientId[QMUX_TYPE_WDS] != 0)
               {
                  if (pAdapter->QmiSyncNeeded > 0)
                  {
                     QCNET_DbgPrint
                     (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_DETAIL,
                        ("<%s> MPth: Adapter sync needed value %d QMIOutOfServ %d\n", pAdapter->PortName,
                          pAdapter->QmiSyncNeeded, pAdapter->IsQMIOutOfService)
                     );

                      QCNET_DbgPrint
                      (
                         MP_DBG_MASK_CONTROL,
                         MP_DBG_LEVEL_DETAIL,
                         ("<%s> MPth: sync needed, init QMI\n", pAdapter->PortName)
                      );
                      MPMAIN_DisconnectNotification(pAdapter);
                      KeSetEvent(&pAdapter->MPThreadInitQmiEvent, IO_NO_INCREMENT, FALSE);
                      InterlockedDecrement(&pAdapter->QmiSyncNeeded);
                  }
               }

               if ((pAdapter->WdsMediaConnected == TRUE) &&
                   (pAdapter->ClientId[QMUX_TYPE_WDS] != 0))
                   //(pAdapter->QMIType == QMUX_TYPE_WDS)
               {
                  ++retries;
#if 0 //need to check if this is needed in the new implementation or not
                  pAdapter->WdsMediaConnected = FALSE; // reset
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL,
                     MP_DBG_LEVEL_DETAIL,
                     ("<%s> MPth: retry MEDIA_CONNECT-%d\n", pAdapter->PortName, retries)
                  );
                  // Query IPV4/IPV6 tp confirm media still connected
                  MPQMUX_ComposeWdsGetPktSrvcStatusReqSend(pAdapter, pAdapter);
                  if (pAdapter->WdsIpClientContext != NULL)
                  {
                     MPQMUX_ComposeWdsGetPktSrvcStatusReqSend(pAdapter, pAdapter->WdsIpClientContext);
                  }
#endif
                  // KeSetEvent
                  // (
                  //    &pAdapter->MPThreadMediaConnectEvent,
                  //    IO_NO_INCREMENT,
                  //    FALSE
                  // );
               }
               else
               {
                  // KeClearEvent(&pAdapter->MPThreadMediaConnectEvent);

                  // self-check the TX queue
                  NdisAcquireSpinLock(&pAdapter->TxLock);
                  if (!IsListEmpty(&pAdapter->TxPendingList))
                  {
                     KeSetEvent(&pAdapter->MPThreadTxEvent, IO_NO_INCREMENT, FALSE);
                  }
                  else
                  {
                     QCNET_DbgPrint
                     (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_ERROR,
                        ("<%s> MPth: TX queue empty, TO %d SyncNeeded %d WDS %d\n", pAdapter->PortName,
                          numTimeouts, pAdapter->QmiSyncNeeded, pAdapter->ClientId[QMUX_TYPE_WDS])
                     );
                  }
                  NdisReleaseSpinLock(&pAdapter->TxLock);
               }
            }
            break;
         }
      }
   }  // while

   if (pwbArray != NULL)
   {
      ExFreePool(pwbArray);
   }

   // Again, try to cleanup the Tx pending queue
   // MP_ProcessPendingTxQueue(pAdapter);
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
    if ((pAdapter->QMAPEnabledV2 == TRUE)|| (pAdapter->QMAPEnabledV3 == TRUE))
    {
           MPUSB_TLPProcessPendingTxQueue(pAdapter);
    }            
    else
    {
#endif
#endif
#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
    if ((pAdapter->TLPEnabled == TRUE) || (pAdapter->MBIMULEnabled == TRUE)||(pAdapter->QMAPEnabledV4 == TRUE) 
          || (pAdapter->QMAPEnabledV1 == TRUE)    || (pAdapter->MPQuickTx != 0))
    {
       MPUSB_TLPProcessPendingTxQueue(pAdapter);
    }
    else
#endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT
    {
   MPTX_ProcessPendingTxQueue(pAdapter);
    }
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
    }
#endif
#endif

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_FORCE,
      ("<%s> MPth: OUT (0x%x)\n", pAdapter->PortName, ntStatus)
   );

   KeSetEvent(&pAdapter->MPThreadClosedEvent, IO_NO_INCREMENT, FALSE);

   PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}  // MPMAIN_MPThread

VOID MPMAIN_Wait(LONGLONG WaitTime)
{
   LARGE_INTEGER delayValue;
   KEVENT forTimeoutEvent;

   delayValue.QuadPart = WaitTime;
   KeInitializeEvent(&forTimeoutEvent, NotificationEvent, FALSE);
   KeWaitForSingleObject
   (
      &forTimeoutEvent,
      Executive,
      KernelMode,
      FALSE,
      &delayValue
   );

   return;
}  // MPMAIN_Wait

VOID MPMAIN_PrintBytes
(
   PVOID Buf,
   ULONG len,
   ULONG PktLen,
   char *info,
   PMP_ADAPTER pAdapter,
   ULONG DbgMask,
   ULONG DbgLevel
)
{
   #ifdef QCUSB_DBGPRINT_BYTES

   ULONG nWritten;
   char  *buf, *p, *cBuf, *cp;
   char *buffer;
   ULONG count = 0, lastCnt = 0, spaceNeeded;
   ULONG i, j, s;
   ULONG nts;
   PCHAR dbgOutputBuffer;
   ULONG myTextSize = 1280;

   #define SPLIT_CHAR '|'

   if (pAdapter != NULL)
   {
#ifdef EVENT_TRACING
      if (!((pAdapter->DebugMask !=0) && (QCWPP_USER_FLAGS(WPP_DRV_MASK_CONTROL) & DbgMask) && 
            (QCWPP_USER_LEVEL(WPP_DRV_MASK_CONTROL) >= DbgLevel))) 
   
      {
         return;
      }
#else
      if (((pAdapter->DebugMask & DbgMask) == 0) || (pAdapter->DebugLevel < DbgLevel))
      {
         return;
      }
#endif
   }

   // re-calculate text buffer size
   if (myTextSize < (len * 5 +360))
   {
      myTextSize = len * 5 +360;
   }

   buffer = (char *)Buf;

   dbgOutputBuffer = ExAllocatePool(NonPagedPool, myTextSize);
   if (dbgOutputBuffer == NULL)
   {
      return;
   }

   RtlZeroMemory(dbgOutputBuffer, myTextSize);
   cBuf = dbgOutputBuffer;
   buf  = dbgOutputBuffer + 128;
   p    = buf;
   cp   = cBuf;

   if (PktLen < len)
   {
      len = PktLen;
   }

   sprintf(p, "\r\n\t   --- <%s> DATA %u/%u BYTES ---\r\n", info, len, PktLen);
   p += strlen(p);

   for (i = 1; i <= len; i++)
   {
      if (i % 16 == 1)
      {
         sprintf(p, "  %04u:  ", i-1);
         p += 9;
      }

      sprintf(p, "%02X ", (UCHAR)buffer[i-1]);
      if (isprint(buffer[i-1]) && (!isspace(buffer[i-1])))
      {
         sprintf(cp, "%c", buffer[i-1]);
      }
      else
      {
         sprintf(cp, ".");
      }

      p += 3;
      cp += 1;

      if ((i % 16) == 8)
      {
         sprintf(p, "  ");
         p += 2;
      }

      if (i % 16 == 0)
      {
         if (i % 64 == 0)
         {
            sprintf(p, " %c  %s\r\n\r\n", SPLIT_CHAR, cBuf);
         }
         else
         {
            sprintf(p, " %c  %s\r\n", SPLIT_CHAR, cBuf);
         }
         QCNET_DbgPrint
         (
            DbgMask,
            DbgLevel,
            ("%s",buf)
         );
         RtlZeroMemory(dbgOutputBuffer, myTextSize);
         p = buf;
         cp = cBuf;
      }
   }

   lastCnt = i % 16;

   if (lastCnt == 0)
   {
      lastCnt = 16;
   }

   if (lastCnt != 1)
   {
      // 10 + 3*8 + 2 + 3*8 = 60 (full line bytes)
      spaceNeeded = (16 - lastCnt + 1) * 3;
      if (lastCnt <= 8)
      {
         spaceNeeded += 2;
      }
      for (s = 0; s < spaceNeeded; s++)
      {
         sprintf(p++, " ");
      }
      sprintf(p, " %c  %s\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n",
              SPLIT_CHAR, cBuf, info, len, PktLen);
      QCNET_DbgPrint
      (
         DbgMask,
         DbgLevel,
         ("%s",buf)
      );
   }
   else
   {
      sprintf(buf, "\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n", info, len, PktLen);
      QCNET_DbgPrint
      (
         DbgMask,
         DbgLevel,
         ("%s",buf)
      );
   }

   ExFreePool(dbgOutputBuffer);

   #endif // QCUSB_DBGPRINT_BYTES
}  //MPMAIN_PrintBytes

VOID MPMAIN_PowerDownDevice(PVOID Context, DEVICE_POWER_STATE DevicePower)
{
   PMP_ADAPTER  pAdapter = (PMP_ADAPTER)Context;
   NDIS_DEVICE_POWER_STATE pwrState = NdisDeviceStateD3;

   if (DevicePower < PowerDeviceD3)
   {
      pwrState = NdisDeviceStateD2;
   }

   USBIF_SetPowerState(pAdapter->USBDo, pwrState);
}  // MPMAIN_PowerDownDevice

VOID MPMAIN_ResetPacketCount(PMP_ADAPTER pAdapter)
{
   pAdapter->GoodTransmits   = 0;
   pAdapter->GoodReceives    = 0;
   pAdapter->BadTransmits    = 0;
   pAdapter->BadReceives     = 0;
   pAdapter->DroppedReceives = 0;
}  // MPMAIN_ResetPacketCount

VOID MPMAIN_DeregisterMiniport(VOID)
{
   #ifdef NDIS60_MINIPORT
   if (QCMP_NDIS6_Ok == TRUE)
   {
      if (NdisMiniportDriverHandle != NULL)
      {
         NdisMDeregisterMiniportDriver(NdisMiniportDriverHandle);
      }
   }
   // else
   #else
   {
      NdisTerminateWrapper(MyNdisHandle, NULL);
   }
   #endif // NDIS60_MINIPORT
}  // MPMAIN_DeregisterMiniport

VOID MPMAIN_DetermineNdisVersion(VOID)
{
   QCMP_NDIS6_Ok = QCMP_NDIS620_Ok = QCMP_NDIS630_Ok = FALSE;

   #ifdef NDIS60_MINIPORT
   if (TRUE == RtlIsNtDdiVersionAvailable(NTDDI_LONGHORN))
   {
      QCNET_DbgPrintG(("\n<%s> QCMP_NDIS6_Ok\n", gDeviceName));
      QCMP_NDIS6_Ok = TRUE;
   }
   #endif // NDIS60_MINIPORT

   #ifdef NDIS620_MINIPORT
   if (TRUE == RtlIsNtDdiVersionAvailable(NTDDI_WIN7))
   {
      QCNET_DbgPrintG(("\n<%s> QCMP_NDIS6_Ok/QCMP_NDIS620_Ok\n", gDeviceName));
      QCMP_NDIS6_Ok = TRUE;   
      QCMP_NDIS620_Ok = TRUE;
   }
   #endif // NDIS620_MINIPORT

   #ifdef NDIS630_MINIPORT
   if (TRUE == RtlIsNtDdiVersionAvailable(NTDDI_WIN10))
   {
       QCNET_DbgPrintG(("\n<%s> QCMP_NDIS6_Ok/QCMP_NDIS620_Ok/QCMP_NDIS630_Ok\n", gDeviceName));
       QCMP_NDIS6_Ok = TRUE;
       QCMP_NDIS620_Ok = TRUE;
       QCMP_NDIS630_Ok = TRUE;
   }
   #endif // NDIS620_MINIPORT

}  // MPMAIN_DetermineNdisVersion

VOID MyNdisMIndicateStatus
(
   NDIS_HANDLE  MiniportAdapterHandle,
   NDIS_STATUS  GeneralStatus,
   PVOID        StatusBuffer,
   UINT         StatusBufferSize
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)MiniportAdapterHandle;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_OID_QMI,
      MP_DBG_LEVEL_INFO,
      ("\n<%s> MyNdisMIndicateStatus oid 0x%x\n\n", gDeviceName, GeneralStatus)
   );

   #ifdef NDIS60_MINIPORT
   if (QCMP_NDIS6_Ok == TRUE)
   {
      NDIS_STATUS_INDICATION  ind;

      NdisZeroMemory(&ind, sizeof(NDIS_STATUS_INDICATION));
      ind.Header.Type      = NDIS_OBJECT_TYPE_STATUS_INDICATION;
      ind.Header.Revision  = NDIS_STATUS_INDICATION_REVISION_1;
      ind.Header.Size      = sizeof(NDIS_STATUS_INDICATION);
      ind.SourceHandle     = MiniportAdapterHandle;
      ind.StatusCode       = GeneralStatus;
      ind.StatusBuffer     = StatusBuffer;
      ind.StatusBufferSize = StatusBufferSize;
      NdisMIndicateStatusEx(MiniportAdapterHandle, &ind);
   }
   // else
   #else
   {
      NdisMIndicateStatus
      (
         MiniportAdapterHandle,
         GeneralStatus,
         StatusBuffer,
         StatusBufferSize
      );
      NdisMIndicateStatusComplete(MiniportAdapterHandle);
   }
   #endif // NDIS60_MINIPORT

}  // MyNdisMIndicateStatus

#ifdef NDIS60_MINIPORT

/*********
NDIS calls a miniport driver's MiniportPause function to stop the
flow of network data through a specified miniport adapter.
http://msdn.microsoft.com/en-us/library/bb743038.aspx
 1. Deny all send requests
 2. Stop issuing new read requests
 3. wait till all sends are completed
 4. wait for all read buffers are returned from NDIS
**********/
NDIS_STATUS MPMAIN_MiniportPause
(
   IN NDIS_HANDLE                     MiniportAdapterContext,
   IN PNDIS_MINIPORT_PAUSE_PARAMETERS MiniportPauseParameters
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)MiniportAdapterContext;
   NDIS_STATUS ndisStatus = NDIS_STATUS_SUCCESS;
   ULONG pauseCount = 0;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->MPMAIN_MiniportPause: Flags 0x%x\n", pAdapter->PortName, pAdapter->Flags)
   );

   pAdapter->Flags |= fMP_STATE_PAUSE;

   CleanupTxQueues(pAdapter);

   while (TRUE)
   {
      if (TRUE == MPMAIN_AreDataQueuesEmpty(pAdapter, "PAUSE"))
      {
         break;
      }
      ++pauseCount;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_MiniportPause - waiting(%u)\n", pAdapter->PortName, pauseCount)
      );
      NdisMSleep(1000*100);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--MPMAIN_MiniportPause: Flags 0x%x\n", pAdapter->PortName, pAdapter->Flags)
   );
   return ndisStatus;
}  // MPMAIN_MiniportPause

NDIS_STATUS MPMAIN_MiniportRestart
(
   NDIS_HANDLE                       MiniportAdapterContext,
   PNDIS_MINIPORT_RESTART_PARAMETERS MiniportRestartParameters
)
{

   PMP_ADAPTER pAdapter = (PMP_ADAPTER)MiniportAdapterContext;
   NDIS_STATUS ndisStatus = NDIS_STATUS_SUCCESS;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->MPMAIN_MiniportRestart: Flags 0x%x\n", pAdapter->PortName, pAdapter->Flags)
   );

   pAdapter->Flags &= ~fMP_STATE_PAUSE;

   // activate I/O
   KeSetEvent(&pAdapter->WorkThreadProcessEvent, IO_NO_INCREMENT, FALSE);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--MPMAIN_MiniportRestart: Flags 0x%x\n", pAdapter->PortName, pAdapter->Flags)
   );

   return ndisStatus;
}  // MPMAIN_MiniportRestart

VOID MPMAIN_MiniportDevicePnPEventNotify
(
   NDIS_HANDLE            MiniportAdapterContext,
   PNET_DEVICE_PNP_EVENT  NetDevicePnPEvent
)
{
   MPMAIN_MiniportPnPEventNotify
   (
      MiniportAdapterContext,
      NetDevicePnPEvent->DevicePnPEvent,
      NetDevicePnPEvent->InformationBuffer,
      NetDevicePnPEvent->InformationBufferLength
   );
}  // MPMAIN_MiniportDevicePnPEventNotify

#endif // NDIS60_MINIPORT

BOOLEAN MPMAIN_InPauseState(PMP_ADAPTER pAdapter)
{
   return ((pAdapter->Flags & fMP_STATE_PAUSE) != 0);
}  // MPMAIN_InPauseState

BOOLEAN MPMAIN_AreDataQueuesEmpty(PMP_ADAPTER pAdapter, PCHAR Caller)
{
   BOOLEAN allQueuesClear = TRUE;

   // We do not checkk RX queue. RX will be dropped when paused
   /***************************
   if (pAdapter->nBusyRx > 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> %s: nBusyRx = %d F%d P%d (N%d F%d U%d)\n", pAdapter->PortName,
           Caller, pAdapter->nBusyRx,
           listDepth(&pAdapter->RxFreeList, &pAdapter->RxLock),
           listDepth(&pAdapter->RxPendingList, &pAdapter->RxLock),
           pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb)
         )
      );
      allQueuesClear = FALSE;
   }
   ****************************/

   if (pAdapter->nBusyTx > 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> %s: nBusyTx = %d P%d B%d C%d\n", pAdapter->PortName,
           Caller, pAdapter->nBusyTx,
           listDepth(&pAdapter->TxPendingList, &pAdapter->TxLock),
           listDepth(&pAdapter->TxBusyList, &pAdapter->TxLock),
           listDepth(&pAdapter->TxCompleteList, &pAdapter->TxLock)
         )
      );
      allQueuesClear = FALSE;
   }

   return allQueuesClear;
}  // MPMAIN_AreDataQueuesEmpty

NDIS_STATUS MPMAIN_OpenQMIClients(PMP_ADAPTER pAdapter)
{
   NDIS_STATUS   ndisStatus;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPMAIN_OpenQMIClients\n", pAdapter->PortName)
   );

   ndisStatus = MPQCTL_GetClientId(pAdapter, QMUX_TYPE_WDS, pAdapter);
   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      goto ExitPoint;
   }
   ndisStatus = MPQCTL_GetClientId(pAdapter, QMUX_TYPE_DMS, pAdapter);
   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      goto ExitPoint;
   }
   ndisStatus = MPQCTL_GetClientId(pAdapter, QMUX_TYPE_NAS, pAdapter);
   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      goto ExitPoint;
   }
   ndisStatus = MPQCTL_GetClientId(pAdapter, QMUX_TYPE_WMS, pAdapter);
   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      goto ExitPoint;
   }

   ndisStatus = MPQCTL_GetClientId(pAdapter, QMUX_TYPE_UIM, pAdapter);
   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      goto ExitPoint;
   }
   
ExitPoint:

   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      // clean up device
      USBCTL_ClrDtrRts(pAdapter->USBDo);
      USBCTL_SetDtr(pAdapter->USBDo);

      // clean up host
      RtlZeroMemory((PVOID)pAdapter->ClientId, (QMUX_TYPE_MAX+1));
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPMAIN_OpenQMIClients 0x%x\n", pAdapter->PortName, ndisStatus)
   );
   return ndisStatus;
}  // MPMAIN_OpenQMIClients

NDIS_STATUS MPMAIN_MiniportSetOptions
(
   NDIS_HANDLE  NdisDriverHandle,
   NDIS_HANDLE  DriverContext
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)NdisDriverHandle;
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_TRACE,
      ("\n<%s> -->MPMAIN_MiniportSetOptions", gDeviceName) 
   );
   // ...
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_TRACE,
      ("\n<%s> <--MPMAIN_MiniportSetOptions", gDeviceName)
   );
   return NDIS_STATUS_SUCCESS;
}  // MPMAIN_MiniportSetOptions

BOOLEAN QCMAIN_IsDualIPSupported(PMP_ADAPTER pAdapter)
{
  return (pAdapter->QMUXVersion[QMUX_TYPE_WDS].Major >= 1 && pAdapter->QMUXVersion[QMUX_TYPE_WDS].Minor >= 9);
}  // QCMAIN_IsDualIPSupported

VOID MPMAIN_MediaDisconnect(PMP_ADAPTER pAdapter, BOOLEAN DisconnectAll)
{
   BOOLEAN resetResolution = FALSE;
   NdisAcquireSpinLock(&pAdapter->QMICTLLock);
   if (DisconnectAll == TRUE)
   {
      pAdapter->NumberOfConnections = 0;
   }
   else
   {
      if (pAdapter->NumberOfConnections > 0)
      {
         pAdapter->NumberOfConnections--;
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPMAIN_MediaDisconnect: already disconnected\n", pAdapter->PortName)
         );
      }
   }

   if (pAdapter->NumberOfConnections == 0)
   {
      resetResolution = TRUE;
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
      pAdapter->QMAPDLPauseEnabled = FALSE;
      pAdapter->QMAPFlowControl = FALSE;
   }
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> MPMAIN_MediaDisconnect: %d\n", pAdapter->PortName, pAdapter->NumberOfConnections)
   );
   NdisReleaseSpinLock(&pAdapter->QMICTLLock);
   
#ifdef NDIS620_MINIPORT
#ifdef QC_IP_MODE
   if (pAdapter->NumberOfConnections == 0)
   {
      MPWWAN_ClearIPSettings(pAdapter);
   }
#endif
#endif

   if (resetResolution == TRUE)
   {
       // Reset the timer resolution
       if (KeGetCurrentIrql() < DISPATCH_LEVEL)
       {
          if (pAdapter->DisableTimerResolution == 0)
          {
             SetTimerResolution( pAdapter, FALSE );
          }
       }
   }   

   // reset MTU to QC_DATA_MTU_DEFAULT (1500)
   pAdapter->IPV4Mtu = pAdapter->IPV6Mtu = QC_DATA_MTU_DEFAULT; // pAdapter->MTUSize;
   KeSetEvent(&pAdapter->IPV4MtuReceivedEvent, IO_NO_INCREMENT, FALSE);
   KeSetEvent(&pAdapter->IPV6MtuReceivedEvent, IO_NO_INCREMENT, FALSE);
}  // MPMAIN_MediaDisconnect

/***
MPMAIN_DisconnectNotification is called when: 1) SSR happens; 2) QMI_SYNC received
***/
VOID MPMAIN_DisconnectNotification(PMP_ADAPTER pAdapter)
{
   PMP_ADAPTER returnAdapter = NULL;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPMAIN_DisconnectNotification: 0x%x  WDS 0x%x\n", pAdapter->PortName,
        pAdapter->ulMediaConnectStatus, pAdapter->ClientId[QMUX_TYPE_WDS])
   );

   if (pAdapter->ulMediaConnectStatus == NdisMediaStateConnected)
   {
      pAdapter->IPV6Connected = pAdapter->IPV4Connected = FALSE;
      pAdapter->IPV6Address   = pAdapter->IPV4Address   = 0;
      pAdapter->IPv4DataCall = pAdapter->IPv6DataCall = 0;

      MPMAIN_MediaDisconnect(pAdapter, TRUE);

      // generate fake pkt-service-status ind to external WDS clients
      MPQMUX_BroadcastWdsPktSrvcStatusInd
      (
         pAdapter,
         0x01, // WDS_PKT_DATAC_DISCONNECTED
         0
      );

      #ifdef MP_QCQOS_ENABLED
      MPQOS_PurgeQueues(pAdapter);
      MPQOS_EnableDefaultFlow(pAdapter, TRUE); // Reset
      #endif // MP_QCQOS_ENABLED

      MPMAIN_ResetPacketCount(pAdapter);
      if (TRUE == DisconnectedAllAdapters(pAdapter, &returnAdapter))
      {
         USBIF_PollDevice(returnAdapter->USBDo, FALSE);
      }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> MPMAIN_DisconnectNotification: clear clients\n", pAdapter->PortName)
   );
   // Release internal client id
   //pAdapter->QMIType  = 0;
   //pAdapter->ClientId = 0;
   RtlZeroMemory((PVOID)pAdapter->ClientId, (QMUX_TYPE_MAX+1));

   // reset the PendingCtrlRequests
   NdisZeroMemory(&(pAdapter->PendingCtrlRequests), sizeof(pAdapter->PendingCtrlRequests));

#ifdef MP_QCQOS_ENABLED
   MPQOSC_CancelQmiQosClient(pAdapter); 
   MPQOS_CancelQosThread(pAdapter); 
#endif // MP_QCQOS_ENABLED

   // stop the IPv6 client
   MPIP_CancelWdsIpClient(pAdapter);

   // reset QMI initialization
   pAdapter->QmiInitialized = FALSE;
   pAdapter->IsQmiWorking = FALSE;

   MPIOC_InvalidateClients(pAdapter);

} // MPMAIN_DisconnectNotification

#ifdef NDIS620_MINIPORT

VOID SignalStateTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)FunctionContext;
   NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> SignalStateTimerDpc\n", pAdapter->PortName)
   );

   NdisAcquireSpinLock(&pAdapter->QMICTLLock);

   // only query module if radio is on
   if (pAdapter->DeviceRadioState == DeviceWWanRadioOn)
   {
      MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_NAS, 
                          QMINAS_GET_SIGNAL_STRENGTH_REQ, NULL, TRUE );
   }
   if (pAdapter->nSignalStateTimerCount > 0)
   {
      NdisSetTimer( &pAdapter->SignalStateTimer, pAdapter->nSignalStateTimerCount );
      NdisReleaseSpinLock(&pAdapter->QMICTLLock);
      return;
   }

   NdisReleaseSpinLock(&pAdapter->QMICTLLock);
   QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 222)
  
}  

VOID SignalStateDisconnectTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)FunctionContext;
   NDIS_LINK_STATE LinkState;
   NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> SignalStateDisconnectTimerDpc\n", pAdapter->PortName)
   );

   NdisAcquireSpinLock(&pAdapter->QMICTLLock);

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

   pAdapter->nSignalStateDisconnectTimerCalled = 1;
   NdisReleaseSpinLock(&pAdapter->QMICTLLock);
   QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 222)
  
}  

VOID MsisdnTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)FunctionContext;
   NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> MsisdnTimerDpc\n", pAdapter->PortName)
   );

   NdisAcquireSpinLock(&pAdapter->QMICTLLock);

   MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_DMS, 
                       QMIDMS_UIM_GET_IMSI_REQ, NULL, TRUE );

   /* Call this to send the unsolicited SMS conf ind */
   if ( strlen( pAdapter->SmsConfiguration.SmsConfiguration.ScAddress)  == 0 )
   {
      MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WMS, 
                             QMIWMS_GET_SMSC_ADDRESS_REQ, NULL, TRUE );
   }
   else
   {
      MPQMUX_ComposeQMUXReq( pAdapter, NULL, QMUX_TYPE_WMS, 
                             QMIWMS_GET_STORE_MAX_SIZE_REQ, MPQMUX_ComposeWmsGetStoreMaxSizeReqSend, TRUE );
      
   }

   pAdapter->MsisdnTimerValue = 0;
   pAdapter->MsisdnTimerCount++;
   
   QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 444)

   NdisReleaseSpinLock(&pAdapter->QMICTLLock);
  
}  

VOID RegisterPacketTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)FunctionContext;
   PMP_OID_WRITE pOID = (PMP_OID_WRITE)pAdapter->RegisterPacketTimerContext;
   NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> RegisterPacketTimerDpc\n", pAdapter->PortName)
   );

   NdisAcquireSpinLock(&pAdapter->QMICTLLock);

   if ( pOID != NULL)
   {
      if (pAdapter->IsNASSysInfoPresent == FALSE)
      {
      MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                             QMINAS_GET_SERVING_SYSTEM_REQ, NULL, TRUE );
      }
      else
      {
          MPQMUX_ComposeQMUXReq( pAdapter, pOID, QMUX_TYPE_NAS, 
                         QMINAS_GET_SYS_INFO_REQ, NULL, TRUE );
      }
      
      pAdapter->RegisterPacketTimerContext = NULL;
   }

   NdisReleaseSpinLock(&pAdapter->QMICTLLock);
   QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 222)
  
}  

#endif

NTSTATUS MPMAIN_GetDeviceInstance(PMP_ADAPTER pAdapter)
{
   NTSTATUS nts = STATUS_UNSUCCESSFUL;
   ULONG    bufLen = 512, resultLen = 0;
   CHAR     driverKey[512];

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->_GetDeviceInstance for 0x%p\n", pAdapter->PortName, pAdapter->Fdo)
   );

   if (pAdapter->Pdo == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> <--_GetDeviceInstance: NULL FDO\n", pAdapter->PortName)
      );
      return nts;
   }

   driverKey[0] = 0;
   nts = IoGetDeviceProperty
         (
            pAdapter->Pdo,
            DevicePropertyDriverKeyName,
            bufLen,
            (PVOID)driverKey,
            &resultLen
         );

   if (nts == STATUS_SUCCESS)
   {
      int i, shifts = 0;
      USHORT v;
      PCHAR p;

      // extract driver key
      for (i = 0; i < resultLen; i++)
      {
         if (driverKey[i] == 0)
         {
            driverKey[i] = ' ';
         }
      }
      driverKey[i] = 0;

      // extract instance ID into 2-byte word
      pAdapter->DeviceInstance = 0;
      p = &(driverKey[i]);
      while ((p > driverKey) && (shifts < 16))
      {
         if ((*p >= '0') && (*p <='9'))
         {
            v = *p - '0';
            pAdapter->DeviceInstance |= (v << shifts);
            shifts += 4;
         }
         --p;
      }

      pAdapter->DeviceInstanceObtained = TRUE;            
   }
   else
   {
      pAdapter->DeviceInstanceObtained = FALSE;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--_GetDeviceInstance: 0x%04x (ST 0x%X)\n", pAdapter->PortName, pAdapter->DeviceInstance, nts)
   );

   return nts;

}  // MPMAIN_GetDeviceInstance

NTSTATUS MPMAIN_GetDeviceFriendlyName(PMP_ADAPTER pAdapter)
{
   NTSTATUS nts = STATUS_UNSUCCESSFUL;
   ULONG    bufLen = 510, resultLen = 0;
   WCHAR    driverKey[512];

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPMAIN_GetDeviceFriendlyName for 0x%p\n", pAdapter->PortName, pAdapter->Fdo)
   );

   if (pAdapter->Pdo == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> <--MPMAIN_GetDeviceFriendlyName: NULL FDO\n", pAdapter->PortName)
      );
      return nts;
   }

   RtlZeroMemory(driverKey, bufLen*sizeof(WCHAR));
   RtlZeroMemory(pAdapter->FriendlyName, 256);
   strcpy(pAdapter->FriendlyName, "QUALCOMM INC WWAN");
   
   nts = IoGetDeviceProperty
         (
            pAdapter->Pdo,
            DevicePropertyFriendlyName,
            bufLen,
            (PVOID)driverKey,
            &resultLen
         );

   if (nts == STATUS_SUCCESS)
   {
      ANSI_STRING FriendlyNameA;
      UNICODE_STRING FriendlyNameW;
      RtlInitUnicodeString(&FriendlyNameW, driverKey);
      RtlUnicodeStringToAnsiString(&FriendlyNameA, &FriendlyNameW, TRUE);
      if (FriendlyNameA.Length <= 255)
      {
         strcpy(pAdapter->FriendlyName, FriendlyNameA.Buffer);
      }
      pAdapter->FriendlyNameWLen = resultLen;
      RtlCopyMemory(pAdapter->FriendlyNameW, driverKey, resultLen);
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_DETAIL,
         ("<%s> MPMAIN_GetDeviceFriendlyName: (%s)\n", pAdapter->PortName, pAdapter->FriendlyName)
      );
      
      RtlFreeAnsiString( &FriendlyNameA );
   }
   else if (nts != STATUS_BUFFER_TOO_SMALL)
   {
       nts = IoGetDeviceProperty
             (
                pAdapter->Pdo,
                DevicePropertyDeviceDescription,
                bufLen,
                (PVOID)driverKey,
                &resultLen
             );
       
       if (nts == STATUS_SUCCESS)
       {
          ANSI_STRING FriendlyNameA;
          UNICODE_STRING FriendlyNameW;
          RtlInitUnicodeString(&FriendlyNameW, driverKey);
          RtlUnicodeStringToAnsiString(&FriendlyNameA, &FriendlyNameW, TRUE);
          if (FriendlyNameA.Length <= 255)
          {
             strcpy(pAdapter->FriendlyName, FriendlyNameA.Buffer);
          }
          pAdapter->FriendlyNameWLen = resultLen;
          RtlCopyMemory(pAdapter->FriendlyNameW, driverKey, resultLen);
          QCNET_DbgPrint
          (
             MP_DBG_MASK_CONTROL,
             MP_DBG_LEVEL_DETAIL,
             ("<%s> MPMAIN_GetDeviceFriendlyName(DeviceDesc): (%s)\n", pAdapter->PortName, pAdapter->FriendlyName)
          );
          
          RtlFreeAnsiString( &FriendlyNameA );
       }
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPMAIN_GetDeviceFriendlyName: (ST 0x%X)\n", pAdapter->PortName, nts)
   );

   return nts;

}  // MPMAIN_GetDeviceFriendlyName

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QC_DUAL_IP_FC

VOID MPMAIN_FlowControlCall(PMP_ADAPTER pAdapter, UCHAR IPVer, BOOLEAN FlowControlled)
{
   PLIST_ENTRY fcQueue, pList;
   LIST_ENTRY  tmpQueue;
   ULONG       dataLen = 0;
   PUCHAR      pkt;
   UCHAR       pktIpVer;

   NdisInitializeListHead(&tmpQueue);

   if (IPVer == 4)
   {
      fcQueue = &pAdapter->FlowControlListV4;
   }
   else
   {
      fcQueue = &pAdapter->FlowControlListV6;
   }

   NdisAcquireSpinLock(&pAdapter->TxLock);

   if (FlowControlled == TRUE)
   {
      // move IP pkts from TxPendingList to fcQueue
      while (!IsListEmpty(&pAdapter->TxPendingList))
      {
         pList = RemoveHeadList(&pAdapter->TxPendingList);

#ifndef NDIS60_MINIPORT
         // analyze pkt in pList, borrow QosPacketBuffer here
         dataLen = MPUSB_CopyNdisPacketToBuffer
                   (
                      ListItemToPacket(pList),
                      pAdapter->QosPacketBuffer,
                      0
                   );
#endif
         if (dataLen > 14)
         {
            pkt = pAdapter->QosPacketBuffer;
            pkt += 14; // skip ETH header
            pktIpVer = (*pkt & 0xF0) >> 4;
 
            if (pktIpVer == IPVer) // pause
            {
               InsertTailList(fcQueue, pList);
            }
            else
            {
               InsertTailList(&tmpQueue, pList);
            }
         }
         else
         {
            InsertTailList(&tmpQueue, pList);
         }
      }

      while (!IsListEmpty(&tmpQueue))
      {
         pList = RemoveHeadList(&tmpQueue);
         InsertTailList(&pAdapter->TxPendingList, pList);
      }
   }
   else
   {
      // move pkts from fcQueue to TxPendingList
      while (!IsListEmpty(fcQueue))
      {
         pList = RemoveTailList(fcQueue);
         InsertHeadList(&pAdapter->TxPendingList, pList);
      }
   }

   // set flag
   if (IPVer == 4)
   {
      pAdapter->FlowControlV4 =  FlowControlled;
   }
   else
   {
      pAdapter->FlowControlV6 =  FlowControlled;
   }

   NdisReleaseSpinLock(&pAdapter->TxLock);

   KeSetEvent(&pAdapter->MPThreadTxEvent, IO_NO_INCREMENT, FALSE);

}  // MPMAIN_FlowControlCall

// this is called within a spin lock
BOOLEAN MPMAIN_IsFlowControlledData(PMP_ADAPTER pAdapter, PLIST_ENTRY pList)
{
   ULONG dataLen = 0;
   PUCHAR pkt;
   UCHAR  pktIpVer;

#ifndef NDIS60_MINIPORT

   dataLen = MPUSB_CopyNdisPacketToBuffer
             (
                ListItemToPacket(pList),
                pAdapter->QosPacketBuffer,
                0
             );
#endif

   if (dataLen > 14)
   {
      pkt = pAdapter->QosPacketBuffer;
      pkt += 14; // skip ETH header
      pktIpVer = (*pkt & 0xF0) >> 4;

      if ((pktIpVer == 4) && (pAdapter->FlowControlV4 == TRUE))
      {
         InsertTailList(&pAdapter->FlowControlListV4, pList);
         return TRUE;
      }
      if ((pktIpVer == 6) && (pAdapter->FlowControlV6 == TRUE))
      {
         InsertTailList(&pAdapter->FlowControlListV6, pList);
         return TRUE;
      }
   }
   return FALSE;
}  // MPMAIN_IsFlowControlledData

VOID MPMAIN_PurgeFlowControlledPackets(PMP_ADAPTER pAdapter, BOOLEAN InQueueCompletion)
{
   PNDIS_PACKET pNdisPacket;
   PLIST_ENTRY  pList;
   NDIS_STATUS  ndisStatus = NDIS_STATUS_FAILURE;
   int          numQueued = 0;
#ifdef NDIS60_MINIPORT   
   PMPUSB_TX_CONTEXT_NB txNb;
#endif

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPMAIN_PurgeFlowControlledPackets: nBusyTx %d\n", pAdapter->PortName, pAdapter->nBusyTx)
   );

   NdisAcquireSpinLock(&pAdapter->TxLock);

   while (!IsListEmpty(&pAdapter->FlowControlListV4))
   {
      pList = RemoveHeadList(&pAdapter->FlowControlListV4);
#ifndef NDIS60_MINIPORT   
      pNdisPacket = ListItemToPacket(pList);
      NDIS_SET_PACKET_STATUS(pNdisPacket, ndisStatus); // not necessary?
      if (InQueueCompletion == TRUE)
      {
         InsertTailList(&pAdapter->TxCompleteList,  pList);
         ++numQueued;
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> _PurgeFlowControlledPackets: V4 0x%p\n", pAdapter->PortName, pNdisPacket)
         );
         NdisMSendComplete(pAdapter->AdapterHandle, pNdisPacket, ndisStatus);
         InterlockedDecrement(&(pAdapter->nBusyTx)); // decremented when processing TxCompleteList
      }
#else
     txNb = CONTAINING_RECORD
             (
                pList,
                MPUSB_TX_CONTEXT_NB,
                List
             );

     ((PMPUSB_TX_CONTEXT_NBL)txNb->NBL)->NdisStatus = NDIS_STATUS_FAILURE;
      MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, txNb, FALSE); 
      InterlockedDecrement(&(pAdapter->nBusyTx)); // decremented when processing TxCompleteList
#endif
   }

   while (!IsListEmpty(&pAdapter->FlowControlListV6))
   {
      pList = RemoveHeadList(&pAdapter->FlowControlListV6);
#ifndef NDIS60_MINIPORT
      pNdisPacket = ListItemToPacket(pList);
      NDIS_SET_PACKET_STATUS(pNdisPacket, ndisStatus); // not necessary?
      if (InQueueCompletion == TRUE)
      {
         InsertTailList(&pAdapter->TxCompleteList,  pList);
         ++numQueued;
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> _PurgeFlowControlledPackets: V6 0x%p\n", pAdapter->PortName, pNdisPacket)
         );
         NdisMSendComplete(pAdapter->AdapterHandle, pNdisPacket, ndisStatus);
         InterlockedDecrement(&(pAdapter->nBusyTx)); // decremented when processing TxCompleteList
      }
#else
      txNb = CONTAINING_RECORD
              (
                 pList,
                 MPUSB_TX_CONTEXT_NB,
                 List
              );
      
      ((PMPUSB_TX_CONTEXT_NBL)txNb->NBL)->NdisStatus = NDIS_STATUS_FAILURE;
       MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, txNb, FALSE); 
      InterlockedDecrement(&(pAdapter->nBusyTx)); // decremented when processing TxCompleteList
#endif

   }

   NdisReleaseSpinLock(&pAdapter->TxLock);

   if (numQueued != 0)
   {
      MPWork_ScheduleWorkItem(pAdapter);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPMAIN_PurgeFlowControlledPackets: nBusyTx %d\n", pAdapter->PortName, pAdapter->nBusyTx)
   );
}  // MPMAIN_PurgeFlowControlledPackets

#endif // QC_DUAL_IP_FC

#endif // QCUSB_MUX_PROTOCOL

#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)

VOID MPMAIN_QMAPFlowControlCall(PMP_ADAPTER pAdapter, BOOLEAN FlowControlled)
{
   PLIST_ENTRY fcQueue, pList;
   fcQueue = &pAdapter->QMAPFlowControlList;

   NdisAcquireSpinLock(&pAdapter->TxLock);

   if (FlowControlled == TRUE)
   {
      // move IP pkts from TxPendingList to fcQueue
      while (!IsListEmpty(&pAdapter->TxPendingList))
      {
         pList = RemoveHeadList(&pAdapter->TxPendingList);
         InsertTailList(fcQueue, pList);
      }
   }
   else
   {
      // move pkts from fcQueue to TxPendingList
      while (!IsListEmpty(fcQueue))
      {
         pList = RemoveTailList(fcQueue);
         InsertHeadList(&pAdapter->TxPendingList, pList);
      }
   }

   // set flag
   pAdapter->QMAPFlowControl =  FlowControlled;

   NdisReleaseSpinLock(&pAdapter->TxLock);

   KeSetEvent(&pAdapter->MPThreadTxEvent, IO_NO_INCREMENT, FALSE);

}  // MPMAIN_FlowControlCall

// this is called within a spin lock
BOOLEAN MPMAIN_QMAPIsFlowControlledData(PMP_ADAPTER pAdapter, PLIST_ENTRY pList)
{
   if (pAdapter->QMAPFlowControl == TRUE)
   {
      InsertTailList(&pAdapter->QMAPFlowControlList, pList);
      return TRUE;
   }
   return FALSE;
}  // MPMAIN_IsFlowControlledData

VOID MPMAIN_QMAPPurgeFlowControlledPackets(PMP_ADAPTER pAdapter, BOOLEAN InQueueCompletion)
{
   PNDIS_PACKET pNdisPacket;
   PLIST_ENTRY  pList;
   NDIS_STATUS  ndisStatus = NDIS_STATUS_FAILURE;
   int          numQueued = 0;
#ifdef NDIS60_MINIPORT   
   PMPUSB_TX_CONTEXT_NB txNb;
#endif

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPMAIN_PurgeFlowControlledPackets: nBusyTx %d\n", pAdapter->PortName, pAdapter->nBusyTx)
   );

   NdisAcquireSpinLock(&pAdapter->TxLock);

   while (!IsListEmpty(&pAdapter->QMAPFlowControlList))
   {
      pList = RemoveHeadList(&pAdapter->QMAPFlowControlList);
#ifndef NDIS60_MINIPORT   
      pNdisPacket = ListItemToPacket(pList);
      NDIS_SET_PACKET_STATUS(pNdisPacket, ndisStatus); // not necessary?
      if (InQueueCompletion == TRUE)
      {
         InsertTailList(&pAdapter->TxCompleteList,  pList);
         ++numQueued;
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> _PurgeFlowControlledPackets: 0x%p\n", pAdapter->PortName, pNdisPacket)
         );
         NdisMSendComplete(pAdapter->AdapterHandle, pNdisPacket, ndisStatus);
         InterlockedDecrement(&(pAdapter->nBusyTx)); // decremented when processing TxCompleteList
      }
#else
      txNb = CONTAINING_RECORD
              (
                 pList,
                 MPUSB_TX_CONTEXT_NB,
                 List
              );
      
      ((PMPUSB_TX_CONTEXT_NBL)txNb->NBL)->NdisStatus = NDIS_STATUS_FAILURE;
       MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, txNb, FALSE); 
      InterlockedDecrement(&(pAdapter->nBusyTx)); // decremented when processing TxCompleteList
#endif
   }

   NdisReleaseSpinLock(&pAdapter->TxLock);

   if (numQueued != 0)
   {
      MPWork_ScheduleWorkItem(pAdapter);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPMAIN_PurgeFlowControlledPackets: nBusyTx %d\n", pAdapter->PortName, pAdapter->nBusyTx)
   );
}  // MPMAIN_PurgeFlowControlledPackets

#endif // defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)

BOOLEAN SetTimerResolution ( PMP_ADAPTER pAdapter, BOOLEAN Flag )
{
   if (Flag == TRUE)
   {
      if (InterlockedCompareExchange(&TimerResolution, 1, 0) == 0)
      {
         ULONG returnVal = 0;
         // Set the timer resolution
         returnVal = ExSetTimerResolution(10000, TRUE);
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPth: MEDIA_CONN: TransmitTimer ExSetTimerResolution SET returned %d\n", pAdapter->PortName, returnVal)
         );
      }
   }
   else
   {
      if (InterlockedCompareExchange(&TimerResolution, 0, 1) > 0)
      {
         ULONG returnVal = 0;
         // Reset the timer resolution
         returnVal = ExSetTimerResolution(0, FALSE);
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
           ("<%s> media disconnect: TransmitTimer ExSetTimerResolution RESET returned %d\n", pAdapter->PortName, returnVal)
         );
      }
   }
   return TRUE;
}

BOOLEAN MoveQMIRequests( PMP_ADAPTER pAdapter )
{
   NdisAcquireSpinLock( &pAdapter->CtrlWriteLock);
   while ( !IsListEmpty( &pAdapter->CtrlWritePendingList ) )
   {
      PLIST_ENTRY listEntry;
      PQCWRITEQUEUE QMIElement;
      PQCQMI QMI;
      listEntry = RemoveHeadList( &pAdapter->CtrlWritePendingList );
      QMIElement = CONTAINING_RECORD( listEntry, QCWRITEQUEUE, QCWRITERequest);
      QMI = (PQCQMI)QMIElement->Buffer;
      
      if ( (QMI->QMIType != QMUX_TYPE_CTL) && (QMI->ClientId == 0x00))
      {
         if (pAdapter->ClientId[QMI->QMIType] != 0x00)
         {
            QMI->ClientId = pAdapter->ClientId[QMI->QMIType];
         }
         else
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL,
               MP_DBG_LEVEL_DETAIL,
              ("<%s> MoveQMIRequests: Client ID should never be 0\n", pAdapter->PortName)
            );
         }
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
           ("<%s> MoveQMIRequests: Unexpected Client ID/Type , Type:%d, Id:%d\n", pAdapter->PortName, QMI->QMIType, QMI->ClientId)
         );
      }
      InsertTailList( &pAdapter->CtrlWriteList, &(QMIElement->QCWRITERequest));
   }
   NdisReleaseSpinLock( &pAdapter->CtrlWriteLock );
   MPWork_ScheduleWorkItem( pAdapter ); 
   return TRUE;
}
