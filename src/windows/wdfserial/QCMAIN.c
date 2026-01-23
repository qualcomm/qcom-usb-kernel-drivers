/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "QCMAIN.h"
#include "QCPNP.h"

#ifdef EVENT_TRACING
#include "QCWPP.h"
#include "QCMAIN.tmh"
#endif

QCSER_VENDOR_CONFIG gVendorConfig;
UNICODE_STRING gServicePath;
long gDeviceIndex = 0;

NTSTATUS
DriverEntry
(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS              status;
    WDF_DRIVER_CONFIG     config;
    WDF_OBJECT_ATTRIBUTES attributes;

#ifdef EVENT_TRACING
    WPP_INIT_TRACING(DriverObject, RegistryPath);
#endif

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = QCPNP_EvtDriverCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, QCPNP_EvtDeviceAdd);
    QCSER_DbgPrintG
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("QCSER DriverEntry attribute size: %u, config size: %u, reg path: %ws\n", attributes.Size, config.Size, RegistryPath->Buffer)
    );

    status = WdfDriverCreate
    (
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE
    );

    if (NT_SUCCESS(status))
    {
        // Store the service path
        status = AllocateUnicodeString
        (
            &gServicePath,
            RegistryPath->Length,
            '6gaT'
        );
        if (NT_SUCCESS(status))
        {
            RtlCopyUnicodeString(&gServicePath, RegistryPath);
        }
        else
        {
            QCSER_DbgPrintG
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_CRITICAL,
                ("QCSER DriverEntry gServicePath string allocation FAILED status: 0x%x\n", status)
            );
        }
    }
    else
    {
        QCSER_DbgPrintG
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("QCSER DriverEntry WdfDriverCreate FAILED status: 0x%x, driverObj: 0x%p\n", status, DriverObject)
        );
#ifdef EVENT_TRACING
        WPP_CLEANUP(DriverObject);
#endif
    }

    return status;
}

NTSTATUS QCMAIN_SetDriverRegistryStringW
(
    PWCHAR          pValueName,
    PWCHAR          pValue,
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    if (pValueName && pValue)
    {
        WDFKEY         key;
        UNICODE_STRING ucValueName;
        UNICODE_STRING ucValue;

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCMAIN_SetDriverRegistryValueW valueName:%ws, value:%ws\n", pDevContext->PortName, pValueName, pValue)
        );

        status = WdfDeviceOpenRegistryKey
        (
            pDevContext->Device,
            PLUGPLAY_REGKEY_DRIVER,
            KEY_READ | KEY_WRITE,
            WDF_NO_OBJECT_ATTRIBUTES,
            &key
        );
        if (NT_SUCCESS(status))
        {
            RtlInitUnicodeString(&ucValueName, pValueName);
            RtlInitUnicodeString(&ucValue, pValue);
            status = WdfRegistryAssignUnicodeString
            (
                key,
                &ucValueName,
                &ucValue
            );
            WdfRegistryClose(key);
        }
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCMAIN_SetDriverRegistryStringW FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    return status;
}


NTSTATUS QCMAIN_DeleteDriverRegistryValue
(
    PWCHAR          pValueName,
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    if (pValueName)
    {
        WDFKEY         key;
        UNICODE_STRING ucValueName;

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCMAIN_DeleteDriverRegistryValue valueName:%ws\n", pDevContext->PortName, pValueName)
        );

        status = WdfDeviceOpenRegistryKey
        (
            pDevContext->Device,
            PLUGPLAY_REGKEY_DRIVER,
            KEY_READ | KEY_WRITE,
            WDF_NO_OBJECT_ATTRIBUTES,
            &key
        );
        if (NT_SUCCESS(status))
        {
            RtlInitUnicodeString(&ucValueName, pValueName);
            WdfRegistryRemoveValue(key, &ucValueName);
            WdfRegistryClose(key);
        }
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCMAIN_DeleteDriverRegistryValue FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    return status;
}
NTSTATUS QCMAIN_SetDriverRegistryDword
(
    PWCHAR              pValueName,
    DWORD               value,
    PDEVICE_CONTEXT     pDevContext
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    if (pValueName)
    {
        WDFKEY         key;
        UNICODE_STRING ucValueName;

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCMAIN_SetDriverRegistryDword valueName:%ws, value:%d\n", pDevContext->PortName, pValueName, value)
        );

        status = WdfDeviceOpenRegistryKey
        (
            pDevContext->Device,
            PLUGPLAY_REGKEY_DRIVER,
            KEY_READ | KEY_WRITE,
            WDF_NO_OBJECT_ATTRIBUTES,
            &key
        );
        if (NT_SUCCESS(status))
        {
            RtlInitUnicodeString(&ucValueName, pValueName);
            status = WdfRegistryAssignValue
            (
                key,
                &ucValueName,
                REG_DWORD,
                sizeof(DWORD),
                &value
            );
            WdfRegistryClose(key);
        }
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCMAIN_SetDriverRegistryDword FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    return status;
}

NTSTATUS QCMAIN_GetDriverRegistryStringW
(
    WDFKEY              key,
    PCUNICODE_STRING    pValueName,
    PUNICODE_STRING     pValueEntryData,
    PDEVICE_CONTEXT     pDevContext
)
{
    NTSTATUS            status = STATUS_INVALID_PARAMETER;

    if (pValueName && pValueName->Buffer && pValueEntryData)
    {
        QCSER_DbgPrintG
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCMAIN_GetDriverRegistryStringW valueName: %ws\n", pDevContext->PortName, pValueName->Buffer)
        );

        status = WdfRegistryQueryUnicodeString
        (
            key,
            pValueName,
            NULL,
            pValueEntryData
        );
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCMAIN_GetDriverRegistryStringW FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }
    return status;
}

NTSTATUS QCMAIN_GetDriverRegistryDword
(
    WDFKEY              key,
    PCUNICODE_STRING    pValueName,
    PULONG              pValue,
    PDEVICE_CONTEXT     pDevContext
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    if (pValueName && pValueName->Buffer && pValue)
    {
        QCSER_DbgPrintG
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCMAIN_GetDriverRegistryDword valueName: %ws\n", pDevContext->PortName, pValueName->Buffer)
        );

        status = WdfRegistryQueryValue
        (
            key,
            pValueName,
            REG_DWORD,
            pValue,
            NULL,
            NULL
        );
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCMAIN_GetDriverRegistryDword FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    return status;
}

VOID QCMAIN_Wait
(
    PDEVICE_CONTEXT pDevContext,
    LONGLONG WaitTime
)
{
    LARGE_INTEGER delayValue;
    delayValue.QuadPart = WaitTime; // 100-nanosecond units
    KeWaitForSingleObject
    (
        &pDevContext->TimeoutEvent,
        Executive,
        KernelMode,
        FALSE,
        &delayValue
    );
}

NTSTATUS AllocateUnicodeString(PUNICODE_STRING pusString, SIZE_T ulSize, ULONG pucTag)
{
    pusString->Buffer = (PWSTR)ExAllocatePoolZero(NonPagedPoolNx, ulSize, pucTag);
    if (pusString->Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    pusString->MaximumLength = (USHORT)ulSize;
    return STATUS_SUCCESS;
}
