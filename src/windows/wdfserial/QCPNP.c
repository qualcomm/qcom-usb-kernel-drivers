/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q C P N P . C

GENERAL DESCRIPTION
    This file implements all WDF PnP and power management callbacks for
    the wdfserial driver. It handles device addition, hardware preparation
    and release, D0 entry/exit power transitions, file create/close
    lifecycle, USB device and pipe configuration, vendor registry
    parameter processing, selective suspend, WMI power management
    registration, device name reporting, and worker thread creation.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "QCPNP.h"
#include "QCDSP.h"
#include "QCSER.h"
#include "QCINT.h"
#include "QCRD.h"
#include "QCWT.h"
#include "QCUTILS.h"
#include <wdmguid.h>

#ifdef EVENT_TRACING
#include "QCWPP.h"
#include "QCPNP.tmh"
#endif

WMIGUIDREGINFO PMWmiGuidList[] =
{
    // "Allow the computer to turn off this device to save power."
    // If this feature is on, the selective suspension is supported:
    //    1. If QCDriverSelectiveSuspendIdleTime does not exist or
    //       its value is less than 3s, we use 5s as the default value.
    //    2. If QCDriverSelectiveSuspendIdleTime has a valid value,
    //       use it as the idle timeout time.
    {
        &GUID_POWER_DEVICE_ENABLE,
        1,
        0
    }
};

/****************************************************************************
 *
 * function: QCPNP_EvtDeviceAdd
 *
 * purpose:  WDF callback invoked when a new device instance is added.
 *           Creates the device object, configures it, and processes vendor
 *           registry settings.
 *
 * arguments:Driver     = handle to the WDF driver object.
 *           DeviceInit = pointer to the device initialization structure.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_EvtDeviceAdd
(
    WDFDRIVER       Driver,
    PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS        status;
    PDEVICE_CONTEXT pDevContext;

    UNREFERENCED_PARAMETER(Driver);

    QCSER_DbgPrintG
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("QCSER QCPNP_EvtDeviceAdd\n")
    );

    status = QCPNP_DeviceCreate(DeviceInit, &pDevContext);
    if (!NT_SUCCESS(status))
    {
        goto exit;
    }
    pDevContext->FdoDeviceType = QCPNP_GetDeviceType(pDevContext);

    status = QCPNP_DeviceConfig(pDevContext);
    if (!NT_SUCCESS(status))
    {
        goto exit;
    }
    status = QCPNP_VendorRegistryProcess(pDevContext);
    if (!NT_SUCCESS(status))
    {
        goto exit;
    }

    if (pDevContext->FdoDeviceType == FILE_DEVICE_SERIAL_PORT)
    {
        QCPNP_ReportDeviceName(pDevContext);
    }

exit:
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrintG
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("QCSER QCPNP_EvtDeviceAdd FAILED status: 0x%x\n", status)
        );
    }
    return status;
}

/****************************************************************************
 *
 * function: QCPNP_GetDeviceType
 *
 * purpose:  Queries the device class name from PnP and returns whether the
 *           device is a serial port, modem, or unknown device type.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  DEVICE_TYPE value (FILE_DEVICE_SERIAL_PORT, FILE_DEVICE_MODEM,
 *           or FILE_DEVICE_UNKNOWN).
 *
 ****************************************************************************/
//Checks device type Modem or Ports
DEVICE_TYPE QCPNP_GetDeviceType(PDEVICE_CONTEXT pDevContext)
{
#define DEV_CLASS_UNKNOWN 0
#define DEV_CLASS_PORTS   1
#define DEV_CLASS_MODEM   2

    WDFDEVICE           device;
    NTSTATUS          ntStatus;
    CHAR              className[128];
    ULONG             bufLen = 128, resultLen = 0;
    PCHAR             classPorts = "P o r t s   ";  // len = 12
    PCHAR             classModem = "M o d e m   ";  // len = 12
    UCHAR             classType = DEV_CLASS_UNKNOWN;
    DEVICE_TYPE       deviceType = FILE_DEVICE_UNKNOWN;


    device = pDevContext->Device;
    className[0] = 0;

    ntStatus = WdfDeviceQueryProperty
    (
        device,
        DevicePropertyClassName,
        bufLen,
        (PVOID)className,
        &resultLen
    );

    if (ntStatus == STATUS_SUCCESS)
    {
        ULONG i;

        // extract class name into an ANSI string
        for (i = 0; i < resultLen; i++)
        {
            if (i >= 30)
            {
                i++;
                break;
            }
            if (className[i] == 0)
            {
                className[i] = ' ';
            }
        }
        className[i] = 0;

        if (RtlCompareMemory(className, classPorts, 12) == 12)
        {
            deviceType = FILE_DEVICE_SERIAL_PORT;
        }
        else if (RtlCompareMemory(className, classModem, 12) == 12)
        {
            deviceType = FILE_DEVICE_MODEM;
        }
        else
        {
            deviceType = FILE_DEVICE_UNKNOWN;
        }
    }
    else
    {
        deviceType = FILE_DEVICE_UNKNOWN;
        QCSER_DbgPrintG
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("QCSER GetDeviceType WdfDeviceQueryProperty FAILED status: 0x%x\n", ntStatus)
        );
    }

    QCSER_DbgPrintG
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("QCSER <--GetDeviceType: class=%s[%u] devType 0x%x ST 0x%x\n", className, classType, deviceType, ntStatus)
    );
    return deviceType;
}  // QCPNP_GetDeviceType ends

/****************************************************************************
 *
 * function: QCPNP_SetStamp
 *
 * purpose:  Writes a timestamp/presence stamp DWORD to the driver registry
 *           key to indicate driver startup or shutdown state.
 *
 * arguments:pDevContext = pointer to the device context.
 *           Startup     = TRUE to set the stamp on startup, FALSE on removal.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
//Set stamp value in registry
NTSTATUS QCPNP_SetStamp
(
    PDEVICE_CONTEXT pDevContext,
    BOOLEAN        Startup
)
{
    NTSTATUS       status = STATUS_SUCCESS;
    WDFDEVICE       device = pDevContext->Device;
    WDFKEY         key;
    UNICODE_STRING ucValueName;
    ULONG stampValue = 0;
    BOOLEAN bSelfOpen = FALSE;

    status = WdfDeviceOpenRegistryKey
    (
        device,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_QUERY_VALUE,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );

    if (!NT_SUCCESS(status))
    {
        return status;
    }
    bSelfOpen = TRUE;

    RtlInitUnicodeString(&ucValueName, VEN_DEV_TIME);
    if (Startup == TRUE)
    {
        stampValue = 1;
        status = QCMAIN_SetDriverRegistryDword((LPWSTR)ucValueName.Buffer, stampValue, pDevContext);
    }
    else
    {
        stampValue = 0;
        status = QCMAIN_SetDriverRegistryDword((LPWSTR)ucValueName.Buffer, stampValue, pDevContext);
    }

    if (bSelfOpen == TRUE)
    {
        WdfRegistryClose(key);
    }
    return STATUS_SUCCESS;
}

/****************************************************************************
 *
 * function: QCPNP_DeviceCreate
 *
 * purpose:  Creates the WDF device object, registers PnP/power callbacks,
 *           configures file object and I/O type, and initializes the device
 *           context.
 *
 * arguments:DeviceInit    = pointer to the device initialization structure.
 *           DeviceContext = output pointer to receive the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
/* Create device object and init device context area */
NTSTATUS QCPNP_DeviceCreate
(
    PWDFDEVICE_INIT DeviceInit,
    PDEVICE_CONTEXT *DeviceContext
)
{
    NTSTATUS                     status;
    WDFDEVICE                    device;
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_FILEOBJECT_CONFIG        fileSettings;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerEventCB;
    WDF_DEVICE_PNP_CAPABILITIES  pnpCapabilities;

    QCSER_DbgPrintG
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("QCSER QCPNP_DeviceCreate\n")
    );

    // Set up PNP callbacks
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerEventCB);
    pnpPowerEventCB.EvtDevicePrepareHardware = QCPNP_EvtDevicePrepareHardware;
    pnpPowerEventCB.EvtDeviceReleaseHardware = QCPNP_EvtDeviceReleaseHardware;
    pnpPowerEventCB.EvtDeviceD0Entry = QCPNP_EvtDeviceD0Entry;
    pnpPowerEventCB.EvtDeviceD0Exit = QCPNP_EvtDeviceD0Exit;
    pnpPowerEventCB.EvtDeviceQueryRemove = QCPNP_EvtDeviceQueryRemoval;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerEventCB);

    status = WdfDeviceInitAssignWdmIrpPreprocessCallback
    (
        DeviceInit,
        QCPNP_WdmPreprocessSystemControl,
        IRP_MJ_SYSTEM_CONTROL,
        NULL,
        0
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrintG
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("QCSER QCPNP_DeviceCreate set preprocess callback for IRP_MJ_SYSTEM_CONTROL FAILED status: 0x%x\n", status)
        );
    }

    // Request context
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, REQUEST_CONTEXT);
    WdfDeviceInitSetRequestAttributes(DeviceInit, &attributes);

    // File object config
    WDF_FILEOBJECT_CONFIG_INIT
    (
        &fileSettings,
        QCPNP_EvtFileCreate,
        QCPNP_EvtFileClose,
        WDF_NO_EVENT_CALLBACK
    );
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WdfDeviceInitSetFileObjectConfig
    (
        DeviceInit,
        &fileSettings,
        &attributes
    );
    WdfDeviceInitSetExclusive(DeviceInit, TRUE);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = QCPNP_EvtDeviceCleanup;
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_SERIAL_PORT);
    status = WdfDeviceCreate
    (
        &DeviceInit,
        &attributes,
        &device
    );
    if (NT_SUCCESS(status))
    {
        // Set up PnP capability
        WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCapabilities);
        pnpCapabilities.SurpriseRemovalOK = TRUE;
        WdfDeviceSetPnpCapabilities(device, &pnpCapabilities);

        *DeviceContext = QCDevGetContext(device);
        (*DeviceContext)->Device = device;
        status = QCSER_ResetDevice(*DeviceContext);
    }
    else
    {
        QCSER_DbgPrintG
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("QCSER QCPNP_DeviceCreate DevObj Creation FAILED status: 0x%x\n", status)
        );
    }

    return status;
}

/****************************************************************************
 *
 * function: QCPNP_DeviceConfig
 *
 * purpose:  Initializes kernel events, creates I/O queues, registers the
 *           device interface and symbolic link, and writes the SERIALCOMM
 *           registry entry.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
/* Setup device interface, symbolic link, queues and pnp callbacks */
NTSTATUS QCPNP_DeviceConfig
(
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS            status;
    WDFDEVICE           device;
    WDFKEY              key;
    WDF_IO_QUEUE_CONFIG queueConfig;
    UNICODE_STRING      deviceMapKey;

    QCSER_DbgPrintG
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("QCSER QCPNP_DeviceConfig\n")
    );

    // Initialize pnp event objects
    KeInitializeEvent
    (
        &pDevContext->TimeoutEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->DeviceRemoveEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadThreadD0ExitEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadThreadD0EntryEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->IntThreadD0ExitEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->IntThreadD0EntryEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadThreadD0EntryReadyEvent,
        NotificationEvent,
        TRUE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadThreadD0ExitReadyEvent,
        NotificationEvent,
        TRUE
    );
    KeInitializeEvent
    (
        &pDevContext->IntThreadD0EntryReadyEvent,
        NotificationEvent,
        TRUE
    );
    KeInitializeEvent
    (
        &pDevContext->IntThreadD0ExitReadyEvent,
        NotificationEvent,
        TRUE
    );
    KeInitializeEvent
    (
        &pDevContext->FileCreateEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->FileCloseEventRead,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->FileCloseEventWrite,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadThreadFileCloseReadyEvent,
        SynchronizationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->WriteThreadFileCloseReadyEvent,
        SynchronizationEvent,
        FALSE
    );

    //interrupt pipe thread events
    KeInitializeEvent
    (
        &pDevContext->IntThreadLpcEvent,
        SynchronizationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->IntThreadStartedEvent,
        SynchronizationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->InterruptCompletion,
        NotificationEvent,
        FALSE
    );

    KeInitializeEvent
    (
        &pDevContext->InterruptStartService,
        NotificationEvent,
        FALSE
    );

    KeInitializeEvent
    (
        &pDevContext->InterruptStopServiceEvent,
        NotificationEvent,
        FALSE
    );

    pDevContext->pInterruptPipeEvents[INT_COMPLETION_EVENT_INDEX] = &pDevContext->InterruptCompletion;
    pDevContext->pInterruptPipeEvents[INT_START_SERVICE_EVENT] = &pDevContext->InterruptStartService;
    pDevContext->pInterruptPipeEvents[INT_STOP_SERVICE_EVENT] = &pDevContext->InterruptStopServiceEvent;
    pDevContext->pInterruptPipeEvents[INT_DEVICE_D0_ENTRY_EVENT] = &pDevContext->IntThreadD0EntryEvent;
    pDevContext->pInterruptPipeEvents[INT_DEVICE_D0_EXIT_EVENT] = &pDevContext->IntThreadD0ExitEvent;
    pDevContext->pInterruptPipeEvents[INT_LPC_EVENT] = &pDevContext->IntThreadLpcEvent;

    KeClearEvent(&pDevContext->InterruptCompletion);
    KeClearEvent(&pDevContext->InterruptStartService);
    KeClearEvent(&pDevContext->InterruptStopServiceEvent);
    KeClearEvent(&pDevContext->IntThreadStartedEvent);
    KeClearEvent(&pDevContext->IntThreadD0EntryEvent);
    KeClearEvent(&pDevContext->IntThreadD0EntryReadyEvent);
    KeClearEvent(&pDevContext->IntThreadD0ExitEvent);
    KeClearEvent(&pDevContext->IntThreadD0ExitReadyEvent);

    // 1. Get comport number from registry
    device = pDevContext->Device;
    status = WdfDeviceOpenRegistryKey
    (
        device,
        PLUGPLAY_REGKEY_DEVICE,
        KEY_QUERY_VALUE,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrintG
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("QCSER QCPNP_DeviceConfig device regkey open FAILED status: 0x%x\n", status)
        );
        goto exit;
    }

    DECLARE_CONST_UNICODE_STRING(valueName, REG_VALUENAME_PORTNAME);
    DECLARE_CONST_UNICODE_STRING(symbolicLinkPrefix, DEVICE_LINK_NAME_PATH);
    DECLARE_UNICODE_STRING_SIZE(symbolicLinkName, 32);
    DECLARE_UNICODE_STRING_SIZE(comPort, 10);
    DECLARE_UNICODE_STRING_SIZE(serialcommValueName, 1024);

    status = WdfRegistryQueryUnicodeString
    (
        key,
        &valueName,
        NULL,
        &comPort
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrintG
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("QCSER QCPNP_DeviceConfig comport key query FAILED status: 0x%x\n", status)
        );
        goto exit;
    }

    QCSER_DbgPrintG
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("QCSER QCPNP_DeviceConfig comport key read: %ws\n", comPort.Buffer)
    );

    pDevContext->PortName = ExAllocatePoolZero(NonPagedPoolNx, comPort.Length * sizeof(WCHAR), '1gaT');
    if (pDevContext->PortName == NULL)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig allocate memory for PortName FAILED\n", comPort.Buffer)
        );
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }

    RtlCopyMemory(pDevContext->PortName, comPort.Buffer, comPort.Length * sizeof(WCHAR));

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_DeviceConfig comport key stored: %ws\n", pDevContext->PortName, pDevContext->PortName)
    );

    // 2. Initialze variables in device context
    pDevContext->CurrUartState = 0;
    pDevContext->WaitMask = 0xffff;
    pDevContext->UartStateInitialized = FALSE;
    pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_UNDEF;
    pDevContext->ReadTimeout.bUseReadInterval = FALSE;
    pDevContext->ReadTimeout.bReturnOnAnyChars = FALSE;
    pDevContext->DebugMask = 0x7FFFFFFF;
    pDevContext->DebugLevel = QCSER_DBG_LEVEL_VERBOSE;
    pDevContext->InterruptInPipe = NULL;
    pDevContext->BulkIN = NULL;
    pDevContext->BulkOUT = NULL;
    pDevContext->PowerManagementEnabled = TRUE;
    pDevContext->AmountInInQueue = 0;
    RtlZeroMemory(&pDevContext->Timeouts, sizeof(SERIAL_TIMEOUTS));
    RtlZeroMemory(&pDevContext->PerfStats, sizeof(SERIALPERF_STATS));
    RtlZeroMemory(&pDevContext->HandFlow, sizeof(SERIAL_HANDFLOW));
    RtlZeroMemory(&pDevContext->SerialStatus, sizeof(SERIAL_STATUS));

    // 3. Queues
    // Default Queue (IOCTL only)
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = QCDSP_EvtIoDeviceControl;
    status = WdfIoQueueCreate
    (
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevContext->DefaultQueue
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig DEFAULT queue create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    // Read Queue
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    status = WdfIoQueueCreate
    (
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevContext->ReadQueue
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig READ queue create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }
    status = WdfDeviceConfigureRequestDispatching
    (
        device,
        pDevContext->ReadQueue,
        WdfRequestTypeRead
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig READ queue config FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }
    status = WdfIoQueueReadyNotify
    (
        pDevContext->ReadQueue,
        QCDSP_EvtIoReadQueueReady,
        pDevContext
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig READ queue set queue state callback FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    // Write Queue
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    status = WdfIoQueueCreate
    (
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevContext->WriteQueue
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig WRITE queue create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }
    status = WdfDeviceConfigureRequestDispatching
    (
        device,
        pDevContext->WriteQueue,
        WdfRequestTypeWrite
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig WRITE queue config FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }
    status = WdfIoQueueReadyNotify
    (
        pDevContext->WriteQueue,
        QCDSP_EvtIoWriteQueueReady,
        pDevContext
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig WRITE queue set queue state callback FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    // Manual Queues
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfFalse;
    status = WdfIoQueueCreate
    (
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevContext->WaitOnDeviceRemovalQueue
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig WaitOnDeviceRemovalQueue create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfFalse;
    status = WdfIoQueueCreate
    (
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevContext->WaitOnMaskQueue
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig WaitOnMaskQueue create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    // Timeout Read Queue
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfFalse;
    status = WdfIoQueueCreate
    (
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevContext->TimeoutReadQueue
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig TimeoutReadQueue create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    // 4. Create symbilic link
    symbolicLinkName.Buffer[0] = 0;
    RtlAppendUnicodeStringToString(&symbolicLinkName, &symbolicLinkPrefix);
    RtlAppendUnicodeStringToString(&symbolicLinkName, &comPort);
    status = WdfDeviceCreateSymbolicLink
    (
        device,
        &symbolicLinkName
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig symbolic link create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_INFO,
        ("<%ws> QCPNP_DeviceConfig symbolic link created: %ws\n", pDevContext->PortName, symbolicLinkName.Buffer)
    );

    // Write registry entry to \HKLM\Hardware\DeviceMap\SERIALCOMM
    RtlInitUnicodeString(&deviceMapKey, REG_DEVICE_MAP_SUBKEY);
    status = WdfDeviceOpenDevicemapKey
    (
        device,
        &deviceMapKey,
        KEY_READ | KEY_WRITE,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig device map key open FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    // Generate new device number
    InterlockedIncrement(&gDeviceIndex);

    // Construct serialcomm ValueName (\Device\QCUSB_COM?_?)
    swprintf_s(serialcommValueName.Buffer, 1024, L"%wsQCUSB_%ws_%d", DEVICE_NAME_PATH, comPort.Buffer, gDeviceIndex);
    serialcommValueName.Length = (USHORT)wcslen(serialcommValueName.Buffer) * sizeof(WCHAR);
    status = WdfRegistryAssignUnicodeString
    (
        key,
        &serialcommValueName,
        &comPort
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig device map key assign FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    // Save to device extension
    pDevContext->SerialCommName = (PWSTR)ExAllocatePoolZero(NonPagedPoolNx, serialcommValueName.Length + sizeof(WCHAR), '8gaT');
    if (pDevContext->SerialCommName != NULL)
    {
        RtlCopyMemory(pDevContext->SerialCommName, serialcommValueName.Buffer, serialcommValueName.Length + sizeof(WCHAR));
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_INFO,
            ("<%ws> QCPNP_DeviceConfig device map key assigned valueName: %ws, length: %hu\n",
            pDevContext->PortName, pDevContext->SerialCommName, serialcommValueName.Length)
        );
    }

    // 5. Create device interface
    switch (pDevContext->FdoDeviceType)
    {
        case FILE_DEVICE_SERIAL_PORT:
        {
            status = WdfDeviceCreateDeviceInterface
            (
                device,
                (LPGUID)&GUID_DEVINTERFACE_COMPORT,
                NULL
            );
            break;
        }
        case FILE_DEVICE_MODEM:
        {
            status = WdfDeviceCreateDeviceInterface
            (
                device,
                (LPGUID)&GUID_DEVINTERFACE_MODEM,
                NULL
            );
            break;
        }
    }
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_DeviceConfig device interface create FAILED status: 0x%x, fdo device type: 0x%x\n", pDevContext->PortName, status, pDevContext->FdoDeviceType)
        );
        goto exit;
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_DeviceConfig device interface created\n", pDevContext->PortName)
    );

    // 6. Read registry settings
    status = QCPNP_PostVendorRegistryProcess(pDevContext);
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_DeviceConfig post vendor registry process FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    // 7. Set Stamp settings
    status = QCPNP_SetStamp(pDevContext, TRUE);
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_DeviceConfig set Stamp FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }
exit:
    if (key != NULL)
    {
        WdfRegistryClose(key);
        key = NULL;
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrintG
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("QCSER QCPNP_DeviceConfig FAILED status: 0x%x\n", status)
        );
    }
    return status;
}

/****************************************************************************
 *
 * function: QCPNP_EvtFileCreate
 *
 * purpose:  WDF callback for file create requests. Resets the bulk USB
 *           pipes, clears DTR/RTS, and signals the read thread to start.
 *
 * arguments:Device     = handle to the WDF device object.
 *           Request    = handle to the file create request.
 *           FileObject = handle to the file object being created.
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QCPNP_EvtFileCreate
(
    WDFDEVICE     Device,
    WDFREQUEST    Request,
    WDFFILEOBJECT FileObject
)
{
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(Device);
    NTSTATUS        status = STATUS_SUCCESS;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CIRP,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtFileCreate request: 0x%p\n", pDevContext->PortName, Request)
    );

#ifdef QCUSB_MUX_PROTOCOL
    pDevContext->QcStats.SessionTotal = 0;
    pDevContext->QcStats.ViCurrentAddress = -1;
    pDevContext->QcStats.ViCurrentDirection = 0;
    pDevContext->QcStats.ViCurrentDataSize = 0;
#endif

    // reset bulk in & bulk out pipes
    status = WdfDeviceStopIdle(pDevContext->Device, TRUE);     // unblocking call, return immediately
    if (status == STATUS_PENDING || status == STATUS_SUCCESS)   // otherwise, the device may fails
    {
        for (int i = 0; i < QCSER_RESET_RETRIES; i++)
        {
            status = QCPNP_ResetUsbPipe(pDevContext, pDevContext->BulkIN, 500);
            if (!NT_SUCCESS(status))
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CIRP,
                    QCSER_DBG_LEVEL_ERROR,
                    ("<%ws> QCPNP_EvtFileCreate reset bulk in pipe FAILED status: 0x%x\n", pDevContext->PortName, status)
                );
                if (i < QCSER_RESET_RETRIES - 1)    // should not wait if the last attempt failed
                {
                    QCMAIN_Wait(pDevContext, -(4 * 1000 * 1000));  // 0.4 sec
                }
            }
            else
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CIRP,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> QCPNP_EvtFileCreate reset bulk in pipe SUCCESSFUL status: 0x%x\n", pDevContext->PortName, status)
                );
                break;
            }
        }
        for (int i = 0; i < QCSER_RESET_RETRIES; i++)
        {
            status = QCPNP_ResetUsbPipe(pDevContext, pDevContext->BulkOUT, 500);
            if (!NT_SUCCESS(status))
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CIRP,
                    QCSER_DBG_LEVEL_ERROR,
                    ("<%ws> QCPNP_EvtFileCreate reset bulk out pipe FAILED status: 0x%x\n", pDevContext->PortName, status)
                );
                if (i < QCSER_RESET_RETRIES - 1)    // should not wait if the last attempt failed
                {
                    QCMAIN_Wait(pDevContext, -(4 * 1000 * 1000));  // 0.4 sec
                }
            }
            else
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CIRP,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> QCPNP_EvtFileCreate reset bulk out pipe SUCCESSFUL status: 0x%x\n", pDevContext->PortName, status)
                );
                break;
            }
        }
        WdfDeviceResumeIdle(pDevContext->Device);
    }

    if (NT_SUCCESS(status))
    {
        QCSER_SerialClrDtr(pDevContext);
        QCSER_SerialClrRts(pDevContext);

        // wake up read thread
        KeSetEvent(&pDevContext->FileCreateEvent, IO_NO_INCREMENT, FALSE);
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCPNP_EvtFileCreate completed status: 0x%x\n", pDevContext->PortName, status)
        );
    }
    else
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_EvtFileCreate FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    WdfRequestComplete(Request, status);
}

/****************************************************************************
 *
 * function: QCPNP_EvtFileClose
 *
 * purpose:  WDF callback for file close. Signals the read and write threads
 *           to flush pending I/O and waits for them to acknowledge closure.
 *
 * arguments:FileObject = handle to the file object being closed.
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QCPNP_EvtFileClose
(
    WDFFILEOBJECT FileObject
)
{
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(WdfFileObjectGetDevice(FileObject));
    LARGE_INTEGER fileCloseTimeout;
    fileCloseTimeout.QuadPart = -(10 * 1000 * QCPNP_FILE_CLOSE_TIMEOUT_MS); // 1000ms timeout in 100-nanosecond unit

    if (pDevContext->ReadRequestHandlerThread != NULL)
    {
        KeSetEvent(&pDevContext->FileCloseEventRead, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject
        (
            &pDevContext->ReadThreadFileCloseReadyEvent,
            Executive,
            KernelMode,
            FALSE,
            &fileCloseTimeout
        );
    }

    if (pDevContext->WriteRequestHandlerThread != NULL)
    {
        KeSetEvent(&pDevContext->FileCloseEventWrite, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject
        (
            &pDevContext->WriteThreadFileCloseReadyEvent,
            Executive,
            KernelMode,
            FALSE,
            &fileCloseTimeout
        );
    }

    QCSER_SerialClrDtr(pDevContext);
    QCSER_SerialClrRts(pDevContext);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CIRP,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtFileClose\n", pDevContext->PortName)
    );
}

/****************************************************************************
 *
 * function: QCPNP_EvtDriverCleanup
 *
 * purpose:  WDF cleanup callback for the driver object. Stops WPP tracing
 *           and frees the global service path buffer.
 *
 * arguments:Object = handle to the WDF driver object being cleaned up.
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QCPNP_EvtDriverCleanup
(
    WDFOBJECT Object
)
{
    PDRIVER_OBJECT driver = Object;

    QCSER_DbgPrintG
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("QCSER QCPNP_EvtDriverCleanup\n")
    );

#ifdef EVENT_TRACING
    WPP_CLEANUP(driver);
#endif

    if (gServicePath.Buffer != NULL)
    {
        ExFreePoolWithTag(gServicePath.Buffer, '6gaT');
        gServicePath.Buffer = NULL;
    }
}

/****************************************************************************
 *
 * function: QCPNP_EvtDeviceCleanup
 *
 * purpose:  WDF cleanup callback for the device object. Removes the
 *           SERIALCOMM registry entry and frees device context strings.
 *
 * arguments:Object = handle to the WDF device object being cleaned up.
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QCPNP_EvtDeviceCleanup
(
    WDFOBJECT Object
)
{
    NTSTATUS        status;
    WDFDEVICE       device = Object;
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(device);
    WDFKEY          key;
    UNICODE_STRING  deviceMapKey;
    UNICODE_STRING  serialcommValueName;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtDeviceCleanup\n", pDevContext->PortName)
    );

    RtlInitUnicodeString(&deviceMapKey, REG_DEVICE_MAP_SUBKEY);
    status = WdfDeviceOpenDevicemapKey
    (
        device,
        &deviceMapKey,
        KEY_READ | KEY_WRITE,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_EvtDeviceCleanup device map key open FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    if (pDevContext->SerialCommName != NULL)
    {
        RtlInitUnicodeString(&serialcommValueName, pDevContext->SerialCommName);
        status = WdfRegistryRemoveValue
        (
            key,
            &serialcommValueName
        );
        if (!NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> QCPNP_EvtDeviceCleanup device map key remove FAILED status: 0x%x\n", pDevContext->PortName, status)
            );
        }
    }

exit:
    if (pDevContext->SerialCommName != NULL)
    {
        ExFreePoolWithTag(pDevContext->SerialCommName, '8gaT');
        pDevContext->SerialCommName = NULL;
    }
    if (pDevContext->PortName != NULL)
    {
        ExFreePoolWithTag(pDevContext->PortName, '1gaT');
        pDevContext->PortName = NULL;
    }
    if (key != NULL)
    {
        WdfRegistryClose(key);
        key = NULL;
    }
} // Remove SERIALCOMM registry

/****************************************************************************
 *
 * function: QCPNP_EvtDevicePrepareHardware
 *
 * purpose:  WDF callback to prepare hardware. Creates the USB target device,
 *           selects the USB configuration, enumerates pipes, starts the
 *           interrupt and I/O threads, and enables selective suspend.
 *
 * arguments:Device              = handle to the WDF device object.
 *           ResourcesRaw        = handle to the raw hardware resource list.
 *           ResourcesTranslated = handle to the translated resource list.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_EvtDevicePrepareHardware
(
    WDFDEVICE    Device,
    WDFCMRESLIST ResourcesRaw,
    WDFCMRESLIST ResourcesTranslated
)
{
    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    NTSTATUS              status;
    PDEVICE_CONTEXT       pDevContext = QCDevGetContext(Device);
    USB_DEVICE_DESCRIPTOR usbDeviceDesc;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtDevicePrepareHardware\n", pDevContext->PortName)
    );

    status = WdfUsbTargetDeviceCreate
    (
        Device,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevContext->UsbDevice
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_EvtDevicePrepareHardware create USB target device FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    WdfUsbTargetDeviceGetDeviceDescriptor
    (
        pDevContext->UsbDevice,
        &usbDeviceDesc
    );

    // Start printing usb device info as debug message
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_EvtDevicePrepareHardware USB VID: 0x%08x, PID: 0x%08x\n", pDevContext->PortName, usbDeviceDesc.idVendor, usbDeviceDesc.idProduct)
    );
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_EvtDevicePrepareHardware USB NumofConfig: %u\n", pDevContext->PortName, usbDeviceDesc.bNumConfigurations)
    );
    USHORT strLen = 0;
    status = WdfUsbTargetDeviceQueryString
    (
        pDevContext->UsbDevice,
        NULL,
        NULL,
        NULL,
        &strLen,
        usbDeviceDesc.iProduct,
        0x0409
    );
    if (!NT_SUCCESS(status) || strlen == 0)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_EvtDevicePrepareHardware USB iProduct query FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }
    else
    {
        strLen = strLen + 1;    // NULL Terminator
        PWSTR buffer = (PWSTR)ExAllocatePoolZero(NonPagedPoolNx, strLen * sizeof(WCHAR), '2gaT');
        if (buffer == NULL)
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> QCPNP_EvtDevicePrepareHardware memory allocation FAILED\n", pDevContext->PortName)
            );
        }
        else
        {
            status = WdfUsbTargetDeviceQueryString
            (
                pDevContext->UsbDevice,
                NULL,
                NULL,
                buffer,
                &strLen,
                usbDeviceDesc.iProduct,
                0x0409
            );
            buffer[strLen] = L'\0';
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> QCPNP_EvtDevicePrepareHardware USB ProductInfo: %ws, status: 0x%x, strlen: %llu\n", pDevContext->PortName, buffer, status, strLen)
            );
            ExFreePoolWithTag(buffer, '2gaT');
        } // End printing usb device info as debug message
    }

    // Config usb device
    status = QCPNP_ConfigUsbDevice(Device);
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_EvtDevicePrepareHardware USB select config FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    // Update registry values
    status = QCMAIN_SetDriverRegistryStringW(VEN_DEV_PORT, pDevContext->PortName, pDevContext);
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            (QCSER_DBG_LEVEL_ERROR),
            ("<%ws> QCPNP_EvtDevicePrepareHardware: AssignedPortForQCDevice set FAILED: 0x%x\n", pDevContext->PortName, status)
        );
    }

    size_t      outputBufferLen = 1024;
    PVOID       outputBuffer;
    WDFREQUEST  request;
    WDFIOTARGET deviceIoTarget = WdfDeviceGetIoTarget(Device);
    WDFMEMORY   outputMemory;
    WDF_OBJECT_ATTRIBUTES requestAttr;
    WDF_OBJECT_ATTRIBUTES_INIT(&requestAttr);
    requestAttr.ParentObject = deviceIoTarget;

    status = WdfRequestCreate
    (
        &requestAttr,
        deviceIoTarget,
        &request
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_EvtDevicePrepareHardware request create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    status = WdfMemoryCreate
    (
        &requestAttr,
        NonPagedPoolNx,
        0,
        outputBufferLen,
        &outputMemory,
        &outputBuffer
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_EvtDevicePrepareHardware memory create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    RtlZeroMemory(outputBuffer, outputBufferLen);
    status = WdfIoTargetFormatRequestForIoctl
    (
        deviceIoTarget,
        request,
        IOCTL_QCDEV_GET_PARENT_DEV_NAME,
        NULL,
        NULL,
        outputMemory,
        NULL
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_EvtDevicePrepareHardware WdfIoTargetFormatRequestForIoctl FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    WdfRequestSetCompletionRoutine(request, QCPNP_GetParentDevNameCompletion, pDevContext);
    if (WdfRequestSend(request, deviceIoTarget, WDF_NO_SEND_OPTIONS) == FALSE)
    {
        status = WdfRequestGetStatus(request);
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_EvtDevicePrepareHardware WdfRequestSend FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        WdfObjectDelete(request);
    }

    status = QCPNP_GetStringDescriptor(Device, usbDeviceDesc.iProduct, 0x0409, TRUE);
    if ((!NT_SUCCESS(status)) && (usbDeviceDesc.iProduct != 2))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCPNP_EvtDevicePrepareHardware: _SERN: Failed with iProduct: 0x%x, try default\n",
            pDevContext->PortName, usbDeviceDesc.iProduct)
        );
        // workaround: try default iProduct value 0x02
        status = QCPNP_GetStringDescriptor(Device, 0x02, 0x0409, TRUE);
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_EvtDevicePrepareHardware: _SERN: tried iProduct: ST(0x%x)\n",
        pDevContext->PortName, status)
    );

    status = QCPNP_GetStringDescriptor(Device, usbDeviceDesc.iSerialNumber, 0x0409, FALSE);
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_ERROR,
        ("<%ws> QCPNP_EvtDevicePrepareHardware: _SERN: tried iSerialNumber: 0x%x ST(0x%x)\n",
        pDevContext->PortName, usbDeviceDesc.iSerialNumber, status)
    );

    USB_INTERFACE_DESCRIPTOR usbInterfaceDesc;
    WDFUSBINTERFACE usbInterface = WdfUsbTargetDeviceGetInterface(pDevContext->UsbDevice, 0);

    if (usbInterface == NULL)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_EvtDevicePrepareHardware WdfUsbTargetDeviceGetInterface FAILED\n", pDevContext->PortName)
        );
    }
    else
    {
        WdfUsbInterfaceGetDescriptor(usbInterface, 0, &usbInterfaceDesc);
        ULONG ifProtocol =
            usbInterfaceDesc.bInterfaceProtocol       |
            usbInterfaceDesc.bInterfaceClass    << 8  |
            usbInterfaceDesc.bAlternateSetting  << 16 |
            usbInterfaceDesc.bInterfaceNumber   << 24;
        pDevContext->InterfaceIndex = usbInterfaceDesc.bInterfaceNumber;
        status = QCMAIN_SetDriverRegistryDword(VEN_DEV_PROTOC, ifProtocol, pDevContext);

        QCPNP_SetFunctionProtocol(pDevContext, ifProtocol);

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCPNP_EvtDevicePrepareHardware set interface protocol: %lu, status: 0x%x\n", pDevContext->PortName, ifProtocol, status)
        );
    } // End update registry settings

    // Reset BULK pipes
    for (int i = 0; i < QCSER_RESET_RETRIES; i++)
    {
        status = QCPNP_ResetUsbPipe(pDevContext, pDevContext->BulkIN, 500);
        if (NT_SUCCESS(status))
        {
            status = QCPNP_ResetUsbPipe(pDevContext, pDevContext->BulkOUT, 500);
        }
        if (NT_SUCCESS(status))
        {
            break;
        }
        else
        {
            QCMAIN_Wait(pDevContext, -(4 * 1000 * 1000));  // 0.4 sec
        }
    }
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_EvtDevicePrepareHardware reset BULK pipes FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    //Retrieve service config
    QCPNP_RetrieveServiceConfig(pDevContext);

    // Bring up Interrupt Listener
    status = QCINT_InitInterruptPipe(pDevContext);
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_EvtDevicePrepareHardware interruptPipe thread setup FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    // Create io handler threads
    status = QCPNP_SetupIoThreadsAndQueues(pDevContext);
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_EvtDevicePrepareHardware io threads setup FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    // Setup usb selective suspend
    status = QCPNP_EnableSelectiveSuspend(Device);
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_POWER,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_EvtDevicePrepareHardware USB selective suspend setup FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

#ifdef QCUSB_MUX_PROTOCOL
    if (pDevContext->DeviceFunction == QCUSB_DEV_FUNC_LPC)
    {
        KeSetEvent(&pDevContext->IntThreadLpcEvent, IO_NO_INCREMENT, FALSE);
    }
    else
#endif
    {
        // Register WMI Power Guid
        status = QCPNP_RegisterWmiPowerGuid(pDevContext);
    }

exit:
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_EvtDevicePrepareHardware completed with status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    return status;
}

/****************************************************************************
 *
 * function: QCPNP_EvtDeviceReleaseHardware
 *
 * purpose:  WDF callback to release hardware. Stops the interrupt service
 *           and I/O threads, cleans up read URBs and ring buffer, and
 *           clears the function protocol registry entry.
 *
 * arguments:Device              = handle to the WDF device object.
 *           ResourcesTranslated = handle to the translated resource list.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_EvtDeviceReleaseHardware
(
    WDFDEVICE    Device,
    WDFCMRESLIST ResourcesTranslated
)
{
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(Device);
    NTSTATUS status = STATUS_SUCCESS;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtDeviceReleaseHardware\n", pDevContext->PortName)
    );

    // cleanup wait_on requests
    QCPNP_NotifyDeviceRemoval(pDevContext);
    QCSER_CompleteWomRequest(pDevContext, STATUS_CANCELLED, 0);

    StopInterruptService(pDevContext, TRUE);
    // signal the I/O threads to exit
    KeSetEvent(&pDevContext->DeviceRemoveEvent, IO_NO_INCREMENT, FALSE);

    // wait until all io threads exit;
    if (pDevContext->ReadRequestHandlerThread != NULL)
    {
        KeWaitForSingleObject
        (
            pDevContext->ReadRequestHandlerThread,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        ObDereferenceObject(pDevContext->ReadRequestHandlerThread);
        pDevContext->ReadRequestHandlerThread = NULL;
    }
    if (pDevContext->WriteRequestHandlerThread != NULL)
    {
        KeWaitForSingleObject
        (
            pDevContext->WriteRequestHandlerThread,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        ObDereferenceObject(pDevContext->WriteRequestHandlerThread);
        pDevContext->WriteRequestHandlerThread = NULL;
    }

    // cleanup read urbs
    if (pDevContext->UrbReadListCapacity > 0)
    {
        QCRD_CleanupReadQueues(pDevContext);
    }

    // delete ring buffer for rx
    QCUTIL_RingBufferDelete(&pDevContext->ReadRingBuffer);

    QCPNP_SetFunctionProtocol(pDevContext, 0);
    QCPNP_SetStamp(pDevContext, FALSE);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtDeviceReleaseHardware completed status: 0x%x\n", pDevContext->PortName, status)
    );

    return status;
}

/****************************************************************************
 *
 * function: QCPNP_ConfigUsbDevice
 *
 * purpose:  Retrieves the USB configuration descriptor, selects the device
 *           configuration, and identifies the bulk IN, bulk OUT, and
 *           interrupt IN pipe handles.
 *
 * arguments:Device = handle to the WDF device object.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_ConfigUsbDevice(WDFDEVICE Device)
{
    PDEVICE_CONTEXT       pDevContext = QCDevGetContext(Device);
    WDF_OBJECT_ATTRIBUTES memoryAttr;
    WDFMEMORY             buffer;
    NTSTATUS              status;
    USHORT                bufferLen = 0;
    PUSB_CONFIGURATION_DESCRIPTOR usbConfigDescriptor = NULL;
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_ConfigUsbDevice\n", pDevContext->PortName)
    );

    status = WdfUsbTargetDeviceRetrieveConfigDescriptor
    (
        pDevContext->UsbDevice,
        NULL,
        &bufferLen
    );
    if (status != STATUS_BUFFER_TOO_SMALL)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_UsbDeviceConfig retrive usb config descriptor length FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&memoryAttr);
    memoryAttr.ParentObject = pDevContext->UsbDevice;
    status = WdfMemoryCreate
    (
        &memoryAttr,
        NonPagedPoolNx,
        0,
        bufferLen,
        &buffer,
        &usbConfigDescriptor
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_UsbDeviceConfig memory allocation FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    status = WdfUsbTargetDeviceRetrieveConfigDescriptor
    (
        pDevContext->UsbDevice,
        usbConfigDescriptor,
        &bufferLen
    );
    if (!NT_SUCCESS(status) || usbConfigDescriptor->bNumInterfaces == 0)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_UsbDeviceConfig retrive usb config descriptor object FAILED status: 0x%x, numInterface: %u\n", pDevContext->PortName, status, usbConfigDescriptor->bNumInterfaces)
        );
        goto exit;
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_UsbDeviceConfig USB configuration bLength: %u, bConfigurationValue: %u, bDescriptorType: %u, bNumInterfaces: %u, MaxPower: %u\n", pDevContext->PortName,
        usbConfigDescriptor->bLength, usbConfigDescriptor->bConfigurationValue, usbConfigDescriptor->bDescriptorType, usbConfigDescriptor->bNumInterfaces, usbConfigDescriptor->MaxPower)
    );

    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES(&configParams, 0, NULL);
    status = WdfUsbTargetDeviceSelectConfig
    (
        pDevContext->UsbDevice,
        WDF_NO_OBJECT_ATTRIBUTES,
        &configParams
    );
    if (NT_SUCCESS(status))
    {
        USHORT numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(pDevContext->UsbDevice);
        if (numInterfaces != 1) // assume there is only 1 interface
        {
            status = STATUS_UNSUCCESSFUL;
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> QCPNP_UsbDeviceConfig current config has %u interfaces\n", pDevContext->PortName, numInterfaces)
            );
            if (numInterfaces == 0)
            {
                goto exit;
            }
        }

        WDFUSBINTERFACE usbInterface = WdfUsbTargetDeviceGetInterface(pDevContext->UsbDevice, 0);
        if (usbInterface == NULL)
        {
            status = STATUS_UNSUCCESSFUL;
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_CRITICAL,
                ("<%ws> QCPNP_UsbDeviceConfig get interface FAILED index: %u, status: 0x%x\n", pDevContext->PortName, 0, status)
            );
            goto exit;
        }

        // verify endpoint type
        BYTE numPipes = WdfUsbInterfaceGetNumConfiguredPipes(usbInterface);
        WDF_USB_PIPE_INFORMATION pipeInfo;
        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
        for (UCHAR pipeIndex = 0; pipeIndex < numPipes; pipeIndex++)
        {
            WDFUSBPIPE pipe = WdfUsbInterfaceGetConfiguredPipe(
                usbInterface,
                pipeIndex,
                &pipeInfo);
            if (pipe == NULL)
            {
                status = STATUS_UNSUCCESSFUL;
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CONTROL,
                    QCSER_DBG_LEVEL_CRITICAL,
                    ("<%ws> QCPNP_UsbDeviceConfig pipe validation FAILED pipe index: %u, address: %p\n", pDevContext->PortName, pipeIndex, pipe)
                );
                goto exit;
            }

            switch (pipeInfo.PipeType)
            {
                case WdfUsbPipeTypeBulk:
                {
                    if (WdfUsbTargetPipeIsOutEndpoint(pipe))
                    {
                        pDevContext->BulkOUT = pipe;
                        pDevContext->wMaxPktSize = pipeInfo.MaximumPacketSize;
                        pDevContext->MaxBulkPacketSize = pipeInfo.MaximumPacketSize;
                        WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pDevContext->BulkOUT);
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_CONTROL,
                            QCSER_DBG_LEVEL_DETAIL,
                            ("<%ws> Found BulkOut EndPoint max packet size: %llu\n", pDevContext->PortName, pipeInfo.MaximumPacketSize)
                        );
                    }
                    else
                    {
                        pDevContext->BulkIN = pipe;
                        WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pDevContext->BulkIN);
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_CONTROL,
                            QCSER_DBG_LEVEL_DETAIL,
                            ("<%ws> Found BulkIn EndPoint max packet size: %llu\n", pDevContext->PortName, pipeInfo.MaximumPacketSize)
                        );
                    }
                    break;
                }
                case WdfUsbPipeTypeInterrupt:
                {
                    pDevContext->InterruptInPipe = pipe;
                    pDevContext->MaxIntPacketSize = pipeInfo.MaximumPacketSize;
                    WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pDevContext->InterruptInPipe);
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_CONTROL,
                        QCSER_DBG_LEVEL_DETAIL,
                        ("<%ws> Found Interrupt EndPoint max packet size: %llu\n", pDevContext->PortName, pipeInfo.MaximumPacketSize)
                    );
                    break;
                }
            }
        }

        if (pDevContext->BulkIN != NULL && pDevContext->BulkOUT != NULL)
        {
            if (pDevContext->InterruptInPipe != NULL)
            {
                pDevContext->UsbDeviceType = DEVICETYPE_CDC;
            }
            else
            {
                pDevContext->UsbDeviceType = DEVICETYPE_SERIAL;
            }
        }
        else if (pDevContext->BulkIN == NULL && pDevContext->BulkOUT == NULL && pDevContext->InterruptInPipe == NULL)
        {
            pDevContext->UsbDeviceType = DEVICETYPE_CTRL;
        }
        else
        {
            pDevContext->UsbDeviceType = DEVICETYPE_INVALID;
        }
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCPNP_UsbDeviceConfig numPipes: %u, devType: 0x%x\n", pDevContext->PortName, numPipes, pDevContext->UsbDeviceType)
        );
    }
    else
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_UsbDeviceConfig select usb config FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

exit:
    return status;
}

/****************************************************************************
 *
 * function: QCPNP_EnableSelectiveSuspend
 *
 * purpose:  Configures USB selective suspend idle settings based on the
 *           registry-specified idle timeout value.
 *
 * arguments:Device = handle to the WDF device object.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_EnableSelectiveSuspend
(
    WDFDEVICE Device
)
{
    NTSTATUS        status = STATUS_SUCCESS;
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(Device);
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idleSettings;
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idleSettings, IdleUsbSelectiveSuspend); // support both idle and self wakeup

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_POWER,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EnableSelectiveSuspend\n", pDevContext->PortName)
    );

#ifdef QCUSB_MUX_PROTOCOL
    if (pDevContext->DeviceFunction == QCUSB_DEV_FUNC_LPC)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_POWER,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCPNP_EnableSelectiveSuspend LPC deivce found, selective suspend disabled\n", pDevContext->PortName)
        );

        // prevent sending further control packets
        idleSettings.Enabled = WdfFalse;
        idleSettings.IdleCaps = IdleCannotWakeFromS0;
        status = WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
    }
    else
#endif
    {
        idleSettings.UserControlOfIdleSettings = IdleAllowUserControl;
        idleSettings.IdleTimeout = pDevContext->SelectiveSuspendIdleTime;
#ifdef POFX_SUPPORT
        idleSettings.IdleTimeoutType = SystemManagedIdleTimeoutWithHint;
#endif
        if (idleSettings.IdleTimeout == 0)
        {
            // registry not set, ss is disabled
            idleSettings.Enabled = WdfFalse;
            idleSettings.IdleCaps = IdleCannotWakeFromS0;
            status = WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
        }
        else
        {
            if (pDevContext->SelectiveSuspendInMiliSeconds == FALSE)
            {
                idleSettings.IdleTimeout *= 1000;
            }
            status = WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
            if (status == STATUS_POWER_STATE_INVALID)
            {
                // bus driver reports the device cannot wakeup itself
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_POWER,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> QCPNP_EnableSelectiveSuspend device reported cannot wake from idle\n", pDevContext->PortName)
                );
                idleSettings.IdleCaps = IdleCannotWakeFromS0;
                status = WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
            }
        }
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_POWER,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCPNP_EnableSelectiveSuspend timeout: %lu, status: 0x%x\n", pDevContext->PortName, idleSettings.IdleTimeout, status)
        );
    }

    return status;
}

/****************************************************************************
 *
 * function: QCPNP_DisableSelectiveSuspend
 *
 * purpose:  Disables USB selective suspend by assigning idle settings that
 *           prevent the device from entering a low-power idle state.
 *
 * arguments:Device = handle to the WDF device object.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_DisableSelectiveSuspend
(
    WDFDEVICE Device
)
{
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(Device);
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idleSettings;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_POWER,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_DisableSelectiveSuspend\n", pDevContext->PortName)
    );

    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idleSettings, IdleCannotWakeFromS0);
    idleSettings.Enabled = WdfFalse;
    return WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
}

/****************************************************************************
 *
 * function: QCPNP_EvtDeviceD0Entry
 *
 * purpose:  WDF power callback invoked when the device enters the D0 (fully
 *           on) state. Signals the read and interrupt threads to resume I/O.
 *
 * arguments:Device        = handle to the WDF device object.
 *           PreviousState = the power state the device is transitioning from.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_EvtDeviceD0Entry
(
    WDFDEVICE              Device,
    WDF_POWER_DEVICE_STATE PreviousState
)
{
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(Device);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_POWER,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtDeviceD0Entry prev state: %d\n", pDevContext->PortName, (int)PreviousState)
    );

    if (pDevContext->ReadRequestHandlerThread != NULL)
    {
        KeClearEvent(&pDevContext->ReadThreadD0ExitEvent);
        KeSetEvent(&pDevContext->ReadThreadD0EntryEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject
        (
            &pDevContext->ReadThreadD0EntryReadyEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        KeClearEvent(&pDevContext->ReadThreadD0EntryReadyEvent);
    }
    if (pDevContext->interruptThread != NULL)
    {
        KeClearEvent(&pDevContext->IntThreadD0ExitEvent);
        KeSetEvent(&pDevContext->IntThreadD0EntryEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject
        (
            &pDevContext->IntThreadD0EntryReadyEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        KeClearEvent(&pDevContext->IntThreadD0EntryReadyEvent);
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_POWER,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtDeviceD0Entry Completed!\n", pDevContext->PortName)
    );
    return STATUS_SUCCESS;
}

/****************************************************************************
 *
 * function: QCPNP_EvtDeviceD0Exit
 *
 * purpose:  WDF power callback invoked when the device leaves the D0 state.
 *           Signals the read and interrupt threads to suspend I/O and waits
 *           for acknowledgment.
 *
 * arguments:Device      = handle to the WDF device object.
 *           TargetState = the power state the device is transitioning to.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_EvtDeviceD0Exit
(
    WDFDEVICE              Device,
    WDF_POWER_DEVICE_STATE TargetState
)
{
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(Device);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_POWER,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtDeviceD0Exit target state: %d\n", pDevContext->PortName, (int)TargetState)
    );

    if (pDevContext->ReadRequestHandlerThread != NULL)
    {
        KeClearEvent(&pDevContext->ReadThreadD0EntryEvent);
        KeSetEvent(&pDevContext->ReadThreadD0ExitEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject
        (
            &pDevContext->ReadThreadD0ExitReadyEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        KeClearEvent(&pDevContext->ReadThreadD0ExitReadyEvent);
    }
    if (pDevContext->interruptThread != NULL)
    {
        KeClearEvent(&pDevContext->IntThreadD0EntryEvent);
        KeSetEvent(&pDevContext->IntThreadD0ExitEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject
        (
            &pDevContext->IntThreadD0ExitReadyEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        KeClearEvent(&pDevContext->IntThreadD0ExitReadyEvent);
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_POWER,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtDeviceD0Exit Completed!\n", pDevContext->PortName)
    );
    return STATUS_SUCCESS;
}

/****************************************************************************
 *
 * function: QCPNP_EvtDeviceQueryRemoval
 *
 * purpose:  WDF callback to query whether the device can be removed.
 *           Notifies pending device-removal requests before removal proceeds.
 *
 * arguments:Device = handle to the WDF device object.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_EvtDeviceQueryRemoval
(
    WDFDEVICE Device
)
{
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(Device);
    NTSTATUS status = STATUS_SUCCESS;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_POWER,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_EvtDeviceQueryRemoval\n", pDevContext->PortName)
    );

    QCPNP_NotifyDeviceRemoval(pDevContext);

    return status;
}

/****************************************************************************
 *
 * function: QCPNP_GetParentDevNameCompletion
 *
 * purpose:  WDF completion routine for the IOCTL_QCDEV_GET_PARENT_DEV_NAME
 *           request. Stores the parent device name in the driver registry.
 *
 * arguments:Request = handle to the completed I/O request.
 *           Target  = handle to the I/O target.
 *           Params  = pointer to the request completion parameters.
 *           Context = pointer to the device context.
 *
 * returns:  VOID
 *
 ****************************************************************************/
void QCPNP_GetParentDevNameCompletion
(
    WDFREQUEST  Request,
    WDFIOTARGET Target,
    PWDF_REQUEST_COMPLETION_PARAMS Params,
    WDFCONTEXT  Context
)
{
    UNREFERENCED_PARAMETER(Target);
    NTSTATUS status;
    PDEVICE_CONTEXT pDevContext = Context;
    PWCHAR pParentDevName = (PWCHAR)WdfMemoryGetBuffer(Params->Parameters.Ioctl.Output.Buffer, NULL);
    size_t parentDevNamelen = Params->Parameters.Ioctl.Output.Length;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_GetParentDevNameCompletion parentDevName: 0x%p, len: %llu\n", pDevContext->PortName, pParentDevName, parentDevNamelen)
    );

    if (pParentDevName && parentDevNamelen > 0)
    {
        status = QCMAIN_SetDriverRegistryStringW(VEN_DEV_PARENT, pParentDevName, pDevContext);
    }
    else
    {
        status = STATUS_INVALID_PARAMETER;
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_GetParentDevNameCompletion completed with status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    WdfObjectDelete(Request);
}

#define QCDEV_NAME_LEN_MAX 1024

/****************************************************************************
 *
 * function: QCPNP_ReportDeviceName
 *
 * purpose:  Queries the device friendly or description name and, if it
 *           matches the diagnostics class, reports it to the parent driver
 *           via IOCTL_QCUSB_REPORT_DEV_NAME.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_ReportDeviceName(PDEVICE_CONTEXT pDevContext)
{
    NTSTATUS          ntStatus;
    CHAR              devName[QCDEV_NAME_LEN_MAX], tmpName[QCDEV_NAME_LEN_MAX];
    ULONG             bufLen = QCDEV_NAME_LEN_MAX, resultLen = 0;
    PCHAR             matchText = "D i a g n o s t i c s ";
    WDFDEVICE         device;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> -->ReportDeviceName PDO 0x%p\n", pDevContext->PortName, pDevContext->Device)
    );

    device = pDevContext->Device;

    RtlZeroMemory(devName, QCDEV_NAME_LEN_MAX);
    RtlZeroMemory(tmpName, QCDEV_NAME_LEN_MAX);


    ntStatus = WdfDeviceQueryProperty
    (
        device,
        DevicePropertyFriendlyName,
        bufLen,
        (PVOID)devName,
        &resultLen
    );

    if (ntStatus != STATUS_SUCCESS)
    {
        ntStatus = WdfDeviceQueryProperty
        (
            device,
            DevicePropertyDeviceDescription,
            bufLen,
            (PVOID)devName,
            &resultLen
        );
    }

    if (ntStatus != STATUS_SUCCESS)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> <--ReportDeviceName failure 0x%X\n", pDevContext->PortName, ntStatus)
        );
        return ntStatus;
    }
    else
    {
        PTSTR matchPtr;
        int i;

        RtlCopyMemory(tmpName, devName, resultLen);

        for (i = 0; i < (int)resultLen; i++)
        {
            if (tmpName[i] == 0)
            {
                tmpName[i] = ' ';
            }
        }
        tmpName[i] = 0;

        matchPtr = strstr(tmpName, matchText);

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> ReportDeviceName <%s> match 0x%p\n", pDevContext->PortName, tmpName, matchPtr)
        );

        if (matchPtr != NULL)
        {
            ntStatus = QCPNP_RegisterDevName
            (
                pDevContext,
                IOCTL_QCUSB_REPORT_DEV_NAME,
                devName,
                resultLen
            );
        }
    }

    return ntStatus;

}  // QCPNP_ReportDeviceName

/****************************************************************************
 *
 * function: QCPNP_RegisterDevName
 *
 * purpose:  Sends an IOCTL to the parent driver to register the device name
 *           and symbolic link name as a peer device entry.
 *
 * arguments:pDevContext    = pointer to the device context.
 *           IoControlCode  = IOCTL code to send to the parent driver.
 *           Buffer         = pointer to the device name buffer.
 *           BufferLength   = length of the device name buffer in bytes.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_RegisterDevName
(
    PDEVICE_CONTEXT pDevContext,
    ULONG       IoControlCode,
    PVOID       Buffer,
    ULONG       BufferLength
)
{

    NTSTATUS Status;
    PPEER_DEV_INFO_HDR pDevInfoHdr;
    PCHAR pLocation;
    ULONG totalLength;

    PVOID       inputBuffer;
    WDFREQUEST  request;
    WDFIOTARGET deviceIoTarget = WdfDeviceGetIoTarget(pDevContext->Device);
    WDFMEMORY   inputMemory;
    WDF_OBJECT_ATTRIBUTES requestAttr;
    WDF_OBJECT_ATTRIBUTES_INIT(&requestAttr);
    requestAttr.ParentObject = deviceIoTarget;

    Status = WdfRequestCreate
    (
        &requestAttr,
        deviceIoTarget,
        &request
    );
    if (!NT_SUCCESS(Status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_RegisterDevName request create FAILED status: 0x%x\n", pDevContext->PortName, Status)
        );
        return Status;
    }

#pragma warning(suppress: 6305)
    totalLength = sizeof(PEER_DEV_INFO_HDR) + BufferLength + wcslen(pDevContext->PortName);

    requestAttr.ParentObject = request;
    Status = WdfMemoryCreate
    (
        &requestAttr,
        NonPagedPoolNx,
        0,
        totalLength,
        &inputMemory,
        &inputBuffer
    );
    if (!NT_SUCCESS(Status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_RegisterDevName memory create FAILED status: 0x%x\n", pDevContext->PortName, Status)
        );
        WdfObjectDelete(request);
        return Status;
    }

    RtlZeroMemory(inputBuffer, totalLength);

    pDevInfoHdr = (PPEER_DEV_INFO_HDR)(inputBuffer);
    pDevInfoHdr->Version = 1;
    pDevInfoHdr->DeviceNameLength = (USHORT)BufferLength;
    pDevInfoHdr->SymLinkNameLength = (USHORT)wcslen(pDevContext->PortName);

    // device name
    pLocation = (PCHAR)(inputBuffer);
    pLocation += sizeof(PEER_DEV_INFO_HDR);
    RtlCopyMemory(pLocation, Buffer, BufferLength);

    // symbolic name
    pLocation += BufferLength;
    RtlCopyMemory(pLocation, pDevContext->PortName, pDevInfoHdr->SymLinkNameLength);

    Status = WdfIoTargetFormatRequestForIoctl
    (
        deviceIoTarget,
        request,
        IoControlCode,
        inputMemory,
        NULL,
        NULL,
        NULL
    );
    if (!NT_SUCCESS(Status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_RegisterDevName WdfIoTargetFormatRequestForIoctl FAILED status: 0x%x\n", pDevContext->PortName, Status)
        );
        WdfObjectDelete(inputMemory);
        WdfObjectDelete(request);
        return Status;
    }

    WdfRequestSetCompletionRoutine(request, QCPNP_RegisterDevNameCompletion, pDevContext);
    if (WdfRequestSend(request, deviceIoTarget, WDF_NO_SEND_OPTIONS) == FALSE)
    {
        Status = WdfRequestGetStatus(request);
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCPNP_RegisterDevName WdfRequestSend FAILED status: 0x%x\n", pDevContext->PortName, Status)
        );
        WdfObjectDelete(inputMemory);
        WdfObjectDelete(request);
    }

    return Status;

} // QCPNP_RegisterDevName

/****************************************************************************
 *
 * function: QCPNP_RegisterDevNameCompletion
 *
 * purpose:  WDF completion routine for the register device name IOCTL.
 *           Deletes the request object upon completion.
 *
 * arguments:Request = handle to the completed I/O request.
 *           Target  = handle to the I/O target.
 *           Params  = pointer to the request completion parameters.
 *           Context = pointer to the device context.
 *
 * returns:  VOID
 *
 ****************************************************************************/
void QCPNP_RegisterDevNameCompletion
(
    WDFREQUEST  Request,
    WDFIOTARGET Target,
    PWDF_REQUEST_COMPLETION_PARAMS Params,
    WDFCONTEXT  Context
)
{
    UNREFERENCED_PARAMETER(Target);

    PDEVICE_CONTEXT pDevContext = (PDEVICE_CONTEXT)Context;
    NTSTATUS status = Params->IoStatus.Status;
    WdfObjectDelete(Request);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_RegisterDevNameCompletion status: 0x%x\n", pDevContext->PortName, status)
    );

}  // QCPNP_RegisterDevNameCompletion


/****************************************************************************
 *
 * function: QCPNP_VendorRegistryProcess
 *
 * purpose:  Reads vendor-specific configuration values from the driver
 *           registry key and populates the global gVendorConfig structure.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_VendorRegistryProcess(PDEVICE_CONTEXT pDevContext)
{
    NTSTATUS       status = STATUS_SUCCESS;
    WDFDEVICE       device = pDevContext->Device;
    WDFKEY         key;
    UNICODE_STRING ucValueName;
    ULONG          ulWriteUnit = 0;
    ULONG          gDriverConfigParam = 0;
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_VendorRegistryProcess\n", pDevContext->PortName)
    );
    // Init default configuration
    gVendorConfig.ContinueOnOverflow  = FALSE;
    gVendorConfig.ContinueOnOverflow  = FALSE;
    gVendorConfig.ContinueOnDataError = FALSE;
    gVendorConfig.Use128ByteInPkt     = FALSE;
    gVendorConfig.Use256ByteInPkt     = FALSE;
    gVendorConfig.Use512ByteInPkt     = FALSE;
    gVendorConfig.Use1024ByteInPkt    = FALSE;
    gVendorConfig.Use2048ByteInPkt    = FALSE;
    gVendorConfig.Use1kByteOutPkt     = FALSE;
    gVendorConfig.Use2kByteOutPkt     = FALSE;
    gVendorConfig.Use4kByteOutPkt     = FALSE;
    gVendorConfig.Use128KReadBuffer   = FALSE;
    gVendorConfig.Use256KReadBuffer   = FALSE;
    gVendorConfig.NoTimeoutOnCtlReq   = FALSE;
    gVendorConfig.RetryOnTxError      = FALSE;
    gVendorConfig.UseReadArray        = TRUE;
    gVendorConfig.UseMultiWrites      = TRUE;
    gVendorConfig.EnableLogging       = FALSE;
    gVendorConfig.MinInPktSize        = 64;
#ifdef QCUSB_MUX_PROTOCOL
    gVendorConfig.MaxPipeXferSize = 1024 * QCUSB_IO_MULTIPLE;
#else
    gVendorConfig.MaxPipeXferSize = QCSER_RECEIVE_BUFFER_SIZE * 8;
#endif
    gVendorConfig.WriteUnitSize       = USB_WRITE_UNIT_SIZE;
    gVendorConfig.InternalReadBufSize = USB_INTERNAL_READ_BUFFER_SIZE;
    gVendorConfig.LoggingWriteThrough = FALSE;
    gVendorConfig.NumOfRetriesOnError = BEST_RETRIES;
    gVendorConfig.LogLatestPkts       = FALSE;
    gVendorConfig.UrbReadErrorMaxLimit   = 2000;
    gVendorConfig.UrbReadErrorThreshold  = 6;
    gVendorConfig.EnableSerialTimeout    = TRUE;
    gVendorConfig.EnableZeroLengthPacket = TRUE;

    // Update driver version in the registry
    status = WdfDeviceOpenRegistryKey
    (
        device,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_QUERY_VALUE,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_VendorRegistryProcess device regkey open FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }
    DECLARE_UNICODE_STRING_SIZE(ucDriverVersion, 20);
    RtlInitUnicodeString(&ucValueName, VEN_DEV_VER);
    status = QCMAIN_GetDriverRegistryStringW(key, &ucValueName, &ucDriverVersion, pDevContext);
    if (status == STATUS_SUCCESS)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCPNP_VendorRegistryProcess <DriverVersion>:<%ws>\n", pDevContext->PortName, ucDriverVersion.Buffer)
        );

        ANSI_STRING ansiStr;
        RtlUnicodeStringToAnsiString(&ansiStr, &ucDriverVersion, TRUE);
        if (ansiStr.Buffer != NULL)
        {
            strncpy_s(gVendorConfig.DriverVersion, 16, ansiStr.Buffer, 15);
            RtlFreeAnsiString(&ansiStr);
        }
    }

    // Get usb zero length packet compatibility
    ULONG enableZeroLengthPacket = 0;
    RtlInitUnicodeString(&ucValueName, VEN_DEV_ZLP_ON);
    status = QCMAIN_GetDriverRegistryDword
    (
        key,
        &ucValueName,
        &enableZeroLengthPacket,
        pDevContext
    );
    if (status != STATUS_SUCCESS)
    {
        gVendorConfig.EnableZeroLengthPacket = TRUE;
    }
    else if (enableZeroLengthPacket > 0)
    {
        gVendorConfig.EnableZeroLengthPacket = TRUE;
    }
    else
    {
        gVendorConfig.EnableZeroLengthPacket = FALSE;
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_VendorRegistryProcess <QCDeviceZLPEnabled>: <%ld>\n", pDevContext->PortName, enableZeroLengthPacket)
    );

    // Get number of retries on error
    RtlInitUnicodeString(&ucValueName, VEN_DEV_RTY_NUM);
    status = QCMAIN_GetDriverRegistryDword
    (
        key,
        &ucValueName,
        &gVendorConfig.NumOfRetriesOnError,
        pDevContext
    );
    if (status != STATUS_SUCCESS)
    {
        gVendorConfig.NumOfRetriesOnError = BEST_RETRIES;
    }
    else
    {
        if (gVendorConfig.NumOfRetriesOnError < BEST_RETRIES_MIN)
        {
            gVendorConfig.NumOfRetriesOnError = BEST_RETRIES_MIN;
        }
        else if (gVendorConfig.NumOfRetriesOnError > BEST_RETRIES_MAX)
        {
            gVendorConfig.NumOfRetriesOnError = BEST_RETRIES_MAX;
        }
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_VendorRegistryProcess <QCDriverRetriesOnError>: <%ld>\n", pDevContext->PortName, gVendorConfig.NumOfRetriesOnError)
    );

    // Get Max pipe transfer size
    RtlInitUnicodeString(&ucValueName, VEN_DEV_MAX_XFR);
    status = QCMAIN_GetDriverRegistryDword
    (
        key,
        &ucValueName,
        &gVendorConfig.MaxPipeXferSize,
        pDevContext
    );

    if (status != STATUS_SUCCESS)
    {
#ifdef QCUSB_MUX_PROTOCOL
        gVendorConfig.MaxPipeXferSize = 1024 * QCUSB_IO_MULTIPLE;
#else
        gVendorConfig.MaxPipeXferSize = QCSER_RECEIVE_BUFFER_SIZE * 8;
#endif
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_VendorRegistryProcess <QCDriverMaxPipeXferSize>: <%ld>\n", pDevContext->PortName, gVendorConfig.MaxPipeXferSize)
    );

    // Get number of L2 buffers
    RtlInitUnicodeString(&ucValueName, VEN_DEV_L2_BUFS);
    status = QCMAIN_GetDriverRegistryDword
    (
        key,
        &ucValueName,
        &gVendorConfig.NumberOfL2Buffers,
        pDevContext
    );

    if (status != STATUS_SUCCESS)
    {
        gVendorConfig.NumberOfL2Buffers = QCSER_NUM_OF_LEVEL2_BUF;
    }
    else
    {
        if (gVendorConfig.NumberOfL2Buffers < 2)
        {
            gVendorConfig.NumberOfL2Buffers = 2;
        }
        else if (gVendorConfig.NumberOfL2Buffers > QCUSB_MAX_MRW_BUF_COUNT)
        {
            gVendorConfig.NumberOfL2Buffers = QCUSB_MAX_MRW_BUF_COUNT;
        }
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_VendorRegistryProcess <QCDriverL2Buffers>: <%ld>\n", pDevContext->PortName, gVendorConfig.NumberOfL2Buffers)
    );

    // Get Debug Level
    RtlInitUnicodeString(&ucValueName, VEN_DBG_MASK);
    status = QCMAIN_GetDriverRegistryDword
    (
        key,
        &ucValueName,
        &gVendorConfig.DebugMask,
        pDevContext
    );

    if (status != STATUS_SUCCESS)
    {
        gVendorConfig.DebugMask = QCSER_DBG_LEVEL_FORCE;
    }
#ifdef DEBUG_MSGS
    gVendorConfig.DebugMask = 0xFFFFFFFF;
#endif
    gVendorConfig.DebugLevel = (UCHAR)(gVendorConfig.DebugMask & 0x0F);
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_VendorRegistryProcess <QCDriverDebugMask>: <0x%x>\n", pDevContext->PortName, gVendorConfig.DebugMask)
    );

    // Get driver write unit
    RtlInitUnicodeString(&ucValueName, VEN_DRV_WRITE_UNIT);
    status = QCMAIN_GetDriverRegistryDword
    (
        key,
        &ucValueName,
        &ulWriteUnit,
        pDevContext
    );

    if (status != STATUS_SUCCESS)
    {
        ulWriteUnit = 0;
    }

#ifdef QCUSB_MUX_PROTOCOL
    gVendorConfig.MaxPipeXferSize = gVendorConfig.MaxPipeXferSize / 1024 * 1024;
    if (gVendorConfig.MaxPipeXferSize > QCUSB_IO_MULTIPLE * 1024)
    {
        gVendorConfig.MaxPipeXferSize = QCUSB_IO_MULTIPLE * 1024;
    }
#else
    gVendorConfig.MaxPipeXferSize = gVendorConfig.MaxPipeXferSize / 64 * 64;
    if (gVendorConfig.MaxPipeXferSize > 4096 * 8)
    {
        gVendorConfig.MaxPipeXferSize = 4096 * 8;
    }
#endif
    if (gVendorConfig.MaxPipeXferSize < 1024)
    {
        gVendorConfig.MaxPipeXferSize = 1024;
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_VendorRegistryProcess <QCDriverWriteUnit>: <%ld>\n", pDevContext->PortName, ulWriteUnit)
    );

    // Get config parameter
    RtlInitUnicodeString(&ucValueName, VEN_DEV_CONFIG);
    status = QCMAIN_GetDriverRegistryDword
    (
        key,
        &ucValueName,
        &gDriverConfigParam,
        pDevContext
    );

    if (status != STATUS_SUCCESS)
    {
        gDriverConfigParam = 0;
    }
    else
    {
        if (gDriverConfigParam & QCSER_CONTINUE_ON_OVERFLOW)
        {
            gVendorConfig.ContinueOnOverflow = TRUE;
        }
        if (gDriverConfigParam & QCSER_CONTINUE_ON_DATA_ERR)
        {
            gVendorConfig.ContinueOnDataError = TRUE;
        }
        if (gDriverConfigParam & QCSER_USE_128_BYTE_IN_PKT)
        {
            gVendorConfig.Use128ByteInPkt = TRUE;
            gVendorConfig.MinInPktSize = 128;
        }
        if (gDriverConfigParam & QCSER_USE_256_BYTE_IN_PKT)
        {
            gVendorConfig.Use256ByteInPkt = TRUE;
            gVendorConfig.MinInPktSize = 256;
        }
        if (gDriverConfigParam & QCSER_USE_512_BYTE_IN_PKT)
        {
            gVendorConfig.Use512ByteInPkt = TRUE;
            gVendorConfig.MinInPktSize = 512;
        }
        if (gDriverConfigParam & QCSER_USE_1024_BYTE_IN_PKT)
        {
            gVendorConfig.Use1024ByteInPkt = TRUE;
            gVendorConfig.MinInPktSize = 1024;
        }
        if (gDriverConfigParam & QCSER_USE_2048_BYTE_IN_PKT)
        {
            gVendorConfig.Use2048ByteInPkt = TRUE;
            gVendorConfig.MinInPktSize = 2048;
        }
        if (gDriverConfigParam & QCSER_USE_1K_BYTE_OUT_PKT)
        {
            gVendorConfig.Use1kByteOutPkt = TRUE;
            gVendorConfig.WriteUnitSize = 1024;
        }
        if (gDriverConfigParam & QCSER_USE_2K_BYTE_OUT_PKT)
        {
            gVendorConfig.Use2kByteOutPkt = TRUE;
            gVendorConfig.WriteUnitSize = 2048;
        }
        if (gDriverConfigParam & QCSER_USE_4K_BYTE_OUT_PKT)
        {
            gVendorConfig.Use4kByteOutPkt = TRUE;
            gVendorConfig.WriteUnitSize = 4096;
        }
        if (gDriverConfigParam & QCSER_USE_128K_READ_BUFFER)
        {
            gVendorConfig.Use128KReadBuffer = TRUE;
            gVendorConfig.InternalReadBufSize = 128 * 1024L;
        }
        if (gDriverConfigParam & QCSER_USE_256K_READ_BUFFER)
        {
            gVendorConfig.Use256KReadBuffer = TRUE;
            gVendorConfig.InternalReadBufSize = 256 * 1024L;
        }
        if (gDriverConfigParam & QCSER_NO_TIMEOUT_ON_CTL_REQ)
        {
            gVendorConfig.NoTimeoutOnCtlReq = TRUE;
        }
        if (gDriverConfigParam & QCSER_ENABLE_LOGGING)
        {
            gVendorConfig.EnableLogging = TRUE;
        }
        if (gDriverConfigParam & QCSER_RETRY_ON_TX_ERROR)
        {
            gVendorConfig.RetryOnTxError = TRUE;
        }
        if (gDriverConfigParam & QCSER_LOG_LATEST_PKTS)
        {
            gVendorConfig.LogLatestPkts = TRUE;
        }
        if (gDriverConfigParam & QCSER_LOGGING_WRITE_THROUGH)
        {
            gVendorConfig.LoggingWriteThrough = TRUE;
        }
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_VendorRegistryProcess <QCDriverConfig>: <%ld>\n", pDevContext->PortName, gDriverConfigParam)
    );

    if (ulWriteUnit > 64)
    {
        gVendorConfig.WriteUnitSize = ulWriteUnit;
    }
    gVendorConfig.WriteUnitSize = gVendorConfig.WriteUnitSize / 64 * 64;
    if (gVendorConfig.WriteUnitSize < 64)
    {
        gVendorConfig.WriteUnitSize = 64;
    }
    if (gVendorConfig.MaxPipeXferSize < gVendorConfig.MinInPktSize)
    {
        gVendorConfig.MaxPipeXferSize = gVendorConfig.MinInPktSize;
    }

    WdfRegistryClose(key);
    return STATUS_SUCCESS;
exit:
    if (key != NULL)
    {
        WdfRegistryClose(key);
        key = NULL;
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_VendorRegistryProcess FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    return status;
}

/****************************************************************************
 *
 * function: QCPNP_ResetUsbPipe
 *
 * purpose:  Stops the I/O target for the specified USB pipe, issues a
 *           synchronous pipe reset, and restarts the I/O target.
 *
 * arguments:pDevContext = pointer to the device context.
 *           usbPipe     = handle to the USB pipe to reset.
 *           timeoutMs   = timeout in milliseconds for the reset request.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_ResetUsbPipe
(
    PDEVICE_CONTEXT pDevContext,
    WDFUSBPIPE      usbPipe,
    ULONGLONG       timeoutMs
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFIOTARGET ioTarget = WdfUsbTargetPipeGetIoTarget(usbPipe);
    WDF_REQUEST_SEND_OPTIONS syncReqOptions;
    WDF_REQUEST_SEND_OPTIONS_INIT(&syncReqOptions, 0);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&syncReqOptions, WDF_REL_TIMEOUT_IN_MS(timeoutMs));

#ifdef QCUSB_MUX_PROTOCOL
    if (pDevContext->DeviceFunction == QCUSB_DEV_FUNC_LPC)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCPNP_ResetUsbPipe skip on LPC device\n", pDevContext->PortName)
        );
    }
    else
#endif
    {
        WdfIoTargetStop(ioTarget, WdfIoTargetCancelSentIo);
        status = WdfUsbTargetPipeResetSynchronously(usbPipe, WDF_NO_HANDLE, &syncReqOptions);
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCPNP_ResetUsbPipe reset request returned with status: 0x%x\n", pDevContext->PortName, status)
        );
        if (!NT_SUCCESS(status))
        {
            WdfIoTargetStart(ioTarget);
            return status;
        }
        else
        {
            status = WdfIoTargetStart(ioTarget);
        }
    }

    return status;
}

/****************************************************************************
 *
 * function: QCPNP_CreateWorkerThread
 *
 * purpose:  Creates a kernel system thread and optionally waits for it to
 *           signal a started event before returning the thread object.
 *
 * arguments:pDevContext           = pointer to the device context passed to
 *                                   the thread routine.
 *           startRoutine          = pointer to the thread start routine.
 *           waitForStartedEvent   = event the thread signals when ready;
 *                                   NULL to skip waiting.
 *           waitForStartedTimeout = timeout for the started event wait;
 *                                   NULL to skip waiting.
 *           threadObject          = output pointer to receive the thread
 *                                   object reference.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_CreateWorkerThread
(
    PDEVICE_CONTEXT pDevContext,
    PKSTART_ROUTINE startRoutine,
    PKEVENT waitForStartedEvent,
    PLARGE_INTEGER waitForStartedTimeout,
    PVOID *threadObject
)
{
    NTSTATUS          status;
    HANDLE            threadHandle;
    OBJECT_ATTRIBUTES threadAttributes;

    if (startRoutine == NULL || threadObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    InitializeObjectAttributes
    (
        &threadAttributes,
        NULL,
        OBJ_KERNEL_HANDLE,
        NULL,
        NULL
    );
    status = PsCreateSystemThread
    (
        &threadHandle,
        THREAD_ALL_ACCESS,
        &threadAttributes,
        NULL,
        NULL,
        startRoutine,
        pDevContext
    );
    if (NT_SUCCESS(status))
    {
        if (waitForStartedEvent != NULL && waitForStartedTimeout != NULL)
        {
            KeWaitForSingleObject
            (
                waitForStartedEvent,
                Executive,
                KernelMode,
                FALSE,
                waitForStartedTimeout
            );
            if (NT_SUCCESS(status))
            {
                KeClearEvent(waitForStartedEvent);
                status = ObReferenceObjectByHandle
                (
                    threadHandle,
                    THREAD_ALL_ACCESS,
                    NULL,
                    KernelMode,
                    threadObject,
                    NULL
                );
            }
            ZwClose(threadHandle);
        }
    }
    return status;
}

/****************************************************************************
 *
 * function: QCPNP_SetupIoThreadsAndQueues
 *
 * purpose:  Initializes all I/O events, lists, and locks; allocates read
 *           URBs; creates the ring buffer; and starts the read and write
 *           worker threads.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_SetupIoThreadsAndQueues
(
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS status;
    LARGE_INTEGER threadInitTimeout;
    threadInitTimeout.QuadPart = WDF_REL_TIMEOUT_IN_MS(QCPNP_THREAD_INIT_TIMEOUT_MS);

    // Init write request list, lock and events
    InitializeListHead(&pDevContext->WriteRequestPendingList);
    WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &pDevContext->WriteRequestPendingListLock);
    KeInitializeEvent
    (
        &pDevContext->WriteThreadStartedEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->WriteRequestArriveEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->WriteThreadPurgeEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->WritePurgeCompletionEvent,
        NotificationEvent,
        FALSE
    );
    pDevContext->WriteThreadResumeEvents[WRITE_THREAD_DEVICE_REMOVE_EVENT] = &pDevContext->DeviceRemoveEvent;
    pDevContext->WriteThreadResumeEvents[WRITE_THREAD_REQUEST_ARRIVE_EVENT] = &pDevContext->WriteRequestArriveEvent;
    pDevContext->WriteThreadResumeEvents[WRITE_THREAD_FILE_CLOSE_EVENT] = &pDevContext->FileCloseEventWrite;
    pDevContext->WriteThreadResumeEvents[WRITE_THREAD_PURGE_EVENT] = &pDevContext->WriteThreadPurgeEvent;
    pDevContext->WriteRequestPendingListDataSize = 0;
    pDevContext->WriteRequestPendingListLength = 0;

    // Init read buffer lists and parameters
    InitializeListHead(&pDevContext->UrbReadCompletionList);
    InitializeListHead(&pDevContext->UrbReadPendingList);
    InitializeListHead(&pDevContext->UrbReadFreeList);
    WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &pDevContext->UrbReadListLock);
    KeInitializeEvent
    (
        &pDevContext->ReadThreadStartedEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadRequestArriveEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadRequestCompletionEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadThreadScanWaitMaskEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadThreadClearBufferEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadPurgeCompletionEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadRequestTimeoutEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->ReadThreadCancelCurrentEvent,
        NotificationEvent,
        FALSE
    );
    KeInitializeEvent
    (
        &pDevContext->SessionTotalSetEvent,
        NotificationEvent,
        FALSE
    );
    pDevContext->ReadThreadResumeEvents[READ_THREAD_DEVICE_REMOVE_EVENT] = &pDevContext->DeviceRemoveEvent;
    pDevContext->ReadThreadResumeEvents[READ_THREAD_REQUEST_ARRIVE_EVENT] = &pDevContext->ReadRequestArriveEvent;
    pDevContext->ReadThreadResumeEvents[READ_THREAD_FILE_CREATE_EVENT] = &pDevContext->FileCreateEvent;
    pDevContext->ReadThreadResumeEvents[READ_THREAD_FILE_CLOSE_EVENT] = &pDevContext->FileCloseEventRead;
    pDevContext->ReadThreadResumeEvents[READ_THREAD_CLEAR_BUFFER_EVENT] = &pDevContext->ReadThreadClearBufferEvent;
    pDevContext->ReadThreadResumeEvents[READ_THREAD_SESSION_TOTAL_SET_EVENT] = &pDevContext->SessionTotalSetEvent;
    pDevContext->ReadThreadResumeEvents[READ_THREAD_DEVICE_D0_EXIT_EVENT] = &pDevContext->ReadThreadD0ExitEvent;
    pDevContext->ReadThreadResumeEvents[READ_THREAD_DEVICE_D0_ENTRY_EVENT] = &pDevContext->ReadThreadD0EntryEvent;
    pDevContext->ReadThreadResumeEvents[READ_THREAD_REQUEST_TIMEOUT_EVENT] = &pDevContext->ReadRequestTimeoutEvent;
    pDevContext->ReadThreadResumeEvents[READ_THREAD_REQUEST_COMPLETION_EVENT] = &pDevContext->ReadRequestCompletionEvent;
    pDevContext->ReadThreadResumeEvents[READ_THREAD_SCAN_WAIT_MASK_EVENT] = &pDevContext->ReadThreadScanWaitMaskEvent;
    pDevContext->UrbReadCompletionListDataSize = 0;
    pDevContext->UrbReadCompletionListLength = 0;
    pDevContext->UrbReadPendingListLength = 0;
    pDevContext->UrbReadFreeListLength = 0;
    pDevContext->UrbReadListCapacity = gVendorConfig.NumberOfL2Buffers;
    pDevContext->UrbReadBufferSize = gVendorConfig.MaxPipeXferSize;

#ifdef QCUSB_MUX_PROTOCOL
    if ((pDevContext->DeviceFunction == QCUSB_DEV_FUNC_LPC) || (pDevContext->DeviceFunction == QCUSB_DEV_FUNC_VI))
    {
        pDevContext->UrbReadListCapacity = 0;   // no urbs will be constructed until sessionTotal is set
    }
#endif

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> RIRP: QCPNP_SetupIoThreadsAndQueues UrbReadBufferSize: %llu, UrbReadListCapacity: %llu\n",
        pDevContext->PortName, pDevContext->UrbReadBufferSize, pDevContext->UrbReadListCapacity)
    );

    // Create read urbs
    for (ULONG i = 0; i < pDevContext->UrbReadListCapacity; i++)
    {
        WDFREQUEST readRequest;
        status = QCRD_CreateReadUrb(pDevContext, pDevContext->UrbReadBufferSize, '4gaT', &readRequest);
        if (!NT_SUCCESS(status) || readRequest == NULL)
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_READ,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> RIRP: QCPNP_SetupIoThreadsAndQueues read urb create FAILED index: %d, status: 0x%x\n", pDevContext->PortName, i, status)
            );
            goto exit;
        }
        // put urb into free list
        PREQUEST_CONTEXT pReqContext = QCReqGetContext(readRequest);
        InsertTailList(&pDevContext->UrbReadFreeList, &pReqContext->Link);
        pDevContext->UrbReadFreeListLength++;
    }

    // create ring buffer for rx
    QCUTIL_RingBufferInit(&pDevContext->ReadRingBuffer, USB_INTERNAL_READ_BUFFER_SIZE);

    // create write thread
    status = QCPNP_CreateWorkerThread
    (
        pDevContext,
        QCWT_WriteRequestHandlerThread,
        &pDevContext->WriteThreadStartedEvent,
        &threadInitTimeout,
        &pDevContext->WriteRequestHandlerThread
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_SetupIoThreadsAndQueues write handler thread create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        pDevContext->WriteRequestHandlerThread = NULL;
        goto exit;
    }

    // create read thread
    status = QCPNP_CreateWorkerThread
    (
        pDevContext,
        QCRD_ReadRequestHandlerThread,
        &pDevContext->ReadThreadStartedEvent,
        &threadInitTimeout,
        &pDevContext->ReadRequestHandlerThread
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_SetupIoThreadsAndQueues read handler thread create FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        pDevContext->ReadRequestHandlerThread = NULL;
        goto exit;
    }

    KeInitializeTimer(&pDevContext->ReadTimer);
    KeInitializeDpc(&pDevContext->ReadTimeoutDpc, QCRD_ReadTimeoutDpc, pDevContext);

exit:
    if (!NT_SUCCESS(status))
    {
        // exit io threads
        KeSetEvent(&pDevContext->DeviceRemoveEvent, IO_NO_INCREMENT, FALSE);

        // delete ring buffer for rx
        QCUTIL_RingBufferDelete(&pDevContext->ReadRingBuffer);

        // cleanup free list
        PLIST_ENTRY head = &pDevContext->UrbReadFreeList;
        while (!IsListEmpty(head))
        {
            PLIST_ENTRY peek = head->Flink;
            RemoveEntryList(peek);
            PREQUEST_CONTEXT pReqContext = CONTAINING_RECORD(peek, REQUEST_CONTEXT, Link);
            if (pReqContext->ReadBufferParam != NULL)
            {
                ExFreePoolWithTag(pReqContext->ReadBufferParam, '4gaT');
                pReqContext->ReadBufferParam = NULL;
            }
            WDFREQUEST request = pReqContext->Self;
            WdfObjectDelete(request);
        }
        pDevContext->UrbReadListCapacity = 0;
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_SetupIoThreadsAndQueues completed with status: 0x%x\n", pDevContext->PortName, status)
    );
    return status;
}

/****************************************************************************
 *
 * function: QCPNP_NotifyDeviceRemoval
 *
 * purpose:  Retrieves and completes any pending device-removal notification
 *           request from the WaitOnDeviceRemoval queue.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QCPNP_NotifyDeviceRemoval
(
    PDEVICE_CONTEXT pDevContext
)
{
    PULONG     buffer;
    NTSTATUS   status;
    WDFREQUEST request;

    status = WdfIoQueueRetrieveNextRequest
    (
        pDevContext->WaitOnDeviceRemovalQueue,
        &request
    );

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_NotifyDeviceRemoval status: 0x%x\n", pDevContext->PortName, status)
    );

    if (NT_SUCCESS(status) && request != NULL)
    {
        ULONG buffer = QCOMSER_REMOVAL_NOTIFICATION;
        status = QCUTIL_RequestCopyFromBuffer(request, &buffer, sizeof(ULONG), pDevContext);
        WdfRequestCompleteWithInformation(request, status, sizeof(ULONG));

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCPNP_NotifyDeviceRemoval complete request: 0x%p\n", pDevContext->PortName, request)
        );
    }
}

/****************************************************************************
 *
 * function: QCPNP_PostVendorRegistryProcess
 *
 * purpose:  Reads post-initialization vendor registry values such as the
 *           logging directory, device function, MUX DLCI, aggregation
 *           settings, and resolution time.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_PostVendorRegistryProcess(PDEVICE_CONTEXT pDevContext)
{
    NTSTATUS       status = STATUS_SUCCESS;
    WDFDEVICE      device = pDevContext->Device;
    WDFKEY         key = NULL;
    UNICODE_STRING ucValueName;
    ULONG          ulValue = 0;

    pDevContext->bLoggingOk = FALSE;

    status = WdfDeviceOpenRegistryKey
    (
        device,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_QUERY_VALUE,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_PostVendorRegistryProcess device regkey open FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }
    pDevContext->ucLoggingDir[0] = 0;
    DECLARE_UNICODE_STRING_SIZE(ucLogDir, 128);
    RtlInitUnicodeString(&ucValueName, VEN_DEV_LOG_DIR);
    status = QCMAIN_GetDriverRegistryStringW(key, &ucValueName, &ucLogDir, pDevContext);
    if (status == STATUS_SUCCESS)
    {
        pDevContext->bLoggingOk = TRUE;
        wcsncpy_s(pDevContext->ucLoggingDir, 128, ucLogDir.Buffer, _TRUNCATE);
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_PostVendorRegistryProcess <QCDriverLoggingDirectory>: <%ws> status: 0x%x\n", pDevContext->PortName, pDevContext->ucLoggingDir, status)
    );
    // Get device function
    RtlInitUnicodeString(&ucValueName, VEN_DRV_DEV_FUNC);
    status = QCMAIN_GetDriverRegistryDword(key, &ucValueName, &ulValue, pDevContext);

    pDevContext->DeviceFunction = QCUSB_DEV_FUNC_UNDEF;
    if (status == STATUS_SUCCESS)
    {
        pDevContext->DeviceFunction = ulValue;
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_PostVendorRegistryProcess <QCDeviceFunction>: <%ld> status: 0x%x\n", pDevContext->PortName, pDevContext->DeviceFunction, status)
    );
#ifdef QCUSB_MUX_PROTOCOL

    // Get mux DLCI
    RtlInitUnicodeString(&ucValueName, VEN_DRV_DEV_DLCI);
    status = QCMAIN_GetDriverRegistryDword(key, &ucValueName, &ulValue, pDevContext);

    pDevContext->MuxDataDLCI = 2;
    pDevContext->MuxEnabled = FALSE;
    if (status == STATUS_SUCCESS)
    {
        pDevContext->MuxDataDLCI = ulValue;
        pDevContext->MuxEnabled = TRUE;
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_PostVendorRegistryProcess [MUX] <QCDevDataDLCI>: <%ul> status: 0x%x\n", pDevContext->PortName, pDevContext->MuxDataDLCI, status)
    );

#endif // QCUSB_MUX_PROTOCOL

    // Aggregation enabled
    RtlInitUnicodeString(&ucValueName, VEN_DRV_AGG_ON);
    status = QCMAIN_GetDriverRegistryDword(key, &ucValueName, &(pDevContext->AggregationEnabled), pDevContext);
    if (status != STATUS_SUCCESS)
    {
        pDevContext->AggregationEnabled = 0;
    }
    else
    {
        if (pDevContext->AggregationEnabled != 1)
        {
            pDevContext->AggregationEnabled = 0;
        }
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_PostVendorRegistryProcess <QCDriverAggregationEnabled>: <%ul> status: 0x%x\n", pDevContext->PortName, pDevContext->AggregationEnabled, status)
    );

    // aggregation time/size
    RtlInitUnicodeString(&ucValueName, VEN_DRV_AGG_TIME);
    status = QCMAIN_GetDriverRegistryDword(key, &ucValueName, &(pDevContext->AggregationPeriod), pDevContext);
    if (status != STATUS_SUCCESS)
    {
        pDevContext->AggregationPeriod = QCUSB_AGGREGATION_PERIOD_DEFAULT;  // 2ms
    }
    else
    {
        if (pDevContext->AggregationPeriod > QCUSB_AGGREGATION_PERIOD_MAX)
        {
            pDevContext->AggregationPeriod = QCUSB_AGGREGATION_PERIOD_MAX;  // 5ms
        }
        if (pDevContext->AggregationPeriod < QCUSB_AGGREGATION_PERIOD_MIN)
        {
            pDevContext->AggregationPeriod = QCUSB_AGGREGATION_PERIOD_MIN;   // .5ms
        }
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_PostVendorRegistryProcess <QCDriverAggregationTime>: <%ul> status: 0x%x\n", pDevContext->PortName, pDevContext->AggregationPeriod, status)
    );
    RtlInitUnicodeString(&ucValueName, VEN_DRV_AGG_SIZE);
    status = QCMAIN_GetDriverRegistryDword(key, &ucValueName, &(pDevContext->WriteAggBuffer.MaxSize), pDevContext);
    if (status != STATUS_SUCCESS)
    {
        pDevContext->WriteAggBuffer.MaxSize = QCUSB_AGGREGATION_SIZE_DEFAULT;
    }
    else
    {
        if (pDevContext->WriteAggBuffer.MaxSize > QCUSB_AGGREGATION_SIZE_MAX)
        {
            pDevContext->WriteAggBuffer.MaxSize = QCUSB_AGGREGATION_SIZE_MAX;
        }
        if (pDevContext->WriteAggBuffer.MaxSize < QCUSB_AGGREGATION_SIZE_MIN)
        {
            pDevContext->WriteAggBuffer.MaxSize = QCUSB_AGGREGATION_SIZE_MIN;
        }
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_PostVendorRegistryProcess <QCDriverAggregationSize>: <%ul> status: 0x%x\n", pDevContext->PortName, pDevContext->WriteAggBuffer.MaxSize, status)
    );

    // resolution time
    RtlInitUnicodeString(&ucValueName, VEN_DRV_RES_TIME);
    status = QCMAIN_GetDriverRegistryDword(key, &ucValueName, &(pDevContext->ResolutionTime), pDevContext);
    if (status != STATUS_SUCCESS)
    {
        pDevContext->ResolutionTime = QCUSB_RESOLUTION_TIME_DEFAULT;  // 1ms
    }
    else
    {
        if (pDevContext->ResolutionTime > QCUSB_RESOLUTION_TIME_MAX)
        {
            pDevContext->ResolutionTime = QCUSB_RESOLUTION_TIME_MAX;  // 5ms
        }
        if (pDevContext->ResolutionTime <= QCUSB_RESOLUTION_TIME_MIN)
        {
            pDevContext->ResolutionTime = QCUSB_RESOLUTION_TIME_MIN;   // 1ms
        }
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCPNP_PostVendorRegistryProcess <QCDriverResolutionTime>: <%ul> status: 0x%x\n", pDevContext->PortName, pDevContext->ResolutionTime, status)
    );

    WdfRegistryClose(key);
    return STATUS_SUCCESS;
exit:
    if (key != NULL)
    {
        WdfRegistryClose(key);
        key = NULL;
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_PostVendorRegistryProcess FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    return status;
}

/****************************************************************************
 *
 * function: QCPNP_RetrieveServiceConfig
 *
 * purpose:  Reads service-level configuration from the driver service
 *           registry key, including driver residency and selective suspend
 *           idle timeout settings.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QCPNP_RetrieveServiceConfig(PDEVICE_CONTEXT pDevContext)
{
    NTSTATUS          status = STATUS_SUCCESS;
    WDFDEVICE         device = pDevContext->Device;
    WDFKEY            key = NULL;
    UNICODE_STRING    ucValueName;
    ULONG             driverResident, selectiveSuspendInMili = 0, selectiveSuspendIdleTime = 0;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_RetrieveServiceConfig\n", pDevContext->PortName)
    );

    status = WdfRegistryOpenKey
    (
        NULL,
        &gServicePath,
        KEY_READ,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
    );
    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_RetrieveServiceConfig device regkey open FAILED status: 0x%x\n", pDevContext->PortName, status)
        );
        goto exit;
    }

    RtlInitUnicodeString(&ucValueName, VEN_DRV_RESIDENT);
    status = QCMAIN_GetDriverRegistryDword
    (
        key,
        &ucValueName,
        &driverResident,
        pDevContext
    );
    if (status == STATUS_SUCCESS)
    {
        if (driverResident != gVendorConfig.DriverResident)
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_READ,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> QCPNP_RetrieveServiceConfig: DriverResident %lu\n", pDevContext->PortName, driverResident)
            );
            gVendorConfig.DriverResident = driverResident;
        }
    }
    // Get selective suspend idle time
    RtlInitUnicodeString(&ucValueName, VEN_DRV_SS_IDLE_T);
    status = QCMAIN_GetDriverRegistryDword
    (
        key,
        &ucValueName,
        &selectiveSuspendIdleTime,
        pDevContext
    );
    if (status == STATUS_SUCCESS)
    {
        if (QCUTIL_IsHighSpeedDevice(pDevContext) == TRUE)
        {
            pDevContext->InServiceSelectiveSuspension = TRUE;
        }
        else
        {
            pDevContext->InServiceSelectiveSuspension = ((selectiveSuspendIdleTime >> 31) != 0);
        }

        selectiveSuspendInMili = selectiveSuspendIdleTime & 0x40000000;

        selectiveSuspendIdleTime &= 0x00FFFFFF;

        if (selectiveSuspendInMili == 0)
        {
            pDevContext->SelectiveSuspendInMiliSeconds = FALSE;
            if ((selectiveSuspendIdleTime < QCUSB_SS_IDLE_MIN) &&
                (selectiveSuspendIdleTime != 0))
            {
                selectiveSuspendIdleTime = QCUSB_SS_IDLE_MIN;
            }
            else if (selectiveSuspendIdleTime > QCUSB_SS_IDLE_MAX)
            {
                selectiveSuspendIdleTime = QCUSB_SS_IDLE_MAX;
            }
        }
        else
        {
            pDevContext->SelectiveSuspendInMiliSeconds = TRUE;
            if ((selectiveSuspendIdleTime < QCUSB_SS_MILI_IDLE_MIN) &&
                (selectiveSuspendIdleTime != 0))
            {
                selectiveSuspendIdleTime = QCUSB_SS_MILI_IDLE_MIN;
            }
            else if (selectiveSuspendIdleTime > QCUSB_SS_MILI_IDLE_MAX)
            {
                selectiveSuspendIdleTime = QCUSB_SS_MILI_IDLE_MAX;
            }
        }

        if (selectiveSuspendIdleTime != pDevContext->SelectiveSuspendIdleTime)
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_READ,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> QCPNP_RetrieveServiceConfig: new selective suspend idle time=%us(%u)\n",
                pDevContext->PortName, selectiveSuspendIdleTime,
                pDevContext->InServiceSelectiveSuspension)
            );
            pDevContext->SelectiveSuspendIdleTime = selectiveSuspendIdleTime;
            QCPWR_SyncUpWaitWake(pDevContext);
            QCPWR_SetIdleTimer(pDevContext, 0, FALSE, 8);
        }
    }
    else
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCPNP_RetrieveServiceConfig: failed to fetch SS value from reg 0x%x\n", pDevContext->PortName, status)
        );
    }
    WdfRegistryClose(key);
    return;

exit:
    if (key != NULL)
    {
        WdfRegistryClose(key);
        key = NULL;
    }

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCPNP_RetrieveServiceConfig completed with status: 0x%x\n", pDevContext->PortName, status)
        );
    }

    return;
}  // QCPNP_RetrieveServiceConfig

/****************************************************************************
 *
 * function: QCUTIL_IsHighSpeedDevice
 *
 * purpose:  Stub function. Returns FALSE to indicate the device is not a
 *           high-speed USB device in this implementation.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  BOOLEAN (always FALSE)
 *
 ****************************************************************************/
//Empty Functions to support QCPNP_RetrieveServiceConfig
BOOLEAN QCUTIL_IsHighSpeedDevice(PDEVICE_CONTEXT pDevContext)
{
    UNREFERENCED_PARAMETER(pDevContext);
    return 0;
}  // QCUTIL_IsHighSpeedDevice

/****************************************************************************
 *
 * function: QCPWR_SyncUpWaitWake
 *
 * purpose:  Stub function. Placeholder for synchronizing wait/wake power
 *           management state.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QCPWR_SyncUpWaitWake(PDEVICE_CONTEXT pDevContext)
{
    UNREFERENCED_PARAMETER(pDevContext);
}

/****************************************************************************
 *
 * function: QCPWR_SetIdleTimer
 *
 * purpose:  Stub function. Placeholder for setting the selective suspend
 *           idle timer.
 *
 * arguments:pDevContext = pointer to the device context.
 *           BusyMask    = bitmask indicating busy state sources.
 *           NoReset     = if TRUE, do not reset the timer.
 *           Cookie      = caller identifier for debug purposes.
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QCPWR_SetIdleTimer(PDEVICE_CONTEXT pDevContext, UCHAR BusyMask, BOOLEAN NoReset, UCHAR Cookie)
{
    UNREFERENCED_PARAMETER(pDevContext);
    UNREFERENCED_PARAMETER(BusyMask);
    UNREFERENCED_PARAMETER(NoReset);
    UNREFERENCED_PARAMETER(Cookie);
}

/****************************************************************************
 *
 * function: QCPNP_GetCID
 *
 * purpose:  Parses the USB product string for a "_CID:" field and writes
 *           the extracted value to the driver registry.
 *
 * arguments:pDevContext    = pointer to the device context.
 *           ProductString  = pointer to the raw product string buffer.
 *           ProductStrLen  = length of the product string in bytes.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_GetCID
(
    PDEVICE_CONTEXT pDevContext,
    PCHAR  ProductString,
    INT    ProductStrLen
)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    CHAR pCidLocBuf[256];
    PCHAR pCidLoc = pCidLocBuf;
    SIZE_T pCidLocLen = ProductStrLen;
    INT            strLen = 0;
    BOOLEAN        bSetEntry = FALSE;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> -->_GetCID DO 0x%p\n", pDevContext->PortName, pDevContext)
    );

    if (ProductStrLen == 0)
    {
        goto UpdateRegistry;
    }

    RtlCopyMemory(pCidLoc, ProductString, pCidLocLen);

    // search for "_CID:"
    if (ProductStrLen > 0)
    {
        INT idx, adjusted = 0;
        PCHAR p = pCidLoc;
        BOOLEAN bMatchFound = FALSE;

        for (idx = 0; idx < ProductStrLen; idx++)
        {
            if ((*p == '_') && (*(p + 1) == 0) &&
                (*(p + 2) == 'C') && (*(p + 3) == 0) &&
                (*(p + 4) == 'I') && (*(p + 5) == 0) &&
                (*(p + 6) == 'D') && (*(p + 7) == 0) &&
                (*(p + 8) == ':') && (*(p + 9) == 0))
            {
                pCidLoc = p + 10;
                adjusted += 10;
                bMatchFound = TRUE;
                bSetEntry = TRUE;
                break;
            }
            p++;
            adjusted++;
        }

        // Adjust length
        if (bMatchFound == TRUE)
        {
            INT tmpLen = ProductStrLen;

            tmpLen -= adjusted;
            p = pCidLoc;
            while (tmpLen > 0)
            {
                if (((*p == ' ') && (*(p + 1) == 0)) ||  // space
                    ((*p == '_') && (*(p + 1) == 0)))    // or _ for another field
                {
                    *p = '\0';
                    break;
                }
                else
                {
                    p += 2;       // advance 1 unicode byte
                    tmpLen -= 2;  // remaining string length
                }
            }
            strLen = (USHORT)(p - pCidLoc);
        }
        else
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_TRACE,
                ("<%ws> QCPNP_GetCID: no CID found\n", pDevContext->PortName)
            );
            ntStatus = STATUS_UNSUCCESSFUL;
            bSetEntry = FALSE;
        }
    }

UpdateRegistry:

    if ((bSetEntry == TRUE) && (strLen > 0) && (pCidLoc != NULL))
    {
        ntStatus = QCMAIN_SetDriverRegistryStringW(VEN_DEV_CID, (PWSTR)pCidLoc, pDevContext);
    }
    else
    {

        QCMAIN_DeleteDriverRegistryValue(VEN_DEV_CID, pDevContext);

    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> <--QCPNP_GetCID: strLen %d ST 0x%x\n", pDevContext->PortName, strLen, ntStatus)
    );

    return ntStatus;

} // QCPNP_GetCID

/****************************************************************************
 *
 * function: QCPNP_GetSocVer
 *
 * purpose:  Parses the USB product string for a "_SOCVER:" field and writes
 *           the extracted value to the driver registry.
 *
 * arguments:pDevContext    = pointer to the device context.
 *           ProductString  = pointer to the raw product string buffer.
 *           ProductStrLen  = length of the product string in bytes.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_GetSocVer
(
    PDEVICE_CONTEXT pDevContext,
    PCHAR  ProductString,
    INT    ProductStrLen
)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    CHAR pSocVerLocBuf[256];
    PCHAR pSocVerLoc = pSocVerLocBuf;

    SIZE_T pSocVerLocLen = ProductStrLen;
    INT            strLen = 0;
    BOOLEAN        bSetEntry = FALSE;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> -->_GetSOCVER DO 0x%p\n", pDevContext->PortName, pDevContext)
    );

    if (ProductStrLen == 0)
    {
        goto UpdateRegistry;
    }

    RtlCopyMemory(pSocVerLoc, ProductString, pSocVerLocLen);

    // search for "_SOCVER:"
    if (ProductStrLen > 0)
    {
        INT idx, adjusted = 0;
        PCHAR p = pSocVerLoc;
        BOOLEAN bMatchFound = FALSE;

        for (idx = 0; idx < ProductStrLen; idx++)
        {
            if ((*p == '_') && (*(p + 1) == 0) &&
                (*(p + 2) == 'S') && (*(p + 3) == 0) &&
                (*(p + 4) == 'O') && (*(p + 5) == 0) &&
                (*(p + 6) == 'C') && (*(p + 7) == 0) &&
                (*(p + 8) == 'V') && (*(p + 9) == 0) &&
                (*(p + 10) == 'E') && (*(p + 9) == 0) &&
                (*(p + 12) == 'R') && (*(p + 11) == 0) &&
                (*(p + 14) == ':') && (*(p + 13) == 0))
            {
                pSocVerLoc = p + 16;
                adjusted += 16;
                bMatchFound = TRUE;
                bSetEntry = TRUE;
                break;
            }
            p++;
            adjusted++;
        }

        // Adjust length
        if (bMatchFound == TRUE)
        {
            INT tmpLen = ProductStrLen;

            tmpLen -= adjusted;
            p = pSocVerLoc;
            while (tmpLen > 0)
            {
                if (((*p == ' ') && (*(p + 1) == 0)) ||  // space
                    ((*p == '_') && (*(p + 1) == 0)))    // or _ for another field
                {
                    *p = '\0';
                    break;
                }
                else
                {
                    p += 2;       // advance 1 unicode byte
                    tmpLen -= 2;  // remaining string length
                }
            }
            strLen = (USHORT)(p - pSocVerLoc);
        }
        else
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_TRACE,
                ("<%ws> QCPNP_GetSocVer: no SOCVER found\n", pDevContext->PortName)
            );
            ntStatus = STATUS_UNSUCCESSFUL;
            bSetEntry = FALSE;
        }
    }

UpdateRegistry:

    if ((bSetEntry == TRUE) && (strLen > 0) && (pSocVerLoc != NULL))
    {

        ntStatus = QCMAIN_SetDriverRegistryStringW(VEN_DEV_SOCVER, (PWSTR)pSocVerLoc, pDevContext);
    }
    else
    {

        QCMAIN_DeleteDriverRegistryValue(VEN_DEV_SOCVER, pDevContext);

    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> <--QCPNP_GetSOCVER: strLen %d ST 0x%x\n", pDevContext->PortName, strLen, ntStatus)
    );

    return ntStatus;

}

/****************************************************************************
 *
 * function: QCPNP_GetStringDescriptor
 *
 * purpose:  Queries a USB string descriptor by index, optionally parses a
 *           "_SN:" prefix to extract the serial number, and stores the
 *           result in the driver registry. Also invokes CID and SoC version
 *           extraction when MatchPrefix is TRUE.
 *
 * arguments:Device      = handle to the WDF device object.
 *           Index       = USB string descriptor index to query.
 *           LanguageId  = language ID for the string descriptor request.
 *           MatchPrefix = if TRUE, search for the "_SN:" prefix in the
 *                         string before storing.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_GetStringDescriptor
(
    WDFDEVICE      Device,
    UCHAR          Index,
    USHORT         LanguageId,
    BOOLEAN        MatchPrefix
)
{
    PWSTR pSerNum;
    NTSTATUS ntStatus = STATUS_SUCCESS;
    PCHAR pSerLoc = NULL;
    PCHAR pCidLoc = NULL;
    PCHAR pSocVerLoc = NULL;
    UNICODE_STRING ucValueName;
    INT            strLen = 0;
    INT            productStrLen = 0;
    BOOLEAN        bSetEntry = FALSE;

    PDEVICE_CONTEXT       pDevContext = QCDevGetContext(Device);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> -->_GetStringDescriptor DO 0x%p idx %d\n", pDevContext->PortName, Device, Index)
    );

    if (Index == 0)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> <--_GetStringDescriptor: index is NULL\n", pDevContext->PortName)
        );
        goto UpdateRegistry;
    }

    pSerNum = (PWSTR)(pDevContext->DevSerialNumber);
    RtlZeroMemory(pDevContext->DevSerialNumber, 256);

    ntStatus = WdfUsbTargetDeviceQueryString
    (
        pDevContext->UsbDevice,
        NULL,
        NULL,
        NULL,
        (PUSHORT)&strLen,
        Index,
        LanguageId
    );
    if (!NT_SUCCESS(ntStatus) || strLen == 0)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> _GetStringDescriptor USB iProduct query FAILED status: 0x%x\n", pDevContext->PortName, ntStatus)
        );
        goto UpdateRegistry;
    }
    else
    {
        ntStatus = WdfUsbTargetDeviceQueryString
        (
            pDevContext->UsbDevice,
            NULL,
            NULL,
            (PWSTR)pSerNum,
            (PUSHORT)&strLen,
            Index,
            LanguageId
        );
    } // End printing usb device info as debug message

    if (!NT_SUCCESS(ntStatus))
    {
        RtlZeroMemory(pDevContext->DevSerialNumber, 256);
        goto UpdateRegistry;
    }
    else
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> _GetStringDescriptor DO 0x%p NTS 0x%x (%dB)\n", pDevContext->PortName,
            pDevContext, ntStatus, strLen)
        );
    }

    strLen = strLen * 2;
    productStrLen = strLen;

    pSerLoc = (PCHAR)pSerNum;
    pCidLoc = (PCHAR)pSerNum;
    pSocVerLoc = (PCHAR)pSerNum;
    bSetEntry = TRUE;

    // search for "_SN:"
    if ((MatchPrefix == TRUE) && (strLen > 0))
    {
        INT idx, adjusted = 0;
        PCHAR p = pSerLoc;
        BOOLEAN bMatchFound = FALSE;

        for (idx = 0; idx < strLen; idx++)
        {
            if ((*p == '_') && (*(p + 1) == 0) &&
                (*(p + 2) == 'S') && (*(p + 3) == 0) &&
                (*(p + 4) == 'N') && (*(p + 5) == 0) &&
                (*(p + 6) == ':') && (*(p + 7) == 0))
            {
                pSerLoc = p + 8;
                adjusted += 8;
                bMatchFound = TRUE;
                break;
            }
            p++;
            adjusted++;
        }

        // Adjust length
        if (bMatchFound == TRUE)
        {
            INT tmpLen = strLen;

            tmpLen -= adjusted;
            p = pSerLoc;
            while (tmpLen > 0)
            {
                if (((*p == ' ') && (*(p + 1) == 0)) ||  // space
                    ((*p == '_') && (*(p + 1) == 0)))    // or _ for another field
                {
                    break;
                }
                else
                {
                    p += 2;       // advance 1 unicode byte
                    tmpLen -= 2;  // remaining string length
                }
            }
            strLen = (USHORT)(p - pSerLoc); // 18;
        }
        else
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_TRACE,
                ("<%ws> <--QDBPNP_GetDeviceSerialNumber: no SN found\n", pDevContext->PortName)
            );
            ntStatus = STATUS_UNSUCCESSFUL;
            bSetEntry = FALSE;
        }
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> _GetDeviceSerialNumber: strLen %d\n", pDevContext->PortName, strLen)
    );

UpdateRegistry:

    if (MatchPrefix == FALSE)
    {
        RtlInitUnicodeString(&ucValueName, VEN_DEV_SERNUM);
    }
    else
    {
        RtlInitUnicodeString(&ucValueName, VEN_DEV_MSM_SERNUM);
    }
    if ((bSetEntry == TRUE) && (strLen > 0))
    {

        ntStatus = QCMAIN_SetDriverRegistryStringW((LPWSTR)ucValueName.Buffer, (PWSTR)pSerLoc, pDevContext);
    }
    else
    {

        QCMAIN_DeleteDriverRegistryValue((LPWSTR)ucValueName.Buffer, pDevContext);

    }
    if (MatchPrefix == TRUE)
    {
        QCPNP_GetCID(pDevContext, pCidLoc, productStrLen);
    }

    if (MatchPrefix == TRUE)
    {
        QCPNP_GetSocVer(pDevContext, pSocVerLoc, productStrLen);
    }

    return ntStatus;

} // QCPNP_GetStringDescriptor

/****************************************************************************
 *
 * function: QCPNP_RegisterWmiPowerGuid
 *
 * purpose:  Registers the WMI power management GUID with the WMI subsystem
 *           to support the "Allow the computer to turn off this device"
 *           power management option.
 *
 * arguments:pDevContext = pointer to the device context.
 *
 * returns:  NT Status (always STATUS_SUCCESS to avoid failing PrepareHardware)
 *
 ****************************************************************************/
NTSTATUS QCPNP_RegisterWmiPowerGuid
(
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device = pDevContext->Device;

    pDevContext->WmiLibInfo.GuidCount = 1;
    pDevContext->WmiLibInfo.GuidList = PMWmiGuidList;
    pDevContext->WmiLibInfo.QueryWmiDataBlock = QCPNP_PMQueryWmiDataBlock;
    pDevContext->WmiLibInfo.SetWmiDataBlock = QCPNP_PMSetWmiDataBlock;
    pDevContext->WmiLibInfo.QueryWmiRegInfo = QCPNP_PMQueryWmiRegInfo;
    pDevContext->WmiLibInfo.SetWmiDataItem = QCPNP_PMSetWmiDataItem;
    pDevContext->WmiLibInfo.ExecuteWmiMethod = NULL;
    pDevContext->WmiLibInfo.WmiFunctionControl = NULL;

    status = IoWMIRegistrationControl
    (
        WdfDeviceWdmGetDeviceObject(device),
        WMIREG_ACTION_REGISTER
    );

    if (!NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCPNP_RegisterWmiPowerGuid FAILED with status 0x%x\n", pDevContext->PortName, status)
        );
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_RegisterWmiPowerGuid completed with status 0x%x\n", pDevContext->PortName, status)
    );

    return STATUS_SUCCESS;  // DON'T FAIL PREPAREHARDWARE OR DEVICEADD
}

/****************************************************************************
 *
 * function: QCPNP_WdmPreprocessSystemControl
 *
 * purpose:  WDM IRP_MJ_SYSTEM_CONTROL preprocessor callback. Dispatches
 *           WMI IRPs through WmiSystemControl and handles each disposition.
 *
 * arguments:Device = handle to the WDF device object.
 *           Irp    = pointer to the WMI IRP to process.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_WdmPreprocessSystemControl
(
    WDFDEVICE Device,
    PIRP Irp
)
{
    NTSTATUS        status = STATUS_SUCCESS;
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(Device);
    PDEVICE_OBJECT  deviceObject = WdfDeviceWdmGetDeviceObject(Device);
    SYSCTL_IRP_DISPOSITION irpDisposition;

    if (deviceObject == NULL)
    {
        return status;
    }

    status = WmiSystemControl
    (
        &pDevContext->WmiLibInfo,
        deviceObject,
        Irp,
        &irpDisposition
    );

    switch (irpDisposition)
    {
        case IrpProcessed:
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_TRACE,
                ("<%ws> QCPNP_WdmPreprocessSystemControl IrpProcessed\n", pDevContext->PortName)
            );
            status = STATUS_SUCCESS;
            break;
        }
        case IrpNotCompleted:
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_TRACE,
                ("<%ws> QCPNP_WdmPreprocessSystemControl IrpNotCompleted\n", pDevContext->PortName)
            );
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            status = STATUS_SUCCESS;
            break;
        }
        case IrpForward:
        case IrpNotWmi:
        default:
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_TRACE,
                ("<%ws> QCPNP_WdmPreprocessSystemControl unsupported irp type: 0x%x\n", pDevContext->PortName, irpDisposition)
            );
            IoSkipCurrentIrpStackLocation(Irp);
            status = WdfDeviceWdmDispatchPreprocessedIrp(Device, Irp);
            break;
        }
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_WdmPreprocessSystemControl completed with status 0x%x\n", pDevContext->PortName, status)
    );
    return status;
}

/****************************************************************************
 *
 * function: QCPNP_PMQueryWmiDataBlock
 *
 * purpose:  WMI callback to query a data block. Returns the current power
 *           management enabled state for the registered power GUID.
 *
 * arguments:DeviceObject        = pointer to the WDM device object.
 *           Irp                 = pointer to the WMI IRP.
 *           GuidIndex           = index of the GUID being queried.
 *           InstanceIndex       = index of the instance being queried.
 *           InstanceCount       = number of instances requested.
 *           InstanceLengthArray = array to receive instance data lengths.
 *           BufferAvail         = size of the output buffer in bytes.
 *           Buffer              = pointer to the output data buffer.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_PMQueryWmiDataBlock
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           Irp,
    IN ULONG          GuidIndex,
    IN ULONG          InstanceIndex,
    IN ULONG          InstanceCount,
    IN OUT PULONG     InstanceLengthArray,
    IN ULONG          BufferAvail,
    OUT PUCHAR        Buffer
)
{
    NTSTATUS          status = STATUS_SUCCESS;
    WDFDEVICE         device = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    PDEVICE_CONTEXT   pDevContext = QCDevGetContext(device);

    if (Buffer == NULL || InstanceLengthArray == NULL)
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        switch (GuidIndex)
        {
            case 0:
            {
                if (BufferAvail < sizeof(BOOLEAN))
                {
                    status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }
                *(PBOOLEAN)Buffer = pDevContext->PowerManagementEnabled;
                *InstanceLengthArray = sizeof(BOOLEAN);
                break;
            }
            default:
            {
                status = STATUS_WMI_GUID_NOT_FOUND;
            }
        }
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_PMQueryWmiDataBlock 0 completed with status 0x%x\n", pDevContext->PortName, status)
    );

    status = WmiCompleteRequest
    (
        DeviceObject,
        Irp,
        status,
        sizeof(BOOLEAN),
        IO_NO_INCREMENT
    );

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_PMQueryWmiDataBlock 1 completed with status 0x%x\n", pDevContext->PortName, status)
    );
    return status;
}

/****************************************************************************
 *
 * function: QCPNP_PMSetWmiDataItem
 *
 * purpose:  WMI callback to set a single data item. Updates the power
 *           management enabled state and enables or disables selective
 *           suspend accordingly.
 *
 * arguments:DeviceObject  = pointer to the WDM device object.
 *           Irp           = pointer to the WMI IRP.
 *           GuidIndex     = index of the GUID being set.
 *           InstanceIndex = index of the instance being set.
 *           DataItemId    = identifier of the data item to set.
 *           BufferSize    = size of the input buffer in bytes.
 *           Buffer        = pointer to the input data buffer.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_PMSetWmiDataItem
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           Irp,
    IN ULONG          GuidIndex,
    IN ULONG          InstanceIndex,
    IN ULONG          DataItemId,
    IN ULONG          BufferSize,
    IN PUCHAR         Buffer
)
{
    NTSTATUS          status = STATUS_SUCCESS;
    WDFDEVICE         device = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    PDEVICE_CONTEXT   pDevContext = QCDevGetContext(device);

    switch (GuidIndex)
    {
        case 0:
        {
            pDevContext->PowerManagementEnabled = *(PBOOLEAN)Buffer;
            if (pDevContext->PowerManagementEnabled)
            {
                QCPNP_EnableSelectiveSuspend(device);
            }
            else
            {
                QCPNP_DisableSelectiveSuspend(device);
            }
            break;
        }
        default:
        {
            status = STATUS_WMI_GUID_NOT_FOUND;
        }
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_PMSetWmiDataItem 0 completed with status 0x%x\n", pDevContext->PortName, status)
    );

    status = WmiCompleteRequest
    (
        DeviceObject,
        Irp,
        status,
        0,
        IO_NO_INCREMENT
    );

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_PMSetWmiDataItem 1 completed with status 0x%x\n", pDevContext->PortName, status)
    );

    return status;
}

/****************************************************************************
 *
 * function: QCPNP_PMQueryWmiRegInfo
 *
 * purpose:  WMI callback to query registration information. Provides the
 *           PDO and service registry path to the WMI subsystem.
 *
 * arguments:DeviceObject    = pointer to the WDM device object.
 *           RegFlags        = output flags for WMI registration.
 *           InstanceName    = output instance name (unused).
 *           RegistryPath    = output pointer to the service registry path.
 *           MofResourceName = output MOF resource name (unused).
 *           Pdo             = output pointer to the physical device object.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_PMQueryWmiRegInfo
(
    IN  PDEVICE_OBJECT  DeviceObject,
    OUT PULONG          RegFlags,
    OUT PUNICODE_STRING InstanceName,
    OUT PUNICODE_STRING *RegistryPath,
    OUT PUNICODE_STRING MofResourceName,
    OUT PDEVICE_OBJECT *Pdo
)
{
    NTSTATUS           status = STATUS_SUCCESS;
    WDFDEVICE          device = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    PDEVICE_CONTEXT    pDevContext = QCDevGetContext(device);
    PDEVICE_OBJECT     pdo = WdfDeviceWdmGetPhysicalDevice(device);

    if (pdo == NULL)
    {
        *RegFlags = 0;
    }
    else
    {
        *RegFlags = WMIREG_FLAG_INSTANCE_PDO;
    }
    *Pdo = pdo;
    *RegistryPath = &gServicePath;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_PMQueryWmiRegInfo pdo: 0x%p\n", pDevContext->PortName, pdo)
    );
    return STATUS_SUCCESS;
}

/****************************************************************************
 *
 * function: QCPNP_PMSetWmiDataBlock
 *
 * purpose:  WMI callback to set an entire data block. Updates the power
 *           management enabled state and enables or disables selective
 *           suspend accordingly.
 *
 * arguments:DeviceObject  = pointer to the WDM device object.
 *           Irp           = pointer to the WMI IRP.
 *           GuidIndex     = index of the GUID being set.
 *           InstanceIndex = index of the instance being set.
 *           BufferSize    = size of the input buffer in bytes.
 *           Buffer        = pointer to the input data buffer.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_PMSetWmiDataBlock
(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           Irp,
    IN ULONG          GuidIndex,
    IN ULONG          InstanceIndex,
    IN ULONG          BufferSize,
    IN PUCHAR         Buffer
)
{
    NTSTATUS          status = STATUS_SUCCESS;
    WDFDEVICE         device = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    PDEVICE_CONTEXT   pDevContext = QCDevGetContext(device);

    switch (GuidIndex)
    {
        case 0:
        {
            if (BufferSize < sizeof(BOOLEAN))
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            pDevContext->PowerManagementEnabled = *(PBOOLEAN)Buffer;
            if (pDevContext->PowerManagementEnabled)
            {
                QCPNP_EnableSelectiveSuspend(device);
            }
            else
            {
                QCPNP_DisableSelectiveSuspend(device);
            }
            break;
        }
        default:
        {
            status = STATUS_WMI_GUID_NOT_FOUND;
        }
    }

    QCSER_DbgPrintG
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_PMSetWmiDataBlock 0 completed with status 0x%x\n", pDevContext->PortName, status)
    );

    status = WmiCompleteRequest
    (
        DeviceObject,
        Irp,
        status,
        sizeof(BOOLEAN),
        IO_NO_INCREMENT
    );

    QCSER_DbgPrintG
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCPNP_PMSetWmiDataBlock 1 completed with status 0x%x\n", pDevContext->PortName, status)
    );
    return status;
}

/****************************************************************************
 *
 * function: QCPNP_SetFunctionProtocol
 *
 * purpose:  Writes the interface protocol code to the driver registry key
 *           to record the current USB function protocol.
 *
 * arguments:pDevContext  = pointer to the device context.
 *           ProtocolCode = protocol code value to store in the registry.
 *
 * returns:  NT Status
 *
 ****************************************************************************/
NTSTATUS QCPNP_SetFunctionProtocol(PDEVICE_CONTEXT pDevContext, ULONG ProtocolCode)
{
    NTSTATUS       ntStatus;
    UNICODE_STRING ucValueName;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> --> _SetFunctionProtocol 0x%x\n", pDevContext->PortName, ProtocolCode)
    );

    RtlInitUnicodeString(&ucValueName, VEN_DEV_PROTOC);
    ntStatus = QCMAIN_SetDriverRegistryDword((LPWSTR)ucValueName.Buffer, ProtocolCode, pDevContext);

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> <-- _SetFunctionProtocolnter 0x%x ST 0x%x\n", pDevContext->PortName, ProtocolCode, ntStatus)
    );

    return ntStatus;
}  // QCPNP_SetFunctionProtocol
