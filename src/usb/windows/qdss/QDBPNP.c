/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B P N P . C

GENERAL DESCRIPTION
  This is the file which contains PnP function for QDSS driver.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdio.h>
#include <wdm.h>
#include <ntstrsafe.h>
#include "QDBMAIN.h"
#include "QDBPNP.h"
#include "QDBDEV.h"
#include "QDBDSP.h"
#include "QDBRD.h"
#include "QDBWT.h"

#ifdef EVENT_TRACING
#include "QDBPNP.tmh"
#endif

NTSTATUS QDBPNP_EvtDriverDeviceAdd
(
   WDFDRIVER        Driver,
   PWDFDEVICE_INIT  DeviceInit
)
{
   WDFDEVICE                    wdfDevice;
   WDF_OBJECT_ATTRIBUTES        devAttrib;
   PDEVICE_CONTEXT              pDevContext;
   NTSTATUS                     ntStatus;
   WDF_FILEOBJECT_CONFIG        fileSettings;
   WDF_OBJECT_ATTRIBUTES        fileAttrib;
   WDF_IO_QUEUE_CONFIG          queueSettings;
   WDF_OBJECT_ATTRIBUTES        reqAttrib;
   WDF_DEVICE_PNP_CAPABILITIES  pnpCapabilities;
   WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerEventCB;

   UNREFERENCED_PARAMETER(Driver);

   QDB_DbgPrintG
   (
      0, 0,
      ("-->QDBPNP_EvtDriverDeviceAdd: driver 0x%p\n", Driver)
   );

   InterlockedIncrement(&DevInstanceNumber);

   // 1. PnP, power, and power policy callback functions
   WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerEventCB);
   pnpPowerEventCB.EvtDevicePrepareHardware = QDBPNP_EvtDevicePrepareHW;
   WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerEventCB);

   // 2. File event callback functions
   WDF_FILEOBJECT_CONFIG_INIT
   (
      &fileSettings,
      QDBDEV_EvtDeviceFileCreate,
      QDBDEV_EvtDeviceFileClose,
      QDBDEV_EvtDeviceFileCleanup
   );
   WDF_OBJECT_ATTRIBUTES_INIT(&fileAttrib);
   WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&fileAttrib, FILE_CONTEXT);
   WdfDeviceInitSetFileObjectConfig
   (
      DeviceInit,
      &fileSettings,
      &fileAttrib
   );

   // 3. I/O request attributes
   WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&reqAttrib, REQUEST_CONTEXT);
   WdfDeviceInitSetRequestAttributes(DeviceInit, &reqAttrib);

   // 4. Device characteristics
   WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

   // 5. Device object with extension/context
   WDF_OBJECT_ATTRIBUTES_INIT(&devAttrib);
   WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&devAttrib, DEVICE_CONTEXT);
   devAttrib.EvtCleanupCallback = QDBPNP_EvtDeviceCleanupCallback;

   // Create Device
   ntStatus = WdfDeviceCreate(&DeviceInit, &devAttrib, &wdfDevice);
   if (!NT_SUCCESS(ntStatus))
   {
       QDB_DbgPrintG
       (
          0, 0,
          ("WdfDeviceCreate failed ST 0x%X\n", ntStatus)
       );
       return ntStatus;
   }

   pDevContext = QdbDeviceGetContext(wdfDevice);
   sprintf(pDevContext->PortName, "%s%04X", QDB_DBG_NAME_PREFIX, DevInstanceNumber);
   pDevContext->MaxXfrSize = QDB_USB_TRANSFER_SIZE_MAX;

   // TODO: for debug purpose
   pDevContext->DebugMask = 0x0;
   pDevContext->DebugLevel = QDB_DBG_LEVEL_FORCE;

   // 6. PnP Capabilities
   WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCapabilities);
   pnpCapabilities.SurpriseRemovalOK = TRUE;
   WdfDeviceSetPnpCapabilities(wdfDevice, &pnpCapabilities);

   // 7. Queues
   // default queue
   WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE
   (
      &queueSettings,
      WdfIoQueueDispatchParallel
   );
   queueSettings.EvtIoDeviceControl = QDBDSP_IoDeviceControl;
   queueSettings.EvtIoStop = QDBDSP_IoStop;
   queueSettings.EvtIoResume = QDBDSP_IoResume;

   ntStatus = WdfIoQueueCreate
              (
                 wdfDevice,
                 &queueSettings,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &pDevContext->DefaultQueue
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrintG
      (
         0, 0,
         ("WdfIoQueueCreate default failed ST 0x%X\n", ntStatus)
      );
      return ntStatus;
   }

   // read queue
   WDF_IO_QUEUE_CONFIG_INIT(&queueSettings, WdfIoQueueDispatchParallel);
   queueSettings.EvtIoRead = QDBRD_IoRead;
   queueSettings.EvtIoStop = QDBDSP_IoStop;
   queueSettings.EvtIoResume = QDBDSP_IoResume;

   ntStatus = WdfIoQueueCreate
              (
                 wdfDevice,
                 &queueSettings,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &pDevContext->RxQueue
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrintG
      (
         0, 0,
         ("WdfIoQueueCreate RX queue failed ST 0x%X\n", ntStatus)
      );
      return ntStatus;
   }

   ntStatus = WdfDeviceConfigureRequestDispatching
              (
                 wdfDevice,
                 pDevContext->RxQueue,
                 WdfRequestTypeRead
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrintG
      (
         0, 0,
         ("WdfDeviceConfigureRequestDispatching RX failure 0x%x\n", ntStatus)
      );
      return ntStatus;
   }

   // write queue
   WDF_IO_QUEUE_CONFIG_INIT(&queueSettings, WdfIoQueueDispatchParallel);
   queueSettings.EvtIoWrite = QDBWT_IoWrite;
   queueSettings.EvtIoStop  = QDBDSP_IoStop;
   queueSettings.EvtIoResume = QDBDSP_IoResume;

   ntStatus = WdfIoQueueCreate
              (
                 wdfDevice,
                 &queueSettings,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &pDevContext->TxQueue
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrintG
      (
         0, 0,
         ("WdfIoQueueCreate TX queue failed ST 0x%X\n", ntStatus)
      );
      return ntStatus;
   }

   ntStatus = WdfDeviceConfigureRequestDispatching
              (
                 wdfDevice,
                 pDevContext->TxQueue,
                 WdfRequestTypeWrite
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrintG
      (
         0, 0,
         ("WdfDeviceConfigureRequestDispatching TX failure 0x%x\n", ntStatus)
      );
      return ntStatus;
   }


   // 8. Symbolic link to friendly name
   pDevContext->PhysicalDeviceObject = WdfDeviceWdmGetPhysicalDevice(wdfDevice);
   ntStatus = QDBPNP_CreateSymbolicName(wdfDevice);

   if (!NT_SUCCESS(ntStatus))
   {
      // register a wdfDevice IF for apps
      ntStatus = WdfDeviceCreateDeviceInterface
                 (
                    wdfDevice,
                    (LPGUID)&QDBUSB_GUID,
                    NULL    // Reference String
                 );
      if (!NT_SUCCESS(ntStatus))
      {
         QDB_DbgPrintG
         (
            0, 0,
            ("WdfDeviceCreateDeviceInterface failed ST 0x%X\n", ntStatus)
         );
         return ntStatus;
      }
   }

   // 9. Anything else

   QDBPNP_ReadDebugMask(wdfDevice);

   QDB_DbgPrintG
   (
      0, 0,
      ("<--QDBPNP_EvtDriverDeviceAdd: 0x%p\n", wdfDevice)
   );

   return ntStatus;
} 


NTSTATUS QDBPNP_ReadDebugMask(WDFDEVICE QCDevice)
{
    WDFKEY      hKey = NULL;
    NTSTATUS    QCstatus;
    ULONG       QCvalue = 0;
    UNICODE_STRING unicodeStr;
    PDEVICE_CONTEXT pDevContext;

    pDevContext = QdbDeviceGetContext(QCDevice);

    QCstatus = WdfDeviceOpenRegistryKey(QCDevice,
        PLUGPLAY_REGKEY_DRIVER,
        STANDARD_RIGHTS_READ,
        NULL,
        &hKey);

    if (NT_SUCCESS(QCstatus))
    {
        RtlInitUnicodeString(&unicodeStr, L"QCDriverDebugMask");
        QCstatus = WdfRegistryQueryULong(hKey, &unicodeStr, &QCvalue);
        pDevContext->DebugMask = QCvalue;
        pDevContext->DebugLevel = (UCHAR)(QCvalue & 0x0F);

        QDB_DbgPrintG
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBPNP_ReadDebugMask: DebugMask 0x%x\n", pDevContext->PortName, QCvalue)
        );

    }
    else
    {
        QDB_DbgPrintG
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> <--QDBPNP_ReadDebugMask: error 0x%x\n", pDevContext->PortName, QCstatus)
        );
    }

    WdfRegistryClose(hKey);
    hKey = NULL;

    return QCstatus;
}

VOID QDBPNP_EvtDriverCleanupCallback(WDFOBJECT Object)
{
   PDRIVER_OBJECT driver;

   driver = (PDRIVER_OBJECT)Object;

   QDB_DbgPrintG
   (
      0, 0,
      ("-->QDBPNP_EvtDriverCleanupCallback: Driver 0x%p\n", driver)
   );

   QDB_DbgPrintG
   (
      0, 0,
      ("<--QDBPNP_EvtDriverCleanupCallback Driver 0x%p\n", driver)
   );

#ifdef EVENT_TRACING
    WPP_CLEANUP(driver);
#endif

   return;
}  // QDBPNP_EvtDriverCleanupCallback

VOID QDBPNP_EvtDeviceCleanupCallback(WDFOBJECT Object)
{
   WDFDEVICE       wdfDevice;
   PDEVICE_CONTEXT pDevContext;

   wdfDevice = (WDFDEVICE)Object;
   pDevContext = QdbDeviceGetContext(wdfDevice);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_DETAIL,
      ("<%s> -->QDBPNP_EvtDeviceCleanupCallback: 0x%p\n", pDevContext->PortName, wdfDevice)
   );

   pDevContext->DeviceRemoval = TRUE;
   pDevContext->PipeDrain = FALSE;

   if (pDevContext->SymbolicLink.Buffer != NULL)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBPNP_EvtDeviceCleanupCallback: free SymbolicLink\n", pDevContext->PortName)
      );
      ExFreePool(pDevContext->SymbolicLink.Buffer);
      pDevContext->SymbolicLink.Buffer = NULL;
      pDevContext->SymbolicLink.Length = 0;
   }
   QDBPNP_SetStamp(pDevContext->PhysicalDeviceObject, 0, 0);
   QDBPNP_SetFunctionProtocol(wdfDevice, 0);

   QDBPNP_WaitForDrainToStop(pDevContext);
   QDBRD_FreeIoBuffer(pDevContext);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--QDBPNP_EvtDeviceCleanupCallback: 0x%p\n", pDevContext->PortName, wdfDevice)
   );

   return;
}  // QDBPNP_EvtDeviceCleanupCallback

VOID QDBPNP_WaitForDrainToStop(PDEVICE_CONTEXT pDevContext)
{
   LARGE_INTEGER   delayValue;
   NTSTATUS        ntStatus;

   delayValue.QuadPart = -(1 * 1000 * 1000); // 0.1 sec
   ntStatus = KeWaitForSingleObject
              (
                 &pDevContext->DrainStoppedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 &delayValue
              );
   if (ntStatus == STATUS_TIMEOUT)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBPNP_WaitForDrainToStop: wait for drain to stop: %d\n",
           pDevContext->PortName, pDevContext->Stats.DrainingRx)
      );
      while (pDevContext->Stats.DrainingRx != 0)
      {
         QDB_DbgPrint
         (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> QDBPNP_WaitForDrainToStop: wait for drain to stop\n", pDevContext->PortName)
         );
         delayValue.QuadPart = -(1 * 1000 * 1000); // 0.1 sec
         KeWaitForSingleObject
         (
            &pDevContext->DrainStoppedEvent,
            Executive,
            KernelMode,
            FALSE,
            &delayValue
         );
      }
   }
}  // QDBPNP_WaitForDrainToStop

// Configure USB device
NTSTATUS QDBPNP_EvtDevicePrepareHW
(
   WDFDEVICE    Device,
   WDFCMRESLIST ResourceList,
   WDFCMRESLIST ResourceListTranslated
)
{
   NTSTATUS                    ntStatus, nts;
   PDEVICE_CONTEXT             pDevContext;

   UNREFERENCED_PARAMETER(ResourceList);
   UNREFERENCED_PARAMETER(ResourceListTranslated);

   pDevContext = QdbDeviceGetContext(Device);

   QDB_DbgPrint
   (
      0, 0,
      ("<%s> -->QDBPNP_EvtDevicePrepareHW: 0x%p\n", pDevContext->PortName, Device)
   );

   // Init context
   KeInitializeEvent(&pDevContext->DrainStoppedEvent, SynchronizationEvent, FALSE);
   pDevContext->DeviceRemoval = FALSE;
   pDevContext->TraceIN = NULL;
   pDevContext->DebugIN = NULL;
   pDevContext->DebugOUT = NULL;
   pDevContext->UsbInterfaceTRACE = NULL;
   pDevContext->UsbInterfaceDEBUG = NULL;
   pDevContext->RxCount = 0;
   pDevContext->TxCount = 0;
   pDevContext->MyIoTarget = WdfDeviceGetIoTarget(Device);
   pDevContext->PhysicalDeviceObject = WdfDeviceWdmGetPhysicalDevice(Device);
   pDevContext->MyDevice = WdfDeviceWdmGetDeviceObject(Device);
   pDevContext->TargetDevice = WdfIoTargetWdmGetTargetDeviceObject(pDevContext->MyIoTarget);

   // TODO: for DPL only
   pDevContext->PipeDrain = FALSE;
   pDevContext->Stats.OutstandingRx = 0;
   pDevContext->Stats.DrainingRx    = 0;
   pDevContext->Stats.NumRxExhausted = 0;
   pDevContext->Stats.BytesDrained = 0;
   pDevContext->Stats.PacketsDrained = 0;
   pDevContext->Stats.IoFailureCount = 0;

   // Device descriptor
   ntStatus = QDBPNP_EnumerateDevice(Device);

   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrint
      (
         0, 0,
         ("<%s> QDBPNP_EvtDevicePrepareHW: ReadandSelectDescriptors failed\n", pDevContext->PortName)
      );
      return ntStatus;
   }

   nts = QDBPNP_EnableSelectiveSuspend(Device);

   QDBMAIN_GetRegistrySettings(Device);
   QDBPNP_SetStamp(pDevContext->PhysicalDeviceObject, 0, 1);

   // Start to drain IN pipe for DPL
   QDBRD_AllocateRequestsRx(pDevContext);
   QDBRD_PipeDrainStart(pDevContext);

   QDB_DbgPrint
   (
      0, 0,
      ("<%s> <--QDBPNP_EvtDevicePrepareHW: 0x%p (SelectiveSuspend status 0x%x)\n", pDevContext->PortName, Device, nts)
   );

   return ntStatus;
}  // QDBPNP_EvtDevicePrepareHW

NTSTATUS QDBPNP_GetCID
(
   WDFDEVICE Device,
   PCHAR  ProductString,
   USHORT ProductStrLen,  // value 0 means to clear/remove the reg entry
   HANDLE hRegKey
)
{
   PDEVICE_CONTEXT  pDevContext;
   NTSTATUS ntStatus;
   PCHAR pCidLoc = NULL;
   UNICODE_STRING ucValueName;
   INT            strLen = 0;
   BOOLEAN        bSetEntry = FALSE;

   pDevContext = QdbDeviceGetContext(Device);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> -->QDBPNP_GetCID: Dev 0x%p\n", pDevContext->PortName, pDevContext)
   );

   if (ProductStrLen == 0)
   {
      goto UpdateRegistry;
   }

   pCidLoc = ProductString;

   // search for "_CID:"
   if (ProductStrLen > 0)
   {
      INT idx, adjusted = 0;
      PCHAR p = pCidLoc;
      BOOLEAN bMatchFound = FALSE;

      for (idx = 0; idx < ProductStrLen; idx++)
      {
         if ((*p     == '_') && (*(p+1) == 0) &&
             (*(p+2) == 'C') && (*(p+3) == 0) &&
             (*(p+4) == 'I') && (*(p+5) == 0) &&
             (*(p+6) == 'D') && (*(p+7) == 0) &&
             (*(p+8) == ':') && (*(p+9) == 0))
         {
            pCidLoc = p + 10;
            adjusted += 10;
            bMatchFound = TRUE;
            bSetEntry = TRUE;
            break;
         }
         p++;
         adjusted++;
      }

      // Adjust length
      if (bMatchFound == TRUE)
      {
         INT tmpLen = ProductStrLen;

         tmpLen -= adjusted;
         p = pCidLoc;
         while (tmpLen > 0)
         {
            if (((*p == ' ') && (*(p+1) == 0)) ||  // space
                ((*p == '_') && (*(p+1) == 0)))    // or _ for another field
            {
               break;
            }
            else
            {
               p += 2;       // advance 1 unicode byte
               tmpLen -= 2;  // remaining string length
            }
         }
         strLen = (USHORT)(p - pCidLoc);
      }
      else
      {
         QDB_DbgPrint
         (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> QDBPNP_GetCID: no CID found\n", pDevContext->PortName)
         );
         ntStatus = STATUS_UNSUCCESSFUL;
         bSetEntry = FALSE;
      }
   }

UpdateRegistry:

   // update registry
   RtlInitUnicodeString(&ucValueName, VEN_DEV_CID);
   if ((bSetEntry == TRUE) && (strLen > 0) && (pCidLoc != NULL))
   {
      ZwSetValueKey
      (
         hRegKey,
         &ucValueName,
         0,
         REG_SZ,
         (PVOID)pCidLoc,
         strLen
      );
   }
   else
   {
      ZwDeleteValueKey(hRegKey, &ucValueName);
   }

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--QDB_GetCID: strLen %d ST 0x%x\n", pDevContext->PortName, strLen, ntStatus)
   );

   return ntStatus;

} // QDBPNP_GetCID

NTSTATUS QDBPNP_GetDeviceSerialNumber(IN WDFDEVICE Device, UCHAR Index, BOOLEAN MatchPrefix)
{
   PDEVICE_CONTEXT  pDevContext;
   NTSTATUS         ntStatus;
   USHORT           strLen = 128;
   USHORT           productStrLen = 0;
   UNICODE_STRING   ucValueName;
   HANDLE           hRegKey;
   PCHAR            pSerLoc = NULL;
   PCHAR            pCidLoc = NULL;
   BOOLEAN          bSetEntry = FALSE;

   pDevContext = QdbDeviceGetContext(Device);

   if (Index == 0)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> <--_GetDeviceSerialNumber: index is NULL\n", pDevContext->PortName)
      );
      goto UpdateRegistry;
   }

   RtlZeroMemory(pDevContext->SerialNumber, 256);

   ntStatus = WdfUsbTargetDeviceQueryString
              (
                 pDevContext->WdfUsbDevice,
                 NULL,
                 NULL,
                 (PUSHORT)pDevContext->SerialNumber,
                 &strLen,
                 (UCHAR)Index,
                 0x0409
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      RtlZeroMemory(pDevContext->SerialNumber, 256);
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBPNP_GetDeviceSerialNumber: failure 0x%x\n", pDevContext->PortName, ntStatus)
      );
      goto UpdateRegistry;
   }
   else
   {
      strLen *= sizeof(WCHAR);

      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> _GetDeviceSerialNumber: QueryString 0x%x (%dB)\n", pDevContext->PortName, ntStatus, strLen)
      );
   }

   productStrLen = strLen;
   pSerLoc = (PCHAR)pDevContext->SerialNumber;
   pCidLoc = (PCHAR)pDevContext->SerialNumber;
   bSetEntry = TRUE;

   // search for "_SN:"
   if ((MatchPrefix == TRUE) && (strLen > 0))
   {
      USHORT idx, adjusted = 0;
      PCHAR p = (PCHAR)pSerLoc;
      BOOLEAN bMatchFound = FALSE;

      for (idx = 0; idx < strLen; idx++)
      {
         if ((*p     == '_') && (*(p+1) == 0) &&
             (*(p+2) == 'S') && (*(p+3) == 0) &&
             (*(p+4) == 'N') && (*(p+5) == 0) &&
             (*(p+6) == ':') && (*(p+7) == 0))
         {
            pSerLoc = p + 8;
            adjusted += 8;
            bMatchFound = TRUE;
            break;
         }
         p++;
         adjusted++;
      }

      // Adjust length
      if (bMatchFound == TRUE)
      {
         INT tmpLen = strLen;

         tmpLen -= adjusted;
         p = pSerLoc;
         while (tmpLen > 0)
         {
            if (((*p == ' ') && (*(p+1) == 0)) ||  // space
                ((*p == '_') && (*(p+1) == 0)))    // or _ for another field
            {
               break;
            }
            else
            {
               p += 2;       // advance 1 unicode byte
               tmpLen -= 2;  // remaining string length
            }
         }
         strLen = (USHORT)(p - pSerLoc); // 8;
      }
      else
      {
         QDB_DbgPrint
         (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> <--QDBPNP_GetDeviceSerialNumber: no SN found\n", pDevContext->PortName)
         );
         ntStatus = STATUS_UNSUCCESSFUL;
         bSetEntry = FALSE;
      }
   }

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> _GetDeviceSerialNumber: adjusted strLen %d\n", pDevContext->PortName, strLen)
   );

UpdateRegistry:

   ntStatus = IoOpenDeviceRegistryKey
              (
                 pDevContext->PhysicalDeviceObject,
                 PLUGPLAY_REGKEY_DRIVER,
                 KEY_ALL_ACCESS,
                 &hRegKey
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> _GetDeviceSerialNumber: reg access failure 0x%x\n", pDevContext->PortName, ntStatus)
      );
      return ntStatus;
   }

   if (MatchPrefix == FALSE)
   {
      RtlInitUnicodeString(&ucValueName, VEN_DEV_SERNUM);
   }
   else
   {
      RtlInitUnicodeString(&ucValueName, VEN_DEV_MSM_SERNUM);
   }
   if (bSetEntry == TRUE)
   {
      ntStatus = ZwSetValueKey
                 (
                    hRegKey,
                    &ucValueName,
                    0,
                    REG_SZ,
                    (PVOID)pSerLoc,
                    strLen
                 );
   }
   else
   {
      ZwDeleteValueKey(hRegKey, &ucValueName);
   }
   if (MatchPrefix == TRUE) 
   {
      QDBPNP_GetCID(Device, pCidLoc, productStrLen, hRegKey);
   }
   ZwClose(hRegKey);

   return ntStatus; 

} // QDBPNP_GetDeviceSerialNumber

NTSTATUS QDBPNP_SetFunctionProtocol(IN WDFDEVICE Device, ULONG ProtocolCode)
{
   PDEVICE_CONTEXT pDevContext;
   NTSTATUS        ntStatus;
   UNICODE_STRING  ucValueName;
   HANDLE          hRegKey;

   pDevContext = QdbDeviceGetContext(Device);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> -->_SetFunctionProtocol: 0x%x\n", pDevContext->PortName, ProtocolCode)
   );

   ntStatus = IoOpenDeviceRegistryKey
              (
                 pDevContext->PhysicalDeviceObject,
                 PLUGPLAY_REGKEY_DRIVER,
                 KEY_ALL_ACCESS,
                 &hRegKey
              );

   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> <--_SetFunctionProtocol: failed to open registry 0x%x\n", pDevContext->PortName, ntStatus)
      );
      return ntStatus;
   }

   RtlInitUnicodeString(&ucValueName, VEN_DEV_PROTOC);
   ntStatus = ZwSetValueKey
              (
                 hRegKey,
                 &ucValueName,
                 0,
                 REG_DWORD,
                 (PVOID)&ProtocolCode,
                 sizeof(ULONG)
              );
   ZwClose(hRegKey);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--_SetFunctionProtocol: 0x%x ST 0x%x\n", pDevContext->PortName, ProtocolCode, ntStatus)
   );

   return ntStatus;
}  // QDBPNP_SetFunctionProtocol

NTSTATUS QDBPNP_EnumerateDevice(IN WDFDEVICE Device)
{
   PDEVICE_CONTEXT       pDevContext;
   USB_DEVICE_DESCRIPTOR usbDeviceDesc;
   NTSTATUS              ntStatus;

   pDevContext = QdbDeviceGetContext(Device);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> -->QDBPNP_EnumerateDevice: 0x%p\n", pDevContext->PortName, Device)
   );

   // Get the USB device handle to communicate with the USB stack
   if (pDevContext->WdfUsbDevice == NULL)
   {
      ntStatus = WdfUsbTargetDeviceCreate
                 (
                    Device,
                    WDF_NO_OBJECT_ATTRIBUTES,
                    &pDevContext->WdfUsbDevice
                 );
      if (!NT_SUCCESS(ntStatus))
      {
         QDB_DbgPrint
         (
            0, 0,
            ("<%s> QDBPNP_EnumerateDevice: couldn't get USB handle 0x%x\n", pDevContext->PortName, ntStatus)
         );
         return ntStatus;
      }
   }

   WdfUsbTargetDeviceGetDeviceDescriptor
   (
      pDevContext->WdfUsbDevice,
      &usbDeviceDesc
   );

   QDBPNP_GetParentDeviceName(Device);

   ntStatus = QDBPNP_GetDeviceSerialNumber(Device, usbDeviceDesc.iProduct, TRUE);
   if ((!NT_SUCCESS(ntStatus)) && (usbDeviceDesc.iProduct != 2))
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_ERROR,
         ("<%s> QDBPNP_EnumerateDevice: _SERN: Failed with iProduct 0x%x, try default\n",
           pDevContext->PortName, usbDeviceDesc.iProduct)
      );
      QDBPNP_GetDeviceSerialNumber(Device, 0x02, TRUE);
   }
   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_ERROR,
      ("<%s> QDBPNP_EnumerateDevice: _SERN: tried iProduct: ST(0x%x)\n",
        pDevContext->PortName, ntStatus)
   );

   QDBPNP_GetDeviceSerialNumber(Device, usbDeviceDesc.iSerialNumber, FALSE);
   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_ERROR,
      ("<%s> QDBPNP_EnumerateDevice: _SERN: tried iSerialNumber 0x%x ST(0x%x)\n",
        pDevContext->PortName, usbDeviceDesc.iSerialNumber, ntStatus)
   );
   ntStatus = STATUS_SUCCESS; // make possible failure none-critical

   if (usbDeviceDesc.bNumConfigurations == 0)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_ERROR,
         ("<%s> <--QDBPNP_EnumerateDevice: failed to get DevDescriptor\n", pDevContext->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_DETAIL,
      ("<%s> USB Device: bLength(%d) bDescType(0x%x) bcdUSB(%d) bDevClass(0x%x)\n",
        pDevContext->PortName,
        usbDeviceDesc.bLength,
        usbDeviceDesc.bDescriptorType,
        usbDeviceDesc.bcdUSB,
        usbDeviceDesc.bDeviceClass
      )
   );
   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_DETAIL,
      ("<%s> USB Device: bDevSubClass(0x%x) bDevProtocol(0x%x) bMaxPktSz0(%d) idVendor(0x%x)\n",
        pDevContext->PortName,
        usbDeviceDesc.bDeviceSubClass,
        usbDeviceDesc.bDeviceProtocol,
        usbDeviceDesc.bMaxPacketSize0,
        usbDeviceDesc.idVendor
      )
   );
   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_DETAIL,
      ("<%s> USB Device: idProduct(0x%x) bcdDevice(0x%x) iMFR(0x%x) iProd(0x%x) iSerNum(0x%x) bNumConfig(%d)\n",
        pDevContext->PortName,
        usbDeviceDesc.idProduct,
        usbDeviceDesc.bcdDevice,
        usbDeviceDesc.iManufacturer,
        usbDeviceDesc.iProduct,
        usbDeviceDesc.iSerialNumber,
        usbDeviceDesc.bNumConfigurations
      )
   );

   ntStatus = QDBPNP_UsbConfigureDevice(Device);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--QDBPNP_EnumerateDevice 0x%p (0x%x)\n", pDevContext->PortName, Device, ntStatus)
   );
   return ntStatus;

}  // QDBPNP_EnumerateDevice

NTSTATUS QDBPNP_UsbConfigureDevice(IN WDFDEVICE Device)
{
   PDEVICE_CONTEXT               pDevContext;
   NTSTATUS                      ntStatus;
   USHORT                        bufSize;
   PUSB_CONFIGURATION_DESCRIPTOR configDesc = NULL;
   WDF_OBJECT_ATTRIBUTES         objAttrib;
   WDFMEMORY                     memory;

   pDevContext = QdbDeviceGetContext(Device);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> -->QDBPNP_UsbConfigureDevice: 0x%p\n", pDevContext->PortName, Device)
   );

   // get the size of config desc
   bufSize = 0; // function call should return BUFFER_TOO_SMALL
   ntStatus = WdfUsbTargetDeviceRetrieveConfigDescriptor
              (
                 pDevContext->WdfUsbDevice, NULL, &bufSize
              );

   if ((bufSize == 0) || (ntStatus != STATUS_BUFFER_TOO_SMALL))
   {
      return ntStatus;
   }

   // create config desc and tie to parent for garbage collection
   WDF_OBJECT_ATTRIBUTES_INIT(&objAttrib);

   objAttrib.ParentObject = pDevContext->WdfUsbDevice;

   ntStatus = WdfMemoryCreate
              (
                 &objAttrib,
                 NonPagedPool,
                 QDB_TAG_GEN,
                 bufSize,
                 &memory,
                 &configDesc
              );
   if (!NT_SUCCESS(ntStatus))
   {
      return ntStatus;
   }

   // retrieve config desc
   ntStatus = WdfUsbTargetDeviceRetrieveConfigDescriptor
              (
                 pDevContext->WdfUsbDevice,
                 configDesc,
                 &bufSize
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_ERROR,
         ("<%s> <--QDBPNP_UsbConfigureDevice: failed to get descriptor 0x%p (0x%x)\n", pDevContext->PortName,
           Device, ntStatus)
      );
      return ntStatus;
   }

   pDevContext->ConfigDesc = configDesc;

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_DETAIL,
      ("<%s> USB Configuration: bLength(%d) bDescType(0x%x) wTotalLen(%d) numIF(%d)\n",
        pDevContext->PortName,
        configDesc->bLength, configDesc->bDescriptorType,
        configDesc->wTotalLength, configDesc->bNumInterfaces
      )
   );
   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_DETAIL,
      ("<%s> USB Configuration: bConfigValue(%d) iConfig(0x%x) bmAttr(0x%X) maxPwr(%d)\n",
        pDevContext->PortName,
        configDesc->bConfigurationValue, configDesc->iConfiguration,
        configDesc->bmAttributes, configDesc->MaxPower
      )
   );

 
   if (pDevContext->ConfigDesc->bNumInterfaces == 0)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBPNP_UsbConfigureDevice: no interface found\n", pDevContext->PortName)
      );
      ntStatus = STATUS_UNSUCCESSFUL;
   }
   else
   {
      ntStatus = QDBPNP_SelectInterfaces(Device);
   }

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--QDBPNP_UsbConfigureDevice: 0x%p (0x%x)\n", pDevContext->PortName, Device, ntStatus)
   );
   return ntStatus;

}  // QDBPNP_UsbConfigureDevice

NTSTATUS QDBPNP_SelectInterfaces(WDFDEVICE Device)
{
   PDEVICE_CONTEXT                     pDevContext;
   NTSTATUS                            ntStatus;
   WDF_USB_DEVICE_SELECT_CONFIG_PARAMS cfgParams;
   UCHAR                               numInterfaces;
   BOOLEAN                             bTraceFound = FALSE;
   BOOLEAN                             bDebugFound = FALSE;

   pDevContext = QdbDeviceGetContext(Device);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> -->QDBPNP_SelectInterfaces: Device 0x%p numIFs=%d\n", pDevContext->PortName,
        Device, pDevContext->ConfigDesc->bNumInterfaces)
   );

   if (pDevContext->ConfigDesc->bNumInterfaces == 1)
   {
      WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(&cfgParams);
   }
   else
   {
      // enable all interfacs with alt setting 0
      WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES
      (
         &cfgParams,
         0, // pDevContext->ConfigDesc->bNumInterfaces,
         NULL
      );
   }

   ntStatus = WdfUsbTargetDeviceSelectConfig
              (
                 pDevContext->WdfUsbDevice,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &cfgParams
              );

   if (NT_SUCCESS(ntStatus))
   {
      // for testing only, customized for single-interface device
      if (pDevContext->ConfigDesc->bNumInterfaces == 1)
      {
         WDFUSBINTERFACE                         configuredInterface;
         WDF_USB_INTERFACE_SELECT_SETTING_PARAMS interfaceSelectSetting;
         NTSTATUS                                ntStatus;

         configuredInterface = cfgParams.Types.SingleInterface.ConfiguredUsbInterface;

         #ifdef QDB_USE_GOBI_DIAG

         // for testing only, customized for GOBI2000 DIAG interface using alt setting 1
         WDF_USB_INTERFACE_SELECT_SETTING_PARAMS_INIT_SETTING(&interfaceSelectSetting, 1);

         #else

         // choose alt setting 0
         WDF_USB_INTERFACE_SELECT_SETTING_PARAMS_INIT_SETTING(&interfaceSelectSetting, 0);

         #endif // QDB_USE_GOBI_DIAG

         ntStatus = WdfUsbInterfaceSelectSetting
                    (
                       configuredInterface,
                       WDF_NO_OBJECT_ATTRIBUTES,
                       &interfaceSelectSetting
                    );
         if (!NT_SUCCESS(ntStatus))
         {
            QDB_DbgPrint
            (
               QDB_DBG_MASK_CONTROL,
               QDB_DBG_LEVEL_ERROR,
               ("<%s> Device 0x%p: Failed to select interface with alt setting 1\n",
                 pDevContext->PortName, Device)
            );
            return ntStatus;
         }
      }  // test code for GOBI2000 DIAG interface
   }

   // we do not mandate there be 2 interfaces, we just look for interfaces that satisfying
   // TRACE and DEBUG interface requirements
   if (NT_SUCCESS(ntStatus))
   {
      numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(pDevContext->WdfUsbDevice);
      if (numInterfaces > 0)
      {
         UCHAR i;
         WDFUSBINTERFACE usbInterface;
         BYTE numEPs, numPipes;

         for (i = 0; i < numInterfaces; i++)
         {
            usbInterface = WdfUsbTargetDeviceGetInterface(pDevContext->WdfUsbDevice, i);
            if (usbInterface == NULL)
            {
               QDB_DbgPrint
               (
                  QDB_DBG_MASK_CONTROL,
                  QDB_DBG_LEVEL_ERROR,
                  ("<%s> invalid interface %d\n", pDevContext->PortName, i)
               );
               continue;
            }
            numEPs = WdfUsbInterfaceGetNumEndpoints(usbInterface, 0);  // for information only
            numPipes = WdfUsbInterfaceGetNumConfiguredPipes(usbInterface);

            QDB_DbgPrint
            (
               QDB_DBG_MASK_CONTROL,
               QDB_DBG_LEVEL_DETAIL,
               ("<%s> examining IF 0x%p(%d) numEPs %d numPipes %d\n", pDevContext->PortName,
                  usbInterface, i, numEPs, numPipes)
            );

            if (numEPs > 0)
            {
               USB_INTERFACE_DESCRIPTOR interfaceDesc;

               WdfUsbInterfaceGetDescriptor(usbInterface, 0, &interfaceDesc);
               pDevContext->IfProtocol = (ULONG)interfaceDesc.bInterfaceProtocol        |
                                         ((ULONG)interfaceDesc.bInterfaceClass)   << 8  |
                                         ((ULONG)interfaceDesc.bAlternateSetting) << 16 |
                                         ((ULONG)interfaceDesc.bInterfaceNumber)  << 24;
               QDBPNP_SetFunctionProtocol(Device, pDevContext->IfProtocol);
            }

            switch (numPipes)
            {
               case 1:  // TRACE or DPL
               {
                  if (bTraceFound == TRUE)
                  {
                     QDB_DbgPrint
                     (
                        QDB_DBG_MASK_CONTROL,
                        QDB_DBG_LEVEL_DETAIL,
                        ("<%s> TRACE/DPL found, skip IF%d\n", pDevContext->PortName, i)
                     );
                     break;
                  }
                  // varify if the EP is bulk IN
                  WDF_USB_PIPE_INFORMATION_INIT(&pDevContext->TracePipeInfo);
                  pDevContext->TraceIN = WdfUsbInterfaceGetConfiguredPipe
                                         (
                                            usbInterface,
                                            0, // pipe index
                                            &pDevContext->TracePipeInfo
                                         );
                  if (pDevContext->TraceIN == NULL)
                  {
                     QDB_DbgPrint
                     (
                        QDB_DBG_MASK_CONTROL,
                        QDB_DBG_LEVEL_ERROR,
                        ("<%s> failed to get TraceIN\n", pDevContext->PortName)
                     );
                     break;
                  }

                  QDB_DbgPrint
                  (
                     QDB_DBG_MASK_CONTROL,
                     QDB_DBG_LEVEL_ERROR,
                     ("<%s> PipeInfo: MaxPktSz %d EP 0x%X Interval %d Index %d Type %d MaxXfrSz %d\n",
                       pDevContext->PortName,
                       pDevContext->TracePipeInfo.MaximumPacketSize,
                       pDevContext->TracePipeInfo.EndpointAddress,
                       pDevContext->TracePipeInfo.Interval,
                       pDevContext->TracePipeInfo.SettingIndex,
                       pDevContext->TracePipeInfo.PipeType,
                       pDevContext->TracePipeInfo.MaximumTransferSize
                     )
                  );

                  if ((pDevContext->TracePipeInfo.PipeType != WdfUsbPipeTypeBulk) ||
                      ((pDevContext->TracePipeInfo.EndpointAddress & 0x80) == 0))
                  {
                     QDB_DbgPrint
                     (
                        QDB_DBG_MASK_CONTROL,
                        QDB_DBG_LEVEL_ERROR,
                        ("<%s> invalid TraceIN (EP 0x%X)\n", pDevContext->PortName,
                          pDevContext->TracePipeInfo.EndpointAddress)
                     );
                     break;
                  }
                  bTraceFound = TRUE;
                  pDevContext->UsbInterfaceTRACE = usbInterface;

                  // disable USB transfer length check
                  WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pDevContext->TraceIN);
                  break;
               }
               case 2: // DEBUG
               {
                  WDFUSBPIPE               pipe0, pipe1;
                  WDF_USB_PIPE_INFORMATION pipeInfo0, pipeInfo1; 

                  if (bDebugFound == TRUE)
                  {
                     QDB_DbgPrint
                     (
                        QDB_DBG_MASK_CONTROL,
                        QDB_DBG_LEVEL_DETAIL,
                        ("<%s> DEBUG found, skip IF%d\n", pDevContext->PortName, i)
                     );
                     break;
                  }
                  // verify the EPs are IN and OUT
                  WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo0);
                  WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo1);
                  pipe0 = WdfUsbInterfaceGetConfiguredPipe(usbInterface, 0, &pipeInfo0);
                  pipe1 = WdfUsbInterfaceGetConfiguredPipe(usbInterface, 1, &pipeInfo1);
                  if ((pipe0 == NULL) || (pipe1 == NULL))
                  {
                     QDB_DbgPrint
                     (
                        QDB_DBG_MASK_CONTROL,
                        QDB_DBG_LEVEL_ERROR,
                        ("<%s> failed to get DebugIN/DebugOUT (0x%p/0x%p)\n",
                          pDevContext->PortName, pipe0, pipe1)
                     );
                     break;
                  }

                  QDB_DbgPrint
                  (
                     QDB_DBG_MASK_CONTROL,
                     QDB_DBG_LEVEL_ERROR,
                     ("<%s> PipeInfo[0]: MaxPktSz %d EP 0x%X Interval %d Index %d Type %d MaxXfrSz %d\n",
                       pDevContext->PortName,
                       pipeInfo0.MaximumPacketSize,
                       pipeInfo0.EndpointAddress,
                       pipeInfo0.Interval,
                       pipeInfo0.SettingIndex,
                       pipeInfo0.PipeType,
                       pipeInfo0.MaximumTransferSize
                     )
                  );
                  QDB_DbgPrint
                  (
                     QDB_DBG_MASK_CONTROL,
                     QDB_DBG_LEVEL_ERROR,
                     ("<%s> PipeInfo[1]: MaxPktSz %d EP 0x%X Interval %d Index %d Type %d MaxXfrSz %d\n",
                       pDevContext->PortName,
                       pipeInfo1.MaximumPacketSize,
                       pipeInfo1.EndpointAddress,
                       pipeInfo1.Interval,
                       pipeInfo1.SettingIndex,
                       pipeInfo1.PipeType,
                       pipeInfo1.MaximumTransferSize
                     )
                  );

                  // pipes have to be BULK
                  if ((pipeInfo0.PipeType != WdfUsbPipeTypeBulk) ||
                      (pipeInfo1.PipeType != WdfUsbPipeTypeBulk))
                  {
                     QDB_DbgPrint
                     (
                        QDB_DBG_MASK_CONTROL,
                        QDB_DBG_LEVEL_ERROR,
                        ("<%s> invalid DebugIN/DebugOUT (EP 0x%X/0x%X)\n", pDevContext->PortName,
                          pipeInfo0.EndpointAddress, pipeInfo1.EndpointAddress)
                     );
                     break;
                  }
                  // pipes have to be IN and OUT
                  if ((pipeInfo0.EndpointAddress & 0x80) == 0)
                  {
                     if ((pipeInfo1.EndpointAddress & 0x80) == 0)
                     {
                        break;
                     }
                     bDebugFound = TRUE;
                     pDevContext->UsbInterfaceDEBUG = usbInterface;
                     // pipe0 is OUT, pipe1 is IN
                     pDevContext->DebugIN = pipe1;
                     pDevContext->DebugOUT = pipe0;
                     RtlCopyMemory(&pDevContext->DebugINPipeInfo, &pipeInfo1, sizeof(WDF_USB_PIPE_INFORMATION));
                     RtlCopyMemory(&pDevContext->DebugOUTPipeInfo, &pipeInfo0, sizeof(WDF_USB_PIPE_INFORMATION));
                  }
                  else 
                  {
                     if ((pipeInfo1.EndpointAddress & 0x80) != 0)
                     {
                        break;
                     }
                     bDebugFound = TRUE;
                     pDevContext->UsbInterfaceDEBUG = usbInterface;
                     // pipe0 is IN, pipe1 is OUT
                     pDevContext->DebugIN = pipe0;
                     pDevContext->DebugOUT = pipe1;
                     RtlCopyMemory(&pDevContext->DebugINPipeInfo, &pipeInfo0, sizeof(WDF_USB_PIPE_INFORMATION));
                     RtlCopyMemory(&pDevContext->DebugOUTPipeInfo, &pipeInfo1, sizeof(WDF_USB_PIPE_INFORMATION));
                  }

                  // disable USB transfer length check
                  WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pDevContext->DebugIN);
                  WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pDevContext->DebugOUT);

                  break;
               }

               default:
               {
                  QDB_DbgPrint
                  (
                     QDB_DBG_MASK_CONTROL,
                     QDB_DBG_LEVEL_DETAIL,
                     ("<%s> Unexpected number of pipes: %d from IF%d\n", pDevContext->PortName, numPipes, i)
                  );
                  break;
               }
            }  // switch
         }  // for
      }
      else
      {
         ntStatus = STATUS_UNSUCCESSFUL;

         QDB_DbgPrint
         (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBPNP_SelectInterfaces: no interface found\n", pDevContext->PortName)
         );
      }
   }
   else
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_ERROR,
         ("<%s> QDBPNP_SelectInterfaces: WdfUsbTargetDeviceSelectConfig failed 0x%x\n",
           pDevContext->PortName, ntStatus)
      );
   }

   if ((bTraceFound == FALSE) && (bDebugFound == FALSE))
   {
      ntStatus = STATUS_UNSUCCESSFUL;
   }

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_CRITICAL,
      ("<%s> QDBPNP_SelectInterfaces: ST 0x%x TraceIN 0x%p(EP%x) DebugIN 0x%p(EP%x) DebugOUT 0x%p(EP%x)\n",
        pDevContext->PortName, ntStatus,
        pDevContext->TraceIN, pDevContext->TracePipeInfo.EndpointAddress,
        pDevContext->DebugIN, pDevContext->DebugINPipeInfo.EndpointAddress,
        pDevContext->DebugOUT, pDevContext->DebugOUTPipeInfo.EndpointAddress
      )
   );
   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--QDBPNP_SelectInterfaces: Device 0x%p(0x%x) (TRACE %d, DEBUG %d)\n", pDevContext->PortName,
        Device, ntStatus, bTraceFound, bDebugFound)
   );

   return ntStatus;
}

NTSTATUS QDBPNP_SetFriendlyName(WDFDEVICE Device, PUNICODE_STRING FriendlyName, PWCHAR TargetDriverKey)
{
#define QC_NAME_LEN 1024
   PDEVICE_CONTEXT pDevContext;
   NTSTATUS        nts;
   ULONG           bufLen, actualLen;
   CHAR            hwId[QC_NAME_LEN], swKey[QC_NAME_LEN], devKey[QC_NAME_LEN];
   PWCHAR          pId;
   DWORD           idLen, restLen, subKeyIdx, subKeyLen;
   int             selected = 0;
   LONG            regResult;
   HANDLE          hwKeyHandle;
   UNICODE_STRING  ucHwKeyPath;
   OBJECT_ATTRIBUTES oa;

   pDevContext = QdbDeviceGetContext(Device);

   RtlZeroMemory(hwId,  QC_NAME_LEN);
   RtlZeroMemory(swKey, QC_NAME_LEN);
   RtlZeroMemory(devKey, QC_NAME_LEN);

   // get HW IDs
   bufLen = QC_NAME_LEN;
   nts = IoGetDeviceProperty
         (
            pDevContext->PhysicalDeviceObject,
            DevicePropertyHardwareID,
            bufLen,
            (PVOID)hwId,
            &actualLen
         );
   if (!NT_SUCCESS(nts))
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBPNP_SetFriendlyName: IoGetDeviceProperty (HWID) failure\n", pDevContext->PortName)
      );
      return nts;
   }
   else
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBPNP_SetFriendlyName: IoGetDeviceProperty (HWID) len: %u\n", pDevContext->PortName, actualLen)
      );
   }
   pId = (PWCHAR)hwId;
   restLen = actualLen;

   while ((idLen = wcsnlen(pId, restLen)) > 0)
   {
      if (idLen >= restLen) break;
      selected = 1;
      if (wcsstr(pId, L"REV_") != NULL)
      { selected = 0;
      }
      else
      {
         break;
      }
      pId += (idLen + 1); // including NULL
      restLen -= (idLen + 1);
   }

   if (selected == 0)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBPNP_SetFriendlyName: failed to find devID\n", pDevContext->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   // HW ID: pId, idLen
   // construct full HW key
   nts = RtlStringCbCopyW((PWCHAR)devKey, QC_NAME_LEN, DEVICE_HW_KEY_ROOT);
   if (nts != STATUS_SUCCESS)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBPNP_SetFriendlyName: HW key root failure\n", pDevContext->PortName)
      );
      return nts;
   }
   nts = RtlStringCbCatW((PWCHAR)devKey, QC_NAME_LEN, pId);
   if (nts != STATUS_SUCCESS)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBPNP_SetFriendlyName: RtlStringCbCat failed\n", pDevContext->PortName)
      );
      return nts;
   }
   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> QDBPNP_SetFriendlyName: devKey <%ws>\n", pDevContext->PortName, (PWCHAR)devKey)
   );

   // open HW key
   RtlInitUnicodeString(&ucHwKeyPath, (PWCHAR)devKey);
   InitializeObjectAttributes
   (
      &oa,
      &ucHwKeyPath,
      (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
      NULL, NULL
   );
   nts = ZwOpenKey
         (
            &hwKeyHandle,
            GENERIC_ALL,
            &oa
         );
   if (nts != STATUS_SUCCESS)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBPNP_SetFriendlyName: ZwOpenKey failed 0x%x\n", pDevContext->PortName, nts)
      );
      return nts;
   }

   // enum subkeys
   subKeyIdx = 0;
   while (nts == STATUS_SUCCESS)
   {
      CHAR           subKeyInfo[QC_NAME_LEN];
      UNICODE_STRING ucSubKeyPath;
      PKEY_BASIC_INFORMATION keyName;
      OBJECT_ATTRIBUTES      oa;

      RtlZeroMemory(subKeyInfo, QC_NAME_LEN);
      nts = ZwEnumerateKey
            (
               hwKeyHandle,
               subKeyIdx,
               KeyBasicInformation,
               (PVOID)subKeyInfo,
               QC_NAME_LEN,
               &subKeyLen
            );
      // open each subkey for R/W and find a match with SW key
      if (nts == STATUS_SUCCESS)
      {
         HANDLE subKeyHandle;
         CHAR   driverInfo[QC_NAME_LEN];
         DWORD  infoLen;
         UNICODE_STRING ucRegEntryName;

         keyName = (PKEY_BASIC_INFORMATION)subKeyInfo;
         QDB_DbgPrint
         (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> QDBPNP_SetFriendlyName: subKey[%d]: <%ws>\n", pDevContext->PortName, subKeyIdx, keyName->Name)
         );

         RtlInitUnicodeString(&ucSubKeyPath, keyName->Name);
         InitializeObjectAttributes
         (
            &oa,
            &ucSubKeyPath,
            (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
            hwKeyHandle,
            NULL
         );
         nts = ZwOpenKey
               (
                  &subKeyHandle,
                  GENERIC_ALL,
                  &oa
               );
         if (nts != STATUS_SUCCESS)
         {
            QDB_DbgPrint
            (
               QDB_DBG_MASK_CONTROL,
               QDB_DBG_LEVEL_TRACE,
               ("<%s> QDBPNP_SetFriendlyName: ZwOpenKey failed (sub): 0x%x\n", pDevContext->PortName, nts)
            );
            break;
         }

         // retrieve driver key and match
         RtlZeroMemory(driverInfo, QC_NAME_LEN);
         RtlInitUnicodeString(&ucRegEntryName, L"Driver");
         nts = ZwQueryValueKey
               (
                  subKeyHandle,
                  &ucRegEntryName,
                  KeyValueFullInformation,
                  (PKEY_VALUE_FULL_INFORMATION)&driverInfo,
                  QC_NAME_LEN,
                  &infoLen
               );
         if (nts == STATUS_SUCCESS)
         {
            UNICODE_STRING swKey1, swKey2;
            PKEY_VALUE_FULL_INFORMATION pDriverKey;
            PCHAR pDrvKeyVal;

            pDriverKey = (PKEY_VALUE_FULL_INFORMATION)&driverInfo;
            pDrvKeyVal = (PCHAR)&driverInfo;
            pDrvKeyVal += pDriverKey->DataOffset;
            QDB_DbgPrint
            (
               QDB_DBG_MASK_CONTROL,
               QDB_DBG_LEVEL_TRACE,
               ("<%s> QDBPNP_SetFriendlyName: retrived %uB swKey <%ws>vs<%ws>\n", pDevContext->PortName,
                 infoLen, (PWCHAR)pDrvKeyVal, TargetDriverKey)
            );
            RtlInitUnicodeString(&swKey1, TargetDriverKey);
            RtlInitUnicodeString(&swKey2, (PWCHAR)pDrvKeyVal);
            if (TRUE == RtlEqualUnicodeString(&swKey1, &swKey2, TRUE))
            {
               UNICODE_STRING ucSetEntryName;

               // set FriendlyName
               RtlInitUnicodeString(&ucSetEntryName, L"FriendlyName");
               nts = ZwSetValueKey
                     (
                        subKeyHandle,
                        &ucSetEntryName,
                        0,
                        REG_SZ,
                        FriendlyName->Buffer,
                        FriendlyName->Length+2
                     );
               if (!NT_SUCCESS(nts))
               {
                  QDB_DbgPrint
                  (
                     QDB_DBG_MASK_CONTROL,
                     QDB_DBG_LEVEL_TRACE,
                     ("<%s> QDBPNP_SetFriendlyName: failed to set FriendlyName: 0x%x\n", pDevContext->PortName, nts)
                  );
               }
            }
         }
         else
         {
            QDB_DbgPrint
            (
               QDB_DBG_MASK_CONTROL,
               QDB_DBG_LEVEL_TRACE,
               ("<%s> QDBPNP_SetFriendlyName: ZwQueryValueKey (swKey) failure 0x%x\n", pDevContext->PortName, nts)
            );
         }
         ZwClose(subKeyHandle);
      }
      else
      {
         QDB_DbgPrint
         (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> QDBPNP_SetFriendlyName: ZwEnumerateKey failed/exhausted 0x%x\n", pDevContext->PortName, nts)
         );
         if (nts == STATUS_NO_MORE_ENTRIES)
         {
            nts = STATUS_SUCCESS;
         }
         break;
      }
      subKeyIdx++;
   }  // while
   ZwClose(hwKeyHandle);

   return nts;

   // if match found, write FriendlyName
}  // QDBPNP_SetFriendlyName

NTSTATUS QDBPNP_CreateSymbolicName(WDFDEVICE Device)
{
   PDEVICE_CONTEXT pDevContext;
   NTSTATUS        nts = STATUS_UNSUCCESSFUL;
   ULONG           bufLen = MAX_NAME_LEN, resultLen = 0;
   UNICODE_STRING  friendlyNameU, tempUcString;
   ANSI_STRING     friendlyNameA;
   CHAR            driverKey[512];
   PCHAR           pSwInstance = NULL;
   BOOLEAN         bMatched = FALSE;

#define LEFT_P L" ("
#define RIGHT_P L")"

   pDevContext = QdbDeviceGetContext(Device);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> -->QDBPNP_CreateSymbolicName: Device 0x%p\n", pDevContext->PortName, Device)
   );

   RtlZeroMemory(driverKey, 512);
   RtlZeroMemory(pDevContext->FriendlyNameHolder, bufLen*sizeof(WCHAR));
   RtlZeroMemory(pDevContext->FriendlyName, MAX_NAME_LEN);
   pDevContext->SymbolicLink.Buffer = NULL;
   pDevContext->SymbolicLink.Length = 0;
   pDevContext->SymbolicLink.MaximumLength = 0;

   nts = QDBMAIN_AllocateUnicodeString
         (
            &pDevContext->SymbolicLink,
            MAX_NAME_LEN,
            (ULONG)'MANF'
         );
   if (nts != STATUS_SUCCESS)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_ERROR,
         ("<%s> <--QDBPNP_CreateSymbolicName: NO MEM 0x%x\n", pDevContext->PortName, nts)
      );
      return nts;
   }

   // force friendly name creation
   bufLen = 512;
   nts = WdfDeviceQueryProperty
         (
            Device,
            DevicePropertyDriverKeyName,
            bufLen,
            (PVOID)driverKey,
            &resultLen
         );

   if (nts == STATUS_SUCCESS)
   {
      PCHAR pStart, pEnd;

      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_DETAIL,
         ("<%s> QDBPNP_CreateSymbolicName(SwKey): <%ws>\n", pDevContext->PortName, (PWCHAR)driverKey)
      );
      pStart = (PCHAR)driverKey;
      pEnd = pStart + resultLen;

      // look for '\', little-endian byte order: 0x5C 0x00
      while (pEnd > pStart)
      {
         if (*pEnd != 0x5C)  // look for '\'
         {
            pEnd--;
         }
         else
         {
            bMatched = TRUE;
            break;
         }
      }
      if (bMatched == TRUE)
      {
         pSwInstance = (pEnd + 2);
      }
   }

   bufLen = MAX_NAME_LEN;
   nts = WdfDeviceQueryProperty
         (
            Device,
            DevicePropertyDeviceDescription,
            bufLen,
            (PVOID)pDevContext->FriendlyNameHolder,
            &resultLen
         );

   if (nts == STATUS_SUCCESS)
   {
      RtlStringCbCatW(pDevContext->FriendlyNameHolder, MAX_NAME_LEN, LEFT_P);
      RtlStringCbCatW(pDevContext->FriendlyNameHolder, MAX_NAME_LEN, (PCWSTR)pSwInstance);
      RtlStringCbCatW(pDevContext->FriendlyNameHolder, MAX_NAME_LEN, RIGHT_P);
      RtlInitUnicodeString(&friendlyNameU, pDevContext->FriendlyNameHolder);

      RtlInitUnicodeString(&tempUcString, DEVICE_LINK_NAME_PATH);       //"\??\"
      RtlCopyUnicodeString(&pDevContext->SymbolicLink, &tempUcString); //"\??\"
      RtlAppendUnicodeStringToString
      (
         &pDevContext->SymbolicLink,
         &friendlyNameU
      );                             //"\??\<FriendlyName>"

      nts = RtlUnicodeStringToAnsiString(&friendlyNameA, &pDevContext->SymbolicLink, TRUE);

      if (nts == STATUS_SUCCESS)
      {
         if (friendlyNameA.Length < MAX_NAME_LEN)
         {
            RtlCopyMemory(pDevContext->FriendlyName, friendlyNameA.Buffer, friendlyNameA.Length);
            QDB_DbgPrint
            (
               QDB_DBG_MASK_CONTROL,
               QDB_DBG_LEVEL_DETAIL,
               ("<%s> QDBPNP_CreateSymbolicName(DeviceDesc): <%s>\n", pDevContext->PortName,
                 pDevContext->FriendlyName)
            );
            RtlFreeAnsiString(&friendlyNameA);
            nts = WdfDeviceCreateSymbolicLink(Device, &pDevContext->SymbolicLink);
         }
         // Create FriendlyName in registry
         nts = QDBPNP_SetFriendlyName(Device, &friendlyNameU, (PWCHAR)driverKey);
      }
   }

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--QDBPNP_CreateSymbolicName: Device 0x%p(0x%x) SymbolicLink %dB\n", pDevContext->PortName,
        Device, nts, pDevContext->SymbolicLink.Length)
   );

   return nts;
}  // QDBPNP_CreateSymbolicName

NTSTATUS QDBPNP_EnableSelectiveSuspend(WDFDEVICE Device)
{
   PDEVICE_CONTEXT pDevContext;
   NTSTATUS        nts = STATUS_UNSUCCESSFUL;
   WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idleSettings;

   pDevContext = QdbDeviceGetContext(Device);

   QDB_DbgPrint
   (
      0, 0,
      ("<%s> -->QDBPNP_EnableSelectiveSuspend: Device 0x%p\n", pDevContext->PortName, Device)
   );

   WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idleSettings, IdleUsbSelectiveSuspend);
   idleSettings.IdleTimeout = 200; // 200ms

   nts = WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
   if (!NT_SUCCESS(nts))
   {
      QDB_DbgPrint
      (
         0, 0,
         ("<%s> QDBPNP_EnableSelectiveSuspend: AssignS0 failed, device 0x%p(ST 0x%x)\n", pDevContext->PortName, Device, nts)
      );
      return nts;
   }


   QDB_DbgPrint
   (
      0, 0,
      ("<%s> <--QDBPNP_EnableSelectiveSuspend: Device 0x%p (0x%x)\n", pDevContext->PortName, Device, nts)
   );

   return nts;
}  // QDBPNP_EnableSelectiveSuspend

NTSTATUS QDBPNP_SetStamp
(
   PDEVICE_OBJECT PhysicalDeviceObject,
   HANDLE         hRegKey,
   BOOLEAN        Startup
)
{
   UNICODE_STRING ucValueName;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   ULONG stampValue = 0;
   BOOLEAN bSelfOpen = FALSE;

   if (hRegKey == 0)
   {
      ntStatus = IoOpenDeviceRegistryKey
                 (
                    PhysicalDeviceObject,
                    PLUGPLAY_REGKEY_DRIVER,
                    KEY_ALL_ACCESS,
                    &hRegKey
                 );
      if (!NT_SUCCESS(ntStatus))
      {
         return ntStatus;
      }
      bSelfOpen = TRUE;
   }

   RtlInitUnicodeString(&ucValueName, VEN_DEV_TIME);
   if (Startup == TRUE)
   {
      stampValue = 1;
      ZwSetValueKey
      (
         hRegKey,
         &ucValueName,
         0,
         REG_DWORD,
         (PVOID)&stampValue,
         sizeof(ULONG)
      );
   }
   else
   {
      // ZwDeleteValueKey(hRegKey, &ucValueName);
      stampValue = 0;
      ZwSetValueKey
      (
         hRegKey,
         &ucValueName,
         0,
         REG_DWORD,
         (PVOID)&stampValue,
         sizeof(ULONG)
      );
   }

   if (bSelfOpen == TRUE)
   {
      ZwClose(hRegKey);
   }

   return STATUS_SUCCESS;
}  // QDBPNP_SetStamp

NTSTATUS QDBPNP_GetParentDeviceName(WDFDEVICE Device)
{
   PDEVICE_CONTEXT pDevContext;
   CHAR parentDevName[MAX_NAME_LEN];
   PIRP pIrp = NULL;
   PIO_STACK_LOCATION nextstack;
   NTSTATUS ntStatus;
   KEVENT event;

   pDevContext = QdbDeviceGetContext(Device);

   RtlZeroMemory(parentDevName, MAX_NAME_LEN);
   KeInitializeEvent(&event, SynchronizationEvent, FALSE);

   pIrp = IoAllocateIrp((CCHAR)(pDevContext->MyDevice->StackSize+1), FALSE);
   if( pIrp == NULL )
   {
       QDB_DbgPrint
       (
          QDB_DBG_MASK_CONTROL,
          QDB_DBG_LEVEL_ERROR,
          ("<%s> QDBPNP_GetParentDeviceName failed to allocate an IRP\n", pDevContext->PortName)
       );
       return STATUS_UNSUCCESSFUL;
   }

   pIrp->AssociatedIrp.SystemBuffer = parentDevName;
   pIrp->UserEvent = &event;

   nextstack = IoGetNextIrpStackLocation(pIrp);
   nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
   nextstack->Parameters.DeviceIoControl.IoControlCode = IOCTL_QCDEV_GET_PARENT_DEV_NAME;
   nextstack->Parameters.DeviceIoControl.OutputBufferLength = MAX_NAME_LEN;

   IoSetCompletionRoutine
   (
      pIrp,
      (PIO_COMPLETION_ROUTINE)QDBDSP_IrpIoCompletion,
      (PVOID)pDevContext,
      TRUE,TRUE,TRUE
   );

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> QDBPNP_GetParentDeviceName (IRP 0x%p)\n", pDevContext->PortName, pIrp)
   );

   ntStatus = IoCallDriver(pDevContext->TargetDevice, pIrp);

   ntStatus = KeWaitForSingleObject(&event, Executive, KernelMode, TRUE, 0);

   if (ntStatus == STATUS_SUCCESS)
   {
      ntStatus = pIrp->IoStatus.Status;
      QDBPNP_SaveParentDeviceNameToRegistry(pDevContext, parentDevName, pIrp->IoStatus.Information);
   }
   else
   {
      QDBPNP_SaveParentDeviceNameToRegistry(pDevContext, parentDevName, 0);
   }

   IoFreeIrp(pIrp);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_CONTROL,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--- QDBPNP_GetParentDeviceName: ST %x\n", pDevContext->PortName, ntStatus)
   );

   return ntStatus;

}  // QDBPNP_GetParentDeviceName

VOID QDBPNP_SaveParentDeviceNameToRegistry
(
   PDEVICE_CONTEXT pDevContext,
   PVOID ParentDeviceName,
   ULONG NameLength
)
{
   HANDLE hRegKey;
   NTSTATUS ntStatus;
   UNICODE_STRING ucValueName;

   // update registry
   ntStatus = IoOpenDeviceRegistryKey
              (
                 pDevContext->PhysicalDeviceObject,
                 PLUGPLAY_REGKEY_DRIVER,
                 KEY_ALL_ACCESS,
                 &hRegKey
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_CONTROL,
         QDB_DBG_LEVEL_ERROR,
         ("<%s> QDBPNP_SaveParentDeviceNameToRegistry: reg access failure 0x%x\n", pDevContext->PortName, ntStatus)
      );
      return;
   }
   RtlInitUnicodeString(&ucValueName, L"QCDeviceParent");
   if (NameLength > 0)
   {
      ZwSetValueKey
      (
         hRegKey,
         &ucValueName,
         0,
         REG_SZ,
         ParentDeviceName,
         NameLength
      );
   }
   else
   {
      ZwDeleteValueKey(hRegKey, &ucValueName);
   }
   ZwClose(hRegKey);
}  // QDBPNP_SaveParentDeviceNameToRegistry

