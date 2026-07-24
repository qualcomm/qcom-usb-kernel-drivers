/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B P N P . C

GENERAL DESCRIPTION
    Plug-and-Play and power management callbacks for the QDSS USB
    function driver. Handles device enumeration, USB interface and
    pipe selection, symbolic link and friendly name creation, selective
    suspend configuration, and TraceIn pipe drain lifecycle management.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "QDBMAIN.h"
#include "QDBPNP.h"
#include "QDBDEV.h"
#include "QDBDSP.h"
#include "QDBRD.h"
#include "QDBWT.h"
#include "QDBREG.h"

#include <ntstrsafe.h>
#include <devpkey.h>

#ifdef EVENT_TRACING
#include "QDBWPP.h"
#include "QDBPNP.tmh"
#endif

/****************************************************************************
 *
 * function: QDBPNP_EvtDriverDeviceAdd
 *
 * purpose:  WDF callback invoked when a new device is found. Creates
 *           the WDF device object, configures file, queue, and PnP/power
 *           callbacks, sets up I/O queues for read/write/control, and
 *           creates a symbolic link for the device.
 *
 * arguments:Driver     = WDF driver handle
 *           DeviceInit = pointer to the device initialization structure
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_EvtDriverDeviceAdd
(
    WDFDRIVER        Driver,
    PWDFDEVICE_INIT  DeviceInit
)
{
    WDFDEVICE                    wdfDevice;
    WDF_OBJECT_ATTRIBUTES        devAttrib;
    PDEVICE_CONTEXT              pDevContext;
    NTSTATUS                     ntStatus;
    WDF_FILEOBJECT_CONFIG        fileSettings;
    WDF_OBJECT_ATTRIBUTES        fileAttrib;
    WDF_IO_QUEUE_CONFIG          queueSettings;
    WDF_DEVICE_PNP_CAPABILITIES  pnpCapabilities;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerEventCB;

    QDB_DbgPrintG
    (
        0, 0,
        ("-->QDBPNP_EvtDriverDeviceAdd: driver 0x%p\n", Driver)
    );

    // 1. PnP, power, and power policy callback functions
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerEventCB);
    pnpPowerEventCB.EvtDevicePrepareHardware = QDBPNP_EvtDevicePrepareHW;
    pnpPowerEventCB.EvtDeviceD0Entry         = QDBPNP_EvtDeviceD0Entry;
    pnpPowerEventCB.EvtDeviceD0Exit          = QDBPNP_EvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerEventCB);

    // 2. File event callback functions
    WDF_FILEOBJECT_CONFIG_INIT
    (
        &fileSettings,
        QDBDEV_EvtDeviceFileCreate,
        WDF_NO_EVENT_CALLBACK,
        QDBDEV_EvtDeviceFileCleanup
    );
    WDF_OBJECT_ATTRIBUTES_INIT(&fileAttrib);
    WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&fileAttrib, FILE_CONTEXT);
    WdfDeviceInitSetFileObjectConfig
    (
        DeviceInit,
        &fileSettings,
        &fileAttrib
    );

    // 4. Device characteristics
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

    // 5. Device object with extension/context
    WDF_OBJECT_ATTRIBUTES_INIT(&devAttrib);
    WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&devAttrib, DEVICE_CONTEXT);
    devAttrib.EvtCleanupCallback = QDBPNP_EvtDeviceCleanupCallback;

    // Create Device
    ntStatus = WdfDeviceCreate(&DeviceInit, &devAttrib, &wdfDevice);
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrintG
        (
            0, 0,
            ("WdfDeviceCreate failed ST 0x%X\n", ntStatus)
        );
        return ntStatus;
    }

    pDevContext = QdbDeviceGetContext(wdfDevice);
    RtlCopyMemory(pDevContext->PortName, QDB_DBG_NAME_PREFIX, sizeof(QDB_DBG_NAME_PREFIX));

    // TODO: for debug purpose
    pDevContext->DebugMask = 0x0;
    pDevContext->DebugLevel = QDB_DBG_LEVEL_FORCE;

    // 6. PnP Capabilities
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCapabilities);
    pnpCapabilities.SurpriseRemovalOK = TRUE;
    WdfDeviceSetPnpCapabilities(wdfDevice, &pnpCapabilities);

    // 7. Queues
    // default queue
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE
    (
        &queueSettings,
        WdfIoQueueDispatchParallel
    );
    queueSettings.EvtIoDeviceControl = QDBDSP_IoDeviceControl;
    queueSettings.EvtIoStop = QDBDSP_IoStop;
    queueSettings.EvtIoResume = QDBDSP_IoResume;

    ntStatus = WdfIoQueueCreate
    (
        wdfDevice,
        &queueSettings,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevContext->DefaultQueue
    );
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrintG
        (
            0, 0,
            ("WdfIoQueueCreate default failed ST 0x%X\n", ntStatus)
        );
        return ntStatus;
    }

    // read queue
    WDF_IO_QUEUE_CONFIG_INIT(&queueSettings, WdfIoQueueDispatchParallel);
    queueSettings.EvtIoRead = QDBRD_IoRead;
    queueSettings.EvtIoStop = QDBDSP_IoStop;
    queueSettings.EvtIoResume = QDBDSP_IoResume;

    ntStatus = WdfIoQueueCreate
    (
        wdfDevice,
        &queueSettings,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevContext->RxQueue
    );
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrintG
        (
            0, 0,
            ("WdfIoQueueCreate RX queue failed ST 0x%X\n", ntStatus)
        );
        return ntStatus;
    }

    ntStatus = WdfDeviceConfigureRequestDispatching
    (
        wdfDevice,
        pDevContext->RxQueue,
        WdfRequestTypeRead
    );
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrintG
        (
            0, 0,
            ("WdfDeviceConfigureRequestDispatching RX failure 0x%x\n", ntStatus)
        );
        return ntStatus;
    }

    // write queue
    WDF_IO_QUEUE_CONFIG_INIT(&queueSettings, WdfIoQueueDispatchParallel);
    queueSettings.EvtIoWrite = QDBWT_IoWrite;
    queueSettings.EvtIoStop = QDBDSP_IoStop;
    queueSettings.EvtIoResume = QDBDSP_IoResume;

    ntStatus = WdfIoQueueCreate
    (
        wdfDevice,
        &queueSettings,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevContext->TxQueue
    );
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrintG
        (
            0, 0,
            ("WdfIoQueueCreate TX queue failed ST 0x%X\n", ntStatus)
        );
        return ntStatus;
    }

    ntStatus = WdfDeviceConfigureRequestDispatching
    (
        wdfDevice,
        pDevContext->TxQueue,
        WdfRequestTypeWrite
    );
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrintG
        (
            0, 0,
            ("WdfDeviceConfigureRequestDispatching TX failure 0x%x\n", ntStatus)
        );
        return ntStatus;
    }

    // 8. Symbolic link to friendly name
    ntStatus = QDBPNP_CreateSymbolicName(wdfDevice);

    if (!NT_SUCCESS(ntStatus))
    {
        // register a wdfDevice IF for apps
        ntStatus = WdfDeviceCreateDeviceInterface
        (
            wdfDevice,
            (LPGUID)&QDBUSB_GUID,
            NULL
        );
        if (!NT_SUCCESS(ntStatus))
        {
            QDB_DbgPrintG
            (
                0, 0,
                ("WdfDeviceCreateDeviceInterface failed ST 0x%X\n", ntStatus)
            );
            return ntStatus;
        }
    }

    // 9. Anything else
    QDBPNP_ReadDebugMask(wdfDevice);

    QDB_DbgPrintG
    (
        0, 0,
        ("<--QDBPNP_EvtDriverDeviceAdd: 0x%p\n", wdfDevice)
    );

    return ntStatus;
}


/****************************************************************************
 *
 * function: QDBPNP_ReadDebugMask
 *
 * purpose:  Reads the QCDriverDebugMask registry value from the driver
 *           software key and updates the debug mask and level in the
 *           device context.
 *
 * arguments:QCDevice = WDF device handle
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_ReadDebugMask(WDFDEVICE QCDevice)
{
    NTSTATUS QCstatus;
    ULONG    QCvalue = 0;
    PDEVICE_CONTEXT pDevContext;

    pDevContext = QdbDeviceGetContext(QCDevice);

    QCstatus = QDBREG_GetDriverRegistryDword(QCDevice, VEN_DBG_MASK, &QCvalue);

    if (NT_SUCCESS(QCstatus))
    {
        pDevContext->DebugMask = QCvalue;
        pDevContext->DebugLevel = (UCHAR)(QCvalue & 0x0F);

        QDB_DbgPrintG
        (
            QDB_DBG_MASK_READ,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBPNP_ReadDebugMask: DebugMask 0x%x\n", pDevContext->PortName, QCvalue)
        );
    }
    else
    {
        QDB_DbgPrintG
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_TRACE,
            ("<%s> <--QDBPNP_ReadDebugMask: error 0x%x\n", pDevContext->PortName, QCstatus)
        );
    }

    return QCstatus;
}

/****************************************************************************
 *
 * function: QDBPNP_EvtDriverCleanupCallback
 *
 * purpose:  WDF cleanup callback for the driver object. Invokes WPP
 *           cleanup to release tracing resources before the driver
 *           is unloaded.
 *
 * arguments:Object = WDF object handle (cast to PDRIVER_OBJECT)
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBPNP_EvtDriverCleanupCallback(WDFOBJECT Object)
{
    PDRIVER_OBJECT driver = (PDRIVER_OBJECT)Object;

    QDB_DbgPrintG
    (
        0, 0,
        ("QDBPNP_EvtDriverCleanupCallback: Driver 0x%p\n", driver)
    );

#ifdef EVENT_TRACING
    WPP_CLEANUP(driver);
#endif
}  // QDBPNP_EvtDriverCleanupCallback

/****************************************************************************
 *
 * function: QDBPNP_EvtDeviceCleanupCallback
 *
 * purpose:  WDF cleanup callback for the device object. Marks the device
 *           as removed, frees the symbolic link buffer, clears registry
 *           stamps, waits for active pipe drain to stop, and releases
 *           RX buffers.
 *
 * arguments:Object = WDF object handle (cast to WDFDEVICE)
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBPNP_EvtDeviceCleanupCallback(WDFOBJECT Object)
{
    WDFDEVICE       wdfDevice;
    PDEVICE_CONTEXT pDevContext;

    wdfDevice = (WDFDEVICE)Object;
    pDevContext = QdbDeviceGetContext(wdfDevice);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> -->QDBPNP_EvtDeviceCleanupCallback: 0x%p\n", pDevContext->PortName, wdfDevice)
    );

    QDBREG_SetDriverRegistryDword(wdfDevice, VEN_DEV_TIME, 0);

    QDBRD_StopDraining(pDevContext);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBPNP_EvtDeviceCleanupCallback: 0x%p\n", pDevContext->PortName, wdfDevice)
    );
}  // QDBPNP_EvtDeviceCleanupCallback

/****************************************************************************
 *
 * function: QDBPNP_EvtDevicePrepareHW
 *
 * purpose:  WDF PnP callback invoked when hardware resources are assigned.
 *           Initializes the device context, enumerates and configures the
 *           USB device, enables selective suspend, reads registry settings,
 *           and configures the continuous reader for QDSS pipe draining.
 *
 * arguments:Device                 = WDF device handle
 *           ResourceList           = raw hardware resource list (unused)
 *           ResourceListTranslated = translated hardware resource list (unused)
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_EvtDevicePrepareHW
(
    WDFDEVICE    Device,
    WDFCMRESLIST ResourceList,
    WDFCMRESLIST ResourceListTranslated
)
{
    NTSTATUS        ntStatus;
    PDEVICE_CONTEXT pDevContext;

    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(ResourceListTranslated);

    pDevContext = QdbDeviceGetContext(Device);

    QDB_DbgPrint
    (
        0, 0,
        ("<%s> -->QDBPNP_EvtDevicePrepareHW: 0x%p\n", pDevContext->PortName, Device)
    );

    // Init context
    pDevContext->TraceIN = NULL;
    pDevContext->DebugIN = NULL;
    pDevContext->DebugOUT = NULL;
    pDevContext->MyIoTarget = WdfDeviceGetIoTarget(Device);
    pDevContext->MyDevice = Device;
    RtlZeroMemory(&pDevContext->Stats, sizeof(QDB_STATS));

    // Device descriptor
    ntStatus = QDBPNP_EnumerateDevice(Device);

    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrint
        (
            0, 0,
            ("<%s> QDBPNP_EvtDevicePrepareHW: ReadandSelectDescriptors failed\n", pDevContext->PortName)
        );
        return ntStatus;
    }

    ntStatus = QDBPNP_EnableSelectiveSuspend(Device);
    QDBPNP_GetParentDeviceName(pDevContext);

    QDBREG_SetDriverRegistryDword(Device, VEN_DEV_TIME, 1);

    QDBRD_ConfigureContinuousReader(pDevContext);

    QDB_DbgPrint
    (
        0, 0,
        ("<%s> <--QDBPNP_EvtDevicePrepareHW: 0x%p (SelectiveSuspend status 0x%x)\n", pDevContext->PortName, Device, ntStatus)
    );

    return STATUS_SUCCESS;
}  // QDBPNP_EvtDevicePrepareHW

/****************************************************************************
 *
 * function: QDBPNP_EvtDeviceD0Entry
 *
 * purpose:  WDF power callback. Starts the continuous reader so that the
 *           pipe is drained automatically while no user application reads.
 *
 * arguments:Device        = WDF device handle
 *           PreviousState = power state the device is transitioning from
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_EvtDeviceD0Entry
(
    WDFDEVICE              Device,
    WDF_POWER_DEVICE_STATE PreviousState
)
{
    PDEVICE_CONTEXT pDevContext = QdbDeviceGetContext(Device);

    QDB_DbgPrintG
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> QDBPNP_EvtDeviceD0Entry: prev state [%u]\n", pDevContext->PortName, PreviousState)
    );

    QDBRD_StartDraining(pDevContext);

    return STATUS_SUCCESS;
}

/****************************************************************************
 *
 * function: QDBPNP_EvtDeviceD0Exit
 *
 * purpose:  WDF power callback. Stops the continuous reader so no I/O
*            is outstanding while the device is in a low-power state.
 *
 * arguments:Device      = WDF device handle
 *           TargetState = power state the device is transitioning to
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_EvtDeviceD0Exit
(
    WDFDEVICE              Device,
    WDF_POWER_DEVICE_STATE TargetState
)
{
    PDEVICE_CONTEXT pDevContext = QdbDeviceGetContext(Device);

    QDB_DbgPrintG
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> QDBPNP_EvtDeviceD0Exit: target state [%u]\n", pDevContext->PortName, TargetState)
    );

    QDBRD_StopDraining(pDevContext);

    return STATUS_SUCCESS;
}  // QDBPNP_EvtDeviceD0Exit

/****************************************************************************
 *
 * function: QDBPNP_SetFunctionProtocol
 *
 * purpose:  Derives and stores the device function type in the device
 *           context from the protocol code in USB interface descriptor
 *           Protocol 0x80 identifies a DPL interface; all other values
 *           (e.g. 0x70) identify a QDSS interface.
 *
 * arguments:Device             = WDF device handle
 *           ProtocolCode       = bInterfaceProtocol field from the USB
 *                                interface descriptor
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBPNP_SetFunctionProtocol(IN WDFDEVICE Device, UCHAR ProtocolCode)
{
    PDEVICE_CONTEXT pDevContext = QdbDeviceGetContext(Device);

    switch (ProtocolCode)
    {
        case 0x80:
        {
            pDevContext->FunctionType = QDB_FUNCTION_TYPE_DPL;
            break;
        }
        case 0x70:
        {
            pDevContext->FunctionType = QDB_FUNCTION_TYPE_QDSS;
            break;
        }
        default:
        {
            pDevContext->FunctionType = QDB_FUNCTION_TYPE_QDSS;
            QDB_DbgPrintG
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_INFO,
                ("<%s> QDBPNP_SetFunctionProtocol: Unknown bInterfaceProtocol 0x%02x, fallback to QDSS\n",
                pDevContext->PortName, ProtocolCode)
            );
            break;
        }
    }

    QDB_DbgPrintG
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_INFO,
        ("<%s> QDBPNP_SetFunctionProtocol: bInterfaceProtocol 0x%02x -> FunctionType %d (0=QDSS 1=DPL)\n",
        pDevContext->PortName, ProtocolCode, pDevContext->FunctionType)
    );
}  // QDBPNP_SetFunctionProtocol

/****************************************************************************
 *
 * function: QDBPNP_GetProductDescriptorString
 *
 * purpose:  Allocates and queries a USB string descriptor by index.
 *           The WDFMEMORY object is parented to UsbDevice and freed
 *           automatically when the USB device is destroyed.
 *
 * arguments:UsbDevice     = WDF USB device handle
 *           StringIndex   = USB string descriptor index to query
 *           pStringMemory = [out] allocated WDFMEMORY; valid on success
 *           pString       = [out] output string; optional, may be NULL
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_GetProductDescriptorString
(
    _In_      WDFUSBDEVICE    UsbDevice,
    _In_      UCHAR           StringIndex,
    _Out_     WDFMEMORY      *pStringMemory,
    _Out_opt_ PUNICODE_STRING pString
)
{
    NTSTATUS status;
    USHORT   numChars = 0;
    WDF_OBJECT_ATTRIBUTES memoryAttributes;

    if (UsbDevice == NULL || pStringMemory == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&memoryAttributes);
    memoryAttributes.ParentObject = UsbDevice;

    status = WdfUsbTargetDeviceAllocAndQueryString
    (
        UsbDevice,
        &memoryAttributes,
        pStringMemory,
        &numChars,
        StringIndex,
        0x0409
    );

    if (NT_SUCCESS(status))
    {
        if (pString != NULL)
        {
            pString->Buffer = WdfMemoryGetBuffer(*pStringMemory, NULL);
            pString->Length = numChars * sizeof(WCHAR);
            pString->MaximumLength = numChars * sizeof(WCHAR);
        }
    }

    return status;
}

/****************************************************************************
 *
 * function: QDBPNP_GetDeviceIdString
 *
 * purpose:  Scans a USB product descriptor string for a tagged ID field
 *           matching the given keyword (e.g. QCOM_USB_ID_TYPE_CHIP or
 *           QCOM_USB_ID_TYPE_SERIAL) and construct the value as a
 *           UNICODE_STRING in-place onto the out buffer
 *
 * arguments:productDescription = read-only USB product descriptor string;
 *                                 need not be null-terminated
 *           keyword            = tagged field keyword to search for
 *                                (e.g. QCOM_USB_ID_TYPE_CHIP = L"_CID:")
 *           value              = [out] output UNICODE_STRING pointing into
 *                                productDescription; valid on STATUS_SUCCESS
 *
 * returns:  STATUS_SUCCESS           - field found; value is populated
 *           STATUS_NOT_FOUND         - field not present in string
 *           STATUS_INVALID_PARAMETER - invalid input strings
 *
 ****************************************************************************/
NTSTATUS QDBPNP_GetDeviceIdString
(
    _In_  PCUNICODE_STRING productDescription,
    _In_  PCUNICODE_STRING keyword,
    _Out_ PUNICODE_STRING  value
)
{
    PWCHAR p, buffer, bufferEnd;

    if (productDescription == NULL || productDescription->Buffer == NULL ||
        keyword == NULL || keyword->Buffer == NULL || value == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(value, sizeof(UNICODE_STRING));

    buffer = productDescription->Buffer;
    bufferEnd = buffer + productDescription->Length / sizeof(WCHAR);
    for (p = buffer; p < bufferEnd; p++)
    {
        if (value->Buffer != NULL)
        {
            if (*p == QCOM_USB_ID_DELIMITER || *p == QCOM_USB_ID_DELIMITER_END)
            {
                break;
            }
        }
        else if (*p == QCOM_USB_ID_DELIMITER)
        {
            if (p + keyword->Length / sizeof(WCHAR) >= bufferEnd)
            {
                break;
            }
            if (RtlCompareMemory(p, keyword->Buffer, keyword->Length) == keyword->Length)
            {
                p = p + keyword->Length / sizeof(WCHAR);
                if (*p != QCOM_USB_ID_DELIMITER &&
                    *p != QCOM_USB_ID_DELIMITER_END && p < bufferEnd)
                {
                    value->Buffer = p;
                }
                else
                {
                    p = p - 1;
                }
            }
        }
    }

    if (value->Buffer != NULL)
    {
        value->Length = (p - value->Buffer) * sizeof(WCHAR);
        value->MaximumLength = value->Length;
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_FOUND;
}

/****************************************************************************
 *
 * function: QDBPNP_EnumerateDevice
 *
 * purpose:  Creates the WDF USB device handle, retrieves the USB device
 *           descriptor, queries serial number strings, and calls
 *           QDBPNP_UsbConfigureDevice to select interfaces and pipes.
 *
 * arguments:Device = WDF device handle
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_EnumerateDevice(IN WDFDEVICE Device)
{
    PDEVICE_CONTEXT       pDevContext;
    USB_DEVICE_DESCRIPTOR usbDeviceDesc;
    NTSTATUS              ntStatus;

    pDevContext = QdbDeviceGetContext(Device);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBPNP_EnumerateDevice: 0x%p\n", pDevContext->PortName, Device)
    );

    // Get the USB device handle to communicate with the USB stack
    if (pDevContext->WdfUsbDevice == NULL)
    {
        ntStatus = WdfUsbTargetDeviceCreate
        (
            Device,
            WDF_NO_OBJECT_ATTRIBUTES,
            &pDevContext->WdfUsbDevice
        );
        if (!NT_SUCCESS(ntStatus))
        {
            QDB_DbgPrint
            (
                0, 0,
                ("<%s> QDBPNP_EnumerateDevice: couldn't get USB handle 0x%x\n", pDevContext->PortName, ntStatus)
            );
            return ntStatus;
        }
    }

    WdfUsbTargetDeviceGetDeviceDescriptor
    (
        pDevContext->WdfUsbDevice,
        &usbDeviceDesc
    );

    // Get device serial number
    WDFMEMORY stringMemory = NULL;
    UNICODE_STRING productString;
    QDBREG_DeleteDriverRegistryValue(Device, VEN_DEV_SERNUM);
    ntStatus = QDBPNP_GetProductDescriptorString(pDevContext->WdfUsbDevice, usbDeviceDesc.iSerialNumber, &stringMemory, &productString);
    if (NT_SUCCESS(ntStatus))
    {
        ntStatus = QDBREG_SetDriverRegistryStringU(Device, VEN_DEV_SERNUM, &productString);
        if (NT_SUCCESS(ntStatus))
        {
            QDB_DbgPrintG
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_INFO,
                ("<%s> -->QDBPNP_EnumerateDevice: serial number %wZ to registry\n", pDevContext->PortName, &productString)
            );
        }
        WdfObjectDelete(stringMemory);
        stringMemory = NULL;
    }

    // Get device msm serial number
    UNICODE_STRING keywordLabel, serialString, cidString;
    RtlInitUnicodeString(&keywordLabel, QCOM_USB_ID_TYPE_SERIAL);
    QDBREG_DeleteDriverRegistryValue(Device, VEN_DEV_MSM_SERNUM);
    ntStatus = QDBPNP_GetProductDescriptorString(pDevContext->WdfUsbDevice, usbDeviceDesc.iProduct, &stringMemory, &productString);
    if (NT_SUCCESS(ntStatus))
    {
        ntStatus = QDBPNP_GetDeviceIdString(&productString, &keywordLabel, &serialString);
    }

    if (!NT_SUCCESS(ntStatus) && usbDeviceDesc.iProduct != 0x02)
    {
        if (stringMemory != NULL)
        {
            WdfObjectDelete(stringMemory);
            stringMemory = NULL;
        }
        ntStatus = QDBPNP_GetProductDescriptorString(pDevContext->WdfUsbDevice, 0x02, &stringMemory, &productString);
        if (NT_SUCCESS(ntStatus))
        {
            ntStatus = QDBPNP_GetDeviceIdString(&productString, &keywordLabel, &serialString);
        }
    }

    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrintG
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBPNP_EnumerateDevice: failed to get serial number\n", pDevContext->PortName)
        );
    }
    else
    {
        ntStatus = QDBREG_SetDriverRegistryStringU(Device, VEN_DEV_MSM_SERNUM, &serialString);
        if (NT_SUCCESS(ntStatus))
        {
            QDB_DbgPrintG
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_INFO,
                ("<%s> -->QDBPNP_EnumerateDevice: msm serial number %wZ to registry\n", pDevContext->PortName, &serialString)
            );
        }
        RtlInitUnicodeString(&keywordLabel, QCOM_USB_ID_TYPE_CHIP);
        QDBREG_DeleteDriverRegistryValue(Device, VEN_DEV_CID);
        ntStatus = QDBPNP_GetDeviceIdString(&productString, &keywordLabel, &cidString);
        if (NT_SUCCESS(ntStatus))
        {
            ntStatus = QDBREG_SetDriverRegistryStringU(Device, VEN_DEV_CID, &cidString);
            if (NT_SUCCESS(ntStatus))
            {
                QDB_DbgPrintG
                (
                    QDB_DBG_MASK_CONTROL,
                    QDB_DBG_LEVEL_INFO,
                    ("<%s> -->QDBPNP_EnumerateDevice: cid number %wZ to registry\n", pDevContext->PortName, &cidString)
                );
            }
        }
    }

    if (stringMemory != NULL)
    {
        WdfObjectDelete(stringMemory);
        stringMemory = NULL;
    }

    if (usbDeviceDesc.bNumConfigurations == 0)
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBPNP_EnumerateDevice: failed to get DevDescriptor\n", pDevContext->PortName)
        );
        return STATUS_UNSUCCESSFUL;
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> USB Device: bLength(%d) bDescType(0x%x) bcdUSB(%d) bDevClass(0x%x)\n",
        pDevContext->PortName,
        usbDeviceDesc.bLength,
        usbDeviceDesc.bDescriptorType,
        usbDeviceDesc.bcdUSB,
        usbDeviceDesc.bDeviceClass
    )
    );
    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> USB Device: bDevSubClass(0x%x) bDevProtocol(0x%x) bMaxPktSz0(%d) idVendor(0x%x)\n",
        pDevContext->PortName,
        usbDeviceDesc.bDeviceSubClass,
        usbDeviceDesc.bDeviceProtocol,
        usbDeviceDesc.bMaxPacketSize0,
        usbDeviceDesc.idVendor
    )
    );
    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> USB Device: idProduct(0x%x) bcdDevice(0x%x) iMFR(0x%x) iProd(0x%x) iSerNum(0x%x) bNumConfig(%d)\n",
        pDevContext->PortName,
        usbDeviceDesc.idProduct,
        usbDeviceDesc.bcdDevice,
        usbDeviceDesc.iManufacturer,
        usbDeviceDesc.iProduct,
        usbDeviceDesc.iSerialNumber,
        usbDeviceDesc.bNumConfigurations
    )
    );

    ntStatus = QDBPNP_UsbConfigureDevice(Device);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBPNP_EnumerateDevice 0x%p (0x%x)\n", pDevContext->PortName, Device, ntStatus)
    );
    return ntStatus;

}  // QDBPNP_EnumerateDevice

/****************************************************************************
 *
 * function: QDBPNP_UsbConfigureDevice
 *
 * purpose:  Retrieves the USB configuration descriptor, allocates memory
 *           for it, and calls QDBPNP_SelectInterfaces to configure USB
 *           interfaces and pipes.
 *
 * arguments:Device = WDF device handle
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_UsbConfigureDevice(IN WDFDEVICE Device)
{
    PDEVICE_CONTEXT               pDevContext;
    NTSTATUS                      ntStatus;
    USHORT                        bufSize = 0;
    PUSB_CONFIGURATION_DESCRIPTOR configDesc = NULL;
    WDF_OBJECT_ATTRIBUTES         objAttrib;
    WDFMEMORY                     memory;

    pDevContext = QdbDeviceGetContext(Device);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBPNP_UsbConfigureDevice: 0x%p\n", pDevContext->PortName, Device)
    );

    // get the size of config desc
    ntStatus = WdfUsbTargetDeviceRetrieveConfigDescriptor
    (
        pDevContext->WdfUsbDevice, NULL, &bufSize
    );

    if (ntStatus != STATUS_BUFFER_TOO_SMALL)
    {
        return ntStatus;
    }

    // create config desc and tie to parent for garbage collection
    WDF_OBJECT_ATTRIBUTES_INIT(&objAttrib);

    objAttrib.ParentObject = pDevContext->WdfUsbDevice;

    ntStatus = WdfMemoryCreate
    (
        &objAttrib,
        NonPagedPoolNx,
        0,
        bufSize,
        &memory,
        &configDesc
    );
    if (!NT_SUCCESS(ntStatus))
    {
        return ntStatus;
    }

    // retrieve config desc
    ntStatus = WdfUsbTargetDeviceRetrieveConfigDescriptor
    (
        pDevContext->WdfUsbDevice,
        configDesc,
        &bufSize
    );
    if (!NT_SUCCESS(ntStatus))
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBPNP_UsbConfigureDevice: failed to get descriptor 0x%p (0x%x)\n", pDevContext->PortName,
            Device, ntStatus)
        );
        return ntStatus;
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> USB Configuration: bLength(%d) bDescType(0x%x) wTotalLen(%d) numIF(%d)\n",
        pDevContext->PortName,
        configDesc->bLength, configDesc->bDescriptorType,
        configDesc->wTotalLength, configDesc->bNumInterfaces
    )
    );
    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_DETAIL,
        ("<%s> USB Configuration: bConfigValue(%d) iConfig(0x%x) bmAttr(0x%X) maxPwr(%d)\n",
        pDevContext->PortName,
        configDesc->bConfigurationValue, configDesc->iConfiguration,
        configDesc->bmAttributes, configDesc->MaxPower
    )
    );

    ntStatus = QDBPNP_SelectInterfaces(Device);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBPNP_UsbConfigureDevice: 0x%p (0x%x)\n", pDevContext->PortName, Device, ntStatus)
    );
    return ntStatus;

}  // QDBPNP_UsbConfigureDevice

/****************************************************************************
 *
 * function: QDBPNP_SelectInterfaces
 *
 * purpose:  Selects the USB configuration and iterates over all interfaces
 *           to identify and configure the TRACE (1-pipe bulk IN) and DEBUG
 *           (2-pipe bulk IN/OUT) interfaces, storing pipe handles in the
 *           device context.
 *
 * arguments:Device = WDF device handle
 *
 * returns:  NT status; STATUS_UNSUCCESSFUL if no valid interface is found
 *
 ****************************************************************************/
NTSTATUS QDBPNP_SelectInterfaces(WDFDEVICE Device)
{
    PDEVICE_CONTEXT                     pDevContext;
    NTSTATUS                            ntStatus;
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS cfgParams;
    UCHAR                               numInterfaces;
    BOOLEAN                             bTraceFound = FALSE;
    BOOLEAN                             bDebugFound = FALSE;
    WDF_USB_PIPE_INFORMATION            pipeInfoTrace;
    WDF_USB_PIPE_INFORMATION            pipeInfoDebug0, pipeInfoDebug1;
    WDFUSBPIPE                          pipe0, pipe1;

    pDevContext = QdbDeviceGetContext(Device);
    numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(pDevContext->WdfUsbDevice);

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBPNP_SelectInterfaces: Device 0x%p numIFs=%d\n", pDevContext->PortName,
        Device, numInterfaces)
    );

    if (numInterfaces == 0)
    {
        QDB_DbgPrintG
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBPNP_SelectInterfaces: no interface found\n", pDevContext->PortName)
        );
        ntStatus = STATUS_UNSUCCESSFUL;
    }
    else if (numInterfaces == 1)
    {
        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(&cfgParams);
    }
    else
    {
        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES(&cfgParams, 0, NULL);
    }

    ntStatus = WdfUsbTargetDeviceSelectConfig
    (
        pDevContext->WdfUsbDevice,
        WDF_NO_OBJECT_ATTRIBUTES,
        &cfgParams
    );

    if (NT_SUCCESS(ntStatus))
    {
        // for testing only, customized for single-interface device
        if (numInterfaces == 1)
        {
            WDFUSBINTERFACE                         configuredInterface;
            WDF_USB_INTERFACE_SELECT_SETTING_PARAMS interfaceSelectSetting;
            NTSTATUS                                ntStatus;

            configuredInterface = cfgParams.Types.SingleInterface.ConfiguredUsbInterface;

#ifdef QDB_USE_GOBI_DIAG

            // for testing only, customized for GOBI2000 DIAG interface using alt setting 1
            WDF_USB_INTERFACE_SELECT_SETTING_PARAMS_INIT_SETTING(&interfaceSelectSetting, 1);

#else

            // choose alt setting 0
            WDF_USB_INTERFACE_SELECT_SETTING_PARAMS_INIT_SETTING(&interfaceSelectSetting, 0);

#endif // QDB_USE_GOBI_DIAG

            ntStatus = WdfUsbInterfaceSelectSetting
            (
                configuredInterface,
                WDF_NO_OBJECT_ATTRIBUTES,
                &interfaceSelectSetting
            );
            if (!NT_SUCCESS(ntStatus))
            {
                QDB_DbgPrint
                (
                    QDB_DBG_MASK_CONTROL,
                    QDB_DBG_LEVEL_ERROR,
                    ("<%s> Device 0x%p: Failed to select interface with alt setting 1\n",
                    pDevContext->PortName, Device)
                );
                return ntStatus;
            }
        }  // test code for GOBI2000 DIAG interface
    }

    // we do not mandate there be 2 interfaces, we just look for interfaces that satisfying
    // TRACE and DEBUG interface requirements
    if (NT_SUCCESS(ntStatus))
    {
        if (numInterfaces > 0)
        {
            UCHAR i;
            WDFUSBINTERFACE usbInterface;
            BYTE numEPs, numPipes;

            for (i = 0; i < numInterfaces; i++)
            {
                usbInterface = WdfUsbTargetDeviceGetInterface(pDevContext->WdfUsbDevice, i);
                if (usbInterface == NULL)
                {
                    QDB_DbgPrint
                    (
                        QDB_DBG_MASK_CONTROL,
                        QDB_DBG_LEVEL_ERROR,
                        ("<%s> invalid interface %d\n", pDevContext->PortName, i)
                    );
                    continue;
                }
                numEPs = WdfUsbInterfaceGetNumEndpoints(usbInterface, 0);  // for information only
                numPipes = WdfUsbInterfaceGetNumConfiguredPipes(usbInterface);

                QDB_DbgPrint
                (
                    QDB_DBG_MASK_CONTROL,
                    QDB_DBG_LEVEL_DETAIL,
                    ("<%s> examining IF 0x%p(%d) numEPs %d numPipes %d\n", pDevContext->PortName,
                    usbInterface, i, numEPs, numPipes)
                );

                if (numEPs > 0)
                {
                    USB_INTERFACE_DESCRIPTOR interfaceDesc;

                    WdfUsbInterfaceGetDescriptor(usbInterface, 0, &interfaceDesc);
                    ULONG ifProtocol = (ULONG)interfaceDesc.bInterfaceProtocol |
                        ((ULONG)interfaceDesc.bInterfaceClass) << 8 |
                        ((ULONG)interfaceDesc.bAlternateSetting) << 16 |
                        ((ULONG)interfaceDesc.bInterfaceNumber) << 24;
                    QDBREG_SetDriverRegistryDword(Device, VEN_DEV_PROTOC, ifProtocol);
                    QDBPNP_SetFunctionProtocol(Device, interfaceDesc.bInterfaceProtocol);
                }

                switch (numPipes)
                {
                    case 1:  // TRACE or DPL
                    {
                        if (bTraceFound == TRUE)
                        {
                            QDB_DbgPrint
                            (
                                QDB_DBG_MASK_CONTROL,
                                QDB_DBG_LEVEL_DETAIL,
                                ("<%s> TRACE/DPL found, skip IF%d\n", pDevContext->PortName, i)
                            );
                            break;
                        }
                        // varify if the EP is bulk IN
                        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfoTrace);
                        pDevContext->TraceIN = WdfUsbInterfaceGetConfiguredPipe
                        (
                            usbInterface,
                            0, // pipe index
                            &pipeInfoTrace
                        );
                        if (pDevContext->TraceIN == NULL)
                        {
                            QDB_DbgPrint
                            (
                                QDB_DBG_MASK_CONTROL,
                                QDB_DBG_LEVEL_ERROR,
                                ("<%s> failed to get TraceIN\n", pDevContext->PortName)
                            );
                            break;
                        }

                        QDB_DbgPrint
                        (
                            QDB_DBG_MASK_CONTROL,
                            QDB_DBG_LEVEL_ERROR,
                            ("<%s> PipeInfo: MaxPktSz %d EP 0x%X Interval %d Index %d Type %d MaxXfrSz %d\n",
                            pDevContext->PortName,
                            pipeInfoTrace.MaximumPacketSize,
                            pipeInfoTrace.EndpointAddress,
                            pipeInfoTrace.Interval,
                            pipeInfoTrace.SettingIndex,
                            pipeInfoTrace.PipeType,
                            pipeInfoTrace.MaximumTransferSize
                        )
                        );

                        if ((pipeInfoTrace.PipeType != WdfUsbPipeTypeBulk) ||
                            ((pipeInfoTrace.EndpointAddress & 0x80) == 0))
                        {
                            QDB_DbgPrint
                            (
                                QDB_DBG_MASK_CONTROL,
                                QDB_DBG_LEVEL_ERROR,
                                ("<%s> invalid TraceIN (EP 0x%X)\n", pDevContext->PortName,
                                pipeInfoTrace.EndpointAddress)
                            );
                            break;
                        }
                        bTraceFound = TRUE;

                        // disable USB transfer length check
                        WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pDevContext->TraceIN);
                        break;
                    }
                    case 2: // DEBUG
                    {
                        if (bDebugFound == TRUE)
                        {
                            QDB_DbgPrint
                            (
                                QDB_DBG_MASK_CONTROL,
                                QDB_DBG_LEVEL_DETAIL,
                                ("<%s> DEBUG found, skip IF%d\n", pDevContext->PortName, i)
                            );
                            break;
                        }
                        // verify the EPs are IN and OUT
                        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfoDebug0);
                        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfoDebug1);
                        pipe0 = WdfUsbInterfaceGetConfiguredPipe(usbInterface, 0, &pipeInfoDebug0);
                        pipe1 = WdfUsbInterfaceGetConfiguredPipe(usbInterface, 1, &pipeInfoDebug1);
                        if ((pipe0 == NULL) || (pipe1 == NULL))
                        {
                            QDB_DbgPrint
                            (
                                QDB_DBG_MASK_CONTROL,
                                QDB_DBG_LEVEL_ERROR,
                                ("<%s> failed to get DebugIN/DebugOUT (0x%p/0x%p)\n",
                                pDevContext->PortName, pipe0, pipe1)
                            );
                            break;
                        }

                        QDB_DbgPrint
                        (
                            QDB_DBG_MASK_CONTROL,
                            QDB_DBG_LEVEL_ERROR,
                            ("<%s> PipeInfo[0]: MaxPktSz %d EP 0x%X Interval %d Index %d Type %d MaxXfrSz %d\n",
                            pDevContext->PortName,
                            pipeInfoDebug0.MaximumPacketSize,
                            pipeInfoDebug0.EndpointAddress,
                            pipeInfoDebug0.Interval,
                            pipeInfoDebug0.SettingIndex,
                            pipeInfoDebug0.PipeType,
                            pipeInfoDebug0.MaximumTransferSize
                        )
                        );
                        QDB_DbgPrint
                        (
                            QDB_DBG_MASK_CONTROL,
                            QDB_DBG_LEVEL_ERROR,
                            ("<%s> PipeInfo[1]: MaxPktSz %d EP 0x%X Interval %d Index %d Type %d MaxXfrSz %d\n",
                            pDevContext->PortName,
                            pipeInfoDebug1.MaximumPacketSize,
                            pipeInfoDebug1.EndpointAddress,
                            pipeInfoDebug1.Interval,
                            pipeInfoDebug1.SettingIndex,
                            pipeInfoDebug1.PipeType,
                            pipeInfoDebug1.MaximumTransferSize
                        )
                        );

                        // pipes have to be BULK
                        if ((pipeInfoDebug0.PipeType != WdfUsbPipeTypeBulk) ||
                            (pipeInfoDebug1.PipeType != WdfUsbPipeTypeBulk))
                        {
                            QDB_DbgPrint
                            (
                                QDB_DBG_MASK_CONTROL,
                                QDB_DBG_LEVEL_ERROR,
                                ("<%s> invalid DebugIN/DebugOUT (EP 0x%X/0x%X)\n", pDevContext->PortName,
                                pipeInfoDebug0.EndpointAddress, pipeInfoDebug1.EndpointAddress)
                            );
                            break;
                        }
                        // pipes have to be IN and OUT
                        if ((pipeInfoDebug0.EndpointAddress & 0x80) == 0)
                        {
                            if ((pipeInfoDebug1.EndpointAddress & 0x80) == 0)
                            {
                                break;
                            }
                            bDebugFound = TRUE;
                            // pipe0 is OUT, pipe1 is IN
                            pDevContext->DebugIN = pipe1;
                            pDevContext->DebugOUT = pipe0;
                        }
                        else
                        {
                            if ((pipeInfoDebug1.EndpointAddress & 0x80) != 0)
                            {
                                break;
                            }
                            bDebugFound = TRUE;
                            // pipe0 is IN, pipe1 is OUT
                            pDevContext->DebugIN = pipe0;
                            pDevContext->DebugOUT = pipe1;
                        }

                        // disable USB transfer length check
                        WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pDevContext->DebugIN);
                        WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pDevContext->DebugOUT);

                        break;
                    }

                    default:
                    {
                        QDB_DbgPrint
                        (
                            QDB_DBG_MASK_CONTROL,
                            QDB_DBG_LEVEL_DETAIL,
                            ("<%s> Unexpected number of pipes: %d from IF%d\n", pDevContext->PortName, numPipes, i)
                        );
                        break;
                    }
                }  // switch
            }  // for
        }
        else
        {
            ntStatus = STATUS_UNSUCCESSFUL;

            QDB_DbgPrint
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBPNP_SelectInterfaces: no interface found\n", pDevContext->PortName)
            );
        }
    }
    else
    {
        QDB_DbgPrint
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBPNP_SelectInterfaces: WdfUsbTargetDeviceSelectConfig failed 0x%x\n",
            pDevContext->PortName, ntStatus)
        );
    }

    if ((bTraceFound == FALSE) && (bDebugFound == FALSE))
    {
        ntStatus = STATUS_UNSUCCESSFUL;
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_INFO,
        ("<%s> QDBPNP_SelectInterfaces: ST 0x%x TraceIN 0x%p(EP%x) Debug0 0x%p(EP%x) Debug1 0x%p(EP%x)\n",
        pDevContext->PortName, ntStatus,
        pDevContext->TraceIN, pipeInfoTrace.EndpointAddress,
        pDevContext->DebugIN, pipeInfoDebug0.EndpointAddress,
        pDevContext->DebugOUT, pipeInfoDebug1.EndpointAddress
    )
    );
    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBPNP_SelectInterfaces: Device 0x%p(0x%x) (TRACE %d, DEBUG %d)\n", pDevContext->PortName,
        Device, ntStatus, bTraceFound, bDebugFound)
    );

    return ntStatus;
}

/****************************************************************************
 *
 * function: QDBPNP_CreateSymbolicName
 *
 * purpose:  Constructs a symbolic link name from the device description
 *           and driver key instance, creates the symbolic link, and writes
 *           the FriendlyName to the registry.
 *
 * arguments:Device = WDF device handle
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_CreateSymbolicName(WDFDEVICE Device)
{
    PDEVICE_CONTEXT          pDevContext;
    NTSTATUS                 nts;
    WCHAR                    nameBuffer[MAX_NAME_LEN] = {0};
    UNICODE_STRING           symbolicLink;
    PWCHAR                   pInstanceId = NULL;
    WDF_DEVICE_PROPERTY_DATA propertyData;
    WDFMEMORY                driverKeyMemory = NULL;
    WDFMEMORY                deviceDescMemory = NULL;
    PWCHAR                   driverKey = NULL;
    PWCHAR                   deviceDesc = NULL;
    PWCHAR                   friendlyName = NULL;

    pDevContext = QdbDeviceGetContext(Device);

    QDB_DbgPrintG
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBPNP_CreateSymbolicName: Device 0x%p\n", pDevContext->PortName, Device)
    );

    // Step 1: query DeviceDescription
    nts = WdfDeviceAllocAndQueryProperty(Device, DevicePropertyDeviceDescription, NonPagedPoolNx, WDF_NO_OBJECT_ATTRIBUTES, &deviceDescMemory);
    if (!NT_SUCCESS(nts))
    {
        QDB_DbgPrintG
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> <--QDBPNP_CreateSymbolicName: read DeviceDesc failed 0x%x\n", pDevContext->PortName, nts)
        );
        return nts;
    }
    deviceDesc = WdfMemoryGetBuffer(deviceDescMemory, NULL);
    QDB_DbgPrintG
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_INFO,
        ("<%s> <--QDBPNP_CreateSymbolicName: deviceDesc = %ws\n", pDevContext->PortName, deviceDesc)
    );

    // Step 2: query DriverKeyName, extract instance suffix (last segment after L'\\')
    nts = WdfDeviceAllocAndQueryProperty(Device, DevicePropertyDriverKeyName, NonPagedPoolNx, WDF_NO_OBJECT_ATTRIBUTES, &driverKeyMemory);
    if (NT_SUCCESS(nts))
    {
        driverKey = WdfMemoryGetBuffer(driverKeyMemory, NULL);
        pInstanceId = wcsrchr(driverKey, L'\\');
        if (pInstanceId != NULL)
        {
            pInstanceId = pInstanceId + 1;  // points past last '\\', e.g. "0006"
            QDB_DbgPrintG
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_INFO,
                ("<%s> <--QDBPNP_CreateSymbolicName: driverKey = %ws\n", pDevContext->PortName, driverKey)
            );
        }
    }

    if (pInstanceId != NULL)
    {
        // Step 3: set PortName to QDB_DBG_NAME_PREFIX + instanceId (e.g. "qdb0006")
        nts = RtlStringCbPrintfA(pDevContext->PortName, sizeof(pDevContext->PortName),
            "%s%ws", QDB_DBG_NAME_PREFIX, pInstanceId);
        QDB_DbgPrintG
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_INFO,
            ("<%s> QDBPNP_CreateSymbolicName: assigned new portName, status: 0x%x\n", pDevContext->PortName, nts)
        );

        // Step 4: build friendly name and symbolic link name
        nts = RtlStringCbPrintfW(nameBuffer, sizeof(nameBuffer), DEVICE_LINK_NAME_PATH L"%s (%s)", deviceDesc, pInstanceId);
    }
    else
    {
        nts = RtlStringCbPrintfW(nameBuffer, sizeof(nameBuffer), DEVICE_LINK_NAME_PATH L"%s", deviceDesc);
    }

    // Step 5: delete old memories
    if (driverKeyMemory != NULL)
    {
        WdfObjectDelete(driverKeyMemory);
    }
    if (deviceDescMemory != NULL)
    {
        WdfObjectDelete(deviceDescMemory);
    }

    // nameBuffer constructed successfully
    if (NT_SUCCESS(nts))
    {
        friendlyName = nameBuffer + wcslen(DEVICE_LINK_NAME_PATH);
        QDB_DbgPrintG
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_INFO,
            ("<%s> QDBPNP_CreateSymbolicName: friendlyName = %ws\n", pDevContext->PortName, friendlyName)
        );

        // Step 6: create symbolic link path = "\??\" + friendlyName
        RtlInitUnicodeString(&symbolicLink, nameBuffer);
        nts = WdfDeviceCreateSymbolicLink(Device, &symbolicLink);
        if (NT_SUCCESS(nts))
        {
            QDB_DbgPrintG
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_INFO,
                ("<%s> QDBPNP_CreateSymbolicName: symbolicLink = %ws\n", pDevContext->PortName, nameBuffer)
            );
        }
        else
        {
            QDB_DbgPrintG
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_INFO,
                ("<%s> QDBPNP_CreateSymbolicName: WdfDeviceCreateSymbolicLink failed status 0x%x\n", pDevContext->PortName, nts)
            );
        }

        // Step 7: update DEVPKEY_Device_FriendlyName
        WDF_DEVICE_PROPERTY_DATA_INIT(&propertyData, &DEVPKEY_Device_FriendlyName);
        propertyData.Lcid = 0;
        propertyData.Flags = 0;
        NTSTATUS ret = WdfDeviceAssignProperty(Device, &propertyData, DEVPROP_TYPE_STRING,
            (ULONG)((wcslen(friendlyName) + 1) * sizeof(WCHAR)), friendlyName);
        if (!NT_SUCCESS(ret))
        {
            QDB_DbgPrintG
            (
                QDB_DBG_MASK_CONTROL,
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBPNP_CreateSymbolicName: friendlyName assign failed 0x%x\n", pDevContext->PortName, ret)
            );
        }
    }
    else
    {
        QDB_DbgPrintG
        (
            QDB_DBG_MASK_CONTROL,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBPNP_CreateSymbolicName: nameBuffer built failed 0x%x\n", pDevContext->PortName, nts)
        );
    }

    QDB_DbgPrintG
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBPNP_CreateSymbolicName: ST 0x%x\n", pDevContext->PortName, nts)
    );

    return nts;
}  // QDBPNP_CreateSymbolicName

/****************************************************************************
 *
 * function: QDBPNP_EnableSelectiveSuspend
 *
 * purpose:  Configures USB selective suspend with a 200 ms idle timeout
 *           using WDF power policy idle settings.
 *
 * arguments:Device = WDF device handle
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_EnableSelectiveSuspend(WDFDEVICE Device)
{
    PDEVICE_CONTEXT pDevContext;
    NTSTATUS        nts = STATUS_UNSUCCESSFUL;
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idleSettings;

    pDevContext = QdbDeviceGetContext(Device);

    QDB_DbgPrint
    (
        0, 0,
        ("<%s> -->QDBPNP_EnableSelectiveSuspend: Device 0x%p\n", pDevContext->PortName, Device)
    );

    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idleSettings, IdleUsbSelectiveSuspend);
    idleSettings.IdleTimeout = 200; // 200ms

    nts = WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
    if (!NT_SUCCESS(nts))
    {
        QDB_DbgPrint
        (
            0, 0,
            ("<%s> QDBPNP_EnableSelectiveSuspend: AssignS0 failed, device 0x%p(ST 0x%x)\n", pDevContext->PortName, Device, nts)
        );
        return nts;
    }


    QDB_DbgPrint
    (
        0, 0,
        ("<%s> <--QDBPNP_EnableSelectiveSuspend: Device 0x%p (0x%x)\n", pDevContext->PortName, Device, nts)
    );

    return nts;
}  // QDBPNP_EnableSelectiveSuspend

/****************************************************************************
 *
 * function: QDBPNP_GetParentDeviceName
 *
 * purpose:  Sends IOCTL_QCDEV_GET_PARENT_DEV_NAME synchronously to the
 *           I/O target to retrieve the parent device name, then saves it
 *           to the registry.
 *
 * arguments:Device = WDF device handle
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QDBPNP_GetParentDeviceName(PDEVICE_CONTEXT pDevContext)
{
    NTSTATUS   ntStatus;
    WCHAR      parentDevName[MAX_NAME_LEN] = {0};
    ULONG_PTR  bytesReturned = 0;
    WDF_MEMORY_DESCRIPTOR outputMemory;

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBPNP_GetParentDeviceName\n", pDevContext->PortName)
    );

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputMemory, parentDevName, sizeof(parentDevName));

    ntStatus = WdfIoTargetSendIoctlSynchronously
    (
        pDevContext->MyIoTarget,
        NULL,
        IOCTL_QCDEV_GET_PARENT_DEV_NAME,
        NULL,
        &outputMemory,
        NULL,
        &bytesReturned
    );

    if (NT_SUCCESS(ntStatus) && bytesReturned > 0)
    {
        QDBREG_SetDriverRegistryStringW(pDevContext->MyDevice, VEN_DEV_PARENT, parentDevName);
    }

    QDB_DbgPrint
    (
        QDB_DBG_MASK_CONTROL,
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--- QDBPNP_GetParentDeviceName: ST 0x%x bytesReturned %Iu\n",
        pDevContext->PortName, ntStatus, bytesReturned)
    );

    return ntStatus;
}  // QDBPNP_GetParentDeviceName
