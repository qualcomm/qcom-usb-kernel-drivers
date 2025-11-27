/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B M A I N . H

GENERAL DESCRIPTION
  This file contains major definitions for the USB driver.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBMAIN_H
#define USBMAIN_H

// --- Important compilation flags: defined in SOURCES ---
// CHECKED_SHIPPABLE
// ENABLE_LOGGING
// QCUSB_DBGPRINT
// QCUSB_DBGPRINT2
// QCUSB_TARGET_XP

#define QCUSB_MULTI_WRITES

#ifdef QCUSB_TARGET_XP
#include <../../wxp/ntddk.h>
#else
#include <ndis.h>
#endif

#ifdef NDIS_WDM
#include <ndis.h>
#endif  // NDIS_WDM

#include <usbdi.h>
#include <usbdlib.h>
#include <ntddser.h>
#include <wdmguid.h>
#include <wmilib.h>
#include <wmistr.h>
#include <ntstrsafe.h>
#include "USBCTL.h"
#include "USBIF.h"
#ifdef QCUSB_SHARE_INTERRUPT
#include "USBSHR.h"
#endif // QCUSB_SHARE_INTERRUPT

#define MAX_DEBUG_MESSAGE_LEN 256

#define QCUSB_MTU 4*1024
#define QCUSB_MAX_PKT (QCUSB_MTU+50)

#ifndef ETH_HEADER_SIZE
#define ETH_HEADER_SIZE 14
#endif
#ifndef ETH_MAX_DATA_SIZE
#define ETH_MAX_DATA_SIZE 1500
#endif
#ifndef ETH_MAX_PACKET_SIZE
#define ETH_MAX_PACKET_SIZE (ETH_HEADER_SIZE + ETH_MAX_DATA_SIZE)
#endif

#define QCUSB_MAX_MWT_BUF_COUNT      52 // do not use larger value!
#define QCUSB_MAX_MRD_BUF_COUNT      100 // do not use larger value!
#define QCDEV_DATA_HEADER_LENGTH     0
#define QCUSB_RECEIVE_BUFFER_SIZE    QCUSB_MAX_PKT  // max ethernet pkt size 1514
#define QCUSB_MRECEIVE_BUFFER_SIZE   QCUSB_MAX_PKT  // for Tin-layer-protocol (TLP)
#define QCUSB_MRECEIVE_MAX_BUFFER_SIZE (1024*32)  // for TLP
#define QCUSB_NUM_OF_LEVEL2_BUF      8
#define QCUSB_NUM_OF_LEVEL2_BUF_FOR_SS 64
#define QCUSB_NUM_OF_LEVEL2_BUF_FOR_SS_5G 800
#define QCUSB_DRIVER_GUID_DATA_STR "{87E5A6EA-D48B-4883-8440-81D8A22508D7}:DATA"
#define QCUSB_DRIVER_GUID_DIAG_STR "{87E5A6EA-D48B-4883-8440-81D8A22508D7}:DIAG"
#define QCUSB_DRIVER_GUID_UNKN_STR "{87E5A6EA-D48B-4883-8440-81D8A22508D7}:UNKNOWN"

#ifndef QCUSB_SHARE_INTERRUPT
#define DEVICE_ID_LEN 16
#define MAX_INTERFACE 0xFF
#endif  // QCUSB_SHARE_INTERRUPT
#define MAX_IO_QUEUES 32

// define an internal read buffer
#define USB_INTERNAL_READ_BUFFER_SIZE      10*4096 
#define USB_INTERNAL_128K_READ_BUFFER_SIZE 32*4096 
#define USB_INTERNAL_256K_READ_BUFFER_SIZE 64*4096 

// thread priority
#define QCUSB_INT_PRIORITY HIGH_PRIORITY  // 27
#define QCUSB_L2_PRIORITY  26
#define QCUSB_L1_PRIORITY  25
#define QCUSB_WT_PRIORITY  HIGH_PRIORITY
#define QCUSB_DSP_PRIORITY 24

#define REMOTE_WAKEUP_MASK 0x20  // bit 5
#define SELF_POWERED_MASK  0x40  // bit 6

// define number of best effort retries on error
#define BEST_RETRIES     500
#define BEST_RETRIES_MIN 6
#define BEST_RETRIES_MAX 1000

#define QCUSB_CREATE_TX_LOG   0
#define QCUSB_CREATE_RX_LOG   1

#define QCUSB_LOG_TYPE_READ               0x0
#define QCUSB_LOG_TYPE_WRITE              0x1
#define QCUSB_LOG_TYPE_RESEND             0x2
#define QCUSB_LOG_TYPE_RESPONSE_RD        0x3
#define QCUSB_LOG_TYPE_RESPONSE_WT        0x4
#define QCUSB_LOG_TYPE_OUT_OF_SEQUENCE_RD 0x5  // read
#define QCUSB_LOG_TYPE_OUT_OF_SEQUENCE_WT 0x6  // write
#define QCUSB_LOG_TYPE_OUT_OF_SEQUENCE_RS 0x7  // resent
#define QCUSB_LOG_TYPE_ADD_READ_REQ       0x8  // add read IRP
#define QCUSB_LOG_TYPE_THREAD_END         0x9
#define QCUSB_LOG_TYPE_CANCEL_THREAD      0xA

#define QCUSB_IRP_TYPE_CIRP 0x0
#define QCUSB_IRP_TYPE_RIRP 0x1
#define QCUSB_IRP_TYPE_WIRP 0x2

// define config bits
#define QCUSB_CONTINUE_ON_OVERFLOW  0x00000001L
#define QCUSB_CONTINUE_ON_DATA_ERR  0x00000002L
#define QCUSB_USE_1600_BYTE_IN_PKT  0x00000010L
#define QCUSB_USE_2048_BYTE_IN_PKT  0x00000020L
#define QCUSB_USE_4096_BYTE_IN_PKT  0x00000040L
#define QCUSB_NO_TIMEOUT_ON_CTL_REQ 0x00001000L
#define QCUSB_USE_MULTI_READS       0x00004000L
#define QCUSB_USE_MULTI_WRITES      0x00008000L
#define QCUSB_ENABLE_LOGGING        0x80000000L


#define QCUSB_NORMAL_PRIORITY   25
#define QCUSB_HIGH_PRIORITY     HIGH_PRIORITY
#define QCUSB_LOW_PRIORITY      10


typedef struct QCUSB_VENDOR_CONFIG
{
   BOOLEAN ContinueOnOverflow;
   BOOLEAN ContinueOnDataError;
   BOOLEAN Use1600ByteInPkt;
   BOOLEAN Use2048ByteInPkt;
   BOOLEAN Use4096ByteInPkt;
   BOOLEAN NoTimeoutOnCtlReq;
   BOOLEAN EnableLogging;
   BOOLEAN UseMultiReads;
   BOOLEAN UseMultiWrites;

   USHORT  MinInPktSize;
   ULONG   NumOfRetriesOnError;
   ULONG   MaxPipeXferSize;
   ULONG   NumberOfL2Buffers;
   ULONG   DebugMask;
   UCHAR   DebugLevel;
   ULONG   DriverResident;
   char    PortName[255];
   char    DriverVersion[32];
   ULONG   ThreadPriority;
} QCUSB_VENDOR_CONFIG;
extern QCUSB_VENDOR_CONFIG gVendorConfig;

typedef struct _QC_STATS
{
   LONG lOversizedReads;
   LONG lAllocatedReads;
   LONG lAllocatedWrites;
   LONG lRmlCount[8];
} QC_STATS;

#define QcRecordOversizedPkt(_x_) InterlockedIncrement(&(_x_->Sts.lOversizedReads))

#ifdef DBG
   #ifdef CHECKED_SHIPPABLE

      #undef DBG
      // #undef DEBUG_MSGS
      #undef ASSERT
      #define ASSERT

   #endif // CHECKED_SHIPPABLE
#endif // DBG

#define QC_MEM_TAG ((ULONG)'MDCQ')
#undef ExAllocatePool
#define ExAllocatePool(a,b) ExAllocatePoolWithTag(a,b,QC_MEM_TAG)

#include "USBDBG.h"

// #define USE_FAST_MUTEX
#ifdef USE_FAST_MUTEX

#define _InitializeMutex(_a_) ExInitializeFastMutex(_a_)
#define _AcquireMutex(_a_)    ExAcquireFastMutex(_a_)
#define _ReleaseMutex(_a_)    ExReleaseFastMutex(_a_)
#define _MUTEX                FAST_MUTEX
#define _PMUTEX               PFAST_MUTEX

#else  // !USE_FAST_MUTEX

#define _InitializeMutex(_a_) KeInitializeMutex(_a_,1)
#define _AcquireMutex(_a_)    KeWaitForMutexObject(_a_,Executive,KernelMode,FALSE,NULL)
#define _ReleaseMutex(_a_)    KeReleaseMutex(_a_,FALSE)
#define _MUTEX                KMUTEX
#define _PMUTEX               PKMUTEX

#endif // !USE_FAST_MUTEX

#ifdef QCUSB_TARGET_XP
   #define QcAcquireSpinLock(_lock,_levelOrHandle) KeAcquireInStackQueuedSpinLock(_lock,_levelOrHandle)
   #define QcAcquireSpinLockAtDpcLevel(_lock,_levelOrHandle) KeAcquireInStackQueuedSpinLockAtDpcLevel(_lock,_levelOrHandle)
   #define QcReleaseSpinLock(_lock,_levelOrHandle) KeReleaseInStackQueuedSpinLock(&(_levelOrHandle))
   #define QcReleaseSpinLockFromDpcLevel(_lock,_levelOrHandle) KeReleaseInStackQueuedSpinLockFromDpcLevel(&(_levelOrHandle))
#else
   #define QcAcquireSpinLock(_lock,_levelOrHandle) KeAcquireSpinLock(_lock,_levelOrHandle)
   #define QcAcquireSpinLockAtDpcLevel(_lock,_levelOrHandle) KeAcquireSpinLockAtDpcLevel(_lock)
   #define QcReleaseSpinLock(_lock,_levelOrHandle) KeReleaseSpinLock(_lock,_levelOrHandle)
   #define QcReleaseSpinLockFromDpcLevel(_lock,_levelOrHandle) KeReleaseSpinLockFromDpcLevel(_lock)

   #define QcAcquireSpinLockDbg(_lock,_levelOrHandle, info) \
           { \
              DbgPrint("AC%d: %s (0x%p)\n", KeGetCurrentIrql(), info, *_lock); \
              KeAcquireSpinLock(_lock,_levelOrHandle); \
           }
   #define QcAcquireSpinLockAtDpcLevelDbg(_lock,_levelOrHandle, info) \
           { \
              DbgPrint("AC2: %s (0x%p)\n", info, *_lock); \
              KeAcquireSpinLockAtDpcLevel(_lock); \
           }
   #define QcAcquireSpinLockWithLevelDbg(_lock,_levelOrHandle, _level, info) \
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

#endif  // QCUSB_TARGET_XP

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

#define QcAcquireDspPass(event) \
        { \
           KeWaitForSingleObject(event, Executive, KernelMode, FALSE, NULL); \
           KeClearEvent(event); \
        }
#define QcReleaseDspPass(event) \
        { \
           KeSetEvent(event, IO_NO_INCREMENT, FALSE); \
        }

#define QcAcquireEntryPass(event, tag) \
        { \
           QCUSB_DbgPrintG2 \
           ( \
              QCUSB_DBG_MASK_CONTROL, \
              QCUSB_DBG_LEVEL_FORCE, \
              ("<%s> entry pass 0\n", tag) \
           ); \
           QcAcquireDspPass(event); \
        }
#define QcReleaseEntryPass(event, tag, tag2) \
        { \
           QcReleaseDspPass(event); \
           QCUSB_DbgPrintG2 \
           ( \
              QCUSB_DBG_MASK_CONTROL, \
              QCUSB_DBG_LEVEL_FORCE, \
              ("<%s> entry pass 1 - %s\n", tag, tag2) \
           ); \
        }

#define arraysize(p) (sizeof(p)/sizeof((p)[0]))

#define initUnicodeString(out,in) { \
    (out).Buffer = (in); \
    (out).Length = sizeof( (in) ) - sizeof( WCHAR ); \
    (out).MaximumLength = sizeof( (in) ); }

#define initAnsiString(out,in) { \
    (out).Buffer = (in); \
    (out).Length = strlen( (in) ); \
    (out).MaximumLength = strlen( (in) ) + sizeof( CHAR ); }

#define initDevState(bits) {pDevExt->bmDevState = (bits);}

#define setDevState(bits) {pDevExt->bmDevState |= (bits);}

#define clearDevState(bits) {pDevExt->bmDevState &= ~(bits);}

#define inDevState(bits) ((pDevExt->bmDevState & (bits)) == (bits))

#define if_DevState(bit) \
    if ( pDevExt -> bmDevState & (bit) )

#define unless_DevState(bit) \
    if ( !(pDevExt -> bmDevState & (bit)) )

#ifdef DEBUG_MSGS
#define QcIoReleaseRemoveLock(p0, p1, p2) { \
           IoReleaseRemoveLock(p0,p1); \
           InterlockedDecrement(&(pDevExt->Sts.lRmlCount[p2])); \
           DbgPrint("RL:RML_COUNTS=%ld,%ld,%ld,%ld\n\n", \
                     pDevExt->Sts.lRmlCount[0], \
                     pDevExt->Sts.lRmlCount[1], \
                     pDevExt->Sts.lRmlCount[2], \
                     pDevExt->Sts.lRmlCount[3]);}
#define QcInterlockedIncrement(p0, p1, p2) { \
           InterlockedIncrement(&(pDevExt->Sts.lRmlCount[p0])); \
           DbgPrint("<%s> A RML-%u 0x%p\n", pDevExt->PortName, p2, p1); }
#else
#define QcIoReleaseRemoveLock(p0, p1, p2) { \
           IoReleaseRemoveLock(p0,p1); \
           InterlockedDecrement(&(pDevExt->Sts.lRmlCount[p2])); }
#define QcInterlockedIncrement(p0, p1, p2) { \
           InterlockedIncrement(&(pDevExt->Sts.lRmlCount[p0])); }
#endif

__inline VOID QC_MP_Paged_Code( VOID ) 
{
#if defined(_PREFAST_)
    __PREfastPagedCode();

#elif DBG
    if (KeGetCurrentIrql() > APC_LEVEL) {
        KdPrint(("EX: Pageable code called at IRQL %d\n", KeGetCurrentIrql()));
        PAGED_ASSERT(FALSE);
    }
#else

#endif

} 

// define modem configuration types
#define MODELTYPE_NONE     0x00
#define MODELTYPE_NET      0x01
#define MODELTYPE_NET_LIKE 0x02
#define MODELTYPE_INVALID  0xFF

//define some class commands

#define GET_PORT_STATUS    1

#define IRP_MN_QUERY_LEGACY_BUS_INFORMATION 0x18

// Commonly used event indexes
#define QC_DUMMY_EVENT_INDEX             0
#define CANCEL_EVENT_INDEX               1

// L2 read event indexes
#define L2_KICK_READ_EVENT_INDEX         2
#define L2_READ_COMPLETION_EVENT_INDEX   3
#define L2_STOP_EVENT_INDEX              4
#define L2_RESUME_EVENT_INDEX            5
#define L2_USB_READ_EVENT_INDEX          6
#define L2_READ_EVENT_COUNT              7

// L1 read event indexes
#define L1_CANCEL_EVENT_INDEX            1
#define L1_KICK_READ_EVENT_INDEX         2
#define L1_READ_COMPLETION_EVENT_INDEX   3
#define L1_READ_AVAILABLE_EVENT_INDEX    4
#define L1_READ_PURGE_EVENT_INDEX        5
#define L1_STOP_EVENT_INDEX              6
#define L1_RESUME_EVENT_INDEX            7
#define L1_READ_EVENT_COUNT              8

// Write event indexes
#define WRITE_CANCEL_CURRENT_EVENT_INDEX 2
#define KICK_WRITE_EVENT_INDEX           3
#define WRITE_COMPLETION_EVENT_INDEX     4
#define WRITE_PURGE_EVENT_INDEX          5
#define WRITE_STOP_EVENT_INDEX           6
#define WRITE_RESUME_EVENT_INDEX         7
#define WRITE_FLOW_ON_EVENT_INDEX        8
#define WRITE_FLOW_OFF_EVENT_INDEX       9
#define WRITE_EVENT_COUNT                10

// Interrupt event indexes
#define INT_COMPLETION_EVENT_INDEX       0
#define INT_STOP_SERVICE_EVENT           2
#define INT_RESUME_SERVICE_EVENT         3
#define INT_EMPTY_RD_QUEUE_EVENT_INDEX   4
#define INT_EMPTY_WT_QUEUE_EVENT_INDEX   5
#define INT_EMPTY_CTL_QUEUE_EVENT_INDEX  6
#define INT_EMPTY_SGL_QUEUE_EVENT_INDEX  7
#define INT_IDLENESS_COMPLETED_EVENT     8
#define INT_IDLE_CBK_COMPLETED_EVENT     9
#define INT_REG_IDLE_NOTIF_EVENT_INDEX   10
#define INT_IDLE_EVENT                   11
#define INT_DUMMY_EVENT_INDEX            12
#define INT_PIPE_EVENT_COUNT             13

// Dispatch event indexes
#define DSP_CANCEL_DISPATCH_EVENT_INDEX  1
#define DSP_START_DISPATCH_EVENT_INDEX   2
#define DSP_READ_CONTROL_EVENT_INDEX     3
#define DSP_PURGE_EVENT_INDEX            4
#define DSP_STANDBY_EVENT_INDEX          5
#define DSP_WAKEUP_EVENT_INDEX           6
#define DSP_WAKEUP_RESET_EVENT_INDEX     7
#define DSP_PRE_WAKEUP_EVENT_INDEX       8
#define DSP_XWDM_OPEN_EVENT_INDEX        9
#define DSP_XWDM_CLOSE_EVENT_INDEX       10
#define DSP_XWDM_QUERY_RM_EVENT_INDEX    11
#define DSP_XWDM_RM_CANCELLED_EVENT_INDEX 12
#define DSP_XWDM_QUERY_S_PWR_EVENT_INDEX 13
#define DSP_XWDM_NOTIFY_EVENT_INDEX      14
#define DSP_START_POLLING_EVENT_INDEX    15
#define DSP_STOP_POLLING_EVENT_INDEX     16
#define DSP_EVENT_COUNT                  17

// Purge Masks
#define USB_PURGE_RD 0x01
#define USB_PURGE_WT 0x02
#define USB_PURGE_CT 0x04
#define USB_PURGE_EC 0x08

// HS-USB determination
#define QC_HSUSB_VERSION         0x0200
#define QC_HSUSB_BULK_MAX_PKT_SZ 512
#define QC_HSUSB_VERSION_OK      0x01
#define QC_HSUSB_ALT_SETTING_OK  0x02
#define QC_HSUSB_BULK_MAX_PKT_OK 0x04
#define QC_SSUSB_VERSION_OK      0x08   // SS USB
#define QC_HS_USB_OK  (QC_HSUSB_VERSION_OK | QC_HSUSB_ALT_SETTING_OK)
#define QC_HS_USB_OK2 (QC_HSUSB_VERSION_OK | QC_HSUSB_BULK_MAX_PKT_OK)
#define QC_HS_USB_OK3 (QC_HSUSB_VERSION_OK |     \
                       QC_HSUSB_ALT_SETTING_OK | \
                       QC_HSUSB_BULK_MAX_PKT_OK)
    
    // SS-USB
#define QC_SSUSB_VERSION         0x0300
#define QC_SS_CTL_PKT_SZ         512                       
#define QC_SS_BLK_PKT_SZ         1024                       

typedef enum _L2BUF_STATE
{
   L2BUF_STATE_READY     = 0,
   L2BUF_STATE_PENDING   = 1,
   L2BUF_STATE_COMPLETED = 2
} L2BUF_STATE;

typedef enum _L2_STATE
{
   L2_STATE_WORKING    = 0,
   L2_STATE_STOPPING   = 1,
   L2_STATE_PURGING    = 2,
   L2_STATE_CANCELLING = 3
} L2_STATE;

typedef struct _USBMRD_L2BUFFER
{
   LIST_ENTRY List;
   LIST_ENTRY PendingList;
   NTSTATUS Status;
   PCHAR    nextIndex;
   PCHAR    Buffer;
   ULONG    Length;
   BOOLEAN  bFilled;
   PCHAR    DataPtr;

   // for multi-reads
   PVOID       DeviceExtension;
   int         Index;  // for debugging purpose
   PIRP        Irp;
   URB         Urb;
   L2BUF_STATE State;
   //KEVENT      CompletionEvt;

} USBMRD_L2BUFFER, *PUSBMRD_L2BUFFER;

#ifdef QCUSB_MULTI_WRITES
#define QCUSB_MULTI_WRITE_BUFFERS 50 // must < QCUSB_MAX_MWT_BUF_COUNT !!!
#define QCUSB_MULTI_WRITE_BUFFERS_DEFAULT 4 // must < QCUSB_MAX_MWT_BUF_COUNT !!!

typedef enum _MWT_STATE
{
   MWT_STATE_WORKING    = 0,
   MWT_STATE_STOPPING   = 1,
   MWT_STATE_PURGING    = 2,
   MWT_STATE_CANCELLING = 3,
   MWT_STATE_FLOW_OFF   = 4
} MWT_STATE;

typedef enum _MWT_BUF_STATE
{
   MWT_BUF_IDLE      = 0,
   MWT_BUF_PENDING   = 1,
   MWT_BUF_COMPLETED = 2
} MWT_BUF_STATE;

typedef struct _QCMWT_BUFFER
{
   LIST_ENTRY    List;
   PVOID         DeviceExtension;
   PIRP          Irp;
   URB           Urb;
   PIRP          CallingIrp;
   KEVENT        WtCompletionEvt;
   ULONG         Length;
   int           Index;
   MWT_BUF_STATE State;
} QCMWT_BUFFER, *PQCMWT_BUFFER;

typedef struct _QCMWT_CXLREQ
{
   LIST_ENTRY    List;
   int           Index;
} QCMWT_CXLREQ, *PQCMWT_CXLREQ;

#endif // QCUSB_MULTI_WRITES

// Definitions for USB Thin Layer Protocol (TLP)
#define QCTLP_MASK_SYNC   0xF800
#define QCTLP_MASK_LENGTH 0x07FF
#define QCTLP_BITS_SYNC   0xF800
#pragma pack(push, 1)
typedef struct _QCTLP_PKT
{
   USHORT Length;
   CHAR   Payload;
} QCTLP_PKT, *PQCTLP_PKT;
#define QCTLP_HDR_LENGTH sizeof(USHORT)
#pragma pack(pop)
typedef enum _QCTLP_BUF_STATE
{
   QCTLP_BUF_STATE_IDLE = 0,
   QCTLP_BUF_STATE_PARTIAL_FILL,
   QCTLP_BUF_STATE_PARTIAL_HDR,
   QCTLP_BUF_STATE_HDR_ONLY,
   QCTLP_BUF_STATE_ERROR
} QCTLP_BUF_STATE;
typedef struct _QCTLP_TMP_BUF
{
   PVOID  Buffer;
   USHORT PktLength;
   USHORT BytesNeeded;
} QCTLP_TMP_BUF, *PQCTLP_TMP_BUF;

typedef struct _QCUSB_IRP_RECORDS
{
   PIRP Irp;
   PVOID Param1;
   PVOID Param2;
   struct _QCUSB_IRP_RECORDS *Next;
} QCUSB_IRP_RECORDS, *PQCUSB_IRP_RECORDS;

#ifdef QCUSB_PWR
#define QCUSB_SYS_PWR_IRP_MAX 4
typedef struct _POWER_COMPLETION_CONTEXT
{
   PVOID DeviceExtension;
   PIRP  Irp;
} POWER_COMPLETION_CONTEXT, *PPOWER_COMPLETION_CONTEXT;

#define SELF_ORIGINATED_PWR_REQ_MAX 4
typedef struct _SELF_ORIGINATED_POWER_REQ
{
   LIST_ENTRY  List;
   POWER_STATE PwrState;
   int         Reserved;
} SELF_ORIGINATED_POWER_REQ, *PSELF_ORIGINATED_POWER_REQ;

typedef struct _QC_NDIS_USB_FDO_MAP
{
   LIST_ENTRY     List;
   PDEVICE_OBJECT NdisFDO;
   PVOID          MiniportContext;
} QC_NDIS_USB_FDO_MAP, *PQC_NDIS_USB_FDO_MAP;

#endif // QCUSB_PWR

#ifdef QC_IP_MODE

#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_IPV6 0x86DD

#define uint16 USHORT
#undef ntohs
#define ntohs(x) \
        ((uint16)((((uint16)(x) & 0x00ff) << 8) | \
        (((uint16)(x) & 0xff00) >> 8)))

#pragma pack(push, 1)
typedef struct _QCUSB_ETH_HDR
{
    UCHAR DstMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR SrcMacAddress[ETH_LENGTH_OF_ADDRESS];
    USHORT EtherType;
} QCUSB_ETH_HDR, *PQCUSB_ETH_HDR;
#pragma pack(pop)
#endif // QC_IP_MODE

#if defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_MBIM_DL_SUPPORT)

#pragma pack(push, 1)

typedef struct _QCDATAGRAM_STRUCT
{
   union {
      ULONG Datagram;
      struct {
         USHORT DatagramPtr;
         USHORT DatagramLen;
      };
   };
} QCDATAGRAM_STRUCT, *PQCDATAGRAM_STRUCT;

typedef struct _QCNTB_16BIT_HEADER
{
   ULONG  dwSignature;
   USHORT wHeaderLength;
   USHORT wSequence;
   USHORT wBlockLength;
   USHORT wNdpIndex;
} QCNTB_16BIT_HEADER, *PQCNTB_16BIT_HEADER;

typedef struct _QCNTB_32BIT_HEADER
{
   ULONG  dwSignature;
   USHORT wHeaderLength;
   USHORT wSequence;
   ULONG  wBlockLength;
   ULONG  wNdpIndex;
} QCNTB_32BIT_HEADER, *PQCNTB_32BIT_HEADER;

typedef struct _QCNDP_16BIT_HEADER
{
   ULONG  dwSignature;
   USHORT wLength;
   //USHORT wReserved;
   USHORT wNextNdpIndex;
} QCNDP_16BIT_HEADER, *PQCNDP_16BIT_HEADER;

typedef struct _QCNDP_32BIT_HEADER
{
   ULONG  dwSignature;
   USHORT wLength;
   USHORT wReserved6;
   ULONG  wNextNdpIndex;
   ULONG  wReserved12;
   ULONG  wDatagramIndex;
   ULONG  wDatagramLength;
} QCNDP_32BIT_HEADER, *PQCNDP_32BIT_HEADER;

#pragma pack(pop)

#endif

#if defined(QCMP_QMAP_V1_SUPPORT) || defined(QCMP_QMAP_V2_SUPPORT)

#define QMAP_FLOWCONTROL_COMMAND 1
#define QMAP_FLOWCONTROL_COMMAND_PAUSE  1
#define QMAP_FLOWCONTROL_COMMAND_RESUME 2

#pragma pack(push, 1)

typedef struct _QCQMAP_STRUCT
{
   union {
      UCHAR PadCD;
      struct {
         UCHAR  PadLen:6;
         UCHAR  ReservedBit:1;
         UCHAR  CDBit:1;
      };
   };
   UCHAR MuxId;
   USHORT PacketLen;
} QCQMAP_STRUCT, *PQCQMAP_STRUCT;

typedef struct _QCQMAP_UL_CHECKSUM
{
   USHORT CksumStartOffset;
   union {
         USHORT CksumInsertOffsetCU;
      struct {
         USHORT  CksumInsertOffset:14;
         USHORT  UDPIP4Ind:1;
         USHORT  CkEnable:1;
      };
   };
} QCQMAP_UL_CHECKSUM, *PQCQMAP_UL_CHECKSUM;

typedef struct _QCQMAP_DL_CHECKSUM
{
   union {
      USHORT RsvdV;
      struct {
         USHORT  Reserved:15;
         USHORT  Valid:1;
      };
   };
   USHORT CksumStartOffset;
   USHORT CksumLength;
   USHORT CksumValue;
} QCQMAP_DL_CHECKSUM, *PQCQMAP_DL_CHECKSUM;

typedef struct _QCQMAP_FLOWCONTROL_STRUCT
{
   UCHAR CommandName;
   union {
      UCHAR RsvdCmdType;
      struct {
         UCHAR CmdType:2;
         UCHAR Reserved:6;
      };
   };
   USHORT Rsvd;
   ULONG  TransactionId;
   union {
      ULONG FlowCtrlSeqNumIp;
      struct {
         ULONG FlowControlSeqNum:16;
         ULONG IpFamily:2;
         ULONG RsvdSeqNo:14;
      };
   };
   ULONG  QOSId;
} QCQMAP_FLOWCONTROL_STRUCT, *PQCQMAP_FLOWCONTROL_STRUCT;

#pragma pack(pop)

#endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)

#pragma pack(push, 1)

typedef struct _QCQMAP_PSEUDO_CHECKSUM
{
   ULONG SrcAddr;
   ULONG DestAddr;
   UCHAR Zeros;
   UCHAR Protocol;
   USHORT Length;
} QCQMAP_PSEUDO_CHECKSUM, *PQCQMAP_PSEUDO_CHECKSUM;

typedef struct _QCQMAP_PSEUDO_CHECKSUMV6
{
    UCHAR SrcAddr[16];
    UCHAR DestAddr[16];   
    ULONG payloadLen;
    UCHAR Zeros[3];
    UCHAR NextHdr;
} QCQMAP_PSEUDO_CHECKSUMV6, *PQCQMAP_PSEUDO_CHECKSUMV6;

typedef struct _QCIPV4_FRAG_PKT
{
   union {
   USHORT Rsvd2;
   struct {
   USHORT Offset:13;
   USHORT Flags:3;
   };
   };
}QCIPV4_FRAG_PKT, *QCPIPV4_FRAG_PKT;

typedef struct _QCQMAP_IPV4
{
   USHORT version;
   USHORT TotaLlen;
   USHORT Rsvd1;
   union {
   USHORT Rsvd2;
   struct {
      USHORT Offset:13;
      USHORT Flags:3;
   };
   };
   UCHAR TTL;
   UCHAR Protocol;
   USHORT HeaderChk;
   ULONG SrcAddr;
   ULONG DestAddr;   
} QCQMAP_IPV4, *PQCQMAP_IPV4;

typedef struct _QCQMAP_IPV6
{
   ULONG Rsvd;
   USHORT payloadLen;
   UCHAR nextHdr;
   UCHAR HopLimit;
   UCHAR SrcAddr[16];
   UCHAR DestAddr[16];   
} QCQMAP_IPV6, *PQCQMAP_IPV6;

typedef struct _QCQMAP_TCP
{
   USHORT SrcPort;
   USHORT DestPort;
   ULONG Sequence;
   ULONG Ack;
   ULONG Rsvd;
   USHORT ChkSum;
} QCQMAP_TCP, *PQCQMAP_TCP;

typedef struct _QCQMAP_UDP
{
   USHORT SrcPort;
   USHORT DestPort;
   USHORT Len;
   USHORT ChkSum;
} QCQMAP_UDP, *PQCQMAP_UDP;

typedef struct _QCQMAP_ICMP
{
   UCHAR Type;
   UCHAR Code;
   USHORT ChkSum;
} QCQMAP_ICMP, *PQCQMAP_ICMP;

#pragma pack(pop)

#endif
#endif


typedef struct _MUX_INTERFACE_INFO
{
    UCHAR InterfaceNumber;
    UCHAR PhysicalInterfaceNumber;
    ULONG MuxEnabled;
    PVOID FilterDeviceObj;
}MUX_INTERFACE_INFO,* PMUX_INTERFACE_INFO;

typedef struct _DEVICE_EXTENSION
{
   PDRIVER_OBJECT MyDriverObject;
   PDEVICE_OBJECT PhysicalDeviceObject;   // physical device object
   PDEVICE_OBJECT StackDeviceObject;      // stack device object
   PDEVICE_OBJECT MyDeviceObject;
   PDEVICE_OBJECT MiniportFDO;
   PVOID          MiniportContext;

   IO_REMOVE_LOCK RemoveLock;           // removal control
   PIO_REMOVE_LOCK pRemoveLock;         // removal control

   // descriptors for device instance
   PUSB_DEVICE_DESCRIPTOR pUsbDevDesc;   // ptr since there is only 1 dev desc
   PUSB_CONFIGURATION_DESCRIPTOR pUsbConfigDesc;
   PUSBD_INTERFACE_INFORMATION Interface[MAX_INTERFACE];
   PURB UsbConfigUrb;
   BOOLEAN IsEcmModel;
   USHORT usCommClassInterface;
   USBD_CONFIGURATION_HANDLE ConfigurationHandle;
   CHAR DevSerialNumber[256];  // to hold USB_STRING_DESCRIPTOR of the serial number
   ULONG IfProtocol;
   _MUTEX muPnPMutex;
   BOOLEAN bInService;                  //set in create, cleared in close

   USHORT PurgeMask;

   LIST_ENTRY RdCompletionQueue;
   LIST_ENTRY WtCompletionQueue;
   LIST_ENTRY CtlCompletionQueue;
   LIST_ENTRY SglCompletionQueue;

   LIST_ENTRY ReadDataQueue;
   LIST_ENTRY WriteDataQueue;
   LIST_ENTRY EncapsulatedDataQueue;
   LIST_ENTRY DispatchQueue;
   PIRP pCurrentDispatch;

   PCHAR pInterruptBuffer;
   USHORT wMaxPktSize;
   UCHAR BulkPipeOutput;
   UCHAR BulkPipeInput;
   UCHAR InterruptPipe;
   UCHAR ControlInterface; // contains the Interrupt Pipe
   UCHAR DataInterface; // contains the Bulk Pipes; == ControlInterface == 0 if not CDC

   PIRP PurgeIrp;
   PIRP pNotificationIrp;
   KEVENT ForTimeoutEvent;
   UCHAR  ucModelType;    // MODELTYPE_NET

   KSPIN_LOCK ControlSpinLock;
   KSPIN_LOCK ReadSpinLock;
   KSPIN_LOCK WriteSpinLock;
   KSPIN_LOCK SingleIrpSpinLock;
   USHORT bmDevState;

   UNICODE_STRING ucsDeviceMapEntry;
   UNICODE_STRING ucsPortName;
   char                 PortName[16];

   USHORT idVendor;
   USHORT idProduct;
   BOOLEAN bPowerManagement; // true = enabled, false = disabled
   LONG lReadBufferSize;
   LONG lReadBuffer20pct;
   LONG lReadBuffer50pct;
   LONG lReadBuffer80pct;
   LONG lReadBufferHigh;
   LONG lReadBufferLow;
   PUCHAR pucReadBufferStart;
   PUCHAR pucReadBufferGet;
   PUCHAR pucReadBufferPut;

   //Bus drivers set the appropriate values in this structure in response
   //to an IRP_MN_QUERY_CAPABILITIES IRP. Function and filter drivers might
   //alter the capabilities set by the bus driver.
   DEVICE_CAPABILITIES DeviceCapabilities;

   // default power state to power down to on self-suspend 
   // ULONG PowerDownLevel; 

   #ifdef QCUSB_PWR
   BOOLEAN            PowerSuspended;
   BOOLEAN            SelectiveSuspended;
   SYSTEM_POWER_STATE SystemPower;
   DEVICE_POWER_STATE DevicePower;
   BOOLEAN            PMWmiRegistered;
   BOOLEAN            bRemoteWakeupEnabled;  // device side
   BOOLEAN            bDeviceSelfPowered;    // device side
   char               bmAttributes;    // device side
   BOOLEAN            PowerManagementEnabled;
   BOOLEAN            WaitWakeEnabled;       // host side
   PIRP               WaitWakeIrp;
   ULONG              PowerDownLevel;
   UCHAR              IoBusyMask;
   BOOLEAN            IdleTimerLaunched;
   KTIMER             IdleTimer;
   KDPC               IdleDpc;
   PIRP               IdleNotificationIrp;
   USHORT             WdmVersion;
   ULONG              SelectiveSuspendIdleTime;
   BOOLEAN            SelectiveSuspendInMili;   
   BOOLEAN            InServiceSelectiveSuspension;
   WMILIB_CONTEXT     WmiLibInfo;
   BOOLEAN            PrepareToPowerDown;
   POWER_COMPLETION_CONTEXT PwrCompContext[QCUSB_SYS_PWR_IRP_MAX];
   LONG               PwrCtextIdx;

   KEVENT             InterruptRegIdleEvent;
   KEVENT             RegIdleAckEvent;

   LIST_ENTRY         IdleIrpCompletionStack;
   KEVENT             InterruptIdleEvent;
   KEVENT             InterruptIdlenessCompletedEvent;
   KEVENT             DispatchPreWakeupEvent;
   LIST_ENTRY         OriginatedPwrReqQueue;
   LIST_ENTRY         OriginatedPwrReqPool;
   SELF_ORIGINATED_POWER_REQ SelfPwrReq[SELF_ORIGINATED_PWR_REQ_MAX];
   #endif

   KEVENT DSPSyncEvent;
   // Start of WDM_VxD.c support structures (service threads)
   PKEVENT pL2ReadEvents[L2_READ_EVENT_COUNT];  // array of events which alert the L2 read thread
   PKEVENT pL1ReadEvents[L1_READ_EVENT_COUNT];  // array of events which alert the L1 read thread
   PKEVENT pWriteEvents[WRITE_EVENT_COUNT+QCUSB_MAX_MWT_BUF_COUNT];  // array of events which alert the write thread
   PKEVENT pInterruptPipeEvents[INT_PIPE_EVENT_COUNT];
   PKEVENT pDispatchEvents[DSP_EVENT_COUNT];

   PKEVENT pReadControlEvent;
   PLONG   pRspAvailableCount;
   LONG   lRspAvailableCount;
   KEVENT DispatchReadControlEvent;

   KEVENT L1ReadThreadClosedEvent;
   KEVENT L2ReadThreadClosedEvent;
   KEVENT ReadIrpPurgedEvent;
   KEVENT WriteThreadClosedEvent;
   KEVENT InterruptPipeClosedEvent;
   KEVENT InterruptStopServiceEvent;
   KEVENT InterruptStopServiceRspEvent;
   KEVENT InterruptResumeServiceEvent;
   KEVENT InterruptEmptyRdQueueEvent;
   KEVENT InterruptEmptyWtQueueEvent;
   KEVENT InterruptEmptyCtlQueueEvent;
   KEVENT InterruptEmptySglQueueEvent;
   KEVENT CancelReadEvent;
   KEVENT L1CancelReadEvent;
   KEVENT CancelWriteEvent;
   KEVENT CancelInterruptPipeEvent;
   KEVENT WriteCompletionEvent;
   KEVENT L2ReadCompletionEvent;
   KEVENT L1ReadCompletionEvent;
   KEVENT L1ReadAvailableEvent;
   KEVENT L1ReadPurgeAckEvent;
   KEVENT KickWriteEvent;
   KEVENT L2KickReadEvent;
   KEVENT L1KickReadEvent;
   KEVENT L1ReadPurgeEvent;
   KEVENT WritePurgeEvent;
   KEVENT ReadThreadStartedEvent;
   KEVENT L2ReadThreadStartedEvent;
   KEVENT WriteThreadStartedEvent;
   KEVENT DspThreadStartedEvent;
   KEVENT IntThreadStartedEvent;
   KEVENT eInterruptCompletion;
   KEVENT DispatchStartEvent;
   KEVENT DispatchCancelEvent;
   KEVENT DispatchThreadClosedEvent;
   KEVENT DispatchPurgeEvent;
   KEVENT DispatchPurgeCompleteEvent;
   KEVENT DispatchStartPollingEvent;
   KEVENT DispatchStopPollingEvent;
   KEVENT DispatchStandbyEvent;
   KEVENT DispatchWakeupEvent;
   KEVENT DispatchWakeupResetEvent;
   KEVENT WriteCancelCurrentEvent;
   KEVENT WriteFlowOnEvent;
   KEVENT WriteFlowOffEvent;
   KEVENT WriteFlowOffAckEvent;
   LIST_ENTRY MWTSentIrpQueue;
   LIST_ENTRY MWTSentIrpRecordPool;
   LONG   MWTPendingCnt;

   KEVENT L1StopEvent;
   KEVENT L1StopAckEvent;
   KEVENT L1ResumeEvent;
   KEVENT L1ResumeAckEvent;
   KEVENT L2StopEvent;
   KEVENT L2StopAckEvent;
   KEVENT L2ResumeEvent;
   KEVENT L2ResumeAckEvent;
   KEVENT WriteStopEvent;
   KEVENT WriteResumeEvent;
   KEVENT L2USBReadCompEvent;

   BOOLEAN bDeviceRemoved;
   BOOLEAN bDeviceSurpriseRemoved;

   NTSTATUS ControlPipeStatus;
   NTSTATUS IntPipeStatus;
   NTSTATUS InputPipeStatus;
   NTSTATUS OutputPipeStatus;
   NTSTATUS XwdmStatus;
   HANDLE hInterruptThreadHandle;
   HANDLE hL1ReadThreadHandle;
   HANDLE hL2ReadThreadHandle;
   HANDLE hWriteThreadHandle;
   HANDLE hDispatchThreadHandle;
   PKTHREAD pInterruptThread;  // typedef struct _KTHREAD *PKTHREAD;
   PKTHREAD pL1ReadThread;
   PKTHREAD pL2ReadThread;
   PKTHREAD pWriteThread;
   PKTHREAD pDispatchThread;
   ULONG    DispatchThreadCancelStarted;
   ULONG    InterruptThreadCancelStarted;
   ULONG    ReadThreadCancelStarted;
   ULONG    ReadThreadInCreation;
   ULONG    WriteThreadCancelStarted;
   ULONG    WriteThreadInCreation;

   HANDLE hTxLogFile;
   HANDLE hRxLogFile;
   UNICODE_STRING ucLoggingDir;
   BOOLEAN        bLoggingOk;
   ULONG          ulRxLogCount;
   ULONG          ulLastRxLogCount;
   ULONG          ulTxLogCount;
   ULONG          ulLastTxLogCount;

   PIRP pWriteCurrent;

   BOOLEAN bL1ReadActive;
   BOOLEAN bL2ReadActive;
   BOOLEAN bWriteActive;
   BOOLEAN bReadBufferReset;
   BOOLEAN bL1InCancellation;
   BOOLEAN bL1Stopped;
   BOOLEAN bL2Stopped;
   BOOLEAN bL2Polling;
   BOOLEAN bWriteStopped;
   BOOLEAN bEncStopped;
   BOOLEAN bIntActive;
   PIRP    InterruptIrp;
   LONG    IdleCallbackInProgress;
   KEVENT  IdleCbkCompleteEvent;
   BOOLEAN bIdleIrpCompleted;
   BOOLEAN bLowPowerMode;

   USBMRD_L2BUFFER *pL2ReadBuffer;

   // for multi-reads
   LIST_ENTRY L2CompletionQueue;
   LIST_ENTRY L2PendingQueue;
   KSPIN_LOCK L2Lock;
   LONG       L2IrpStartIdx;
   LONG       L2IrpEndIdx;

   LONG L2FillIdx;
   LONG L2IrpIdx;

   #ifdef QCUSB_MULTI_WRITES
   BOOLEAN UseMultiWrites;
   PQCMWT_BUFFER pMwtBuffer[QCUSB_MAX_MWT_BUF_COUNT];
   QCMWT_CXLREQ  CxlRequest[QCUSB_MAX_MWT_BUF_COUNT];
   ULONG      NumberOfMultiWrites;
   LIST_ENTRY MWriteIdleQueue;
   LIST_ENTRY MWritePendingQueue;
   LIST_ENTRY MWriteCompletionQueue;
   LIST_ENTRY MWriteCancellingQueue;
   KEVENT     WriteStopAckEvent;
   KEVENT     WritePurgeAckEvent;
   #endif // QCUSB_MULTI_WRITES

   PQCUSB_IRP_RECORDS WriteCancelRecords;

   BOOLEAN bVendorFeature;
   BOOLEAN bFdoReused;

   // device configuration parameters
   BOOLEAN ContinueOnOverflow;
   BOOLEAN ContinueOnDataError;
   BOOLEAN EnableLogging;
   BOOLEAN UseMultiReads;
   BOOLEAN NoTimeoutOnCtlReq;
   USHORT  MinInPktSize;
   ULONG   NumOfRetriesOnError;
   ULONG   MaxPipeXferSize;
   ULONG   NumberOfL2Buffers;

   // pending Read IRPs
   LONG    NumberOfPendingReadIRPs;
   LONG    NumberOfQueuedReadIRPs;

   ULONG   DebugMask;
   UCHAR   DebugLevel;

   QC_STATS Sts;
   QC_XFER_STATISTICS QcXferStats;

   QCUSB_ENTRY_POINTS EntryPoints;  // for direct MJ function calls

   #ifdef NDIS_WDM
   NDIS_HANDLE NdisConfigurationContext;
   #endif

   DEVICE_POWER_STATE  PowerState;
   UCHAR DeviceControlCapabilities;
   UCHAR DeviceDataCapabilities;
   BOOLEAN SetCommFeatureSupported;
   BOOLEAN ExWdmMonitorStarted;
   PVOID   ExWdmIfNotificationEntry;
   KEVENT DispatchXwdmOpenEvent;
   KEVENT DispatchXwdmCloseEvent;
   KEVENT DispatchXwdmQueryRmEvent;
   KEVENT DispatchXwdmReopenEvent;
   KEVENT DispatchXwdmQuerySysPwrEvent;
   KEVENT DispatchXwdmQuerySysPwrAckEvent;
   KEVENT DispatchXwdmNotifyEvent;
   PDEVICE_OBJECT ExWdmDeviceObject;
   PFILE_OBJECT   ExFileObject;
   PVOID          ExWdmDevNotificationEntry;
   UNICODE_STRING ExWdmDeviceSymbName;
   PIRP           XwdmNotificationIrp;
   KSPIN_LOCK     ExWdmSpinLock;
   BOOLEAN        ExWdmInService;
 
   #ifdef QCUSB_SHARE_INTERRUPT
   #ifdef QCUSB_SHARE_INTERRUPT_OLD
   UCHAR DeviceId[DEVICE_ID_LEN];
   #else
   PDEVICE_OBJECT DeviceId;
   #endif
   #endif // QCUSB_SHARE_INTERRUPT
   BOOLEAN bEnableResourceSharing;
   BOOLEAN bEnableDataBundling[2];
   QCTLP_TMP_BUF TlpFrameBuffer;
   QCTLP_BUF_STATE TlpBufferState;   

   UCHAR HighSpeedUsbOk;

   #ifdef QC_IP_MODE
   BOOLEAN IPOnlyMode;
   BOOLEAN QoSMode;
   QCUSB_ETH_HDR EthHdr; // for inbound packets
   #endif // QC_IP_MODE

   BOOLEAN WWANMode;

#if defined(QCMP_MBIM_UL_SUPPORT)
   BOOLEAN  MBIMULEnabled;   // host-device negotiation
#endif

#if defined(QCMP_MBIM_DL_SUPPORT)
   BOOLEAN  MBIMDLEnabled;   // host-device negotiation
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
   BOOLEAN QMAPEnabledV4;
#endif
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   BOOLEAN QMAPEnabledV2;
   BOOLEAN QMAPEnabledV3;
#endif
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
   BOOLEAN QMAPEnabledV1;
#endif

   // to support multiple physical devices
   LONG DispatchThreadInCancellation;
   LONG InterruptThreadInCancellation;
   LONG L1ReadThreadInCancellation;
   LONG WriteThreadInCancellation;
   LONG RdErrorCount;
   LONG WtErrorCount;
   LONG TLPCount;
   BOOLEAN RdThreadInCreation;
   BOOLEAN WtThreadInCreation;

   #ifdef QCUSB_MUX_PROTOCOL
   BOOLEAN MuxEnabled;
   ULONG   MuxCtrlDLCI;
   ULONG   MuxDataDLCI;
   #endif // QCUSB_MUX_PROTOCOL

   LONG QosSetting;

   LONG MTU;

   MUX_INTERFACE_INFO MuxInterface;
   ULONG FrameDropCount;
   ULONGLONG FrameDropBytes;
   LONG FlowControlEnabledCount;
   ULONG QMAPDLMinPadding;   

   #ifdef QCUSB_MUX_PROTOCOL
   // BURST_SUPPORT
   LONG   BurstFashion;  // EQUAL_INTERVAL=0; EQUAL_GAP=1
   KTIMER BurstTimer;
   KDPC   BurstDpc;
   ULONG  BurstInterval;  // ms
   ULONG  BurstSize;
   ULONG  BurstPendingCount;
   ULONG  BurstCount;
   BOOLEAN BurstTimerLaunched;
   #endif // QCUSB_MUX_PROTOCOL

   ULONG EnableData5G;
}  DEVICE_EXTENSION, *PDEVICE_EXTENSION;

// device states

#define DEVICE_STATE_ZERO             0x0000
#define DEVICE_STATE_PRESENT          0x0001 // refer to PDO
#define DEVICE_STATE_DEVICE_ADDED     0x0002
#define DEVICE_STATE_DEVICE_STARTED   0x0004
#define DEVICE_STATE_DEVICE_STOPPED   0x0008
#define DEVICE_STATE_SURPRISE_REMOVED 0x0010
#define DEVICE_STATE_CLIENT_PRESENT   0x0040 // refer to upper level
#define DEVICE_STATE_DELETE_DEVICE    0x0080
#define DEVICE_STATE_DEVICE_REMOVED0  0x0100 // removal start
#define DEVICE_STATE_DEVICE_REMOVED1  0x0200 // removal end
#define DEVICE_STATE_PRESENT_AND_STARTED (DEVICE_STATE_PRESENT|DEVICE_STATE_DEVICE_STARTED)

extern char    gDeviceName[255];
extern BOOLEAN bPowerManagement;
extern int     gModemType;
extern KEVENT  gSyncEntryEvent;

extern BOOLEAN bAddNewDevice;
extern UNICODE_STRING gServicePath;
extern KSPIN_LOCK gGenSpinLock;
extern char gServiceName[255];

#define USB_CDC_INT_RX_CARRIER   0x01   // bit 0
#define USB_CDC_INT_TX_CARRIER   0x02   // bit 1
#define USB_CDC_INT_BREAK        0x04   // bit 2
#define USB_CDC_INT_RING         0x08   // bit 3
#define USB_CDC_INT_FRAME_ERROR  0x10   // bit 4
#define USB_CDC_INT_PARITY_ERROR 0x20   // bit 5
#define USB_CDC_INT_OVERRUN      0x30   // bit 6

#define MAX_DEVICE_ID_LEN 1024 //128
#define MAX_ENTRY_NAME_LEN 1024 //64
#define MAX_ENTRY_DATA_LEN 2048 //256 11/14/98
#define MAX_ENTRY_LARGE_LEN 1024 //512
#define MAX_USB_KEY_LEN 2048
#define MAX_NAME_LEN 1024 //128
#define MAX_KEY_NAME_LEN 1024 //256
#define DEVICE_PATH_SIZE 1024 //255

// for compatibility
#ifndef USBD_STATUS_XACT_ERROR
#define USBD_STATUS_XACT_ERROR   ((USBD_STATUS)0xC0000011L)
#endif
#ifndef USBD_STATUS_DEVICE_GONE
#define USBD_STATUS_DEVICE_GONE ((USBD_STATUS)0xC0007000L)
#endif

#ifdef NDIS_WDM
// #define QcIoDeleteDevice(_d_) {ExFreePool(_d_->DeviceExtension);ExFreePool(_d_);_d_=NULL;}
#define QcIoDeleteDevice(_d_) {IoDeleteDevice(_d_);_d_=NULL;}
#else
#define QcIoDeleteDevice(_d_) IoDeleteDevice(_d_);
#endif

// Function Prototypes

#define _IoMarkIrpPending(_pIrp_) {_pIrp_->IoStatus.Status = STATUS_PENDING; IoMarkIrpPending(_pIrp_);}

VOID USBMAIN_CleanupDeviceExtension
(
    PDEVICE_OBJECT DeviceObject
);

VOID USBMAIN_NotificationIrpCancelRoutine
(
    PDEVICE_OBJECT DriverObject,
    PIRP pIrp
);

VOID USBMAIN_DriverUnload
(
    PDRIVER_OBJECT DriverObject
);

VOID USBMAIN_CancalIoThreads
(
    PDEVICE_OBJECT DeviceObject
);

VOID USBMAIN_CancalNotificationIrp
(
    PDEVICE_OBJECT DeviceObject
);

VOID USBMAIN_DispatchDebugOutput
(
   PIRP               Irp,
   PIO_STACK_LOCATION IrpStack,
   PDEVICE_OBJECT     DeviceObject,
   KIRQL              Irql
);

VOID USBMAIN_ResetRspAvailableCount
(
   PDEVICE_EXTENSION pDevExt,
   USHORT Interface,
   char *info,
   UCHAR cookie
);
NTSTATUS USBMAIN_StopDataThreads(PDEVICE_EXTENSION pDevExt, BOOLEAN CancelWaitWake);

VOID USBMAIN_UpdateXferStats(PDEVICE_EXTENSION pDevExt, ULONG PktLen, BOOLEAN IsRx);

#endif // USBMAIN_H
