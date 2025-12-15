/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef QCMAIN_H
#define QCMAIN_H

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <ntstrsafe.h>
#include <ntddser.h>
#include <usb.h>
#include <wdfusb.h>
#include <wmilib.h>

#define READ_THREAD_RESUME_EVENT_COUNT          11
#define READ_THREAD_DEVICE_REMOVE_EVENT         0
#define READ_THREAD_REQUEST_ARRIVE_EVENT        1
#define READ_THREAD_FILE_CREATE_EVENT           2
#define READ_THREAD_FILE_CLOSE_EVENT            3
#define READ_THREAD_CLEAR_BUFFER_EVENT          4
#define READ_THREAD_SESSION_TOTAL_SET_EVENT     5
#define READ_THREAD_DEVICE_D0_EXIT_EVENT        6
#define READ_THREAD_DEVICE_D0_ENTRY_EVENT       7
#define READ_THREAD_REQUEST_TIMEOUT_EVENT       8
#define READ_THREAD_REQUEST_COMPLETION_EVENT    9
#define READ_THREAD_SCAN_WAIT_MASK_EVENT        10

#define WRITE_THREAD_RESUME_EVENT_COUNT         4
#define WRITE_THREAD_DEVICE_REMOVE_EVENT        0
#define WRITE_THREAD_REQUEST_ARRIVE_EVENT       1
#define WRITE_THREAD_FILE_CLOSE_EVENT           2
#define WRITE_THREAD_PURGE_EVENT                3

// Interrupt event indexes
#define INT_COMPLETION_EVENT_INDEX       0
#define INT_START_SERVICE_EVENT          1
#define INT_STOP_SERVICE_EVENT           2
#define INT_DEVICE_D0_ENTRY_EVENT        3
#define INT_DEVICE_D0_EXIT_EVENT         4
#define INT_LPC_EVENT                    5
#define INT_PIPE_EVENT_COUNT             6


#define USB_CDC_INT_RX_CARRIER   0x01   // bit 0
#define USB_CDC_INT_TX_CARRIER   0x02   // bit 1
#define USB_CDC_INT_BREAK        0x04   // bit 2
#define USB_CDC_INT_RING         0x08   // bit 3
#define USB_CDC_INT_FRAME_ERROR  0x10   // bit 4
#define USB_CDC_INT_PARITY_ERROR 0x20   // bit 5
#define USB_CDC_INT_OVERRUN      0x30   // bit 6

DRIVER_INITIALIZE DriverEntry;

typedef enum _QCUSB_AGG_BUF_STATE
{
    AGG_STATE_IDLE = 0,
    AGG_STATE_BUSY = 1
} QCUSB_AGG_BUF_STATE;

typedef struct _QCUSB_AGG_BUFFER
{

    PIRP                Irp;
    PUCHAR              Buffer;
    PUCHAR              CurrentPtr;
    INT                 DataLength;
    INT                 RemainingCapacity;
    INT                 AggregationCount;
    LIST_ENTRY          PacketList;  // aggregated packets
    LIST_ENTRY          List;
    INT                 Index;
    ULONG               MaxSize;
    ULONG               CurrentCount;
    QCUSB_AGG_BUF_STATE State;
    BOOLEAN             OkToSend;
} QCUSB_AGG_BUFFER;

typedef struct _QC_STATS
{
    LONG                AllocatedDSPs;
    LONG                AllocatedCtls;
    LONG                AllocatedReads;
    LONG                AllocatedRdMem;
    LONG                AllocatedWrites;
    LONG                AllocatedWtMem;
#ifdef QCUSB_MUX_PROTOCOL
    ULONGLONG           SessionTotal;
    LONGLONG            ViCurrentAddress;
    ULONG               ViCurrentDirection;
    ULONG               ViCurrentDataSize;
#endif
    LONG                RmlCount[8];
    LONG                RdPreQueueCnt;
    LONG                PreRdQueueFailure;
} QC_STATS;

#pragma pack(push, 1)
typedef struct _MODEM_INFO {
    ULONG ulDteRate;
    UCHAR ucStopBit;
    UCHAR ucParityType;
    UCHAR ucDataBits;
} MODEM_INFO, * PMODEM_INFO;
#pragma pack(pop)

typedef struct
{
    UCHAR   ucTimeoutType;
    BOOLEAN bUseReadInterval;
    BOOLEAN bReturnOnAnyChars;
} ReadTimeoutType;

typedef struct _RING_BUFFER
{
    PUCHAR pBuffer;
    size_t WriteHead;
    size_t ReadHead;
    size_t Capacity;
} RING_BUFFER, * PRING_BUFFER;

typedef struct _DEVICE_CONTEXT
{
    PWSTR                     PortName;
    PWSTR                     SerialCommName;
    WDFDEVICE                 Device;
    WDFUSBDEVICE              UsbDevice;

    WDFQUEUE                  DefaultQueue;                 // for IRP_MJ_DEVICE_CONTROL requests only

    KEVENT                    IntThreadStartedEvent;
    PCHAR                     pInterruptBuffer;
    PKTHREAD                  interruptThread;
    PKEVENT                   pInterruptPipeEvents[INT_PIPE_EVENT_COUNT];
    KEVENT                    InterruptCompletion;
    KEVENT                    InterruptStartService;
    KEVENT                    InterruptStopServiceEvent;
    KEVENT                    IntThreadD0EntryEvent;
    KEVENT                    IntThreadD0EntryReadyEvent;
    KEVENT                    IntThreadD0ExitEvent;
    KEVENT                    IntThreadD0ExitReadyEvent;
    KEVENT                    IntThreadLpcEvent;

    WDFQUEUE                  ReadQueue;
    RING_BUFFER               ReadRingBuffer;
    PKTHREAD                  ReadRequestHandlerThread;
    KEVENT                    ReadThreadStartedEvent;
    KEVENT                    ReadRequestArriveEvent;
    KEVENT                    ReadRequestCompletionEvent;
    KEVENT                    ReadThreadClearBufferEvent;
    KEVENT                    ReadPurgeCompletionEvent;
    KEVENT                    ReadThreadScanWaitMaskEvent;
    KEVENT                    SessionTotalSetEvent;
    PKEVENT                   ReadThreadResumeEvents[READ_THREAD_RESUME_EVENT_COUNT];
    ReadTimeoutType           ReadTimeout;
    KTIMER                    ReadTimer;
    KDPC                      ReadTimeoutDpc;
    KEVENT                    ReadRequestTimeoutEvent;
    KEVENT                    ReadThreadCancelCurrentEvent;

    WDFREQUEST                PendingReadRequest;           // for LPC/VI only
    LIST_ENTRY                UrbReadCompletionList;
    LIST_ENTRY                UrbReadPendingList;
    LIST_ENTRY                UrbReadFreeList;
    size_t                    UrbReadCompletionListDataSize;
    size_t                    UrbReadCompletionListLength;  // for debug use only
    size_t                    UrbReadPendingListLength;     // for debug use only
    size_t                    UrbReadFreeListLength;        // for debug use only
    WDFSPINLOCK               UrbReadListLock;
    ULONG                     UrbReadListCapacity;          // capacity of all read buffer lists
    ULONG                     UrbReadBufferSize;

    WDFQUEUE                  WriteQueue;
    PKTHREAD                  WriteRequestHandlerThread;
    KEVENT                    WriteThreadStartedEvent;
    KEVENT                    WriteRequestArriveEvent;
    KEVENT                    WriteThreadPurgeEvent;
    KEVENT                    WritePurgeCompletionEvent;
    LIST_ENTRY                WriteRequestPendingList;
    WDFSPINLOCK               WriteRequestPendingListLock;
    size_t                    WriteRequestPendingListLength;     // for debug use only
    size_t                    WriteRequestPendingListDataSize;
    PKEVENT                   WriteThreadResumeEvents[WRITE_THREAD_RESUME_EVENT_COUNT];

    WDFQUEUE                  WaitOnMaskQueue;
    WDFQUEUE                  WaitOnDeviceRemovalQueue;
    WDFQUEUE                  TimeoutReadQueue;

    WDFUSBPIPE                BulkIN;
    WDFUSBPIPE                BulkOUT;
    WDFUSBPIPE                InterruptInPipe;
    ULONG                     MaxBulkPacketSize;
    ULONG                     MaxIntPacketSize;
    WDFUSBPIPE                InterruptPipeIdx;
    ULONG                     wMaxPktSize;
    ULONG                     AmountInInQueue;

    // PnP related
    KEVENT                    FileCreateEvent;
    KEVENT                    FileCloseEventRead;
    KEVENT                    FileCloseEventWrite;
    KEVENT                    DeviceRemoveEvent;
    KEVENT                    ReadThreadD0EntryEvent;
    KEVENT                    ReadThreadD0ExitEvent;
    KEVENT                    ReadThreadD0EntryReadyEvent;
    KEVENT                    ReadThreadD0ExitReadyEvent;
    KEVENT                    ReadThreadFileCloseReadyEvent;
    KEVENT                    WriteThreadFileCloseReadyEvent;

    // serial related, all init to 0 by default by wdf framework
    SERIAL_TIMEOUTS           Timeouts;
    SERIAL_CHARS              Chars;
    SERIAL_HANDFLOW           HandFlow;
    SERIALPERF_STATS          PerfStats;
    SERIAL_STATUS             SerialStatus;
    MODEM_INFO                ModemInfo;
    UCHAR                     ModemStatusReg;
    UCHAR                     ModemControlReg;
    USHORT                    CurrUartState;
    BOOLEAN                   UartStateInitialized;
    QC_STATS                  QcStats;

    // Misc
    ULONG                     WaitMask;
    ULONG                     DebugMask;
    ULONG                     DebugLevel;
    UCHAR                     UsbDeviceType;
    USHORT                    InterfaceIndex;
    DEVICE_TYPE               FdoDeviceType;

    KEVENT                    TimeoutEvent;
    BOOLEAN                   PowerManagementEnabled;

    //QCPNP_PostVendorRegistryProcess
    WCHAR                     ucLoggingDir[128];
    BOOLEAN                   bLoggingOk;
    ULONG                     DeviceFunction;
    ULONG                     AggregationEnabled;
    QCUSB_AGG_BUFFER          WriteAggBuffer;
    ULONG                     ResolutionTime;
    ULONG                     AggregationPeriod;
#ifdef QCUSB_MUX_PROTOCOL
    BOOLEAN                   MuxEnabled;
    ULONG                     MuxCtrlDLCI;
    ULONG                     MuxDataDLCI;
    BOOLEAN                   toInitSVEFlow;
#endif // QCUSB_MUX_PROTOCOL

    //QCPNP_RetrieveServiceConfig
    BOOLEAN                   InServiceSelectiveSuspension;
    ULONG                     SelectiveSuspendIdleTime;
    BOOLEAN                   SelectiveSuspendInMiliSeconds;

    WMILIB_CONTEXT            WmiLibInfo;   // to query system power management tab
    CHAR                      DevSerialNumber[256];  // to hold USB_STRING_DESCRIPTOR of the serial number
    
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

typedef struct _READ_BUFFER_PARAM
{
    size_t      Capacity;
    size_t      AvailableBytes;
    PCHAR       pReadBuffer;
    WDFMEMORY   pReadMemory;
} READ_BUFFER_PARAM, *PREAD_BUFFER_PARAM;

typedef struct _REQUEST_CONTEXT
{
    LIST_ENTRY Link;
    WDFREQUEST Self;
    PDEVICE_CONTEXT pDevContext;
    PREAD_BUFFER_PARAM ReadBufferParam;
} REQUEST_CONTEXT, *PREQUEST_CONTEXT;

#pragma pack(push, 1)
typedef struct _PEER_DEV_INFO_HDR
{
   UCHAR Version;
   USHORT DeviceNameLength;
   USHORT SymLinkNameLength;
} PEER_DEV_INFO_HDR, *PPEER_DEV_INFO_HDR;
#pragma pack(pop)

NTSTATUS QCMAIN_SetDriverRegistryStringW
(
    PWCHAR          pValueName,
    PWCHAR          pValue,
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCMAIN_SetDriverRegistryDword
(
    PWCHAR              pValueName,
    DWORD               value,
    PDEVICE_CONTEXT     pDevContext
);

NTSTATUS QCMAIN_GetDriverRegistryDword
(
    WDFKEY              key,
    PCUNICODE_STRING    pValueName,
    PULONG              value,
    PDEVICE_CONTEXT     pDevContext
);

NTSTATUS QCMAIN_GetDriverRegistryStringW
(
    WDFKEY              key,
    PCUNICODE_STRING    pValueName,
    PUNICODE_STRING     pValueEntryData,
    PDEVICE_CONTEXT     pDevContext
);

NTSTATUS QCMAIN_DeleteDriverRegistryValue
(
    PWCHAR          pValueName,
    PDEVICE_CONTEXT pDevContext
);

VOID QCMAIN_Wait
(
    PDEVICE_CONTEXT pDevContext,
    LONGLONG WaitTime
);

NTSTATUS AllocateUnicodeString
(
    PUNICODE_STRING pusString,
    SIZE_T ulSize,
    ULONG pucTag
);

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, QCDevGetContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, QCReqGetContext)

// Modem configuration types
#define DEVICETYPE_NONE          0x00
#define DEVICETYPE_CDC           0x01  // first dev with int & bulk pipes
#define DEVICETYPE_CDC_LIKE      0x02
#define DEVICETYPE_SERIAL        0x03  // first dev without int pipes
#define DEVICETYPE_CTRL          0x04  // first dev without bulk pipes
#define DEVICETYPE_INVALID       0xFF

// Device function types
#define QCUSB_DEV_FUNC_MODEM     0x00
#define QCUSB_DEV_FUNC_GPS       0x01
#define QCUSB_DEV_FUNC_QDL       0x02
#define QCUSB_DEV_FUNC_LPC       0x03
#define QCUSB_DEV_FUNC_VI        0x04
#define QCUSB_DEV_FUNC_UNDEF     0xFFFFFFFF

// Debug levels
#define QCSER_DBG_LEVEL_FORCE    0x0
#define QCSER_DBG_LEVEL_CRITICAL 0x1
#define QCSER_DBG_LEVEL_ERROR    0x2
#define QCSER_DBG_LEVEL_INFO     0x3
#define QCSER_DBG_LEVEL_DETAIL   0x4
#define QCSER_DBG_LEVEL_TRACE    0x5
#define QCSER_DBG_LEVEL_VERBOSE  0x6

// Debug flags
#define QCSER_DBG_MASK_CONTROL   0x00000001
#define QCSER_DBG_MASK_READ      0x00000002
#define QCSER_DBG_MASK_WRITE     0x00000004
#define QCSER_DBG_MASK_ENC       0x00000008
#define QCSER_DBG_MASK_POWER     0x00000010
#define QCSER_DBG_MASK_STATE     0x00000020
#define QCSER_DBG_MASK_RDATA     0x00000040
#define QCSER_DBG_MASK_TDATA     0x00000080
#define QCSER_DBG_MASK_RIRP      0x00000200
#define QCSER_DBG_MASK_WIRP      0x00000400
#define QCSER_DBG_MASK_CIRP      0x00000800
#define QCSER_DBG_MASK_PIRP      0x00001000

// Registry Value Names
#define VEN_DEV_PORT        L"AssignedPortForQCDevice"
#define VEN_DEV_TIME        L"QCDeviceStamp"
#define VEN_DEV_SERNUM      L"QCDeviceSerialNumber"
#define VEN_DEV_MSM_SERNUM  L"QCDeviceMsmSerialNumber"
#define VEN_DEV_PROTOC      L"QCDeviceProtocol"
#define VEN_DEV_PARENT      L"QCDeviceParent"
#define VEN_DBG_MASK        L"QCDriverDebugMask"
#define VEN_DEV_VER         L"DriverVersion"
#define VEN_DEV_RTY_NUM     L"QCDriverRetriesOnError"
#define VEN_DEV_MAX_XFR     L"QCDriverMaxPipeXferSize"
#define VEN_DEV_L2_BUFS     L"QCDriverL2Buffers"
#define VEN_DRV_WRITE_UNIT  L"QCDriverWriteUnit"
#define VEN_DEV_CONFIG      L"QCDriverConfig"
#define VEN_DEV_ZLP_ON      L"QCDeviceZLPEnabled"
#define VEN_DEV_CID         L"QCDeviceCID"
#define VEN_DEV_SOCVER      L"QCDeviceSOCVER"

//DBG Macros
#define VEN_DRV_RESIDENT    L"QCDriverResident"
#define VEN_DRV_SS_IDLE_T   L"QCDriverSelectiveSuspendIdleTime"
#define VEN_DEV_LOG_DIR     L"QCDriverLoggingDirectory"
#define VEN_DRV_DEV_FUNC    L"QCDeviceFunction"
#define VEN_DRV_DEV_DLCI    L"QCDevDataDLCI"
#define VEN_DRV_AGG_ON      L"QCDriverAggregationEnabled"
#define VEN_DRV_AGG_TIME    L"QCDriverAggregationTime"
#define VEN_DRV_AGG_SIZE    L"QCDriverAggregationSize"
#define VEN_DRV_RES_TIME    L"QCDriverResolutionTime"

#define QCUSB_AGGREGATION_PERIOD_MIN        1000
#define QCUSB_AGGREGATION_PERIOD_MAX        5000
#define QCUSB_AGGREGATION_PERIOD_DEFAULT    2000

#define QCUSB_AGGREGATION_SIZE_MIN          (1024)*4 
#define QCUSB_AGGREGATION_SIZE_MAX          (1024)*30
#define QCUSB_AGGREGATION_SIZE_DEFAULT      (1024)*20

#define QCUSB_RESOLUTION_TIME_MIN           1 
#define QCUSB_RESOLUTION_TIME_MAX           5
#define QCUSB_RESOLUTION_TIME_DEFAULT       1

#define QCUSB_DRIVER_GUID_DATA_STR "{87E5A6EA-D48B-4883-8440-81D8A22508D7}:DATA"
#define QCUSB_DRIVER_GUID_DIAG_STR "{87E5A6EA-D48B-4883-8440-81D8A22508D7}:DIAG"
#define QCUSB_DRIVER_GUID_UNKN_STR "{87E5A6EA-D48B-4883-8440-81D8A22508D7}:UNKNOWN"

#ifdef QCUSB_MUX_PROTOCOL
#define QCUSB_IO_MULTIPLE 512
#define QCUSB_MAX_MRW_BUF_COUNT   16 // do not use larger value!
#else
#define QCUSB_MAX_MRW_BUF_COUNT   16 // do not use larger value!
#endif

#define QCUSB_IO_MULTIPLE               512
#define USB_WRITE_UNIT_SIZE             256L
#define USB_INTERNAL_READ_BUFFER_SIZE   (QCUSB_IO_MULTIPLE*QCUSB_MAX_MRW_BUF_COUNT*1024)
#define QCSER_RECEIVE_BUFFER_SIZE       4096L
#define QCSER_NUM_OF_LEVEL2_BUF         8

// define number of best effort retries on error
#define BEST_RETRIES     10
#define BEST_RETRIES_MIN 6
#define BEST_RETRIES_MAX 20

// define config bits
#define QCSER_CONTINUE_ON_OVERFLOW  0x00000001L
#define QCSER_CONTINUE_ON_DATA_ERR  0x00000002L
#define QCSER_USE_128_BYTE_IN_PKT   0x00000004L
#define QCSER_USE_256_BYTE_IN_PKT   0x00000008L
#define QCSER_USE_512_BYTE_IN_PKT   0x00000010L
#define QCSER_USE_1024_BYTE_IN_PKT  0x00000020L
#define QCSER_USE_2048_BYTE_IN_PKT  0x00000040L
#define QCSER_USE_1K_BYTE_OUT_PKT   0x00000080L
#define QCSER_USE_2K_BYTE_OUT_PKT   0x00000100L
#define QCSER_USE_4K_BYTE_OUT_PKT   0x00000200L
#define QCSER_USE_128K_READ_BUFFER  0x00000400L
#define QCSER_USE_256K_READ_BUFFER  0x00000800L
#define QCSER_NO_TIMEOUT_ON_CTL_REQ 0x00001000L
#define QCSER_RETRY_ON_TX_ERROR     0x00002000L
#define QCSER_USE_READ_ARRAY        0x00004000L
#define QCSER_USE_MULTI_WRITES      0x00008000L
#define QCSER_LOGGING_WRITE_THROUGH 0x10000000L
#define QCSER_LOG_LATEST_PKTS       0x20000000L
#define QCSER_ENABLE_LOGGING        0x80000000L

// HS-USB determination
#define QC_HSUSB_VERSION            0x0200
#define QC_HSUSB_BULK_MAX_PKT_SZ    512
#define QC_SS_BLK_PKT_SZ            1024
#define QC_HSUSB_VERSION_OK         0x01
#define QC_HSUSB_ALT_SETTING_OK     0x02
#define QC_HSUSB_BULK_MAX_PKT_OK    0x04
#define QC_SSUSB_VERSION_OK         0x08   // SS USB
#define QC_HS_USB_OK                (QC_HSUSB_VERSION_OK | QC_HSUSB_ALT_SETTING_OK)
#define QC_HS_USB_OK2               (QC_HSUSB_VERSION_OK | QC_HSUSB_BULK_MAX_PKT_OK)
#define QC_HS_USB_OK3               (QC_HSUSB_VERSION_OK | QC_HSUSB_ALT_SETTING_OK | QC_HSUSB_BULK_MAX_PKT_OK)

// SS-USB
#define QC_SSUSB_VERSION            0x0300
#define QC_SS_CTL_PKT_SZ            512

#ifdef QCUSB_MUX_PROTOCOL
#define VIUSB_CMD_SET_ADDR          0x0013   // payload of 12 bytes
#define VIUSB_CMD_SET_SIZE          0x0003   // payload of  8 bytes
#define VIUSB_CMD_SET_DIR           0x000D   // payload of  8 bytes

#define VIUSB_OP_DIR_READ           1
#define VIUSB_OP_DIR_WRITE          2

#pragma pack(push, 4)
typedef struct _VI_CONFIG
{
    LONGLONG Address;
    ULONG    Direction;
    ULONG    DataSize;
} VI_CONFIG, * PVI_CONFIG;
#pragma pack(pop)
#endif // QCUSB_MUX_PROTOCOL

#define SHARED_ACCESS               0x02

//POWER MODULE MACROS
#define QCUSB_SS_IDLE_MIN         3  // seconds
#define QCUSB_SS_IDLE_MAX       120  // seconds
#define QCUSB_SS_IDLE_DEFAULT     5  // seconds

#define QCUSB_SS_MILI_IDLE_MIN       200     // mili seconds
#define QCUSB_SS_MILI_IDLE_MAX       120000  // mili seconds
#define QCUSB_SS_MILI_IDLE_DEFAULT   5000  // mili seconds

typedef struct QCSER_VENDOR_CONFIG
{
    BOOLEAN ContinueOnOverflow;
    BOOLEAN ContinueOnDataError;
    BOOLEAN Use128ByteInPkt;
    BOOLEAN Use256ByteInPkt;
    BOOLEAN Use512ByteInPkt;
    BOOLEAN Use1024ByteInPkt;
    BOOLEAN Use2048ByteInPkt;
    BOOLEAN Use1kByteOutPkt;
    BOOLEAN Use2kByteOutPkt;
    BOOLEAN Use4kByteOutPkt;
    BOOLEAN Use128KReadBuffer;
    BOOLEAN Use256KReadBuffer;
    BOOLEAN NoTimeoutOnCtlReq;
    BOOLEAN RetryOnTxError;
    BOOLEAN UseReadArray;
    BOOLEAN UseMultiWrites;
    BOOLEAN LoggingWriteThrough;
    BOOLEAN EnableLogging;
    BOOLEAN LogLatestPkts;
    BOOLEAN EnableSerialTimeout;
    BOOLEAN EnableZeroLengthPacket;

    USHORT  MinInPktSize;
    ULONG   WriteUnitSize;
    ULONG   InternalReadBufSize;
    ULONG   NumOfRetriesOnError;
    ULONG   MaxPipeXferSize;
    ULONG   NumberOfL2Buffers;
    ULONG   DebugMask;
    UCHAR   DebugLevel;
    ULONG   DriverResident;
    char    PortName[255];
    char    DriverVersion[16];
    ULONG    QcProtocol;
    ULONG   UrbReadErrorMaxLimit;
    ULONG   UrbReadErrorThreshold;
} QCSER_VENDOR_CONFIG;

extern QCSER_VENDOR_CONFIG gVendorConfig;
extern UNICODE_STRING gServicePath;
extern long gDeviceIndex;

#define _zeroUnicode(_a) { \
  (_a).Buffer = NULL; \
  (_a).MaximumLength = 0; \
  (_a).Length = 0; \
}

// Misc
#define QCSER_RESET_RETRIES     3

#ifndef EVENT_TRACING
#define QCSER_DbgPrintG(mask,level,_x_) \
           { \
                 DbgPrint _x_; \
           }

#define QCSER_DbgPrint(mask,level,_x_) \
           { \
              if ((pDevContext->DebugMask & mask) && \
                  (pDevContext->DebugLevel >= level)) \
              { \
                 DbgPrint _x_; \
              } \
           }
#endif

#endif // QCMAIN_H
