/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                        Q D D L L . C P P

GENERAL DESCRIPTION
    This file implements DLL entry points and exported COM registration
    functions for the Qualcomm device configuration library (qcdev).

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "qddll.h"

QCDEVLIB_API HRESULT __cdecl DllRegisterServer(void)
{
    return S_OK;
}

QCDEVLIB_API HRESULT __cdecl DllUnregisterServer(void)
{
    return S_OK;
}

BOOL WINAPI DllMain
(
    HINSTANCE hinstDLL,    // DLL module handle
    DWORD     fdwReason,   // calling reason
    LPVOID    lpReserved
)
{
    // printf("-->DllMain\n");
    // Perform actions based on the reason for calling.
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            // printf("DllMain: DLL_PROCESS_ATTACH\n");
            DisableThreadLibraryCalls(hinstDLL);
            break;

        case DLL_THREAD_ATTACH:
            // printf("DllMain: DLL_THREAD_ATTACH\n");
            break;

        case DLL_THREAD_DETACH:
            // printf("DllMain: DLL_THREAD_DETACH\n");
            break;

        case DLL_PROCESS_DETACH:
            // printf("DllMain: DLL_PROCESS_DETACH\n");
            break;
    }
    // printf("<--DllMain\n");
    return TRUE;
}

QCDEVLIB_API VOID __cdecl QDDLL_SetLoggingCallback(QCD_LOGGING_CALLBACK Cb)
{
    QcDevice::SetLoggingCallback(Cb);
}

QCDEVLIB_API VOID __cdecl QDDLL_SetDeviceChangeCallback(DEVICECHANGE_CALLBACK Cb)
{
    QcDevice::SetDeviceChangeCallback(Cb);
}

QCDEVLIB_API VOID __cdecl QDDLL_SetDeviceChangeCallbackWithContext(DEVICECHANGE_CALLBACK Cb, PVOID AppContext)
{
    QcDevice::SetDeviceChangeCallback(Cb, AppContext);
}

QCDEVLIB_API VOID __cdecl QDDLL_SetDeviceChangeCallback_N(DEVICECHANGE_CALLBACK_N Cb)
{
    QcDevice::SetDeviceChangeCallback(Cb);
}

QCDEVLIB_API VOID __cdecl QDDLL_SetFeature(PVOID Features)
{
    PDEV_FEATURE_SETTING myst = (PDEV_FEATURE_SETTING)Features;

    QcDevice::QCD_Printf("-->QDDLL_SetFeature: 0x%x, 0x%x, 0x%x\n", myst->Version, myst->Settings, myst->DeviceClass);
    QcDevice::SetFeature(Features);
    QcDevice::QCD_Printf("<--QDDLL_SetFeature\n");
}

QCDEVLIB_API VOID __cdecl QDDLL_StartDeviceMonitor(VOID)
{
    QcDevice::StartDeviceMonitor();
}

QCDEVLIB_API VOID __cdecl QDDLL_StopDeviceMonitor(VOID)
{
    QcDevice::StopDeviceMonitor();
}

QCDEVLIB_API ULONG  __cdecl QDDLL_GetDevice(PVOID DevInfo)
{
    return QcDevice::GetDevice(DevInfo);
}

QCDEVLIB_API PCHAR __cdecl QDDLL_GetPortName(PVOID DeviceName)
{
    return QcDevice::GetPortName(DeviceName);
}

QCDEVLIB_API PCHAR __cdecl QDDLL_LibVersion(PVOID)
{
    return QcDevice::LibVersion();
}

// ************** EXPERIMENTAL APIs *******************

QCDEVLIB_API ULONG __cdecl QDDLL_GetDeviceList(PVOID Buffer, ULONG BufferSize, PULONG ActualSize)
{
    return QcDevice::GetDeviceList(Buffer, BufferSize, ActualSize);
}

QCDEVLIB_API HANDLE __cdecl QDDLL_OpenDevice(PVOID DeviceName)
{
    return QcDevice::OpenDevice(DeviceName);
}

QCDEVLIB_API VOID __cdecl QDDLL_CloseDevice(HANDLE hDevice)
{
    QcDevice::CloseDevice(hDevice);
}

QCDEVLIB_API BOOL __cdecl QDDLL_ReadFromDevice
(
    HANDLE hDevice,
    PVOID  RxBuffer,
    DWORD  NumBytesToRead,
    LPDWORD NumBytesReturned
)
{
    return QcDevice::ReadFromDevice(hDevice, RxBuffer, NumBytesToRead, NumBytesReturned);
}

QCDEVLIB_API BOOL __cdecl QDDLL_SendToDevice
(
    HANDLE hDevice,
    PVOID  TxBuffer,
    DWORD  NumBytesToSend,
    LPDWORD NumBytesSent
)
{
    return QcDevice::SendToDevice(hDevice, TxBuffer, NumBytesToSend, NumBytesSent);
}
// ************** END OF EXPERIMENTAL APIs *******************
