/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          R E G . C

GENERAL DESCRIPTION
    This file implements registry access functions for querying and
    configuring Qualcomm USB driver settings.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "main.h"
#include "reg.h"

extern CMD_LINE_ARGS cmdArgs;

BOOL QueryKey
(
    HKEY  hKey,
    PCHAR DeviceFriendlyName,
    PCHAR ControlFileName,
    PCHAR FullKeyName
)
{
    TCHAR    subKey[MAX_KEY_LENGTH];
    DWORD    nameLen;
    TCHAR    className[MAX_PATH] = TEXT("");
    DWORD    classNameLen = MAX_PATH;
    DWORD    numSubKeys = 0, subKeyMaxLen;
    DWORD    classMaxLen;
    DWORD    numKeyValues, valueMaxLen, valueDataMaxLen;
    DWORD    securityDescLen;
    FILETIME lastWriteTime;
    char     fullKeyName[MAX_KEY_LENGTH];
    DWORD    i, retCode;

    // Get the class name and the value count.
    retCode = RegQueryInfoKey
    (
        hKey,
        className,
        &classNameLen,
        NULL,
        &numSubKeys,
        &subKeyMaxLen,
        &classMaxLen,
        &numKeyValues,
        &valueMaxLen,
        &valueDataMaxLen,
        &securityDescLen,
        &lastWriteTime
    );

    // Enumerate the subkeys, until RegEnumKeyEx fails.

    if (numSubKeys)
    {
        for (i = 0; i < numSubKeys; i++)
        {
            nameLen = MAX_KEY_LENGTH;
            retCode = RegEnumKeyEx
            (
                hKey, i,
                subKey,
                &nameLen,
                NULL,
                NULL,
                NULL,
                &lastWriteTime
            );
            if (retCode == ERROR_SUCCESS)
            {
                BOOL result;

                // _tprintf(TEXT("C-(%d) %s\n"), i+1, subKey);
                sprintf_s(fullKeyName, sizeof(fullKeyName), "%s\\%s", FullKeyName, subKey);
                result = FindDeviceInstance
                (
                    fullKeyName,
                    DeviceFriendlyName,
                    ControlFileName
                );
                if (result == TRUE)
                {
                    printfd("Device HW Key <%s>\n", fullKeyName);
                    if ((strcmp(cmdArgs.EntryName, "QCDeviceMuxEnable") == 0) ||
                        (strcmp(cmdArgs.EntryName, "QCDeviceStartIf") == 0) ||
                        (strcmp(cmdArgs.EntryName, "QCDeviceNumIf") == 0) ||
                        (strcmp(cmdArgs.EntryName, "QCDeviceNumMuxIf") == 0))
                    {
                    }
                    else
                    {
                        return TRUE;
                    }
                }
            }
        }
    }

    return FALSE;
}  // QueryKey

BOOL FindDeviceInstance
(
    PTCHAR InstanceKey,
    PCHAR  DeviceFriendlyName,
    PCHAR  ControlFileName
)
{
    HKEY hTestKey;
    BOOL ret = FALSE;

    if (RegOpenKeyEx
    (
        HKEY_LOCAL_MACHINE,
        InstanceKey,
        0,
        KEY_READ,
        &hTestKey
        ) == ERROR_SUCCESS
        )
    {
        ret = QCWWAN_GetEntryValue
        (
            hTestKey,
            DeviceFriendlyName,
            "QCDeviceControlFile",
            ControlFileName
        );

        RegCloseKey(hTestKey);
    }

    return ret;
}

BOOL QCWWAN_GetEntryValue
(
    HKEY  hKey,
    PCHAR DeviceFriendlyName,
    PCHAR EntryName,
    PCHAR ControlFileName
)
{
    DWORD retCode = ERROR_SUCCESS;
    TCHAR valueName[MAX_VALUE_NAME], swKey[MAX_VALUE_NAME];
    DWORD valueNameLen = MAX_VALUE_NAME;
    BOOL  instanceFound = FALSE;
    HKEY  hSwKey;

    valueName[0] = 0;

    // first get device friendly name
    retCode = RegQueryValueEx
    (
        hKey,
        "FriendlyName", // "DriverDesc", value name
        NULL,           // reserved
        NULL,           // returned type
        (LPBYTE)valueName,
        &valueNameLen
    );

    if (retCode == ERROR_SUCCESS)
    {
        valueName[valueNameLen] = 0;

        if (strcmp(valueName, DeviceFriendlyName) == 0)
        {
            instanceFound = TRUE;
        }
        // printfd("D-FriendlyName-%d <%s>\n", instanceFound, valueName);
    }
    else
    {
        // no FriendlyName, get DeviceDesc
        valueName[0] = 0;
        valueNameLen = MAX_VALUE_NAME;
        retCode = RegQueryValueEx
        (
            hKey,
            "DeviceDesc", // value name
            NULL,         // reserved
            NULL,         // returned type
            (LPBYTE)valueName,
            &valueNameLen
        );
        if (retCode == ERROR_SUCCESS)
        {
            int nameLen = strlen(DeviceFriendlyName) + 1;  // include NULL to match valueNameLen
            PCHAR p;

            valueName[valueNameLen] = 0;
            if (valueNameLen >= (DWORD)nameLen)
            {
                p = valueName + valueNameLen - nameLen; // for VISTA -- only compare later part
                if (strcmp(p, DeviceFriendlyName) == 0)
                {
                    instanceFound = TRUE;
                }
            }
            // printfd("   D-DeviceDesc:reg-%d (%uB) <%s>\n", instanceFound, strlen(valueName), valueName);
            // printfd("   D-DeviceDesc:frn-%d (%uB) <%s>\n", instanceFound, strlen(DeviceFriendlyName), DeviceFriendlyName);
        }
    }

    if (instanceFound == TRUE)
    {
        // Get "Driver" instance path
        valueName[0] = 0;
        valueNameLen = MAX_VALUE_NAME;
        retCode = RegQueryValueEx
        (
            hKey,
            "Driver",     // value name
            NULL,         // reserved
            NULL,         // returned type
            (LPBYTE)valueName,
            &valueNameLen
        );
        if (retCode == ERROR_SUCCESS)
        {
            // Construct device software key
            valueName[valueNameLen] = 0;
            sprintf_s(swKey, sizeof(swKey), "%s\\%s", QCNET_REG_SW_KEY, valueName);

            printfd("Device SW Key <%s>\n", swKey);

            // Open device software key
            retCode = RegOpenKeyEx
            (
                HKEY_LOCAL_MACHINE,
                swKey,
                0,
                (KEY_READ | KEY_WRITE),
                &hSwKey
            );

            if (retCode == ERROR_SUCCESS)
            {
                // Retrieve the control file name
                valueName[0] = 0;
                valueNameLen = MAX_VALUE_NAME;
                retCode = RegQueryValueEx
                (
                    hSwKey,
                    EntryName,    // value name
                    NULL,         // reserved
                    NULL,         // returned type
                    (LPBYTE)valueName,
                    &valueNameLen
                );

                // Check the other port control object
                if (retCode != ERROR_SUCCESS)
                {
                    retCode = RegQueryValueEx
                    (
                        hSwKey,
                        "AssignedPortForQCDevice",    // value name
                        NULL,         // reserved
                        NULL,         // returned type
                        (LPBYTE)valueName,
                        &valueNameLen
                    );
                }
                if (retCode == ERROR_SUCCESS)
                {
                    PCHAR p = (PCHAR)valueName + valueNameLen;

                    valueName[valueNameLen] = 0;

                    while ((p > valueName) && (*p != '\\'))
                    {
                        p--;
                    }
                    if (*p == '\\') p++;
                    strcpy_s(ControlFileName, SERVICE_FILE_BUF_LEN, p);
                }

                // set/get arbitrary entry under SW key
                if (cmdArgs.EntryName[0] != 0)
                {
                    // set
                    if (cmdArgs.Action == 1)
                    {
                        if (cmdArgs.EntryValueisStr == 0x01)
                        {
                            retCode = RegSetValueEx
                            (
                                hSwKey,
                                cmdArgs.EntryName,
                                0,            // reserved
                                REG_SZ,    // type
                                (LPBYTE)&cmdArgs.EntryValueStr,
                                strlen(cmdArgs.EntryValueStr) + 1
                            );
                            if (retCode == ERROR_SUCCESS)
                            {
                                printfd("Entry <%s> set to %s\n", cmdArgs.EntryName, cmdArgs.EntryValueStr);
                            }
                            else
                            {
                                printfd("Error: Entry <%s> failure: %u\n", cmdArgs.EntryName, retCode);
                            }
                        }
                        else
                        {
                            retCode = RegSetValueEx
                            (
                                hSwKey,
                                cmdArgs.EntryName,
                                0,            // reserved
                                REG_DWORD,    // type
                                (LPBYTE)&cmdArgs.EntryValue,
                                sizeof(DWORD)
                            );
                            if (retCode == ERROR_SUCCESS)
                            {
                                printfd("Entry <%s> set to 0x%x\n", cmdArgs.EntryName, cmdArgs.EntryValue);
                            }
                            else
                            {
                                printfd("Error: Entry <%s> failure: %u\n", cmdArgs.EntryName, retCode);
                            }
                        }
                    }
                    else if (cmdArgs.Action == 0)
                    {
                        // get
                        valueName[0] = 0;
                        valueNameLen = MAX_VALUE_NAME;
                        retCode = RegQueryValueEx
                        (
                            hSwKey,
                            cmdArgs.EntryName,    // value name
                            NULL,         // reserved
                            NULL,         // returned type
                            (LPBYTE)valueName,
                            &valueNameLen
                        );

                        if (retCode == ERROR_SUCCESS)
                        {
                            valueName[valueNameLen] = 0;

                            printfd("Entry <%s> has value of 0x%x\n", cmdArgs.EntryName, *((DWORD *)valueName));
                        }
                        else
                        {
                            printfd("Error: Entry <%s> does not exist\n", cmdArgs.EntryName);
                        }
                    }
                }

                RegCloseKey(hSwKey);
                return TRUE;
            }  // if (retCode == ERROR_SUCCESS)
        }  // if (retCode == ERROR_SUCCESS)
        else
        {
            printfd("Error: cannot get device software key, retCode %u\n", retCode);
        }
    }  // if (instanceFound == TRUE)

    return FALSE;
}

BOOL QCWWAN_GetControlFileName
(
    PCHAR DeviceFriendlyName,
    PCHAR ControlFileName
)
{
    HKEY hTestKey;

    if (RegOpenKeyEx
    (
        HKEY_LOCAL_MACHINE,
        TEXT(QCNET_REG_HW_KEY),
        0,
        KEY_READ,
        &hTestKey
        ) == ERROR_SUCCESS
        )
    {
        TCHAR    subKey[MAX_KEY_LENGTH];
        DWORD    nameLen;
        TCHAR    className[MAX_PATH] = TEXT("");
        DWORD    classNameLen = MAX_PATH;
        DWORD    numSubKeys = 0, subKeyMaxLen;
        DWORD    classMaxLen;
        DWORD    numKeyValues, valueMaxLen, valueDataMaxLen;
        DWORD    securityDescLen;
        FILETIME lastWriteTime;
        DWORD    i, retCode;

        // Get the class name and the value count.
        retCode = RegQueryInfoKey
        (
            hTestKey,
            className,
            &classNameLen,
            NULL,
            &numSubKeys,
            &subKeyMaxLen,
            &classMaxLen,
            &numKeyValues,
            &valueMaxLen,
            &valueDataMaxLen,
            &securityDescLen,
            &lastWriteTime
        );

        // Enumerate the subkeys, until RegEnumKeyEx fails.
        if (numSubKeys)
        {
            for (i = 0; i < numSubKeys; i++)
            {
                nameLen = MAX_KEY_LENGTH;
                retCode = RegEnumKeyEx
                (
                    hTestKey, i,
                    subKey,
                    &nameLen,
                    NULL,
                    NULL,
                    NULL,
                    &lastWriteTime
                );
                if (retCode == ERROR_SUCCESS)
                {
                    BOOL result;

                    result = QueryUSBDeviceKeys
                    (
                        subKey,
                        DeviceFriendlyName,
                        ControlFileName
                    );
                    if (result == TRUE)
                    {
                        printfd("QueryKey: TRUE\n");
                        return TRUE;
                    }
                }
            }
        }

        RegCloseKey(hTestKey);
        if ((strcmp(cmdArgs.EntryName, "QCDeviceMuxEnable") == 0) ||
            (strcmp(cmdArgs.EntryName, "QCDeviceStartIf") == 0) ||
            (strcmp(cmdArgs.EntryName, "QCDeviceNumIf") == 0) ||
            (strcmp(cmdArgs.EntryName, "QCDeviceNumMuxIf") == 0))
        {
        }
        else
        {
            printfd("Error: Device <%s> does not exist\n", DeviceFriendlyName);
        }

        return retCode;
    }

    printfd("Error: Fail to access registry\n");

    return FALSE;
}

BOOL QueryUSBDeviceKeys
(
    PTCHAR InstanceKey,
    PCHAR  DeviceFriendlyName,
    PCHAR  ControlFileName
)
{
    HKEY hTestKey;
    char fullKeyName[MAX_KEY_LENGTH];
    BOOL ret = FALSE;

    sprintf_s(fullKeyName, sizeof(fullKeyName), "%s\\%s", QCNET_REG_HW_KEY, InstanceKey);

    // printfd("B-full key name: [%s]\n", fullKeyName);

    if (RegOpenKeyEx
    (
        HKEY_LOCAL_MACHINE,
        fullKeyName,
        0,
        KEY_READ,
        &hTestKey
        ) == ERROR_SUCCESS
        )
    {
        ret = QueryKey(hTestKey, DeviceFriendlyName, ControlFileName, fullKeyName);
        RegCloseKey(hTestKey);

        return ret;
    }
    else
    {
        printfd("Error: couldn't open registry\n");
    }

    return FALSE;
}  // QueryUSBDeviceKeys

ULONG InspectLogging(PCHAR deviceFriendlyName, PULONG errorCode)
{
    // return 0 if logging is disabled, 1 otherwise
    if (deviceFriendlyName == NULL)
    {
        *errorCode = ERROR_INVALID_PARAMETER;
        return 0;
    }

    DWORD entryValue = 0;
    *errorCode = InspectEntry(deviceFriendlyName, "QCDriverDebugMask", &entryValue);
    if (*errorCode)
    {
        return 0;
    }
    else
    {
        return entryValue;
    }
}

ULONG InspectEntry(PCHAR deviceFriendlyName, PCHAR entryName, PDWORD entryValue)
{
    // open parent regkey for all usb devices
    HKEY hKey;
    ULONG errorCode = RegOpenKeyEx
    (
        HKEY_LOCAL_MACHINE,
        TEXT(QCNET_REG_HW_KEY),
        0,
        KEY_READ,
        &hKey
    );

    if (errorCode == ERROR_SUCCESS)
    {
        // query # of USB hwkeys
        DWORD numSubKeys;
        errorCode = RegQueryInfoKey
        (
            hKey,
            NULL,
            NULL,
            NULL,
            &numSubKeys,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL
        );

        if (errorCode == ERROR_SUCCESS)
        {
            DWORD nameLen;
            TCHAR keyNameBuffer[MAX_KEY_LENGTH];

            for (DWORD i = 0; i < numSubKeys; i++)
            {
                nameLen = MAX_KEY_LENGTH;

                if (RegEnumKeyEx
                (
                    hKey, i,
                    keyNameBuffer,
                    &nameLen,
                    NULL,
                    NULL,
                    NULL,
                    NULL
                    ) == ERROR_SUCCESS)
                {
                    errorCode = QueryUSBDeviceKeys2
                    (
                        keyNameBuffer,
                        deviceFriendlyName,
                        keyNameBuffer,
                        MAX_KEY_LENGTH
                    );

                    if (errorCode == ERROR_SUCCESS)
                    {
                        // QC Device found, query the corresponding swkey
                        TCHAR InstanceKey[MAX_KEY_LENGTH];
                        HKEY swKey;

                        sprintf_s(InstanceKey, sizeof(InstanceKey), "%s\\%s", QCNET_REG_SW_KEY, keyNameBuffer);
                        errorCode = RegOpenKeyEx
                        (
                            HKEY_LOCAL_MACHINE,
                            InstanceKey,
                            0,
                            KEY_READ,
                            &swKey
                        );

                        DWORD valueLen = sizeof(DWORD);
                        if (errorCode == ERROR_SUCCESS)
                        {
                            errorCode = RegQueryValueEx
                            (
                                swKey,
                                entryName,
                                NULL,
                                NULL,
                                (LPBYTE)entryValue,
                                &valueLen
                            );

                            return errorCode;
                        }
                        else
                        {
                            printfd("Error: couldn't open registry\n");
                            return errorCode;
                        }
                    }
                }
            }
            errorCode = ERROR_FILE_NOT_FOUND;
        }
        RegCloseKey(hKey);
    }
    return errorCode;
}

ULONG QueryUSBDeviceKeys2(PCHAR rootKey, PCHAR deviceFriendlyName, PCHAR swKeyBuffer, SIZE_T swKeyBufferSize)
{
    // return ERROR_SUCCESS if a name-matched QC device hwKey is found, store the driver instance into buffer
    // otherwise a System Error Code will be returned, buffer is untouched

    TCHAR fullKeyName[MAX_KEY_LENGTH];
    HKEY subKey;

    sprintf_s(fullKeyName, sizeof(fullKeyName), "%s\\%s", QCNET_REG_HW_KEY, rootKey);

    ULONG ret = RegOpenKeyEx
    (
        HKEY_LOCAL_MACHINE,
        fullKeyName,
        0,
        KEY_READ,
        &subKey
    );

    DWORD numSubKeys;
    if (ret == ERROR_SUCCESS)
    {
        ret = RegQueryInfoKey
        (
            subKey,
            NULL,
            NULL,
            NULL,
            &numSubKeys,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL
        );

        if (ret == ERROR_SUCCESS)
        {
            DWORD nameLen;
            TCHAR subKeyName[MAX_KEY_LENGTH];

            for (DWORD i = 0; i < numSubKeys; i++)
            {
                nameLen = MAX_KEY_LENGTH;
                ret = RegEnumKeyEx
                (
                    subKey, i,
                    subKeyName,
                    &nameLen,
                    NULL,
                    NULL,
                    NULL,
                    NULL
                );

                if (ret == ERROR_SUCCESS)
                {
                    HKEY qcKey;
                    ret = RegOpenKeyEx
                    (
                        subKey,
                        subKeyName,
                        0,
                        KEY_READ,
                        &qcKey
                    );

                    if (ret == ERROR_SUCCESS)
                    {
                        DWORD valueBufferSize = strlen(deviceFriendlyName) + 1;
                        PTCHAR valueBuffer = (PTCHAR)malloc(valueBufferSize);        // buffer for qc device friendly name

                        if (valueBuffer == NULL)
                        {
                            RegCloseKey(subKey);
                            RegCloseKey(qcKey);
                            return ERROR_NOT_ENOUGH_MEMORY;
                        }

                        ret = RegQueryValueEx(qcKey, "FriendlyName", NULL, NULL, (LPBYTE)valueBuffer, &valueBufferSize);

                        if (ret == ERROR_SUCCESS)
                        {
                            if (strcmp(valueBuffer, deviceFriendlyName) == 0)       // device found, retrive and copy the swkey
                            {
                                RegCloseKey(subKey);
                                DWORD swKeyBufLen = (DWORD)swKeyBufferSize;
                                ret = RegQueryValueEx(qcKey, "Driver", NULL, NULL, (LPBYTE)swKeyBuffer, &swKeyBufLen);
                                RegCloseKey(qcKey);
                                free(valueBuffer);
                                return ret;
                            }
                        }
                        free(valueBuffer);
                        RegCloseKey(qcKey);
                    }
                }
            }
            ret = ERROR_FILE_NOT_FOUND;
        }
        RegCloseKey(subKey);
    }

    return ret;
}
