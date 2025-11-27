#ifndef QCPNP_H
#define QCPNP_H

#include "QCMAIN.h"

#define DEVICE_NAME_PATH              L"\\Device\\"
#define DEVICE_LINK_NAME_PATH         L"\\DosDevices\\Global\\"

#define REG_VALUENAME_PORTNAME        L"PortName"
#define REG_DEVICE_MAP_SUBKEY         L"SERIALCOMM"
#define REG_SERIAL_DEVICE_MAP         L"HARDWARE\\DEVICEMAP\\SERIALCOMM\\"

#ifndef GUID_DEVINTERFACE_MODEM 
DEFINE_GUID(GUID_DEVINTERFACE_MODEM, 0x2c7089aa, 0x2e0e, 0x11d1, 0xb1, 0x14, 0x00, 0xc0, 0x4f, 0xc2, 0xaa, 0xe4);
#endif

#define QCPNP_THREAD_INIT_TIMEOUT_MS  1000
#define QCPNP_FILE_CLOSE_TIMEOUT_MS   1000

EVT_WDF_DRIVER_DEVICE_ADD             QCPNP_EvtDeviceAdd;
EVT_WDF_DEVICE_FILE_CREATE            QCPNP_EvtFileCreate;
EVT_WDF_FILE_CLOSE                    QCPNP_EvtFileClose;
EVT_WDF_OBJECT_CONTEXT_CLEANUP        QCPNP_EvtDriverCleanup;
EVT_WDF_OBJECT_CONTEXT_CLEANUP        QCPNP_EvtDeviceCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE       QCPNP_EvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE       QCPNP_EvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY               QCPNP_EvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT                QCPNP_EvtDeviceD0Exit;
EVT_WDF_DEVICE_QUERY_REMOVE           QCPNP_EvtDeviceQueryRemoval;
EVT_WDF_REQUEST_COMPLETION_ROUTINE    QCPNP_GetParentDevNameCompletion;
EVT_WDFDEVICE_WDM_IRP_PREPROCESS      QCPNP_WdmPreprocessSystemControl;
WMI_QUERY_DATABLOCK_CALLBACK          QCPNP_PMQueryWmiDataBlock;
WMI_SET_DATABLOCK_CALLBACK            QCPNP_PMSetWmiDataBlock;
WMI_SET_DATAITEM_CALLBACK             QCPNP_PMSetWmiDataItem;
WMI_QUERY_REGINFO_CALLBACK            QCPNP_PMQueryWmiRegInfo;

NTSTATUS QCPNP_DeviceCreate
(
    PWDFDEVICE_INIT  DeviceInit,
    PDEVICE_CONTEXT* DeviceContext
);

DEVICE_TYPE QCPNP_GetDeviceType
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCPNP_DeviceConfig
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCPNP_ConfigUsbDevice
(
    WDFDEVICE Device
);

NTSTATUS QCPNP_EnableSelectiveSuspend
(
    WDFDEVICE Device
);

NTSTATUS QCPNP_DisableSelectiveSuspend
(
    WDFDEVICE Device
);

NTSTATUS QCPNP_VendorRegistryProcess
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCPNP_ResetUsbPipe
(
    PDEVICE_CONTEXT pDevContext,
    WDFUSBPIPE      usbPipe,
    ULONGLONG       timoutMs
);

NTSTATUS QCPNP_CreateWorkerThread
(
    PDEVICE_CONTEXT pDevContext,
    PKSTART_ROUTINE startRoutine,
    PKEVENT waitForStartedEvent,
    PLARGE_INTEGER waitForStartedTimeout,
    PVOID* threadObject
);

NTSTATUS QCPNP_SetupIoThreadsAndQueues
(
    PDEVICE_CONTEXT pDevContext
);

VOID QCPNP_NotifyDeviceRemoval
(
    PDEVICE_CONTEXT pDevContext
);

VOID QCPNP_RetrieveServiceConfig
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCPNP_ReportDeviceName
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCPNP_RegisterDevName
(
   PDEVICE_CONTEXT pDevContext,
   ULONG       IoControlCode,
   PVOID       Buffer,
   ULONG       BufferLength
);

void QCPNP_RegisterDevNameCompletion
(
    WDFREQUEST  Request,
    WDFIOTARGET Target,
    PWDF_REQUEST_COMPLETION_PARAMS Params,
    WDFCONTEXT  Context
);

NTSTATUS QCPNP_RegisterWmiPowerGuid
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCPNP_SetFunctionProtocol
(
    PDEVICE_CONTEXT pDevContext, 
    ULONG ProtocolCode
);

NTSTATUS QCPNP_GetStringDescriptor
(
   WDFDEVICE      Device,
   UCHAR          Index,
   USHORT         LanguageId,
   BOOLEAN        MatchPrefix
);

NTSTATUS QCPNP_GetCID
(
   PDEVICE_CONTEXT pDevContext,
   PCHAR  ProductString,
   INT    ProductStrLen
);

NTSTATUS QCPNP_GetSocVer
(
    PDEVICE_CONTEXT pDevContext,
    PCHAR  ProductString,
    INT    ProductStrLen
);

NTSTATUS QCPNP_PostVendorRegistryProcess
(
    PDEVICE_CONTEXT pDevContext
);

BOOLEAN QCUTIL_IsHighSpeedDevice
(
    PDEVICE_CONTEXT pDevContext
);

//Empty Functions to support QCPNP_RetrieveServiceConfig
BOOLEAN QCUTIL_IsHighSpeedDevice
(
    PDEVICE_CONTEXT pDevContext
);

VOID QCPWR_SyncUpWaitWake
(
    PDEVICE_CONTEXT pDevContext
);

VOID QCPWR_SetIdleTimer
(
    PDEVICE_CONTEXT pDevContex, 
    UCHAR           BusyMask, 
    BOOLEAN         NoReset, 
    UCHAR           Cookie
);

NTSTATUS QCPNP_SetStamp
(
    PDEVICE_CONTEXT pDevContext,
    BOOLEAN        Startup
);
#endif // QCPNP_H
