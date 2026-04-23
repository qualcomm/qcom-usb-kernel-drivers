/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                       D E V M O N . C

GENERAL DESCRIPTION
    This file implements device monitoring, enumeration, and NIC
    management for the Qualcomm MTU configuration service.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include <windows.h>
#include <objbase.h>
#include <setupapi.h>
#include <dbt.h>
#include "api.h"
#include "main.h"
#include "utils.h"
#include "reg.h"
#include "devmon.h"

#define DEVMN_MON_REG_KEY "HARDWARE\\DEVICEMAP\\SERIALCOMM"
#define DEVMON_NET_CONNECTION_REG_KEY "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}"

HANDLE RegMonChangeEvent;
HANDLE RegMonExitEvent;
DEVMN_NIC_RECORD MtuNicRecord[DEVMN_NUM_NIC];  // this is temporary
DEVMN_NIC_RECORD MtuNicActiveRecord[DEVMN_NUM_NIC];
PDEVMN_NIC_RECORD NicRecHead, NicRecTail;
static int LastNicIdx;

#define DEVMN_DEVMON_UNIT_TEST

typedef struct _DIDLG_DATA
{
    HDEVINFO   DeviceInfoSet;
    HDEVNOTIFY hDevNotify;
    GUID       InterfaceClassGuid;
} DIDLG_DATA, *PDIDLG_DATA;

typedef struct _QCMTU_MONITOR_ST
{
    HANDLE StartupEvent;
    DWORD  StartupWaitTime;
    UCHAR  State;
} QCMTU_MONITOR_ST, *PQCMTU_MONITOR_ST;

VOID DisplayNicList(VOID)
{
    int i;

    if (AppDebugMode == FALSE)
    {
        return;
    }

    QCMTU_Print("-->DisplayNicList\n");

    for (i = 0; i < DEVMN_NUM_NIC; ++i)
    {
        if ((MtuNicRecord[i].FriendlyName[0] != 0) && (FALSE == IsInActiveList(MtuNicRecord[i].FriendlyName)))
        {
            QCMTU_Print("NIC[%u] FN: <%s>\n", i, MtuNicRecord[i].FriendlyName);
            QCMTU_Print("NIC[%u] CN: <%s>\n", i, MtuNicRecord[i].ControlFileName);
            QCMTU_Print("NIC[%u] ID: <%s>\n", i, MtuNicRecord[i].InterfaceId);
            QCMTU_Print("NIC[%u] IF: <%s>\n", i, MtuNicRecord[i].InterfaceName);

            // test code
            if (FALSE)
            {
                QC_MTU mtu;

                mtu.IpVer = 4;
                mtu.MTU = 1400;

                QCMTU_SetMTU(&mtu, MtuNicRecord[i].InterfaceName);
            }
        }
        else
        {
            // break;
        }
    }

    QCMTU_Print("<--DisplayNicList\n");
}

VOID DisplayActiveList(VOID)
{
    PDEVMN_NIC_RECORD item;

    if (AppDebugMode == FALSE)
    {
        return;
    }
    QCMTU_RequestLock();

    item = NicRecHead;
    while (item != NULL)
    {
        QCMTU_Print("ActiveNIC[%u] FN: <%s>\n", item->Index, item->FriendlyName);
        item = item->Next;
    }
    if (NicRecTail != NULL)
    {
        QCMTU_Print("ActiveNIC-Tail[%u] FN: <%s>\n", NicRecTail->Index, NicRecTail->FriendlyName);
    }

    QCMTU_ReleaseLock();

}  // DisplayActiveList

VOID InitializeNicRecords(VOID)
{
    ZeroMemory(MtuNicRecord, sizeof(DEVMN_NIC_RECORD) * DEVMN_NUM_NIC);
    ZeroMemory(MtuNicActiveRecord, sizeof(DEVMN_NIC_RECORD) * DEVMN_NUM_NIC);
}  // InitializeNicRecords

VOID DEVMN_DeviceMonitor(BOOL AppMode)
{
    HANDLE hDeviceMonitorHandle;
    DWORD tid;
    DEV_BROADCAST_DEVICEINTERFACE notificationFilter;
    QCMTU_MONITOR_ST startupState;

    QCMTU_Print("Start MTU device monitor thread\n");

    InitializeNicRecords();
    startupState.StartupEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    startupState.StartupWaitTime = 10000; // 10s
    startupState.State = 0;
    if (startupState.StartupEvent == NULL)
    {
        QCMTU_Print("Unable to create startup event: %d\n", GetLastError());
        return;
    }

    hDeviceMonitorHandle = CreateThread
    (
        NULL,
        0,
        DEVMN_DevMonThread,
        (LPVOID)&startupState,
        0,
        &tid
    );
    if (hDeviceMonitorHandle == INVALID_HANDLE_VALUE)
    {
        QCMTU_Print("Unable to launch device monitor thread, error %d\n", GetLastError());
        printf("Error: Unable to launch, possibly another instance already running\n");
        return;
    }

    WaitForSingleObject(startupState.StartupEvent, startupState.StartupWaitTime);
    QCMTU_Print("Startup event signaled: %d\n", startupState.State);
    if (startupState.State != 0)
    {
        QCMTU_Print("Unable to launch device monitor thread\n");
        CloseHandle(startupState.StartupEvent);
        return;
    }
    CloseHandle(startupState.StartupEvent);

    if (AppMode == TRUE)
    {
        QCMTU_Print("App mode, create window\n");

        AppModuleHandle = GetModuleHandle(NULL);
        if (AppModuleHandle == NULL)
        {
            QCMTU_Print("App mode, GetModuleHandle failure %d\n", GetLastError());
        }
        AppWndClass.cbSize = sizeof(WNDCLASSEX);
        AppWndClass.style = CS_HREDRAW | CS_VREDRAW;
        AppWndClass.lpfnWndProc = (WNDPROC)MyWindowProc;
        AppWndClass.cbClsExtra = 0;
        AppWndClass.cbWndExtra = 0;
        AppWndClass.hInstance = AppModuleHandle;
        AppWndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION); // NULL;
        AppWndClass.hCursor = LoadCursor(NULL, IDC_ARROW); // NULL;
        AppWndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH); // NULL;
        AppWndClass.lpszMenuName = TEXT("GenericMenu"); // NULL;
        AppWndClass.lpszClassName = AppClassName;
        AppWndClass.hIconSm = NULL;

        if (RegisterClassEx(&AppWndClass) == 0)
        {
            QCMTU_Print("App mode, RegisterClass failure %d\n", GetLastError());
        }

        AppWinHandle = CreateWindowEx
        (
            WS_EX_NOACTIVATE,
            AppClassName,
            AppTitle,
            WS_MINIMIZE, // WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            NULL,
            NULL,
            AppModuleHandle,
            NULL
        );
        if (AppWinHandle == NULL)
        {
            QCMTU_Print("App mode, CreateWindow failure %d\n", GetLastError());
        }
        else
        {
            // ShowWindow(AppWinHandle, SW_HIDE);
        }
    }
    else
    {
        // Register device notification
        MyDeviceGuid = GUID_QCMTU_NIC;
        ZeroMemory(&notificationFilter, sizeof(notificationFilter));
        notificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
        notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        // notificationFilter.dbcc_classguid = MyDeviceGuid;
        memcpy(&(NotificationFilter.dbcc_classguid), &(GUID_QCMTU_NIC), sizeof(struct _GUID));

        DevNotificationHandle = RegisterDeviceNotification
        (
            ServiceStateHandle,
            &notificationFilter,
            DEVICE_NOTIFY_SERVICE_HANDLE
        );
        if (DevNotificationHandle == NULL)
        {
            QCMTU_Print("Unable to register device notification, error %d\n", GetLastError());
            SetEvent(RegMonExitEvent);
            goto WaitExit;
        }
    }

    // we terminate the service from console with ESC in application mode
    if (AppMode == TRUE)
    {
        int keyPressed = 0;

        while (keyPressed != KEY_PRESSED_ESC)
        {
            keyPressed = _getch();
        }
        SetEvent(RegMonExitEvent);
    }

WaitExit:

    WaitForSingleObject(hDeviceMonitorHandle, INFINITE);
    UnregisterDeviceNotification(hDeviceMonitorHandle);
    CloseHandle(hDeviceMonitorHandle);
    QCMTU_Print("Device monitor thread terminated\n");

    return;
}  // DEVMN_DeviceMonitor

DWORD WINAPI DEVMN_DevMonThread(PVOID Context)
{
    PQCMTU_MONITOR_ST startupSt;
    HKEY hTestKey;
    BOOL ret = FALSE, notDone = TRUE;
    HANDLE waitHandle[2 * DEVMN_NUM_NIC];
    int    waitCount = DEVMON_EVENT_COUNT;
    BOOL firstRun = TRUE, bRegChanged = FALSE;

    startupSt = (PQCMTU_MONITOR_ST)Context;
    NicRecHead = NicRecTail = NULL;
    RegMonChangeEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // signaled to scan reg
    if (INVALID_HANDLE_VALUE == RegMonChangeEvent)
    {
        QCMTU_Print("Error: cannot create reg mon event\n");
        startupSt->State = 1;
        SetEvent(startupSt->StartupEvent);
        return 1;
    }
    RegMonExitEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (INVALID_HANDLE_VALUE == RegMonExitEvent)
    {
        QCMTU_Print("Error: cannot create reg exit event\n");
        CloseHandle(RegMonChangeEvent);
        startupSt->State = 1;
        SetEvent(startupSt->StartupEvent);
        return 1;
    }
    waitHandle[DEVMON_EVENT_REG_CHANGE] = RegMonChangeEvent;
    waitHandle[DEVMON_EVENT_EXIT] = RegMonExitEvent;

    if (RegOpenKeyEx
    (
        HKEY_LOCAL_MACHINE,
        DEVMN_MON_REG_KEY,
        0,
        KEY_READ,
        &hTestKey
        ) != ERROR_SUCCESS
        )
    {

        QCMTU_Print("Error: cannot open reg key for monitoring\n");
        CloseHandle(RegMonChangeEvent);
        CloseHandle(RegMonExitEvent);
        startupSt->State = 1;
        SetEvent(startupSt->StartupEvent);

        return ret;
    }

    if (QCMTU_InitializeLock() == NULL)
    {
        QCMTU_Print("Error: cannot initialize lock\n");
        CloseHandle(RegMonChangeEvent);
        CloseHandle(RegMonExitEvent);
        CloseHandle(hTestKey);
        startupSt->State = 1;
        SetEvent(startupSt->StartupEvent);
        return 1;
    }

    if (QCMTU_RunSingleInstance() == FALSE)
    {
        startupSt->State = 1;
        SetEvent(startupSt->StartupEvent);
        return 1;
    }

    startupSt->State = 0;
    SetEvent(startupSt->StartupEvent);

    ZeroMemory(MtuNicActiveRecord, sizeof(DEVMN_NIC_RECORD) * DEVMN_NUM_NIC);

    while (notDone == TRUE)
    {
        DWORD waitIdx, waitInterval = DEVMON_SCAN_INTERVAL;

        RegNotifyChangeKeyValue
        (
            (HKEY)hTestKey,
            TRUE,
            REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
            RegMonChangeEvent,
            TRUE
        );

        if (firstRun == TRUE)
        {
            // need to scan the reg the first time we run the service
            SetEvent(RegMonChangeEvent);
            firstRun = FALSE;
        }

        // If all handles are not closed, wait
        waitIdx = WaitForMultipleObjects
        (
            waitCount,
            waitHandle,
            FALSE,
            waitInterval  // INFINITE
        );
        waitIdx -= WAIT_OBJECT_0;

        switch (waitIdx)
        {
            case DEVMON_EVENT_EXIT:
            {
                // terminate all MtuThreads
                QCMTU_Print("DevMon_THREAD_EXIT\n");
                ResetEvent(waitHandle[waitIdx]);
                notDone = FALSE;
                TerminateAllMtuThreads();
                break;
            }
            case DEVMON_EVENT_REG_CHANGE:
            {
                QCMTU_Print("DevMon_THREAD_REG_CHANGE\n");

                ResetEvent(waitHandle[waitIdx]);
                bRegChanged = TRUE;

                /***
                            // examine the NICs
                            ZeroMemory(MtuNicRecord, sizeof(DEVMN_NIC_RECORD)*DEVMN_NUM_NIC);
                            LastNicIdx = 0;
                            GetNicList();
                            DisplayNicList();
                            SpawnMtuService();
                ***/

                break;
            }
            case WAIT_TIMEOUT:
            {
                // if (bRegChanged == TRUE) // comment out to reply on periodic reg scan (not reg change)
                {
                    QCMTU_Print("DevMon_WAIT_TIMEOUT: scanning system...\n");

                    bRegChanged = FALSE;

                    // examine the NICs 1s after registry change to consolidate multiple reg changes
                    ZeroMemory(MtuNicRecord, sizeof(DEVMN_NIC_RECORD) * DEVMN_NUM_NIC);
                    LastNicIdx = 0;
                    GetNicList();
                    QCMTU_Print("Scanned system, %d total adapters\n", LastNicIdx);
                    DisplayNicList();
                    SpawnMtuService();
                }
                break;
            }
            default:
            {
                QCMTU_Print("DevMon_EVENT_DEFAULT: %d\n", waitIdx);
                ResetEvent(waitHandle[waitIdx]);
                break;
            }
        } // switch
    }  // while

    CloseHandle(RegMonChangeEvent);
    CloseHandle(RegMonExitEvent);
    RegCloseKey(hTestKey);
    QCMTU_DeleteLock();
    QCMTU_CloseRunningInstance();

    QCMTU_Print("Reg monitor thread exit\n");
    return 0;
}  // DEVMN_DevMonThread

VOID GetNicList(VOID)
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
                    result = QueryNicSwKeys(subKey);
                    if (result == TRUE)
                    {
                        // printf("QueryKey: TRUE\n");
                        return;
                    }
                }
            }
        }

        RegCloseKey(hTestKey);

        GetNetInterfaceName();

        return;
    }

    return;
}  // GetNicList

BOOL QueryNicSwKeys(PTCHAR InstanceKey)
{
    HKEY hTestKey;
    char fullKeyName[MAX_KEY_LENGTH];
    BOOL ret = FALSE;

    sprintf(fullKeyName, "%s\\%s", QCNET_REG_HW_KEY, TEXT(InstanceKey));

    // QCMTU_Print("-->QueryNicSwKeys: full key name: [%s]\n", fullKeyName);

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
        ret = QueryInstance(hTestKey, fullKeyName);
        RegCloseKey(hTestKey);

        return ret;
    }

    // QCMTU_Print("<--QueryNicSwKeys: full key name: [%s]\n", fullKeyName);

    return FALSE;
}  // QueryNicSwKeys

BOOL QueryInstance
(
    HKEY  hKey,
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

    // QCMTU_Print("-->QueryInstance: full key name: [%s]\n", FullKeyName);

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
                sprintf(fullKeyName, "%s\\%s", FullKeyName, TEXT(subKey));
                result = GetDeviceInstance(fullKeyName);
                if (result == TRUE)
                {
                    // QCMTU_Print("-->QueryInstance: TRUE\n");
                    return TRUE;
                }
            }
        }
    }

    // QCMTU_Print("<--QueryInstance: FALSE\n");

    return FALSE;
}  // QueryInstance

BOOL GetDeviceInstance(PTCHAR InstanceKey)
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
        ret = GetInstanceNames
        (
            hTestKey,
            "QCDeviceControlFile"
        );

        RegCloseKey(hTestKey);
    }

    return ret;
}

BOOL GetInstanceNames
(
    HKEY  hKey,
    PCHAR EntryName         // "QCDeviceControlFile"
)
{
    DWORD retCode = ERROR_SUCCESS;
    TCHAR valueName[MAX_VALUE_NAME];
    TCHAR swKeyName[MAX_VALUE_NAME];
    DWORD valueNameLen = MAX_VALUE_NAME;
    BOOL  instanceFound = FALSE;
    HKEY  hSwKey;
    CHAR  friendlyName[MAX_VALUE_NAME];
    CHAR  controlFileName[MAX_VALUE_NAME];

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
        instanceFound = TRUE;
        // QCMTU_Print("GetInstanceNames: FriendlyName: [%s]\n", valueName);
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

            instanceFound = TRUE;
            // QCMTU_Print("GetInstanceNames: desc-%d: [%s]\n", instanceFound, valueName);
        }
    }

    if (instanceFound == TRUE)
    {
        PCHAR p = (PCHAR)valueName + valueNameLen;

        while ((p > valueName) && (*p != ';'))
        {
            p--;
        };
        if (*p == ';') p++;
        strcpy(friendlyName, p);

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
            sprintf(swKeyName, "%s\\%s", QCNET_REG_SW_KEY, TEXT(valueName));

            // printf("Got SW Key <%s>\n", swKeyName);

            // Open device software key
            retCode = RegOpenKeyEx
            (
                HKEY_LOCAL_MACHINE,
                swKeyName,
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
                    strcpy(controlFileName, p);

                    // record the names
                    strcpy(MtuNicRecord[LastNicIdx].FriendlyName, friendlyName);
                    strcpy(MtuNicRecord[LastNicIdx].ControlFileName, controlFileName);

                    // QCMTU_Print("GetInstanceNames: FriendlyName: [%s]\n", friendlyName);  // DEBUG
                    // QCMTU_Print("GetInstanceNames: QCDeviceControlFile: [%s]\n", controlFileName);  // DEBUG

                    // Get NIC GUID
                    valueName[0] = 0;
                    valueNameLen = MAX_VALUE_NAME;
                    retCode = RegQueryValueEx
                    (
                        hSwKey,
                        "NetCfgInstanceId",
                        NULL,         // reserved
                        NULL,         // returned type
                        valueName,
                        &valueNameLen
                    );

                    if (retCode == ERROR_SUCCESS)
                    {
                        valueName[valueNameLen] = 0;
                        strcpy(MtuNicRecord[LastNicIdx].InterfaceId, valueName);
                        // QCMTU_Print("GetInstanceNames: InterfaceId: [%s]\n", valueName);  // DEBUG
                    }
                    else
                    {
                        MtuNicRecord[LastNicIdx].InterfaceId[0] = 0;
                    }

                    ++LastNicIdx;
                    if (LastNicIdx >= DEVMN_NUM_NIC)
                    {
                        QCMTU_Print("WARNING: max num adapters reached!\n");
                        LastNicIdx = DEVMN_NUM_NIC - 1;
                    }
                    RegCloseKey(hSwKey);
                    return FALSE; // TRUE;
                }
                else
                {
                    // QCMTU_Print("GetInstanceNames: skip: [%s]\n", friendlyName);
                }

                RegCloseKey(hSwKey);
            }  // if (retCode == ERROR_SUCCESS)
        }  // if (retCode == ERROR_SUCCESS)
        else
        {
            // QCMTU_Print("GetInstanceNames: cannot get device software key, retCode %u\n", retCode);
        }
    }  // if (instanceFound == TRUE)

    return FALSE;
}  // GetInstanceNames

int SpawnMtuService(VOID)
{
    // Compare new NIC list and active list
    int i;
    PDEVMN_NIC_RECORD activeNic;

    if (AppDebugMode == TRUE)
    {
        QCMTU_Print("-->SpawnMtuService\n");
    }

    for (i = 0; i < DEVMN_NUM_NIC; ++i)
    {
        if (MtuNicRecord[i].FriendlyName[0] != 0)
        {
            if (FALSE == IsInActiveList(MtuNicRecord[i].FriendlyName))
            {
                // add to active list
                activeNic = AddNicToActiveList(i);
                if (activeNic != NULL)
                {
                    if (QMICL_StartMTUService(activeNic) == TRUE)
                    {
                        QCMTU_Print("SpawnMtuService: Started MTU service for <%s><%s>\n",
                                    activeNic->FriendlyName, activeNic->InterfaceName);
                    }
                    else
                    {
                        DEVMN_RemoveActiveNic(activeNic);
                        DisplayActiveList();
                        if (AppDebugMode == TRUE)
                        {
                            QCMTU_Print("SpawnMtuService: Cannot start MTU service for <%s>\n", MtuNicRecord[i].FriendlyName);
                        }
                    }
                }
                else
                {
                    QCMTU_Print("SpawnMtuService: AddNicToActiveList failure <%s>\n", MtuNicRecord[i].FriendlyName);
                }
            }
            else
            {
                if (AppDebugMode == TRUE)
                {
                    QCMTU_Print("SpawnMtuService: NIC already active <%s>\n", MtuNicRecord[i].FriendlyName);
                }
            }
        }
        else
        {
            if (AppDebugMode == TRUE)
            {
                QCMTU_Print("SpawnMtuService: no more NIC\n");
            }
            break;
        }
    }

    if (AppDebugMode == TRUE)
    {
        QCMTU_Print("<--SpawnMtuService\n");
    }
    return 0;
}  // SpawnMtuService

BOOL IsInActiveList(PCHAR NicFriendlyName)
{
    PDEVMN_NIC_RECORD item;
    BOOL inList = FALSE;

    QCMTU_RequestLock();

    item = NicRecHead;
    while (item != NULL)
    {
        if (strcmp(item->FriendlyName, NicFriendlyName) == 0)
        {
            inList = TRUE;
            break;
        }
        item = item->Next;
    }

    QCMTU_ReleaseLock();

    return inList;
}  // IsInActiveList

PDEVMN_NIC_RECORD AddNicToActiveList(int NicIdx)
{
    int i, idx = -1;

    QCMTU_RequestLock();

    // find available place holder
    for (i = 0; i < DEVMN_NUM_NIC; ++i)
    {
        if (MtuNicActiveRecord[i].InUse == FALSE)
        {
            idx = i;
            break;
        }
    }

    if (idx < 0)
    {
        QCMTU_Print("AddNicToActiveList: Error: no place holder for MTU rec\n");
        QCMTU_ReleaseLock();
        return NULL;
    }


    if (NicRecHead == NULL)
    {
        NicRecHead = NicRecTail = &MtuNicActiveRecord[idx];
    }
    else
    {
        NicRecTail->Next = &MtuNicActiveRecord[idx];
        NicRecTail = NicRecTail->Next;
    }
    NicRecTail->Next = NULL;

    strcpy(NicRecTail->FriendlyName, MtuNicRecord[NicIdx].FriendlyName);
    strcpy(NicRecTail->ControlFileName, MtuNicRecord[NicIdx].ControlFileName);
    strcpy(NicRecTail->InterfaceId, MtuNicRecord[NicIdx].InterfaceId);
    strcpy(NicRecTail->InterfaceName, MtuNicRecord[NicIdx].InterfaceName);
    NicRecTail->Index = idx;  // same as its array index
    NicRecTail->InUse = TRUE;

    QCMTU_ReleaseLock();

    return NicRecTail;

}  // AddNicToActiveList

BOOL DEVMN_RemoveActiveNic(PVOID Context)
{
    PDEVMN_NIC_RECORD item, prev;
    BOOL result = FALSE;

    if (AppDebugMode == TRUE)
    {
        QCMTU_Print("-->DEVMN_RemoveActiveNic: Remove MTU client (0x%p)\n", Context);
    }

    QCMTU_RequestLock();

    item = prev = NicRecHead;

    if (item == NULL)
    {
        QCMTU_Print("<--DEVMN_RemoveActiveNic: Error: empty active NIC list (0x%p)\n", Context);

        QCMTU_ReleaseLock();

        return result;
    }


    // if it's head
    if (item == Context)
    {
        if (AppDebugMode == TRUE)
        {
            QCMTU_Print("<--DEVMN_RemoveActiveNic-1: Remove MTU client for <%s>\n", prev->FriendlyName);
        }
        item = item->Next;
        NicRecHead = item;
        if (NicRecHead == NULL)  // empty list
        {
            NicRecTail = NULL;
        }
        DEVMN_ResetNicItem(prev);

        QCMTU_ReleaseLock();
        return TRUE;
    }

    item = item->Next;
    while (item != NULL)
    {
        if (item == Context)
        {
            if (AppDebugMode == TRUE)
            {
                QCMTU_Print("<--DEVMN_RemoveActiveNic-2: Remove MTU client for <%s>\n", item->FriendlyName);
            }
            result = TRUE;
            if (item == NicRecTail)
            {
                NicRecTail = prev;
            }
            prev->Next = item->Next;
            DEVMN_ResetNicItem(item);
            break;
        }
        item = item->Next;
        prev = prev->Next;
    }

    QCMTU_ReleaseLock();

    if (AppDebugMode == TRUE)
    {
        QCMTU_Print("<--DEVMN_RemoveActiveNic: Remove MTU client (0x%p) result=%u\n", Context, result);
    }
    return result;
} // DEVMN_RemoveActiveNic

VOID TerminateAllMtuThreads(VOID)
{
    PDEVMN_NIC_RECORD item;

    QCMTU_RequestLock();

    while (item = NicRecHead)
    {
        NicRecHead = NicRecHead->Next;  // de-queue item

        QCMTU_ReleaseLock();

        QCMTU_Print("TerminateAllMtuThreads: Terminate MTU client for <%s>\n", item->FriendlyName);
        SetEvent(item->Event[QMICL_EVENT_TERMINATE]);
        if (item->MtuThreadHandle != INVALID_HANDLE_VALUE)
        {
            QCMTU_Print("TerminateAllMtuThreads: Wait for MTU main thread termination...\n");
            WaitForSingleObject(item->MtuThreadHandle, INFINITE);
            item->MtuThreadHandle = INVALID_HANDLE_VALUE;
            QCMTU_Print("TerminateAllMtuThreads: MTU client terminated for thread 0x%x\n", item->MtuThreadHandle);
        }

        QCMTU_RequestLock();

        DEVMN_ResetNicItem(item);
    }

    QCMTU_ReleaseLock();

    return;
}  // TerminateAllMtuThreads

VOID DEVMN_ResetNicItem(PDEVMN_NIC_RECORD Item)
{
    int i;

    if (AppDebugMode == TRUE)
    {
        QCMTU_Print("-->_ResetNicItem: NIC[%d]\n", Item->Index);
    }
    Item->Next = NULL;
    Item->FriendlyName[0] = 0;
    Item->ControlFileName[0] = 0;
    Item->InterfaceId[0] = 0;
    Item->InterfaceName[0] = 0;
    Item->InUse = FALSE;

    Item->MtuServiceHandle = INVALID_HANDLE_VALUE;
    Item->MtuThreadHandle = INVALID_HANDLE_VALUE;
    for (i = 0; i < QMICL_MANAGING_THREAD_COUNT; i++)
    {
        Item->ManagedThread[i] = INVALID_HANDLE_VALUE;
    }
    for (i = 0; i < QMICL_EVENT_COUNT; i++)
    {
        if (Item->Event[i] != NULL)
        {
            CloseHandle(Item->Event[i]);
            Item->Event[i] = NULL;
        }
    }
    if (AppDebugMode == TRUE)
    {
        QCMTU_Print("<--_ResetNicItem: NIC[%d]\n", Item->Index);
    }
}  // DEVMN_ResetNicItem

// This function must be called after GetNicList()
VOID GetNetInterfaceName(VOID)
{
    HKEY hTestKey;
    char fullKeyName[MAX_KEY_LENGTH];
    BOOL ret = FALSE;
    int i;

    // QCMTU_Print("-->GetNetInterfaceName\n");

    for (i = 0; i < DEVMN_NUM_NIC; ++i)
    {
        if (MtuNicRecord[i].InterfaceId[0] == 0)
        {
            continue;
        }

        sprintf(fullKeyName, "%s\\%s\\Connection", DEVMON_NET_CONNECTION_REG_KEY, MtuNicRecord[i].InterfaceId);

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
            TCHAR valueName[MAX_VALUE_NAME];
            DWORD valueNameLen;
            DWORD retCode;

            valueName[0] = 0;
            valueNameLen = MAX_VALUE_NAME;
            retCode = RegQueryValueEx
            (
                hTestKey,
                "Name",
                NULL,         // reserved
                NULL,         // returned type
                valueName,
                &valueNameLen
            );

            if (retCode == ERROR_SUCCESS)
            {
                valueName[valueNameLen] = 0;
                strcpy(MtuNicRecord[i].InterfaceName, valueName);
                // QCMTU_Print("GetInterfaceNames: InterfaceName: [%s]\n", valueName);
            }
            else
            {
                QCMTU_Print("GetInterfaceNames: error for [%s]\n", fullKeyName);
            }

            RegCloseKey(hTestKey);
        }
    } // for

     QCMTU_Print("<--GetNetInterfaceName\n");

    return;
}  // GetNetInterfaceName
