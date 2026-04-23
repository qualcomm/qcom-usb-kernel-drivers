/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                        U T I L S . H

GENERAL DESCRIPTION
    This file defines utility macros, debug logging, and function
    prototypes for the Qualcomm MTU configuration service helpers.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef UTILS_H
#define UTILS_H

#include "api.h"

#define DBG_MSG_MAX_SZ 1024
#define KEY_PRESSED_ESC 0x1B

#define QCSER_DbgPrint(x,y,z) printf("%s", z);

// Borrow some definitions from ntddk.h
VOID FORCEINLINE
InitializeListHead(IN PLIST_ENTRY ListHead)
{
    ListHead->Flink = ListHead->Blink = ListHead;
}

#define IsListEmpty(ListHead) \
    ((ListHead)->Flink == (ListHead))

VOID FORCEINLINE
RemoveEntryList(IN PLIST_ENTRY Entry)
{
    PLIST_ENTRY Blink;
    PLIST_ENTRY Flink;

    Flink = Entry->Flink;
    Blink = Entry->Blink;
    Blink->Flink = Flink;
    Flink->Blink = Blink;
}

PLIST_ENTRY FORCEINLINE
RemoveHeadList(IN PLIST_ENTRY ListHead)
{
    PLIST_ENTRY Flink;
    PLIST_ENTRY Entry;

    Entry = ListHead->Flink;
    Flink = Entry->Flink;
    ListHead->Flink = Flink;
    Flink->Blink = ListHead;
    return Entry;
}

PLIST_ENTRY FORCEINLINE
RemoveTailList(IN PLIST_ENTRY ListHead)
{
    PLIST_ENTRY Blink;
    PLIST_ENTRY Entry;

    Entry = ListHead->Blink;
    Blink = Entry->Blink;
    ListHead->Blink = Blink;
    Blink->Flink = ListHead;
    return Entry;
}

VOID FORCEINLINE
InsertTailList(IN PLIST_ENTRY ListHead, IN PLIST_ENTRY Entry)
{
    PLIST_ENTRY Blink;

    Blink = ListHead->Blink;
    Entry->Flink = ListHead;
    Entry->Blink = Blink;
    Blink->Flink = Entry;
    ListHead->Blink = Entry;
}

VOID FORCEINLINE
InsertHeadList(IN PLIST_ENTRY ListHead, IN PLIST_ENTRY Entry)
{
    PLIST_ENTRY Flink;

    Flink = ListHead->Flink;
    Entry->Flink = Flink;
    Entry->Blink = ListHead;
    Flink->Blink = Entry;
    ListHead->Flink = Entry;
}

// Function prototypes
BOOL WINAPI GetServiceFile
(
    HANDLE hd,
    PCHAR  ServiceFileName,
    UCHAR  ServiceType
);

DWORD WINAPI RegisterNotification(PVOID Context);

VOID USBUTL_PrintBytes(PVOID Buf, ULONG len, ULONG PktLen, char *info);

PCHAR GetCharArray
(
    PCHAR  InBuffer,
    PCHAR  CharArray,
    UCHAR  MaxElements,
    PUCHAR ActualElements
);

#ifdef QC_SERVICE
VOID __cdecl QCMTU_Print(PCHAR Format, ...);
#endif

#endif // UTILS_H
