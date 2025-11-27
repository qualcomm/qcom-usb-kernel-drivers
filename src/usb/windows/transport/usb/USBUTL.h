/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B U T L . H

GENERAL DESCRIPTION
  This file contains definitions for utility functions.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBUTL_H
#define USBUTL_H

#include "USBMAIN.h"

#define VEN_DRV_SS_IDLE_T   L"QCDriverSelectiveSuspendIdleTime"

typedef struct
{
   PUNICODE_STRING pUs;
   USHORT          usBufsize;
} UNICODE_BUFF_DESC, *PUNICODE_BUFF_DESC;

NTSTATUS GetValueEntry
(
   HANDLE hKey,
   PWSTR FieldName,
   PKEY_VALUE_FULL_INFORMATION  *pKeyValInfo
);

VOID GetDataField_1(PKEY_VALUE_FULL_INFORMATION pKvi, PUNICODE_STRING pUs);
VOID DestroyStrings( IN UNICODE_BUFF_DESC Ubd[] );
BOOLEAN InitStrings( UNICODE_BUFF_DESC Ubd[] );

VOID DebugPrintKeyValues(PKEY_VALUE_FULL_INFORMATION pKeyValueInfo);

NTSTATUS getRegValueEntryData
(
   IN HANDLE OpenRegKey,
   IN PWSTR ValueEntryName,
   OUT PUNICODE_STRING pValueEntryData
);

NTSTATUS getRegDwValueEntryData
(
   IN HANDLE OpenRegKey,
   IN PWSTR ValueEntryName,
   OUT PULONG pValueEntryData
);
NTSTATUS dbgPrintUnicodeString(IN PUNICODE_STRING ToPrint, PCHAR label);
ULONG GetDwordVal(HANDLE hKey, WCHAR *Label, ULONG Len);
ULONG GetDwordField( PKEY_VALUE_FULL_INFORMATION pKvi );
USHORT GetSubUnicodeIndex(PUNICODE_STRING SourceString, PUNICODE_STRING SubString);

NTSTATUS USBUTL_AllocateUnicodeString
(
   PUNICODE_STRING pusString,
   ULONG           ulSize,
   PUCHAR          pucTag
);

NTSTATUS QCUSB_LogData
(
   PDEVICE_EXTENSION pDevExt,
   HANDLE hFile,
   PVOID buffer,
   ULONG length,
   UCHAR type
);
NTSTATUS QCUSB_CreateLogs(PDEVICE_EXTENSION pDevExt, int which);
VOID QCUSB_GetSystemTimeString(char *ts);

VOID USBUTL_Wait(PDEVICE_EXTENSION pDevExt, LONGLONG WaitTime);

VOID QcEmptyCompletionQueue
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY QueueToProcess,
   PKSPIN_LOCK pSpinLock,
   UCHAR rmLockIdx
);
VOID USBUTL_CleanupReadWriteQueues(IN PDEVICE_EXTENSION pDevExt);
VOID USBUTL_PurgeQueue
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY    queue,
   PKSPIN_LOCK    pSpinLock,
   UCHAR          ucType,
   UCHAR          cookie
);
BOOLEAN USBUTL_IsIrpInQueue
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY Queue,
   PIRP        Irp,
   UCHAR       QcType,
   UCHAR       Cookie
);

VOID USBUTL_InitIrpRecord
(
   PQCUSB_IRP_RECORDS Record,
   PIRP               Irp,
   PVOID              Param1,
   PVOID              Param2
);

VOID USBUTL_AddIrpRecordBlock
(
   PQCUSB_IRP_RECORDS RecordHead,
   PQCUSB_IRP_RECORDS Record
);

VOID USBUTL_AddIrpRecord
(
   PQCUSB_IRP_RECORDS RecordHead,
   PIRP               Irp,
   PVOID              Param1,
   PVOID              Param2
);
PQCUSB_IRP_RECORDS USBUTL_RemoveIrpRecord
(
   PQCUSB_IRP_RECORDS RecordHead,
   PIRP Irp
);
PQCUSB_IRP_RECORDS USBUTL_RemoveHead
(
   PQCUSB_IRP_RECORDS RecordHead
);
VOID USBUTL_RemoveAllIrpRecords(PQCUSB_IRP_RECORDS RecordHead);

VOID USBUTL_CompleteRequest
(
    PIRP      Irp,
    NTSTATUS  Status,
    ULONG_PTR Info
);

NTSTATUS USBUTL_IrpCompletionSetEvent
(
    PDEVICE_OBJECT DeviceObject,
    PIRP  Irp,
    PVOID Context
);

VOID USBUTL_PrintBytes
(
   PVOID Buf,
   ULONG Len,
   ULONG PktLen,
   char *info,
   PDEVICE_EXTENSION x,
   ULONG DbgMask,
   ULONG DbgLevel
);

VOID USBUTL_MdlPrintBytes
(
   PMDL  Mdl,
   ULONG printLen,
   char  *info,
   ULONG DbgMask,
   ULONG DbgLevel
);

ULONG USBUTL_GetMdlTotalCount(PMDL Mdl);

VOID USBUTL_LogMdlData
(
   PDEVICE_EXTENSION pDevExt,
   HANDLE            FileHandle,
   PMDL              Mdl,
   UCHAR             Type
);

VOID USBUTL_CheckPurgeStatus(PDEVICE_EXTENSION pDevExt, USHORT newBit);

NTSTATUS USBUTL_NdisConfigurationGetValue
(
   NDIS_HANDLE       ConfigurationHandle,
   UCHAR             EntryIdx,
   PULONG            Value,
   UCHAR             Cookie
);

NTSTATUS USBUTL_NdisConfigurationGetString
(
   NDIS_HANDLE       ConfigurationHandle,
   PDEVICE_EXTENSION pDevExt,
   UCHAR             EntryIdx,
   PUNICODE_STRING   StringText,
   UCHAR             Cookie
);

NTSTATUS USBUTL_NdisConfigurationSetString
(
   NDIS_HANDLE       ConfigurationHandle,  // registry handle
   UCHAR             EntryIdx,             // entry index
   PCHAR             StringText,           // string value to set
   UCHAR             Cookie                // tracking cookie
);

NTSTATUS USBUTL_NdisConfigurationGetBinary
(
   NDIS_HANDLE       ConfigurationHandle,  
   PNDIS_STRING      EntryString,
   PVOID             pData,
   ULONG             Size,
   UCHAR             Cookie
);

NTSTATUS USBUTL_NdisConfigurationSetBinary
(
   NDIS_HANDLE       ConfigurationHandle,  
   PNDIS_STRING      EntryString,
   PVOID             pData,
   ULONG             Size,
   UCHAR             Cookie
);

NTSTATUS
USBUTL_GetServiceRegValue( PWSTR RegPath,PWSTR  ParName, PULONG ParValue);

BOOLEAN USBUTL_IsHighSpeedDevice(PDEVICE_EXTENSION pDevExt);

VOID USBUTL_SetEtherType(PUCHAR Packet);

NTSTATUS USBUTL_DriverRegistrySetDword
(
    PDEVICE_EXTENSION pDevExt,
    PWCHAR pValueName,
    DWORD  value
);

NTSTATUS USBUTL_DriverRegistrySetString
(
    PDEVICE_EXTENSION pDevExt,
    PWCHAR pValueName,
    PVOID  value,
    ULONG  length
);

NTSTATUS USBUTL_DriverRegistryDelete
(
    PDEVICE_EXTENSION pDevExt,
    PWCHAR pValueName
);

#endif // USBUTL_H
