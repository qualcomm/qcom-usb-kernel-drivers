/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                              M P M A I N . H

GENERAL DESCRIPTION
  This module contains structure definitons and function prototypes.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef _MPMAIN_H
#define _MPMAIN_H

#include <ndis.h>
#include <ntstrsafe.h>
#ifdef NDIS620_MINIPORT
#include <windef.h>
#include <wwan.h>
#include <ndiswwan.h>
#endif //NDIS620_MINIPORT
#include "MPParam.h"
#include "MPQMI.h"
#include "USBIF.h"
// #include "MPUSB.h"

/*************** Driver Entry Points ******************/
extern PDRIVER_DISPATCH QCDriverDispatchTable[IRP_MJ_MAXIMUM_FUNCTION + 1];
extern LIST_ENTRY MPUSB_FdoMap;
/*************** Driver Entry Points ******************/

#if defined(NDIS50_MINIPORT)
   #define MP_NDIS_MAJOR_VERSION       5
   #define MP_NDIS_MINOR_VERSION       0
#elif defined(NDIS51_MINIPORT)
   #define MP_NDIS_MAJOR_VERSION       5
   #define MP_NDIS_MINOR_VERSION       1
#elif defined(NDIS60_MINIPORT)
   #define MP_NDIS_MAJOR_VERSION       6
   #define MP_NDIS_MINOR_VERSION       0
   #define MP_NDIS_MAJOR_VERSION620    6
   #define MP_NDIS_MINOR_VERSION620    20
   #define MP_NDIS_MAJOR_VERSION630    6
   #define MP_NDIS_MINOR_VERSION630    30
   #define MP_MAJOR_DRIVER_VERSION     2
   #define MP_MINOR_DRIVER_VERSION     0
#else
#endif

#ifdef NDIS620_MINIPORT
#define SPOOF_PS_ONLY_DETACH 1
#endif

#define QCDEV_MAX_MSISDN_STR_SIZE      24 //16
#define QCDEV_MAX_SER_STR_SIZE         24 //16

#define RESET_TIMEOUT 25
typedef enum _RST_STATE {
      RST_STATE_START_CLEANUP,
      RST_STATE_COMPLETE,
      RST_STATE_WAIT_USB,
      RST_STATE_TIMEOUT,
      RST_STATE_QUEUES_EMPTY,
      RST_STATE_WAIT_FOR_EMPTY
} RST_STATE;

typedef enum _RECONF_TIMER_STATE
{
   RECONF_TIMER_IDLE,
   RECONF_TIMER_ARMED
} RECONF_TIMER_STATE;

#define     QC_DATA_MTU_DEFAULT        1500
#define     QC_DATA_MTU_MAX            10*1024
#ifndef ETH_HEADER_SIZE
#define     ETH_HEADER_SIZE            14
#endif
#ifndef ETH_MAX_DATA_SIZE
#define     ETH_MAX_DATA_SIZE          QC_DATA_MTU_DEFAULT
#endif
#ifndef ETH_MAX_PACKET_SIZE
#define     ETH_MAX_PACKET_SIZE        (ETH_HEADER_SIZE + ETH_MAX_DATA_SIZE)
#endif
#define     ETH_MIN_PACKET_SIZE        60

typedef struct _QCMP_OID_COPY
{
   NDIS_REQUEST_TYPE RequestType;
   PVOID             RequestId;
   NDIS_HANDLE       RequestHandle;

   union _QCMP_REQUEST_DATA
   {
       struct _QCMP_QUERY
       {
           NDIS_OID  Oid;
           PVOID     InformationBuffer;
           UINT      InformationBufferLength;
           UINT      BytesWritten;
           UINT      BytesNeeded;
       } QUERY_INFORMATION;

       struct _QCMP_SET
       {
           NDIS_OID  Oid;
           PVOID     InformationBuffer;
           UINT      InformationBufferLength;
           UINT      BytesRead;
           UINT      BytesNeeded;
       } SET_INFORMATION;

       struct _QCMP_METHOD
       {
           NDIS_OID  Oid;
           PVOID     InformationBuffer;
           ULONG     InputBufferLength;
           ULONG     OutputBufferLength;
           ULONG     MethodId;
           UINT      BytesWritten;
           UINT      BytesRead;
           UINT      BytesNeeded;
       } METHOD_INFORMATION;
   } DATA;
} QCMP_OID_COPY, *PQCMP_OID_COPY;

#ifdef NDIS60_MINIPORT
#define MP_MEDIA_MAX_SPEED            400000000

typedef enum _QNBL_TX_STATUS
{
   QNBL_STATUS_PENDING = 0,
   QNBL_STATUS_COMPLETE,
   QNBL_STATUS_ABORT,
} QNBL_TX_STATUS;

typedef struct _MPUSB_TX_CONTEXT_NBL
{
   LIST_ENTRY     List; 
   PVOID          Adapter;  // miniport adapter
   PVOID          NBL;      // net buffer list
   int            TotalNBs; // total number of net buffers
   int            NumProcessed;
   int            NumCompleted;
   int            TxBytesSucceeded;
   int            TxBytesFailed;
   int            TxFramesFailed;
   int            TxFramesDropped;
   int            AbortFlag;
   int            Canceld;
   NDIS_STATUS    NdisStatus;
} MPUSB_TX_CONTEXT_NBL, *PMPUSB_TX_CONTEXT_NBL;

typedef struct _MPUSB_TX_CONTEXT_NB
{
   LIST_ENTRY       List; 
   PIRP  Irp;   // IRP, for info only
   PVOID NB;    // net buffer, for info only
   PVOID NBL;   // the owning NBL
   int   Index; // net bufer index
   int   DataLength;
   int   Completed;
} MPUSB_TX_CONTEXT_NB, *PMPUSB_TX_CONTEXT_NB;

#define QCMP_MAX_DATA_PKT_SIZE QC_DATA_MTU_MAX

typedef struct _MPUSB_RX_NBL
{
   LIST_ENTRY       List;
   PMDL             RxMdl;
   PNET_BUFFER_LIST RxNBL;
   PIRP             Irp;
   int              Index; // for debugging
   PVOID            BvaPtr;
   UCHAR            BVA[QCMP_MAX_DATA_PKT_SIZE];   // base virtual address
   // NDIS_NET_BUFFER_LIST_8021Q_INFO OOB;
} MPUSB_RX_NBL, *PMPUSB_RX_NBL;

#else

// for backward compatibility
typedef enum _NDIS_HALT_ACTION
{
    NdisHaltDeviceDisabled,
    NdisHaltDeviceInstanceDeInitialized,
    NdisHaltDevicePoweredDown,
    NdisHaltDeviceSurpriseRemoved,
    NdisHaltDeviceFailed,
    NdisHaltDeviceInitializationFailed,
    NdisHaltDeviceStopped
} NDIS_HALT_ACTION, *PNDIS_HALT_ACTION;

typedef enum _NDIS_SHUTDOWN_ACTION {
    NdisShutdownPowerOff,
    NdisShutdownBugCheck
} NDIS_SHUTDOWN_ACTION, PNDIS_SHUTDOWN_ACTION;

#endif // NDIS60_MINIPORT

#define QUALCOMM_TAG                   ((ULONG)'MOCQ')
#define QUALCOMM_MAC_0                 0x00
#define QUALCOMM_MAC_1                 0xA0
#define QUALCOMM_MAC_2                 0xC6
#define QUALCOMM_MAC_3_HOST_BYTE       0x00

#define QUALCOMM_MAC2_0                0x02
#define QUALCOMM_MAC2_1                0x50
#define QUALCOMM_MAC2_2                0xF3
#define QUALCOMM_MAC2_3_HOST_BYTE      0x00

#define QCMP_MEDIA_TYPE                NdisMedium802_3
#define QCMP_MEDIA_TYPE_WWAN           NdisMediumWirelessWan
#define QCMP_PHYSICAL_MEDIUM_TYPE      NdisPhysicalMediumUnspecified
#define QCMP_PHYSICAL_MEDIUM_TYPEWWAN  NdisPhysicalMediumWirelessWan

#define VENDOR_NAME                    "QUALCOMM"
#define VENDOR_DRIVER_VERSION          0x00010000
#define VENDOR_ID                      0x00A0C600

#define MAX_MCAST_LIST_LEN             32

#define MAX_LOOKAHEAD                  ETH_MAX_DATA_SIZE
#define MAX_RX_BUFFER_SIZE             ETH_MAX_PACKET_SIZE
#define MP_INVALID_LINK_SPEED          0xFFFFFFFF

#define fMP_QUERY_OID                  0
#define fMP_SET_OID                    1

#define DEVICE_CLASS_UNKNOWN           0
#define DEVICE_CLASS_CDMA              1
#define DEVICE_CLASS_GSM               2

#define MAX_CTRL_PENDING_REQUESTS      12

#define SUPPORTED_PACKET_FILTERS ( \
                NDIS_PACKET_TYPE_DIRECTED   | \
                NDIS_PACKET_TYPE_MULTICAST  | \
                NDIS_PACKET_TYPE_BROADCAST  | \
                NDIS_PACKET_TYPE_PROMISCUOUS | \
                NDIS_PACKET_TYPE_ALL_MULTICAST)

#define fMP_RESET_IN_PROGRESS               0x00000001
#define fMP_DISCONNECTED                    0x00000002 
#define fMP_ADAPTER_HALT_IN_PROGRESS        0x00000004
#define fMP_ADAPTER_SURPRISE_REMOVED        0x00000008
#define fMP_STATE_INITIALIZED               0x00000010
#define fMP_STATE_PAUSE                     0x00000020
#define fMP_ANY_FLAGS                       (fMP_DISCONNECTED | fMP_RESET_IN_PROGRESS | fMP_ADAPTER_HALT_IN_PROGRESS | fMP_ADAPTER_SURPRISE_REMOVED )

#define RESOURCE_BUF_SIZE           (sizeof(NDIS_RESOURCE_LIST) + \
                                        (10*sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR)))

#define ListItemToPacket(pList)  CONTAINING_RECORD(pList, NDIS_PACKET, MiniportReservedEx)
//
// Debug output selector, bit mapped on each debug type
//


#ifdef EVENT_TRACING
#define MP_DBG_MASK_READ      0x00000002  // Data Read
#define MP_DBG_MASK_WRITE     0x00000004  // Data Write
#define MP_DBG_MASK_OID_QMI   0x00002000  // OID/QMI data
#define MP_DBG_MASK_CONTROL   0x00004000  // PNP + OID + QMI
#define MP_DBG_MASK_QOS       0x00008000  // QOS
#define MP_DBG_MASK_DATA_QOS  0x00010000  // QOS data
#define MP_DBG_MASK_DATA_WT   0x00020000  // QOS data
#define MP_DBG_MASK_DATA_RD   0x00040000  // QOS data
#else
#define MP_DBG_MASK_MASK    0x00FF0000
#define MP_DBG_LEVEL_MASK   0x0000000F

#define MP_DBG_MASK_CONTROL   0x00010000  // PNP + OID + QMI
#define MP_DBG_MASK_READ      0x00020000  // Data Read
#define MP_DBG_MASK_WRITE     0x00040000  // Data Write
#define MP_DBG_MASK_QOS       0x00080000  // QOS
#define MP_DBG_MASK_DATA_QOS  0x00200000  // QOS data
#define MP_DBG_MASK_OID_QMI   0x00400000  // OID/QMI data
#define MP_DBG_MASK_DATA_WT   0x00800000  // WT data
#define MP_DBG_MASK_DATA_RD   0x01000000  // RD data
#endif


#define MP_DBG_LEVEL_FORCE    0x0
#define MP_DBG_LEVEL_CRITICAL 0x1
#define MP_DBG_LEVEL_ERROR    0x2
#define MP_DBG_LEVEL_INFO     0x3
#define MP_DBG_LEVEL_DETAIL   0x4
#define MP_DBG_LEVEL_TRACE    0x5
#define MP_DBG_LEVEL_VERBOSE  0x6

#if DBG
#define QCNET_DbgPrintG(_x_) \
{ \
      DbgPrint _x_; \
}

#define QCNET_DbgPrint(mask,level,_x_) \
        { \
           if ( ((pAdapter->DebugMask & mask) && \
                 (pAdapter->DebugLevel >= level)) || \
                (level == 0) ) \
           { \
              DbgPrint _x_; \
           } \
        }
              
#else 
   #define QCNET_DbgPrintG(_x_)
   #define QCNET_DbgPrint(mask,level,_x_)
#endif

#undef ntohl
#undef ntohll
#undef ntohs

#define ntohl(val) \
        ((ULONG)((((ULONG)(val) & 0xff000000U) >> 24) | \
                 (((ULONG)(val) & 0x00ff0000U) >>  8) | \
                 (((ULONG)(val) & 0x0000ff00U) <<  8) | \
                 (((ULONG)(val) & 0x000000ffU) << 24)))

#define ntohll(val) \
        ((ULONGLONG)((((ULONGLONG)(val) & 0x00000000000000ffULL) << 56)   | \
                     (((ULONGLONG)(val) & 0x000000000000ff00ULL) << 40)   | \
                     (((ULONGLONG)(val) & 0x0000000000ff0000ULL) << 24)   | \
                     (((ULONGLONG)(val) & 0x00000000ff000000ULL) << 8)    | \
                     (((ULONGLONG)(val) & 0x000000ff00000000ULL) >> 8)    | \
                     (((ULONGLONG)(val) & 0x0000ff0000000000ULL) >> 24)   | \
                     (((ULONGLONG)(val) & 0x00ff000000000000ULL) >> 40)   | \
                     (((ULONGLONG)(val) & 0xff00000000000000ULL) >> 56)))

#define ntohs(x) \
        ((USHORT)((((USHORT)(x) & 0x00ff) << 8) | \
        (((USHORT)(x) & 0xff00) >> 8)))

#define MP_DEVICE_ID_NONE  256
#define MP_DEVICE_ID_SET   1
#define MP_DEVICE_ID_MAX   255
#define MP_INVALID_QMI_ID  0xF0000000

// Define config bits
#define IGNORE_QMI_CONTROL_ERRORS 0x00000001

// MuxId Range 0x70 to 0x8F [QUD-1180]
#define USB_MUXID_IDENTIFIER   0x70
#define PCIE_MUXID_IDENTIFIER  0x70
#define THTHERED_CALL_MUXID_UPPER_BOUND  0x8F

typedef struct _MP_GLOBAL_DATA
{
    LIST_ENTRY      AdapterList;
    NDIS_SPIN_LOCK  Lock;

    LONG            MACAddressByte;
    LONG            InstanceIndex;
    UCHAR           DeviceId[MP_DEVICE_ID_MAX+1];
    SYSTEM_POWER_STATE LastSysPwrIrpState;
    LONG            MuxId;
} MP_GLOBAL_DATA, *PMP_GLOBAL_DATA;

extern BOOLEAN        QCMP_NDIS6_Ok, QCMP_NDIS620_Ok, QCMP_NDIS630_Ok;
extern MP_GLOBAL_DATA GlobalData;
extern char           gDeviceName[255];
extern NDIS_OID       QCMPSupportedOidList[];
extern PDRIVER_OBJECT gDriverObject;
extern ULONG          QCMP_SupportedOidListSize;

extern NDIS_GUID      QCMPSupportedOidGuids[];
extern ULONG          QCMP_SupportedOidGuidsSize;

#ifdef NDIS60_MINIPORT
extern NDIS_HANDLE  MiniportDriverContext;
extern NDIS_HANDLE  NdisMiniportDriverHandle;
#endif // NDIS60_MINIPORT

typedef struct ControlTxTag
{
    ULONG       TransactionID;

    NDIS_OID    Oid;
    ULONG       OidType;
    ULONG       BufferLength;

    PCHAR       Buffer;
} ControlTx, *pControlTx;

typedef struct ControlRxTag
{
    ULONG   TransactionID;
    NDIS_STATUS Status;

    union
    {
        struct
        {
            NDIS_OID    Oid;
            ULONG       Num_bytes;
            ULONG       Bytes_needed;
        } response;

        struct
        {
            ULONG       Num_bytes;
            ULONG       reserved[2];
        } indication;
    };
    PCHAR   Buffer;
} ControlRx, *pControlRx;

#define QMI_TAG                   ((ULONG)'IMQQ')

typedef struct _QCWRITEQUEUE
{
   LIST_ENTRY QCWRITERequest;
   ULONG QMILength;
   CHAR Buffer[1];
}QCWRITEQUEUE, *PQCWRITEQUEUE;

typedef struct _QCQMIQUEUE
{
   LIST_ENTRY QCQMIRequest;
   ULONG QMILength;
   QCQMI QMI;
}QCQMIQUEUE, *PQCQMIQUEUE;

typedef struct OIDWriteTag
{
    LIST_ENTRY  List;
    ULONG       IrpDataLength;
    NTSTATUS    IrpStatus;
    PUCHAR      RequestInfoBuffer;
    ULONG       RequestBufferLength;
    PULONG      RequestBytesUsed;
    PULONG      RequestBytesNeeded;
    NDIS_OID    Oid;
    ULONG       OidType;
    PVOID       OidReference;  // ptr to NDIS6 OidRequest

    // remove union -- revisit later -- TODO
    ControlTx   OID;
    QCQMI       QMI;
    LIST_ENTRY  QMIQueue;

    NDIS_STATUS OIDStatus;
    NDIS_STATUS OIDAsyncType;
    NDIS_STATUS IndicationType;

    ULONG OIDRespLen;
    PVOID pOIDResp;

    // #ifdef NDIS60_MINIPORT
    QCMP_OID_COPY OidReqCopy;
    // #endif // NDIS60_MINIPORT
} MP_OID_WRITE, *PMP_OID_WRITE;

typedef struct OIDReadTag
{
    LIST_ENTRY  List;
    ULONG       IrpDataLength;
    NTSTATUS    IrpStatus;
    PVOID       OidReference;  // ptr to NDIS6 OidRequest

    // remove union -- revisit later -- TODO
    ControlRx   OID;
    QCQMI       QMI;
} MP_OID_READ, *PMP_OID_READ;

typedef struct LLHeaderTag
{
    LIST_ENTRY  List;

    UCHAR   Buffer[];
} LLHeader, *pLLHeader;

#define MP_INIT_QMI_EVENT_INDEX      0
#define MP_CANCEL_EVENT_INDEX        1
#define MP_TX_TIMER_INDEX            2
#define MP_TX_EVENT_INDEX            3
#define MP_MEDIA_CONN_EVENT_INDEX    4
#define MP_MEDIA_DISCONN_EVENT_INDEX 5
#define MP_IODEV_ACT_EVENT_INDEX     6
#define MP_MEDIA_CONN_EVENT_INDEX_IPV6 7
#define MP_IPV4_MTU_EVENT_INDEX      8
#define MP_IPV6_MTU_EVENT_INDEX      9

#ifdef QCUSB_MUX_PROTOCOL

#define MP_DL_PAUSE_EVENT_INDEX      10
#define MP_DL_RESUME_EVENT_INDEX     11

#ifdef QC_DUAL_IP_FC
#define MP_PAUSE_V4_EVENT_INDEX      12
#define MP_PAUSE_V6_EVENT_INDEX      13
#define MP_RESUME_V4_EVENT_INDEX     14
#define MP_RESUME_V6_EVENT_INDEX     15
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
#define MP_QMAP_CONTROL_EVENT_INDEX  16
#define MP_PAUSE_DL_QMAP_EVENT_INDEX  17
#define MP_RESUME_DL_QMAP_EVENT_INDEX 18
#define MP_THREAD_EVENT_COUNT         19
#else
#define MP_THREAD_EVENT_COUNT        16
#endif
#else
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
#define MP_QMAP_CONTROL_EVENT_INDEX   12
#define MP_PAUSE_DL_QMAP_EVENT_INDEX  13
#define MP_RESUME_DL_QMAP_EVENT_INDEX 14
#define MP_THREAD_EVENT_COUNT         15
#else
#define MP_THREAD_EVENT_COUNT        12
#endif
#endif // QC_DUAL_IP_FC

#else
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
#define MP_QMAP_CONTROL_EVENT_INDEX   10
#define MP_PAUSE_DL_QMAP_EVENT_INDEX  11
#define MP_RESUME_DL_QMAP_EVENT_INDEX 12
#define MP_THREAD_EVENT_COUNT         13
#else
#define MP_THREAD_EVENT_COUNT        10
#endif

#endif // QCUSB_MUX_PROTOCOL

#define QOS_CANCEL_EVENT_INDEX      0
#define QOS_TX_EVENT_INDEX          1
#define QOS_MAIN_THREAD_EVENT_COUNT 2

#define QOS_DISP_CANCEL_EVENT_INDEX 0
#define QOS_DISP_TX_EVENT_INDEX     1
#define QOS_DISP_THREAD_EVENT_COUNT 2

#define QMIQOS_CANCEL_EVENT_INDEX 0
#define QMIQOS_READ_EVENT_INDEX   1
#define QMIQOS_THREAD_EVENT_COUNT 2

#define WORK_CANCEL_EVENT_INDEX  0
#define WORK_PROCESS_EVENT_INDEX 1
#define WORK_THREAD_EVENT_COUNT  2

#define RX_CANCEL_EVENT_INDEX  0
#define RX_PROCESS_EVENT_INDEX 1
#define RX_THREAD_EVENT_COUNT  2
#define RX_THREAD_COUNT        PARAM_MPRxStreams_MAX

#define WDSIP_CANCEL_EVENT_INDEX 0
#define WDSIP_READ_EVENT_INDEX   1
#define WDSIP_THREAD_EVENT_COUNT 2

typedef struct _RX_THREAD_CONTEXT
{
   PVOID AdapterContext;
   INT   Index;
} RX_THREAD_CONTEXT, *PRX_THREAD_CONTEXT;

typedef enum _QOS_FLOW_QUEUE_TYPE
{
   FLOW_SEND_WITH_DATA    = 0,
   FLOW_SEND_WITHOUT_DATA = 1,
   FLOW_QUEUED            = 2,
   FLOW_DEFAULT,
   FLOW_QUEUE_MAX
} QOS_FLOW_QUEUE_TYPE;

#pragma pack(push, 1)

typedef struct _MPQOS_FLOW_ENTRY
{
   LIST_ENTRY List;
   ULONG      FlowId;
   LIST_ENTRY FilterList;  // list of QCQOS_QMI_FILTER
   LIST_ENTRY Packets;     // list of NDIS_PACKET
   BOOLEAN    QueueState;
   QOS_FLOW_QUEUE_TYPE FlowState;
} MPQOS_FLOW_ENTRY, *PMPQOS_FLOW_ENTRY;

#ifdef QC_IP_MODE
#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_IPV6 0x86DD

typedef struct _QC_ETH_HDR
{
    UCHAR DstMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR SrcMacAddress[ETH_LENGTH_OF_ADDRESS];
    USHORT EtherType;
} QC_ETH_HDR, *PQC_ETH_HDR;

typedef struct _QC_ARP_HDR
{
   USHORT HardwareType;
   USHORT ProtocolType;
   UCHAR  HLEN;        // length of HA  (6)
   UCHAR  PLEN;        // length of IP  (4)
   USHORT Operation;
   UCHAR  SenderHA[ETH_LENGTH_OF_ADDRESS];  // 6
   ULONG  SenderIP;
   UCHAR  TargetHA[ETH_LENGTH_OF_ADDRESS];  // 6
   ULONG  TargetIP;
} QC_ARP_HDR, *PQC_ARP_HDR;

typedef struct _QC_IP_SETTINGS
{
   struct
   {
      int   Flags;        // reserved
      ULONG Address;
      ULONG Gateway;
      ULONG SubnetMask;
      ULONG DnsPrimary;
      ULONG DnsSecondary;
   } IPV4;

   struct
   {
      int   Flags;        // reserved
      union
      {
         UCHAR  Byte[16];
         USHORT Word[8];
      } Address;
      union
      {
         UCHAR  Byte[16];
         USHORT Word[8];
      } Gateway;
      union
      {
         UCHAR  Byte[16];
         USHORT Word[8];
      } DnsPrimary;
      union
      {
         UCHAR  Byte[16];
         USHORT Word[8];
      } DnsSecondary;
      UCHAR PrefixLengthIPAddr;
      UCHAR PrefixLengthGateway;
   } IPV6;

} QC_IP_SETTINGS, *PQC_IP_SETTINGS;

#endif // QC_IP_MODE

#pragma pack(pop)

// statistics
typedef enum _MP_STATS_TYPE
{
   MP_RML_CTL = 0, // control op
   MP_RML_RD,      // read IRP
   MP_RML_WT,      // write IRP
   MP_RML_TH,      // thread
   MP_CNT_TIMER,   // timer
   MP_MEM_CTL,     // control memory blocks
   MP_MEM_RD,      // read memory blocks
   MP_MEM_WT,      // write memory blocks
   MP_STAT_MAX
} MP_STATS_TYPE;

#if DBG
#define QcStatsIncrement(_adapter, _idx, _c) {InterlockedIncrement(&(_adapter->Stats[_idx]));}
#define QcStatsDecrement(_adapter, _idx, _c) {InterlockedDecrement(&(_adapter->Stats[_idx]));}
#else
#define QcStatsIncrement(_adapter, _idx, _c) {InterlockedIncrement(&(_adapter->Stats[_idx]));}
#define QcStatsDecrement(_adapter, _idx, _c) {InterlockedDecrement(&(_adapter->Stats[_idx]));}
#endif // DBG

// Remove lock
#define QcMpIoReleaseRemoveLock(_adapter,_lock,_tag,_idx,_c) { \
           IoReleaseRemoveLock(_lock,_tag); \
           QcStatsDecrement(_adapter, _idx, _c);}

#define DEVICE_REMOVAL_EVENTS_MAX 512

#ifdef NDIS620_MINIPORT

typedef enum _DEVICE_READT_STATE
{
   DeviceWWanUnknown = 0,
   DeviceWWanOff,
   DeviceWWanOn,
   DeviceWWanBadSim,
   DeviceWWanNoSim,
   DeviceWWanDeviceLocked,
   DeviceWWanNoServiceActivated,
   
}DEVICE_READY_STATE;

typedef enum _DEVICE_CK_PIN_STATE
{
   DeviceWWanCkUnknown = 0,
   DeviceWWanCkDeviceLocked,
   DeviceWWanCkDeviceUnlocked,
   
}DEVICE_CK_PIN_STATE;

typedef enum _DEVICE_RADIO_STATE
{
   DeviceWwanRadioUnknown = 0,
   DeviceWWanRadioOff,
   DeviceWWanRadioOn,

}DEVICE_RADIO_STATE;

typedef enum _DEVICE_REGISTER_STATE
{
   QMI_NAS_NOT_REGISTERED = 0,
   QMI_NAS_REGISTERED,
   QMI_NAS_NOT_REGISTERED_SEARCHING,
   QMI_NAS_REGISTRATION_DENIED,
   QMI_NAS_REGISTRATION_UNKNOWN

}DEVICE_REGISTER_STATE;

typedef enum _DEVICE_PACKET_STATE
{
   DeviceWwanPacketUnknown = 0,
   DeviceWWanPacketAttached,
   DeviceWWanPacketDetached,

}DEVICE_PACKET_STATE;

typedef enum _DEVICE_CONTEXT_STATE
{
   DeviceWwanContextUnknown = 0,
   DeviceWWanContextAttached,
   DeviceWWanContextDetached,

}DEVICE_CONTEXT_STATE;

typedef enum _DEVICE_REGISTER_MODE
{
   DeviceWWanRegisteredUnknown = 0,
   DeviceWWanRegisteredAutomatic,
   DeviceWWanRegisteredManual,

}DEVICE_REGISTER_MODE;

#pragma pack(push, 1)

typedef struct _SMS_LIST
{
   ULONG MessageIndex;
   UCHAR TagType;
}SMS_LIST, *PSMS_LIST;

#pragma pack(pop)

#endif

typedef struct _DEVICE_STATIC_INFO
{
   USHORT  ModelIdLen;
   WCHAR   ModelId[64];
   USHORT  FirmwareRevisionLen;
   WCHAR   FirmwareRevision[64];
   USHORT  MEIDLen;
   WCHAR   MEID[64];
   USHORT  ESNLen;
   WCHAR   ESN[64];
   USHORT  IMEILen;
   WCHAR   IMEI[64];
   USHORT  IMSILen;
   WCHAR   IMSI[64];
   USHORT  MINLen;
   WCHAR   MIN[64];
   USHORT  MDNLen;
   WCHAR   MDN[64];
   USHORT  ManufacturerLen;
   WCHAR   Manufacturer[64];
   USHORT  HardwareRevisionLen;
   WCHAR   HardwareRevision[64];
      
}DEVICE_STATIC_INFO, *PDEVICE_STATIC_INFO;

#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_MBIM_DL_SUPPORT)

#pragma pack(push, 1)

typedef struct _DATAGRAM_STRUCT
{
   union {
      ULONG Datagram;
      struct {
         USHORT DatagramPtr;
         USHORT DatagramLen;
      };
   };
} DATAGRAM_STRUCT, *PDATAGRAM_STRUCT;

typedef struct _NTB_16BIT_HEADER
{
   ULONG  dwSignature;
   USHORT wHeaderLength;
   USHORT wSequence;
   USHORT wBlockLength;
   USHORT wNdpIndex;
} NTB_16BIT_HEADER, *PNTB_16BIT_HEADER;

typedef struct _NTB_32BIT_HEADER
{
   ULONG  dwSignature;
   USHORT wHeaderLength;
   USHORT wSequence;
   ULONG  wBlockLength;
   ULONG  wNdpIndex;
} NTB_32BIT_HEADER, *PNTB_32BIT_HEADER;

typedef struct _NDP_16BIT_HEADER
{
   ULONG  dwSignature;
   USHORT wLength;
   //USHORT wReserved;
   USHORT wNextNdpIndex;
} NDP_16BIT_HEADER, *PNDP_16BIT_HEADER;

typedef struct _NDP_32BIT_HEADER
{
   ULONG  dwSignature;
   USHORT wLength;
   USHORT wReserved6;
   ULONG  wNextNdpIndex;
   ULONG  wReserved12;
   ULONG  wDatagramIndex;
   ULONG  wDatagramLength;
} NDP_32BIT_HEADER, *PNDP_32BIT_HEADER;

#pragma pack(pop)

#endif

#if defined(QCMP_QMAP_V1_SUPPORT) || defined(QCMP_QMAP_V2_SUPPORT)

#pragma pack(push, 1)

typedef struct _QMAP_STRUCT
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
} QMAP_STRUCT, *PQMAP_STRUCT;

typedef struct _QMAP_UL_CHECKSUM
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
} QMAP_UL_CHECKSUM, *PQMAP_UL_CHECKSUM;

typedef struct _QMAP_DL_CHECKSUM
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
} QMAP_DL_CHECKSUM, *PQMAP_DL_CHECKSUM;

#pragma pack(pop)

#endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)

#pragma pack(push, 1)

typedef struct _QMAP_PSEUDO_CHECKSUM
{
   ULONG SrcAddr;
   ULONG DestAddr;
   UCHAR Zeros;
   UCHAR Protocol;
   USHORT Length;
} QMAP_PSEUDO_CHECKSUM, *PQMAP_PSEUDO_CHECKSUM;

typedef struct _QMAP_PSEUDO_CHECKSUMV6
{
    UCHAR SrcAddr[16];
    UCHAR DestAddr[16];   
    ULONG payloadLen;
    UCHAR Zeros[3];
    UCHAR NextHdr;
} QMAP_PSEUDO_CHECKSUMV6, *PQMAP_PSEUDO_CHECKSUMV6;

typedef struct _IPV4_FRAG_PKT
{
   union {
   USHORT Rsvd2;
   struct {
   USHORT Offset:13;
   USHORT Flags:3;
   };
   };
}IPV4_FRAG_PKT, *PIPV4_FRAG_PKT;

typedef struct _QMAP_IPV4
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
} QMAP_IPV4, *PQMAP_IPV4;

typedef struct _QMAP_IPV6
{
   ULONG Rsvd;
   USHORT payloadLen;
   UCHAR nextHdr;
   UCHAR HopLimit;
   UCHAR SrcAddr[16];
   UCHAR DestAddr[16];   
} QMAP_IPV6, *PQMAP_IPV6;

typedef struct _QMAP_TCP
{
   USHORT SrcPort;
   USHORT DestPort;
   ULONG Sequence;
   ULONG Ack;
   ULONG Rsvd;
   USHORT ChkSum;
} QMAP_TCP, *PQMAP_TCP;

typedef struct _QMAP_UDP
{
   USHORT SrcPort;
   USHORT DestPort;
   USHORT Len;
   USHORT ChkSum;
} QMAP_UDP, *PQMAP_UDP;

typedef struct _QMAP_ICMP
{
   UCHAR Type;
   UCHAR Code;
   USHORT ChkSum;
} QMAP_ICMP, *PQMAP_ICMP;

#pragma pack(pop)

#endif
#endif

#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT) || defined(QCMP_QMAP_V2_SUPPORT)

#define TRANSMIT_TIMER_VALUE 2 // in miliseconds

#define MP_NUM_UL_TLP_ITEMS_DEFAULT 10
#define MP_NUM_UL_TLP_ITEMS 64
#define MP_MAX_PKT_SZIE     1520  // 1514 + 6 (QoS)
#define MP_NUM_MBIM_UL_DATAGRAM_ITEMS_DEFAULT 1024

typedef enum _TLP_STATE {
   TLP_AGG_SEND = 0,
   TLP_AGG_MORE,
   TLP_AGG_SMALL_BUF,
   TLP_AGG_NO_BUF
} TLP_STATE;

typedef struct _TLP_BUF_ITEM
{
   PIRP   Irp;
   PUCHAR Buffer;
   PUCHAR CurrentPtr;
   INT    DataLength;
   INT    RemainingCapacity;
   INT    AggregationCount;
   LIST_ENTRY PacketList;  // aggregated packets
   LIST_ENTRY List;
   INT    Index;
   PVOID  Adapter;
   PNTB_16BIT_HEADER pNtbHeader;
   NDP_16BIT_HEADER NdpHeader;
   DATAGRAM_STRUCT DataGrams[MP_NUM_MBIM_UL_DATAGRAM_ITEMS_DEFAULT];
} TLP_BUF_ITEM, *PTLP_BUF_ITEM;

#endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT

typedef struct _MP_ADAPTER
{
   LIST_ENTRY              List;
   LONG                    RefCount;
   LONG                    InstanceIdx;
   UNICODE_STRING          InterfaceGUID;
   UNICODE_STRING          DnsRegPathV4;
   UNICODE_STRING          DnsRegPathV6;
   ANSI_STRING             NetCfgInstanceId;
   USHORT                  DeviceInstance;
   BOOLEAN                 DeviceInstanceObtained;
   BOOLEAN                 NetworkAddressReadOk;
   NDIS_MEDIUM             NdisMediumType;
#ifdef NDIS620_MINIPORT
   NET_IFTYPE              NetIfType;
   ULONG                   NetLuidIndex;
   NET_LUID                NetLuid;
#endif // NDIS620_MINIPORT
   SHORT                   DeviceId;
   ULONG                   QMI_ID;
   NDIS_EVENT              RemoveEvent; 
   LONG                    Stats[MP_STAT_MAX];
   IO_REMOVE_LOCK          MPRmLock;
   PIO_REMOVE_LOCK         pMPRmLock;

   CHAR                    PortName[32];
   INT                     DebugLevel;
   ULONG                   DebugMask;
   ULONG                   USBDebugMask;

   // Other information regarding the device for chaching
   LONG                    RuntimeDeviceClass;
   UCHAR                   DeviceClass;
   UCHAR                   CDMAUIMSupport;
   UCHAR                   SimSupport;
   UCHAR                   MipMode;
   UCHAR                   MessageProtocol;
   PCHAR                   ICCID;
   ULONG                   ConnectIPv4Handle;
   ULONG                   ConnectIPv6Handle;
   ULONG                   WWanServiceConnectHandle;
   ULONG                   RssiThreshold;
   ULONG                   RegisterMode;
   ULONG                   RegisterRadioAccess;
   UCHAR                   CkFacility;
   UCHAR                   SMSCapacityType;
   UCHAR                   AuthPreference;
   UCHAR                   RegisterRetryCount;
   UCHAR                   IPType;
   UCHAR                   SetSelDomain;

   // Provisioned Context
#ifdef NDIS620_MINIPORT
   UCHAR                   SIMICCID[256];
   WWAN_CONTEXT            WwanContext;
   UCHAR                   ContextSet;
   NDIS_WWAN_READY_INFO    ReadyInfo;
   WCHAR                   TNList[2][WWAN_TN_LEN];
   CHAR                    CurrentCarrier[255];
   CHAR                    SystemID[255];
   NDIS_WWAN_RADIO_STATE   RadioState;        
   DEVICE_READY_STATE      DeviceReadyState;
   DEVICE_RADIO_STATE      DeviceRadioState;
   DEVICE_REGISTER_STATE   DeviceRegisterState;
   DEVICE_REGISTER_STATE   PreviousDeviceRegisterState;
   DEVICE_PACKET_STATE     DevicePacketState;
   DEVICE_PACKET_STATE     DeviceCircuitState;
   DEVICE_CONTEXT_STATE    DeviceContextState;
   DEVICE_CK_PIN_STATE     DeviceCkPinLock;
   DEVICE_PACKET_STATE     CDMAPacketService;
   ULONG                   nwRejectCause;
   ULONG                   SmsListIndex;
   ULONG                   SmsUIMNumMessages;
   ULONG                   SmsDeviceNumMessages;
   ULONG                   SmsNumMessages;
   ULONG                   SmsUIMMaxMessages;
   ULONG                   SmsDeviceMaxMessages;
   ULONG                   SmsStatusIndex;    
   SMS_LIST                SmsList[1024];
   NDIS_WWAN_SMS_CONFIGURATION   SmsConfiguration;        

   //SIM retries left
   UCHAR                   SimRetriesLeft;

   //MB Access
   ULONG                   MBDunCallOn;

   // Model String from registry
   UNICODE_STRING          RegModelId;

   //Deregister Indication
   UCHAR                   DeregisterIndication; 

#endif

   //
   // Keep track of various device objects.
   //
#if defined(NDIS_WDM)
   PDEVICE_OBJECT          Pdo; 
   PDEVICE_OBJECT          Fdo; 
   PDEVICE_OBJECT          NextDeviceObject; 
   PDEVICE_OBJECT          USBDo;
   BOOLEAN                 UsbRemoved;
   LONG                    RemovalInProgress;
#endif

   NDIS_HANDLE             AdapterHandle;    
   ULONG                   Flags;
   UCHAR                   PermanentAddress[ETH_LENGTH_OF_ADDRESS];
   UCHAR                   CurrentAddress[ETH_LENGTH_OF_ADDRESS];

   ULONG                   NextTxID;

   //
   // Variables to track resources for the Reset operation
   //
   NDIS_TIMER              ResetTimer;
   RST_STATE               ResetState;
   LONG                    nResetTimerCount;

   NDIS_TIMER              ReconfigTimer;
   RECONF_TIMER_STATE      ReconfigTimerState;
   ULONG                   ReconfigDelay;
   KEVENT                  ReconfigCompletionEvent;

   // IPv6
   NDIS_TIMER              ReconfigTimerIPv6;
   RECONF_TIMER_STATE      ReconfigTimerStateIPv6;
   ULONG                   ReconfigDelayIPv6;
   KEVENT                  ReconfigCompletionEventIPv6;

#ifdef NDIS620_MINIPORT
   NDIS_TIMER              SignalStateTimer;
   LONG                    nSignalStateTimerCount;

   NDIS_TIMER              MsisdnTimer;
   ULONG                   MsisdnTimerCount;
   ULONG                   MsisdnTimerValue;
   /* Register Packet timer*/
   NDIS_TIMER              RegisterPacketTimer;
   PVOID                   RegisterPacketTimerContext;

   NDIS_TIMER              SignalStateDisconnectTimer;
   LONG                    nSignalStateDisconnectTimer;
   LONG                    nSignalStateDisconnectTimerCalled;
#endif

#ifdef QCMP_TEST_MODE
   ULONG                   TestConnectDelay;
#endif // QCMP_TEST_MODE

    #ifdef QCUSB_MUX_PROTOCOL
    BOOLEAN                 MuxEnabled;
    ULONG                   MuxCtrlDLCI;
    ULONG                   MuxDataDLCI;
    BOOLEAN                 DLPauseEnabled;
    ULONG                   DLPausePktCount;
    ULONG                   DLPktCount;
    #ifdef QC_DUAL_IP_FC
    BOOLEAN                 FlowControlV4;
    BOOLEAN                 FlowControlV6;
    LIST_ENTRY              FlowControlListV4;
    LIST_ENTRY              FlowControlListV6;
    ULONG                   MPEnableQCDualIpFc;
    #endif // QC_DUAL_IP_FC    
    #endif // QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
    BOOLEAN                 QMAPFlowControl;
    LIST_ENTRY              QMAPFlowControlList;
#endif // QC_DUAL_IP_FC    

    #ifdef QCMP_SUPPORT_CTRL_QMIC
    ULONG   QMIFuzzing;
    ULONG   ExternalQMICtrlCid;
    BOOLEAN ExternalQCTLEnabled;
    BOOLEAN CtrlDevAcquired;
    #endif // QCMP_SUPPORT_CTRL_QMIC
/*
    LONG                    testTxLIndx;
    PNDIS_PACKET            testTxList[2500];
    PUCHAR                  testTxUsbBufList[2500];
    LONG                    testTxCLIndx;
    PNDIS_PACKET            testTxCompleteList[2500];

    LONG                    testNotPendTx;

    LONG                    testTotTxQ;
    LONG                    testTotTxS;
    LONG                    testTotTxU;
    LONG                    testTotTxC;
    LONG                    testTotTxI;
*/
   //
   // Variables to track resources for the send operation
   //
   NDIS_SPIN_LOCK          TxLock;      
   LONG                    nBusyTx;

   LIST_ENTRY              TxPendingList;
   LIST_ENTRY              TxBusyList;
   LIST_ENTRY              TxCompleteList;

   LONG                    nPendingOidReq;
#ifdef NDIS60_MINIPORT
   LIST_ENTRY              OidRequestList;
#endif // NDIS60_MINIPORT

   LIST_ENTRY              LHFreeList;
   PUCHAR                  LLHeaderMem;

   //
   // Variables to track resources for the Receive operation
   //
   NDIS_SPIN_LOCK          RxLock;
   LONG                    nBusyRx;
   LONG                    nRxHeldByNdis;
   LONG                    nRxPendingInUsb;
   LONG                    nRxFreeInMP;
   LONG                    nRxInNblChain;

   LIST_ENTRY              RxFreeList;
   LIST_ENTRY              RxPendingList;

   NDIS_HANDLE             RxPacketPoolHandle;
   NDIS_HANDLE             RxBufferPool;
#ifdef NDIS60_MINIPORT
   PMPUSB_RX_NBL           RxBufferMem;  // for NDIS6
   LIST_ENTRY              RxNblChain[RX_THREAD_COUNT];   // RX chain for protocol layer
   UCHAR                   ActiveRxSlot[RX_THREAD_COUNT];
#endif 
   LONG                    DropDataTest; 

   //
   // Variables used to track resources for control read operatons
   //
   NDIS_SPIN_LOCK          CtrlReadLock;
   LONG                    nBusyRCtrl;

   LIST_ENTRY              CtrlReadFreeList;
   LIST_ENTRY              CtrlReadPendingList;
   LIST_ENTRY              CtrlReadCompleteList;

   PUCHAR                  CtrlReadBufMem;

   // Variables used to write only one QMI request at a time
   NDIS_SPIN_LOCK          CtrlWriteLock;
   LONG                    nBusyWrite;

   LIST_ENTRY              CtrlWriteList;
   LIST_ENTRY              CtrlWritePendingList;

   // Variables used to track resources for the OID write operations
   NDIS_SPIN_LOCK          OIDLock;
   LONG                    nBusyOID;

   LIST_ENTRY              OIDFreeList;
   LIST_ENTRY              OIDPendingList;
   LIST_ENTRY              OIDWaitingList;

   PUCHAR                  OIDBufMem;

   USHORT                  ProtocolType;
   USHORT                  HeaderOffset;

   //
   // Variables used to track resources for OID Async operations
   //
   LIST_ENTRY              OIDAsyncList;

   //
   // Packet Filter and look ahead size.
   //
   ULONG                   PacketFilter;
   ULONG                   ulLookAhead;
   ULONG                   ulMaxBusySends;
   ULONG                   ulMaxBusyRecvs;

   // multicast list
   ULONG                   ulMCListSize;
   UCHAR                   MCList[MAX_MCAST_LIST_LEN][ETH_LENGTH_OF_ADDRESS];

   // Packet counts (local)
   ULONG64                 GoodTransmits;
   ULONG64                 GoodReceives;

   ULONG64                 BadTransmits;
   ULONG64                 BadReceives;
   ULONG64                 DroppedReceives;

   // Bytes counts (local)
   ULONG64                 TxBytesGood;
   ULONG64                 TxBytesBad;
   ULONG64                 TxFramesFailed;
   ULONG64                 TxFramesDropped;
   ULONG64                 RxBytesGood;
   ULONG64                 RxBytesBad;
   ULONG64                 RxBytesDropped;

   // QC transport statistics
   QC_XFER_STATISTICS      QcXferStats;

   // These values may be in the Registry
   // They are initialized to a default then
   // if present re-initialized from values in the
   // registry
   ULONG                   MaxDataSends;
   ULONG                   MaxDataReceives;

   ULONG                   MaxCtrlSends;
   ULONG                   CtrlSendSize;

   ULONG                   MaxCtrlReceives;
   ULONG                   CtrlReceiveSize;

   ULONG                   LLHeaderBytes;

   ULONG                   ulCurrentTxRate;
   ULONG                   ulCurrentRxRate;
   ULONG                   ulServingSystemTxRate;
   ULONG                   ulServingSystemRxRate;
   ULONG                   ulMediaConnectStatus;

   ULONG                   NumClients;
   UCHAR                   ClientId[QMUX_TYPE_MAX+1];
   LONG                    PendingCtrlRequests[QMUX_TYPE_MAX+1];
   LONG                    MaxPendingQMIReqs;
   BOOLEAN                 IsQMIOutOfService;
   UCHAR                   QMIType;
   NDIS_SPIN_LOCK          QMICTLLock;
   LIST_ENTRY              QMICTLTransactionList;
   LONG                    QMICTLTransactionId;
   LONG                    QMUXTransactionId;
   ULONG                   QMIInitInProgress;
   KEVENT                  MainAdapterQmiInitSuccessful;
   KEVENT                  MainAdapterSetDataFormatSuccessful;

   QMI_SERVICE_VERSION     QMUXVersion[QMUX_TYPE_ALL+1];
   CHAR                    AddendumVersionLabel[256];
   KEVENT                  QMICTLInstanceIdReceivedEvent;
   KEVENT                  QMICTLVersionReceivedEvent;
   KEVENT                  DeviceInfoReceivedEvent;
   KEVENT                  ClientIdReceivedEvent;
   KEVENT                  ClientIdReleasedEvent;
   KEVENT                  DataFormatSetEvent;
   KEVENT                  CtlSyncEvent;
   KEVENT                  ControlReadPostedEvent;
   PCHAR                   DeviceManufacturer;
   PCHAR                   DeviceModelID;
   PCHAR                   DeviceRevisionID;
   PCHAR                   HardwareRevisionID;
   PCHAR                   DevicePriID;
   PCHAR                   DeviceMSISDN;
   PCHAR                   DeviceMIN;
   PCHAR                   DeviceIMSI;
   CHAR                    DeviceSerialNumberESN[QCDEV_MAX_SER_STR_SIZE];
   CHAR                    DeviceSerialNumberIMEI[QCDEV_MAX_SER_STR_SIZE];
   CHAR                    DeviceSerialNumberMEID[QCDEV_MAX_SER_STR_SIZE];
   KEVENT                   QMICTLSyncReceivedEvent;

   // statistics from device (foreign)
   ULONG                   TxGoodPkts;
   ULONG                   RxGoodPkts;
   ULONG                   TxErrorPkts;
   ULONG                   RxErrorPkts;
   UCHAR                   EventReportMask;
   UCHAR                   ProposedEventReportMask;

#ifdef NDIS_WDM
   KEVENT USBPnpCompletionEvent;
   NDIS_SPIN_LOCK          UsbLock;      
#endif

   SYSTEM_POWER_STATE       MiniportSystemPower;
   DEVICE_POWER_STATE       MiniportDevicePower;
   NDIS_DEVICE_POWER_STATE  PreviousDevicePowerState;
   BOOLEAN                  ToPowerDown;

   // Needed to determine which RSSI to use
   UCHAR                    nDataBearer;

   // For MP main thread
   HANDLE   hMPThreadHandle;
   PKTHREAD pMPThread;
   KEVENT   MPThreadStartedEvent;
   KEVENT   MPThreadClosedEvent;
   KEVENT   MPThreadCancelEvent;
   KEVENT   MPThreadInitQmiEvent;
   KEVENT   MPThreadTxTimerEvent;   
   KEVENT   MPThreadTxEvent;
   KEVENT   MPThreadMediaConnectEvent;
   KEVENT   MPThreadMediaConnectEventIPv6;
   KEVENT   MPThreadMediaDisconnectEvent;
   KEVENT   MPThreadIODevActivityEvent;
#ifdef QCUSB_MUX_PROTOCOL
   KEVENT   MPThreadDLPauseEvent;
   KEVENT   MPThreadDLResumeEvent;
   KEVENT   MPThreadPauseV4Event;
   KEVENT   MPThreadPauseV6Event;
   KEVENT   MPThreadResumeV4Event;
   KEVENT   MPThreadResumeV6Event;   
#endif // QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
   LIST_ENTRY QMAPControlList;
   KEVENT   MPThreadQMAPDLPauseEvent;
   KEVENT   MPThreadQMAPDLResumeEvent;
   KEVENT   MPThreadQMAPControlEvent;
   BOOLEAN  QMAPDLPauseEnabled;
#endif    
   PKEVENT  MPThreadEvent[MP_THREAD_EVENT_COUNT];
   ULONG    MPThreadCancelStarted;

   // QOS
   LONG     MPDisableQoS;
   ULONG    VlanId;
   BOOLEAN  IsQosPresent;
   BOOLEAN  QosEnabled;
   ULONG    QosHdrFmt;   
   LONG     MPPendingPackets;
   LONG     QosPendingPackets;
   PKTHREAD pQosThread;
   HANDLE   hQosThreadHandle;
   KEVENT   QosThreadStartedEvent;
   KEVENT   QosThreadClosedEvent;
   KEVENT   QosThreadCancelEvent;
   KEVENT   QosThreadTxEvent;
   PKEVENT  QosThreadEvent[QOS_MAIN_THREAD_EVENT_COUNT];

   // WorkThread
   PKTHREAD pWorkThread;
   HANDLE   hWorkThreadHandle;
   KEVENT   WorkThreadStartedEvent;
   KEVENT   WorkThreadClosedEvent;
   KEVENT   WorkThreadCancelEvent;
   KEVENT   WorkThreadProcessEvent;
   PKEVENT  WorkThreadEvent[WORK_THREAD_EVENT_COUNT];
   ULONG    WorkThreadCancelStarted;

   // RxThread
   PKTHREAD pRxThread[RX_THREAD_COUNT];
   HANDLE   hRxThreadHandle[RX_THREAD_COUNT];
   KEVENT   RxThreadStartedEvent[RX_THREAD_COUNT];
   KEVENT   RxThreadClosedEvent[RX_THREAD_COUNT];
   KEVENT   RxThreadCancelEvent[RX_THREAD_COUNT];
   KEVENT   RxThreadProcessEvent[RX_THREAD_COUNT];
   PKEVENT  RxThreadEvent[RX_THREAD_COUNT][RX_THREAD_EVENT_COUNT];
   ULONG    RxThreadCancelStarted;
   RX_THREAD_CONTEXT RxThreadContext[RX_THREAD_COUNT];
   NDIS_SPIN_LOCK RxIndLock[RX_THREAD_COUNT];
   NDIS_SPIN_LOCK RxProtocolLock[RX_THREAD_COUNT];
   ULONG    EnableData5G;
   ULONG    RxIndClusterSize;
   ULONG    RxStreams;

#ifdef QC_IP_MODE
   // Link Protocol
   LONG     MPDisableIPMode;   
   BOOLEAN  IsLinkProtocolSupported;
   BOOLEAN  IPModeEnabled;
   QC_IP_SETTINGS IPSettings;
   KEVENT   QMIWDSIPAddrReceivedEvent;
   USHORT   DeviceId2;
   UCHAR    MacAddress2[ETH_LENGTH_OF_ADDRESS];
#endif // QC_IP_MODE
   ULONG IPv4DataCall;
   ULONG IPv6DataCall;

   ULONG    IPV4Address;
   ULONG    IPV6Address;  // use as flag, not actual address
   BOOLEAN  IPV4Connected;
   BOOLEAN  IPV6Connected;

   LONG     NumberOfConnections;

   PKTHREAD pQosDispThread;
   HANDLE   hQosDispThreadHandle;
   KEVENT   QosDispThreadStartedEvent;
   KEVENT   QosDispThreadClosedEvent;
   KEVENT   QosDispThreadCancelEvent;
   KEVENT   QosDispThreadTxEvent;
   PKEVENT  QosDispThreadEvent[QOS_DISP_THREAD_EVENT_COUNT];

   LIST_ENTRY FilterPrecedenceList; // global QoS filter list
   LIST_ENTRY FlowDataQueue[FLOW_QUEUE_MAX]; // list of MPQOS_FLOW_ENTRY
   MPQOS_FLOW_ENTRY DefaultQosFlow;
   BOOLEAN  DefaultFlowEnabled;
   UCHAR    QosPacketBuffer[ETH_MAX_PACKET_SIZE + 100];
   
   // QMIQOS Client
   PKTHREAD pQmiQosThread;
   HANDLE   hQmiQosThreadHandle;
   KEVENT   QmiQosThreadStartedEvent;
   KEVENT   QmiQosThreadClosedEvent;
   KEVENT   QmiQosThreadCancelEvent;
   PKEVENT  QmiQosThreadEvent[QMIQOS_THREAD_EVENT_COUNT];

   LONG MPMainThreadInCancellation;
   LONG QosThreadInCancellation;
   LONG QosDispatchThreadInCancellation;
   LONG QosClientInCancellation;
   LONG WorkThreadInCancellation;

   #if defined(QCMP_MBIM_DL_SUPPORT)|| defined(QCMP_MBIM_UL_SUPPORT)
   LONG EnableMBIM;
   #endif
   
   #if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT)
   LONG     MPEnableMBIMUL;  // boolean value 0/1 from registry
   BOOLEAN  MBIMULEnabled;   // host-device negotiation
   LONG ntbSequence;
   #endif

   #if defined(QCMP_MBIM_DL_SUPPORT)
   LONG     MPEnableMBIMDL;  // boolean value 0/1 from registry
   BOOLEAN  MBIMDLEnabled;   // host-device negotiation
   #endif
   
   #if defined(QCMP_QMAP_V1_SUPPORT)
   LONG MPEnableQMAPV1;
   BOOLEAN QMAPEnabledV1;
   #endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   LONG MPEnableQMAPV2;
   BOOLEAN QMAPEnabledV2;
#endif
#endif
   
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
      LONG MPEnableQMAPV3;
      BOOLEAN QMAPEnabledV3;
#endif
#endif

   BOOLEAN WdsEnableUlParams;

   #if defined(QCMP_QMAP_V1_SUPPORT)
   LONG MPEnableQMAPV4;
   BOOLEAN QMAPEnabledV4;
   #endif
   
   #if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT)
   BOOLEAN    NumTLPBuffersConfigured;
   LONG       NumTLPBuffers;
   LONG       UplinkTLPSize;
   BOOLEAN    TLPUplinkOn;
   LIST_ENTRY UplinkFreeBufferQueue;
   TLP_BUF_ITEM UlTlpItem[MP_NUM_UL_TLP_ITEMS];

   LONG       TxTLPPendinginUSB;

   LONG     MPEnableTLP;  // boolean value 0/1 from registry
   BOOLEAN  TLPEnabled;    // host-device negotiation

   /* Register Transmit timer*/
   KTIMER  TransmitTimer;
   KDPC    TransmitTimerDPC;
   ULONG   TransmitTimerValue;
   BOOLEAN TransmitTimerLaunched;
   ULONG   FlushBufferinTimer;
   ULONG   FlushBufferBufferFull;
   LONG    TimerResolution;
   ULONG   ndpSignature;

   ULONG   MaxTLPPackets;
   #endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT

#if defined(QCMP_DL_TLP)
   BOOLEAN  IsDLTLPSupported;
   LONG       MPEnableDLTLP; 
   BOOLEAN  TLPDLEnabled;
#endif

   LONG MPQuickTx;  // boolean value 0/1

   // WDSIP Client
   UCHAR    WdsIpClientId;
   PVOID    WdsIpClientContext;  // PMPIOC_DEV_INFO
   PKTHREAD pWdsIpThread;
   HANDLE   hWdsIpThreadHandle;
   KEVENT   WdsIpThreadStartedEvent;
   KEVENT   WdsIpThreadClosedEvent;
   KEVENT   WdsIpThreadCancelEvent;
   PKEVENT  WdsIpThreadEvent[WDSIP_THREAD_EVENT_COUNT];
   CHAR     DnsInfoV4[256];
   CHAR     DnsInfoV6[256];
   BOOLEAN  IpFamilySet;
   ULONG    IpThreadCancelStarted;
   // WDS_ADMIN Client
   BOOLEAN  IsWdsAdminPresent;

   // QMI Initialization
   LONG    QmiSyncNeeded;
   LONG    QmiInInitialization;
   BOOLEAN QmiInitialized;
   BOOLEAN IsQmiWorking;
   BOOLEAN WdsMediaConnected;
   INT     QmiInitRetries;
   //Named Events for triggering removal
   PKEVENT NamedEvents[DEVICE_REMOVAL_EVENTS_MAX];
   HANDLE  NamedEventsHandle[DEVICE_REMOVAL_EVENTS_MAX];
   ULONG   NamedEventsIndex;

   DEVICE_STATIC_INFO DeviceStaticInfo;
   ULONG DeviceStaticInfoLen;

   //Mip Dereg
   BOOLEAN IsMipderegSent;

#ifdef NDIS620_MINIPORT
   NDIS_WWAN_PACKET_SERVICE_STATE WwanServingState;
   NDIS_WWAN_REGISTRATION_STATE   NdisRegisterState;
   UCHAR                          nVisibleIndex;
#endif

   BOOLEAN IgnoreQMICTLErrors;
   UCHAR   FriendlyName[256];
   USHORT  FriendlyNameWLen;
   WCHAR   FriendlyNameW[256];
   ULONG   MTUSize;

   ULONG Deregister;
   ULONG DisableTimerResolution;

#ifdef QCMP_DISABLE_QMI
   ULONG DisableQMI;
#endif
   UCHAR MuxId;
   ULONG DLAggMaxPackets;
   ULONG DLAggSize;

   ULONG   IPV4Mtu;
   ULONG   IPV6Mtu;
   KEVENT  IPV4MtuReceivedEvent;
   KEVENT  IPV6MtuReceivedEvent;
   PFILE_OBJECT MtuClient;
   USHORT MCC;
   USHORT MNC;
   BOOLEAN IsLTE;
   ULONG QMAPDLMinPadding;   
   UCHAR BindIFId;
   UCHAR BindEPType;
   ULONG DisableQMAPFC;
   
   BOOLEAN  IsNASSysInfoPresent;
   UCHAR AppType;
   ULONG UIMICCID;
   ULONG EnableSSDisconnectTimer;

#ifdef QCUSB_MUX_PROTOCOL
   ULONG FakeIMSI;
   ULONG FakeICCID;
#endif
} MP_ADAPTER, *PMP_ADAPTER;


/* ------------------------------- *
   Function Prototypes
 * ------------------------------- */

NDIS_STATUS DriverEntry(PVOID DriverObject, PVOID RegistryPath);

BOOLEAN MPMAIN_MiniportCheckForHang(NDIS_HANDLE MiniportAdapterContext);

VOID MPMAIN_MiniportHalt (NDIS_HANDLE MiniportAdapterContext);
VOID MPMAIN_MiniportHaltEx
(
   NDIS_HANDLE MiniportAdapterContext,
   NDIS_HALT_ACTION HaltAction
);

VOID MPMAIN_MiniportShutdown
(
   IN  NDIS_HANDLE MiniportAdapterContext
);
VOID MPMAIN_MiniportShutdownEx
(
   IN  NDIS_HANDLE          MiniportAdapterContext,
   IN  NDIS_SHUTDOWN_ACTION ShutdownAction
);

VOID MPMAIN_MiniportUnload(PDRIVER_OBJECT DriverObject);

NDIS_STATUS MPMAIN_MiniportReset
(
   PBOOLEAN AddressingReset,
   NDIS_HANDLE MiniportAdapterContext
);

#ifdef NDIS51_MINIPORT

VOID MPMAIN_MiniportPnPEventNotify
(
   NDIS_HANDLE             MiniportAdapterContext,
   NDIS_DEVICE_PNP_EVENT   PnPEvent,
   PVOID                   InformationBuffer,
   ULONG                   InformationBufferLength
);

#endif

#ifdef NDIS_WDM
NTSTATUS MPMAIN_PnpCompletionRoutine
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
);
BOOLEAN MPMAIN_SetupPnpIrp
(
   PMP_ADAPTER           Adapter,
   NDIS_DEVICE_PNP_EVENT PnPEvent,
   PIRP                  pIrp
);

NTSTATUS MPMAIN_PnpEventWithRemoval
(
   PMP_ADAPTER           Adapter,
   NDIS_DEVICE_PNP_EVENT PnPEvent,
   BOOLEAN               bRemove
);

VOID MPMAIN_InitUsbGlobal(void);

VOID MPMAIN_ResetOIDWaitingQueue(PMP_ADAPTER pAdapter);
#endif // NDIS_WDM

int listDepth(PLIST_ENTRY listHead, PNDIS_SPIN_LOCK lock);

NDIS_STATUS MPMAIN_SetDeviceId(PMP_ADAPTER pAdapter, BOOLEAN state);

NDIS_STATUS MPMAIN_GetDeviceDescription(PMP_ADAPTER pAdapter);

NDIS_STATUS MPMAIN_SetDeviceDataFormat(PMP_ADAPTER pAdapter);

VOID MPMAIN_DevicePowerStateChange
(
   PMP_ADAPTER             pAdapter,
   NDIS_DEVICE_POWER_STATE PowerState
);

BOOLEAN MPMAIN_InitializeQMI(PMP_ADAPTER pAdapter, INT QmiRetries);

NTSTATUS MPMAIN_StartMPThread(PMP_ADAPTER pAdapter);
NTSTATUS MPMAIN_CancelMPThread(PMP_ADAPTER pAdapter);
VOID MPMAIN_MPThread(PVOID Context);
VOID MPMAIN_Wait(LONGLONG WaitTime);

VOID MPMAIN_PrintBytes
(
   PVOID Buf,
   ULONG len,
   ULONG PktLen,
   char *info,
   PMP_ADAPTER pAdapter,
   ULONG DbgMask,
   ULONG DbgLevel
);

VOID MPMAIN_PowerDownDevice(PVOID Context, DEVICE_POWER_STATE DevicePower);

VOID MPMAIN_ResetPacketCount(PMP_ADAPTER pAdapter);

BOOLEAN QCMAIN_IsDualIPSupported(PMP_ADAPTER pAdapter);

VOID MPMAIN_MediaDisconnect(PMP_ADAPTER pAdapter, BOOLEAN DisconnectAll);

VOID MPMAIN_DeregisterMiniport(VOID);

VOID MPMAIN_DetermineNdisVersion(VOID);

VOID MyNdisMIndicateStatus
(
   IN NDIS_HANDLE  MiniportAdapterHandle,
   IN NDIS_STATUS  GeneralStatus,
   IN PVOID        StatusBuffer,
   IN UINT         StatusBufferSize
);

VOID MPMAIN_DisconnectNotification(PMP_ADAPTER pAdapter);

NTSTATUS MPMAIN_GetDeviceInstance(PMP_ADAPTER pAdapter);

NTSTATUS MPMAIN_GetDeviceFriendlyName(PMP_ADAPTER pAdapter);

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QC_DUAL_IP_FC
VOID MPMAIN_FlowControlCall(PMP_ADAPTER pAdapter, UCHAR IPVer, BOOLEAN FlowControlled);
BOOLEAN MPMAIN_IsFlowControlledData(PMP_ADAPTER pAdapter, PLIST_ENTRY pList);
VOID MPMAIN_PurgeFlowControlledPackets(PMP_ADAPTER pAdapter, BOOLEAN InQueueCompletion);
#endif
#endif
#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)
VOID MPMAIN_QMAPFlowControlCall(PMP_ADAPTER pAdapter, BOOLEAN FlowControlled);
BOOLEAN MPMAIN_QMAPIsFlowControlledData(PMP_ADAPTER pAdapter, PLIST_ENTRY pList);
VOID MPMAIN_QMAPPurgeFlowControlledPackets(PMP_ADAPTER pAdapter, BOOLEAN InQueueCompletion);
#endif

BOOLEAN MPTX_ProcessPendingTxQueue(PMP_ADAPTER);

#ifdef NDIS60_MINIPORT

VOID MPMAIN_MiniportDevicePnPEventNotify
(
   IN NDIS_HANDLE            MiniportAdapterContext,
   IN PNET_DEVICE_PNP_EVENT  NetDevicePnPEvent
);

NDIS_STATUS MPMAIN_MiniportResetEx
(
   IN NDIS_HANDLE  MiniportAdapterContext,
   OUT PBOOLEAN    AddressingReset
);

NDIS_STATUS MPMAIN_MiniportPause
(
   IN NDIS_HANDLE                     MiniportAdapterContext,
   IN PNDIS_MINIPORT_PAUSE_PARAMETERS MiniportPauseParameters
);

NDIS_STATUS MPMAIN_MiniportRestart
(
   IN NDIS_HANDLE                       MiniportAdapterContext,
   IN PNDIS_MINIPORT_RESTART_PARAMETERS MiniportRestartParameters
);

#endif // NDIS60_MINIPORT

BOOLEAN MPMAIN_InPauseState(PMP_ADAPTER pAdapter);

BOOLEAN MPMAIN_AreDataQueuesEmpty(PMP_ADAPTER pAdapter, PCHAR Caller);

NDIS_STATUS MPMAIN_OpenQMIClients(PMP_ADAPTER pAdapter);

NDIS_STATUS MPMAIN_MiniportSetOptions
(
   IN NDIS_HANDLE  NdisDriverHandle,
   IN NDIS_HANDLE  DriverContext
);

BOOLEAN SetTimerResolution ( PMP_ADAPTER pAdapter, BOOLEAN Flag );

BOOLEAN MoveQMIRequests( PMP_ADAPTER pAdapter );

VOID CleanupTxQueues(PMP_ADAPTER pAdapter);

#ifdef NDIS60_MINIPORT
// For debugging only
VOID MPRCV_QnblInfo
(
   PMP_ADAPTER   pAdapter,
   PMPUSB_RX_NBL RxNBL,
   int           Index,
   PCHAR         Msg
);
#endif // NDIS60_MINIPORT

VOID ResetCompleteTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
);

VOID ReconfigTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
);

VOID ReconfigTimerDpcIPv6
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
);

VOID TransmitTimerDpc
(
   PKDPC Dpc,
   PVOID DeferredContext,
   PVOID SystemArgument1,
   PVOID SystemArgument2
);

#ifdef NDIS620_MINIPORT

VOID SignalStateTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
);

VOID MsisdnTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
);
VOID RegisterPacketTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
);

VOID SignalStateDisconnectTimerDpc
(
   PVOID SystemSpecific1,
   PVOID FunctionContext,
   PVOID SystemSpecific2,
   PVOID SystemSpecific3
);

#endif

#endif    // _MPMAIN_H

