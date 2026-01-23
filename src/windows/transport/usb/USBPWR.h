/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q C P W R . H

GENERAL DESCRIPTION
    This file implementations of power management functions.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef QCPWR_H
#define QCPWR_H

#include "USBMAIN.h"

#define QCUSB_SS_IDLE_MIN         3  // seconds
#define QCUSB_SS_IDLE_MAX       120  // seconds
#define QCUSB_SS_IDLE_DEFAULT     5  // seconds

#define QCUSB_SS_MILI_IDLE_MIN       200     // mili seconds
#define QCUSB_SS_MILI_IDLE_MAX       120000  // mili seconds
#define QCUSB_SS_MILI_IDLE_DEFAULT   5000  // mili seconds

#define QCUSB_BUSY_WT   0x01
#define QCUSB_BUSY_CTRL 0x02

#define WDM_MJ_VERSION    0x01
#define WDM_MN_VERSION_2K 0x10
#define WDM_MN_VERSION_XP 0x20
#define WDM_MJ_VERSION_VISTA 0x06
#define WDM_MN_VERSION_VISTA 0x00

typedef enum _WDM_VERSION
{
    WinMeOrOlder = 0,
    Win2kOrHigher,
    WinXpOrHigher,
    WinVistaOrHigher
}WDM_VERSION;

typedef enum _QCUSB_POWER_REQ_TYPE
{
    QCUSB_SYS_POWER_CURRENT = 0,
    QCUSB_PREPARE_TO_STANDBY = 1
} QCUSB_POWER_REQ_TYPE;

#pragma pack(push, 1)
typedef struct _QCUSB_POWER_REQ
{
    UCHAR Type;
    UCHAR State;
} QCUSB_POWER_REQ, *PQCUSB_POWER_REQ;
#pragma pack(pop)

typedef struct _WORKER_ITEM_CONTEXT
{
    PDEVICE_EXTENSION DeviceExtension;
    PIRP              Irp;
    PIO_WORKITEM      WorkItem;
} WORKER_ITEM_CONTEXT, *PWORKER_ITEM_CONTEXT;

typedef struct _IRP_COMPLETION_CONTEXT
{
    PDEVICE_EXTENSION DeviceExtension;
    PKEVENT           Event;
} IRP_COMPLETION_CONTEXT, *PIRP_COMPLETION_CONTEXT;

#ifndef QCSER_SELECTIVE_SUSPEND

typedef VOID(*USB_IDLE_CALLBACK)(PVOID Context);

typedef struct _USB_IDLE_CALLBACK_INFO
{
    USB_IDLE_CALLBACK IdleCallback;
    PVOID IdleContext;
} USB_IDLE_CALLBACK_INFO, *PUSB_IDLE_CALLBACK_INFO;

#define USB_IDLE_NOTIFICATION 9

#define IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION CTL_CODE \
                                                    ( \
                                                       FILE_DEVICE_USB,  \
                                                       USB_IDLE_NOTIFICATION,  \
                                                       METHOD_NEITHER,  \
                                                       FILE_ANY_ACCESS \
                                                    )


#endif  // QCSER_SELECTIVE_SUSPEND

typedef struct _QCUSB_IDLE_IRP_COMPLETION_PARAM
{
    LIST_ENTRY              List;
    PDEVICE_OBJECT          DeviceObject;
    PIRP                    Irp;
    PUSB_IDLE_CALLBACK_INFO IdleCallbackInfo;
} QCUSB_IDLE_IRP_COMPLETION_PARAM, *PQCUSB_IDLE_IRP_COMPLETION_PARAM;

// ========================================================
//          Power IRP's Handling
// ========================================================
VOID QCPWR_PowrerManagement
(
    PDEVICE_EXTENSION  pDevExt,
    PIRP               Irp,
    PIO_STACK_LOCATION IrpStack
);

BOOLEAN QCPWR_Prelude
(
    PIRP               Irp,
    PDEVICE_EXTENSION  pDevExt,
    PNTSTATUS          pNtStatus
);

NTSTATUS QCPWR_QuerySystemPowerState
(
    PDEVICE_EXTENSION  pDevExt,
    SYSTEM_POWER_STATE PowerState
);
NTSTATUS QCPWR_QueryDevicePowerState
(
    PDEVICE_EXTENSION  pDevExt,
    DEVICE_POWER_STATE PowerState
);

NTSTATUS QCPWR_SetSystemPowerState
(
    PDEVICE_EXTENSION  pDevExt,
    SYSTEM_POWER_STATE PowerState
);

VOID QCPWR_CancelPendingIrps
(
    PDEVICE_EXTENSION  pDevExt,
    SYSTEM_POWER_STATE PowerState
);

NTSTATUS QCPWR_SetDevicePowerState
(
    PDEVICE_EXTENSION  pDevExt,
    PIRP               Irp,
    DEVICE_POWER_STATE PowerState
);

NTSTATUS QCPWR_SystemPowerRequestCompletion
(
    PDEVICE_OBJECT    DeviceObject,
    PIRP              Irp,
    PDEVICE_EXTENSION pDevExt
);

VOID QCPWR_RequestDevicePowerIrp
(
    PDEVICE_EXTENSION pDevExt,
    PIRP              Irp
);

// This routine is set one stack locaton higher than the
// following two power-up/down routines
VOID QCPWR_TopDevicePowerIrpCompletionRoutine
(
    PDEVICE_OBJECT DeviceObject,
    UCHAR MinorFunction,
    POWER_STATE PowerState,
    PVOID Context,
    PIO_STATUS_BLOCK IoStatus
);

NTSTATUS QCPWR_DevicePowerUpIrpCompletion
(
    PDEVICE_OBJECT    DeviceObject,
    PIRP              Irp,
    PDEVICE_EXTENSION pDevExt
);

NTSTATUS QCPWR_DevicePowerDownIrpCompletion
(
    PDEVICE_OBJECT    DeviceObject,
    PIRP              Irp,
    PDEVICE_EXTENSION pDevExt
);

NTSTATUS QCPWR_DecreaseDevicePower
(
    PDEVICE_EXTENSION  pDevExt,
    PIRP               Irp,
    DEVICE_POWER_STATE NewDevicePower
);

NTSTATUS QCPWR_ScheduleDevicePowerUp
(
    PDEVICE_EXTENSION pDevExt,
    PIRP              Irp
);

VOID QCPWR_PowerUpDevice
(
    PDEVICE_OBJECT DeviceObject,
    PVOID          Context
);

// ========================================================
//          Selective Suspend
// ========================================================
VOID QCPWR_CancelIdleTimer
(
    PDEVICE_EXTENSION pDevExt,
    UCHAR             BusyMask,
    BOOLEAN           CancelIdleNotificationIrp,
    UCHAR             Cookie
);

VOID QCPWR_SetIdleTimer
(
    PDEVICE_EXTENSION pDevExt,
    UCHAR             BusyMask,
    BOOLEAN           NoReset,
    UCHAR             Cookie
);

VOID QCPWR_IdleDpcRoutine
(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2
);

NTSTATUS QCPWR_RegisterIdleNotification(PDEVICE_EXTENSION pDevExt);

VOID QCPWR_IdleNotificationCallback(PDEVICE_EXTENSION DeviceExtension);

VOID QCPWR_IdleNotificationCallbackEpisode(PDEVICE_EXTENSION DeviceExtension);

VOID QCPWR_TopDeviceSetPowerIrpCompletionRoutine
(
    PDEVICE_OBJECT DeviceObject,
    UCHAR MinorFunction,
    POWER_STATE PowerState,
    PVOID Context,
    PIO_STATUS_BLOCK IoStatus
);

NTSTATUS QCPWR_IdleNotificationIrpCompletion
(
    PDEVICE_OBJECT          DeviceObject,
    PIRP                    Irp,
    PUSB_IDLE_CALLBACK_INFO IdleCallbackInfo
);

VOID QCPWR_GetOutOfIdleState(PDEVICE_EXTENSION pDevExt);

NTSTATUS QCPWR_IdleNotificationIrpCompletionEpisode
(
    PDEVICE_OBJECT          DeviceObject,
    PIRP                    Irp,
    PUSB_IDLE_CALLBACK_INFO IdleCallbackInfo
);

VOID QCPWR_TopIdlePowerUpCompletionRoutine
(
    PDEVICE_OBJECT   DeviceObject,
    UCHAR            MinorFunction,
    POWER_STATE      PowerState,
    PVOID            Context,
    PIO_STATUS_BLOCK IoStatus
);

VOID QCPWR_CancelIdleNotificationIrp(PDEVICE_EXTENSION pDevExt, UCHAR Cookie);

// ========================================================
//          Wait-Wake
// ========================================================
VOID QCPWR_ProcessWaitWake(PDEVICE_EXTENSION pDevExt, PIRP Irp);

NTSTATUS QCPWR_WaitWakeCompletion
(
    PDEVICE_OBJECT    DeviceObject,
    PIRP              Irp,
    PDEVICE_EXTENSION pDevExt
);

VOID QCPWR_RegisterWaitWakeIrp(PDEVICE_EXTENSION  pDevExt, UCHAR Cookie);

VOID QCPWR_TopWaitWakeCompletionRoutine
(
    PDEVICE_OBJECT   DeviceObject,
    UCHAR            MinorFunction,
    POWER_STATE      PowerState,
    PVOID            Context,
    PIO_STATUS_BLOCK IoStatus
);

VOID QCPWR_TopDeviceWakeUpCompletionRoutine
(
    PDEVICE_OBJECT   DeviceObject,
    UCHAR            MinorFunction,
    POWER_STATE      PowerState,
    PVOID            Context,
    PIO_STATUS_BLOCK IoStatus
);

VOID QCPWR_CancelWaitWakeIrp(PDEVICE_EXTENSION pDevExt, UCHAR Cookie);

// ========================================================
//          Version Support for Power Management
// ========================================================
VOID QCPWR_GetWdmVersion(PDEVICE_EXTENSION pDevExt);

// ========================================================
//          Data Threads Suspend/Resume
// ========================================================
BOOLEAN QCPWR_CheckToWakeup
(
    PDEVICE_EXTENSION pDevExt,
    PIRP              Irp,
    UCHAR             BusyMask,
    UCHAR             Cookie
);
VOID QCPWR_ResumeDataThreads(PDEVICE_EXTENSION pDevExt, BOOLEAN Blocking);

// ========================================================
//      Power management WMI guids for device control
// ========================================================
#define QCPWR_WMI_POWER_DEVICE_ENABLE      0x00
#define QCPWR_WMI_POWER_DEVICE_WAKE_ENABLE 0x01

#define QCPWR_REG_NAME_PMENABLED L"QCDriverPowerManagementEnabled"
#define QCPWR_REG_NAME_WWENABLED L"QCDriverWaitWakeEnabled"

VOID QCPWR_RegisterWmiPowerGuids(PDEVICE_EXTENSION pDevExt);

VOID QCPWR_DeregisterWmiPowerGuids(PDEVICE_EXTENSION pDevExt);

NTSTATUS QCPWR_ProcessSystemControlIrps
(
    IN  PDEVICE_EXTENSION pDevExt,
    IN  PIRP              Irp
);

NTSTATUS QCPWR_PMQueryWmiRegInfo
(
    IN  PDEVICE_OBJECT   DeviceObject,
    OUT PULONG           RegFlags,
    OUT PUNICODE_STRING  InstanceName,
    OUT PUNICODE_STRING *RegistryPath,
    OUT PUNICODE_STRING  MofResourceName,
    OUT PDEVICE_OBJECT  *Pdo
);

NTSTATUS QCPWR_PMQueryWmiDataBlock
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           Irp,
    IN ULONG          GuidIndex,
    IN ULONG          InstanceIndex,
    IN ULONG          InstanceCount,
    IN OUT PULONG     InstanceLengthArray,
    IN ULONG          BufferAvail,
    OUT PUCHAR        Buffer
);

NTSTATUS QCPWR_PMSetWmiDataBlock
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           Irp,
    IN ULONG          GuidIndex,
    IN ULONG          InstanceIndex,
    IN ULONG          BufferSize,
    IN PUCHAR         Buffer
);

NTSTATUS QCPWR_PMSetWmiDataItem
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           Irp,
    IN ULONG          GuidIndex,
    IN ULONG          InstanceIndex,
    IN ULONG          DataItemId,
    IN ULONG          BufferSize,
    IN PUCHAR         Buffer
);

NTSTATUS QCPWR_SetPMState
(
    PDEVICE_EXTENSION pDevExt,
    BOOLEAN           IsEnabled
);

NTSTATUS QCPWR_SetWaitWakeState
(
    PDEVICE_EXTENSION pDevExt,
    BOOLEAN           IsEnabled
);

// ========================================================
//      Power States in Device Capabilities
// ========================================================
VOID QCPWR_VerifyDeviceCapabilities(PDEVICE_EXTENSION pDevExt);

// ========================================================
//      Enqueue Power IRP
// ========================================================
VOID QCPWR_Enqueue(PDEVICE_EXTENSION  pDevExt, PIRP Irp);

// ========================================================
//      EX WDM Device API's
// ========================================================
VOID USBPWR_StartExWdmDeviceMonitor(PDEVICE_EXTENSION pDevExt);

VOID USBPWR_StopExWdmDeviceMonitor(PDEVICE_EXTENSION pDevExt);

NTSTATUS USBPWR_DeviceInterfaceChange
(
    PVOID NotificationStructure,
    PVOID Context
);

NTSTATUS USBPWR_DeviceChange
(
    PTARGET_DEVICE_REMOVAL_NOTIFICATION NotificationStruct,
    PVOID                               Context
);

NTSTATUS USBPWR_OpenExWdmDevice(PDEVICE_EXTENSION pDevExt);

VOID USBPWR_CloseExWdmDevice(PDEVICE_EXTENSION pDevExt);

VOID USBPWR_QuerySystemPowerState(PDEVICE_EXTENSION pDevExt);

SYSTEM_POWER_STATE USBPWR_GetSystemPowerState
(
    PDEVICE_EXTENSION    pDevExt,
    QCUSB_POWER_REQ_TYPE Type
);

NTSTATUS USBPWR_RegisterXwdmNotification(PDEVICE_EXTENSION pDevExt);

VOID USBPWR_CancelXwdmNotification(PDEVICE_EXTENSION pDevExt);

NTSTATUS USBPWR_HandleExWdmQueryRemove(PDEVICE_EXTENSION pDevExt);

NTSTATUS USBPWR_HandleExWdmReopen(PDEVICE_EXTENSION pDevExt);

#endif // QCPWR_H
