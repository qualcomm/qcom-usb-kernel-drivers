/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B I F . H

GENERAL DESCRIPTION
    This file provides external interfaces for USB driver initialization.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBIF_H
#define USBIF_H

#include <ndis.h>

#define XFER_10K 10*1024
#define XFER_20K 20*1024
#define XFER_30K 30*1024

#pragma pack(push, 4)

typedef struct _QC_XFER_STATISTICS
{
   ULONG RxPktsLessThan10k;
   ULONG RxPkts10kTo20k;
   ULONG RxPkts20kTo30k;
   ULONG RxPktsMoreThan30k;
   ULONG TxPktsLessThan10k;
   ULONG TxPkts10kTo20k;
   ULONG TxPkts20kTo30k;
   ULONG TxPktsMoreThan30k;
} QC_XFER_STATISTICS, *PQC_XFER_STATISTICS;

#pragma pack(pop)

typedef struct _QCUSB_ENTRY_POINTS
{
    PDRIVER_DISPATCH MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];
    PDRIVER_UNLOAD   DriverUnload;
} QCUSB_ENTRY_POINTS, *PQCUSB_ENTRY_POINTS;

PDEVICE_OBJECT USBIF_InitializeUSB
(
   IN PVOID          MiniportContext,
   IN PDRIVER_OBJECT DriverObject,
   IN PDEVICE_OBJECT Pdo,
   IN PDEVICE_OBJECT Fdo,
   IN PDEVICE_OBJECT NextDO,
   IN NDIS_HANDLE    WrapperConfigurationContext,
   PCHAR             PortName
);

VOID USBIF_ShutdownUSB(PDEVICE_OBJECT DeviceObject);

NTSTATUS QCIoCallDriver
(
   IN PDEVICE_OBJECT DeviceObject,
   IN OUT PIRP       Irp
);

NTSTATUS QCDirectIoCallDriver
(
   IN PDEVICE_OBJECT DeviceObject,
   IN OUT PIRP       Irp
);

PDEVICE_OBJECT USBIF_CreateUSBComponent
(
   IN PVOID          MiniportContext,
   IN NDIS_HANDLE    WrapperConfigurationContext,
   IN PDEVICE_OBJECT Pdo,
   IN PDEVICE_OBJECT Fdo,
   IN PDEVICE_OBJECT NextDO,
   IN LONG           QosSetting
);

NTSTATUS USBIF_Open(PDEVICE_OBJECT DeviceObject);
NTSTATUS USBIF_Close(PDEVICE_OBJECT DeviceObject);

NTSTATUS USBIF_CancelWriteThread(PDEVICE_OBJECT pDO);

NTSTATUS USBIF_NdisVendorRegistryProcess
(
   IN PVOID       MiniportContext,
   IN NDIS_HANDLE WrapperConfigurationContext
);
NTSTATUS USBIF_NdisPostVendorRegistryProcess
(
   IN PVOID          MiniportContext,
   IN NDIS_HANDLE    WrapperConfigurationContext,
   IN PDEVICE_OBJECT DeviceObject
);

VOID USBIF_SetDebugMask(PDEVICE_OBJECT DO, char *buffer);

NTSTATUS USBIF_SetPowerState
(
   PDEVICE_OBJECT DO,
   NDIS_DEVICE_POWER_STATE PowerState
);

UCHAR USBIF_GetDataInterfaceNumber(PDEVICE_OBJECT DO);

SYSTEM_POWER_STATE USBIF_GetSystemPowerState(PDEVICE_OBJECT DO);

VOID USBIF_PollDevice(PDEVICE_OBJECT DO, BOOLEAN Active);

VOID USBIF_PowerDownDevice
(
   PVOID              DeviceExtension,
   DEVICE_POWER_STATE DevicePower
);

VOID USBIF_SyncPowerDownDevice
(
   PVOID              DeviceExtension,
   DEVICE_POWER_STATE DevicePower
);

/******************** Dispatch Filter *********************/
VOID USBIF_SetupDispatchFilter(PVOID DriverObject);

NTSTATUS USBIF_DispatchFilter
(
   IN PDEVICE_OBJECT DO,
   IN PIRP Irp
);

VOID USBIF_AddFdoMap
(
   PDEVICE_OBJECT NdisFdo,
   PVOID          MiniportContext
);

VOID USBIF_CleanupFdoMap(PDEVICE_OBJECT NdisFdo);

PVOID USBIF_FindNdisContext(PDEVICE_OBJECT NdisFdo);

BOOLEAN USBIF_IsPowerIrpOriginatedFromUSB
(
   PDEVICE_OBJECT UsbFDO,
   PIRP           Irp
);

BOOLEAN USBIF_IsUsbBroken(PDEVICE_OBJECT UsbFDO);

#ifdef QC_IP_MODE
VOID USBIF_SetDataMode(PDEVICE_OBJECT UsbFDO);
#endif // QC_IP_MODE

VOID USBIF_SetAggregationMode(PDEVICE_OBJECT UsbFDO);

VOID USBIF_NotifyLinkDown(PDEVICE_OBJECT UsbFDO);

unsigned short CheckSum(unsigned short *buffer, int count);

#ifdef QCUSB_MUX_PROTOCOL

VOID USBIF_SetDLCI(PDEVICE_OBJECT UsbFDO);

VOID USBIF_EncodeMuxFrame
(
   PDEVICE_OBJECT UsbFDO,
   PCHAR          Payload,
   ULONG          PayloadLength,
   PUCHAR         MuxFrameBuffer,
   PULONG         MuxFrameLength,
   BOOLEAN        IsDataFrame
);

PCHAR USBIF_DecodeMuxFrame
(
   PDEVICE_OBJECT UsbFDO,
   PUCHAR         MuxFrameBuffer,
   ULONG          MuxFrameLength,
   PULONG         PayloadLength,
   BOOLEAN        IsDataFrame
);

#endif // QCUSB_MUX_PROTOCOL

#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)

#define QMAP_CONTROL_MESSAGE_LEN 255

typedef struct _QCQMAPCONTROLQUEUE
{
   LIST_ENTRY QMAPListEntry;
   CHAR Buffer[QMAP_CONTROL_MESSAGE_LEN];
}QCQMAPCONTROLQUEUE, *PQCQMAPCONTROLQUEUE;


BOOLEAN USBIF_QMAPSendFlowControlCommand(PDEVICE_OBJECT UsbFDO, ULONG TransactionId, UCHAR QMAPCommand, PVOID pInputFlowControl);

VOID USBIF_QMAPAddtoControlQueue(PDEVICE_OBJECT UsbFDO, PQCQMAPCONTROLQUEUE pQMAPControl);

VOID USBIF_QMAPProcessControlQueue(PDEVICE_OBJECT UsbFDO);

VOID USBIF_QMAPProcessControlMessage(PVOID pDevExtVoid, PUCHAR BufferPtr, ULONG Size);

#endif

BOOLEAN USBIF_InjectPacket(PDEVICE_OBJECT UsbFDO, PUCHAR pPacket, ULONG Size);

VOID USBIF_SetDataMTU(PDEVICE_OBJECT UsbFDO, LONG MtuValue);

PIRP GetAdapterReadDataItemIrp(PVOID pDevExt1, UCHAR MuxId, PVOID *pReturnDevExt, BOOLEAN *ReadQueueEmpty);

BOOLEAN IsEmptyAllReadQueue(PVOID pDevExt1);

NTSTATUS USBIF_UpdateSSR(PDEVICE_OBJECT UsbFDO, ULONG State);

#endif // USBIF_H
