/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B M A I N . H

GENERAL DESCRIPTION
  This is the file which contains definitions for QDSS function driver. 

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef QDBMAIN_H
#define QDBMAIN_H

#include <ntddk.h>
#include <ntintsafe.h>
#include <initguid.h>
#include <wdf.h>
#include <usbdi.h>
#include <usbdlib.h>
#include <wdfusb.h>


// {E093662A-FF18-4579-8681-506E9F561D3D}
DEFINE_GUID(QDBUSB_GUID, 
0xe093662a, 0xff18, 0x4579, 0x86, 0x81, 0x50, 0x6e, 0x9f, 0x56, 0x1d, 0x3d);


// global variable
extern LONG DevInstanceNumber;
extern ULONG SimData[2724];

#define MAX_NAME_LEN              1024
#define QDB_USB_TRANSFER_SIZE_MAX 1024*1024
#define QDB_MAX_IO_SIZE_RX        1024*16
#define IO_REQ_NUM_RX             4
#define QDB_TAG_RD  (ULONG)'RBDQ'
#define QDB_TAG_WT  (ULONG)'WBDQ'
#define QDB_TAG_GEN (ULONG)'GBDQ'

// === bits allocation ===
// 0x0000000F - Debug level
// 0x0000FFF0 - USB operation messages
// 0x00FF0000 - NDIS miniport messages
// 0xFF000000 - USB/miniport IRP debugging
#define QDB_DBG_LEVEL_FORCE    0x0
#define QDB_DBG_LEVEL_CRITICAL 0x1
#define QDB_DBG_LEVEL_ERROR    0x2
#define QDB_DBG_LEVEL_INFO     0x3
#define QDB_DBG_LEVEL_DETAIL   0x4
#define QDB_DBG_LEVEL_DATA     0x5
#define QDB_DBG_LEVEL_TRACE    0x6
#define QDB_DBG_LEVEL_VERBOSE  0x7

#define QDB_DBG_MASK_CONTROL  0x00000010
#define QDB_DBG_MASK_READ     0x00000020
#define QDB_DBG_MASK_WRITE    0x00000040
#define QDB_DBG_MASK_ENC      0x00000080
#define QDB_DBG_MASK_POWER    0x00000100
#define QDB_DBG_MASK_STATE    0x00000200
#define QDB_DBG_MASK_RDATA    0x00001000
#define QDB_DBG_MASK_WDATA    0x00002000
#define QDB_DBG_MASK_ENDAT    0x00004000

#define QDB_DBG_MASK_RIRP     0x10000000
#define QDB_DBG_MASK_WIRP     0x20000000
#define QDB_DBG_MASK_CIRP     0x40000000
#define QDB_DBG_MASK_PIRP     0x80000000

#undef QDB_DbgPrint
#undef QDB_DbgPrintG

#ifdef QDB_DBGPRINT
   #define QDB_DbgPrintG(mask,level,_x_) \
           { \
                 DbgPrint _x_; \
           }

   #define QDB_DbgPrint(mask,level,_x_) \
           { \
              if ((pDevContext->DebugMask & mask) && \
                  (pDevContext->DebugLevel >= level)) \
              { \
                 DbgPrint _x_; \
              } \
           }
#else

   #define QDB_DbgPrint(mask,level,_x_)
   #define QDB_DbgPrintG(mask,level,_x_)

#endif // QDB_DBGPRINT

#undef ntohs
#define ntohs(x) \
        ((USHORT)((((USHORT)(x) & 0x00ff) << 8) | \
        (((USHORT)(x) & 0xff00) >> 8)))

#define QDB_DBG_NAME_PREFIX "qdb"

#define QDB_FUNCTION_TYPE_QDSS 0
#define QDB_FUNCTION_TYPE_DPL  1

#define QCDEV_IOCTL_INDEX 2048

// Reserve  QCDEV_IOCTL_INDEX + 60 to 69 for QDB_DPL function
#define IOCTL_QCDEV_DPL_STATS CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+60, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_REQUEST_DEVICEID CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+1304, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

/* Make the following code as 3354 - Parent device name from filter device*/
#define IOCTL_QCDEV_GET_PARENT_DEV_NAME CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                               QCDEV_IOCTL_INDEX+1306, \
                                               METHOD_BUFFERED, \
                                               FILE_ANY_ACCESS)

typedef struct _QDB_IO_REQUEST
{
   LIST_ENTRY List;
   PVOID      DeviceContext;
   WDFREQUEST IoRequest;
   WDFMEMORY  IoMemory;
   PVOID      IoBuffer;
   int        Index;
   int        IsPending;
} QDB_IO_REQUEST, *PQDB_IO_REQUEST;

#pragma pack(push, 1)

typedef struct _QCQMAP_STRUCT
{
   UCHAR PadCD;
   UCHAR MuxId;
   USHORT PacketLen;
} QCQMAP_STRUCT, *PQCQMAP_STRUCT;

#pragma pack(pop)

typedef struct _QDB_STATS
{
   LONG  OutstandingRx;  // buffers not returned to app
   LONG  DrainingRx;     // internal buffer for draining
   LONG  NumRxExhausted; // no RX buffers
   ULONG BytesDrained;   // bytes since drain started
   ULONG PacketsDrained; // reserved -- debug only
   ULONG IoFailureCount; // debug only
} QDB_STATS, *PQDB_STATS; 

typedef struct _DEVICE_CONTEXT
{
   WDFIOTARGET                   MyIoTarget;
   WDFUSBDEVICE                  WdfUsbDevice;
   PDEVICE_OBJECT                PhysicalDeviceObject;
   PDEVICE_OBJECT                MyDevice;
   PDEVICE_OBJECT                TargetDevice;
   PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc; // dynamically allocated
   CHAR                          SerialNumber[256];
   LONG                          IfProtocol;
   UCHAR                         FriendlyName[MAX_NAME_LEN];
   WCHAR                         FriendlyNameHolder[MAX_NAME_LEN];
   UNICODE_STRING                SymbolicLink;
   BOOLEAN                       DeviceRemoval;
   KEVENT                        DrainStoppedEvent;
   ULONG                         DebugMask;
   ULONG                         DebugLevel;
   CHAR                          PortName[16];
   WDFUSBINTERFACE               UsbInterfaceTRACE;
   WDFUSBINTERFACE               UsbInterfaceDEBUG;
   WDFUSBPIPE                    TraceIN;
   WDFUSBPIPE                    DebugIN;
   WDFUSBPIPE                    DebugOUT;
   WDF_USB_PIPE_INFORMATION      TracePipeInfo;
   WDF_USB_PIPE_INFORMATION      DebugINPipeInfo;
   WDF_USB_PIPE_INFORMATION      DebugOUTPipeInfo;
   ULONG                         MaxXfrSize; // not really used
   WDFQUEUE                      RxQueue;
   WDFQUEUE                      TxQueue;
   WDFQUEUE                      DefaultQueue;
   LONG                          RxCount;
   LONG                          TxCount;
   KSPIN_LOCK                    RxLock;
   ULONG                         NumOfRxReqs;
   ULONG                         FunctionType;
   ULONG                         IoFailureThreshold;
   QDB_IO_REQUEST                RxRequest[IO_REQ_NUM_RX];
   BOOLEAN                       PipeDrain;
   QDB_STATS                     Stats;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

typedef struct _REQUEST_CONTEXT
{
   WDFMEMORY        IoBlock;
   ULONG            Index;   // for debugging purpose
} REQUEST_CONTEXT, *PREQUEST_CONTEXT;

#define QDB_FILE_TYPE_TRACE 1
#define QDB_FILE_TYPE_DEBUG 2
#define QDB_FILE_TYPE_DPL   3

typedef struct _FILE_CONTEXT
{
   PDEVICE_CONTEXT DeviceContext;
   WDFUSBPIPE TraceIN;
   WDFUSBPIPE DebugIN;
   WDFUSBPIPE DebugOUT;
   UCHAR      Type;
} FILE_CONTEXT, *PFILE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, QdbDeviceGetContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, QdbRequestGetContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILE_CONTEXT, QdbFileGetContext)

NTSTATUS QDBMAIN_AllocateUnicodeString(PUNICODE_STRING Ustring, SIZE_T Size, ULONG Tag);

VOID QDBMAIN_GetRegistrySettings(WDFDEVICE Device);

#ifdef EVENT_TRACING
#include "QDBWPP.h"
#endif

#endif // QDBMAIN_H
