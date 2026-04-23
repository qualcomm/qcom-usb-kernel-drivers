/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                        T R A C E . C

GENERAL DESCRIPTION
    This file implements ETW trace logging management (start/stop tracing
    and AutoLogger configuration) for Qualcomm USB drivers.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include <windows.h>
#include <stdio.h>
#include "reg.h"
#include "trace.h"

extern void printfd(const char *const format, ...);

// Default Guid
static const GUID ProviderGuid =
{0x5F02CA82, 0xB28B, 0x4a95, { 0xBA, 0x2C, 0xFB, 0xED, 0x52, 0xDD, 0xD3, 0xDC }};

typedef struct
{
    char *Name;
    char DataType; // REG_DWORD or REG_SZ
    union
    {
        long LongData;
        char *StringData;
    } Data;
} RegEntry, *pRegEntry;


// Registry settings for AUTOLOGGER_REG_KEY
#define INDEX_MAX_FILE 0

RegEntry RegAutoLoggerSession[] =
{
   {"MaxFileSize", REG_DWORD,  MAX_FILE_SIZE}, // must be index INDEX_MAX_FILE
   {"ClockType",   REG_DWORD,  CLOCKTYPE},
   {"FlushTimer",  REG_DWORD,  0},
   {"LogFileMode", REG_DWORD,  LOG_FILE_MODE},
   {"Start",       REG_DWORD,  1},
   {"FileName",    REG_SZ,     (long)AUTOLOGGER_LOGFILE_PATH},
   {"Guid",        REG_SZ,     (long)AUTOLOGGER_GUID}
};

RegEntry RegAutoLoggerSessionOff[] =
{
   {"Start",       REG_DWORD,  0}
};

// Registry settings for AUTOLOGGER_REG_GUID_KEY
#define INDEX_FLAGS 0
#define INDEX_LEVEL 1

RegEntry RegAutoLoggerSessionGuid[] =
{
   {"EnableFlags", REG_DWORD,  DEFAULT_FLAGS}, // must be index INDEX_FLAGS
   {"EnableLevel", REG_DWORD,  DEFAULT_LEVEL}, // must be index INDEX_LEVEL
   {"Enabled",     REG_DWORD,  1},
};

// Registry settings for GLOBALLOGGER_REG_KEY
RegEntry RegGlobalLoggerSession[] =
{
   {"FileMax",     REG_DWORD,  MAX_FILE_SIZE}, // must be index INDEX_MAX_FILE
   {"Start",       REG_DWORD,  1},
   {"FileName",    REG_SZ,     (long)GLOBALLOGGER_LOGFILE_PATH},
};

RegEntry RegGlobalLoggerSessionOff[] =
{
   {"Start",       REG_DWORD,  0}
};


// Registry settings for GLOBALLOGGER_REG_GUID_KEY
RegEntry RegGlobalLoggerSessionGuid[] =
{
   {"Flags", REG_DWORD,  DEFAULT_FLAGS}, // must be index INDEX_FLAGS
   {"Level", REG_DWORD,  DEFAULT_LEVEL}, // must be index INDEX_LEVEL
   {"FlushTimer",  REG_DWORD,  0},
};


static void DisplayStatus(LONG status, char *msg)
{
    if (ERROR_SUCCESS != status)
    {
        LPVOID lpBuffer;
        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            status,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&lpBuffer,
            100,
            NULL);
        printfd("Failed at %s with error %lu: %s\n", msg ? msg : "unknown task", status, lpBuffer); fflush(stdout);
        LocalFree(lpBuffer);
    }
    else
    {
        printfd("Succeeded in %s.\n", msg); fflush(stdout);
    }
}

static LONG AddToReg
(
    char       *pszRegKey,
    pRegEntry   pReg,
    int         size
)
{
    HKEY      hTestKey;
    pRegEntry pEntry;
    LONG      retCode = 0;
    int       i, numEntries = size / sizeof(RegEntry);

    // create or open reg key
    retCode = RegCreateKeyEx(
        HKEY_LOCAL_MACHINE,
        pszRegKey,
        0,
        0,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        NULL,
        &hTestKey,
        NULL);

    if (retCode != ERROR_SUCCESS)
    {
        return retCode;
    }

    // add registry items to above reg key
    for (i = 0, pEntry = pReg; i < numEntries; i++, pEntry++)
    {
        LPBYTE pData;
        DWORD  len;

        if (pEntry->DataType == REG_SZ)
        {
            pData = (LPBYTE)pEntry->Data.StringData;
            len = strlen(pEntry->Data.StringData);
        }
        else
        {
            pData = (LPBYTE)&pEntry->Data.LongData;
            len = sizeof(pEntry->Data.LongData);
        }
        retCode = RegSetValueEx(
            hTestKey,
            pEntry->Name,
            0,
            pEntry->DataType,
            (LPBYTE)pData,
            len);

        if (retCode != ERROR_SUCCESS)
        {
            break;
        }
    }
    RegCloseKey(hTestKey);

    return retCode;
}


static void SetAutoLogger
(
    BOOL  bOn,
    ULONG Flags,
    ULONG Level,
    ULONG MaxFile
)
{
    LONG           retCode;
    BOOL           bIsVistaOrLater = TRUE;

    if (bOn)
    {
        if (bIsVistaOrLater)
        {
            if (Flags != 0 || Level != 0)
            {
                RegAutoLoggerSession[INDEX_MAX_FILE].Data.LongData = MaxFile;
                RegAutoLoggerSessionGuid[INDEX_FLAGS].Data.LongData = Flags;
                RegAutoLoggerSessionGuid[INDEX_LEVEL].Data.LongData = Level;
            }
            retCode = AddToReg(AUTOLOGGER_REG_KEY, RegAutoLoggerSession, sizeof(RegAutoLoggerSession));
            if (retCode == ERROR_SUCCESS)
            {
                retCode = AddToReg(AUTOLOGGER_REG_GUID_KEY, RegAutoLoggerSessionGuid, sizeof(RegAutoLoggerSessionGuid));
                DisplayStatus(retCode, "configuring autologger for reboot"); fflush(stdout);
                if (retCode == ERROR_SUCCESS)
                {
                    printfd("Autologger file path: %s\n", AUTOLOGGER_LOGFILE_PATH); fflush(stdout);
                }
            }
        }
        else
        {
            if (Flags != 0 || Level != 0)
            {
                RegGlobalLoggerSession[INDEX_MAX_FILE].Data.LongData = MaxFile;
                RegGlobalLoggerSessionGuid[INDEX_FLAGS].Data.LongData = Flags;
                RegGlobalLoggerSessionGuid[INDEX_LEVEL].Data.LongData = Level;
            }
            retCode = AddToReg(GLOBALLOGGER_REG_KEY, RegGlobalLoggerSession, sizeof(RegGlobalLoggerSession));
            if (retCode == ERROR_SUCCESS)
            {
                retCode = AddToReg(GLOBALLOGGER_REG_GUID_KEY, RegGlobalLoggerSessionGuid, sizeof(RegGlobalLoggerSessionGuid));
            }
            DisplayStatus(retCode, "configuring globallogger for reboot");
            if (retCode == ERROR_SUCCESS)
            {
                printfd("Globallogger file path: %s\n", GLOBALLOGGER_LOGFILE_PATH);
            }
        }
        if (MaxFile == 0)
        {
            printfd("You have set an unlimited log file size. ");
        }
        printfd("Please remember to disable logging when done.\n");
    }
    else
    {
        if (bIsVistaOrLater)
        {
            retCode = AddToReg(AUTOLOGGER_REG_KEY, RegAutoLoggerSessionOff, sizeof(RegAutoLoggerSessionOff));
            DisplayStatus(retCode, "disabling autologger for reboot");
        }
        else
        {
            retCode = AddToReg(GLOBALLOGGER_REG_KEY, RegGlobalLoggerSessionOff, sizeof(RegGlobalLoggerSessionOff));
            DisplayStatus(retCode, "disabling globallogger for reboot");
        }
    }
}  // SetAutoLogger


BOOL IsRegistered(void)
{
    ULONG status = ERROR_SUCCESS;
    PTRACE_GUID_PROPERTIES pProviderProperties = NULL; // Buffer that contains the block of property structures
    PTRACE_GUID_PROPERTIES *pProviders = NULL;         // Array of pointers to property structures in ProviderProperties
    PTRACE_GUID_PROPERTIES *pTemp = NULL;
    ULONG RegisteredProviderCount = 0;                 // Actual number of providers registered on the computer
    ULONG ProviderCount = 0;
    ULONG BufferSize = 0;
    BOOL  Registered = FALSE;

    // EnumerateTraceGuids requires a valid pointer. Create a dummy
    // allocation, so that you can get the actual allocation size.

    pProviders = (PTRACE_GUID_PROPERTIES *)malloc(sizeof(PTRACE_GUID_PROPERTIES));

    if (NULL == pProviders)
    {
        goto cleanup;
    }

    // Pass zero for the array size to get the number of registered providers
    // for this snapshot. Then allocate memory for the block of structures
    // and the array of pointers.

    status = EnumerateTraceGuids(pProviders, 1, &RegisteredProviderCount);

    if (ERROR_MORE_DATA == status)
    {
        USHORT i;

        ProviderCount = RegisteredProviderCount;

        BufferSize = sizeof(TRACE_GUID_PROPERTIES) * RegisteredProviderCount;

        pProviderProperties = (PTRACE_GUID_PROPERTIES)malloc(BufferSize);

        if (NULL == pProviderProperties)
        {
            goto cleanup;
        }

        ZeroMemory(pProviderProperties, BufferSize);

        pTemp = (PTRACE_GUID_PROPERTIES *)realloc(pProviders, RegisteredProviderCount * sizeof(PTRACE_GUID_PROPERTIES));

        if (NULL == pTemp)
        {
            goto cleanup;
        }

        pProviders = pTemp;
        pTemp = NULL;

        for (i = 0; i < RegisteredProviderCount; i++)
        {
            pProviders[i] = &pProviderProperties[i];
        }
        status = EnumerateTraceGuids(pProviders, ProviderCount, &RegisteredProviderCount);

        if (ERROR_SUCCESS == status || ERROR_MORE_DATA == status)
        {
            for (i = 0; i < RegisteredProviderCount; i++)
            {
                if (IsEqualGUID(&pProviders[i]->Guid, &ProviderGuid) == 1)
                {
                    Registered = TRUE;
                    break;
                }
            }
        }
        else
        {
            goto cleanup;
        }
    }

cleanup:

    if (pProviders)
    {
        free(pProviders);
    }

    if (pProviderProperties)
    {
        free(pProviderProperties);
    }
    return Registered;
}

void StartTracing
(
    ULONG     Flags,
    ULONGLONG Level,
    ULONG     MaxFile
)
{
    LONG                    retCode;
    TRACEHANDLE             SessionHandle = 0;
    EVENT_TRACE_PROPERTIES *pSessionProperties = NULL;
    ULONG                   BufferSize = 0;

    if (Flags == 0)
    {
        Flags = DEFAULT_FLAGS;
    }
    if (Level == 0)
    {
        Level = DEFAULT_LEVEL;
    }

    // Allocate memory for the session properties. The memory must
    // be large enough to include the log file name and session name,
    // which get appended to the end of the session properties structure.

    BufferSize = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(LOGFILE_PATH) + sizeof(LOGSESSION_NAME);
    pSessionProperties = (EVENT_TRACE_PROPERTIES *)malloc(BufferSize);
    if (NULL == pSessionProperties)
    {
        printfd("Failed to start tracing due to insufficient memory.\n", BufferSize);
        return;
    }

    // Set the session properties. You only append the log file name
    // to the properties structure; the StartTrace function appends
    // the session name for you.

    ZeroMemory(pSessionProperties, BufferSize);
    pSessionProperties->Wnode.BufferSize = BufferSize;
    pSessionProperties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    pSessionProperties->Wnode.ClientContext = CLOCKTYPE;
    pSessionProperties->LogFileMode = LOG_FILE_MODE;
    pSessionProperties->MaximumFileSize = MaxFile;
    pSessionProperties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    pSessionProperties->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(LOGSESSION_NAME);
    strncpy_s(((char *)pSessionProperties + pSessionProperties->LogFileNameOffset), sizeof(LOGFILE_PATH), LOGFILE_PATH, sizeof(LOGFILE_PATH) - 1);

    retCode = StartTrace((PTRACEHANDLE)&SessionHandle, LOGSESSION_NAME, pSessionProperties);
    // if session was already started, report so and allow user to change level/flags
    if (retCode == ERROR_ALREADY_EXISTS)
    {
        printfd("Session already in progress.\n"); fflush(stdout);
    }
    else if (retCode != ERROR_SUCCESS)
    {
        DisplayStatus(retCode, "starting trace session"); fflush(stdout);
    }

    // Enable the providers that you want to log events to your session.
    if (retCode == ERROR_SUCCESS)
    {
        retCode = EnableTrace(
            TRUE,
            Flags,
            (UCHAR)Level,
            (LPCGUID)&ProviderGuid,
            SessionHandle);

        DisplayStatus(retCode, "enabling trace session"); fflush(stdout);
        SetLastError(ERROR_SUCCESS);
    }
    if (retCode == ERROR_SUCCESS)
    {
        printfd("Log file path: %s\n", LOGFILE_PATH); fflush(stdout);
    }
    if (pSessionProperties)
    {
        free(pSessionProperties);
    }
} // StartTracing

ULONG InspectTracing(void)
{
    // return ERROR_WMI_INSTANCE_NOT_FOUND if tracking is not running, ERROR_SUCCESS otherwise
    ULONG BufferSize = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(LOGFILE_PATH) + sizeof(LOGSESSION_NAME);
    PEVENT_TRACE_PROPERTIES pSessionProperties = (PEVENT_TRACE_PROPERTIES)malloc(BufferSize);

    if (NULL == pSessionProperties)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    ZeroMemory(pSessionProperties, BufferSize);
    pSessionProperties->Wnode.BufferSize = BufferSize;
    pSessionProperties->LogFileMode = LOG_FILE_MODE;
    pSessionProperties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    pSessionProperties->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(LOGSESSION_NAME);

    ULONG ret = ControlTrace(
        0,
        LOGSESSION_NAME,
        pSessionProperties,
        EVENT_TRACE_CONTROL_QUERY
    );

    free(pSessionProperties);
    return ret;
}

ULONG GetTracingConfig(TracingConfig *config)
{
    HKEY hKey;

    ULONG ret = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        AUTOLOGGER_REG_KEY,
        0,
        KEY_READ,
        &hKey
    );

    if (ret == ERROR_SUCCESS)
    {
        ULONG maxFileSize;
        ULONG enableFlags;
        ULONG enableLevel;
        DWORD size = sizeof(ULONG);

        ret = RegQueryValueEx(hKey, "MaxFileSize", NULL, NULL, (LPBYTE)&maxFileSize, &size);
        if (ret == ERROR_SUCCESS)
        {
            HKEY providerKey;

            ret = RegOpenKeyExA(
                hKey,
                "{"DRIVERS_GUID"}",
                0,
                KEY_READ,
                &providerKey
            );
            if (ret == ERROR_SUCCESS)
            {
                ret = RegQueryValueEx(providerKey, "EnableFlags", NULL, NULL, (LPBYTE)&enableFlags, &size);
                if (ret == ERROR_SUCCESS)
                {
                    ret = RegQueryValueEx(providerKey, "EnableLevel", NULL, NULL, (LPBYTE)&enableLevel, &size);
                    if (ret == ERROR_SUCCESS)
                    {
                        config->MaxFile = maxFileSize;
                        config->Flags = enableFlags;
                        config->Level = enableLevel;
                    }
                }
                RegCloseKey(providerKey);
            }
        }
        RegCloseKey(hKey);
    }

    return ret;
}

#define MAX_SESSION_NAME 1024
#define MAX_LOG_NAME     1024

void StopTracing(void)
{
    LONG                    retCode = ERROR_SUCCESS;
    EVENT_TRACE_PROPERTIES *pSessionProperties = NULL;
    ULONG                   BufferSize = 0;
    BOOL                    bIsVistaOrLater = TRUE;

    BufferSize = sizeof(EVENT_TRACE_PROPERTIES) + MAX_SESSION_NAME + MAX_LOG_NAME;
    pSessionProperties = (EVENT_TRACE_PROPERTIES *)malloc(BufferSize);
    if (NULL == pSessionProperties)
    {
        printfd("Failed at stopping trace and autologger sessions due to insufficient memory.\n");
        return;
    }

    ZeroMemory(pSessionProperties, BufferSize);
    pSessionProperties->Wnode.BufferSize = BufferSize;
    pSessionProperties->Wnode.Guid = ProviderGuid;

    pSessionProperties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    pSessionProperties->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + MAX_LOG_NAME;

    retCode = ControlTrace((TRACEHANDLE)NULL, LOGSESSION_NAME, pSessionProperties, EVENT_TRACE_CONTROL_STOP);
    // don't bother reporting since the tracing session wasn't running
    if (retCode != ERROR_WMI_INSTANCE_NOT_FOUND)
    {
        DisplayStatus(retCode, "stopping trace session");
    }

    if (bIsVistaOrLater)
    {
        retCode = ControlTrace((TRACEHANDLE)NULL, AUTOLOGGER_NAME, pSessionProperties, EVENT_TRACE_CONTROL_STOP);
        // don't bother reporting since the autologger session wasn't running
        if (retCode != ERROR_WMI_INSTANCE_NOT_FOUND)
        {
            DisplayStatus(retCode, "stopping autologger session");
        }
    }

    if (pSessionProperties)
    {
        free(pSessionProperties);
    }
}

void StartAutoLogger
(
    ULONG Flags,
    ULONG Level,
    ULONG MaxFile
)
{
    SetAutoLogger(TRUE, Flags, Level, MaxFile);
}


void StopAutoLogger(void)
{
    SetAutoLogger(FALSE, 0, 0, 0);
}

void ParseLogsMsg(void)
{
    DisplayStatus(ERROR_SUCCESS, "Log Parsing");
}
