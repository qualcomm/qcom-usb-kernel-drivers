/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q C M A I N . C

GENERAL DESCRIPTION
    This file implements the WDF-based USB serial driver entry point and
    device lifecycle management. It provides DriverEntry, WDF driver and
    device configuration, USB interface and pipe enumeration, vendor
    registry parameter processing, device context initialization, kernel
    thread creation for read/write/interrupt pipelines, and helper
    utilities for registry access and timed waits.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "QCMAIN.h"
#include "QCPNP.h"

#ifdef EVENT_TRACING
#include "QCWPP.h"
#include "QCMAIN.tmh"
#endif

QCSER_VENDOR_CONFIG gVendorConfig;
UNICODE_STRING gServicePath;
long gDeviceIndex = 0;

/****************************************************************************
 *
 * function: DriverEntry
 *
 * purpose:  Driver entry point. Initializes the WDF driver object and
 *           stores the service registry path for later use.
 *
 * arguments:DriverObject  = pointer to the driver object.
 *           RegistryPath  = pointer to the driver registry path string.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
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

/****************************************************************************
 *
 * function: QCMAIN_SetDriverRegistryStringW
 *
 * purpose:  Writes a Unicode string value to the driver's registry key.
 *
 * arguments:pValueName  = name of the registry value to write.
 *           pValue      = Unicode string data to write.
 *           pDevContext = pointer to the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
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


/****************************************************************************
 *
 * function: QCMAIN_DeleteDriverRegistryValue
 *
 * purpose:  Removes a value entry from the driver's registry key.
 *
 * arguments:pValueName  = name of the registry value to delete.
 *           pDevContext = pointer to the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
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
/****************************************************************************
 *
 * function: QCMAIN_SetDriverRegistryDword
 *
 * purpose:  Writes a DWORD value to the driver's registry key.
 *
 * arguments:pValueName  = name of the registry value to write.
 *           value       = DWORD data to write.
 *           pDevContext = pointer to the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
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

/****************************************************************************
 *
 * function: QCMAIN_GetDriverRegistryStringW
 *
 * purpose:  Reads a Unicode string value from the driver's registry key.
 *
 * arguments:key             = handle to the open registry key.
 *           pValueName      = name of the registry value to read.
 *           pValueEntryData = output buffer for the Unicode string.
 *           pDevContext     = pointer to the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
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

/****************************************************************************
 *
 * function: QCMAIN_GetDriverRegistryDword
 *
 * purpose:  Reads a DWORD value from the driver's registry key.
 *
 * arguments:key         = handle to the open registry key.
 *           pValueName  = name of the registry value to read.
 *           pValue      = output pointer to receive the DWORD value.
 *           pDevContext = pointer to the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
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

/****************************************************************************
 *
 * function: QCMAIN_Wait
 *
 * purpose:  Blocks the calling thread for the specified duration using a
 *           kernel timer event on the device timeout event object.
 *
 * arguments:pDevContext = pointer to the device context.
 *           WaitTime    = wait duration in 100-nanosecond units (negative
 *                         for relative time).
 *
 * returns:  VOID
 *
 ****************************************************************************/
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

/****************************************************************************
 *
 * function: AllocateUnicodeString
 *
 * purpose:  Allocates a non-paged pool buffer for a UNICODE_STRING and
 *           initializes its MaximumLength field.
 *
 * arguments:pusString = pointer to the UNICODE_STRING to initialize.
 *           ulSize    = size in bytes to allocate for the string buffer.
 *           pucTag    = pool allocation tag.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
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
