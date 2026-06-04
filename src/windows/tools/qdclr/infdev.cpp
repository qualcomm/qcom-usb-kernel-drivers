/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                           I N F D E V. C P P

GENERAL DESCRIPTION
    Scan and remove VID_05C6 related devnodes and inf files from system

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "infdev.h"

// Global Variables
PUCHAR gFileDataRaw;
EXEC_MODE gExecutionMode;

void print_timestamp(PCHAR text, bool NewLine)
{
    SYSTEMTIME lt;
    GetLocalTime(&lt);
    printf("[%02d:%02d:%02d.%03d] %s\n",
           lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds, text);
    if (NewLine == TRUE)
    {
        printf("\n");
    }
}

int remove_drivers(EXEC_MODE mode)
{
    print_timestamp((PCHAR)"=== Start Scanning Driver Store ===", false);
    if (mode == EXEC_MODE::PREVIEW)
    {
        printf("   Mode: Preview\n\n");
    }
    else
    {
        printf("   Mode: Removal\n\n");
    }
    gExecutionMode = mode;
    gFileDataRaw = (PUCHAR)malloc(INF_SIZE_MAX);
    if (gFileDataRaw == NULL)
    {
        printf("   ERROR: no memory for gFileDataRaw\n");
        return EXIT_FAILURE;
    }

    // 1. remove devnode
    ScanAndRemoveDevice(TEXT(MATCH_VID));

    // 2. remove INF
    ScanAndRemoveInf(INF_INSTALL_PATH);

    free(gFileDataRaw);

    DeleteFile(TEXT("C:\\Windows\\system32\\drivers\\qcusbfilter.sys"));
    DeleteFile(TEXT("C:\\Windows\\system32\\drivers\\qcusbwwan.sys"));
    DeleteFile(TEXT("C:\\Windows\\system32\\drivers\\qcusbnet.sys"));
    DeleteFile(TEXT("C:\\Windows\\system32\\drivers\\qcusbser.sys"));
    DeleteFile(TEXT("C:\\Windows\\system32\\drivers\\qdbusb.sys"));
    DeleteFile(TEXT("C:\\Windows\\system32\\drivers\\qcwdfserial.sys"));

    // Trigger re-enumeration to refresh device tree
    DEVINST devRoot;
    if (CM_Locate_DevNode(&devRoot, NULL, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
    {
        CM_Reenumerate_DevNode(devRoot, CM_REENUMERATE_SYNCHRONOUS);
        printf("\n   Scanning for hardware changes...\n");
    }

    print_timestamp((PCHAR)"=== End of Scanning ===", true);
    return EXIT_SUCCESS;
}

VOID ScanAndRemoveInf(LPCTSTR InfPath)
{
    WIN32_FIND_DATA fileData;
    HANDLE          fileHandle;
    BOOL            notDone = TRUE;
    WCHAR           fullPath[MAX_PATH];
    WCHAR           searchPath[MAX_PATH];
    LARGE_INTEGER   infSize;
    ULONG           dataSize;

    StringCchCopy(searchPath, MAX_PATH, InfPath);
    StringCchCat(searchPath, MAX_PATH, INT_OEM_NAMING);

    printf("   INF Scan Path: <%ws>\n", InfPath);

    fileHandle = FindFirstFile(searchPath, &fileData);
    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        printf("FindFirstFile failure\n");
        return;
    }

    while (notDone == TRUE)
    {
        if ((fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            // got a file
            StringCchCopy(fullPath, MAX_PATH, InfPath);
            StringCchCat(fullPath, MAX_PATH, TEXT("\\"));
            infSize.LowPart = fileData.nFileSizeLow;
            infSize.HighPart = fileData.nFileSizeHigh;
            StringCchCat(fullPath, MAX_PATH, fileData.cFileName);
            if (infSize.QuadPart > (INF_SIZE_MAX - 2))
            {
                MatchingInf(fullPath, fileData.cFileName, (INF_SIZE_MAX - 2));
            }
            else
            {
                dataSize = (ULONG)infSize.QuadPart;
                MatchingInf(fullPath, fileData.cFileName, dataSize);
            }
        }
        notDone = FindNextFile(fileHandle, &fileData);
    }

    FindClose(fileHandle);
}

BOOL MatchingInf(PCTSTR InfFullPath, PCTSTR InfFileName, ULONG DataSize)
{
    HINF myHandle;
    BOOL bResult = TRUE;
    BOOL matchFound = FALSE;
    DWORD bytesRead;
    BOOL notAscii = TRUE;
    PCHAR matchType = (PCHAR)"A";

    myHandle = CreateFile
    (
        InfFullPath,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (myHandle == INVALID_HANDLE_VALUE)
    {
        printf("   Error: SetupOpenInfFile <%ws> 0x%X\n", InfFullPath, GetLastError());
        return FALSE;
    }

    ZeroMemory(gFileDataRaw, INF_SIZE_MAX);

    bResult = ReadFile
    (
        myHandle,
        (PVOID)gFileDataRaw,
        DataSize,
        &bytesRead,
        NULL
    );
    if (bResult == FALSE)
    {
        printf("   Error: Failed to read <%ws>\n", InfFullPath);
        CloseHandle(myHandle);
        return FALSE;
    }

    gFileDataRaw[bytesRead] = 0;
    gFileDataRaw[bytesRead + 1] = 0;

    // simple detection of encoding format
    if (gFileDataRaw[0] == 0xEF || gFileDataRaw[0] == 0xFE || gFileDataRaw[0] == 0xFF ||
        gFileDataRaw[0] == 0x00)
    {
        notAscii = TRUE;
    }
    if (gFileDataRaw[0] < 0x80 && gFileDataRaw[1] < 0x80)
    {
        notAscii = FALSE;
    }

    // Filter based on VID/PID to avoid removal of commercial GOBI devices
    // For more aggressive removal based on VID only, comment out the PID lines
    if (notAscii == FALSE)
    {
        // traditional ASCII file
        if (strstr((char *)gFileDataRaw, (char *)MATCH_VID))
        {
            if ((strstr((char *)gFileDataRaw, (char *)MATCH_PID) != NULL) ||
                (strstr((char *)gFileDataRaw, (char *)MATCH_PID2) != NULL) ||
                (strstr((char *)gFileDataRaw, (char *)MATCH_PID3) != NULL))
            {
                matchType = (PCHAR)"A";
                matchFound = TRUE;
            }
            else
            {
                printf("(A) Candidate but not to be removed: <%ws>\n", InfFullPath);
            }
        }

    }
    else
    {
        if (StrStrW((PTSTR)gFileDataRaw, TEXT(MATCH_VID)) != NULL)
        {
            if ((StrStrW((PTSTR)gFileDataRaw, TEXT(MATCH_PID)) != NULL) ||
                (StrStrW((PTSTR)gFileDataRaw, TEXT(MATCH_PID2)) != NULL) ||
                (StrStrW((PTSTR)gFileDataRaw, TEXT(MATCH_PID3)) != NULL))
            {
                matchType = (PCHAR)"W";
                matchFound = TRUE;
            }
            else
            {
                printf("(W) Candidate but not to be removed: <%ws>\n", InfFullPath);
            }
        }
    }

    CloseHandle(myHandle);

    // Confirm INF candidate
    if (matchFound == TRUE)
    {
        matchFound = ConfirmInfFile(InfFullPath);
    }

    if (matchFound == TRUE)
    {
        RemoveInfFile(InfFileName, matchType);
    }

    return matchFound;
}

BOOL ConfirmInfFile(PCTSTR InfFullPath)
{
    UINT errLine = 0;
    HINF myHandle;
    INFCONTEXT infCtxt;
    BOOL bResult = TRUE;
    WCHAR lineText[LINE_LEN_MAX];
    DWORD requiredSize = 0;
    BOOL matchConfirmed = FALSE;

    myHandle = SetupOpenInfFile(InfFullPath, NULL, INF_STYLE_WIN4, &errLine);
    if (myHandle == INVALID_HANDLE_VALUE)
    {
        printf("   Error: SetupOpenInfFile <%ws> 0x%X\n", InfFullPath, GetLastError());
        return FALSE;
    }

    bResult = SetupFindFirstLine(myHandle, TEXT("Version"), TEXT("Class"), &infCtxt);
    if (bResult == FALSE)
    {
        printf("   Error: SetFindFirstLine <%ws> 0x%X\n", InfFullPath, GetLastError());
        SetupCloseInfFile(myHandle);
        return FALSE;
    }

    bResult = SetupGetLineText(&infCtxt, NULL, NULL, NULL, lineText, LINE_LEN_MAX, &requiredSize);
    if (bResult == TRUE)
    {
        if (DeviceMatch(lineText, requiredSize) == TRUE)
        {
            matchConfirmed = TRUE;
        }
    }
    else
    {
        printf("   Error: SetupGetLineText <%ws> 0x%X\n", InfFullPath, GetLastError());
    }

    SetupCloseInfFile(myHandle);

    return matchConfirmed;
}

BOOL DeviceMatch(PTSTR InfText, DWORD TextSize)
{
    DWORD actualSize = sizeof(WCHAR) * TextSize;
    BOOL matchFound = FALSE;

    if (actualSize > LINE_LEN_MAX)
    {
        printf("DeviceMatch: line too long (%uB/%uB)\n", TextSize, LINE_LEN_MAX);
        return FALSE;
    }

    if (StrStrW(InfText, TEXT("Modem")) != NULL)      // MODEM
    {
        matchFound = TRUE;
    }
    else if (StrStrW(InfText, TEXT("Net")) != NULL)   // NET ADAPTR
    {
        matchFound = TRUE;
    }
    else if (StrStrW(InfText, TEXT("Ports")) != NULL) // SER PORT
    {
        matchFound = TRUE;
    }
    else if (StrStrW(InfText, TEXT("USB")) != NULL)   // QDSS, DPL, FILTER
    {
        matchFound = TRUE;
    }
    else if (StrStrW(InfText, TEXT("AndroidUsbDeviceClass")) != NULL)   // QCADB
    {
        matchFound = TRUE;
    }
    else if (StrStrW(InfText, TEXT("libusb-win32 devices")) != NULL)    // userspace (WinUSB)
    {
        matchFound = TRUE;
    }

    return matchFound;
}

VOID RemoveInfFile(PCTSTR InfFileName, PCHAR Type)
{
    WCHAR cmdLineW[MAX_PATH];
    PROCESS_INFORMATION processInfo;
    STARTUPINFO startupInfo;
    BOOL result;
    DWORD exitCode;

    StringCchCopy(cmdLineW, MAX_PATH, INF_REMOVE_COMMAND);
    StringCchCat(cmdLineW, MAX_PATH, InfFileName);

    if (gExecutionMode == EXEC_MODE::PREVIEW)
    {
        printf("(%s) %ws\n", Type, cmdLineW);
    }
    else
    {
        printf("\nDeleting %ws ...\n", InfFileName);

        memset(&processInfo, 0, sizeof(processInfo));
        memset(&startupInfo, 0, sizeof(startupInfo));
        startupInfo.cb = sizeof(startupInfo);

        result = CreateProcess
        (
            NULL,
            cmdLineW,
            NULL,
            NULL,
            FALSE,
            NORMAL_PRIORITY_CLASS,
            NULL,
            NULL,
            &startupInfo,
            &processInfo
        );

        if (result == FALSE)
        {
            printf("   Error: CreateProcess failure 0x%X\n", GetLastError());
        }

        WaitForSingleObject(processInfo.hProcess, INFINITE);
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
    }
}

// ================ Removal of DevNode ================
VOID ScanAndRemoveDevice(LPCTSTR HwId)
{
    HDEVINFO        devInfoHandle = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA devInfoData;
    DWORD           memberIdx = 0;
    CHAR            hardwareIds[REG_HW_ID_SIZE];
    CHAR            compatibleIds[REG_HW_ID_SIZE];
    CHAR            friendlyName[REG_HW_ID_SIZE];
    DWORD           requiredSize;
    BOOL            bResult;
    BOOL            bMatch, bExclude;
    DWORD           errorCode;

    devInfoHandle = SetupDiGetClassDevsEx
    (
        NULL,
        NULL,  // TEXT("USB")
        NULL,
        DIGCF_ALLCLASSES,
        NULL,
        NULL,  // Machine,
        NULL
    );
    if (devInfoHandle == INVALID_HANDLE_VALUE)
    {
        return;
    }

    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    while (SetupDiEnumDeviceInfo(devInfoHandle, memberIdx, &devInfoData) == TRUE)
    {
        bMatch = bExclude = FALSE;
        ZeroMemory(hardwareIds, REG_HW_ID_SIZE);
        ZeroMemory(compatibleIds, REG_HW_ID_SIZE);
        ZeroMemory(friendlyName, REG_HW_ID_SIZE);

        bResult = SetupDiGetDeviceRegistryProperty
        (
            devInfoHandle,
            &devInfoData,
            SPDRP_FRIENDLYNAME,
            NULL,
            (LPBYTE)friendlyName,
            REG_HW_ID_SIZE,
            &requiredSize
        );
        if (bResult == FALSE)
        {
            SetupDiGetDeviceRegistryProperty
            (
                devInfoHandle,
                &devInfoData,
                SPDRP_DEVICEDESC,
                NULL,
                (LPBYTE)friendlyName,
                REG_HW_ID_SIZE,
                &requiredSize
            );
        }

        bResult = SetupDiGetDeviceRegistryProperty
        (
            devInfoHandle,
            &devInfoData,
            SPDRP_HARDWAREID,
            NULL,
            (LPBYTE)hardwareIds,
            REG_HW_ID_SIZE,
            &requiredSize
        );
        if (bResult == FALSE)
        {
            errorCode = GetLastError();
            if (errorCode != ERROR_INVALID_DATA)
            {
                printf("   Error: SetupDiGetDeviceRegistryProperty: hwid (0x%X) reqSZ %d\n",
                       GetLastError(), requiredSize);
            }
        }
        else
        {
            CharUpper((PTSTR)hardwareIds);

            // matching
            if (StrStrW((PTSTR)hardwareIds, HwId) != NULL)
            {
                printf("HWID <%ws>\n", (PTSTR)hardwareIds);
                bMatch = TRUE;
            }

            // exclusion
            if (StrStrW((PTSTR)hardwareIds, TEXT(EXCLUDE_PID)) != NULL)
            {
                bExclude = TRUE;
            }
            else if (StrStrW((PTSTR)hardwareIds, TEXT(EXCLUDE_PID2)) != NULL)
            {
                bExclude = TRUE;
            }
            else if (StrStrW((PTSTR)hardwareIds, TEXT(EXCLUDE_PID3)) != NULL)
            {
                bExclude = TRUE;
            }
            else if (StrStrW((PTSTR)hardwareIds, TEXT(EXCLUDE_PID4)) != NULL)
            {
                bExclude = TRUE;
            }
            else if (StrStrW((PTSTR)hardwareIds, TEXT(EXCLUDE_PID5)) != NULL)
            {
                bExclude = TRUE;
            }
        }

        if (bMatch == FALSE)
        {
            bResult = SetupDiGetDeviceRegistryProperty
            (
                devInfoHandle,
                &devInfoData,
                SPDRP_COMPATIBLEIDS,
                NULL,
                (LPBYTE)compatibleIds,
                REG_HW_ID_SIZE,
                &requiredSize
            );
            if (bResult == FALSE)
            {
                errorCode = GetLastError();
                if (errorCode != ERROR_INVALID_DATA)
                {
                    printf("   Error: SetupDiGetDeviceRegistryProperty: cpid (0x%X) reqSZ %d\n",
                           GetLastError(), requiredSize);
                }
            }
            else
            {
                CharUpper((PTSTR)compatibleIds);

                // matching
                if (StrStrW((PTSTR)compatibleIds, HwId) != NULL)
                {
                    printf("COMPAT ID <%ws>\n", (PTSTR)hardwareIds);
                    bMatch = TRUE;
                }
            }
        }

        if (bMatch == TRUE)
        {
            printf("     <%ws>\n", (PTSTR)friendlyName);
            if (gExecutionMode == EXEC_MODE::PREVIEW)
            {
                if (bExclude == FALSE)
                {
                    printf("     ^ to be removed ^\n");
                }
                else
                {
                    printf("     ^ candidate but not to be removed ^\n");
                }
            }
            else
            {
                if (bExclude == FALSE)
                {
                    RemoveDevice(devInfoHandle, &devInfoData);
                }
                else
                {
                    printf("     ^ candidate but not to be removed ^\n");
                }
            }
        }
        memberIdx++;
    }

    if (devInfoHandle != INVALID_HANDLE_VALUE)
    {
        SetupDiDestroyDeviceInfoList(devInfoHandle);
    }
}

BOOL RemoveDevice(HDEVINFO DevInfoHandle, PSP_DEVINFO_DATA DevInfoData)
{
    SP_REMOVEDEVICE_PARAMS removeDevParams;
    SP_DEVINSTALL_PARAMS devInstallParams;
    BOOL bResult = FALSE;

    removeDevParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    removeDevParams.Scope = DI_REMOVEDEVICE_GLOBAL;
    removeDevParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
    removeDevParams.HwProfile = 0;

    bResult = SetupDiSetClassInstallParams
    (
        DevInfoHandle,
        DevInfoData,
        &removeDevParams.ClassInstallHeader,
        sizeof(SP_REMOVEDEVICE_PARAMS)
    );
    if (bResult == TRUE)
    {
        bResult = SetupDiCallClassInstaller(DIF_REMOVE, DevInfoHandle, DevInfoData);
        if (bResult == FALSE)
        {
            DWORD err = GetLastError();
            if (err == ERROR_IN_WOW64)
            {
                printf("   Error: executable might not work on 64-bit OS, please run 64-bit executable\n");
            }
            else
            {
                printf("   Error: SetupDiCallClassInstaller failure 0x%X\n", err);
            }
        }
    }
    else
    {
        printf("   Error: SetupDiSetClassInstallParams failure\n");
    }

    if (bResult == FALSE)
    {
        printf("   Error: failed to remove device\n");
    }
    else
    {
        devInstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS);
        bResult = SetupDiGetDeviceInstallParams
        (
            DevInfoHandle,
            DevInfoData,
            &devInstallParams
        );
        if (bResult == TRUE)
        {
            if (devInstallParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT))
            {
                bResult = TRUE;
                printf("   Device instance removed successfully, please reboot system\n");
            }
            else
            {
                bResult = TRUE;
                printf("   Device instance removed successfully, no reboot needed\n");
            }
        }
    }
    return bResult;
}
