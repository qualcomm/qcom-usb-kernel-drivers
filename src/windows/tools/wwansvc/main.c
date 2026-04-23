/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M A I N . C

GENERAL DESCRIPTION
    This file implements the main entry point and Windows service control
    logic for the Qualcomm MTU configuration service (qcmtusvc).

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "main.h"
#include "utils.h"
#include "devmon.h"

#define HANDLER_EX

#define QCMTU_SERVICE_NAME TEXT("qcmtusvc")
#define QCMTU_SERVICE_DISPLAY_NAME TEXT("Qualcomm MTU Service")

GUID GUID_QCMTU_NIC = {
      0x2c7089aa, 0x2e0e, 0x11d1, {0xb1, 0x14, 0x00, 0xc0, 0x4f, 0xc2, 0xaa, 0xe4}};  // MODEM
      // 0x4d36e972, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}}; // NET
      // 0x86e0d1e0, 0x8089, 0x11d0, 0x9c, 0xe4, 0x08, 0x00, 0x3e, 0x30, 0x1f, 0x73}; // PORTS

HDEVNOTIFY  DevNotificationHandle = NULL;
DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
GUID MyDeviceGuid;
extern HANDLE RegMonExitEvent;
HANDLE MtuInstance = NULL;

// for app mode
HINSTANCE AppModuleHandle;
HWND AppWinHandle;
WNDCLASSEX AppWndClass;
TCHAR *AppClassName = TEXT("QCMTU");
TCHAR *AppTitle = TEXT("QCMTU");
BOOL AppDebugMode = FALSE;

// for service mode
SERVICE_STATUS ServiceState;
SERVICE_STATUS_HANDLE ServiceStateHandle = NULL;
HANDLE NicLock;

HANDLE QCMTU_InitializeLock(VOID)
{
    NicLock = CreateMutex(NULL, FALSE, NULL);
    return NicLock;
}  // QCMTU_InitializeLock

VOID QCMTU_DeleteLock(VOID)
{
    CloseHandle(NicLock);
}  // QCMTU_DeleteLock

VOID QCMTU_RequestLock(VOID)
{
    WaitForSingleObject(NicLock, INFINITE);
}  // QCMTU_RequestLock

VOID QCMTU_ReleaseLock(VOID)
{
    ReleaseMutex(NicLock);
}  // QCMTU_ReleaseLock

void Usage(char *AppName)
{
    char info[1024];

    sprintf(info, "\t========================================================\n");
    sprintf(info + strlen(info), "\tUsage: %s [install | uninstall | app | /?]\n", AppName);
    sprintf(info + strlen(info), "\t  Options:\n");
    sprintf(info + strlen(info), "\t     install - to install the service\n");
    sprintf(info + strlen(info), "\t             - after installation, start service with \"net start qcmtusvc\"\n");
    sprintf(info + strlen(info), "\t     uninstall - to uninstall the service\n");
    sprintf(info + strlen(info), "\t     app - to run in application/console mode\n");
    sprintf(info + strlen(info), "\t     /? - to print this usage info\n\n");
    sprintf(info + strlen(info), "\t  *IMPORTANT*: run from a Cmd Prompt launched \"As Admin\"\n\n");
    sprintf(info + strlen(info), "\t  Copyright (c) 2013, Qualcomm Inc. All rights reserved.\n");
    sprintf(info + strlen(info), "\t========================================================\n\n");

    QCMTU_Print("%s", info);
    printf("%s", info);
}

int __cdecl main(int argc, LPTSTR argv[])
{
    SERVICE_TABLE_ENTRY svcTbl[] =
    {
       {QCMTU_SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)QCMTU_ServiceMain},
       {NULL, NULL}
    };

    if (argc > 2)
    {
        Usage(argv[0]);
        return 1;
    }

    if (argc == 1)
    {
        // Run in service mode
        BOOL ret = StartServiceCtrlDispatcher(svcTbl);  // calls DEVMN_DeviceMonitor(FALSE)
        if (ret == FALSE)
        {
            QCMTU_Print("%s start error %u\n", QCMTU_SERVICE_DISPLAY_NAME, GetLastError());
            QCMTU_Print("To run in console mode, use: %s app\n", argv[0]);
            QCMTU_Print("To run in service mode, install it and try: net start %s\n", QCMTU_SERVICE_NAME);
            printf("%s start error %u\n", QCMTU_SERVICE_DISPLAY_NAME, GetLastError());
            printf("To run in console mode, use: %s app\n", argv[0]);
            printf("To run in service mode, install it and try: net start %s\n", QCMTU_SERVICE_NAME);
            Usage(argv[0]);
        }
    }
    else
    {
        // may run in service mode
        if (strstr(argv[1], "/") || strstr(argv[1], "?"))
        {
            Usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[1], "app") == 0)
        {
            printf("%s running in application/console mode.\n", QCMTU_SERVICE_DISPLAY_NAME);
            printf("Press ESC to exit with device disconnected\n");
            QCMTU_Print("%s running in application/console mode.\n", QCMTU_SERVICE_DISPLAY_NAME);
            QCMTU_Print("Press ESC to exit with device disconnected\n");

            DEVMN_DeviceMonitor(TRUE);
        }
        else if (strcmp(argv[1], "app_debug") == 0)
        {
            printf("%s running in application/console DEBUG mode\n", QCMTU_SERVICE_DISPLAY_NAME);
            printf("Press ESC to exit with device disconnected\n");
            QCMTU_Print("%s running in application/console DEBUG mode\n", QCMTU_SERVICE_DISPLAY_NAME);
            QCMTU_Print("Press ESC to exit with device disconnected\n");

            AppDebugMode = TRUE;
            DEVMN_DeviceMonitor(TRUE);
        }
        else if (strcmp(argv[1], "install") == 0)
        {
            QCMTU_InstallService();
        }
        else if (strcmp(argv[1], "uninstall") == 0)
        {
            QCMTU_UninstallService();
        }
        else
        {
            Usage(argv[0]);
            return 1;
        }
    }

    return 0;
}  // main

VOID QCMTU_InstallService(void)
{
    SC_HANDLE serviceControlManager, hService;
    TCHAR modulePath[_MAX_PATH + 1];
    DWORD size, rtnSize;

    QCMTU_Print("Installing: %s\n", QCMTU_SERVICE_DISPLAY_NAME);

    serviceControlManager = OpenSCManager(0, 0, SC_MANAGER_CREATE_SERVICE);
    // serviceControlManager = OpenSCManager(0, 0, SERVICE_ALL_ACCESS);

    size = sizeof(modulePath) / sizeof(modulePath[0]);

    if (serviceControlManager != NULL)
    {
        rtnSize = GetModuleFileName(0, modulePath, size);
        QCMTU_Print("Installing Module Path: <%s>\n", modulePath);

        if ((rtnSize > 0) && (rtnSize < size))
        {
            hService = CreateService
            (
                serviceControlManager,
                QCMTU_SERVICE_NAME,
                QCMTU_SERVICE_DISPLAY_NAME,
                SERVICE_ALL_ACCESS,         // desired access
                SERVICE_WIN32_OWN_PROCESS,  // service type
                SERVICE_AUTO_START,         // SERVICE_DEMAND_START, // start type
                SERVICE_ERROR_NORMAL,       // error control
                modulePath,
                0,                          // load order group
                0,                          // tag id
                0,                          // dependencies
                0,                          // service start name
                0                           // password
            );
            if (hService != NULL)
            {
                CloseServiceHandle(hService);
                QCMTU_Print("Installed: %s\n", QCMTU_SERVICE_DISPLAY_NAME);
            }
            else
            {
                QCMTU_Print("CreateService failure: <%s> error %d\n", QCMTU_SERVICE_DISPLAY_NAME, GetLastError());
            }
        }
        else
        {
            QCMTU_Print("Error: failed to obtain service path.\n");
        }

        CloseServiceHandle(serviceControlManager);
    }
    else
    {
        QCMTU_Print("OpenSCManager failure: <%s> error %d\n", QCMTU_SERVICE_DISPLAY_NAME, GetLastError());
    }
}  // QCMTU_InstallService

VOID QCMTU_UninstallService(void)
{
    SC_HANDLE serviceControlManager, hService;
    SERVICE_STATUS status;

    QCMTU_Print("Uninstalling: %s\n", QCMTU_SERVICE_DISPLAY_NAME);

    serviceControlManager = OpenSCManager(0, 0, SC_MANAGER_CONNECT);

    if (serviceControlManager != NULL)
    {
        hService = OpenService
        (
            serviceControlManager,
            QCMTU_SERVICE_NAME,
            SERVICE_QUERY_STATUS | DELETE
        );
        if (hService != NULL)
        {
            if (QueryServiceStatus(hService, &status))
            {
                if (status.dwCurrentState == SERVICE_STOPPED)
                {
                    DeleteService(hService);
                    QCMTU_Print("Uninstalled: %s\n", QCMTU_SERVICE_DISPLAY_NAME);
                }
            }

            CloseServiceHandle(hService);
        }

        CloseServiceHandle(serviceControlManager);
    }
}  // QCMTU_UninstallService

VOID WINAPI QCMTU_ServiceMain(DWORD Argc, LPTSTR *Argv)
{
    QCMTU_Print("Starting: %s\n", QCMTU_SERVICE_DISPLAY_NAME);

    // Init service state
    ServiceState.dwServiceType = SERVICE_WIN32;
    ServiceState.dwCurrentState = SERVICE_STOPPED;
    ServiceState.dwControlsAccepted = 0;
    ServiceState.dwWin32ExitCode = NO_ERROR;
    ServiceState.dwServiceSpecificExitCode = NO_ERROR;
    ServiceState.dwCheckPoint = 0;
    ServiceState.dwWaitHint = 0;

#ifdef HANDLER_EX
    ServiceStateHandle = RegisterServiceCtrlHandlerEx
    (
        QCMTU_SERVICE_NAME,
        (LPHANDLER_FUNCTION_EX)QCMTU_ServiceControlHandlerEx,
        NULL
    );
#else
    ServiceStateHandle = RegisterServiceCtrlHandler
    (
        QCMTU_SERVICE_NAME,
        (LPHANDLER_FUNCTION)QCMTU_ServiceControlHandler
    );
#endif // HANDLER_EX

    if (ServiceStateHandle != NULL)
    {
        // starting service
        ServiceState.dwCurrentState = SERVICE_START_PENDING;
        SetServiceStatus(ServiceStateHandle, &ServiceState);

        // run service
        ServiceState.dwControlsAccepted |= (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
        ServiceState.dwCurrentState = SERVICE_RUNNING;
        SetServiceStatus(ServiceStateHandle, &ServiceState);

        // run in service mode
        QCMTU_Print("%s running in service mode.\n", QCMTU_SERVICE_DISPLAY_NAME);
        DEVMN_DeviceMonitor(FALSE);
        QCMTU_Print("Terminated: %s\n", QCMTU_SERVICE_DISPLAY_NAME);

        // service to be stopped
        ServiceState.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(ServiceStateHandle, &ServiceState);

        // service stopped
        ServiceState.dwControlsAccepted &= ~(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
        ServiceState.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(ServiceStateHandle, &ServiceState);
    }
    else
    {
        QCMTU_Print("Service failure: %s\n", QCMTU_SERVICE_DISPLAY_NAME);
    }

    QCMTU_Print("ServiceMain Terminated: %s\n", QCMTU_SERVICE_DISPLAY_NAME);

}  // QCMTU_ServiceMain

#ifdef HANDLER_EX
DWORD WINAPI QCMTU_ServiceControlHandlerEx
(
    DWORD  ControlCode,
    DWORD  EventType,
    LPVOID EventData,
    LPVOID Context
)
#else
VOID WINAPI QCMTU_ServiceControlHandler
(
    DWORD  ControlCode
)
#endif
{
    DWORD rtnCode = ERROR_CALL_NOT_IMPLEMENTED;

    QCMTU_Print("%s QCMTU_ServiceControlHandler\n", QCMTU_SERVICE_DISPLAY_NAME);

    switch (ControlCode)
    {
        case SERVICE_CONTROL_INTERROGATE:
        {
            QCMTU_Print("<%s> SERVICE_CONTROL_INTERROGATE\n", QCMTU_SERVICE_NAME);
            rtnCode = NO_ERROR;
            break;
        }
        // case SERVICE_CONTROL_PRESHUTDOWN:
        case SERVICE_CONTROL_SHUTDOWN:
        {
            QCMTU_Print("<%s> SERVICE_CONTROL_SHUTDOWN\n", QCMTU_SERVICE_NAME);
            ServiceState.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(ServiceStateHandle, &ServiceState);
            SetEvent(RegMonExitEvent);
#ifdef HANDLER_EX
            return NO_ERROR;
#else
            return;
#endif
        }
        case SERVICE_CONTROL_STOP:
        {
            QCMTU_Print("<%s> SERVICE_CONTROL_STOP\n", QCMTU_SERVICE_NAME);
            ServiceState.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(ServiceStateHandle, &ServiceState);
            SetEvent(RegMonExitEvent);
#ifdef HANDLER_EX
            return NO_ERROR;
#else
            return;
#endif
        }
        case SERVICE_CONTROL_PAUSE:
        {
            QCMTU_Print("<%s> SERVICE_CONTROL_PAUSE\n", QCMTU_SERVICE_NAME);
            break;
        }
        case SERVICE_CONTROL_CONTINUE:
        {
            QCMTU_Print("<%s> SERVICE_CONTROL_CONTINUE\n", QCMTU_SERVICE_NAME);
            break;
        }
        case SERVICE_CONTROL_POWEREVENT:
        {
            QCMTU_Print("<%s> SERVICE_CONTROL_POWEREVENT\n", QCMTU_SERVICE_NAME);
            break;
        }
#ifdef HANDLER_EX
        case SERVICE_CONTROL_DEVICEEVENT:
        {
            switch (EventType)
            {
                case DBT_DEVICEARRIVAL:
                {
                    QCMTU_Print("<%s> SERVICE_CONTROL_DEVICEEVENT/DBT_DEVICEARRIVAL\n", QCMTU_SERVICE_NAME);
                    break;
                }
                case DBT_DEVICEREMOVECOMPLETE:
                {
                    QCMTU_Print("<%s> SERVICE_CONTROL_DEVICEEVENT/DBT_DEVICEREMOVECOMPLETE\n", QCMTU_SERVICE_NAME);
                    break;
                }
                case DBT_DEVICEQUERYREMOVE:
                {
                    QCMTU_Print("<%s> SERVICE_CONTROL_DEVICEEVENT/DBT_DEVICEQUERYREMOVE\n", QCMTU_SERVICE_NAME);
                    break;
                }
                case DBT_DEVICEQUERYREMOVEFAILED:
                {
                    QCMTU_Print("<%s> SERVICE_CONTROL_DEVICEEVENT/DBT_DEVICEQUERYREMOVEFAILED\n", QCMTU_SERVICE_NAME);
                    break;
                }
                case DBT_DEVICEREMOVEPENDING:
                {
                    QCMTU_Print("<%s> SERVICE_CONTROL_DEVICEEVENT/DBT_DEVICEREMOVEPENDING\n", QCMTU_SERVICE_NAME);
                    break;
                }
                case DBT_CUSTOMEVENT:
                {
                    QCMTU_Print("<%s> SERVICE_CONTROL_DEVICEEVENT/DBT_CUSTOMEVENT\n", QCMTU_SERVICE_NAME);
                    break;
                }
                default:
                {
                    QCMTU_Print("<%s> SERVICE_CONTROL_DEVICEEVENT/default\n", QCMTU_SERVICE_NAME);
                    break;
                }
            }
        }
#endif  // HANDLER_EX
        default:
        {
            QCMTU_Print("<%s> Control code not supported 0x%x\n",
                        QCMTU_SERVICE_NAME, ControlCode);

            if ((ControlCode >= 128) && (ControlCode <= 255))
                // user defined control code
                break;
            else
                // unrecognised control code
                break;
        }
    }

    SetServiceStatus(ServiceStateHandle, &ServiceState);

#ifdef HANDLER_EX
    return rtnCode;
#else
    return;
#endif
}  // QCMTU_ServiceControlHandler

LRESULT MyWindowProc
(
    HWND   Wnd,
    UINT   Message,
    WPARAM WParam,
    LPARAM LParam
)
{
    switch (Message)
    {
        case WM_CREATE:
        case WM_INITDIALOG:
        {
            QCMTU_Print("<%s> WM_CREAT/WM_INITDIALOG\n", AppClassName);

            MyDeviceGuid = GUID_QCMTU_NIC;

            // register device notification
            ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
            NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
            NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
            // NotificationFilter.dbcc_classguid = GUID_QCMTU_NIC; // MyDeviceGuid;
            memcpy(&NotificationFilter.dbcc_classguid, &GUID_QCMTU_NIC, sizeof(GUID));

            DevNotificationHandle = RegisterDeviceNotification
            (
                Wnd,
                &NotificationFilter,
                0 // DEVICE_NOTIFY_WINDOW_HANDLE
            );
            if (DevNotificationHandle == NULL)
            {
                QCMTU_Print("MyWindowProc:  register device notification, error %d\n", GetLastError());
                SetEvent(RegMonExitEvent);
            }
            else
            {
                return TRUE;
            }

            break;
        }
        case WM_DEVICECHANGE:
        {
            switch (WParam)
            {
                QCMTU_Print("<%s> WM_DEVICECHANGE\n", AppClassName);

                case DBT_DEVICEARRIVAL:
                {
                    QCMTU_Print("<%s> WM_DEVICECHANGE/DBT_DEVICEARRIVAL\n", AppClassName);
                    break;
                }
                case DBT_DEVICEREMOVECOMPLETE:
                {
                    QCMTU_Print("<%s> WM_DEVICECHANGE/DBT_DEVICEREMOVECOMPLETE\n", AppClassName);
                    break;
                }
                case DBT_DEVICEQUERYREMOVE:
                {
                    QCMTU_Print("<%s> WM_DEVICECHANGE/DBT_DEVICEQUERYREMOVE\n", AppClassName);
                    break;
                }
                case DBT_DEVICEQUERYREMOVEFAILED:
                {
                    QCMTU_Print("<%s> WM_DEVICECHANGE/DBT_DEVICEQUERYREMOVEFAILED\n", AppClassName);
                    break;
                }
                case DBT_DEVICEREMOVEPENDING:
                {
                    QCMTU_Print("<%s> WM_DEVICECHANGE/DBT_DEVICEREMOVEPENDING\n", AppClassName);
                    break;
                }
                case DBT_CUSTOMEVENT:
                {
                    QCMTU_Print("<%s> WM_DEVICECHANGE/DBT_CUSTOMEVENT\n", AppClassName);
                    break;
                }
                default:
                {
                    QCMTU_Print("<%s> WM_DEVICECHANGE/default\n", AppClassName);
                    break;
                }
            }
            return 0;
        }
        case WM_POWERBROADCAST:
        {
            QCMTU_Print("<%s> WM_POWERBROADCAST\n", AppClassName);
            return 0;
        }
        case WM_CLOSE:
        {
            QCMTU_Print("<%s> WM_CLOSE\n", AppClassName);

            if (DevNotificationHandle != NULL)
            {
                UnregisterDeviceNotification(DevNotificationHandle);
            }
            return  DefWindowProc(Wnd, Message, WParam, LParam);
        }
        case WM_DESTROY:
        {
            QCMTU_Print("<%s> WM_DESTROY\n", AppClassName);

            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(Wnd, Message, WParam, LParam);
}  // MyWindowProc

BOOL QCMTU_SetMTU(PQC_MTU Mtu, PCHAR InterfaceName)
{
    CHAR cmdLine[256];
    PCHAR ipver;
    PROCESS_INFORMATION processInfo;
    STARTUPINFO startupInfo;
    BOOL rtnVal;
    DWORD exitCode;

    if (Mtu->IpVer == 4)
    {
        ipver = "ipv4";
    }
    else if (Mtu->IpVer == 6)
    {
        ipver = "ipv6";
    }
    else
    {
        QCMTU_Print("QCMTU_SetMTU: error: invalid IP version\n");
        return FALSE;
    }

    // minimum MTU value required by Microsoft Windows
    if (Mtu->MTU < QCMTU_MIN_MTU)
    {
        Mtu->MTU = QCMTU_MIN_MTU;
    }

    sprintf(cmdLine, "C:\\Windows\\system32\\netsh.exe interface %s set interface interface=\"%s\" mtu=%u",
            ipver, InterfaceName, Mtu->MTU);

    QCMTU_Print("QCMTU_SetMTU: calling <%s>\n", cmdLine);

    // set MTU
    memset(&processInfo, 0, sizeof(processInfo));
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    rtnVal = CreateProcess
    (
        NULL, // L"C:\\Windows\\netsh.exe",
        cmdLine,
        NULL,
        NULL,
        FALSE,
        NORMAL_PRIORITY_CLASS,
        NULL,
        NULL,
        &startupInfo,
        &processInfo
    );

    if (rtnVal == FALSE)
    {
        QCMTU_Print("QCMTU_SetMTU: CreateProcess failure\n");
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    QCMTU_Print("QCMTU_SetMTU: exit 0x%X\n", exitCode);

    return rtnVal;

}  // QCMTU_SetMTU

BOOLEAN QCMTU_RunSingleInstance(VOID)
{
    DWORD errNo;

    MtuInstance = CreateEvent(NULL, FALSE, FALSE, "Global\\QualcommMTUSvc2013");
    errNo = GetLastError();

    if (MtuInstance == NULL)
    {
        QCMTU_Print("CreateEvent error [%d]\n", errNo);
        return FALSE;
    }
    else if (errNo == ERROR_ALREADY_EXISTS)
    {
        QCMTU_Print("Error: Another instance of the service is running\n");
        CloseHandle(MtuInstance);
        MtuInstance = NULL;
        return FALSE;
    }

    QCMTU_Print("MTU instance running...\n");

    return TRUE;
}  // QCMTU_RunSingleInstance

VOID QCMTU_CloseRunningInstance(VOID)
{
    if (MtuInstance != NULL)
    {
        CloseHandle(MtuInstance);
        MtuInstance = NULL;
    }
}  // QCMTU_CloseRunningInstance
