/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                        Q D D L L . H

GENERAL DESCRIPTION
    This file defines the DLL export/import macros and exported function
    prototypes for the Qualcomm device configuration library (qcdev).

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef QDDLL_H
#define QDDLL_H

#include "qdpublic.h"

#ifdef QCDEVLIB_EXPORTS
#define QCDEVLIB_API __declspec(dllexport)
#else
#define QCDEVLIB_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

    QCDEVLIB_API HRESULT __cdecl DllRegisterServer(void);
    QCDEVLIB_API HRESULT __cdecl DllUnregisterServer(void);
    QCDEVLIB_API VOID __cdecl   QDDLL_SetLoggingCallback(QCD_LOGGING_CALLBACK Cb);
    QCDEVLIB_API VOID __cdecl   QDDLL_SetDeviceChangeCallback(DEVICECHANGE_CALLBACK Cb);
    QCDEVLIB_API VOID __cdecl   QDDLL_SetDeviceChangeCallbackWithContext(DEVICECHANGE_CALLBACK Cb, PVOID AppContext);
    QCDEVLIB_API VOID __cdecl   QDDLL_SetDeviceChangeCallback_N(DEVICECHANGE_CALLBACK_N Cb);
    QCDEVLIB_API VOID __cdecl   QDDLL_SetFeature(PVOID Features);
    QCDEVLIB_API VOID __cdecl   QDDLL_StartDeviceMonitor(VOID);
    QCDEVLIB_API VOID __cdecl   QDDLL_StopDeviceMonitor(VOID);
    QCDEVLIB_API ULONG __cdecl  QDDLL_GetDevice(PVOID DevInfo);
    QCDEVLIB_API PCHAR __cdecl  QDDLL_GetPortName(PVOID DeviceName);
    QCDEVLIB_API PCHAR __cdecl  QDDLL_LibVersion(PVOID);
    QCDEVLIB_API ULONG __cdecl  QDDLL_GetDeviceList(PVOID Buffer, ULONG BufferSize, PULONG ActualSize);
    QCDEVLIB_API HANDLE __cdecl QDDLL_OpenDevice(PVOID DeviceName);
    QCDEVLIB_API VOID __cdecl   QDDLL_CloseDevice(HANDLE hDevice);

    QCDEVLIB_API BOOL __cdecl QDDLL_ReadFromDevice
    (
        HANDLE hDevice,
        PVOID  RxBuffer,
        DWORD  NumBytesToRead,
        LPDWORD NumBytesReturned
    );

    QCDEVLIB_API BOOL __cdecl QDDLL_SendToDevice
    (
        HANDLE hDevice,
        PVOID  TxBuffer,
        DWORD  NumBytesToSend,
        LPDWORD NumBytesSent
    );

#ifdef __cplusplus
}
#endif

#endif // QDDLL_H
