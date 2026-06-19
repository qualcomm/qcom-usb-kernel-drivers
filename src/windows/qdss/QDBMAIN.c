/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B M A I N . C

GENERAL DESCRIPTION
    Driver entry point and common utility functions for the QDSS USB
    function driver. Implements DriverEntry and Unicode string allocation.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "QDBMAIN.h"
#include "QDBPNP.h"

#ifdef EVENT_TRACING
#include "QDBWPP.h"
#include "QDBMAIN.tmh"      //  this is the file that will be auto generated
#endif

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

#ifdef EVENT_TRACING
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
 *           and initializes its MaximumLength fields.
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
    Ustring->Buffer = (PWSTR)ExAllocatePoolZero(NonPagedPoolNx, Size, Tag);
    if (Ustring->Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Ustring->MaximumLength = (USHORT)Size;
    return STATUS_SUCCESS;
}  // QDBMAIN_AllocateUnicodeString

