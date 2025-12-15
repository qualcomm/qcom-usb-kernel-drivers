/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B U T L . C

GENERAL DESCRIPTION
    This file contains utility functions.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdio.h>
#include "USBMAIN.h"
#include "USBUTL.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBUTL.tmh"

#endif   // EVENT_TRACING

// The following protypes are implemented in ntoskrnl.lib
extern NTKERNELAPI VOID ExSystemTimeToLocalTime
(
   IN PLARGE_INTEGER SystemTime,
   OUT PLARGE_INTEGER LocalTime
);

#ifdef NDIS_WDM
/***
#define NDIS_STR_DEV_VER     "DriverVersion"
#define NDIS_STR_DEV_CONFIG  "QCDriverConfig"
#define NDIS_STR_LOG_DIR     "QCDriverLoggingDirectory"
#define NDIS_STR_RTY_NUM     "QCDriverRetriesOnError"
#define NDIS_STR_MAX_XFR     "QCDriverMaxPipeXferSize"
#define NDIS_STR_L2_BUFS     "QCDriverL2Buffers"
#define NDIS_STR_DBG_MASK    "QCDriverDebugMask"
#define NDIS_STR_RESIDENT    "QCDriverResident"
#define NDIS_STR_WRITE_UNIT  "QCDriverWriteUnit"
#define NDIS_STR_CTR_FILE    "QCDeviceControlFile"

NDIS_STRING NdisRegString[NDIS_MAX_REG_ENTRY] =
{
   NDIS_STRING_CONST(NDIS_STR_DEV_VER),
   NDIS_STRING_CONST(NDIS_STR_DEV_CONFIG),
   NDIS_STRING_CONST(NDIS_STR_LOG_DIR),
   NDIS_STRING_CONST(NDIS_STR_RTY_NUM),
   NDIS_STRING_CONST(NDIS_STR_MAX_XFR),
   NDIS_STRING_CONST(NDIS_STR_L2_BUFS),
   NDIS_STRING_CONST(NDIS_STR_DBG_MASK),
   NDIS_STRING_CONST(NDIS_STR_RESIDENT),
   NDIS_STRING_CONST(NDIS_STR_WRITE_UNIT)
};
***/

NDIS_STRING NdisRegString[NDIS_MAX_REG_ENTRY] =
{
   NDIS_STRING_CONST("DriverVersion"),
   NDIS_STRING_CONST("QCDriverConfig"),
   NDIS_STRING_CONST("QCDriverLoggingDirectory"),
   NDIS_STRING_CONST("QCDriverRetriesOnError"),
   NDIS_STRING_CONST("QCDriverMaxPipeXferSize"),
   NDIS_STRING_CONST("QCDriverL2Buffers"),
   NDIS_STRING_CONST("QCDriverDebugMask"),
   NDIS_STRING_CONST("QCDriverResident"),
   NDIS_STRING_CONST("QCDriverWriteUnit"),
   NDIS_STRING_CONST("QCDeviceControlFile"),
   NDIS_STRING_CONST("QCDriverThreadPriority")
};
#endif // NDIS_WDM

NTSTATUS GetValueEntry( HANDLE hKey, PWSTR FieldName, PKEY_VALUE_FULL_INFORMATION  *pKeyValInfo )
{
   PKEY_VALUE_FULL_INFORMATION  pKeyValueInfo = NULL;
   UNICODE_STRING Keyname;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   ULONG ulReturnLength = 0;
   ULONG ulBuffLength = 0; 

   _zeroString( Keyname );

   RtlInitUnicodeString(&Keyname,   FieldName);

   _dbgPrintUnicodeString(&Keyname, "Keyname");
   
   ulBuffLength = sizeof( KEY_VALUE_FULL_INFORMATION );

   while (1)
   {
      pKeyValueInfo = (PKEY_VALUE_FULL_INFORMATION) _ExAllocatePool( 
         NonPagedPool, 
         ulBuffLength,
         "GetValueEntry - 2");     
 
      if ( pKeyValueInfo == NULL )
      {
         *pKeyValInfo = NULL;
         ntStatus = STATUS_NO_MEMORY;
         // _failx;
         goto GetValueEntry_Return; 
      }

      ntStatus = ZwQueryValueKey( 
         hKey,
         &Keyname, 
         KeyValueFullInformation, 
         pKeyValueInfo,
         ulBuffLength,
         &ulReturnLength );

      if ( NT_SUCCESS(ntStatus) )
      {
         *pKeyValInfo = pKeyValueInfo;
         break;
      }
      else
      {
         if ( ntStatus == STATUS_BUFFER_OVERFLOW )   
         {
            _freeBuf( pKeyValueInfo );
            ulBuffLength = ulReturnLength;
            continue;
         }
         else
         {
            // _failx;
            _freeBuf( pKeyValueInfo );
            *pKeyValInfo = NULL;
            break;
         }
   
      }
   }// while

GetValueEntry_Return:

  return ntStatus;
   
}


VOID GetDataField_1( PKEY_VALUE_FULL_INFORMATION pKvi,  PUNICODE_STRING pUs )

{
 char *strPtr;
 USHORT len;

 pUs -> Length = 0;
 strPtr = (char *)((PCHAR)pKvi + pKvi -> DataOffset);
 len = (USHORT)pKvi -> DataLength;
 RtlCopyMemory( pUs -> Buffer, strPtr, len );
 pUs -> Length = (len - sizeof( WCHAR ));
}


VOID DestroyStrings( IN UNICODE_BUFF_DESC Ubd[] )
{
   short i;
 
 i = 0;
 do {
     if (Ubd[i].pUs -> Buffer != NULL)
       _ExFreePool( Ubd[i].pUs -> Buffer );
     Ubd[i].pUs -> Buffer = NULL; //in case this table is passed in twice
     i++;
    }while (Ubd[i].pUs != NULL);
}

BOOLEAN InitStrings( UNICODE_BUFF_DESC Ubd[] )
{
 PUNICODE_BUFF_DESC pUnbufdesc;
 PUNICODE_STRING pUs;
 PVOID pBuf;
 short i;
 
 i = 0;
 do {
     Ubd[i].pUs -> Buffer = NULL;
     i++;
    } while (Ubd[i].pUs != NULL);
 i = 0;
 do {
     pBuf = _ExAllocatePool( NonPagedPool, Ubd[i].usBufsize << 1,"InitStrings()" ); //wchar to bytes
     if (pBuf == NULL)
       {
        DestroyStrings( Ubd );
       return FALSE;
      }
     Ubd[i].pUs -> Buffer = pBuf;
     Ubd[i].pUs -> MaximumLength = Ubd[i].usBufsize << 1;
     Ubd[i].pUs -> Length = 0;
     i++;
    }while (Ubd[i].pUs != NULL);
 return TRUE;
}
     

VOID DebugPrintKeyValues( PKEY_VALUE_FULL_INFORMATION pKeyValueInfo )

{
   char *strPtr;
   ULONG len, i, j;
   char cBuf[264];
 
   strPtr = (char *)pKeyValueInfo -> Name;    
   len = (pKeyValueInfo -> NameLength) >> 1;
   for (i = j = 0; i < len; i++, j++)
   {
      cBuf[i] = strPtr[j];
      j++;
   }
   cBuf[i] = '\0';    
   strPtr = (PCHAR)((PCHAR)pKeyValueInfo + pKeyValueInfo -> DataOffset);
   len = (pKeyValueInfo -> DataLength) >> 1;
   for (i = j = 0; i < len; i++, j++)
   {
      cBuf[i] = strPtr[j];
      j++;
   }
   cBuf[i] = '\0';    
}

/******************************************************************************************
* Function Name: getRegValueEntryData
* Arguments:
* 
*  IN HANDLE OpenRegKey, 
*  IN PWSTR ValueEntryName,
*  OUT PUNICODE_STRING ValueEntryData
*
* notes:
*   Buffer for valueEntryData is allocated in this function.
*******************************************************************************************/
NTSTATUS getRegValueEntryData
(
   IN HANDLE OpenRegKey, 
   IN PWSTR ValueEntryName,
   OUT PUNICODE_STRING pValueEntryData
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   PKEY_VALUE_FULL_INFORMATION pKeyValueInfo = NULL;
   UNICODE_STRING ucData;

   _zeroString( ucData );
   _zeroStringPtr( pValueEntryData );

   ntStatus = GetValueEntry( OpenRegKey, ValueEntryName, &pKeyValueInfo );
   
   if ( !NT_SUCCESS( ntStatus ))
   {
      // _failx;
      goto getRegValueEntryData_Return;
   }

#ifdef DEBUG_MSGS_KEYINFO
   if (pKeyValueInfo)
      DebugPrintKeyValues( pKeyValueInfo );
#endif

   ucData.Buffer = (PWSTR) _ExAllocatePool( 
         NonPagedPool,
         MAX_ENTRY_DATA_LEN,
         "getRegValueEntryData, ucData.Buffer");    

   if ( ucData.Buffer == NULL )
   {
      ntStatus = STATUS_NO_MEMORY;
      // _failx;
      goto getRegValueEntryData_Return;
   }

   ucData.MaximumLength = MAX_ENTRY_DATA_LEN;

   GetDataField_1( pKeyValueInfo, &ucData );

   pValueEntryData -> Buffer = (PWSTR) _ExAllocatePool( 
         NonPagedPool,
         MAX_ENTRY_DATA_LEN,
         "getRegValueEntryData, pValueEntryData -> Buffer");    

   if ( pValueEntryData -> Buffer == NULL )
   {
      ntStatus = STATUS_NO_MEMORY;
      // _failx;
      goto getRegValueEntryData_Return;
   }

   pValueEntryData -> MaximumLength = MAX_ENTRY_DATA_LEN;
    

   _dbgPrintUnicodeString(&ucData, "ucData");
   RtlCopyUnicodeString(OUT pValueEntryData,IN &ucData);
   _dbgPrintUnicodeString(pValueEntryData, "(after) pValueEntryData");

getRegValueEntryData_Return:

    _freeBuf(pKeyValueInfo);
    _freeUcBuf(ucData);
    return ntStatus;   

}


/******************************************************************************************
* Function Name: getRegDwValueEntryData
* Arguments:
* 
*  IN HANDLE OpenRegKey, 
*  IN PWSTR ValueEntryName,
*  OUT PULONG pValueEntryData
*
*******************************************************************************************/
NTSTATUS getRegDwValueEntryData
(
   IN HANDLE OpenRegKey, 
   IN PWSTR ValueEntryName,
   OUT PULONG pValueEntryData
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   PKEY_VALUE_FULL_INFORMATION pKeyValueInfo = NULL;

   ntStatus = GetValueEntry( OpenRegKey, ValueEntryName, &pKeyValueInfo );
   
   if (!NT_SUCCESS( ntStatus ))
   {
      goto getRegDwValueEntryData_Return;
   }
               
#ifdef DEBUG_MSGS_KEYINFO
   if (pKeyValueInfo)
      DebugPrintKeyValues( pKeyValueInfo );
#endif

   *pValueEntryData = GetDwordField( pKeyValueInfo );

getRegDwValueEntryData_Return:

    if (pKeyValueInfo)
    {
       _ExFreePool( pKeyValueInfo );
       pKeyValueInfo = NULL;
    }

   return ntStatus;   
}


NTSTATUS dbgPrintUnicodeString(IN PUNICODE_STRING ToPrint, PCHAR label)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   ANSI_STRING asToPrint;

   #ifdef DEBUG_MSGS

   ntStatus = RtlUnicodeStringToAnsiString( 
      &asToPrint,                                   
      ToPrint, 
      TRUE);     

   if ( !NT_SUCCESS( ntStatus ) )
   {
      // _failx;
      goto dbgPrintUnicodeString_Return;
   }

   // DbgPrint(" Value: %s\n", asToPrint.Buffer);

dbgPrintUnicodeString_Return:

   // _freeString(asToPrint);  // or:
   RtlFreeAnsiString(&asToPrint);

   #endif  // DEBUG_MSGS

   return ntStatus;
}


/************************************************************************
   Retrieve DWORD value from registry
************************************************************************/      
ULONG GetDwordVal( HANDLE hKey, WCHAR *Label, ULONG Len )
{
   PKEY_VALUE_FULL_INFORMATION pKeyValueInfo = NULL;
   NTSTATUS ntStatus;
   ULONG dwVal = 0;

   ntStatus = GetValueEntry( hKey, Label, &pKeyValueInfo );
   if (NT_SUCCESS( ntStatus ))
   {
      dwVal = GetDwordField( pKeyValueInfo );
      _ExFreePool( pKeyValueInfo );
   }
   return dwVal;
}
 
ULONG GetDwordField( PKEY_VALUE_FULL_INFORMATION pKvi )
{
 ULONG dwVal, *pVal;
 
 pVal = (ULONG *)((PCHAR)pKvi + pKvi -> DataOffset);
 dwVal = *pVal;
 return dwVal;
}

/*********************************************************************
 *
 * function:   GetSubUnicodeIndex
 *
 * purpose:    search a unicode source string for a unicode sub string
 *             and return the index into the source string of the sub string
 *             if the sub string is found.  Return zero if not found.  Return
 *             -1 if one or both of the arguments are invalid.
 *
 * returns:    char index
 *
 */

USHORT GetSubUnicodeIndex(PUNICODE_STRING SourceString, PUNICODE_STRING SubString) 
{
   PWCHAR pSource, pSub;
   USHORT SourceCharIndex, SourceCharLen, SubCharLen, SubIndex;

   SourceCharLen = SourceString->Length >> 1;
   SubCharLen = SubString->Length >> 1;

   if ( (SourceCharLen == 0) || (SubCharLen == 0) ) return 0xFFFF; // invalid args

   SourceCharIndex = 0;
   
   while ( SourceCharIndex + SubCharLen < SourceCharLen )
   {
      pSub = SubString->Buffer;
      pSource = SourceString->Buffer + SourceCharIndex; // init to place in source we
                                               // are testing.
      SubIndex = SubString->Length >> 1;

      while ( (*pSource++ == *pSub++) && SubIndex-- );
      
      if ( !SubIndex ) return SourceCharIndex; // found sub in source because
                                               // subindex went to zero.
      SourceCharIndex++;
   }
   return 0;
} // end of GetSubUnicodeIndex function

NTSTATUS USBUTL_AllocateUnicodeString(PUNICODE_STRING pusString, ULONG ulSize, PUCHAR pucTag)
{
   pusString->Buffer = (PWSTR) _ExAllocatePool( NonPagedPool, ulSize, pucTag );
   if (pusString->Buffer == NULL)
   {
     return STATUS_NO_MEMORY;
   }   
   pusString->MaximumLength = ulSize;
   return STATUS_SUCCESS;
}

#ifdef ENABLE_LOGGING
NTSTATUS QCUSB_LogData
(
   PDEVICE_EXTENSION pDevExt,
   HANDLE hFile,
   PVOID buffer,
   ULONG length,
   UCHAR type
)
{
   NTSTATUS        ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER   systemTime;
   IO_STATUS_BLOCK ioStatus;

   if ((pDevExt->EnableLogging == FALSE) || (hFile == NULL))
   {
      return ntStatus;
   }

   // This function must be running at IRQL PASSIVE_LEVEL
   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      return STATUS_CANCELLED;
   }

   if (type == QCUSB_LOG_TYPE_READ)
   {
      if (pDevExt->ulRxLogCount != pDevExt->ulLastRxLogCount)
      {
         type = QCUSB_LOG_TYPE_OUT_OF_SEQUENCE_RD;
      }
      pDevExt->ulRxLogCount++;
   }
   else if (type == QCUSB_LOG_TYPE_WRITE)
   {
      if (pDevExt->ulTxLogCount != pDevExt->ulLastTxLogCount)
      {
         type = QCUSB_LOG_TYPE_OUT_OF_SEQUENCE_WT;
      }
      pDevExt->ulTxLogCount++;
   }
   else if (type == QCUSB_LOG_TYPE_RESEND)
   {
      if (pDevExt->ulTxLogCount != pDevExt->ulLastTxLogCount)
      {
         type = QCUSB_LOG_TYPE_OUT_OF_SEQUENCE_RS;
      }
      pDevExt->ulTxLogCount++;
   }
      
   KeQuerySystemTime(&systemTime);

   // Log system time
   ntStatus = ZwWriteFile
              (
                 hFile,
                 NULL,  // Event
                 NULL,  // ApcRoutine
                 NULL,  // ApcContext
                 &ioStatus,
                 (PVOID)&systemTime,
                 sizeof(LARGE_INTEGER),
                 NULL,  // ByteOffset
                 NULL
              );
   // log the logging type
   ntStatus = ZwWriteFile
              (
                 hFile,
                 NULL,  // Event
                 NULL,  // ApcRoutine
                 NULL,  // ApcContext
                 &ioStatus,
                 (PVOID)&type,
                 sizeof(UCHAR),
                 NULL,  // ByteOffset
                 NULL
              );
   // log data length
   ntStatus = ZwWriteFile
              (
                 hFile,
                 NULL,  // Event
                 NULL,  // ApcRoutine
                 NULL,  // ApcContext
                 &ioStatus,
                 (PVOID)&length,
                 sizeof(ULONG),
                 NULL,  // ByteOffset
                 NULL
              );
   // log data
   if (length != 0)
   {
      ntStatus = ZwWriteFile
                 (
                    hFile,
                    NULL,  // Event
                    NULL,  // ApcRoutine
                    NULL,  // ApcContext
                    &ioStatus,
                    buffer,
                    length,
                    NULL,  // ByteOffset
                    NULL
                 );
   }

   if (type == QCUSB_LOG_TYPE_READ)
   {
      pDevExt->ulLastRxLogCount++;
   }
   else if ((type == QCUSB_LOG_TYPE_WRITE) || (type == QCUSB_LOG_TYPE_RESEND))
   {
      pDevExt->ulLastTxLogCount++;
   }

   return ntStatus;
}  // QCUSB_LogData

NTSTATUS QCUSB_CreateLogs(PDEVICE_EXTENSION pDevExt, int which)
{
   NTSTATUS          ntStatus = STATUS_SUCCESS;
   OBJECT_ATTRIBUTES objectAttr;
   IO_STATUS_BLOCK   ioStatus;
   LARGE_INTEGER     systemTime, localTime;
   TIME_FIELDS       timeFields;
   ANSI_STRING       tmpAnsiString, asStr2;
   char              txFileName[48];
   char              rxFileName[48];
   char              *p, cNetLogPrefix[8];
   UNICODE_STRING    ucTxFileName, ucRxFileName, ucTxTmp, ucRxTmp;

   /* ---------- References ------------
    typedef struct _TIME_FIELDS {
        CSHORT Year;        // range [1601...]
        CSHORT Month;       // range [1..12]
        CSHORT Day;         // range [1..31]
        CSHORT Hour;        // range [0..23]
        CSHORT Minute;      // range [0..59]
        CSHORT Second;      // range [0..59]
        CSHORT Milliseconds;// range [0..999]
        CSHORT Weekday;     // range [0..6] == [Sunday..Saturday]
    } TIME_FIELDS;
    -------------------------------------*/

   if (pDevExt->bLoggingOk == FALSE)
   {
      return STATUS_UNSUCCESSFUL;
   }

   sprintf(cNetLogPrefix, "\\Net");

   // If necessary to put a leading '\' to a log file name
   ntStatus = RtlUnicodeStringToAnsiString
              (
                 &asStr2,
                 &pDevExt->ucLoggingDir,
                 TRUE
              );

   if ( ntStatus == STATUS_SUCCESS )
   {
      p = asStr2.Buffer + asStr2.Length - 1;
      if (*p == '\\')
      {
         sprintf(cNetLogPrefix, "Net");
      }
   }

   KeQuerySystemTime(&systemTime);
   ExSystemTimeToLocalTime(&systemTime, &localTime);
   RtlTimeToTimeFields(&localTime, &timeFields);

   if (pDevExt->ucModelType == MODELTYPE_NET)
   {
      sprintf(
                txFileName,
                "%sTx%02u%02u%02u%02u%02u%03u.log",
                cNetLogPrefix,
                timeFields.Month,
                timeFields.Day,
                timeFields.Hour,
                timeFields.Minute,
                timeFields.Second,
                timeFields.Milliseconds
             );
      sprintf(
                rxFileName,
                "%sRx%02u%02u%02u%02u%02u%03u.log",
                cNetLogPrefix,
                timeFields.Month,
                timeFields.Day,
                timeFields.Hour,
                timeFields.Minute,
                timeFields.Second,
                timeFields.Milliseconds
             );
   }

   ntStatus = USBUTL_AllocateUnicodeString
              (
                 &ucTxFileName,
                 MAX_NAME_LEN,
                 "QCUSB_CreateLogFile, ucTxFileName.Buffer"
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> NO_MEM - TxFileName\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      return ntStatus;
   }
   ntStatus = USBUTL_AllocateUnicodeString
              (
                 &ucRxFileName,
                 MAX_NAME_LEN,
                 "QCUSB_CreateLogFile, ucRxFileName.Buffer"
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> NO_MEM - RxFileName\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
      return ntStatus;
   }

   RtlCopyUnicodeString(OUT &ucTxFileName,IN &pDevExt->ucLoggingDir);
   RtlCopyUnicodeString(OUT &ucRxFileName,IN &pDevExt->ucLoggingDir);

   RtlInitAnsiString(&tmpAnsiString, txFileName);
   ntStatus = RtlAnsiStringToUnicodeString(&ucTxTmp, &tmpAnsiString, TRUE);
   if (ntStatus != STATUS_SUCCESS)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> Error - ucTxTmp\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
      _freeUcBuf(ucRxFileName); // RtlFreeUnicodeString(&ucRxFileName);
      return ntStatus;
   }

   ntStatus = RtlAppendUnicodeStringToString
              (
                 &ucTxFileName,
                 &ucTxTmp
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> SMALL_BUF - TxFileName\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      RtlFreeUnicodeString(&ucTxTmp);
      _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
      _freeUcBuf(ucRxFileName); // RtlFreeUnicodeString(&ucRxFileName);
      return ntStatus;
   }
   RtlFreeUnicodeString(&ucTxTmp);

   RtlInitAnsiString(&tmpAnsiString, rxFileName);
   ntStatus = RtlAnsiStringToUnicodeString(&ucRxTmp, &tmpAnsiString, TRUE);
   if (ntStatus != STATUS_SUCCESS)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> Error - ucRxTmp\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      RtlFreeUnicodeString(&ucTxTmp);
      _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
      _freeUcBuf(ucRxFileName); // RtlFreeUnicodeString(&ucRxFileName);
      return ntStatus;
   }
   ntStatus = RtlAppendUnicodeStringToString
              (
                 &ucRxFileName,
                 &ucRxTmp
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> SMALL_BUF - RxFileName\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      RtlFreeUnicodeString(&ucTxTmp);
      RtlFreeUnicodeString(&ucRxTmp);
      _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
      _freeUcBuf(ucRxFileName); // RtlFreeUnicodeString(&ucRxFileName);
      return ntStatus;
   }
   RtlFreeUnicodeString(&ucRxTmp);

   _dbgPrintUnicodeString(&ucTxFileName, "ucTxFileName");
   _dbgPrintUnicodeString(&ucRxFileName, "ucRxFileName");

   if ((which == QCUSB_CREATE_TX_LOG) && (pDevExt->hTxLogFile == NULL))
   {
      pDevExt->ulLastTxLogCount = pDevExt->ulTxLogCount = 0;

      // Create disk log files
      InitializeObjectAttributes
      (
         &objectAttr,
         &ucTxFileName,
         OBJ_CASE_INSENSITIVE,
         NULL,
         NULL
      );

      ntStatus = ZwCreateFile
                 (
                    &pDevExt->hTxLogFile,
                    FILE_GENERIC_WRITE | SYNCHRONIZE,
                    &objectAttr,
                    &ioStatus,
                    0,                                  // AllocationSize
                    FILE_ATTRIBUTE_NORMAL,              // FileAttributes
                    FILE_SHARE_READ | FILE_SHARE_WRITE, // ShareAccess
                    FILE_OPEN_IF,                       // CreateDisposition
                    FILE_SYNCHRONOUS_IO_NONALERT,       // CreateOptions
                    NULL,
                    0
                 );
      if (ntStatus != STATUS_SUCCESS)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> File creation failure: TxFileName\n", pDevExt->PortName)
         );
        pDevExt->hTxLogFile = NULL;
      }
   }

   if ((which == QCUSB_CREATE_RX_LOG) && (pDevExt->hRxLogFile == NULL))
   {
      pDevExt->ulLastRxLogCount = pDevExt->ulRxLogCount = 0;

      InitializeObjectAttributes
      (
         &objectAttr,
         &ucRxFileName,
         OBJ_CASE_INSENSITIVE,
         NULL,
         NULL
      );

      ntStatus = ZwCreateFile
                 (
                    &pDevExt->hRxLogFile,
                    FILE_GENERIC_WRITE | SYNCHRONIZE,
                    &objectAttr,
                    &ioStatus,
                    0,                     // AllocationSize
                    FILE_ATTRIBUTE_NORMAL, // FileAttributes
                    FILE_SHARE_READ | FILE_SHARE_WRITE,  // ShareAccess
                    FILE_OPEN_IF,          // CreateDisposition
                    FILE_SYNCHRONOUS_IO_NONALERT, // CreateOptions
                    NULL,
                    0
                 );
      if (ntStatus != STATUS_SUCCESS)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> File creation failure: RxFileName\n", pDevExt->PortName)
         );
        pDevExt->hRxLogFile = NULL;
      }
   }

   RtlFreeAnsiString(&asStr2);
   _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
   _freeUcBuf(ucRxFileName); // RtlFreeUnicodeString(&ucRxFileName);

   return ntStatus;

}  // QCUSB_CreateLogs

#endif // ENABLE_LOGGING

VOID QCUSB_GetSystemTimeString(char *ts)
{
   LARGE_INTEGER systemTime, localTime;
   TIME_FIELDS   timeFields;

   KeQuerySystemTime(&systemTime);
   ExSystemTimeToLocalTime(&systemTime, &localTime);
   RtlTimeToTimeFields(&localTime, &timeFields);

   sprintf(
             ts,
             "%02u/%02u/%02u:%02u:%02u:%03u",
             timeFields.Month,
             timeFields.Day,
             timeFields.Hour,
             timeFields.Minute,
             timeFields.Second,
             timeFields.Milliseconds
          );
}

VOID USBUTL_Wait(PDEVICE_EXTENSION pDevExt, LONGLONG WaitTime)
{
   LARGE_INTEGER delayValue;

   delayValue.QuadPart = WaitTime;
   KeClearEvent(&pDevExt->ForTimeoutEvent);
   KeWaitForSingleObject
   (
      &pDevExt->ForTimeoutEvent,
      Executive,
      KernelMode,
      FALSE,
      &delayValue
   );
   KeClearEvent(&pDevExt->ForTimeoutEvent);

   return;
}

VOID QcEmptyCompletionQueue
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY QueueToProcess,
   PKSPIN_LOCK pSpinLock,
   UCHAR rmLockIdx
)
{
   PIRP pIrp;
   PLIST_ENTRY headOfList;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   ULONG debugLevel;

   while (TRUE)
   {
      QcAcquireSpinLock(pSpinLock, &levelOrHandle);
      if (!IsListEmpty(QueueToProcess))
      {
         headOfList = RemoveHeadList(QueueToProcess);
         pIrp = CONTAINING_RECORD
                (
                   headOfList,
                   IRP,
                   Tail.Overlay.ListEntry
                );
         QcReleaseSpinLock(pSpinLock, levelOrHandle);

         if (NT_SUCCESS(pIrp->IoStatus.Status))
         {
            debugLevel = QCUSB_DBG_LEVEL_DETAIL;
         }
         else
         {
            debugLevel = QCUSB_DBG_LEVEL_ERROR;
         }
         if (rmLockIdx == QCUSB_IRP_TYPE_CIRP)
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CIRP,
               debugLevel,
               ("<%s> CIRP (Cq 0x%x) 0x%p\n", pDevExt->PortName, pIrp->IoStatus.Status, pIrp) 
            );
         }
         else if (rmLockIdx == QCUSB_IRP_TYPE_RIRP)
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_RIRP,
               debugLevel,
               ("<%s> RIRP (Cq 0x%x/%ldB) 0x%p RmlCount[0]=%u\n",
                 pDevExt->PortName, pIrp->IoStatus.Status, pIrp->IoStatus.Information, pIrp,
                 pDevExt->Sts.lRmlCount[0]) 
            );
         }
         else if (rmLockIdx == QCUSB_IRP_TYPE_WIRP)
         {
            PQCUSB_IRP_RECORDS irpRec;

            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WIRP,
               debugLevel,
               ("<%s> WIRP (Cq 0x%x/%ldB) 0x%p\n", 
                 pDevExt->PortName, pIrp->IoStatus.Status, pIrp->IoStatus.Information, pIrp) 
            );
            irpRec = USBUTL_RemoveIrpRecord
                     (
                        pDevExt->WriteCancelRecords,
                        pIrp
                     );
            if (irpRec != NULL)
            {
               _ExFreePool(irpRec);
            }
         }
         else
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               debugLevel,
               ("<%s> UIRP (Cq 0x%x) 0x%p\n", pDevExt->PortName, pIrp->IoStatus.Status, pIrp) 
            );
         }

         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, rmLockIdx);
         IoCompleteRequest(pIrp, IO_NO_INCREMENT);
      }
      else
      {
         QcReleaseSpinLock(pSpinLock, levelOrHandle);
         return;
      }
   } // while
}  // QcEmptyCompletionQueue

VOID USBUTL_CleanupReadWriteQueues(IN PDEVICE_EXTENSION pDevExt)
{
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint2
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> _CleanupReadWriteQueues: enter\n", pDevExt->PortName)
   );

   // clean read/write queue
   USBUTL_PurgeQueue
   (
      pDevExt,
      &pDevExt->ReadDataQueue,
      &pDevExt->ReadSpinLock,
      QCUSB_IRP_TYPE_RIRP,
      1
   );
   USBUTL_PurgeQueue
   (
      pDevExt,
      &pDevExt->WriteDataQueue,
      &pDevExt->WriteSpinLock,
      QCUSB_IRP_TYPE_WIRP,
      1
   );
   USBUTL_PurgeQueue
   (
      pDevExt,
      &pDevExt->DispatchQueue,
      &pDevExt->ControlSpinLock,
      QCUSB_IRP_TYPE_CIRP,
      1
   );
   USBUTL_PurgeQueue
   (
      pDevExt,
      &pDevExt->EncapsulatedDataQueue,
      &pDevExt->ControlSpinLock,
      QCUSB_IRP_TYPE_CIRP,
      2
   );

   QCUSB_DbgPrint2
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> _CleanupReadWriteQueues: exit\n", pDevExt->PortName)
   );
}  // USBUTL_CleanupReadWriteQueues

VOID USBUTL_PurgeQueue
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY    queue,
   PKSPIN_LOCK    pSpinLock,
   UCHAR          ucType,
   UCHAR          cookie
)
{
   PLIST_ENTRY headOfList;
   PIRP pIrp;
   BOOLEAN bDone = FALSE;
   BOOLEAN bQueued = FALSE;
   PCHAR pcIrpName;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   switch (ucType)
   {
      case QCUSB_IRP_TYPE_CIRP:
         pcIrpName = "CIRP";
         break;
      case QCUSB_IRP_TYPE_RIRP:
         pcIrpName = "RIRP";
         break;
      case QCUSB_IRP_TYPE_WIRP:
         pcIrpName = "WIRP";
         break;
      default:
         pcIrpName = "XIRP";
         break;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> PurgeQueue: %s - %d\n", pDevExt->PortName, pcIrpName, cookie)
   );

   QcAcquireSpinLock(pSpinLock, &levelOrHandle);

   while (bDone == FALSE)
   {
      if (!IsListEmpty(queue))
      {
         headOfList = RemoveHeadList(queue);
         pIrp =  CONTAINING_RECORD
                 (
                    headOfList,
                    IRP,
                    Tail.Overlay.ListEntry
                 );
         if (pIrp == NULL)
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> PurgeQueue: %s - %d NUL\n", pDevExt->PortName, pcIrpName, cookie)
            );
            continue;
         }
         if (IoSetCancelRoutine(pIrp, NULL) != NULL)
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CIRP,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> %s: (Cx 0x%p)\n", pDevExt->PortName, pcIrpName, pIrp)
            );
            pIrp->IoStatus.Status = STATUS_CANCELLED;
            pIrp->IoStatus.Information = 0;

            switch (ucType)
            {
               case QCUSB_IRP_TYPE_CIRP:
                  InsertTailList(&pDevExt->CtlCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
                  break;
               case QCUSB_IRP_TYPE_RIRP:
                  InsertTailList(&pDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
                  break;
               case QCUSB_IRP_TYPE_WIRP:
                  InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
                  break;
               default:
                   IoCompleteRequest(pIrp, IO_NO_INCREMENT);
                  break;
            }

            bQueued = TRUE;
         }
         else
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> PurgeQueue: Cxled %s 0x%p\n", pDevExt->PortName, pcIrpName, pIrp)
            );
         }
      }
      else
      {
         bDone = TRUE;
      }
   }  // while

   if (bQueued == TRUE)
   {
      switch (ucType)
      {
         case QCUSB_IRP_TYPE_CIRP:
            KeSetEvent(&pDevExt->InterruptEmptyCtlQueueEvent, IO_NO_INCREMENT, FALSE);
            break;
         case QCUSB_IRP_TYPE_RIRP:
            KeSetEvent(&pDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
            break;
         case QCUSB_IRP_TYPE_WIRP:
            KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);
            break;
         default:
            break;
      }
   }

   QcReleaseSpinLock(pSpinLock, levelOrHandle);

   return;
}  // USBUTL_PurgeQueue

BOOLEAN USBUTL_IsIrpInQueue
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY Queue,
   PIRP        Irp,
   UCHAR       ucType,
   UCHAR       Cookie
)
{
   PLIST_ENTRY headOfList, peekEntry;
   PIRP pIrp;
   BOOLEAN bInQueue = FALSE;
   PCHAR pcIrpName;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   switch (ucType)
   {
      case QCUSB_IRP_TYPE_CIRP:
         pcIrpName = "CIRP";
         break;
      case QCUSB_IRP_TYPE_RIRP:
         pcIrpName = "RIRP";
         break;
      case QCUSB_IRP_TYPE_WIRP:
         pcIrpName = "WIRP";
         break;
      default:
         pcIrpName = "XIRP";
         break;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> IsIrpInQueue: %s - %d\n", pDevExt->PortName, pcIrpName, Cookie)
   );

   if (!IsListEmpty(Queue))
   {
      headOfList = Queue;
      peekEntry = headOfList->Flink;

      while (peekEntry != headOfList)
      {
         pIrp =  CONTAINING_RECORD
                 (
                    peekEntry,
                    IRP,
                    Tail.Overlay.ListEntry
                 );

         QCUSB_DbgPrint2
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> IsIrpInQueue<%d>: exam (0x%p, 0x%p)\n", pDevExt->PortName,
               Cookie, pIrp, Irp)
         );
         if (pIrp == Irp)
         {
            bInQueue = TRUE;
            break;
         }

         peekEntry = peekEntry->Flink;
      }
   }

   return bInQueue;

}  // USBUTL_IsIrpInQueue

BOOLEAN USBUTL_IsIrpInQueue2
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY Queue,
   PIRP        Irp,
   UCHAR       ucType,
   UCHAR       Cookie
)
{
   PLIST_ENTRY queue, listHead, item;
   PIRP pIrp;
   PCHAR pcIrpName;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   switch (ucType)
   {
      case QCUSB_IRP_TYPE_CIRP:
         pcIrpName = "CIRP";
         break;
      case QCUSB_IRP_TYPE_RIRP:
         pcIrpName = "RIRP";
         break;
      case QCUSB_IRP_TYPE_WIRP:
         pcIrpName = "WIRP";
         break;
      default:
         pcIrpName = "XIRP";
         break;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> IsIrpInQueue: %s - %d\n", pDevExt->PortName, pcIrpName, Cookie)
   );

   if (IsListEmpty(Queue))
   {
      return FALSE;
   }

   item = listHead = Queue;
   while ((item)->Flink != listHead)
   {
      pIrp =  CONTAINING_RECORD
              (
                 item,
                 IRP,
                 Tail.Overlay.ListEntry
              );
      QCUSB_DbgPrint2
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> IsIrpInQueue<%d>: exam (0x%p, 0x%p)\n", pDevExt->PortName,
            Cookie, pIrp, Irp)
      );
      if (pIrp == Irp)
      {
         return TRUE;
      }
      item = (item)->Flink;
   }

   return FALSE;

}  // USBUTL_IsIrpInQueue2

VOID USBUTL_InitIrpRecord
(
   PQCUSB_IRP_RECORDS Record,
   PIRP               Irp,
   PVOID              Param1,
   PVOID              Param2
)
{
   Record->Irp    = Irp;
   Record->Param1 = Param1;
   Record->Param2 = Param2;
   Record->Next   = NULL;
}

VOID USBUTL_AddIrpRecordBlock
(
   PQCUSB_IRP_RECORDS RecordHead,
   PQCUSB_IRP_RECORDS Record
)
{
   PQCUSB_IRP_RECORDS rec;

   if (RecordHead == NULL)
   {
      RecordHead = Record;
      return;
   }
   
   rec = RecordHead;
   while (rec->Next != NULL)
   {
      rec = rec->Next;
   }
   rec->Next = Record;
}

VOID USBUTL_AddIrpRecord
(
   PQCUSB_IRP_RECORDS RecordHead,
   PIRP               Irp,
   PVOID              Param1,
   PVOID              Param2
)
{
   PQCUSB_IRP_RECORDS rec;

   if (RecordHead == NULL)
   {
      RecordHead = _ExAllocatePool
                   (
                      NonPagedPool,
                      sizeof(QCUSB_IRP_RECORDS),
                      "Irp_Rec"
                   );
      if (RecordHead == NULL)
      {
         return;
      }
      RecordHead->Irp    = Irp;
      RecordHead->Next   = NULL;
      RecordHead->Param1 = Param1;
      RecordHead->Param2 = Param2;
      return;
   }
 
   rec = RecordHead;
   while (rec->Next != NULL)
   {
      rec = rec->Next;
   }
   rec->Next = _ExAllocatePool
               (
                  NonPagedPool,
                  sizeof(QCUSB_IRP_RECORDS),
                  "Irp_Rec"
               );
   if (rec->Next == NULL)
   {
      return;
   }
   rec = rec->Next;
   rec->Irp    = Irp;
   rec->Param1 = Param1;
   rec->Param2 = Param2;
   rec->Next   = NULL;

   return;
}  //USBUTL_AddIrpRecord

PQCUSB_IRP_RECORDS USBUTL_RemoveIrpRecord
(
   PQCUSB_IRP_RECORDS RecordHead,
   PIRP Irp
)
{
   PQCUSB_IRP_RECORDS rec = NULL, rec1;

   if ((Irp != NULL) && (rec1 = RecordHead) != NULL)
   {
      if (rec1->Irp == Irp)
      {
         rec = rec1;
         RecordHead = RecordHead->Next;
      }
      else
      {
         while (rec1->Next != NULL)
         {
            if (rec1->Next->Irp == Irp)
            {
               rec = rec1->Next;
               rec1->Next = rec->Next;
               break;
            }
            rec1 = rec1->Next;
         }
      }
   }

   return rec;
}  // USBUTL_RemoveRecord

PQCUSB_IRP_RECORDS USBUTL_RemoveHead
(
   PQCUSB_IRP_RECORDS RecordHead
)
{
   PQCUSB_IRP_RECORDS rec = RecordHead;

   if (rec != NULL)
   {
      RecordHead = RecordHead->Next;
   }
   return rec;
}  // USBUTL_RemoveHead

VOID USBUTL_RemoveAllIrpRecords(PQCUSB_IRP_RECORDS RecordHead)
{
   PQCUSB_IRP_RECORDS rec;

   while ((rec = RecordHead) != NULL)
   {
      RecordHead = RecordHead->Next;
      _ExFreePool(rec);
   }
}  // USBUTL_RemoveAllRecords

VOID USBUTL_CompleteRequest
(
    PIRP      Irp,
    NTSTATUS  Status,
    ULONG_PTR Info
)
{
    if (Irp != NULL)
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = Info;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        QCUSB_DbgPrintG
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> USBUTL_CompleteRequest: IRP 0x%p\n", gDeviceName, Irp)
        );
    }
}

NTSTATUS USBUTL_IrpCompletionSetEvent
(
    PDEVICE_OBJECT DeviceObject,
    PIRP  Irp,
    PVOID Context
)
{
    PKEVENT pEvent = (PKEVENT)Context;

    if (Irp->PendingReturned)
    {
        _IoMarkIrpPending(Irp);
    }

    if (pEvent != NULL)
    {
        KeSetEvent(pEvent, IO_NO_INCREMENT, FALSE);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID USBUTL_PrintBytes
(
   PVOID Buf,
   ULONG len,
   ULONG PktLen,
   char *info,
   PDEVICE_EXTENSION x,
   ULONG DbgMask,
   ULONG DbgLevel
)
{
   #ifdef QCUSB_DBGPRINT_BYTES

   ULONG nWritten;
   char  *buf, *p, *cBuf, *cp;
   char *buffer;
   ULONG count = 0, lastCnt = 0, spaceNeeded;
   ULONG i, j, s;
   ULONG nts;
   PCHAR dbgOutputBuffer;
   ULONG myTextSize = 1280;

   #define SPLIT_CHAR '|'

   if (x != NULL)
   {
#ifdef EVENT_TRACING
      if (!((x->DebugMask !=0) && (QCWPP_USER_FLAGS(WPP_DRV_MASK_CONTROL) & DbgMask) && 
            (QCWPP_USER_LEVEL(WPP_DRV_MASK_CONTROL) >= DbgLevel))) 
   
      {
         return;
      }
#else
      if (((x->DebugMask & DbgMask) == 0) || (x->DebugLevel < DbgLevel))
      {
         return;
      }
#endif
   }

   // re-calculate text buffer size
   if (myTextSize < (len * 5 +360))
   {
      myTextSize = len * 5 +360;
   }

   buffer = (char *)Buf;

   dbgOutputBuffer = ExAllocatePool(NonPagedPool, myTextSize);
   if (dbgOutputBuffer == NULL)
   {
      return;
   }

   RtlZeroMemory(dbgOutputBuffer, myTextSize);
   cBuf = dbgOutputBuffer;
   buf  = dbgOutputBuffer + 128;
   p    = buf;
   cp   = cBuf;

   if (PktLen < len)
   {
      len = PktLen;
   }

   sprintf(p, "\r\n\t   --- <%s> DATA %u/%u BYTES ---\r\n", info, len, PktLen);
   p += strlen(p);

   for (i = 1; i <= len; i++)
   {
      if (i % 16 == 1)
      {
         sprintf(p, "  %04u:  ", i-1);
         p += 9;
      }

      sprintf(p, "%02X ", (UCHAR)buffer[i-1]);
      if (isprint(buffer[i-1]) && (!isspace(buffer[i-1])))
      {
         *cp = buffer[i-1]; // sprintf(cp, "%c", buffer[i-1]);
      }
      else
      {
         *cp = '.'; // sprintf(cp, ".");
      }

      p += 3;
      cp += 1;

      if ((i % 16) == 8)
      {
         *p = *(p+1) = ' '; // sprintf(p, "  ");
         p += 2;
      }

      if (i % 16 == 0)
      {
         if (i % 64 == 0)
         {
            sprintf(p, " %c  %s\r\n\r\n", SPLIT_CHAR, cBuf);
         }
         else
         {
            sprintf(p, " %c  %s\r\n", SPLIT_CHAR, cBuf);
         }
         QCUSB_DbgPrintX
         (
            x,
            DbgMask,
            QCUSB_DBG_LEVEL_VERBOSE,
            ("%s",buf)
         );
         RtlZeroMemory(dbgOutputBuffer, myTextSize);
         p = buf;
         cp = cBuf;
      }
   }

   lastCnt = i % 16;

   if (lastCnt == 0)
   {
      lastCnt = 16;
   }

   if (lastCnt != 1)
   {
      // 10 + 3*8 + 2 + 3*8 = 60 (full line bytes)
      spaceNeeded = (16 - lastCnt + 1) * 3;
      if (lastCnt <= 8)
      {
         spaceNeeded += 2;
      }
      for (s = 0; s < spaceNeeded; s++)
      {
         *p++ = ' '; // sprintf(p++, " ");
      }
      sprintf(p, " %c  %s\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n",
              SPLIT_CHAR, cBuf, info, len, PktLen);
      QCUSB_DbgPrintX
      (
         x,
         DbgMask,
         QCUSB_DBG_LEVEL_VERBOSE,
         ("%s",buf)
      );
   }
   else
   {
      sprintf(buf, "\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n", info, len, PktLen);
      QCUSB_DbgPrintX
      (
         x,
         DbgMask,
         DbgLevel,
         ("%s",buf)
      );
   }

   ExFreePool(dbgOutputBuffer);

   #endif // QCUSB_DBGPRINT_BYTES
}  //USBUTL_PrintBytes

ULONG USBUTL_GetMdlTotalCount(PMDL Mdl)
{
   PMDL pCurrentMdlBuffer;
   ULONG totalCount = 0L;

   pCurrentMdlBuffer = Mdl;

   while (pCurrentMdlBuffer != NULL)
   {
      totalCount += MmGetMdlByteCount(pCurrentMdlBuffer);
      pCurrentMdlBuffer = pCurrentMdlBuffer->Next;
   }

   return totalCount;
}  // USBUTL_GetMdlTotalCount

#ifdef ENABLE_LOGGING
VOID USBUTL_LogMdlData
(
   PDEVICE_EXTENSION pDevExt,
   HANDLE            FileHandle,
   PMDL              Mdl,
   UCHAR             Type
)
{
   PMDL pCurrentMdlBuffer;
   PVOID pBuffer;
   ULONG byteCount = 0L;

   pCurrentMdlBuffer = Mdl;

   while (pCurrentMdlBuffer != NULL)
   {
      byteCount = MmGetMdlByteCount(pCurrentMdlBuffer);
      pBuffer = MmGetSystemAddressForMdlSafe(pCurrentMdlBuffer, HighPagePriority); 

      if (pBuffer != NULL)
      {
         QCUSB_LogData
         (
            pDevExt,
            FileHandle,
            pBuffer,
            byteCount,
            Type
         );
      }

      pCurrentMdlBuffer = pCurrentMdlBuffer->Next;
   }
}  // USBUTL_LogMdlData
#endif // ENABLE_LOGGING

VOID USBUTL_MdlPrintBytes
(
   PMDL  Mdl,
   ULONG printLen,
   char  *info,
   ULONG DbgMask,
   ULONG DbgLevel
)
{
   PMDL pCurrentMdlBuffer;
   PVOID pBuffer;
   ULONG byteCount = 0L;

   pCurrentMdlBuffer = Mdl;

   while (pCurrentMdlBuffer != NULL)
   {
      byteCount = MmGetMdlByteCount(pCurrentMdlBuffer);
      pBuffer = MmGetSystemAddressForMdlSafe(pCurrentMdlBuffer, HighPagePriority);

      if (pBuffer != NULL)
      {
         USBUTL_PrintBytes(pBuffer, printLen, byteCount, info, NULL, DbgMask, DbgLevel);
      }

      pCurrentMdlBuffer = pCurrentMdlBuffer->Next;
   }
}  // USBUTL_MdlPrintBytes

VOID USBUTL_CheckPurgeStatus(PDEVICE_EXTENSION pDevExt, USHORT newBit)
{
   PIRP pPurgeIrp = NULL;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
   pDevExt->PurgeMask |= newBit;

   if ((pDevExt->PurgeMask & USB_PURGE_WT) &&
       (pDevExt->PurgeMask & USB_PURGE_RD) &&
       (pDevExt->PurgeMask & USB_PURGE_EC))
   {
      pPurgeIrp = pDevExt->PurgeIrp;
      pDevExt->PurgeIrp = NULL;
      pDevExt->PurgeMask = 0;
   }
   QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

   if (pPurgeIrp != NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> CIRP: (Cpg 0x%x) 0x%p\n", pDevExt->PortName, STATUS_SUCCESS, pPurgeIrp)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pPurgeIrp, 0);
      pPurgeIrp->IoStatus.Status = STATUS_SUCCESS;
      pPurgeIrp->IoStatus.Information = 0;
      IoCompleteRequest(pPurgeIrp, IO_NO_INCREMENT);
   }
}  // USBUTL_CheckPurgeStatus

NTSTATUS USBUTL_NdisConfigurationGetValue
(
   NDIS_HANDLE       ConfigurationHandle,
   UCHAR             EntryIdx,
   PULONG            Value,
   UCHAR             Cookie
)
{
   NDIS_STATUS                     ndisStatus;
   NDIS_HANDLE                     configurationHandle;
   PNDIS_CONFIGURATION_PARAMETER   pReturnedValue = NULL;
   NDIS_CONFIGURATION_PARAMETER    ReturnedValue;

   // #define NDIS_STRING_CONST(x) {sizeof(L##x)-2, sizeof(L##x), L##x}

   NdisReadConfiguration
   (
      &ndisStatus,
      &pReturnedValue,
      ConfigurationHandle,
      &NdisRegString[EntryIdx],
      NdisParameterInteger  // NdisParameterHexInteger
   );

   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      return STATUS_UNSUCCESSFUL;
   }
   *Value = pReturnedValue->ParameterData.IntegerData;
   // NdisCloseConfiguration(configurationHandle);
   return STATUS_SUCCESS;
}  // USBUTL_NdisConfigurationGetValue

NTSTATUS USBUTL_NdisConfigurationGetString
(
   NDIS_HANDLE       ConfigurationHandle,  // registry handle
   PDEVICE_EXTENSION pDevExt,
   UCHAR             EntryIdx,
   PUNICODE_STRING   StringText,
   UCHAR             Cookie
)
{
   NDIS_STATUS                     ndisStatus;
   PNDIS_CONFIGURATION_PARAMETER   pReturnedValue = NULL;
   NDIS_CONFIGURATION_PARAMETER    ReturnedValue;
   NTSTATUS                        ntStatus;

   // #define NDIS_STRING_CONST(x) {sizeof(L##x)-2, sizeof(L##x), L##x}

   NdisReadConfiguration
   (
      &ndisStatus,
      &pReturnedValue,
      ConfigurationHandle,
      &NdisRegString[EntryIdx],
      NdisParameterString
   );

   // DbgPrint("QCNETREG: read configurations [%d] done! 0x%x\n", EntryIdx, ndisStatus);

   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      return STATUS_UNSUCCESSFUL;
   }

   ntStatus = USBUTL_AllocateUnicodeString
              (
                 StringText,
                 pReturnedValue->ParameterData.StringData.MaximumLength,
                 "RegString"
              );

   if (ntStatus != STATUS_SUCCESS)
   {
      return ntStatus;
   }

   RtlCopyUnicodeString(StringText, &(pReturnedValue->ParameterData.StringData));

   return STATUS_SUCCESS;
}  // USBUTL_NdisConfigurationGetString

NTSTATUS USBUTL_NdisConfigurationSetString
(
   NDIS_HANDLE       ConfigurationHandle,  // registry handle
   UCHAR             EntryIdx,             // entry index
   PCHAR             StringText,           // string value to set
   UCHAR             Cookie                // tracking cookie
)
{
   NDIS_STATUS status;
   NDIS_CONFIGURATION_PARAMETER configParameter;
   PNDIS_STRING keyword = &NdisRegString[EntryIdx];
   PNDIS_STRING pucStringText;
   ANSI_STRING ansiStr;

   ansiStr.Length = strlen(StringText);
   ansiStr.MaximumLength = strlen(StringText);
   ansiStr.Buffer = StringText;

   // Set configParameter.ParameterData.StringData
   // typedef UNICODE_STRING NDIS_STRING, *PNDIS_STRING;

   configParameter.ParameterType = NdisParameterString;
   pucStringText = &(configParameter.ParameterData.StringData);
   RtlAnsiStringToUnicodeString(pucStringText, &ansiStr, TRUE);

   NdisWriteConfiguration
   (
      &status,
      ConfigurationHandle,
      keyword,
      &configParameter
   );

   RtlFreeUnicodeString(pucStringText);

   if (status != NDIS_STATUS_SUCCESS)
   {
      // emit error
      QCUSB_DbgPrintG
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CfgSetStr: reg ERR(%d)\n", gDeviceName, Cookie)
      );
      return STATUS_UNSUCCESSFUL;
   }
   
   return STATUS_SUCCESS;
}  // USBUTL_NdisConfigurationSetString


NTSTATUS USBUTL_NdisConfigurationGetBinary
(
   NDIS_HANDLE       ConfigurationHandle,  
   PNDIS_STRING      EntryString,
   PVOID             pData,
   ULONG             Size,
   UCHAR             Cookie
)
{
   NDIS_STATUS                     ndisStatus;
   PNDIS_CONFIGURATION_PARAMETER   pReturnedValue = NULL;
   NTSTATUS                        ntStatus;

   NdisReadConfiguration
   (
      &ndisStatus,
      &pReturnedValue,
      ConfigurationHandle,
      EntryString,
      NdisParameterBinary
   );

   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      if (ndisStatus == NDIS_STATUS_RESOURCES)
      {
         return STATUS_INSUFFICIENT_RESOURCES;
      }
      return STATUS_UNSUCCESSFUL;
   }

   RtlCopyMemory(pData, 
                 pReturnedValue->ParameterData.StringData.Buffer, 
                 pReturnedValue->ParameterData.StringData.Length);

   return STATUS_SUCCESS;
}  

NTSTATUS USBUTL_NdisConfigurationSetBinary
(
   NDIS_HANDLE       ConfigurationHandle,  
   PNDIS_STRING      EntryString,
   PVOID             pData,
   ULONG             Size,
   UCHAR             Cookie
)
{
   NDIS_STATUS status;
   NDIS_CONFIGURATION_PARAMETER configParameter;
   NDIS_STRING ParameterStr;

   configParameter.ParameterType = NdisParameterBinary;
   configParameter.ParameterData.StringData.Length = Size;
   configParameter.ParameterData.StringData.MaximumLength = Size;
   configParameter.ParameterData.StringData.Buffer = pData;

   NdisWriteConfiguration
   (
      &status,
      ConfigurationHandle,
      EntryString,
      &configParameter
   );

   if (status != NDIS_STATUS_SUCCESS)
   {
      // emit error
      QCUSB_DbgPrintG
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CfgSetStr: reg ERR(%d)\n", gDeviceName, Cookie)
      );
      return STATUS_UNSUCCESSFUL;
   }
   
   return STATUS_SUCCESS;
} 


BOOLEAN USBUTL_IsHighSpeedDevice(PDEVICE_EXTENSION pDevExt)
{
   return ((pDevExt->HighSpeedUsbOk == QC_HS_USB_OK)  ||
           (pDevExt->HighSpeedUsbOk == QC_HS_USB_OK2) ||
           (pDevExt->HighSpeedUsbOk == QC_HS_USB_OK3));
}  // USBUTL_IsHighSpeedDevice

#ifdef QC_IP_MODE
VOID USBUTL_SetEtherType(PUCHAR Packet)
{
   PQCUSB_ETH_HDR hdr;
   PUCHAR         pkt;
   UCHAR          ipVer;

   hdr = (PQCUSB_ETH_HDR)Packet;
   pkt = Packet + sizeof(QCUSB_ETH_HDR); // skip to IP portion
   ipVer = (*pkt & 0xF0) >> 4;           // retrieve IP version

   // EtherType is set for IPV4 by default, so we only care IPV6
   if (ipVer == 6)
   {
      hdr->EtherType = ntohs(ETH_TYPE_IPV6);
   }
}  // USBUTL_SetEtherType
#endif

NTSTATUS
USBUTL_GetServiceRegValue( PWSTR ServiceName,PWSTR  ParName, PULONG ParValue)
{
    NTSTATUS                  status;
    RTL_QUERY_REGISTRY_TABLE  paramTable[2];
    ULONG                     defaultValue;

    if( ( NULL == ServiceName ) || 
        ( NULL == ParName ) || 
        ( NULL == ParValue ) ) {
        return STATUS_INVALID_PARAMETER;
    }

    QCUSB_DbgPrintG
    (
       QCUSB_DBG_MASK_READ,
       QCUSB_DBG_LEVEL_ERROR,
       ("<%s> USBUTL_GetServiceRegValue: regpath %s, parname %s and parvalue %x\n", gDeviceName, (char *)ServiceName, (char *)ParName, *ParValue)
    );

    //
    // set up table entries for call to RtlQueryRegistryValues
    //
    // leave paramTable[1] as all zeros to terminate the table
    //
    // use original value as default value
    //
    // use RtlQueryRegistryValues to do the grunge work
    //
    RtlZeroMemory( paramTable, sizeof(paramTable) );

    defaultValue = *ParValue;

    paramTable[0].Flags         = RTL_QUERY_REGISTRY_DIRECT;
    paramTable[0].Name          = ParName;
    paramTable[0].EntryContext  = ParValue;
    paramTable[0].DefaultType   = REG_DWORD;
    paramTable[0].DefaultData   = &defaultValue;
    paramTable[0].DefaultLength = sizeof(ULONG);

    status = RtlQueryRegistryValues( RTL_REGISTRY_SERVICES,
                                     ServiceName,
                                     &paramTable[0],
                                     NULL,
                                     NULL);
       
    if( status != STATUS_SUCCESS ) 
    {
        QCUSB_DbgPrintG
        (
           QCUSB_DBG_MASK_READ,
           QCUSB_DBG_LEVEL_ERROR,
           ("<%s> USBUTL_GetServiceRegValue: ERR(0x%x)\n", gDeviceName, status)
        );
    }

    return status;
}

NTSTATUS USBUTL_DriverRegistrySetDword
(
    PDEVICE_EXTENSION pDevExt,
    PWCHAR pValueName,
    DWORD  value
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    if (pDevExt && pValueName)
    {
        HANDLE hKey;
        UNICODE_STRING ucValueName;

        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> --> _SetDriverRegistryDword %ws 0x%x\n", pDevExt->PortName, pValueName, value)
        );

        status = IoOpenDeviceRegistryKey
        (
            pDevExt->PhysicalDeviceObject,
            PLUGPLAY_REGKEY_DRIVER,
            KEY_ALL_ACCESS,
            &hKey
        );

        if (NT_SUCCESS(status))
        {
            RtlInitUnicodeString(&ucValueName, pValueName);
            status = ZwSetValueKey
            (
                hKey,
                &ucValueName,
                0,
                REG_DWORD,
                &value,
                sizeof(ULONG)
            );
            ZwClose(hKey);
        }

        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> <-- _SetDriverRegistryDword VALUE 0x%x ST 0x%x\n", pDevExt->PortName, value, status)
        );
    }
    else
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<-- _SetDriverRegistryDword Invalid Param 0x%p 0x%p\n", pDevExt, pValueName)
        );
    }

    return status;
}

NTSTATUS USBUTL_DriverRegistrySetString
(
    PDEVICE_EXTENSION pDevExt,
    PWCHAR pValueName,
    PVOID  value,
    ULONG  length
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    if (pDevExt && pValueName && value)
    {
        HANDLE hKey;
        UNICODE_STRING ucValueName;

        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> --> _DriverRegistrySetString %ws 0x%x\n", pDevExt->PortName, pValueName, value)
        );

        status = IoOpenDeviceRegistryKey
        (
            pDevExt->PhysicalDeviceObject,
            PLUGPLAY_REGKEY_DRIVER,
            KEY_ALL_ACCESS,
            &hKey
        );

        if (NT_SUCCESS(status))
        {
            RtlInitUnicodeString(&ucValueName, pValueName);
            status = ZwSetValueKey
            (
                hKey,
                &ucValueName,
                0,
                REG_SZ,
                value,
                length
            );
            ZwClose(hKey);
        }

        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> <-- _DriverRegistrySetString VALUE 0x%x ST 0x%x\n", pDevExt->PortName, value, status)
        );
    }
    else
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<-- _DriverRegistrySetString Invalid Param 0x%p 0x%p\n", pDevExt, pValueName)
        );
    }

    return status;
}

NTSTATUS USBUTL_DriverRegistryDelete
(
    PDEVICE_EXTENSION pDevExt,
    PWCHAR pValueName
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    if (pDevExt && pValueName)
    {
        HANDLE hKey;
        UNICODE_STRING ucValueName;

        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> --> _DeleteDriverRegistry %ws\n", pDevExt->PortName, pValueName)
        );

        status = IoOpenDeviceRegistryKey
        (
            pDevExt->PhysicalDeviceObject,
            PLUGPLAY_REGKEY_DRIVER,
            KEY_ALL_ACCESS,
            &hKey
        );

        if (NT_SUCCESS(status))
        {
            RtlInitUnicodeString(&ucValueName, pValueName);
            status = ZwDeleteValueKey(hKey, &ucValueName);
            ZwClose(hKey);
        }

        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> <-- _DeleteDriverRegistry VALUENAME %ws ST 0x%x\n", pDevExt->PortName, pValueName, status)
        );
    }
    else
    {
        QCUSB_DbgPrint
        (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<-- _DeleteDriverRegistry Invalid Param 0x%p 0x%p\n", pDevExt, pValueName)
        );
    }

    return status;
}