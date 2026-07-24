/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B M A I N . C

GENERAL DESCRIPTION
    Driver entry point and common utility functions for the QDSS USB
    function driver.

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

    //call this to make sure NonPagedPoolNx is passed to ExAllocatePool in Win10 and above
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

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
