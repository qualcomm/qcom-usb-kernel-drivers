/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B M A I N . C

GENERAL DESCRIPTION
    Driver entry point and common utility functions for the QDSS USB
    function driver. Implements DriverEntry, Unicode string allocation,
    and per-device registry settings retrieval.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "QDBMAIN.h"
#include "QDBPNP.h"

#ifdef EVENT_TRACING
#define WPP_GLOBALLOGGER
#include "QDBMAIN.tmh"      //  this is the file that will be auto generated
#endif

LONG DevInstanceNumber = 0;

/****************************************************************************
 *
 * function: DriverEntry
 *
 * purpose:  Driver initialization entry point. Initializes the WDF
 *           driver object, registers device-add and driver-cleanup
 *           callbacks, and enables WPP software tracing.
 *
 * arguments:DriverObject = pointer to the driver object
 *           RegPath      = registry path string for driver parameters
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS DriverEntry
(
    PDRIVER_OBJECT  DriverObject,
    PUNICODE_STRING RegPath
)
{
    NTSTATUS              ntStatus;
    WDF_DRIVER_CONFIG     qdbConfig;
    WDF_OBJECT_ATTRIBUTES qdbAttributes;

    //call this to make sure NonPagedPoolNx is passed to ExAllocatePool in Win10 and above
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    QDB_DbgPrintG
    (
        0, 0,
        //("-->DriverEntry (Build: %s/%s)\n", __DATE__, __TIME__)
        ("-->DriverEntry (Build: )\n")
    );

#ifdef EVENT_TRACING
    // include this to support WPP.
    // This macro is required to initialize software tracing.
    WPP_INIT_TRACING(DriverObject, RegPath);
#endif

    WDF_OBJECT_ATTRIBUTES_INIT(&qdbAttributes);

    QDB_DbgPrintG(0, 0, ("DriverEntry: drv attr size: %u\n", qdbAttributes.Size));
    qdbAttributes.EvtCleanupCallback = QDBPNP_EvtDriverCleanupCallback;
    qdbAttributes.EvtDestroyCallback = NULL;
    QDB_DbgPrintG(0, 0, ("DriverEntry: ContextSizeOverride: %u\n", qdbAttributes.ContextSizeOverride));

    WDF_DRIVER_CONFIG_INIT(&qdbConfig, QDBPNP_EvtDriverDeviceAdd);

    ntStatus = WdfDriverCreate
    (
        DriverObject,
        RegPath,
        &qdbAttributes,
        &qdbConfig,
        WDF_NO_HANDLE
    );

    if (NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrintG(0, 0, ("<--DriverEntry ST 0x%X DriverObj 0x%p\n", ntStatus, DriverObject));
    }
    else
    {
        QDB_DbgPrintG(0, 0, ("<--DriverEntry failure 0x%X DriverObj 0x%p\n", ntStatus, DriverObject));
#ifdef EVENT_TRACING
        WPP_CLEANUP(DriverObject);
#endif
    }

    return ntStatus;

}  // DriverEntry

/****************************************************************************
 *
 * function: QDBMAIN_AllocateUnicodeString
 *
 * purpose:  Allocates a non-paged pool buffer for a UNICODE_STRING
 *           and initializes its Length and MaximumLength fields.
 *
 * arguments:Ustring = pointer to the UNICODE_STRING to initialize
 *           Size    = size in bytes to allocate for the string buffer
 *           Tag     = pool allocation tag
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBMAIN_AllocateUnicodeString(PUNICODE_STRING Ustring, SIZE_T Size, ULONG Tag)
{
    Ustring->Buffer = (PWSTR)ExAllocatePoolWithTag(NonPagedPool, Size, Tag);
    if (Ustring->Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Ustring->MaximumLength = (USHORT)Size;
    Ustring->Length = (USHORT)Size;
    return STATUS_SUCCESS;
}  // QDBMAIN_AllocateUnicodeString

/****************************************************************************
 *
 * function: QDBMAIN_GetRegistrySettings
 *
 * purpose:  Reads per-device registry settings (function type and
 *           I/O failure threshold) from the driver software key and
 *           stores them in the device context.
 *
 * arguments:Device = WDF device handle
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBMAIN_GetRegistrySettings(WDFDEVICE Device)
{
    PDEVICE_CONTEXT pDevContext;
    NTSTATUS        ntStatus;
    WDFKEY          hKey = NULL;
    DECLARE_CONST_UNICODE_STRING(valueFunctionName, L"QCDeviceFunction");
    DECLARE_CONST_UNICODE_STRING(valueIoFailureThreshold, L"QCDeviceIoFailureThreshold");

    pDevContext = QdbDeviceGetContext(Device);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBMAIN_GetRegistrySettings\n", pDevContext->PortName)
    );

    pDevContext->FunctionType = QDB_FUNCTION_TYPE_QDSS; // default
    pDevContext->IoFailureThreshold = 24;  // default

    ntStatus = WdfDeviceOpenRegistryKey
    (
        Device,
        PLUGPLAY_REGKEY_DRIVER,
        STANDARD_RIGHTS_READ,
        NULL,
        &hKey
    );

    if (NT_SUCCESS(ntStatus))
    {
        ntStatus = WdfRegistryQueryULong
        (
            hKey,
            &valueFunctionName,
            &(pDevContext->FunctionType)
        );
        if (!NT_SUCCESS(ntStatus))
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBMAIN_GetRegistrySettings: use default for funcType\n", pDevContext->PortName)
            );
            pDevContext->FunctionType = 0;
            return;
        }

        ntStatus = WdfRegistryQueryULong
        (
            hKey,
            &valueIoFailureThreshold,
            &(pDevContext->IoFailureThreshold)
        );
        if (!NT_SUCCESS(ntStatus))
        {
            QDB_DbgPrint
            (
                QDB_DBG_MASK_READ,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBMAIN_GetRegistrySettings: use default for funcType\n", pDevContext->PortName)
            );
            pDevContext->IoFailureThreshold = 24;  // default
            return;
        }

        WdfRegistryClose(hKey);
    }
    QDB_DbgPrint
    (
        QDB_DBG_MASK_READ,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBMAIN_GetRegistrySettings (ST 0x%x) Type %d \n",
        pDevContext->PortName, ntStatus, pDevContext->FunctionType)
    );

    return;

}  // QDBMAIN_GetRegistrySettings
