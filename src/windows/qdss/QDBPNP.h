/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B P N P . H

GENERAL DESCRIPTION
    Function declarations and registry value name constants for the
    QDSS PnP and power management module. Covers device-add, hardware
    prepare, USB configuration, symbolic link creation, selective
    suspend, and registry related functions.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef QDBPNP_H
#define QDBPNP_H

#define DEVICE_LINK_NAME_PATH L"\\??\\"
#define VEN_DEV_TIME          L"QCDeviceStamp"
#define VEN_DEV_SERNUM        L"QCDeviceSerialNumber"
#define VEN_DEV_MSM_SERNUM    L"QCDeviceMsmSerialNumber"
#define VEN_DEV_PROTOC        L"QCDeviceProtocol"
#define VEN_DEV_CID           L"QCDeviceCID"
#define VEN_DEV_PARENT        L"QCDeviceParent"
#define VEN_DBG_MASK          L"QCDriverDebugMask"

EVT_WDF_DRIVER_DEVICE_ADD QDBPNP_EvtDriverDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP QDBPNP_EvtDriverCleanupCallback;
EVT_WDF_OBJECT_CONTEXT_CLEANUP QDBPNP_EvtDeviceCleanupCallback;

EVT_WDF_DEVICE_PREPARE_HARDWARE QDBPNP_EvtDevicePrepareHW;

EVT_WDF_DEVICE_D0_ENTRY QDBPNP_EvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT  QDBPNP_EvtDeviceD0Exit;

NTSTATUS QDBPNP_EnumerateDevice(IN WDFDEVICE Device);

NTSTATUS QDBPNP_UsbConfigureDevice(IN WDFDEVICE Device);

NTSTATUS QDBPNP_SelectInterfaces(WDFDEVICE Device);

NTSTATUS QDBPNP_CreateSymbolicName(WDFDEVICE Device);

NTSTATUS QDBPNP_EnableSelectiveSuspend(WDFDEVICE Device);

NTSTATUS QDBPNP_GetParentDeviceName(PDEVICE_CONTEXT pDevContext);

VOID QDBPNP_SetFunctionProtocol(IN WDFDEVICE Device, UCHAR ProtocolCode);

NTSTATUS QDBPNP_ReadDebugMask(WDFDEVICE QCDevice);

NTSTATUS QDBPNP_GetProductDescriptorString
(
    _In_      WDFUSBDEVICE    UsbDevice,
    _In_      UCHAR           StringIndex,
    _Out_     WDFMEMORY      *pStringMemory,
    _Out_opt_ PUNICODE_STRING pString
);

NTSTATUS QDBPNP_GetDeviceIdString
(
    _In_  PCUNICODE_STRING productDescription,
    _In_  PCUNICODE_STRING keyword,
    _Out_ PUNICODE_STRING  value
);

#endif // QDBPNP_H
