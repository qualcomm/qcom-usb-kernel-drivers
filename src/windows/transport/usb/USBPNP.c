/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B P N P . C

GENERAL DESCRIPTION
    This file conatins plug-and-play related functions.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include <stdarg.h>
#include <stdio.h>
#include "USBMAIN.h"
#include "USBUTL.h"
#include "USBINT.h"
#include "USBPNP.h"
#include "USBRD.h"
#include "USBWT.h"
#include "USBDSP.h"
#include "USBPWR.h"
#include "USBIOC.h"
#include "..\..\qcversion.h"

#ifdef NDIS_WDM
#include "USBIF.h"
#endif

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBPNP.tmh"

#endif   // EVENT_TRACING

extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);

//{0x98b06a49, 0xb09e, 0x4896, {0x94, 0x46, 0xd9, 0x9a, 0x28, 0xca, 0x4e, 0x5d}};
GUID gQcFeatureGUID = {0x496ab098, 0x9eb0, 0x9648,  // in network byte order
                       {0x94, 0x46, 0xd9, 0x9a, 0x28, 0xca, 0x4e, 0x5d}};


static const PCHAR systemPowerStateList[] =
{
   "PowerSystemUnspecified",
   "PowerSystemWorking",
   "PowerSystemSleeping1",
   "PowerSystemSleeping2",
   "PowerSystemSleeping3",
   "PowerSystemHibernate",
   "PowerSystemShutdown",
   "PowerSystemMaximum"
};

static const PCHAR devicePowerStateList[] =
{
   "PowerDeviceUnspecified",
   "PowerDeviceD0",
   "PowerDeviceD1",
   "PowerDeviceD2",
   "PowerDeviceD3",
   "PowerDeviceMaximum"
};
#define USBPNP_SystemPowerToString(sysState) systemPowerStateList[sysState]
#define USBPNP_DevicePowerToString(devState) devicePowerStateList[devState]

NTSTATUS USBPNP_GetDeviceCapabilities
(
   PDEVICE_EXTENSION pDevExt,
   BOOLEAN bPowerManagement
)
{
    KEVENT   completionEvent;
    NTSTATUS status;
    PIRP     pIrp;
    PIO_STACK_LOCATION pNextStack;

    RtlZeroMemory(&pDevExt->DeviceCapabilities, sizeof(DEVICE_CAPABILITIES));
    pIrp = IoAllocateIrp(pDevExt->PhysicalDeviceObject->StackSize, FALSE);
    if (pIrp == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    else
    {
        pDevExt->DeviceCapabilities.Version = 1;
        pDevExt->DeviceCapabilities.Size = sizeof(DEVICE_CAPABILITIES);
        pDevExt->DeviceCapabilities.Address = 0xffffffff;
        pDevExt->DeviceCapabilities.UINumber = 0xffffffff;
        pNextStack = IoGetNextIrpStackLocation(pIrp);
        pNextStack->MajorFunction = IRP_MJ_PNP;
        pNextStack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;;
        pNextStack->Parameters.DeviceCapabilities.Capabilities = &pDevExt->DeviceCapabilities;
        pIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;

        KeInitializeEvent(&completionEvent, SynchronizationEvent, FALSE);
        IoSetCompletionRoutine(pIrp, QCUSB_CallUSBD_Completion, &completionEvent, TRUE, TRUE, TRUE);

        status = IoCallDriver(pDevExt->PhysicalDeviceObject, pIrp);
        if (status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&completionEvent, Executive, KernelMode, TRUE, NULL);
        }

        pDevExt->PowerDownLevel = PowerDeviceUnspecified;

        // determine the lowest power state that is higher than D3
        // if D3 is the lowest, auto power saving will be disabled
        for (int i = PowerSystemUnspecified; i < PowerSystemMaximum; i++)
        {
            if (pDevExt->DeviceCapabilities.DeviceState[i] < PowerDeviceD3)
            {
                pDevExt->PowerDownLevel = pDevExt->DeviceCapabilities.DeviceState[i];
            }

            if (!bPowerManagement)
            {
                pDevExt->DeviceCapabilities.DeviceState[i] = PowerDeviceUnspecified;
            }
        }

        if (pDevExt->PowerDownLevel <= PowerDeviceD0)
        {
            pDevExt->PowerDownLevel = PowerDeviceD2;
        }

        pDevExt->DeviceCapabilities.D1Latency = 4000;
        pDevExt->DeviceCapabilities.D2Latency = 5000;

        QCPWR_VerifyDeviceCapabilities(pDevExt);

        IoFreeIrp(pIrp);
        pIrp = NULL;

        QCUSB_DbgPrintG
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("WakeFromD0/1/2/3 = %u, %u, %u, %u\n",
                pDevExt->DeviceCapabilities.WakeFromD0,
                pDevExt->DeviceCapabilities.WakeFromD1,
                pDevExt->DeviceCapabilities.WakeFromD2,
                pDevExt->DeviceCapabilities.WakeFromD3)
        );

        QCUSB_DbgPrintG
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("SystemWake = %s\n", USBPNP_SystemPowerToString(pDevExt->DeviceCapabilities.SystemWake))
        );

        QCUSB_DbgPrintG
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("DeviceWake = %s\n", USBPNP_DevicePowerToString(pDevExt->DeviceCapabilities.DeviceWake))
        );

        for (int i = PowerSystemUnspecified; i < PowerSystemMaximum; i++)
        {
            QCUSB_DbgPrintG
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_DETAIL,
                ("Power State Mapping: sysstate %s = devstate %s\n",
                    USBPNP_SystemPowerToString(i),
                    USBPNP_DevicePowerToString(pDevExt->DeviceCapabilities.DeviceState[i]))
            );
        }

        QCUSB_DbgPrintG
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            (
                "D1-D3 Latency = %u, %u, %u (x 100us)\n",
                pDevExt->DeviceCapabilities.D1Latency,
                pDevExt->DeviceCapabilities.D2Latency,
                pDevExt->DeviceCapabilities.D3Latency)
        );

        QCUSB_DbgPrintG
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("DeviceCapabilities: PowerDownLevel = %s (PM %d)\n", USBPNP_DevicePowerToString(pDevExt->PowerDownLevel), bPowerManagement)
        );
    }
    return status;
}

NTSTATUS QCPNP_GetDeviceCID
(
    PDEVICE_OBJECT DeviceObject,
    PCHAR  ProductString,
    ULONG  ProductStrLen
)
{
   PCHAR             pCidLoc = ProductString;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->QCPNP_GetDeviceCID DO 0x%p\n", pDevExt->PortName, DeviceObject)
   );

   if (ProductStrLen == 0 || ProductString == NULL)
   {
       return USBUTL_DriverRegistryDelete(pDevExt, VEN_DEV_CID);
   }

   // search for "_CID:"
   int strLen = 0;
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
            break;
         }
         p++;
         adjusted++;
      }

      // Adjust length
      if (bMatchFound == TRUE)
      {
         int tmpLen = ProductStrLen - adjusted;
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
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_TRACE,
            ("<%s> QCPNP_GetDeviceCID: no CID found\n", pDevExt->PortName)
         );
         return STATUS_UNSUCCESSFUL;
      }
   }

   QCUSB_DbgPrint
   (
       QCUSB_DBG_MASK_CONTROL,
       QCUSB_DBG_LEVEL_TRACE,
       ("<%s> <--QCPNP_GetDeviceCID: strLen %d\n", pDevExt->PortName, strLen)
   );

   return USBUTL_DriverRegistrySetString(pDevExt, VEN_DEV_CID, pCidLoc, strLen);
}

NTSTATUS QCPNP_GetStringDescriptor
(
   PDEVICE_OBJECT DeviceObject,
   UCHAR          Index,
   USHORT         LanguageId,
   BOOLEAN        MatchPrefix
)
{
   PDEVICE_EXTENSION pDevExt;
   PUSB_STRING_DESCRIPTOR pSerNum;
   URB urb;
   NTSTATUS ntStatus;
   PCHAR pSerLoc = NULL;
   PCHAR pCidLoc = NULL;
   UNICODE_STRING ucValueName;
   HANDLE         hRegKey;
   INT            strLen;
   INT            productStrLen = 0;
   BOOLEAN        bSetEntry = FALSE;

   pDevExt = DeviceObject->DeviceExtension;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->_GetStringDescriptor DO 0x%p idx %d\n", pDevExt->PortName, DeviceObject, Index)
   );

   if (Index == 0)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> <--_GetStringDescriptor: index is NULL\n", pDevExt->PortName)
      );
      goto UpdateRegistry;
   }

   pSerNum = (PUSB_STRING_DESCRIPTOR)(pDevExt->DevSerialNumber);
   RtlZeroMemory(pDevExt->DevSerialNumber, 256);

   UsbBuildGetDescriptorRequest
   (
      &urb,
      (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
      USB_STRING_DESCRIPTOR_TYPE,
      Index,
      LanguageId,
      pSerNum,
      NULL,
      sizeof(USB_STRING_DESCRIPTOR_TYPE),
      NULL
   );

   ntStatus = QCUSB_CallUSBD(DeviceObject, &urb);
   if (!NT_SUCCESS(ntStatus))
   {
      goto UpdateRegistry;
   }

   UsbBuildGetDescriptorRequest
   (
      &urb,
      (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
      USB_STRING_DESCRIPTOR_TYPE,
      Index,
      LanguageId,
      pSerNum,
      NULL,
      pSerNum->bLength,
      NULL
   );

   ntStatus = QCUSB_CallUSBD(DeviceObject, &urb);
   if (!NT_SUCCESS(ntStatus))
   {
      RtlZeroMemory(pDevExt->DevSerialNumber, 256);
   }
   else
   {
      USBUTL_PrintBytes
      (
         (PVOID)(pDevExt->DevSerialNumber),
         pSerNum->bLength,
         256,
         "SerialNumber",
         pDevExt,
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_DETAIL
      );
   }

   if (!NT_SUCCESS(ntStatus))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> _GetStringDescriptor DO 0x%p failure NTS 0x%x\n", pDevExt->PortName,
           DeviceObject, ntStatus)
      );
      goto UpdateRegistry;
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> _GetStringDescriptor DO 0x%p NTS 0x%x (%dB)\n", pDevExt->PortName,
           DeviceObject, ntStatus, pSerNum->bLength)
      );
   }

   if (pSerNum->bLength > 2)
   {
      strLen = (INT)pSerNum->bLength -2;  // exclude 2 bytes in header
   }
   else
   {
      strLen = 0;
   }
   productStrLen = strLen;
   pSerLoc = (PCHAR)pSerNum->bString;
   pCidLoc = (PCHAR)pSerNum->bString;
   bSetEntry = TRUE;

   // search for "_SN:"
   if ((MatchPrefix == TRUE) && (strLen > 0))
   {
      INT   idx, adjusted = 0;
      PCHAR p = pSerLoc;
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
         strLen = (USHORT)(p - pSerLoc); // 18;
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_TRACE,
            ("<%s> <--QDBPNP_GetDeviceSerialNumber: no SN found\n", pDevExt->PortName)
         );
         ntStatus = STATUS_UNSUCCESSFUL;
         bSetEntry = FALSE;
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> _GetDeviceSerialNumber: strLen %d\n", pDevExt->PortName, strLen)
   );

UpdateRegistry:

   // update registry
   ntStatus = IoOpenDeviceRegistryKey
              (
                 pDevExt->PhysicalDeviceObject,
                 PLUGPLAY_REGKEY_DRIVER,
                 KEY_ALL_ACCESS,
                 &hRegKey
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> QDBPNP_GetDeviceSerialNumber: reg access failure 0x%x\n", pDevExt->PortName, ntStatus)
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
      ZwSetValueKey
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
   ZwClose(hRegKey);

   if (MatchPrefix == TRUE)
   {
      QCPNP_GetDeviceCID(DeviceObject, pCidLoc, productStrLen);
   }

   return ntStatus;

} // QCPNP_GetStringDescriptor

NTSTATUS QCPNP_GenericCompletion
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pContext;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> QCPNP_GenericCompletion (IRP 0x%p IoStatus: 0x%x)\n", pDevExt->PortName, pIrp, pIrp->IoStatus.Status)
   );

   KeSetEvent(pIrp->UserEvent, 0, FALSE);

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // QCPNP_GenericCompletion

NTSTATUS QCPNP_GetParentDeviceName(PDEVICE_EXTENSION pDevExt)
{
   CHAR parentDevName[MAX_NAME_LEN];
   PIRP pIrp = NULL;
   PIO_STACK_LOCATION nextstack;
   NTSTATUS ntStatus;
   KEVENT event;

   RtlZeroMemory(parentDevName, MAX_NAME_LEN);
   KeInitializeEvent(&event, SynchronizationEvent, FALSE);

   pIrp = IoAllocateIrp((CCHAR)(pDevExt->StackDeviceObject->StackSize+2), FALSE);
   if( pIrp == NULL )
   {
       QCUSB_DbgPrint
       (
          QCUSB_DBG_MASK_CONTROL,
          QCUSB_DBG_LEVEL_ERROR,
          ("<%s> QCPNP_GetParentDeviceName failed to allocate an IRP\n", pDevExt->PortName)
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
      (PIO_COMPLETION_ROUTINE)QCPNP_GenericCompletion,
      (PVOID)pDevExt,
      TRUE,TRUE,TRUE
   );

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> QCPNP_GetParentDeviceName (IRP 0x%p)\n", pDevExt->PortName, pIrp)
   );

   ntStatus = IoCallDriver(pDevExt->StackDeviceObject, pIrp);

   ntStatus = KeWaitForSingleObject(&event, Executive, KernelMode, TRUE, 0);

   if (ntStatus == STATUS_SUCCESS)
   {
       ntStatus = pIrp->IoStatus.Status;
       USBUTL_DriverRegistrySetString(pDevExt, VEN_DEV_PARENT, parentDevName, pIrp->IoStatus.Information);
   }
   else
   {
       USBUTL_DriverRegistryDelete(pDevExt, VEN_DEV_PARENT);
   }

   IoFreeIrp(pIrp);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--- QCPNP_GetParentDeviceName: ST %x\n", pDevExt->PortName, ntStatus)
   );

   return ntStatus;

}  // QCPNP_GetParentDeviceName

NTSTATUS USBPNP_StartDevice
(
    PDEVICE_OBJECT DeviceObject
)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    NTSTATUS status;
    PURB     pUrb;
    size_t   urbLength = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> StartDevice device object 0x%p\n", pDevExt->PortName, DeviceObject)
    );

    setDevState(DEVICE_STATE_PRESENT);

    if (pDevExt->pUsbDevDesc != NULL)
    {
        ExFreePool(pDevExt->pUsbDevDesc);
        pDevExt->pUsbDevDesc = NULL;
    }

    if (pDevExt->pUsbConfigDesc != NULL)
    {
        ExFreePool(pDevExt->pUsbConfigDesc);
        pDevExt->pUsbConfigDesc = NULL;
    }

    if (pDevExt->UsbConfigUrb != NULL)
    {
        ExFreePool(pDevExt->UsbConfigUrb);
        pDevExt->UsbConfigUrb = NULL;
    }

    for (int i = 0; i < MAX_INTERFACE; i++)
    {
        if (pDevExt->Interface[i] != NULL)
        {
            ExFreePool(pDevExt->Interface[i]);
            pDevExt->Interface[i] = NULL;
        }
    }

    // Read usb descriptor
    pUrb = ExAllocatePool(NonPagedPoolNx, urbLength);
    pDevExt->pUsbDevDesc = ExAllocatePool(NonPagedPoolNx, sizeof(USB_DEVICE_DESCRIPTOR));
    if (pUrb == NULL || pDevExt->pUsbDevDesc == NULL)
    {
        status = STATUS_NO_MEMORY;
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> StartDevice memory allocation failed pUrb: 0x%p, pUsbDevDesc: 0x%p\n", pDevExt->PortName, pUrb, pDevExt->pUsbDevDesc)
        );
    }
    else
    {
        UsbBuildGetDescriptorRequest
        (
            pUrb,
            urbLength,
            USB_DEVICE_DESCRIPTOR_TYPE,
            0,
            0,
            pDevExt->pUsbDevDesc,
            NULL,
            sizeof(USB_DEVICE_DESCRIPTOR),
            NULL
        );
        status = QCUSB_CallUSBD(DeviceObject, pUrb);
        if (NT_SUCCESS(status))
        {
            DbgPrint("Device Descriptor 0x%p, size %u\n", pDevExt->pUsbDevDesc, pUrb->UrbControlDescriptorRequest.TransferBufferLength);
            DbgPrint("USB Device Descriptor:\n");
            DbgPrint("-------------------------\n");
            DbgPrint("bLength %d\n", pDevExt->pUsbDevDesc->bLength);
            DbgPrint("bDescriptorType 0x%x\n", pDevExt->pUsbDevDesc->bDescriptorType);
            DbgPrint("bcdUSB 0x%x\n", pDevExt->pUsbDevDesc->bcdUSB);
            DbgPrint("bDeviceClass 0x%x\n", pDevExt->pUsbDevDesc->bDeviceClass);
            DbgPrint("bDeviceSubClass 0x%x\n", pDevExt->pUsbDevDesc->bDeviceSubClass);
            DbgPrint("bDeviceProtocol 0x%x\n", pDevExt->pUsbDevDesc->bDeviceProtocol);
            DbgPrint("bMaxPacketSize0 0x%x\n", pDevExt->pUsbDevDesc->bMaxPacketSize0);
            DbgPrint("idVendor 0x%x\n", pDevExt->pUsbDevDesc->idVendor);
            DbgPrint("idProduct 0x%x\n", pDevExt->pUsbDevDesc->idProduct);
            DbgPrint("bcdDevice 0x%x\n", pDevExt->pUsbDevDesc->bcdDevice);
            DbgPrint("iManufacturer 0x%x\n", pDevExt->pUsbDevDesc->iManufacturer);
            DbgPrint("iProduct 0x%x\n", pDevExt->pUsbDevDesc->iProduct);
            DbgPrint("iSerialNumber 0x%x\n", pDevExt->pUsbDevDesc->iSerialNumber);
            DbgPrint("bNumConfigurations 0x%x\n", pDevExt->pUsbDevDesc->bNumConfigurations);
            DbgPrint("-------------------------\n");

            status = QCPNP_GetParentDeviceName(pDevExt);
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_INFO,
                ("<%s> StartDevice: GetParentDeviceName status 0x%x\n", pDevExt->PortName, status)
            );

            status = QCPNP_GetStringDescriptor(DeviceObject, pDevExt->pUsbDevDesc->iProduct, 0x0409, TRUE);
            if (!NT_SUCCESS(status))
            {
                if (pDevExt->pUsbDevDesc->iProduct != 2)
                {
                    status = QCPNP_GetStringDescriptor(DeviceObject, 0x02, 0x0409, TRUE);
                }
            }
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_INFO,
                ("<%s> StartDevice: GetStringDescriptor status 0x%x, iProduct 0x%x\n", pDevExt->PortName, status, pDevExt->pUsbDevDesc->iProduct)
            );

            status = QCPNP_GetStringDescriptor(DeviceObject, pDevExt->pUsbDevDesc->iSerialNumber, 0x0409, FALSE);
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_INFO,
                ("<%s> StartDevice: GetStringDescriptor status 0x%x, iSerialNumber 0x%x\n", pDevExt->PortName, status, pDevExt->pUsbDevDesc->iSerialNumber)
            );

            pDevExt->idVendor = pDevExt->pUsbDevDesc->idVendor;
            pDevExt->idProduct = pDevExt->pUsbDevDesc->idProduct;

            if (pDevExt->pUsbDevDesc->bcdUSB == QC_HSUSB_VERSION)
            {
                pDevExt->HighSpeedUsbOk |= QC_HSUSB_VERSION_OK;
            }
            if (pDevExt->pUsbDevDesc->bcdUSB >= QC_SSUSB_VERSION)
            {
                pDevExt->HighSpeedUsbOk |= QC_SSUSB_VERSION_OK;
            }

            if (USBPNP_ValidateDeviceDescriptor(pDevExt, pDevExt->pUsbDevDesc) == TRUE)
            {
                status = STATUS_SUCCESS;
            }
            else
            {
                status = STATUS_UNSUCCESSFUL;
            }
        }
        else if (status == STATUS_DEVICE_NOT_READY)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> StartDevice: device is not ready\n", pDevExt->PortName)
            );
        }
        else
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_CRITICAL,
                ("<%s> StartDevice: call usbd error 0x%x\n", pDevExt->PortName, status)
            );
        }
    }

    if (NT_SUCCESS(status))
    {
        for (int index = 0; index < pDevExt->pUsbDevDesc->bNumConfigurations; index++)
        {
            if (NT_SUCCESS(status == USBPNP_ConfigureUsbDevice(DeviceObject, index)))
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_CONTROL,
                    QCUSB_DBG_LEVEL_INFO,
                    ("<%s> StartDevice: ConfigureDevice completed index %d\n", pDevExt->PortName, index)
                );
                break;
            }
        }
        if (NT_SUCCESS(status))
        {
            pDevExt->bDeviceRemoved = FALSE;
            pDevExt->bDeviceSurpriseRemoved = FALSE;

            if ((pDevExt->MuxInterface.MuxEnabled != 0x01) ||
                (pDevExt->MuxInterface.InterfaceNumber == pDevExt->MuxInterface.PhysicalInterfaceNumber))
            {
                // reset usb pipes
                LARGE_INTEGER retryTimeout;
                retryTimeout.QuadPart = -(4 * 1000 * 1000); // 0.4 sec
                int retryCount = 10;
                for (int i = 0; i < retryCount; i++)
                {
                    status = QCUSB_ResetInt(DeviceObject, QCUSB_RESET_PIPE_AND_ENDPOINT);
                    if (NT_SUCCESS(status))
                    {
                        status = QCUSB_ResetInput(DeviceObject, QCUSB_RESET_PIPE_AND_ENDPOINT);
                        if (NT_SUCCESS(status))
                        {
                            status = QCUSB_ResetOutput(DeviceObject, QCUSB_RESET_PIPE_AND_ENDPOINT);
                            if (NT_SUCCESS(status))
                            {
                                QCUSB_DbgPrint
                                (
                                    QCUSB_DBG_MASK_CONTROL,
                                    QCUSB_DBG_LEVEL_INFO,
                                    ("<%s> StartDevice: ResetPipe complete\n", pDevExt->PortName)
                                );
                                break;
                            }
                        }
                    }
                    if (!NT_SUCCESS(status))
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_CONTROL,
                            QCUSB_DBG_LEVEL_INFO,
                            ("<%s> StartDevice: ResetPipe failed attempt %d\n", pDevExt->PortName, i)
                        );
                        KeDelayExecutionThread(KernelMode, FALSE, &retryTimeout);
                    }
                }
            }

            if (NT_SUCCESS(status))
            {
                if (USBIF_IsUsbBroken(DeviceObject) == TRUE)
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_CONTROL,
                        QCUSB_DBG_LEVEL_CRITICAL,
                        ("<%s> StartDevice: usb is broken, exit\n", pDevExt->PortName)
                    );
                    status = STATUS_DELETE_PENDING;
                }
                else
                {
                    if ((pDevExt->MuxInterface.MuxEnabled != 0x01) ||
                        (pDevExt->MuxInterface.InterfaceNumber == pDevExt->MuxInterface.PhysicalInterfaceNumber))
                    {
                        status = USBCTL_EnableDataBundling(DeviceObject, pDevExt->DataInterface, QC_LINK_DL);
                        if (!NT_SUCCESS(status))
                        {
                            QCUSB_DbgPrint
                            (
                                QCUSB_DBG_MASK_CONTROL,
                                QCUSB_DBG_LEVEL_ERROR,
                                ("<%s> StartDevice: EnableDataBundling failed status 0x%x\n", pDevExt->PortName, status)
                            );
                        }
                    }

                    status = USBRD_InitializeL2Buffers(pDevExt);
                    if (NT_SUCCESS(status))
                    {
                        KeClearEvent(&pDevExt->CancelInterruptPipeEvent);
                        status = USBINT_InitInterruptPipe(DeviceObject);
                        if (pDevExt->InterruptPipe == 0xFF)
                        {
                            status = STATUS_SUCCESS;
                        }
                    }
                    else
                    {
                        QCUSB_DbgPrint
                        (
                            QCUSB_DBG_MASK_CONTROL,
                            QCUSB_DBG_LEVEL_FORCE,
                            ("<%s> StartDevice: InitializeL2Buffers failed status 0x%x\n", pDevExt->PortName, status)
                        );
                    }
                }
            }
            else
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_CONTROL,
                    QCUSB_DBG_LEVEL_INFO,
                    ("<%s> StartDevice: ResetPipe failed for all attempts\n", pDevExt->PortName)
                );
            }
        }
        else
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_INFO,
                ("<%s> StartDevice: ConfigureDevice failed status 0x%x\n", pDevExt->PortName, status)
            );
        }
    }

    if (NT_SUCCESS(status))
    {
        USBUTL_DriverRegistrySetDword(pDevExt, VEN_DEV_TIME, 1);
    }
    else
    {
        clearDevState(DEVICE_STATE_PRESENT_AND_STARTED);
        USBRD_CancelReadThread(pDevExt, 3);
        USBWT_CancelWriteThread(pDevExt, 3);
        USBDSP_CancelDispatchThread(pDevExt, 2);
        USBINT_CancelInterruptThread(pDevExt, 1);
    }

    if (pUrb != NULL)
    {
        ExFreePool(pUrb);
    }

    return status;
}

NTSTATUS USBPNP_ConfigureUsbDevice
(
    PDEVICE_OBJECT DeviceObject,
    UCHAR DescriptorIndex
)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    NTSTATUS status;
    ULONG    urbSize = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);
    PURB     pUrb;
    ULONG    configDescriptorSize = sizeof(USB_CONFIGURATION_DESCRIPTOR);
    PUSB_CONFIGURATION_DESCRIPTOR pConfigDescriptor = NULL;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> -->USBPNP_ConfigureDevice DO 0x%p, Index 0x%x\n", pDevExt->PortName, DeviceObject, DescriptorIndex)
    );

    pUrb = ExAllocatePool(NonPagedPoolNx, urbSize);
    if (pUrb == NULL)
    {
        status = STATUS_NO_MEMORY;
    }
    else
    {
        pConfigDescriptor = ExAllocatePool(NonPagedPoolNx, configDescriptorSize);
        if (pConfigDescriptor == NULL)
        {
            status = STATUS_NO_MEMORY;
        }
        else
        {
            UsbBuildGetDescriptorRequest
            (
                pUrb,
                urbSize,
                USB_CONFIGURATION_DESCRIPTOR_TYPE,
                DescriptorIndex,
                0x0,
                pConfigDescriptor,
                NULL,
                configDescriptorSize,
                NULL
            );
            status = QCUSB_CallUSBD(DeviceObject, pUrb);
            if (NT_SUCCESS(status))
            {
                if (USBPNP_ValidateConfigDescriptor(pDevExt, pConfigDescriptor) == TRUE)
                {
                    // get the real length of descriptor string
                    configDescriptorSize = pConfigDescriptor->wTotalLength;
                    // delete the previous configuration descriptor object
                    ExFreePool(pConfigDescriptor);
                    // allocate new configuration descriptor
                    pConfigDescriptor = ExAllocatePool(NonPagedPoolNx, configDescriptorSize);
                    if (pConfigDescriptor == NULL)
                    {
                        status = STATUS_NO_MEMORY;
                    }
                    else
                    {
                        UsbBuildGetDescriptorRequest
                        (
                            pUrb,
                            urbSize,
                            USB_CONFIGURATION_DESCRIPTOR_TYPE,
                            DescriptorIndex,
                            0x0,
                            pConfigDescriptor,
                            NULL,
                            configDescriptorSize,
                            NULL
                        );
                        status = QCUSB_CallUSBD(DeviceObject, pUrb);
                        if (NT_SUCCESS(status))
                        {
                            if (USBPNP_ValidateConfigDescriptor(pDevExt, pConfigDescriptor) == TRUE)
                            {
                                pDevExt->pUsbConfigDesc = pConfigDescriptor;
                                pDevExt->bmAttributes = pConfigDescriptor->bmAttributes;
                                status = USBPNP_SelectInterfaces(DeviceObject);
                                if ((pDevExt->MuxInterface.MuxEnabled != 0x01) ||
                                    (pDevExt->MuxInterface.InterfaceNumber == pDevExt->MuxInterface.PhysicalInterfaceNumber))
                                {
                                    if (NT_SUCCESS(status) && ((pDevExt->bmAttributes & REMOTE_WAKEUP_MASK) != 0))
                                    {
                                        QCUSB_ClearRemoteWakeup(DeviceObject);
                                    }
                                }
                                if (pDevExt->bmAttributes & SELF_POWERED_MASK)
                                {
                                    pDevExt->bDeviceSelfPowered = TRUE;
                                }
                                pConfigDescriptor = NULL;
                            }
                            else
                            {
                                pDevExt->pUsbConfigDesc = NULL;
                                status = STATUS_UNSUCCESSFUL;
                            }
                        }
                    }
                }
                else
                {
                    status = STATUS_UNSUCCESSFUL;
                }
            }
        }
    }

    if (pUrb != NULL)
    {
        ExFreePool(pUrb);
    }

    if (pConfigDescriptor != NULL)
    {
        ExFreePool(pConfigDescriptor);
    }

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> <--USBPNP_ConfigureDevice Status 0x%x\n", pDevExt->PortName, status)
    );

    return status;
}

NTSTATUS USBPNP_SelectInterfaces
(
    PDEVICE_OBJECT DeviceObject

)
{
    PURB pUrb;
    NTSTATUS status;
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    USBD_INTERFACE_LIST_ENTRY interfaceList[MAX_INTERFACE];
    PUSB_INTERFACE_DESCRIPTOR interfaceDescList[MAX_INTERFACE];
    PUSBD_INTERFACE_INFORMATION interfaceInfoList[MAX_INTERFACE];
    PUSB_CONFIGURATION_DESCRIPTOR pUsbConfigDesc = pDevExt->pUsbConfigDesc;

    if (pUsbConfigDesc == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    pDevExt->IsEcmModel = FALSE;
    pDevExt->InterruptPipe = 0xFF;
    pDevExt->BulkPipeInput = 0xFF;
    pDevExt->BulkPipeOutput = 0xFF;
    pDevExt->ControlInterface = 0;
    pDevExt->DataInterface = 0;

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_DETAIL,
        ("<%s> <--USBPNP_SelectInterfaces bDescriptorType 0x%x, bLength 0x%x, bNumInterfaces 0x%x, MaxPower 0x%x, wTotalLength 0x%x\n",
            pDevExt->PortName,
            pUsbConfigDesc->bDescriptorType,
            pUsbConfigDesc->bLength,
            pUsbConfigDesc->bNumInterfaces,
            pUsbConfigDesc->MaxPower,
            pUsbConfigDesc->wTotalLength)
    );

    if (pUsbConfigDesc->bNumInterfaces > MAX_INTERFACE)
    {
        return STATUS_NO_MEMORY;
    }

    for (int i = 0; i < MAX_INTERFACE; i++)
    {
        pDevExt->Interface[i] = NULL;
        interfaceList[i].Interface = NULL;
        interfaceList[i].InterfaceDescriptor = NULL;
    }

    // Parse through all descriptors in the whole USB configuration
    for (UCHAR* pStart = (UCHAR*)pUsbConfigDesc, *pEnd = pStart + pUsbConfigDesc->wTotalLength; pStart < pEnd; pStart += pStart[0])
    {
        if (pStart[1] == USB_CDC_CS_INTERFACE)
        {
            if (pStart[2] == USB_CDC_FD_MDLM)
            {
                // verify byte-stuffing feature
                GUID* featureGuid = (GUID*)(pStart + 5);
                if ((featureGuid->Data1 == gQcFeatureGUID.Data1) &&
                    (featureGuid->Data2 == gQcFeatureGUID.Data2) &&
                    (featureGuid->Data3 == gQcFeatureGUID.Data3) &&
                    (featureGuid->Data4[0] == gQcFeatureGUID.Data4[0]) &&
                    (featureGuid->Data4[1] == gQcFeatureGUID.Data4[1]) &&
                    (featureGuid->Data4[2] == gQcFeatureGUID.Data4[2]) &&
                    (featureGuid->Data4[3] == gQcFeatureGUID.Data4[3]) &&
                    (featureGuid->Data4[4] == gQcFeatureGUID.Data4[4]) &&
                    (featureGuid->Data4[5] == gQcFeatureGUID.Data4[5]) &&
                    (featureGuid->Data4[6] == gQcFeatureGUID.Data4[6]) &&
                    (featureGuid->Data4[7] == gQcFeatureGUID.Data4[7]))
                {
                    pDevExt->bVendorFeature = TRUE;
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_INFO,
                        ("<%s>: bVendorFeature set to ON\n", pDevExt->PortName)
                    );
                }
            }
            else if (pStart[2] == USB_CDC_FD_MDLMD)
            {
                if (pStart[0] >= 6)
                {
                    pDevExt->DeviceControlCapabilities = pStart[4];
                    pDevExt->DeviceDataCapabilities = pStart[5];
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_CRITICAL,
                        ("<%s>: Read controlCapabilities 0x%x, dataCapabilities 0x%x\n", pDevExt->PortName, pStart[4], pStart[5])
                    );
                }
            }
        }
    }

    // Iterate throught the interface descriptors
    ULONG interfaceCount = 0;
    PVOID pStartPosition = (PVOID)((PCHAR)pUsbConfigDesc + pUsbConfigDesc->bLength);
    PUSB_INTERFACE_DESCRIPTOR pInterfaceDesc;
    do
    {
        pInterfaceDesc = USBD_ParseConfigurationDescriptorEx
        (
            pUsbConfigDesc,
            pStartPosition,
            -1,
            -1,
            -1,
            -1,
            -1
        );
        if (pInterfaceDesc != NULL)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_DETAIL,
                ("<%s> SelectInterfaces: %lu, bAlternateSetting 0x%x, bDescriptorType 0x%x, bInterfaceClass 0x%x, bInterfaceNumber 0x%x, bInterfaceProtocol 0x%x, bInterfaceSubClass 0x%x\n",
                    pDevExt->PortName,
                    interfaceCount,
                    pInterfaceDesc->bAlternateSetting,
                    pInterfaceDesc->bDescriptorType,
                    pInterfaceDesc->bInterfaceClass,
                    pInterfaceDesc->bInterfaceNumber,
                    pInterfaceDesc->bInterfaceProtocol,
                    pInterfaceDesc->bInterfaceSubClass)
            );

            pDevExt->IfProtocol =
                ((ULONG)pInterfaceDesc->bInterfaceProtocol)      |
                ((ULONG)pInterfaceDesc->bInterfaceClass)   << 8  |
                ((ULONG)pInterfaceDesc->bAlternateSetting) << 16 |
                ((ULONG)pInterfaceDesc->bInterfaceNumber)  << 24;

            if ((interfaceCount == 0) &&
                (pInterfaceDesc->bInterfaceClass == CDCC_COMMUNICATION_INTERFACE_CLASS) &&
                (pInterfaceDesc->bInterfaceSubClass == CDCC_ECM_INTERFACE_CLASS) &&
                (pInterfaceDesc->bAlternateSetting == 0) &&
                (pInterfaceDesc->bNumEndpoints == 1))
            {
                pDevExt->IsEcmModel = TRUE;
            }

            if ((pDevExt->IsEcmModel == TRUE) && (interfaceCount > 0))
            {
                if ((pInterfaceDesc->bAlternateSetting != 0) &&
                    (pInterfaceDesc->bInterfaceClass == CDCC_DATA_INTERFACE_CLASS) &&
                    (pInterfaceDesc->bInterfaceSubClass == 0) &&
                    (pInterfaceDesc->bInterfaceProtocol == 0))
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_CONTROL,
                        QCUSB_DBG_LEVEL_DETAIL,
                        ("<%s> SelectInterfaces interface %lu alternate setting %u selected\n",
                            pDevExt->PortName, pInterfaceDesc->bInterfaceNumber, pInterfaceDesc->bAlternateSetting)
                    );

                    // Select alternate setting 1 to enable the data interface
                    interfaceList[interfaceCount++].InterfaceDescriptor = pInterfaceDesc;
                    pDevExt->HighSpeedUsbOk |= QC_HSUSB_ALT_SETTING_OK;
                }
            }
            else
            {
                if ((pInterfaceDesc->bAlternateSetting != 0) && (pInterfaceDesc->bNumEndpoints > 1))
                {
                    interfaceList[--interfaceCount].InterfaceDescriptor = pInterfaceDesc;
                    pDevExt->SetCommFeatureSupported = FALSE;
                    pDevExt->HighSpeedUsbOk |= QC_HSUSB_ALT_SETTING_OK;
                }
                else
                {
                    pDevExt->SetCommFeatureSupported = TRUE;
                    interfaceList[interfaceCount++].InterfaceDescriptor = pInterfaceDesc;
                }
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_CONTROL,
                    QCUSB_DBG_LEVEL_DETAIL,
                    ("<%s> SelectInterfaces interface %lu alternate setting %u selected\n",
                        pDevExt->PortName, pInterfaceDesc->bInterfaceNumber, pInterfaceDesc->bAlternateSetting)
                );

                USBUTL_DriverRegistrySetDword(pDevExt, VEN_DEV_PROTOC, pDevExt->IfProtocol);
                USBUTL_DriverRegistrySetDword(pDevExt, VEN_DEV_SSR, 0); // reset SSR
            }

            if (interfaceCount >= MAX_INTERFACE)
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_CONTROL,
                    QCUSB_DBG_LEVEL_CRITICAL,
                    ("<%s> SelectInterfaces: error - no space for interfaces count %lu\n", pDevExt->PortName, MAX_INTERFACE)
                );
                return STATUS_UNSUCCESSFUL;
            }

            pStartPosition = (PVOID)((PCHAR)pInterfaceDesc + pInterfaceDesc->bLength);
        }
    } while (pInterfaceDesc != NULL);

    pUrb = USBD_CreateConfigurationRequestEx(pUsbConfigDesc, interfaceList);
    if (pUrb == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        PUSB_INTERFACE_DESCRIPTOR pInterfaceDesc = NULL;
        PUSBD_INTERFACE_INFORMATION pInterfaceInfo = NULL;
        PUSBD_PIPE_INFORMATION pPipeInfo = NULL;
        pDevExt->UsbConfigUrb = pUrb;
        ULONG pipeCount = 0;
        for (int i = 0; i < MAX_INTERFACE; i++)
        {
            if (interfaceList[i].InterfaceDescriptor != NULL)
            {
                pInterfaceDesc = interfaceList[i].InterfaceDescriptor;
                pInterfaceInfo = interfaceList[i].Interface;

                if (pInterfaceInfo == NULL)
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_CONTROL,
                        QCUSB_DBG_LEVEL_CRITICAL,
                        ("<%s> SelectInterfaces pInterfaceInfo is NULL for index %d\n", pDevExt->PortName, i)
                    );
                    ExFreePool(pUrb);
                    pDevExt->UsbConfigUrb = NULL;
                    return STATUS_NO_MEMORY;
                }

                for (int j = 0; j < pInterfaceInfo->NumberOfPipes; j++)
                {
                    pInterfaceInfo->Pipes[j].MaximumTransferSize = pDevExt->MaxPipeXferSize;
                }
                pipeCount += pInterfaceInfo->NumberOfPipes;
                pInterfaceInfo->Length = GET_USBD_INTERFACE_SIZE(pInterfaceDesc->bNumEndpoints);
                pInterfaceInfo->InterfaceNumber = pInterfaceDesc->bInterfaceNumber;
                pInterfaceInfo->AlternateSetting = pInterfaceDesc->bAlternateSetting;
            }
        }

        if (pipeCount > MAX_IO_QUEUES)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> SelectInterfaces pipe count overflow error: %d of %d\n", pDevExt->PortName, pipeCount, MAX_IO_QUEUES)
            );
            ExFreePool(pUrb);
            pDevExt->UsbConfigUrb = NULL;
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> SelectInterfaces configuration URB Length: %d, Function: %x\n", pDevExt->PortName,
                pUrb->UrbSelectConfiguration.Hdr.Length,
                pUrb->UrbSelectConfiguration.Hdr.Function)
        );

        status = QCUSB_CallUSBD(DeviceObject, pUrb);
        if ((NT_SUCCESS(status)) && (USBD_SUCCESS(pUrb->UrbSelectConfiguration.Hdr.Status)))
        {
            pDevExt->ConfigurationHandle = pUrb->UrbSelectConfiguration.ConfigurationHandle;

            for (int i = 0; i < pUsbConfigDesc->bNumInterfaces; i++)
            {
                pInterfaceInfo = interfaceList[i].Interface;
                pDevExt->Interface[pInterfaceInfo->InterfaceNumber] = ExAllocatePool(NonPagedPoolNx, pInterfaceInfo->Length);
                if (pDevExt->Interface[pInterfaceInfo->InterfaceNumber] != NULL)
                {
                    RtlCopyMemory(pDevExt->Interface[pInterfaceInfo->InterfaceNumber], pInterfaceInfo, pInterfaceInfo->Length);

                    // the following codes work only when there is exactly one interface
                    for (int pipeIndex = 0; pipeIndex < pInterfaceInfo->NumberOfPipes; pipeIndex++)
                    {
                        pPipeInfo = &pInterfaceInfo->Pipes[pipeIndex];
                        if (pPipeInfo->PipeType == UsbdPipeTypeBulk)
                        {
                            if (((pPipeInfo->EndpointAddress) & 0x80) == 0) // BULK OUT
                            {
                                pDevExt->wMaxPktSize = pPipeInfo->MaximumPacketSize;
                                if (pDevExt->BulkPipeOutput == (UCHAR)-1)
                                {
                                    pDevExt->DataInterface = pInterfaceInfo->InterfaceNumber;
                                    pDevExt->BulkPipeOutput = pipeIndex;
                                }
                            }
                            else // BULK IN
                            {
                                if (pDevExt->BulkPipeInput == (UCHAR)-1)
                                {
                                    pDevExt->BulkPipeInput = pipeIndex;
                                }
                            }
                        }
                        else if (pPipeInfo->PipeType == UsbdPipeTypeInterrupt)
                        {
                            if (pDevExt->InterruptPipe == (UCHAR)-1)
                            {
                                pDevExt->usCommClassInterface = pInterfaceInfo->InterfaceNumber;
                                pDevExt->InterruptPipe = pipeIndex;
                            }
                        }
                    }
                }
                else
                {
                    QCUSB_DbgPrint
                    (
                        QCUSB_DBG_MASK_CONTROL,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> SelectInterfaces memory allocation error interface num 0x%x len 0x%x\n", pDevExt->PortName,
                            pInterfaceInfo->InterfaceNumber, pInterfaceInfo->InterfaceNumber)
                    );
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
            }
        }

        if (pDevExt->wMaxPktSize == QC_HSUSB_BULK_MAX_PKT_SZ)
        {
            pDevExt->HighSpeedUsbOk |= QC_HSUSB_BULK_MAX_PKT_OK;
        }

        if (NT_SUCCESS(status))
        {
            if (pDevExt->BulkPipeInput != (UCHAR)-1 && pDevExt->BulkPipeOutput != (UCHAR)-1)
            {
                if (pDevExt->InterruptPipe != (UCHAR)-1)
                {
                    pDevExt->ucModelType = MODELTYPE_NET;
                    gModemType = MODELTYPE_NET;

                    DbgPrint("\n   ============ Loaded ==========\n");
                    DbgPrint("   | Device Type: CDC(%02X)       |\n", pDevExt->DataInterface);
                    DbgPrint("   |   Version: %-10s      |\n", gVendorConfig.DriverVersion);
                    DbgPrint("   |   Device:  %-10s      |\n", pDevExt->PortName);
                    DbgPrint("   |   IF: Ctrl%02d-Comm%02d-Data%02d       |\n", pDevExt->ControlInterface,
                        pDevExt->usCommClassInterface, pDevExt->DataInterface);
                    DbgPrint("   |   EP(0x%x, 0x%x, 0x%x) HS 0x%x  ATTR 0x%x |\n",
                        pDevExt->Interface[pDevExt->DataInterface]
                        ->Pipes[pDevExt->BulkPipeInput].EndpointAddress,
                        pDevExt->Interface[pDevExt->DataInterface]
                        ->Pipes[pDevExt->BulkPipeOutput].EndpointAddress,
                        pDevExt->Interface[pDevExt->usCommClassInterface]
                        ->Pipes[pDevExt->InterruptPipe].EndpointAddress,
                        pDevExt->HighSpeedUsbOk, pDevExt->bmAttributes);
                    DbgPrint("Driver Version %s\n", QC_NET_PRODUCT_VERSION_STRING);
                    DbgPrint("   |============================|\n");
                }
                else
                {
                    pDevExt->ucModelType = MODELTYPE_NET_LIKE;
                    gModemType = MODELTYPE_NET_LIKE;

                    DbgPrint("\n   ============== Loaded ===========\n");
                    DbgPrint("   | DeviceType: NET0(%02X)          |\n", pDevExt->DataInterface);
                    DbgPrint("   |   Version: %-10s         |\n", gVendorConfig.DriverVersion);
                    DbgPrint("   |   Device:  %-10s         |\n", pDevExt->PortName);
                    DbgPrint("   |   IF: Ctrl%02d-Comm%02d-Data%02d          |\n", pDevExt->ControlInterface,
                        pDevExt->usCommClassInterface, pDevExt->DataInterface);
                    DbgPrint("   |   EP(0x%x, 0x%x) HS 0x%x  ATTR 0x%x |\n",
                        pDevExt->Interface[pDevExt->DataInterface]
                        ->Pipes[pDevExt->BulkPipeInput].EndpointAddress,
                        pDevExt->Interface[pDevExt->DataInterface]
                        ->Pipes[pDevExt->BulkPipeOutput].EndpointAddress,
                        pDevExt->HighSpeedUsbOk, pDevExt->bmAttributes);
                    DbgPrint("Driver Version %s\n", QC_NET_PRODUCT_VERSION_STRING);
                    DbgPrint("   |===============================|\n");
                }

                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_CONTROL,
                    QCUSB_DBG_LEVEL_INFO,
                    ("<%s> MaxPipeXferSize: %u Bytes\n\n", pDevExt->PortName, pDevExt->MaxPipeXferSize)
                );
            }
            else
            {
                QCUSB_DbgPrint
                (
                    QCUSB_DBG_MASK_CONTROL,
                    QCUSB_DBG_LEVEL_INFO,
                    ("<%s> Modem Type: NONE, MaxPipeXferSize: %u Bytes", pDevExt->PortName, pDevExt->MaxPipeXferSize)
                );
                status = STATUS_INSUFFICIENT_RESOURCES;
                pDevExt->ucModelType = MODELTYPE_NONE;
                gModemType = MODELTYPE_NONE;
            }
        }

        if (pInterfaceInfo != NULL)
        {
            USBDSP_GetMUXInterface(pDevExt, pInterfaceInfo->InterfaceNumber);
        }

        if (pDevExt->MuxInterface.MuxEnabled == 0x01)
        {
            status = STATUS_SUCCESS;
            if (pDevExt->MuxInterface.PhysicalInterfaceNumber != pDevExt->MuxInterface.InterfaceNumber)
            {
                pDevExt->ucModelType = MODELTYPE_NET_LIKE;
            }
        }

        if (NT_SUCCESS(status) && pDevExt->ucModelType == MODELTYPE_NONE)
        {
            QCUSB_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_CRITICAL,
                ("<%s> SelectInterfaces failed to identify device type", pDevExt->PortName)
            );
            status = STATUS_UNSUCCESSFUL;
        }

        if (!NT_SUCCESS(status))
        {
            for (int i = 0; i < MAX_INTERFACE; i++)
            {
                if (pDevExt->Interface[i] != NULL)
                {
                    ExFreePool(pDevExt->Interface[i]);
                    pDevExt->Interface[i] = NULL;
                }
            }
        }
    }

    if (pUrb != NULL)
    {
        ExFreePool(pUrb);
        pDevExt->UsbConfigUrb = NULL;
    }

    return status;
}

NTSTATUS USBPNP_StopDevice
(
    PDEVICE_OBJECT DeviceObject
)
{
    PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
    NTSTATUS status;
    ULONG    urbSize = sizeof(struct _URB_SELECT_CONFIGURATION);
    PURB     pUrb    = ExAllocatePool(NonPagedPool, urbSize);

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> -->USBPNP_StopDevice DO 0x%p\n", pDevExt->PortName, DeviceObject)
    );

    // send a select confuguration request with NULL descriptor
    // to set the device into unconfigured state

    if (pUrb)
    {
        UsbBuildSelectConfigurationRequest(pUrb, urbSize, NULL);
        status = QCUSB_CallUSBD(DeviceObject, pUrb);
        ExFreePool(pUrb);
    }
    else
    {
        status = STATUS_NO_MEMORY;
    }

    QCUSB_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> <--USBPNP_StopDevice ST 0x%x\n", pDevExt->PortName, status)
    );
    return status;
}

NTSTATUS USBPNP_InitDevExt
(
#ifdef NDIS_WDM
   NDIS_HANDLE     WrapperConfigurationContext,
   LONG            QosSetting,
#else
   PDRIVER_OBJECT  pDriverObject,
   LONG            Reserved,
#endif
   PDEVICE_OBJECT  PhysicalDeviceObject,
   PDEVICE_OBJECT  deviceObject,
   char*           myPortName
)
{
   NTSTATUS            ntStatus               = STATUS_SUCCESS;
   PDEVICE_EXTENSION   pDevExt                = NULL;
   PUCHAR              pucNewReadBuffer       = NULL;
   char                myDeviceName[32];

   UNICODE_STRING ucDeviceMapEntry;  // "\Device\QCOMSERn(nn)"
   UNICODE_STRING ucPortName;        // "COMn(n)"
   UNICODE_STRING ucDeviceNumber;    // "n(nn)"
   UNICODE_STRING ucDeviceNameBase;  // "QCOMSER" from registry
   UNICODE_STRING ucDeviceName;      // "QCOMSERn(nn)"
   UNICODE_STRING ucValueName;
   UNICODE_STRING tmpUnicodeString;
   ANSI_STRING    tmpAnsiString;
   POWER_STATE    initialPwrState;
   int            i;
   ULONG selectiveSuspendIdleTime = 0;
   ULONG selectiveSuspendInMili = 0;

   _zeroUnicode(ucDeviceMapEntry);
   _zeroUnicode(ucPortName);
   _zeroUnicode(ucDeviceNumber);
   _zeroUnicode(ucDeviceNameBase);
   _zeroUnicode(ucDeviceName);
   _zeroUnicode(tmpUnicodeString);

   QCUSB_DbgPrintG
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> USBPNP_InitDevExt: 0x%p\n", myPortName, deviceObject)
   );
   // Initialize device extension
   pDevExt = deviceObject->DeviceExtension;
   RtlZeroMemory(pDevExt, sizeof(DEVICE_EXTENSION));

   // StackDeviceObject will be assigned by FDO
   pDevExt->PhysicalDeviceObject = PhysicalDeviceObject;

   strcpy(pDevExt->PortName, myPortName);
   pDevExt->bFdoReused           = FALSE;
   pDevExt->bVendorFeature       = FALSE;
   pDevExt->MyDeviceObject       = deviceObject;

   // device config parameters
   pDevExt->UseMultiReads       = gVendorConfig.UseMultiReads;
   pDevExt->UseMultiWrites      = gVendorConfig.UseMultiWrites;
   pDevExt->ContinueOnOverflow  = gVendorConfig.ContinueOnOverflow;
   pDevExt->ContinueOnDataError = gVendorConfig.ContinueOnDataError;
   pDevExt->EnableLogging       = gVendorConfig.EnableLogging;
   pDevExt->NoTimeoutOnCtlReq   = gVendorConfig.NoTimeoutOnCtlReq;
   pDevExt->MinInPktSize        = gVendorConfig.MinInPktSize;
   pDevExt->NumOfRetriesOnError = gVendorConfig.NumOfRetriesOnError;
   pDevExt->NumberOfL2Buffers   = gVendorConfig.NumberOfL2Buffers;
   pDevExt->DebugMask           = gVendorConfig.DebugMask;
   pDevExt->DebugLevel          = gVendorConfig.DebugLevel;
   pDevExt->MaxPipeXferSize     = gVendorConfig.MaxPipeXferSize;
#ifdef NDIS_WDM
   pDevExt->QosSetting          = QosSetting;
#endif // NDIS_WDM

   // allocate the read buffer here, before we decide to create the PTDO
   pDevExt->TlpFrameBuffer.Buffer = ExAllocatePool(NonPagedPool, QCUSB_RECEIVE_BUFFER_SIZE);
   if (pDevExt->TlpFrameBuffer.Buffer == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_FORCE,
         ("<%s> USBPNP_InitDevExt: NO MEM - A\n", myPortName)
      );
      ntStatus = STATUS_NO_MEMORY;
      goto USBPNP_InitDevExt_Return;
   }
   pDevExt->TlpFrameBuffer.PktLength   = 0;
   pDevExt->TlpFrameBuffer.BytesNeeded = 0;

   pucNewReadBuffer =  _ExAllocatePool
                       (
                          NonPagedPool,
                          2 + pDevExt->lReadBufferSize,
                          "pucNewReadBuffer"
                       );
   if (!pucNewReadBuffer)
   {
      pDevExt->lReadBufferSize /= 2;  // reduce the size and try again
      pucNewReadBuffer =  _ExAllocatePool
                          (
                             NonPagedPool,
                             2 + pDevExt->lReadBufferSize,
                             "pucNewReadBuffer"
                          );
      if (!pucNewReadBuffer)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_FORCE,
            ("<%s> USBPNP_InitDevExt: NO MEM - 0\n", myPortName)
         );
         ntStatus = STATUS_NO_MEMORY;
         goto USBPNP_InitDevExt_Return;
      }
   }

   // Initialize L2 Buffers
   // ntStatus = USBRD_InitializeL2Buffers(pDevExt);
   // if (!NT_SUCCESS(ntStatus))
   // {
   //   QCUSB_DbgPrint
   //   (
   //      QCUSB_DBG_MASK_CONTROL,
   //      QCUSB_DBG_LEVEL_FORCE,
   //      ("<%s> USBPNP_InitDevExt: L2 NO MEM - 7a\n", myPortName)
   //   );
   //   goto USBPNP_InitDevExt_Return;
   // }

   #ifdef QCUSB_MULTI_WRITES
   ntStatus = USBMWT_InitializeMultiWriteElements(pDevExt);
   if (!NT_SUCCESS(ntStatus))
   {
     QCUSB_DbgPrintG
     (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_FORCE,
        ("<%s> USBPNP: MWT NO MEM\n", myPortName)
     );
     goto USBPNP_InitDevExt_Return;
   }
   #endif // QCUSB_MULTI_WRITES

   pDevExt->bInService     = FALSE;  //open the gate
   pDevExt->bL1ReadActive  = FALSE;
   pDevExt->bWriteActive   = FALSE;

   pDevExt->bReadBufferReset = FALSE;

   // initialize our unicode strings for object deletion

   pDevExt->ucsDeviceMapEntry.Buffer = ucDeviceMapEntry.Buffer;
   pDevExt->ucsDeviceMapEntry.Length = ucDeviceMapEntry.Length;
   pDevExt->ucsDeviceMapEntry.MaximumLength =
      ucDeviceMapEntry.MaximumLength;
   ucDeviceMapEntry.Buffer = NULL; // we've handed off that buffer

   pDevExt->ucsPortName.Buffer = ucPortName.Buffer;
   pDevExt->ucsPortName.Length = ucPortName.Length;
   pDevExt->ucsPortName.MaximumLength = ucPortName.MaximumLength;
   ucPortName.Buffer = NULL;

   pDevExt->pucReadBufferStart = pucNewReadBuffer;
   pucNewReadBuffer            = NULL; // so we won't free it on exit from this sbr

   // setup flow limits
   pDevExt->lReadBufferLow   = pDevExt->lReadBufferSize / 10;       /* 10% */
   pDevExt->lReadBufferHigh  = (pDevExt->lReadBufferSize * 9) / 10; /* 90% */
   pDevExt->lReadBuffer80pct = (pDevExt->lReadBufferSize * 4) / 5;  /* 80% */
   pDevExt->lReadBuffer50pct = pDevExt->lReadBufferSize / 2;        /* 50% */
   pDevExt->lReadBuffer20pct = pDevExt->lReadBufferSize / 5;        /* 20% */

   pDevExt->pNotificationIrp   = NULL;

   // Init locks, events, and mutex
   _InitializeMutex(&pDevExt->muPnPMutex);
   KeInitializeSpinLock(&pDevExt->ControlSpinLock);
   KeInitializeSpinLock(&pDevExt->ReadSpinLock);
   KeInitializeSpinLock(&pDevExt->WriteSpinLock);
   KeInitializeSpinLock(&pDevExt->SingleIrpSpinLock);

   KeInitializeEvent
   (
      &pDevExt->DSPSyncEvent,
      SynchronizationEvent,
      TRUE
   );
   KeSetEvent(&pDevExt->DSPSyncEvent,IO_NO_INCREMENT,FALSE);
   KeInitializeEvent
   (
      &pDevExt->ForTimeoutEvent,
      SynchronizationEvent,
      TRUE
   );
   KeInitializeEvent
   (
      &pDevExt->IntThreadStartedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptPipeClosedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->ReadThreadStartedEvent,
      SynchronizationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->L2ReadThreadStartedEvent,
      SynchronizationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->WriteThreadStartedEvent,
      SynchronizationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->DspThreadStartedEvent,
      SynchronizationEvent,
      FALSE
   );

   // interrupt pipe thread waits on these
   pDevExt->pInterruptPipeEvents[INT_COMPLETION_EVENT_INDEX] =
      &pDevExt->eInterruptCompletion;
   KeInitializeEvent(&pDevExt->eInterruptCompletion,NotificationEvent,FALSE);
   pDevExt->pInterruptPipeEvents[CANCEL_EVENT_INDEX] =
      &pDevExt->CancelInterruptPipeEvent;
   KeInitializeEvent
   (
      &pDevExt->CancelInterruptPipeEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pInterruptPipeEvents[INT_STOP_SERVICE_EVENT] =
      &pDevExt->InterruptStopServiceEvent;
   KeInitializeEvent
   (
      &pDevExt->InterruptStopServiceEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptStopServiceRspEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pInterruptPipeEvents[INT_RESUME_SERVICE_EVENT] =
      &pDevExt->InterruptResumeServiceEvent;
   KeInitializeEvent
   (
      &pDevExt->InterruptResumeServiceEvent,
      NotificationEvent,
      FALSE
   );

   pDevExt->pInterruptPipeEvents[INT_EMPTY_RD_QUEUE_EVENT_INDEX] =
      &pDevExt->InterruptEmptyRdQueueEvent;
   pDevExt->pInterruptPipeEvents[INT_EMPTY_WT_QUEUE_EVENT_INDEX] =
      &pDevExt->InterruptEmptyWtQueueEvent;
   pDevExt->pInterruptPipeEvents[INT_EMPTY_CTL_QUEUE_EVENT_INDEX] =
      &pDevExt->InterruptEmptyCtlQueueEvent;
   pDevExt->pInterruptPipeEvents[INT_EMPTY_SGL_QUEUE_EVENT_INDEX] =
      &pDevExt->InterruptEmptySglQueueEvent;
   pDevExt->pInterruptPipeEvents[INT_REG_IDLE_NOTIF_EVENT_INDEX] =
      &pDevExt->InterruptRegIdleEvent;
   pDevExt->pInterruptPipeEvents[INT_IDLE_EVENT] = &pDevExt->InterruptIdleEvent;
   KeInitializeEvent
   (
      &pDevExt->InterruptIdleEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pInterruptPipeEvents[INT_IDLENESS_COMPLETED_EVENT] = &pDevExt->InterruptIdlenessCompletedEvent;
   KeInitializeEvent
   (
      &pDevExt->InterruptIdlenessCompletedEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pInterruptPipeEvents[INT_IDLE_CBK_COMPLETED_EVENT] = &pDevExt->IdleCbkCompleteEvent;
   KeInitializeEvent
   (
      &pDevExt->IdleCbkCompleteEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->IdleCallbackInProgress = 0;
   pDevExt->bIdleIrpCompleted      = FALSE;
   pDevExt->bLowPowerMode          = FALSE;

   KeInitializeEvent
   (
      &pDevExt->InterruptEmptyRdQueueEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptEmptyWtQueueEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptEmptyCtlQueueEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptEmptySglQueueEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptRegIdleEvent,
      NotificationEvent,
      FALSE
   );

   // write thread waits on these
   pDevExt->pWriteEvents[CANCEL_EVENT_INDEX] = &pDevExt->CancelWriteEvent;
   KeInitializeEvent(&pDevExt->CancelWriteEvent,NotificationEvent,FALSE);
   pDevExt->pWriteEvents[WRITE_COMPLETION_EVENT_INDEX] = &pDevExt->WriteCompletionEvent;
   KeInitializeEvent
   (
      &pDevExt->WriteCompletionEvent,
      NotificationEvent,
      FALSE
   );

   pDevExt->pWriteEvents[KICK_WRITE_EVENT_INDEX] = &pDevExt->KickWriteEvent;
   KeInitializeEvent(&pDevExt->KickWriteEvent, NotificationEvent, FALSE);

   pDevExt->pWriteEvents[WRITE_CANCEL_CURRENT_EVENT_INDEX] =
      &pDevExt->WriteCancelCurrentEvent;
   KeInitializeEvent(&pDevExt->WriteCancelCurrentEvent, NotificationEvent, FALSE);

   pDevExt->pWriteEvents[WRITE_PURGE_EVENT_INDEX] = &pDevExt->WritePurgeEvent;
   KeInitializeEvent(&pDevExt->WritePurgeEvent, NotificationEvent, FALSE);

   pDevExt->pWriteEvents[WRITE_STOP_EVENT_INDEX] = &pDevExt->WriteStopEvent;
   KeInitializeEvent(&pDevExt->WriteStopEvent, NotificationEvent, FALSE);

   pDevExt->pWriteEvents[WRITE_RESUME_EVENT_INDEX] = &pDevExt->WriteResumeEvent;
   KeInitializeEvent(&pDevExt->WriteResumeEvent, NotificationEvent, FALSE);

   pDevExt->pWriteEvents[WRITE_FLOW_ON_EVENT_INDEX] = &pDevExt->WriteFlowOnEvent;
   KeInitializeEvent(&pDevExt->WriteFlowOnEvent, NotificationEvent, FALSE);

   pDevExt->pWriteEvents[WRITE_FLOW_OFF_EVENT_INDEX] = &pDevExt->WriteFlowOffEvent;
   KeInitializeEvent(&pDevExt->WriteFlowOffEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->WriteFlowOffAckEvent, NotificationEvent, FALSE);

   // read thread waits on these
   pDevExt->pL2ReadEvents[CANCEL_EVENT_INDEX] = &pDevExt->CancelReadEvent;
   KeInitializeEvent(&pDevExt->CancelReadEvent,NotificationEvent,FALSE);

   pDevExt->pL1ReadEvents[L1_CANCEL_EVENT_INDEX] = &pDevExt->L1CancelReadEvent;
   KeInitializeEvent(&pDevExt->L1CancelReadEvent,NotificationEvent,FALSE);

   pDevExt->pL2ReadEvents[L2_READ_COMPLETION_EVENT_INDEX] = &pDevExt->L2ReadCompletionEvent;
   pDevExt->pL1ReadEvents[L1_READ_COMPLETION_EVENT_INDEX] = &pDevExt->L1ReadCompletionEvent;
   pDevExt->pL1ReadEvents[L1_READ_AVAILABLE_EVENT_INDEX] = &pDevExt->L1ReadAvailableEvent;

   KeInitializeEvent(&pDevExt->L2ReadCompletionEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L1ReadCompletionEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L1ReadAvailableEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L1ReadPurgeAckEvent, NotificationEvent, FALSE);

   pDevExt->pL2ReadEvents[L2_KICK_READ_EVENT_INDEX] = &pDevExt->L2KickReadEvent;
   KeInitializeEvent(&pDevExt->L2KickReadEvent, NotificationEvent, FALSE);

   pDevExt->pL2ReadEvents[L2_STOP_EVENT_INDEX] = &pDevExt->L2StopEvent;
   KeInitializeEvent(&pDevExt->L2StopEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L2StopAckEvent, NotificationEvent, FALSE);

   pDevExt->pL2ReadEvents[L2_RESUME_EVENT_INDEX] = &pDevExt->L2ResumeEvent;
   pDevExt->pL2ReadEvents[L2_USB_READ_EVENT_INDEX] = &pDevExt->L2USBReadCompEvent;
   
   KeInitializeEvent(&pDevExt->L2ResumeEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L2USBReadCompEvent, NotificationEvent, FALSE);
   
   KeInitializeEvent(&pDevExt->L2ResumeAckEvent, NotificationEvent, FALSE);

   pDevExt->pL1ReadEvents[L1_KICK_READ_EVENT_INDEX] = &pDevExt->L1KickReadEvent;
   KeInitializeEvent(&pDevExt->L1KickReadEvent, NotificationEvent, FALSE);

   pDevExt->pL1ReadEvents[L1_READ_PURGE_EVENT_INDEX] = &pDevExt->L1ReadPurgeEvent;
   KeInitializeEvent(&pDevExt->L1ReadPurgeEvent, NotificationEvent, FALSE);

   pDevExt->pL1ReadEvents[L1_STOP_EVENT_INDEX] = &pDevExt->L1StopEvent;
   KeInitializeEvent(&pDevExt->L1StopEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L1StopAckEvent, NotificationEvent, FALSE);

   pDevExt->pL1ReadEvents[L1_RESUME_EVENT_INDEX] = &pDevExt->L1ResumeEvent;
   KeInitializeEvent(&pDevExt->L1ResumeEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L1ResumeAckEvent, NotificationEvent, FALSE);

   KeInitializeEvent
   (
      &pDevExt->DispatchReadControlEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->L1ReadThreadClosedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->L2ReadThreadClosedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->ReadIrpPurgedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->WriteThreadClosedEvent,
      NotificationEvent,
      FALSE
   );

   pDevExt->pDispatchEvents[DSP_CANCEL_DISPATCH_EVENT_INDEX] = &pDevExt->DispatchCancelEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchCancelEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_PURGE_EVENT_INDEX] = &pDevExt->DispatchPurgeEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchPurgeEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_START_POLLING_EVENT_INDEX] = &pDevExt->DispatchStartPollingEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchStartPollingEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_STOP_POLLING_EVENT_INDEX] = &pDevExt->DispatchStopPollingEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchStopPollingEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_STANDBY_EVENT_INDEX] = &pDevExt->DispatchStandbyEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchStandbyEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_WAKEUP_EVENT_INDEX] = &pDevExt->DispatchWakeupEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchWakeupEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_WAKEUP_RESET_EVENT_INDEX] = &pDevExt->DispatchWakeupResetEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchWakeupResetEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->DispatchPurgeCompleteEvent,
      NotificationEvent,
      FALSE
   );

   pDevExt->pDispatchEvents[DSP_START_DISPATCH_EVENT_INDEX] = &pDevExt->DispatchStartEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchStartEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->DispatchThreadClosedEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_PRE_WAKEUP_EVENT_INDEX] = &pDevExt->DispatchPreWakeupEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchPreWakeupEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_XWDM_OPEN_EVENT_INDEX] = &pDevExt->DispatchXwdmOpenEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchXwdmOpenEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_XWDM_CLOSE_EVENT_INDEX] = &pDevExt->DispatchXwdmCloseEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchXwdmCloseEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_XWDM_QUERY_RM_EVENT_INDEX] = &pDevExt->DispatchXwdmQueryRmEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchXwdmQueryRmEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_XWDM_RM_CANCELLED_EVENT_INDEX] = &pDevExt->DispatchXwdmReopenEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchXwdmReopenEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_XWDM_QUERY_S_PWR_EVENT_INDEX] = &pDevExt->DispatchXwdmQuerySysPwrEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchXwdmQuerySysPwrEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->DispatchXwdmQuerySysPwrAckEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pDispatchEvents[DSP_XWDM_NOTIFY_EVENT_INDEX] = &pDevExt->DispatchXwdmNotifyEvent;
   KeInitializeEvent
   (
      &pDevExt->DispatchXwdmNotifyEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeSpinLock(&pDevExt->ExWdmSpinLock);
   pDevExt->ExWdmInService = FALSE;

   // Initialize device extension
   InitializeListHead(&pDevExt->RdCompletionQueue);
   InitializeListHead(&pDevExt->WtCompletionQueue);
   InitializeListHead(&pDevExt->CtlCompletionQueue);
   InitializeListHead(&pDevExt->SglCompletionQueue);
   InitializeListHead(&pDevExt->EncapsulatedDataQueue);
   InitializeListHead(&pDevExt->DispatchQueue);
   InitializeListHead(&pDevExt->ReadDataQueue);
   InitializeListHead(&pDevExt->WriteDataQueue);
   InitializeListHead(&pDevExt->MWTSentIrpQueue);
   InitializeListHead(&pDevExt->MWTSentIrpRecordPool);
   InitializeListHead(&pDevExt->IdleIrpCompletionStack);

   pDevExt->hInterruptThreadHandle = NULL;
   pDevExt->hL1ReadThreadHandle    = NULL;
   pDevExt->hL2ReadThreadHandle    = NULL;
   pDevExt->hWriteThreadHandle     = NULL;
   pDevExt->hTxLogFile             = NULL;
   pDevExt->hRxLogFile             = NULL;
   pDevExt->bL1InCancellation      = FALSE;
   pDevExt->bEnableResourceSharing = FALSE;
   pDevExt->pReadControlEvent  = &(pDevExt->DispatchReadControlEvent);
   pDevExt->pRspAvailableCount = &(pDevExt->lRspAvailableCount);

   KeClearEvent(&pDevExt->L1ReadThreadClosedEvent);
   KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
   KeClearEvent(&pDevExt->ReadIrpPurgedEvent);
   KeClearEvent(&pDevExt->L1ReadPurgeAckEvent);
   KeClearEvent(&pDevExt->WriteThreadClosedEvent);
   KeClearEvent(&pDevExt->InterruptPipeClosedEvent);
   KeClearEvent(&pDevExt->InterruptStopServiceEvent);
   KeClearEvent(&pDevExt->InterruptStopServiceRspEvent);
   KeClearEvent(&pDevExt->InterruptResumeServiceEvent);
   KeClearEvent(&pDevExt->CancelReadEvent);
   KeClearEvent(&pDevExt->L1CancelReadEvent);
   KeClearEvent(&pDevExt->CancelWriteEvent);
   // KeClearEvent(&pDevExt->CancelInterruptPipeEvent);

   initDevState(DEVICE_STATE_ZERO);
   pDevExt->bDeviceRemoved = FALSE;
   pDevExt->bDeviceSurpriseRemoved = FALSE;
   pDevExt->pRemoveLock = &pDevExt->RemoveLock;
   pDevExt->PowerState = PowerDeviceUnspecified;
   pDevExt->SetCommFeatureSupported = FALSE;
   IoInitializeRemoveLock(pDevExt->pRemoveLock, 0, 0, 0);

   //Initialize all thread synchronization
   pDevExt->DispatchThreadCancelStarted = 0;
   pDevExt->InterruptThreadCancelStarted = 0;
   pDevExt->ReadThreadCancelStarted = 0;
   pDevExt->ReadThreadInCreation = 0;
   pDevExt->WriteThreadCancelStarted = 0;
   pDevExt->WriteThreadInCreation = 0;
   

   #ifdef QCUSB_PWR
   KeInitializeEvent
   (
      &pDevExt->RegIdleAckEvent,
      NotificationEvent,
      FALSE
   );

   pDevExt->PMWmiRegistered   = FALSE;
   pDevExt->PowerSuspended    = FALSE;
   pDevExt->SelectiveSuspended = FALSE;
   pDevExt->PrepareToPowerDown= FALSE;
   pDevExt->IdleTimerLaunched = FALSE;
   pDevExt->IoBusyMask        = 0;
   pDevExt->SelectiveSuspendIdleTime = 10;  // chage it to 5 by default
   pDevExt->SelectiveSuspendInMili = FALSE;   
   pDevExt->InServiceSelectiveSuspension = FALSE;  // disable for 5G data for now -- TODO 
   pDevExt->bRemoteWakeupEnabled = FALSE;  // hardcode for now
   pDevExt->bDeviceSelfPowered = FALSE;
   pDevExt->PowerManagementEnabled = TRUE;
   pDevExt->WaitWakeEnabled = FALSE;  // hardcode for now
   QCPWR_GetWdmVersion(pDevExt);
   KeInitializeTimer(&pDevExt->IdleTimer);
   KeInitializeDpc(&pDevExt->IdleDpc, QCPWR_IdleDpcRoutine, pDevExt);
   pDevExt->SystemPower = PowerSystemWorking;
   pDevExt->DevicePower = initialPwrState.DeviceState = PowerDeviceD0;
   // PoSetPowerState(fdo, DevicePowerState, initialPwrState);
   USBPNP_GetDeviceCapabilities(pDevExt, bPowerManagement); // store info in portExt
   pDevExt->bPowerManagement = bPowerManagement;
   InitializeListHead(&pDevExt->OriginatedPwrReqQueue);
   InitializeListHead(&pDevExt->OriginatedPwrReqPool);
   for (i = 0; i < SELF_ORIGINATED_PWR_REQ_MAX; i++)
   {
      InsertTailList
      (
         &pDevExt->OriginatedPwrReqPool,
         &(pDevExt->SelfPwrReq[i].List)
      );
   }

   #endif 

USBPNP_InitDevExt_Return:
   if (pucNewReadBuffer)
   {
      ExFreePool(pucNewReadBuffer);
      pucNewReadBuffer = NULL;
   }

   _freeString(ucDeviceMapEntry);
   _freeString(ucPortName);
   _freeString(ucDeviceNumber);
   _freeString(ucDeviceNameBase);
   _freeString(ucDeviceName);

   if (NT_SUCCESS(ntStatus))
   {
      // Get the SS value here
      {
         ANSI_STRING ansiStr;
         UNICODE_STRING unicodeStr;
         RtlInitAnsiString( &ansiStr, gServiceName);
         RtlAnsiStringToUnicodeString(&unicodeStr, &ansiStr, TRUE);
         selectiveSuspendIdleTime = 0;
         ntStatus = USBUTL_GetServiceRegValue( unicodeStr.Buffer, VEN_DRV_SS_IDLE_T, &selectiveSuspendIdleTime);
         RtlFreeUnicodeString( &unicodeStr );
      }
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> USBUTL_GetServiceRegValue: returned 0x%x Value 0x%x\n", myPortName, ntStatus, selectiveSuspendIdleTime)
      );
      
      selectiveSuspendInMili = selectiveSuspendIdleTime & 0x40000000;
      
      selectiveSuspendIdleTime &= 0x00FFFFFF;
      
      if (selectiveSuspendInMili == 0)
      {
          pDevExt->SelectiveSuspendInMili = FALSE;
          if (selectiveSuspendIdleTime < QCUSB_SS_IDLE_MIN)
          {
             selectiveSuspendIdleTime = QCUSB_SS_IDLE_MIN;
          }
          else if (selectiveSuspendIdleTime > QCUSB_SS_IDLE_MAX)
          {
             selectiveSuspendIdleTime = QCUSB_SS_IDLE_MAX;
          }
      }
      else
      {
          pDevExt->SelectiveSuspendInMili = TRUE;
          if (selectiveSuspendIdleTime < QCUSB_SS_MILI_IDLE_MIN)
          {
             selectiveSuspendIdleTime = QCUSB_SS_MILI_IDLE_MIN;
          }
          else if (selectiveSuspendIdleTime > QCUSB_SS_MILI_IDLE_MAX)
          {
             selectiveSuspendIdleTime = QCUSB_SS_MILI_IDLE_MAX;
          }
          
      }
      pDevExt->SelectiveSuspendIdleTime = selectiveSuspendIdleTime;

      if (!NT_SUCCESS(ntStatus))
      {
         ntStatus = STATUS_SUCCESS;
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> USBPNP_InitDevExt: exit 0x%x\n", myPortName, ntStatus)
   );
   return ntStatus;
}  // USBPNP_InitDevExt

BOOLEAN USBPNP_ValidateConfigDescriptor
(
   PDEVICE_EXTENSION pDevExt,
   PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc
)
{
   if ((ConfigDesc->bLength ==0) ||
       (ConfigDesc->bLength > 9) ||
       (ConfigDesc->wTotalLength == 0) ||
       (ConfigDesc->wTotalLength < 9))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _ValidateConfigDescriptor: bad length: %uB, %uB\n",
           pDevExt->PortName, ConfigDesc->bLength, ConfigDesc->wTotalLength
         )
      );
      return FALSE;
   }

   if (ConfigDesc->bNumInterfaces >= MAX_INTERFACE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _ValidateConfigDescriptor: bad bNumInterfaces 0x%x\n",
           pDevExt->PortName, ConfigDesc->bNumInterfaces)
      );
      return FALSE;
   }

   if (ConfigDesc->bDescriptorType != 0x02)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _ValidateConfigDescriptor: bad bDescriptorType 0x%x\n",
           pDevExt->PortName, ConfigDesc->bDescriptorType)
      );
      return FALSE;
   }

   return TRUE;

}  // USBPNP_ValidateConfigDescriptor

BOOLEAN USBPNP_ValidateDeviceDescriptor
(
   PDEVICE_EXTENSION      pDevExt,
   PUSB_DEVICE_DESCRIPTOR DevDesc
)
{
   if (DevDesc->bLength == 0)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _ValidateDeviceDescriptor: 0 bLength\n", pDevExt->PortName)
      );
      return FALSE;
   }

   if (DevDesc->bDescriptorType != 0x01)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _ValidateDeviceDescriptor: bad bDescriptorType 0x%x\n",
           pDevExt->PortName, DevDesc->bDescriptorType)
      );
      return FALSE;
   }

   if ((pDevExt->HighSpeedUsbOk & QC_SSUSB_VERSION_OK) == 0)  // if not SS USB
   {
      if ((DevDesc->bMaxPacketSize0 != 0x08) &&
          (DevDesc->bMaxPacketSize0 != 0x10) &&
          (DevDesc->bMaxPacketSize0 != 0x20) &&
          (DevDesc->bMaxPacketSize0 != 0x40))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _ValidateDeviceDescriptor: bad bMaxPacketSize0 0x%x\n",
              pDevExt->PortName, DevDesc->bMaxPacketSize0)
         );
         return FALSE;
      }
   }
   else
   {
      if (DevDesc->bMaxPacketSize0 != 0x09)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _ValidateDeviceDescriptor: bad SS bMaxPacketSize0 0x%x\n",
              pDevExt->PortName, DevDesc->bMaxPacketSize0)
         );
         return FALSE;
      }
   }

   return TRUE;

}  // USBPNP_ValidateDeviceDescriptor

