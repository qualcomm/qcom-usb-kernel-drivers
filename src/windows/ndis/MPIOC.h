/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                            M P I O C. H

GENERAL DESCRIPTION
    This module contains forward references to the IOCTL module.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef MPIOC_H
#define MPIOC_H

#include "MPMain.h"
#include "USBIOC.h"

extern LIST_ENTRY MP_DeviceList;
extern PNDIS_SPIN_LOCK MP_CtlLock;

#define MP_NUM_DEV 5
#define MP_REG_DEV_MAX_RETRIES 1000
#define MP_DEV_TYPE_CONTROL 0
#define MP_DEV_TYPE_CLIENT  1
#define MP_DEV_TYPE_INTERNAL_QOS 2
#define MP_DEV_TYPE_EXTERNAL_CTL 3
#define MP_DEV_TYPE_WDS_IPV6     4
#define MP_DEV_TYPE_MTU_SVC      5

#define QCDEV_NOTIFY_TYPE_GENERAL  0
#define QCDEV_NOTIFY_TYPE_MTU_IPV4 1
#define QCDEV_NOTIFY_TYPE_MTU_IPV6 2

// Define stop state in MP
#define MP_STATE_STOP(IocDev) (IocDev->MPStopped == TRUE)

#define MPIOC_REMOVAL_NOTIFICATION 0x00000001L

#define IOCDEV_WRITE_COMPLETION_EVENT_INDEX       0
#define IOCDEV_WRITE_CANCEL_CURRENT_EVENT_INDEX   1
#define IOCDEV_EMPTY_COMPLETION_QUEUE_EVENT_INDEX 2
#define IOCDEV_KICK_WRITE_EVENT_INDEX             3
#define IOCDEV_CANCEL_EVENT_INDEX                 4
#define IOCDEV_WRITE_PURGE_EVENT_INDEX            5
#define IOCDEV_WRITE_STOP_EVENT_INDEX             6
#define IOCDEV_WRITE_EVENT_COUNT                  7

#define IOCDEV_CLIENT_FILE_NAME_LEN 256

typedef struct _FILTER_DEVICE_INFO
{
    PMP_ADAPTER pAdapter;
    PUNICODE_STRING pDeviceName;
    PUNICODE_STRING pDeviceLinkName;
    PDRIVER_DISPATCH *MajorFunctions;
    PDEVICE_OBJECT pControlDeviceObject;
    PDEVICE_OBJECT pFilterDeviceObject;
    PUNICODE_STRING pFileName;
    ULONG UniqueIdentifier;
}FILTER_DEVICE_INFO, *PFILTER_DEVICE_INFO;


typedef struct _MPIOC_DEV_INFO
{
    BOOLEAN        Acquired;
    UCHAR          IdleTime;
    LONG           DeviceOpenCount;
    LIST_ENTRY     List;
    UCHAR          Type;
    UCHAR          ClientId;
    UCHAR          ClientIdV6;
    UCHAR          QMIType;
    PVOID          FsContext;
    LONG           QMUXTransactionId;
    KEVENT         ClientIdReceivedEvent;
    KEVENT         ClientIdReleasedEvent;
    KEVENT         DataFormatSetEvent;
    KEVENT         CtlSyncEvent;
    PMP_ADAPTER    Adapter;
    BOOLEAN        MPStopped;
    NDIS_HANDLE    NdisDeviceHandle;
    PDEVICE_OBJECT ControlDeviceObject;
    FILTER_DEVICE_INFO FilterDeviceInfo;
    UNICODE_STRING SymbolicName;
    UNICODE_STRING DeviceName;
    CHAR           ClientFileName[IOCDEV_CLIENT_FILE_NAME_LEN];
    LIST_ENTRY     ReadIrpQueue;
    LIST_ENTRY     WriteIrpQueue;
    LIST_ENTRY     ReadDataQueue;
    LIST_ENTRY     IrpCompletionQueue;
    NDIS_SPIN_LOCK IoLock;
    PIRP           GetServiceFileIrp;

    // for write thread
    PKTHREAD       pWriteThread;
    HANDLE         hWriteThreadHandle;
    BOOLEAN        bWtThreadInCreation;
    KEVENT         WriteThreadStartedEvent;
    BOOLEAN        bWriteActive;
    BOOLEAN        bWriteStopped;
    PIRP           pWriteCurrent;
    PKEVENT        pWriteEvents[IOCDEV_WRITE_EVENT_COUNT];
    KEVENT         WriteCompletionEvent;
    KEVENT         EmptyCompletionQueueEvent;
    KEVENT         WriteCancelCurrentEvent;
    KEVENT         KickWriteEvent;
    KEVENT         CancelWriteEvent;
    KEVENT         WritePurgeEvent;
    KEVENT         WriteStopEvent;
    KEVENT         WtWorkerThreadExitEvent;
    KEVENT         ClientClosedEvent;
    USHORT         NumOfRetriesOnError;
    LONG           WriteThreadInCancellation;
    INT            iErrorCount;

    // for read thread (internal client)
    KEVENT         ReadDataAvailableEvent;

    // custom PnP support
    PIRP           NotificationIrp;  // IOCTL_QCDEV_WAIT_NOTIFY
    PIRP           IPV4MtuNotificationIrp;  // IOCTL_QCDEV_IPV4_MTU_NOTIFY
    PIRP           IPV6MtuNotificationIrp;  // IOCTL_QCDEV_IPV6_MTU_NOTIFY

    // Security State
    UCHAR          SecurityReset;

    // IRP count
    ULONG          IrpCount;

    // WDS IP Client
    KEVENT         QMIWDSIPAddrReceivedEvent;
    BOOLEAN        IpFamilySet;

} MPIOC_DEV_INFO, *PMPIOC_DEV_INFO, **PPMPIOC_DEV_INFO;

typedef struct _MPIOC_READ_ITEM
{
    LIST_ENTRY List;
    PVOID      Buffer;
    ULONG      Length;
} MPIOC_READ_ITEM, *PMPIOC_READ_ITEM;

// ==================== Function Prototypes =======================

#if defined(IOCTL_INTERFACE)

NDIS_STATUS MPIOC_RegisterDevice
(
    PMP_ADAPTER Adapter,
    NDIS_HANDLE WrapperConfigurationContext
);

NDIS_STATUS MPIOC_DeregisterDevice(PMP_ADAPTER Adapter);

NTSTATUS MPIOC_IRPDispatch
(
    PDEVICE_OBJECT DeviceObject,
    PIRP           Irp
);

#endif  // IOCTL_INTERFACE

NDIS_STATUS MPIOC_RegisterAllDevices
(
    NDIS_HANDLE WrapperConfigurationContext,
    PMP_ADAPTER Adapter,
    USHORT      NumDev
);

NDIS_STATUS MPIOC_RegisterFilterDevice
(
    NDIS_HANDLE WrapperConfigurationContext,
    PMP_ADAPTER pAdapter
);

NDIS_STATUS MPIOC_AddDevice
(
    OUT PPMPIOC_DEV_INFO IocDevice,
    UCHAR           Type,
    PMP_ADAPTER     Adapter,
    NDIS_HANDLE     NdisDeviceHandle,
    PDEVICE_OBJECT  ControlDeviceObject,
    PUNICODE_STRING SymbolicName,
    PUNICODE_STRING DeviceName,
    PCHAR           ClientFileName
);

PMPIOC_DEV_INFO MPIOC_FindFreeClientObject(PDEVICE_OBJECT DeviceObject);

VOID MPIOC_CheckForAquiredDeviceIdleTime
(
    PMP_ADAPTER pAdapter
);

NDIS_STATUS MPIOC_DeleteDevice
(
    PMP_ADAPTER     pAdapter,
    PMPIOC_DEV_INFO IocDevice,
    PDEVICE_OBJECT  ControlDeviceObject
);

NDIS_STATUS MPIOC_DeleteFilterDevice
(
    PMP_ADAPTER     pAdapter,
    PMPIOC_DEV_INFO IocDevice,
    PDEVICE_OBJECT  ControlDeviceObject
);

PMPIOC_DEV_INFO MPIOC_FindIoDevice
(
    PMP_ADAPTER     pAdapter,
    PDEVICE_OBJECT  DeviceObject,
    PQCQMI          pQMI,
    PMPIOC_DEV_INFO IocDevice,
    PIRP            Irp,
    ULONG           QMIType
);

VOID MPIOC_CancelGetServiceFileIrpRoutine
(
    PDEVICE_OBJECT DeviceObject,
    PIRP           pIrp
);

NTSTATUS MPIOC_Read
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           Irp
);

VOID MPIOC_CancelReadRoutine
(
    PDEVICE_OBJECT DeviceObject,
    PIRP           pIrp
);

NTSTATUS MPIOC_Write
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           Irp
);

NTSTATUS MPIOC_WtIrpCompletion
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
);

void MPIOC_WriteThread
(
    PVOID           Context    // PMPIOC_DEV_INFO
);

VOID MPIOC_EmptyIrpCompletionQueue(PMPIOC_DEV_INFO pIocDev);

VOID MPIOC_PurgeQueue
(
    PMPIOC_DEV_INFO pIocDev,
    PLIST_ENTRY     queue,
    PNDIS_SPIN_LOCK pSpinLock,
    UCHAR           cookie
);

ULONG MPIOC_GetMdlTotalCount(PMDL Mdl);

NTSTATUS MPIOC_WriteIrpCompletion
(
    PMPIOC_DEV_INFO   pIocDev,
    ULONG             ulRequestedBytes,
    NTSTATUS          ntStatus,
    UCHAR             cookie
);

VOID MPIOC_CancelWriteRoutine
(
    PDEVICE_OBJECT DeviceObject,
    PIRP           pIrp
);

NTSTATUS MPIOC_StartWriteThread(PMPIOC_DEV_INFO pIocDev);

VOID MPIOC_CancelWriteThread
(
    PMPIOC_DEV_INFO pIocDev
);

VOID MPIOC_CleanupQueues(PMPIOC_DEV_INFO pIocDev, UCHAR cookie);

VOID MPIOC_SetStopState
(
    PMP_ADAPTER pAdapter,
    BOOLEAN     TerminateAll
);

VOID MPIOC_InvalidateClients(PMP_ADAPTER pAdapter);

// Event Notification Support
NTSTATUS MPIOC_CacheNotificationIrp(PMPIOC_DEV_INFO pIocDev, PIRP pIrp, ULONG Type);

VOID MPIOC_CancelNotificationRoutine(PDEVICE_OBJECT DeviceObject, PIRP pIrp);

NTSTATUS MPIOC_NotifyClient(PIRP pIrp, ULONG info);

VOID MPIOC_PostRemovalNotification(PMPIOC_DEV_INFO pIocDev);

VOID MPIOC_PostMtuNotification(PMPIOC_DEV_INFO pIocDev, UCHAR IpVer, ULONG Mtu, BOOLEAN UseSpinlock);

VOID MPIOC_AdvertiseMTU(PMP_ADAPTER pAdapter, UCHAR IpVersion, ULONG Mtu);

NTSTATUS MPIOC_CancelNotificationIrp(PMPIOC_DEV_INFO pIocDev, ULONG Type);

ULONG MPIOC_GetClientOpenCount(VOID);

BOOLEAN MPIOC_LocalProcessing(PIRP Irp);

#ifndef SECURITY_DESCRIPTOR_CONTROL
typedef USHORT SECURITY_DESCRIPTOR_CONTROL, *PSECURITY_DESCRIPTOR_CONTROL;
#endif 

#ifndef SECURITY_DESCRIPTOR
typedef struct _SECURITY_DESCRIPTOR
{
    UCHAR  Revision;
    UCHAR  Sbz1;
    SECURITY_DESCRIPTOR_CONTROL Control;
    PSID   Owner;
    PSID   Group;
    PACL   Sacl;
    PACL   Dacl;
} SECURITY_DESCRIPTOR, *PISECURITY_DESCRIPTOR;
#endif  // SECURITY_DESCRIPTOR

NDIS_STATUS MPIOC_ResetDeviceSecurity
(
    PMP_ADAPTER     pAdapter,
    PUNICODE_STRING DeviceLinkName
);

NTSTATUS MPIOC_GetPeerDeviceNameCompletion
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
);

NTSTATUS MPIOC_GetPeerDeviceName(PMP_ADAPTER pAdapter, PIRP Irp, ULONG BufLen);

PMP_ADAPTER GetPrimaryAdapter(PMP_ADAPTER pAdapter);

NTSTATUS MPIOC_GetPrimaryAdapterName(PMP_ADAPTER pAdapter, PIRP Irp, ULONG BufLen);

BOOLEAN MPIOC_IsIrpInQueue(PMPIOC_DEV_INFO pIocDev, PIRP WriteIrp, INT QueueFlag);

#endif // MPIOC_H
