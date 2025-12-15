/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B P N P . H

GENERAL DESCRIPTION
  This file definitions of PNP functions.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBPNP_H
#define USBPNP_H

#include "USBMAIN.h"

#define VEN_DEV_TIME        L"QCDeviceStamp"
#define VEN_DEV_SERNUM      L"QCDeviceSerialNumber"
#define VEN_DEV_MSM_SERNUM  L"QCDeviceMsmSerialNumber"
#define VEN_DEV_PROTOC      L"QCDeviceProtocol"
#define VEN_DEV_SSR         L"QCDeviceSSR"
#define VEN_DEV_CID         L"QCDeviceCID"
#define VEN_DEV_PARENT      L"QCDeviceParent"

NTSTATUS USBPNP_AddDevice
(
   IN PDRIVER_OBJECT pDriverObject,
   IN PDEVICE_OBJECT pdo
);
NTSTATUS USBPNP_GetDeviceCapabilities
(
    PDEVICE_EXTENSION pDevExt,
    BOOLEAN bPowerManagement
);

NTSTATUS QCPNP_GetParentDeviceName(PDEVICE_EXTENSION pDevExt);

NTSTATUS USBPNP_StartDevice
(
    PDEVICE_OBJECT DeviceObject
);

NTSTATUS USBPNP_ConfigureUsbDevice
(
    PDEVICE_OBJECT DeviceObject,
    UCHAR DescriptorIndex
);

NTSTATUS USBPNP_SelectInterfaces
(
    PDEVICE_OBJECT DeviceObject
);

NTSTATUS USBPNP_StopDevice
(
    PDEVICE_OBJECT DeviceObject
);

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
);

BOOLEAN USBPNP_ValidateConfigDescriptor
(
   PDEVICE_EXTENSION pDevExt,
   PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc
);

BOOLEAN USBPNP_ValidateDeviceDescriptor
(
   PDEVICE_EXTENSION      pDevExt,
   PUSB_DEVICE_DESCRIPTOR DevDesc
);

NTSTATUS QCPNP_GetDeviceCID
(
    PDEVICE_OBJECT DeviceObject,
    PCHAR  ProductString,
    ULONG  ProductStrLen
);

NTSTATUS QCPNP_GetStringDescriptor
(
   PDEVICE_OBJECT DeviceObject,
   UCHAR          Index,
   USHORT         LanguageId,
   BOOLEAN        MatchPrefix
);

#endif // USBPNP_H
