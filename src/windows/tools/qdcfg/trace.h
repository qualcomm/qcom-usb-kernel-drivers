/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                        T R A C E . H

GENERAL DESCRIPTION
    This file defines constants, macros, and function prototypes for
    ETW trace logging management for Qualcomm USB drivers.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef TRACE_H
#define TRACE_H

#include <evntrace.h>

#define DRIVERS_GUID              "5F02CA82-B28B-4a95-BA2C-FBED52DDD3DC"

// globallogger for xp only
#define GLOBALLOGGER_REG_KEY      "SYSTEM\\CurrentControlSet\\Control\\WMI\\GlobalLogger\\"
#define GLOBALLOGGER_REG_GUID_KEY GLOBALLOGGER_REG_KEY \
                                  DRIVERS_GUID

// autologger for vista and later
#define AUTOLOGGER_REG_KEY        "SYSTEM\\CurrentControlSet\\Control\\WMI\\Autologger\\QcDriverSession\\"
#define AUTOLOGGER_REG_GUID_KEY   AUTOLOGGER_REG_KEY \
                                 "{" \
                                  DRIVERS_GUID \
                                  "}"

#define AUTOLOGGER_GUID           "{C1514B62-EB15-4FF2-9475-5D0E63383ADC}"

#define LOGFILE_PATH              "C:\\QCDriverLog.etl"
#define AUTOLOGGER_LOGFILE_PATH   "C:\\QCDriverAutoLog.etl"
#define GLOBALLOGGER_LOGFILE_PATH "C:\\QCDriverGlobalLog.etl"

#define LOGSESSION_NAME           "QCDriver"
#define AUTOLOGGER_NAME           "QCDriverSession"

#define DEFAULT_FLAGS             0x7fffffff
#define DEFAULT_LEVEL             0xff
#define CLOCKTYPE                 1
#define LOG_FILE_MODE             EVENT_TRACE_FILE_MODE_CIRCULAR
#define MAX_FILE_SIZE             100
#define FLUSH_TIMER               0

typedef struct tracingConfig
{
    ULONG     Flags;
    ULONG     Level;
    ULONG     MaxFile;
} TracingConfig;

void StartTracing
(
    ULONG     Flags,
    ULONGLONG Level,
    ULONG     MaxFile
);

__declspec(dllexport) ULONG InspectTracing(void);

__declspec(dllexport) ULONG GetTracingConfig(TracingConfig *);

void StopTracing(void);

void StartAutoLogger
(
    ULONG Flags,
    ULONG Level,
    ULONG MaxFile
);

void StopAutoLogger(void);

BOOL IsRegistered(void);

#endif
