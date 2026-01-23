/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B D B G . H

GENERAL DESCRIPTION
    This file contains definitions various debugging purposes.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBDBG_H
#define USBDBG_H

// #define DEBUG_MSGS

// VEN = Value Entry Name
// VED = Value Entry Data

extern char gDeviceName[255];

#define ENUM_PATH           L"Enum\\"
//#define DRIVER_PATH       L"System\\CurrentControlSet\\Services\\Class\\"
#define VEN_DRIVER          L"Driver"
#define VEN_DEVICE          L"Device"
#define VEN_STATUS          L"Status"
#define VEN_USB_PATH        L"UsbPath"
#define VEN_DEV_SYM_NAME    L"DeviceSymbolicName"
#define VEN_DEV_OBJ_NAME    L"DeviceObjectNameBase"
#define VEN_DEVICE_KEY      L"DeviceKey"
#define VED_NEW_DEVICE_KEY  L"NewDeviceKey"
#define VEN_USB_ID          L"USB_ID"
#define SLASH               L"\\"
#define VEN_DEV_NUM         L"DeviceNumber"
#define VEN_DEV_NUM_N       "DeviceNumber"
#define VED_NONE            L"Not Currently Present"
#define VEN_DEV_VER         L"DriverVersion"
#define VEN_DEV_CONFIG      L"QCDriverConfig"
#define VEN_DEV_LOG_DIR     L"QCDriverLoggingDirectory"
#define VEN_DEV_RTY_NUM     L"QCDriverRetriesOnError"
#define VEN_DEV_MAX_XFR     L"QCDriverMaxPipeXferSize"
#define VEN_DEV_L2_BUFS     L"QCDriverL2Buffers"
#define VEN_DBG_MASK        L"QCDriverDebugMask"
#define VEN_DRV_RESIDENT    L"QCDriverResident"
#define VEN_DRV_WRITE_UNIT  L"QCDriverWriteUnit"
#define VEN_DRV_THREAD_PRI  L"QCDriverThreadPriority"
#define HW_KEY_BASE         "Enum\\"

#ifdef NDIS_WDM
enum Ndis_RegEntryIndex
{
    NDIS_DEV_VER = 0,    // REG_SZ
    NDIS_DEV_CONFIG,     // REG_DWORD
    NDIS_LOG_DIR,        // REG_SZ
    NDIS_RTY_NUM,        // REG_DWORD
    NDIS_MAX_XFR,        // REG_DWORD
    NDIS_L2_BUFS,        // REG_DWORD
    NDIS_DBG_MASK,       // REG_DWORD
    NDIS_RESIDENT,       // REG_DWORD
    NDIS_WRITE_UNIT,     // REG_DWORD
    NDIS_CTRL_FILE,      // REG_SZ
    NDIS_THREAD_PRI,     // REG_DWORD
    NDIS_MAX_REG_ENTRY
};

#define NDIS_STR_DEV_VER     "DriverVersion"
#define NDIS_STR_DEV_CONFIG  "QCDriverConfig"
#define NDIS_STR_LOG_DIR     "QCDriverLoggingDirectory"
#define NDIS_STR_RTY_NUM     "QCDriverRetriesOnError"
#define NDIS_STR_MAX_XFR     "QCDriverMaxPipeXferSize"
#define NDIS_STR_L2_BUFS     "QCDriverL2Buffers"
#define NDIS_STR_DBG_MASK    "QCDriverDebugMask"
#define NDIS_STR_RESIDENT    "QCDriverResident"
#define NDIS_STR_WRITE_UNIT  "QCDriverWriteUnit"
#define NDIS_STR_THREAD_PRI  "QCDriverThreadPriority"

#endif

#define DEVICE_OBJECT_PRESENT         0x0001
#define DEVICE_SYMBOLIC_NAME_PRESENT  0x0002

//#define HW_MODEM_PATH               L"Enum\\Root\\UsbMODEM\\"
#define DEVICE_PATH_1                 L"System\\CurrentControlSet\\Controls\\Class"
#define DRIVER_FIELD                  L"Driver"

#define DEVICE_PORTNAME_LABEL         L"PortName"

#define DEVICE_NAME_PATH              L"\\Device\\"
#define DEVICE_LINK_NAME_PATH         L"\\??\\"

#define QCUSB_DBG_LEVEL_FORCE    0x0
#define QCUSB_DBG_LEVEL_CRITICAL 0x1
#define QCUSB_DBG_LEVEL_ERROR    0x2
#define QCUSB_DBG_LEVEL_INFO     0x3
#define QCUSB_DBG_LEVEL_DETAIL   0x4
#define QCUSB_DBG_LEVEL_TRACE    0x5
#define QCUSB_DBG_LEVEL_VERBOSE  0x6


#ifdef EVENT_TRACING
#define QCUSB_DBG_MASK_CONTROL  0x00000001
#define QCUSB_DBG_MASK_READ     0x00000002
#define QCUSB_DBG_MASK_WRITE    0x00000004
#define QCUSB_DBG_MASK_ENC      0x00000008
#define QCUSB_DBG_MASK_POWER    0x00000010
#define QCUSB_DBG_MASK_STATE    0x00000020
#define QCUSB_DBG_MASK_RDATA    0x00000040
#define QCUSB_DBG_MASK_WDATA    0x00000080
#define QCUSB_DBG_MASK_ENDAT    0x00000100
#define QCUSB_DBG_MASK_RIRP     0x00000200
#define QCUSB_DBG_MASK_WIRP     0x00000400
#define QCUSB_DBG_MASK_CIRP     0x00000800
#define QCUSB_DBG_MASK_PIRP     0x00001000
#else

// === bits allocation ===
// 0x0000000F - Debug level
// 0x0000FFF0 - USB operation messages
// 0x00FF0000 - NDIS miniport messages
// 0xFF000000 - USB/miniport IRP debugging

#define QCUSB_DBG_MASK_CONTROL  0x00000010
#define QCUSB_DBG_MASK_READ     0x00000020
#define QCUSB_DBG_MASK_WRITE    0x00000040
#define QCUSB_DBG_MASK_ENC      0x00000080
#define QCUSB_DBG_MASK_POWER    0x00000100
#define QCUSB_DBG_MASK_STATE    0x00000200
#define QCUSB_DBG_MASK_RDATA    0x00001000
#define QCUSB_DBG_MASK_WDATA    0x00002000
#define QCUSB_DBG_MASK_ENDAT    0x00004000

#define QCUSB_DBG_MASK_RIRP     0x10000000
#define QCUSB_DBG_MASK_WIRP     0x20000000
#define QCUSB_DBG_MASK_CIRP     0x40000000
#define QCUSB_DBG_MASK_PIRP     0x80000000
#endif

// macros 
#ifdef DBG
#ifdef VERBOSE_ALLOC
typedef struct tagAllocationList
{
    struct tagAllocationList *pPrev, *pNext;
    PVOID pBuffer;
    UCHAR *pWho;
    ULONG ulSize;
}  ALLOCATION_LIST;

void _ReportMemoryUsage();
PVOID _ExAllocatePool
(
    IN    POOL_TYPE PoolType,
    IN    ULONG NumberOfBytes,
    UCHAR *pWho
);
VOID _ExFreePool(IN PVOID P);

#define REPORT_MEMORY_USAGE
#endif
#endif

#ifndef REPORT_MEMORY_USAGE
#define _ReportMemoryUsage();
#define _ExAllocatePool(a,b,c) ExAllocatePool(a,b)

#ifdef DEBUG_MSGS
#define _ExFreePool(_a) { \
         DbgPrint("\t\t\tfreeing: 0x%p \n", _a); \
         ExFreePool(_a);_a=NULL;}
#else
#define _ExFreePool(_a) { ExFreePool(_a); _a=NULL; }
#endif //DEBUG_MSGS

#endif

#define _freeUcBuf(_a) { \
     if ( (_a).Buffer ) \
     { \
       _ExFreePool( (_a).Buffer ); \
       (_a).Buffer = NULL; \
       (_a).MaximumLength = (_a).Length = 0; \
     } \
}

#define _freeString(_a) _freeUcBuf(_a)

#define _freeBuf(_a) { \
     if ( (_a) ) \
     { \
       _ExFreePool( (_a) ); \
       (_a) = NULL; \
     } \
}

#define _zeroUnicode(_a) { \
  (_a).Buffer = NULL; \
  (_a).MaximumLength = 0; \
  (_a).Length = 0; \
}

#define _zeroString(_a) { _zeroUnicode(_a) }
#define _zeroAnsi(_a) { _zeroUnicode(_a) }

#define _zeroStringPtr(_a) { \
  (_a) -> Buffer = NULL; \
  (_a) -> MaximumLength = 0; \
  (_a) -> Length = 0; \
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
                    in=NULL; \
                 } \
              } \
              else \
              { \
              } \
           } \
        }

#define _closeHandleG(x, in, clue) \
        { \
           if ( (in) ) \
           { \
              KPROCESSOR_MODE procMode = ExGetPreviousMode(); \
              if (STATUS_SUCCESS != (ZwClose(in))) \
              { \
              } \
              else \
              { \
                 in=NULL; \
              } \
           } \
        }

#define _closeRegKey(in,clue)    _closeHandleG((pDevExt->PortName),in,clue)
#define _closeRegKeyG(x,in,clue) _closeHandleG(x,in,clue)

#ifdef QCUSB_DBGPRINT
#define QCUSB_DbgPrintG(mask,level,_x_) \
           { \
              if ( ((gVendorConfig.DebugMask & mask) && \
                    (gVendorConfig.DebugLevel >= level)) || \
                   (level == QCUSB_DBG_LEVEL_FORCE) ) \
              { \
                 DbgPrint _x_; \
              }    \
           }

#define QCUSB_DbgPrintX(x,mask,level,_x_) \
           { \
              if (x != NULL) \
              { \
                 if ( (((x)->DebugMask & mask) && \
                       ((x)->DebugLevel >= level)) || \
                      (level == QCUSB_DBG_LEVEL_FORCE) ) \
                 { \
                    DbgPrint _x_; \
                 }    \
              } \
              else \
              { \
                 QCUSB_DbgPrintG(mask,level,_x_) \
              } \
           }

#define QCUSB_DbgPrint(mask,level,_x_) \
           { \
              if ( ((pDevExt->DebugMask & mask) && \
                    (pDevExt->DebugLevel >= level)) || \
                   (level == QCUSB_DBG_LEVEL_FORCE) ) \
              { \
                 DbgPrint _x_; \
              }    \
           }

#ifdef QCUSB_DBGPRINT2
#define QCUSB_DbgPrint2(mask,level,_x_) QCUSB_DbgPrint(mask,level,_x_)
#define QCUSB_DbgPrintX2(x,mask,level,_x_) QCUSB_DbgPrintX(x,mask,level,_x_)
#define QCUSB_DbgPrintG2(mask,level,_x_) QCUSB_DbgPrintG(mask,level,_x_)
#else
#define QCUSB_DbgPrint2(mask,level,_x_)
#define QCUSB_DbgPrintX2(mask,level,_x_)
#define QCUSB_DbgPrintG2(mask,level,_x_)
#endif
#else
#define QCUSB_DbgPrint(mask,level,_x_)
#define QCUSB_DbgPrint2(mask,level,_x_)
#define QCUSB_DbgPrintX(x,mask,level,_x_)
#define QCUSB_DbgPrintX2(x,mask,level,_x_)
#define QCUSB_DbgPrintG(mask,level,_x_)
#define QCUSB_DbgPrintG2(mask,level,_x_)
#endif // QCUSB_DBGPRINT

#ifdef DBG
#ifndef DEBUG_MSGS
#define DEBUG_MSGS
#endif // !DEBUG_MSGS
#ifndef DEBUG_IRPS
#define DEBUG_IRPS
#endif
#endif // DBG

#ifdef DEBUG_MSGS
// #define DEBUG_IRPS

extern BOOLEAN bDbgout; // because Windbg does dbgout over serial line.
extern ULONG ulIrpsCompleted;

#ifdef DEBUG_IRPS
#define _IoCompleteRequest(pIrp,_b_) {_QcIoCompleteRequest(pIrp,_b_);}
#else // !DEBUG_IRPS
#define _IoCompleteRequest IoCompleteRequest
#endif // !DEBUG_IRPS

// #define _int3 { KIRQL ki3= KeGetCurrentIrql(); _asm int 3 }
#define _int3 

#undef ASSERT // when Windbg fails ASSERT(bTrue) it breaks & prints "bTrue" but doesn't point to source code
#define ASSERT(_x_) {if (!(_x_)) _int3;}

#define _dbgPrintUnicodeString(a,b) if (bDbgout) { dbgPrintUnicodeString(a,b); }

#else //DEBUG_MSGS

#ifndef _int3
#define _int3
#endif // _int3

#define _IoCompleteRequest(_a_,_b_) IoCompleteRequest(_a_,_b_)
#define _fail
#define _failx
#define _success
#define _warning
#define _dbgPrintUnicodeString(a,b)

#endif //DEBUG_MSGS

#endif // USBDBG_H
