/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          R E G . C

GENERAL DESCRIPTION
    This file implements Windows registry query and device enumeration
    helper functions for the Qualcomm MTU configuration service.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "reg.h"

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
    DWORD    numSubKeys = 0;
    DWORD    subKeyMaxLen;
    DWORD    classMaxLen;
    DWORD    numKeyValues;
    DWORD    valueMaxLen;
    DWORD    valueDataMaxLen;
    DWORD    securityDescLen;
    FILETIME lastWriteTime;
    char     fullKeyName[MAX_KEY_LENGTH];

    DWORD i, retCode;

    // retrieve class name
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

    // go through subkeys
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
                sprintf(fullKeyName, "%s\\%s", FullKeyName, TEXT(subKey));
                result = FindDeviceInstance(fullKeyName, DeviceFriendlyName, ControlFileName);
                if (result == TRUE)
                {
                    // printf("QueryKey: TRUE\n");
                    return TRUE;
                }
            }
        }
    }

    // printf("QueryKey: FALSE\n");

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
    PCHAR EntryName,         // "QCDeviceControlFile"
    PCHAR ControlFileName
)
{
    DWORD retCode = ERROR_SUCCESS;
    TCHAR valueName[MAX_VALUE_NAME];
    TCHAR swKey[MAX_VALUE_NAME];
    DWORD valueNameLen = MAX_VALUE_NAME;
    BOOL  instanceFound = FALSE;
    HKEY  hSwKey;

    valueName[0] = 0;

    // first get device friendly name
    retCode = RegQueryValueEx
    (
        hKey,
        "FriendlyName", // "DriverDesc", // value name
        NULL,         // reserved
        NULL,         // returned type
        valueName,
        &valueNameLen
    );

    if (retCode == ERROR_SUCCESS)
    {
        valueName[valueNameLen] = 0;

        if (strcmp(valueName, DeviceFriendlyName) == 0)
        {
            instanceFound = TRUE;
        }
        // printf("FriendlyName <%s>\n", valueName);
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
            valueName,
            &valueNameLen
        );
        if (retCode == ERROR_SUCCESS)
        {
            valueName[valueNameLen] = 0;

            if (strcmp(valueName, DeviceFriendlyName) == 0)
            {
                instanceFound = TRUE;
            }
            // printf("DeviceDesc:reg-%d <%s>\n", instanceFound, valueName);
            // printf("DeviceDesc:frn-%d <%s>\n", instanceFound, DeviceFriendlyName);
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
            valueName,
            &valueNameLen
        );
        if (retCode == ERROR_SUCCESS)
        {
            // Construct device software key
            valueName[valueNameLen] = 0;
            sprintf(swKey, "%s\\%s", QCNET_REG_SW_KEY, TEXT(valueName));

            // printf("Got SW Key <%s>\n", swKey);

            // Open device software key
            retCode = RegOpenKeyEx
            (
                HKEY_LOCAL_MACHINE,
                swKey,
                0,
                KEY_READ,
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
                    valueName,
                    &valueNameLen
                );

                if (retCode == ERROR_SUCCESS)
                {
                    PCHAR p = (PCHAR)valueName + valueNameLen;

                    valueName[valueNameLen] = 0;

                    while ((p > valueName) && (*p != '\\'))
                    {
                        p--;
                    }
                    if (*p == '\\') p++;
                    strcpy(ControlFileName, p);
                    // _tprintf(TEXT("QCDeviceControlFile: %s\n"), valueName);
                    return TRUE;
                }

                RegCloseKey(hSwKey);
            }  // if (retCode == ERROR_SUCCESS)
        }  // if (retCode == ERROR_SUCCESS)
        else
        {
            printf("Error: cannot get device software key, retCode %u\n", retCode);
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
        DWORD    numSubKeys = 0;
        DWORD    subKeyMaxLen;
        DWORD    classMaxLen;
        DWORD    numKeyValues;
        DWORD    valueMaxLen;
        DWORD    valueDataMaxLen;
        DWORD    securityDescLen;
        FILETIME lastWriteTime;
        DWORD    i, retCode;

        // retrieve class name
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

        // go through subkeys
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

                    // _tprintf(TEXT("A-(%d) %s\n"), i+1, subKey);
                    result = QueryUSBDeviceKeys(subKey, DeviceFriendlyName, ControlFileName);
                    if (result == TRUE)
                    {
                        // printf("QueryKey: TRUE\n");
                        return TRUE;
                    }
                }
            }
        }

        RegCloseKey(hTestKey);

        return retCode;
    }

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

    sprintf(fullKeyName, "%s\\%s", QCNET_REG_HW_KEY, TEXT(InstanceKey));

    // printf("B-full key name: [%s]\n", fullKeyName);

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

    return FALSE;
}  // QueryUSBDeviceKeys
