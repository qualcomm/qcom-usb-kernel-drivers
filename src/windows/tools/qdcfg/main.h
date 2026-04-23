/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M A I N . H

GENERAL DESCRIPTION
    This file defines data structures, macros, and function prototypes
    for the qdcfg driver configuration utility.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef MAIN_H
#define MAIN_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "api.h"

/* Left-shifted flag of dev type in device change callback */
#define QC_DEV_TYPE2_NONE		0x00
#define QC_DEV_TYPE2_NET		0x01			/* Net		->		DevFlag.DEV_TYPE_NET	*/
#define QC_DEV_TYPE2_PORTS		0x02			/* Diag		->		DevFlag.DEV_TYPE_PORTS	*/
#define QC_DEV_TYPE2_USB		0x04			/* QDSS/DPL	->		DevFlag.DEV_TYPE_USB	*/
#define QC_DEV_TYPE2_FLT		0x08			/* Filter	->		DevFlag.DEV_TYPE_FILT	*/
#define QC_DEV_TYPE2_ALL		0xff

typedef struct _CMD_COMMAND
{
	char*	entry;		// "QCDriverDebugMask"
	char*	usage;		// "0x10 (default) to 0xFF"
	int		isExposed;
	BYTE	devType;
	int		defaultVal;
	char*	intro;		// friendly usage
} CMD_COMMAND;

typedef struct _CMD_LINE_ARGS
{
   int   DebugMask;
   char  NIC_Name[SERVICE_FILE_BUF_LEN];
   BOOL  IsCOM;

   char  EntryName[128];
   int   EntryValue;
   int   Action; // 0: get; 1: set; 2: runtime only
   int   DebugFlags;
   int   DebugLevel;
   int   DebugMaxFile;
   int   EntryValueisStr;
   char  EntryValueStr[1024];
} CMD_LINE_ARGS, *PCMD_LINE_ARGS;

// Function Prototypes
int GetCommandLineArguments(int argc, char *argv[], PCMD_LINE_ARGS Args);
void Usage(INT, PCHAR);
VOID ProcessRegistryEntry(CMD_LINE_ARGS* Args);

// Driver Configuration APIs
__declspec(dllexport) UINT getSupportedEntries(void);
__declspec(dllexport) int getEntryLen(UINT index);
__declspec(dllexport) int getUsageLen(UINT index);
__declspec(dllexport) int getCommand(CMD_COMMAND* buffer, UINT index);
__declspec(dllexport) int processCommand(int argc, LPTSTR argv[]);
__declspec(dllexport) int executeCommand(char* entry, char action, char** params, int paramLen, BOOL device, char* deviceName);
__declspec(dllexport) const char* getQUDVersionNum();

// Delegate version of printf
typedef int (_cdecl* PRINTF_DELEGATE) (char* message);
static PRINTF_DELEGATE hidden_printer = NULL;
__declspec(dllexport) void setPrintDelegate(PRINTF_DELEGATE printer);
void printfd(const char* const format, ...);

#endif // MAIN_H
