/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B R E G . C

GENERAL DESCRIPTION
    Shared KMDF registry utility functions. Provides type-safe wrappers
    around WdfRegistry* APIs for reading and writing driver software keys.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "QDBREG.h"

NTSTATUS QDBREG_SetDriverRegistryStringW
(
    _In_ WDFDEVICE    device,
    _In_ PWCHAR const pValueName,
    _In_ PWCHAR const pValue
)
{
    NTSTATUS       status;
    WDFKEY         key;
    UNICODE_STRING ucValueName;
    UNICODE_STRING ucValue;

    if (device == NULL || pValueName == NULL || pValue == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    status = WdfDeviceOpenRegistryKey
    (
        device,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_SET_VALUE,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (NT_SUCCESS(status))
    {
        RtlInitUnicodeString(&ucValueName, pValueName);
        RtlInitUnicodeString(&ucValue, pValue);
        status = WdfRegistryAssignUnicodeString(key, &ucValueName, &ucValue);
        WdfRegistryClose(key);
    }

    return status;
}

NTSTATUS QDBREG_GetDriverRegistryStringW
(
    _In_                        WDFDEVICE    device,
    _In_                        PWCHAR const pValueName,
    _Out_writes_bytes_(bufLen)  PWCHAR       pOutBuf,
    _In_                        ULONG        bufLen
)
{
    NTSTATUS       status;
    WDFKEY         key;
    UNICODE_STRING ucValueName;
    UNICODE_STRING ucValue;

    if (device == NULL || pValueName == NULL || pOutBuf == NULL || bufLen == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    status = WdfDeviceOpenRegistryKey
    (
        device,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_READ,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (NT_SUCCESS(status))
    {
        RtlInitUnicodeString(&ucValueName, pValueName);
        ucValue.Buffer = pOutBuf;
        ucValue.Length = 0;
        ucValue.MaximumLength = (USHORT)min(bufLen, (ULONG)MAXUSHORT);
        status = WdfRegistryQueryUnicodeString(key, &ucValueName, NULL, &ucValue);
        WdfRegistryClose(key);
    }

    if (NT_SUCCESS(status))
    {
        if (ucValue.Length + sizeof(WCHAR) <= ucValue.MaximumLength)
        {
            pOutBuf[ucValue.Length / sizeof(WCHAR)] = L'\0';
        }
    }

    return status;
}

NTSTATUS QDBREG_SetDriverRegistryStringA
(
    _In_ WDFDEVICE   device,
    _In_ PWCHAR const pValueName,
    _In_ PCHAR  const pValue
)
{
    NTSTATUS       status;
    WDFKEY         key;
    UNICODE_STRING ucValueName;
    UNICODE_STRING ucValue;
    ANSI_STRING    ansiValue;

    if (device == NULL || pValueName == NULL || pValue == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    status = WdfDeviceOpenRegistryKey
    (
        device,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_SET_VALUE,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (NT_SUCCESS(status))
    {
        RtlInitAnsiString(&ansiValue, pValue);
        status = RtlAnsiStringToUnicodeString(&ucValue, &ansiValue, TRUE);
        if (NT_SUCCESS(status))
        {
            RtlInitUnicodeString(&ucValueName, pValueName);
            status = WdfRegistryAssignUnicodeString(key, &ucValueName, &ucValue);
            RtlFreeUnicodeString(&ucValue);
        }
        WdfRegistryClose(key);
    }

    return status;
}

NTSTATUS QDBREG_SetDriverRegistryDword
(
    _In_ WDFDEVICE    device,
    _In_ PWCHAR const pValueName,
    _In_ ULONG        value
)
{
    NTSTATUS       status;
    WDFKEY         key;
    UNICODE_STRING ucValueName;

    if (device == NULL || pValueName == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    status = WdfDeviceOpenRegistryKey
    (
        device,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_SET_VALUE,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (NT_SUCCESS(status))
    {
        RtlInitUnicodeString(&ucValueName, pValueName);
        status = WdfRegistryAssignULong(key, &ucValueName, value);
        WdfRegistryClose(key);
    }

    return status;
}

NTSTATUS QDBREG_GetDriverRegistryDword
(
    _In_  WDFDEVICE    device,
    _In_  PWCHAR const pValueName,
    _Out_ PULONG       pValue
)
{
    NTSTATUS       status;
    WDFKEY         key;
    UNICODE_STRING ucValueName;

    if (device == NULL || pValueName == NULL || pValue == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    status = WdfDeviceOpenRegistryKey
    (
        device,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_READ,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (NT_SUCCESS(status))
    {
        RtlInitUnicodeString(&ucValueName, pValueName);
        status = WdfRegistryQueryULong(key, &ucValueName, pValue);
        WdfRegistryClose(key);
    }

    return status;
}

NTSTATUS QDBREG_DeleteDriverRegistryValue
(
    _In_ WDFDEVICE    device,
    _In_ PWCHAR const pValueName
)
{
    NTSTATUS       status;
    WDFKEY         key;
    UNICODE_STRING ucValueName;

    if (device == NULL || pValueName == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    status = WdfDeviceOpenRegistryKey
    (
        device,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_SET_VALUE,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (NT_SUCCESS(status))
    {
        RtlInitUnicodeString(&ucValueName, pValueName);
        status = WdfRegistryRemoveValue(key, &ucValueName);
        WdfRegistryClose(key);
    }

    return status;
}
