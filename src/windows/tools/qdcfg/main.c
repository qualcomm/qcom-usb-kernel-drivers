/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M A I N . C

GENERAL DESCRIPTION
    This file implements the main entry point and command-line interface for
    the qdcfg driver configuration utility.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "main.h"
#include "reg.h"
#include "trace.h"
#include "../../qcversion.h"

CMD_LINE_ARGS cmdArgs;

#define NUM_SUPPORTED_ENTRIES 59

CMD_COMMAND SupportedCommands[] =
{
    {"QCDriverDebugMask",       "0: off(default), 1 : on\n" \
     "                                [opt Flags(default 0x7fffffff)]\n" \
     "                                [opt Level(default 0xff)]\n" \
     "                                [opt MaxFile in MB(default 100, 0: unlimited)]",  TRUE,   QC_DEV_TYPE2_NONE,   0,      ""},
    {"MPNumClients",            "0x10 (default) to 0xFF",                               FALSE,  QC_DEV_TYPE2_NET,   0x10,   ""},
    {"MPReconfigDelay",         "0x1F4 (default), range from 0xC8 to 0x1388 in ms",     TRUE,   QC_DEV_TYPE2_NET,   0x1F4,  ""},
    {"QCDevDisableQoS",         "0: Enable(default)\n" \
     "                                1: Default Flow Only\n" \
     "                                2: No QoS\n" \
     "                                3: More UL buffers, no QoS",          TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDevDisableIPMode",      "0: Enable(default)  1: Disable",                       TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDriverTLPSize",         "UL aggregation size, 0 to 0x10000 (default 0x8000)",   TRUE,   QC_DEV_TYPE2_NET,   0x8000, ""},
    {"QCMPEnableTLP",           "0: Disable  1: Enable(default)",                       TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCDeviceFunction",        "0 (default)  1: bi-directional GPS",       FALSE,  QC_DEV_TYPE2_PORTS | QC_DEV_TYPE2_USB, 0, ""},
    {"QCRuntimeDeviceClass",    "1: CDMA(default)  2: GSM/UMTS/LTE",        TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCDriverTxBuffers",       "4 (default), range from 0x2 to 0xF",       FALSE,  QC_DEV_TYPE2_NONE,  4,      ""},
    {"QMICtrlCid",              "0 (default) to 255",                       FALSE,  QC_DEV_TYPE2_NET,   0,      ""},
    {"QMIFuzzingOn",            "0 (default) to 1",                         FALSE,  QC_DEV_TYPE2_NET,   0,      ""},
    {"QCMPEnableMBIMUL",        "0: Disable  1: Enable(default)",           TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCMPEnableMBIMDL",        "0: Disable  1: Enable(default)",           TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCIgnoreErrors",          "0 (default) to 0xFFFFFFFF",                TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDevDLPausePktCount",    "1 to 0xFFFFFFFF, 0xC8 is default",         TRUE,   QC_DEV_TYPE2_NET,   0xC8,   ""},
    {"DLResume",                "any value including 0",                    TRUE,   QC_DEV_TYPE2_NET,   -1,     ""},
    {"QCDriverMTUSize",         "1000 to 1500, 1500 is default",            TRUE,   QC_DEV_TYPE2_NET,   1500,   ""},
    {"QCDriverTransmitTimer",       "1 to 1000, 2 is default",              TRUE,   QC_DEV_TYPE2_NET,   2,      ""},
    {"QCDriverAggregationEnabled",  "0:Disable(default) 1:Enable",          TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDriverAggregationTime",     "1000 to 5000, 2000 is default",            TRUE,   QC_DEV_TYPE2_PORTS, 2000,   ""},
    {"QCDriverAggregationSize",     "1024*4 to 1024*30, 1024*20 is default",    TRUE,   QC_DEV_TYPE2_PORTS, 1024 * 20,  ""},
    {"QCDriverResolutionTime",      "1 to 5, 1 by default",                     FALSE,  QC_DEV_TYPE2_PORTS, 1,      ""},
    {"QCMPEnableQCDualIpFc",    "0: Disable(Default) 1: Enable",            TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDriverTLPMaxPackets",   "0 to 1024, 0 is default",                  TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDriverDeregister",      "0: Disable(default) 1: Enable",            TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDriverDisableTimerResolution",  "0(default) 1: Disable",            FALSE,  QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDriverThreadPriority",  "1 to 32, 26 is default",                   TRUE,   QC_DEV_TYPE2_NET,   26,     ""},
    {"QCDriverL2Buffers",       "2 to 100, 8 by default",                   TRUE,   QC_DEV_TYPE2_NET,   8,      ""},
    {"QCMPEnableDLTLP",         "0: Disable  1: Enable(default)",           TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCMPEnableQMAPV1",        "0: Disable  1: Enable(default)",           TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCMPEnableQMAPV2",        "0: Disable  1: Enable(default)",           TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCMPEnableQMAPV3",        "0: Disable  1: Enable(default)",           TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCMPEnableQMAPV4",        "0: Disable  1: Enable(default)",           TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCMPDisableQMI",          "0: Disable(default)  1: Enable",           TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCDriverMediaConnect",    "IP address to be set Statically, \n" \
     "                                For Ex. 0xAB30A8C0 for IP addr 192.168.48.171",   FALSE,  QC_DEV_TYPE2_NET,   -1,    ""},
    {"QCDriverMediaDisconnect", "any value including 0",                    FALSE,  QC_DEV_TYPE2_NET,   -1,     ""},
    {"QCDriverQMAPDLPause",     "any value including 0",                    FALSE,  QC_DEV_TYPE2_NET,   -1,     ""},
    {"QCDriverQMAPDLResume",    "any value including 0",                    FALSE,  QC_DEV_TYPE2_NET,   -1,     ""},
    {"QCMPNDPSignature",        "0 to 0xFFFFFFFF (default 0x00535049)",     TRUE,   QC_DEV_TYPE2_NET,   0x00535049, ""},
    {"QCMPMuxId",                   "0 (default) to 255",                   TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDriverDLAggregationSize",   "0 (default) to 1024*64",               TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDriverDLMaxPackets",        "0 (default) to 1024",                  TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDriverEnableMBIM",      "0: Disable  1: Enable(default)",           TRUE,   QC_DEV_TYPE2_NET,   1,      ""},
    {"QCDeviceMuxEnable",       "0: Disable  1: Enable(default)",           TRUE,   QC_DEV_TYPE2_FLT,   1,      ""},
    {"QCDeviceStartIf",         "0 (default) to 255",               TRUE,   QC_DEV_TYPE2_FLT,   0,      ""},
    {"QCDeviceNumIf",           "1 (default) to 32",                TRUE,   QC_DEV_TYPE2_FLT,   1,      ""},
    {"QCDeviceNumMuxIf",        "7 (default) to 32",                TRUE,   QC_DEV_TYPE2_FLT,   7,      ""},
    {"QCDriverNumTLPBuffers",   "2 to 64, 10 is default",           TRUE,   QC_DEV_TYPE2_NET,   10,     ""},
    {"QCMPQMAPDLMinPadding",    "0 (default) to 64",                TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"MPMaxDataReceives",       "1 to 2000, 500 is default",        TRUE,   QC_DEV_TYPE2_NET,   500,    ""},
    {"QCMPBindIFId",            "0 (default) to 255",               FALSE,  QC_DEV_TYPE2_NET,   0,      ""},
    {"QCMPBindEPType",          "0 (default) to 255",               TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCMPDisableQMAPFC",       "0: Disable(default)  1: Enable",   TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCMPMaxPendingQMIReqs",   "3 to 0xFFFF, 12 is default",       TRUE,   QC_DEV_TYPE2_NET,   12,     ""},
    {"QCMPFakeDeviceIMSI",      "0: Disable(default)  1: Enable",   TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCMPFakeDeviceICCID",     "0: Disable(default)  1: Enable",   TRUE,   QC_DEV_TYPE2_NET,   0,      ""},
    {"QCMPEnableData5G",        "0: Disable(default)  1: Enable",   FALSE,  QC_DEV_TYPE2_NET,   0,      ""},
    {"QCDeviceZLPEnabled",      "0: Disable  1: Enable(default)",   TRUE,   QC_DEV_TYPE2_PORTS, 1,      ""}
};

enum QC_ERR_CODE
{
    QC_NO_ERROR = 0,
    QC_ERR_DEV_NAME,
    QC_ERR_ENTRY_NAME_NO_SUPPORT,
    QC_ERR_ENTRY_NAME,
    QC_ERR_VALUE,
    QC_ERR_UNKNOWN_ARG,
    QC_ERR_INVALID_CMD
};

char *QcErrorMessage[] =
{
   "no error",                                     // NO_ERROR
   "valid device name must be supplied",            // ERR_DEV_NAME
   "registry entry name not supported",             // ERR_DEV_NAME_NO_SUPPORT
   "valid registry entry name must be supplied",    // ERR_ENTRY_NAME
   "entry value must be supplied to the -s option", // ERR_VALUE
   "unknown arg",                                   // ERR_UNKNOWN_ARG
   "invalid command"                                // ERR_INVALID_CMD
};

BOOLEAN ValidateEntryName(PCHAR EntryName)
{
    int i;
    BOOLEAN match = FALSE;

    for (i = 0; i < NUM_SUPPORTED_ENTRIES; i++)
    {
        if (strcmp(SupportedCommands[i].entry, EntryName) == 0)
        {
            match = TRUE;
            break;
        }
    }

    return match;
} // ValidateEntryName

VOID DisplaySupportedEntryNames(VOID)
{
    int i;

    printfd("\n   %-28s\t%s\n", "Supported Reg Entry", "Value");

    for (i = 0; i < NUM_SUPPORTED_ENTRIES; i++)
    {
        printfd("   %-28s %s\n", SupportedCommands[i].entry, SupportedCommands[i].usage);
    }
}

UINT getSupportedEntries(void)
{
    return NUM_SUPPORTED_ENTRIES;
}

int getEntryLen(UINT index)
{
    if (index < NUM_SUPPORTED_ENTRIES)
    {
        return strlen(SupportedCommands[index].entry) + 1;
    }
    return -1;
}

int getUsageLen(UINT index)
{
    if (index < NUM_SUPPORTED_ENTRIES)
    {
        return strlen(SupportedCommands[index].usage) + 1;
    }
    return -1;
}

int getCommand(CMD_COMMAND *buffer, UINT index)
{
    size_t len = getEntryLen(index);
    if (buffer->entry != NULL)
    {
        strncpy_s(buffer->entry, len + 1, SupportedCommands[index].entry, len);
        len = getUsageLen(index);
        if (buffer->usage != NULL)
        {
            strncpy_s(buffer->usage, len + 1, SupportedCommands[index].usage, len);
            // TODO: copy friendly usage
            // buffer->intro = SupportedCommands[index].intro;
        }
        buffer->defaultVal = SupportedCommands[index].defaultVal;
        buffer->devType = SupportedCommands[index].devType;
        buffer->isExposed = SupportedCommands[index].isExposed;
        return 0;
    }
    return ERROR_INSUFFICIENT_BUFFER;
}

int executeCommand(char *entry, char action, char **params, int paramLen, BOOL device, char *deviceName)
{
    if (entry == NULL || strlen(entry) == 0)
    {
        return QC_ERR_ENTRY_NAME;
    }
    if (action != 's' && action != 'g')
    {
        return QC_ERR_INVALID_CMD;
    }
    for (int i = 0; i < NUM_SUPPORTED_ENTRIES; i++)
    {
        if (strcmp(entry, SupportedCommands[i].entry) == 0)
        {
            int argc = 2;
            if (action == 's')
            {
                argc += paramLen;
            }
            if (device == TRUE)
            {
                argc += 2;
            }
            LPTSTR *argv = (LPTSTR *)malloc(sizeof(LPTSTR *) * argc);
            if (argv == NULL)
            {
                return ERROR_NOT_ENOUGH_MEMORY;
            }
            LPTSTR *iter = argv;
            if (action == 's')
            {
                *iter++ = "-s";
            }
            else
            {
                *iter++ = "-g";
            }
            *iter++ = entry;
            if (action == 's')
            {
                for (int j = 0; j < paramLen; j++)
                {
                    *iter++ = params[j];
                }
            }
            if (device == TRUE)
            {
                *iter++ = "-n";
                *iter = deviceName;
            }

            return processCommand(argc, argv);
        }
    }
    return QC_ERR_ENTRY_NAME_NO_SUPPORT;
}

void setPrintDelegate(PRINTF_DELEGATE printer)
{
    hidden_printer = printer;
}

void printfd(const char *const format, ...)
{
    va_list args;
    va_start(args, format);
    if (hidden_printer == NULL)
    {
        vprintf(format, args);
    }
    else
    {
        char buffer[2048];
        vsnprintf(buffer, 2048, format, args);
        hidden_printer(buffer);
    }
    va_end(args);
}

int processCommand(int argc, LPTSTR argv[])
{
    PCHAR p;

    ZeroMemory(&cmdArgs, sizeof(CMD_LINE_ARGS));
    int ret = GetCommandLineArguments(argc, argv, &cmdArgs);
    if (ret == -1)
    {
        return 0;    // config global DebugMask or query usage, ignore other errors
    }
    if (ret != 0)
    {
        return ret;
    }

    p = cmdArgs.NIC_Name;
    if ((*p == 'c' || *p == 'C') ||
        (*(p + 1) == 'o' || *(p + 1) == 'O') ||
        (*(p + 2) == 'm' || *(p + 2) == 'M'))
    {
        cmdArgs.IsCOM = TRUE;
    }
    else
    {
        cmdArgs.IsCOM = FALSE;
    }

    printfd("------------ Input --------------\n");
    printfd("Dev Name: <%s>\n", cmdArgs.NIC_Name);
    printfd("Debug   : <0x%x>\n", cmdArgs.DebugMask);
    printfd("IsCOM   : %s\n", (cmdArgs.IsCOM == TRUE ? "TRUE" : "FALSE"));
    printfd("Entry   : <%s>\n", cmdArgs.EntryName);
    if (cmdArgs.DebugFlags != 0)
    {
        printfd("Mask    : <0x%x>\n", cmdArgs.DebugFlags);
    }
    if (cmdArgs.DebugLevel != 0)
    {
        printfd("Level   : <0x%x>\n", cmdArgs.DebugLevel);
    }
    if (cmdArgs.DebugMaxFile != 0)
    {
        printfd("MaxFile   : <%d>\n", cmdArgs.DebugMaxFile);
    }
    printfd("Value   : <0x%x>\n", cmdArgs.EntryValue);
    printfd("Action  : %d\n", cmdArgs.Action);
    printfd("\n------------ Output -------------\n");

    if (cmdArgs.EntryName[0] != 0)
    {
        if (FALSE == ValidateEntryName(cmdArgs.EntryName))
        {
            Usage(QC_ERR_ENTRY_NAME_NO_SUPPORT, cmdArgs.EntryName);
            return QC_ERR_ENTRY_NAME_NO_SUPPORT;
        }
    }

    if (strcmp(cmdArgs.EntryName, "DLResume") == 0)
    {
        cmdArgs.Action = 2;  // not setting registry
    }

    if (strcmp(cmdArgs.EntryName, "QCDriverMediaConnect") == 0)
    {
        cmdArgs.Action = 2;  // not setting registry
    }

    if (strcmp(cmdArgs.EntryName, "QCDriverMediaDisconnect") == 0)
    {
        cmdArgs.Action = 2;  // not setting registry
    }

    if (strcmp(cmdArgs.EntryName, "QCDriverQMAPDLPause") == 0)
    {
        cmdArgs.Action = 2;  // not setting registry
    }

    if (strcmp(cmdArgs.EntryName, "QCDriverQMAPDLResume") == 0)
    {
        cmdArgs.Action = 2;  // not setting registry
    }

    ProcessRegistryEntry(&cmdArgs);
    printfd("\n");

    return 0;
}  // main

int GetCommandLineArguments(int argc, char *argv[], PCMD_LINE_ARGS Args)
{
    int i;

    Args->EntryName[0] = 0;
    Args->Action = -1;

    for (i = 0; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] != '\0' && argv[i][2] == '\0')
        {
            switch (argv[i][1])
            {
                case 'h':
                    Usage(QC_NO_ERROR, NULL);
                    return -1;
                case 'n':
                    if (i == argc - 1)
                    {
                        Usage(QC_ERR_DEV_NAME, NULL);
                        return QC_ERR_DEV_NAME;
                    }
                    sprintf_s(Args->NIC_Name, sizeof(Args->NIC_Name), "%s", argv[++i]);
                    break;
                case 'g':
                case 's':
                {
                    if (i == argc - 1)
                    {
                        Usage(QC_ERR_ENTRY_NAME, NULL);
                        return QC_ERR_ENTRY_NAME;
                    }
                    if (argv[i][1] == 'g')
                    {
                        Args->Action = 0;
                    }
                    else
                    {
                        Args->Action = 1;
                    }
                    sprintf_s(Args->EntryName, sizeof(Args->EntryName), "%s", argv[++i]);

                    // examine EntryValue
                    if ((Args->Action == 1) && (i == argc - 1))
                    {
                        Usage(QC_ERR_VALUE, NULL);
                        return QC_ERR_VALUE;
                    }
                    if ((Args->Action == 0) && (i == argc - 1)) // no EntryValue
                    {
                        Args->EntryValue = 0;
                        break;
                    }

                    if (argv[i + 1][0] == '-' && argv[i + 1][1] != '\0') // another arg
                    {
                        Args->EntryValue = 0;
                        break;
                    }
                    else
                    {
                        // get EntryValue
                        PCHAR p, p1;

                        p = argv[++i];
                        if (strlen(p) == 0)
                        {
                            Usage(QC_ERR_VALUE, NULL);
                            return QC_ERR_VALUE;
                        }
                        if (strlen(p) > 2)
                        {
                            if (*p == '0')
                            {
                                p1 = p + 1;
                                if (*p1 == 'x' || *p1 == 'X')
                                {
                                    p = p1 + 1;
                                }
                            }
                        }

                        if (strcmp(Args->EntryName, "QCDevicePidList") == 0)
                        {
                            sscanf_s(p, "%s", Args->EntryValueStr, (unsigned)sizeof(Args->EntryValueStr));
                            Args->EntryValueisStr = 0x01;
                        }

                        sscanf_s(p, "%x", &(Args->EntryValue));

                        // parse optional parameters
                        // for now, only optional params are for entry QCDriverDebugMask
                        //  e.g. qdcfg -s QCDriverDebugMask 1 0x7fffffff 0xff 0
                        if ((strcmp(Args->EntryName, "QCDriverDebugMask") == 0) && (Args->EntryValue != 0))
                        {
                            Args->DebugMaxFile = MAX_FILE_SIZE;
                            if ((i != argc - 1) && (argv[i + 1][0] != '-')) // mask
                            {
                                p = argv[++i];
                                sscanf_s(p, "%x", &(Args->DebugFlags));

                                if ((i != argc - 1) && (argv[i + 1][0] != '-')) // level
                                {
                                    p = argv[++i];
                                    sscanf_s(p, "%x", &(Args->DebugLevel));
                                }

                                if ((i != argc - 1) && (argv[i + 1][0] != '-')) // maxfile
                                {
                                    p = argv[++i];
                                    sscanf_s(p, "%d", &(Args->DebugMaxFile));
                                }
                            }
                        }
                    }
                    break;
                }
                default:
                    Usage(QC_ERR_UNKNOWN_ARG, argv[i]);
                    return QC_ERR_UNKNOWN_ARG;
            } // switch
        }
        else
        {
            Usage(QC_ERR_INVALID_CMD, NULL);
            if ((strcmp(cmdArgs.EntryName, "QCDriverDebugMask") == 0) && (cmdArgs.Action == 1))
            {
                return -1; // ignore optional params for QCDriverDebugMask
            }
            else
            {
                return QC_ERR_INVALID_CMD;
            }
        }
    }

    // Check required arguments
    if (Args->NIC_Name[0] == 0)
    {
        Usage(QC_ERR_DEV_NAME, NULL);
        if ((strcmp(cmdArgs.EntryName, "QCDriverDebugMask") == 0) && (cmdArgs.Action == 1))
        {
            return -1;
        }
        return QC_ERR_DEV_NAME;
    }
    return QC_NO_ERROR;
}  // GetCommandLineArguments

void Usage(int ErrorCode, PCHAR Info)
{
    char *usage = "\n"
        "Qualcomm Driver Configuration Tool\n"
        "   Version 4.4 Copyright(c) Qualcomm Inc. 2011. All rights reserved.\n\n"
        "   Usage:\n"
        "      qdcfg <-n Device_Name> [-<g|s> <EntryName> [EntryValue] [OptParams]] [-h]\n\n"
        "   Options:\n"
        "      -n: the exact name of the device in Device Manager\n"
        "      -g: get EntryValue of EntryName\n"
        "      -s: set EntryValue (default 0) in HEX to EntryName + optional params\n"
        "      -h: help\n\n"
        "   Example:\n"
        "      qdcfg -g QCDevDisableQoS -n \"Qualcomm Wireless Ethernet Adapter 7001\"\n"
        "      qdcfg -s QCDevDisableQoS 2 -n \"Qualcomm Wireless HS-USB Ethernet Adapter 9001\"\n"
        "      qdcfg -s QCDriverDebugMask 1 -n \"Qualcomm USB Modem 3197\"\n"
        "      qdcfg -s QCDriverDebugMask 1 -n \"Qualcomm HS-USB Modem 9001\"\n"
        "      qdcfg -s QCDriverDebugMask 1 -n \"Qualcomm Diagnostics Interface 3197 (COM5)\"\n"
        "      qdcfg -s QCDriverDebugMask 1 -n \"Qualcomm HS-USB Diagnostics 9001 (COM5)\"\n"
        "      qdcfg -s QCDriverDebugMask 1 -n \"Qualcomm WinMobile Diagnostics 3200 (COM5)\"\n"
        "      qdcfg -s QCDriverDebugMask 1 0x03ffffff 0xff 0 -n \"Qualcomm NMEA Device (COM5)\"\n"
        "      qdcfg -s QCDeviceFunction 1 -n \"Qualcomm HS-USB NMEA 9001 (COM5)\"\n"
        "      qdcfg -g QCDeviceFunction -n \"Qualcomm WinMobile NMEA 3200 (COM5)\"\n";


    if ((strcmp(cmdArgs.EntryName, "QCDriverDebugMask") == 0) && (cmdArgs.Action == 1))
    {
        if (cmdArgs.EntryValue == 0)
        {
            StopTracing();
            StopAutoLogger();
        }
        else
        {
            StartTracing(cmdArgs.DebugFlags, cmdArgs.DebugLevel, cmdArgs.DebugMaxFile);
            StartAutoLogger(cmdArgs.DebugFlags, cmdArgs.DebugLevel, cmdArgs.DebugMaxFile);
        }
    }
    else
    {
        printfd("%s", usage);
        DisplaySupportedEntryNames();

        if (ErrorCode != NO_ERROR)
        {
            if (Info == NULL)
            {
                printfd("\n   Error: %s\n", QcErrorMessage[ErrorCode]);
            }
            else
            {
                printfd("\n   Error: %s: <%s>\n", QcErrorMessage[ErrorCode], Info);
            }
        }
    }
}  // Usage

VOID ProcessRegistryEntry(CMD_LINE_ARGS *Args)
{
    char controlFileName[SERVICE_FILE_BUF_LEN];
    char deviceName[SERVICE_FILE_BUF_LEN];
    HANDLE hDevice;
    DWORD bytesReturned = 0;

    ZeroMemory(controlFileName, SERVICE_FILE_BUF_LEN);
    QCWWAN_GetControlFileName(Args->NIC_Name, controlFileName);

    // Also set at run-time if driver is running
    if (strlen(controlFileName) > 0)
    {
        printfd("NIC control file name <%s>\n", controlFileName);

        sprintf_s(deviceName, sizeof(deviceName), "\\\\.\\%s", controlFileName);

        hDevice = CreateFile
        (
            deviceName,
            GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        printfd("\n");

        if (hDevice == INVALID_HANDLE_VALUE)
        {
            if ((cmdArgs.Action == 1) || (cmdArgs.Action == 2))
            {
                printfd("Error: couldn't open device, registry set statically.\n");
                printfd("Please re-connect device for changes to take effect.");
            }
            return;
        }

        if (strcmp(cmdArgs.EntryName, "QCDriverMediaConnect") == 0)
        {
            DeviceIoControl(
                hDevice,
                IOCTL_QCDEV_MEDIA_CONNECT,
                (LPVOID) & (Args->EntryValue),
                (DWORD)sizeof(ULONG),
                (LPVOID)NULL,
                (DWORD)0,
                &bytesReturned,
                NULL
            );
            CloseHandle(hDevice);
            return;
        }

        if (strcmp(cmdArgs.EntryName, "QCDriverMediaDisconnect") == 0)
        {
            DeviceIoControl(
                hDevice,
                IOCTL_QCDEV_MEDIA_DISCONNECT,
                (LPVOID) & (Args->EntryValue),
                (DWORD)sizeof(ULONG),
                (LPVOID)NULL,
                (DWORD)0,
                &bytesReturned,
                NULL
            );
            CloseHandle(hDevice);
            return;
        }

        if (strcmp(cmdArgs.EntryName, "QCDriverQMAPDLPause") == 0)
        {
            DeviceIoControl(
                hDevice,
                IOCTL_QCDEV_PAUSE_QMAP_DL,
                (LPVOID) & (Args->EntryValue),
                (DWORD)sizeof(ULONG),
                (LPVOID)NULL,
                (DWORD)0,
                &bytesReturned,
                NULL
            );
            CloseHandle(hDevice);
            return;
        }

        if (strcmp(cmdArgs.EntryName, "QCDriverQMAPDLResume") == 0)
        {
            DeviceIoControl(
                hDevice,
                IOCTL_QCDEV_RESUME_QMAP_DL,
                (LPVOID) & (Args->EntryValue),
                (DWORD)sizeof(ULONG),
                (LPVOID)NULL,
                (DWORD)0,
                &bytesReturned,
                NULL
            );
            CloseHandle(hDevice);
            return;
        }

        if (strcmp(cmdArgs.EntryName, "DLResume") == 0)
        {
            DeviceIoControl(
                hDevice,
                IOCTL_QCDEV_RESUME_DL,
                (LPVOID) & (Args->EntryValue),
                (DWORD)sizeof(ULONG),
                (LPVOID)NULL,
                (DWORD)0,
                &bytesReturned,
                NULL
            );
            CloseHandle(hDevice);
            return;
        }

        if ((strcmp(cmdArgs.EntryName, "QCDriverDebugMask") != 0) ||
            (cmdArgs.Action != 1))
        {
            CloseHandle(hDevice);
            return;
        }

        Args->DebugMask = Args->EntryValue;

        if (DeviceIoControl(
            hDevice,
            IOCTL_QCDEV_SET_DBG_UMSK,
            (LPVOID) & (Args->DebugMask),
            (DWORD)sizeof(ULONG),
            (LPVOID)NULL,
            (DWORD)0,
            &bytesReturned,
            NULL
            )
            )
        {
            printfd("Registry set successfully at run-time: 0x%x\n", Args->DebugMask);
        }
        else
        {
            if (DeviceIoControl(
                hDevice,
                IOCTL_QCUSB_SET_DBG_UMSK,
                (LPVOID) & (Args->DebugMask),
                (DWORD)sizeof(ULONG),
                (LPVOID)NULL,
                (DWORD)0,
                &bytesReturned,
                NULL
                )
                )
            {
                printfd("Registry set successfully at run-time: 0x%x\n", Args->DebugMask);
            }
            else
            {
                printfd("Error: Registry couldn't be set at run-time, please power cycle device. %d\n", GetLastError());
            }
        }

        CloseHandle(hDevice);
    }
    else
    {
        if (cmdArgs.Action == 1)
        {
            printfd("If successful, please re-connect device for changes to take effect.\n");
        }
    }
}  // ProcessRegistryEntry

const char *getQUDVersionNum()
{
#ifdef QC_USB_DRIVERS_PRODUCT_VERSION
    return QCSTR(QC_USB_DRIVERS_PRODUCT_VERSION);
#else
    return "1.00.00.0";
#endif // DEBUG
}
