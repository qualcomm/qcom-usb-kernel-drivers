/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                           I N F D E V. H

GENERAL DESCRIPTION
    Scan and remove VID_05C6 related devnodes and inf files from system

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef INFDEV_H
#define INFDEV_H

#include <stdlib.h>
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <Strsafe.h>
#include <Shlwapi.h>

#define INT_OEM_NAMING       L"\\oem*.inf"
#define INF_INSTALL_PATH     L"C:\\Windows\\Inf"

#ifdef _WIN64
#define INF_REMOVE_COMMAND   L"C:\\Windows\\System32\\pnputil.exe -f -d "
#else
#define INF_REMOVE_COMMAND   L"C:\\Windows\\Sysnative\\pnputil.exe -f -d "
#endif

enum class EXEC_MODE
{
    PREVIEW,
    REMOVE_OEM,
    REMOVE_ALL
};

#define INF_SIZE_MAX    (1024*1024)
#define LINE_LEN_MAX    2048
#define REG_HW_ID_SIZE  2048

#define MATCH_VID       "VID_05C6"
#define MATCH_PID       "PID_9001"
#define MATCH_PID2      "PID_9049"
#define MATCH_PID3      "PID_9025"
#define EXCLUDE_PID     "PID_9204"
#define EXCLUDE_PID2    "PID_9205"
#define EXCLUDE_PID3    "PID_9301"
#define EXCLUDE_PID4    "PID_9302"
#define EXCLUDE_PID5    "PID_9303"

// Help functions
void print_timestamp(PCHAR Text, bool NewLine);
int  remove_drivers(EXEC_MODE);

// INF removal
VOID ScanAndRemoveInf(LPCTSTR InfPath);
BOOL MatchingInf(PCTSTR InfFullPath, PCTSTR InfFileName, ULONG DataSize);
BOOL ConfirmInfFile(PCTSTR InfFullPath);
BOOL DeviceMatch(PTSTR InfText, DWORD TextSize);
VOID RemoveInfFile(PCTSTR InfFileName, PCHAR Type);

// Dev node removal
VOID ScanAndRemoveDevice(LPCTSTR HwId);
BOOL RemoveDevice(HDEVINFO DevInfoHandle, PSP_DEVINFO_DATA DevInfoData);

#endif // INFDEV_H
