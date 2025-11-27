/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q C F I L T E R . H

GENERAL DESCRIPTION
    This module contains WDS Client functions for IP(V6).
    requests.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include <ntddk.h>
#include <wdmsec.h> 
#include <initguid.h>
#include <dontuse.h>

//
// GUID definition are required to be outside of header inclusion pragma to avoid
// error during precompiled headers.
//
// {B7D25408-B34A-4f67-BF36-F6182CC0EC48}
DEFINE_GUID(QMI_DEVICE_INTERFACE_CLASS, 
0xb7d25408, 0xb34a, 0x4f67, 0xbf, 0x36, 0xf6, 0x18, 0x2c, 0xc0, 0xec, 0x48);


#if !defined(_FILTER_H_)
#define _FILTER_H_

#ifndef  STATUS_CONTINUE_COMPLETION 
#define STATUS_CONTINUE_COMPLETION      STATUS_SUCCESS
#endif

#define POOL_TAG   'liFT'

#define NTDEVICE_NAME_STRING      L"\\Device\\QcFilter"
#define SYMBOLIC_NAME_STRING      L"\\DosDevices\\QcFilter"

#define VEN_DEV_SERNUM            L"QCDeviceSerialNumber"
#define VEN_DEV_MSM_SERNUM        L"QCDeviceMsmSerialNumber"

#define VEN_DBG_MASK        L"QCDriverDebugMask"
#define VEN_MUX_ENABLED             L"QCDeviceMuxEnable"
#define VEN_MUX_STARTRMNETIF        L"QCDeviceStartIf"
#define VEN_MUX_NUMRMNETIF          L"QCDeviceNumIf"
#define VEN_MUX_NUM_MUX_RMNETIF     L"QCDeviceNumMuxIf"

#define NUM_MUX_INTERFACES 4
#define NUM_START_INTERFACEID 0x80

#define NUM_MUX_INTERFACES_MUX 16

// Debug settings
#ifndef EVENT_TRACING

#define WPP_LEVEL_FORCE    0x0
#define WPP_LEVEL_CRITICAL 0x1
#define WPP_LEVEL_ERROR    0x2
#define WPP_LEVEL_INFO     0x3
#define WPP_LEVEL_DETAIL   0x4
#define WPP_LEVEL_TRACE    0x5
#define WPP_LEVEL_VERBOSE  0x6

#endif

#define DBG_LEVEL_FORCE    WPP_LEVEL_FORCE
#define DBG_LEVEL_CRITICAL WPP_LEVEL_CRITICAL
#define DBG_LEVEL_ERROR    WPP_LEVEL_ERROR
#define DBG_LEVEL_INFO     WPP_LEVEL_INFO
#define DBG_LEVEL_DETAIL   WPP_LEVEL_DETAIL
#define DBG_LEVEL_TRACE    WPP_LEVEL_TRACE
#define DBG_LEVEL_VERBOSE  WPP_LEVEL_VERBOSE

#if DBG
   #define QCFLT_DbgPrint(level,_x_) \
           { \
              if ((pDevExt->DebugLevel >= level) || \
                   (level == 0) ) \
              { \
                 DbgPrint _x_; \
              } \
           }
   #define QCFLT_DbgPrintG(_x_) \
           { \
              { \
                 DbgPrint _x_; \
              } \
           }
   #define TRAP() DbgBreakPoint()
   
#else 
   #define QCFLT_DbgPrint(level,_x_)
   #define QCFLT_DbgPrintG(_x_)
   #define TRAP()
#endif

/* PNP State definations */
typedef enum _QC_DEVICE_PNP_STATE {
   QC_NOT_STARTED = 0,
   QC_STARTED,          
   QC_STOP_PENDING,   
   QC_STOPPED,        
   QC_REMOVE_PENDING, 
   QC_SURPRISE_REMOVE_PENDING,
   QC_DELETED
} QC_DEVICE_PNP_STATE;


#define QC_INIT_PNP_STATE(DevExt)    \
        (DevExt)->DevicePnPState =  QC_NOT_STARTED;\
        (DevExt)->PreviousPnPState = QC_NOT_STARTED;

#define QC_SET_NEW_PNP_STATE(DevExt, pnpState) \
        (DevExt)->PreviousPnPState =  (DevExt)->DevicePnPState;\
        (DevExt)->DevicePnPState = (pnpState);

#define QC_SET_PREV_PNP_STATE(DevExt)   \
        (DevExt)->DevicePnPState =   (DevExt)->PreviousPnPState;\


/* Other definations */
#define QcAcquireSpinLock(_lock,_levelOrHandle) {KeAcquireSpinLock(_lock,_levelOrHandle);}
#define QcReleaseSpinLock(_lock,_levelOrHandle) {KeReleaseSpinLock(_lock,_levelOrHandle);}
#define QcAcquireSpinLockAtDpcLevel(_lock,_levelOrHandle) KeAcquireSpinLockAtDpcLevel(_lock)
#define QcReleaseSpinLockFromDpcLevel(_lock,_levelOrHandle) KeReleaseSpinLockFromDpcLevel(_lock)

#define QcAcquireSpinLockWithLevel(_lock,_levelOrHandle, _level) \
        { \
           if (_level < DISPATCH_LEVEL) \
           { \
              QcAcquireSpinLock(_lock,_levelOrHandle); \
           } \
           else \
           { \
              QcAcquireSpinLockAtDpcLevel(_lock,_levelOrHandle); \
           } \
        }
#define QcReleaseSpinLockWithLevel(_lock,_levelOrHandle, _level) \
        { \
           if (_level < DISPATCH_LEVEL) \
           { \
              QcReleaseSpinLock(_lock,_levelOrHandle); \
           } \
           else \
           { \
              QcReleaseSpinLockFromDpcLevel(_lock,_levelOrHandle); \
           } \
        }


#define _closeHandle(in, clue) \
        { \
           if ( (in) ) \
           { \
              KPROCESSOR_MODE procMode = ExGetPreviousMode(); \
              if (procMode == KernelMode) \
              { \
                 if (STATUS_SUCCESS != (ZwClose(in))) \
                 { \
                 } \
                 else \
                 { \
                 } \
                 in=NULL; \
              } \
              else \
              { \
              } \
           } \
        }

#define _ExAllocatePool(a,b,c) ExAllocatePool(a,b)
#define _ExFreePool(_a) { ExFreePool(_a); _a=NULL; }

#define _freeBuf(_a) \
        { \
           if ( (_a) ) \
           { \
              _ExFreePool( (_a) ); \
              (_a) = NULL; \
           } \
        }

#define _zeroUnicode(_a) \
        { \
           (_a).Buffer = NULL; \
           (_a).MaximumLength = 0; \
           (_a).Length = 0; \
        }

#define _zeroString(_a) { _zeroUnicode(_a) }

#define _IoMarkIrpPending(_pIrp_) {_pIrp_->IoStatus.Status = STATUS_PENDING; IoMarkIrpPending(_pIrp_);}

// Device Type
typedef enum _DEVICE_TYPE 
{
   DEVICE_TYPE_INVALID = 0,         // Invalid Type;
   DEVICE_TYPE_QCFIDO,              // Device is a filter device.
   DEVICE_TYPE_QCCDO,               // Device is a control device.
} DEVICE_TYPE;

#define DUMMY_EVENT_INDEX                         0
#define QCFLT_FILTER_CREATE_CONTROL               1
#define QCFLT_FILTER_CLOSE_CONTROL                2
#define QCFLT_STOP_FILTER                         3
#define QCFLT_FILTER_EVENT_COUNT                  4

#define MAX_NAME_LEN 1024

typedef struct _FILTER_THREAD_CONTEXT
{
   // for FILTER thread
   PKTHREAD       pFilterThread;
   HANDLE         hFilterThreadHandle;
   BOOLEAN        bFilterThreadInCreation;
   KEVENT         FilterThreadStartedEvent;
   BOOLEAN        bFilterActive;
   BOOLEAN        bFilterStopped;
   PIRP           pFilterCurrent;
   PKEVENT        pFilterEvents[QCFLT_FILTER_EVENT_COUNT];
   KEVENT         FilterCreateControlDeviceEvent;
   KEVENT         FilterCloseControlDeviceEvent;
   KEVENT         FilterStopEvent;
   KEVENT         FilterThreadExitEvent;
   ULONG          FilterThreadInCancellation;
   
}FILTER_THREAD_CONTEXT, *PFILTER_THREAD_CONTEXT;

typedef struct _INTERFACE_DETAILS
{
   ULONG RmnetInterface;
   PVOID FilterDevInfo;
   PVOID pAdapter;
}INTERFACE_DETAILS, *PINTERFACE_DETAILS;

typedef struct _INTERFACE_MUX_LIST
{
   INTERFACE_DETAILS PhysicalRmnetInterface;
   INTERFACE_DETAILS MuxInterfaces[NUM_MUX_INTERFACES_MUX];
}INTERFACE_MUX_LIST, *PINTERFACE_MUX_LIST;

typedef struct _MUX_INTERFACE_INFO
{
    UCHAR InterfaceNumber;
    UCHAR PhysicalInterfaceNumber;
    ULONG MuxEnabled;
    PVOID FilterDeviceObj;
}MUX_INTERFACE_INFO,* PMUX_INTERFACE_INFO;

typedef struct _DEVICE_EXTENSION
{
   DEVICE_TYPE Type;
   // A back pointer to the device object.
   PDEVICE_OBJECT Self;
   // The top of the stack before this filter was added.
   PDEVICE_OBJECT NextLowerDriver;
   // A pointer to the PDO.
   PDEVICE_OBJECT QCPhysicalDeviceObject;
   // current PnP state of the device
   QC_DEVICE_PNP_STATE DevicePnPState;
   // Remembers the previous pnp state
   QC_DEVICE_PNP_STATE PreviousPnPState;
   // Removelock to track IRPs so that device can be removed and
   // the driver can be unloaded safely.
   IO_REMOVE_LOCK RemoveLock;
   // The top of the stack before this filter was added.
   PDEVICE_OBJECT ControlDeviceObject;
   // Adapter Context
   PVOID pAdapterContext;
   // Filter thread IO Lock
   KSPIN_LOCK FilterSpinLock;
   // Filter Thread for handeling IRPs
   FILTER_THREAD_CONTEXT FilterThread;
   // Dispatch Queue
   LIST_ENTRY DispatchQueue;
   // Current IRP Processing
   PIRP pCurrentDispatch;

   KEVENT ForTimeoutEvent;   
   
   //Debug Settings
   CHAR PortName[32];
   INT DebugLevel;
   ULONG DebugMask;

   UCHAR VID;
   UCHAR PID;

   USB_DEVICE_DESCRIPTOR DeviceDescriptor;

   CHAR DevSerialNumber[256];  // to hold USB_STRING_DESCRIPTOR of the serial number

   ULONG MuxSupport;

   ULONG StartRmnetIface;

   ULONG NumRmnetIface;

   ULONG NumMuxRmnetIface;

   ULONG NumRmnetIfaceCount;

   ULONG NumRmnetIfaceVCount;

   INTERFACE_MUX_LIST InterfaceMuxList[NUM_MUX_INTERFACES_MUX];

   CHAR  PeerDevName[4096];
   ULONG PeerDevNameLength;

   WCHAR FriendlyNameHolder[MAX_NAME_LEN];

} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

typedef struct _FILTER_DEVICE_INFO
{
   PVOID pAdapter;
   PUNICODE_STRING pDeviceName;
   PUNICODE_STRING pDeviceLinkName;
   PDRIVER_DISPATCH*  MajorFunctions;
   PDEVICE_OBJECT pControlDeviceObject;
   PDEVICE_OBJECT pFilterDeviceObject;
   PUNICODE_STRING pFileName;
   ULONG UniqueIdentifier;
}FILTER_DEVICE_INFO, *PFILTER_DEVICE_INFO;

typedef struct _FILTER_DEVICE_LIST
{
   LIST_ENTRY  List;
   PVOID pAdapter;
   UNICODE_STRING DeviceName;
   UNICODE_STRING DeviceLinkName;
   PDRIVER_DISPATCH DispatchTable[IRP_MJ_MAXIMUM_FUNCTION+1];
   PDEVICE_OBJECT pControlDeviceObject;
   PDEVICE_OBJECT pFilterDeviceObject;
   PUNICODE_STRING pFileName;
   ULONG UniqueIdentifier;
}FILTER_DEVICE_LIST, *PFILTER_DEVICE_LIST;

typedef struct _CONTROL_DEVICE_EXTENSION 
{

   DEVICE_TYPE Type;
   // A back pointer to the device object.
   PDEVICE_OBJECT Self;
   // The top of the stack before this filter was added.
   PDRIVER_OBJECT DriverObject;
   // The top of the stack before this filter was added.
   PDEVICE_OBJECT FidoDeviceObject;
   // Adapter Context
   PVOID pAdapterContext;

   UNICODE_STRING SymbolicName;
   UNICODE_STRING DeviceName;

   ULONG Deleted; // False if the deviceobject is valid, TRUE if it's deleted

   BOOLEAN InService;

   LONG DeviceOpenCount;
   
   //Debug Settings
   CHAR PortName[32];
   INT DebugLevel;
   ULONG DebugMask;

   IO_REMOVE_LOCK RmLock;

} CONTROL_DEVICE_EXTENSION, *PCONTROL_DEVICE_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

DRIVER_ADD_DEVICE QCFilterAddDevice;

DRIVER_DISPATCH QCFilterDispatchPnp;

DRIVER_DISPATCH QCFilterDispatchPower;

DRIVER_DISPATCH QCFilterPass;

DRIVER_UNLOAD QCFilterUnload;

IO_COMPLETION_ROUTINE QCFilterDeviceUsageNotificationCompletionRoutine;

__drv_dispatchType(IRP_MJ_CREATE)
__drv_dispatchType(IRP_MJ_CLOSE)
__drv_dispatchType(IRP_MJ_CLEANUP)
__drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
__drv_dispatchType(IRP_MJ_READ)
__drv_dispatchType(IRP_MJ_WRITE)
DRIVER_DISPATCH QCFilterDispatchIo;

PCHAR QCPnPMinorFunctionString ( UCHAR MinorFunction);

NTSTATUS QCFilterStartCompletionRoutine( PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);

NTSTATUS QCFilterQueryCapabilitiesCompletionRoutine ( PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);

NTSTATUS
QCFilterCreateControlObject( PDEVICE_OBJECT DeviceObject, PFILTER_DEVICE_INFO pFilterDeviceInfo);

NTSTATUS
QCFilterDeleteControlObject( PDEVICE_OBJECT DeviceObject,PFILTER_DEVICE_INFO pFilterDeviceInfo);

NTSTATUS QCFilter_StartFilterThread(PDEVICE_OBJECT devObj);

VOID QCFilter_CancelFilterThread(PDEVICE_OBJECT pDevObj);

VOID QCFLT_Wait(PDEVICE_EXTENSION pDevExt, LONGLONG WaitTime);

NTSTATUS QCFilter_InitializeDeviceExt( PDEVICE_OBJECT deviceObject );

BOOLEAN QCFLT_IsIrpInQueue
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY Queue,
   PIRP        Irp
);

NTSTATUS QCFLT_AddControlDevice( PFILTER_DEVICE_INFO pFilterDeviceInfo );

NTSTATUS QCFLT_DeleteControlDevice( PDEVICE_OBJECT pControlDeviceObject );

PFILTER_DEVICE_LIST QCFLT_FindControlDevice( PDEVICE_OBJECT pControlDeviceObject);

PDEVICE_OBJECT QCFLT_FindControlDeviceFido( PDEVICE_OBJECT pFido );

PFILTER_DEVICE_LIST QCFLT_FindFilterDevice( PDEVICE_OBJECT    DeviceObject, 
                                                 PFILTER_DEVICE_INFO pFilterDeviceInfo );

VOID QCFLT_FreeControlDeviceList();

NTSTATUS QCFLT_VendorRegistryProcess
(
   IN PDRIVER_OBJECT pDriverObject,
   IN PDEVICE_OBJECT PhysicalDeviceObject,
   IN PDEVICE_OBJECT Fido 
);

NTSTATUS getRegDwValueEntryData
(
   IN HANDLE OpenRegKey, 
   IN PWSTR ValueEntryName,
   OUT PULONG pValueEntryData
);

NTSTATUS getRegSzValueEntryData
(
   IN HANDLE OpenRegKey, 
   IN PWSTR ValueEntryName,
   OUT PCHAR pValueEntryData
);

NTSTATUS GetValueEntry( HANDLE hKey, PWSTR FieldName, PKEY_VALUE_FULL_INFORMATION  *pKeyValInfo );

ULONG GetDwordField( PKEY_VALUE_FULL_INFORMATION pKvi );

VOID GetSzField(PKEY_VALUE_FULL_INFORMATION pKvi, PCHAR ValueEntryDataSz);

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

NTSTATUS USBPNP_SelectAndAddInterfaces
(
   IN PDEVICE_EXTENSION pDevExt,
   IN PURB pUrb
);

VOID QCFLT_PrintBytes
(
   PVOID Buf,
   ULONG len,
   ULONG PktLen,
   char *info,
   PDEVICE_EXTENSION x,
   ULONG DbgLevel
);

NTSTATUS QCFilterCreateFriendlyName
(
   PDEVICE_EXTENSION pDevExt,
   PDEVICE_OBJECT QCPhysicalDeviceObject
);

NTSTATUS QCFilterSetFriendlyName
(
   PDEVICE_EXTENSION pDevExt,
   PDEVICE_OBJECT    QCPhysicalDeviceObject,
   PUNICODE_STRING   FriendlyName,
   PWCHAR            TargetDriverKey
);

#ifdef EVENT_TRACING
#include "QCFILTERWPP.h"
#endif

#endif

