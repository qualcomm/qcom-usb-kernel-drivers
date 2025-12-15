/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "QCMAIN.h"
#include "QCDSP.h"
#include "QCSER.h"
#include "QCPNP.h"
#include "QCUTILS.h"

#ifdef EVENT_TRACING
#include "QCWPP.h"
#include "QCDSP.tmh"
#endif

void QCDSP_EvtIoDeviceControl
(
    WDFQUEUE    Queue,
    WDFREQUEST  Request,
    size_t      OutputBufferLength,
    size_t      InputBufferLength,
    ULONG       IoControlCode
)
{
    NTSTATUS        status = STATUS_SUCCESS;
    ULONG_PTR       information = 0;
    PDEVICE_CONTEXT pDevContext = QCDevGetContext(WdfIoQueueGetDevice(Queue));

    QCUTIL_PrintNewLine(QCSER_DBG_MASK_CONTROL, QCSER_DBG_LEVEL_TRACE, pDevContext);
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> CIRP: QCDSP_EvtIoDeviceControl request: 0x%p, code: 0x%x\n", pDevContext->PortName, Request, IoControlCode)
    );

    switch (IoControlCode)
    {
    case IOCTL_SERIAL_GET_STATS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_STATS buffer outsize: %llu, minsize: %llu\n", pDevContext->PortName, OutputBufferLength, sizeof(SERIALPERF_STATS))
        );
        status = QCSER_GetStats(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIALPERF_STATS);
        }
        break;

    case IOCTL_SERIAL_CLEAR_STATS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_CLEAR_STATS\n", pDevContext->PortName)
        );
        status = QCSER_ClearStats(pDevContext);
        break;

    case IOCTL_SERIAL_GET_PROPERTIES:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_PROPERTIES buffer outsize: %llu, minsize: %llu\n", pDevContext->PortName, OutputBufferLength, sizeof(SERIAL_COMMPROP))
        );
        status = QCSER_GetProperties(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_COMMPROP);
        }
        break;

    case IOCTL_SERIAL_GET_MODEMSTATUS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_MODEMSTATUS\n", pDevContext->PortName)
        );
        status = QCSER_GetModemStatus(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(ULONG);
        }
        break;

    case IOCTL_SERIAL_GET_COMMSTATUS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_COMMSTATUS\n", pDevContext->PortName)
        );
        status = QCSER_GetCommStatus(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_STATUS);
        }
        break;

    case IOCTL_SERIAL_RESET_DEVICE:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_RESET_DEVICE\n", pDevContext->PortName)
        );
        status = QCSER_ResetDevice(pDevContext);
        break;

    case IOCTL_SERIAL_PURGE:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_PURGE\n", pDevContext->PortName)
        );
        status = QCSER_Purge(pDevContext, Request, InputBufferLength);
        break;

    case IOCTL_SERIAL_LSRMST_INSERT:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_LSRMST_INSERT\n", pDevContext->PortName)
        );
        status = QCSER_LsrMstInsert(pDevContext);
        if (NT_SUCCESS(status))
        {
            information = sizeof(UCHAR);
        }
        break;

    case IOCTL_SERIAL_SET_BAUD_RATE:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_BAUD_RATE buffer input size: %llu, required size: %llu\n", pDevContext->PortName, InputBufferLength, sizeof(SERIAL_BAUD_RATE))
        );
        status = QCSER_SetBaudRate(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_BAUD_RATE);
        }
        break;

    case IOCTL_SERIAL_GET_BAUD_RATE:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_BAUD_RATE buffer outsize: %llu, minsize: %llu\n", pDevContext->PortName, OutputBufferLength, sizeof(SERIAL_BAUD_RATE))
        );
        status = QCSER_GetBaudRate(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_BAUD_RATE);
        }
        break;

    case IOCTL_SERIAL_SET_QUEUE_SIZE:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_QUEUE_SIZE\n", pDevContext->PortName)
        );
        status = QCSER_SetQueueSize(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_QUEUE_SIZE);
        }
        break;

    case IOCTL_SERIAL_GET_HANDFLOW:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_HANDFLOW buffer outsize: %llu, minsize: %llu\n", pDevContext->PortName, OutputBufferLength, sizeof(SERIAL_HANDFLOW))
        );
        status = QCSER_GetHandflow(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_HANDFLOW);
        }
        break;

    case IOCTL_SERIAL_SET_HANDFLOW:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_HANDFLOW buffer input size: %llu, required size: %llu\n", pDevContext->PortName, InputBufferLength, sizeof(SERIAL_HANDFLOW))
        );
        status = QCSER_SetHandflow(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_HANDFLOW);
        }
        break;

    case IOCTL_SERIAL_GET_LINE_CONTROL:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_LINE_CONTROL buffer outsize: %llu, minsize: %llu\n", pDevContext->PortName, OutputBufferLength, sizeof(SERIAL_LINE_CONTROL))
        );
        status = QCSER_GetLineControl(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_LINE_CONTROL);
        }
        break;

    case IOCTL_SERIAL_SET_LINE_CONTROL:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_LINE_CONTROL buffer input size: %llu, required size: %llu\n", pDevContext->PortName, InputBufferLength, sizeof(SERIAL_LINE_CONTROL))
        );
        status = QCSER_SetLineControl(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_LINE_CONTROL);
        }
        break;

    case IOCTL_SERIAL_SET_BREAK_ON:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_BREAK_ON\n", pDevContext->PortName)
        );
        status = QCSER_SetBreak(pDevContext, 0xFFFF);
        break;

    case IOCTL_SERIAL_SET_BREAK_OFF:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_BREAK_OFF\n", pDevContext->PortName)
        );
        status = QCSER_SetBreak(pDevContext, 0);
        break;

    case IOCTL_SERIAL_GET_TIMEOUTS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_TIMEOUTS buffer outsize: %llu, minsize: %llu\n", pDevContext->PortName, OutputBufferLength, sizeof(SERIAL_TIMEOUTS))
        );
        status = QCSER_GetTimeout(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_TIMEOUTS);
        }
        break;

    case IOCTL_SERIAL_SET_TIMEOUTS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_TIMEOUTS buffer input size: %llu, required size: %llu\n", pDevContext->PortName, InputBufferLength, sizeof(SERIAL_TIMEOUTS))
        );
        status = QCSER_SetTimeout(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_TIMEOUTS);
        }
        break;

    case IOCTL_SERIAL_IMMEDIATE_CHAR:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_IMMEDIATE_CHAR\n", pDevContext->PortName)
        );
        status = QCSER_ImmediateChar(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(UCHAR);
        }
        break;

    case IOCTL_SERIAL_XOFF_COUNTER:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_XOFF_COUNTER\n", pDevContext->PortName)
        );
        status = QCSER_XoffCounter(pDevContext);
        break;

    case IOCTL_SERIAL_SET_DTR:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_DTR\n", pDevContext->PortName)
        );
        status = QCSER_SerialSetDtr(pDevContext);
        break;

    case IOCTL_SERIAL_CLR_DTR:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_CLR_DTR\n", pDevContext->PortName)
        );
        status = QCSER_SerialClrDtr(pDevContext);
        break;

    case IOCTL_SERIAL_SET_RTS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_RTS\n", pDevContext->PortName)
        );
        status = QCSER_SerialSetRts(pDevContext);
        break;

    case IOCTL_SERIAL_CLR_RTS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_RTS\n", pDevContext->PortName)
        );
        status = QCSER_SerialClrRts(pDevContext);
        break;

    case IOCTL_SERIAL_GET_DTRRTS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_DTRRTS buffer outsize: %llu, minsize: %llu\n", pDevContext->PortName, OutputBufferLength, sizeof(ULONG))
        );
        status = QCSER_GetDtrRts(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(ULONG);
        }
        break;

    case IOCTL_SERIAL_SET_XON:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_XON\n", pDevContext->PortName)
        );
        status = QCSER_SetXon(pDevContext);
        break;

    case IOCTL_SERIAL_SET_XOFF:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_XOFF\n", pDevContext->PortName)
        );
        status = QCSER_SetXoff(pDevContext);
        break;

    case IOCTL_SERIAL_GET_WAIT_MASK:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_WAIT_MASK\n", pDevContext->PortName)
        );
        status = QCSER_GetWaitMask(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(ULONG);
        }
        break;

    case IOCTL_SERIAL_SET_WAIT_MASK:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_WAIT_MASK\n", pDevContext->PortName)
        );
        status = QCSER_SetWaitMask(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(ULONG);
        }
        break;

    case IOCTL_SERIAL_WAIT_ON_MASK:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_WAIT_ON_MASK\n", pDevContext->PortName)
        );
        status = QCSER_WaitOnMask(pDevContext, Request, OutputBufferLength);
        if (status == STATUS_PENDING)
        {
            // wait for events to complete this request
            KeSetEvent(&pDevContext->ReadThreadScanWaitMaskEvent, IO_NO_INCREMENT, FALSE);
            goto exit;
        }
        break;

    case IOCTL_SERIAL_GET_CHARS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_GET_CHARS buffer outsize: %llu, minsize: %llu\n", pDevContext->PortName, OutputBufferLength, sizeof(SERIAL_CHARS))
        );
        status = QCSER_GetChars(pDevContext, Request, OutputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_CHARS);
        }
        break;

    case IOCTL_SERIAL_SET_CHARS:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_SERIAL_SET_CHARS buffer input size: %llu, required size: %llu\n", pDevContext->PortName, InputBufferLength, sizeof(SERIAL_CHARS))
        );
        status = QCSER_SetChars(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(SERIAL_CHARS);
        }
        break;

    case IOCTL_QCUSB_SET_DBG_UMSK:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_QCUSB_SET_DBG_UMSK\n", pDevContext->PortName)
        );
        status = QCSER_SetDebugMask(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(ULONG);
        }
        break;

    case IOCTL_QCOMSER_WAIT_NOTIFY:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_QCOMSER_WAIT_NOTIFY\n", pDevContext->PortName)
        );
        status = QCSER_WaitOnDeviceRemoval(pDevContext, Request, OutputBufferLength);
        if (status == STATUS_PENDING)
        {
            goto exit;
        }
        break;

    case IOCTL_QCUSB_QCDEV_NOTIFY:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_QCUSB_QCDEV_NOTIFY\n", pDevContext->PortName)
        );
        status = QCSER_WaitOnDeviceRemoval(pDevContext, Request, OutputBufferLength);
        if (status == STATUS_PENDING)
        {
            goto exit;
        }
        break;

    case IOCTL_QCOMSER_DRIVER_ID:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_QCOMSER_DRIVER_ID\n", pDevContext->PortName)
        );
        status = QCSER_GetDriverGUIDString(pDevContext, Request, OutputBufferLength);
        break;

    case IOCTL_QCSER_GET_SERVICE_KEY:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_QCSER_GET_SERVICE_KEY\n", pDevContext->PortName)
        );
        status = QCSER_GetServiceKey(pDevContext, Request, OutputBufferLength);
        break;

    case IOCTL_QCDEV_REQUEST_DEVICEID:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_QCDEV_REQUEST_DEVICEID\n", pDevContext->PortName)
        );
        // do nothing but forward request to lower level driver
        status = QCSER_GetDeviceId(pDevContext, Request);
        if (status == STATUS_PENDING)
        {
            goto exit;
        }
        break;

#ifdef QCUSB_MUX_PROTOCOL
    case IOCTL_QCUSB_SET_SESSION_TOTAL:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_QCUSB_SET_SESSION_TOTAL\n", pDevContext->PortName)
        );
        status = QCSER_SetSessionTotal(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(LONGLONG);
        }
        break;

    case IOCTL_VIUSB_CONFIG_DEVICE:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl IOCTL_VIUSB_CONFIG_DEVICE\n", pDevContext->PortName)
        );
        status = QCSER_ViUsbConfigDevice(pDevContext, Request, InputBufferLength);
        if (NT_SUCCESS(status))
        {
            information = sizeof(VI_CONFIG);
        }
        break;
#endif // QCUSB_MUX_PROTOCOL

    case IOCTL_QCUSB_SYSTEM_POWER:
    case IOCTL_QCUSB_DEVICE_POWER:
    case IOCTL_QCUSB_SEND_CONTROL:
    case IOCTL_QCUSB_CDC_SEND_ENCAPSULATED:
    default:
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCDSP_EvtIoDeviceControl UNSUPPORTED IOCTL code: 0x%x\n", pDevContext->PortName, IoControlCode)
        );
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, information);

exit:
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCDSP_EvtIoDeviceControl request 0x%p completed with status: 0x%x, infomation: %llu\n", pDevContext->PortName, Request, status, information)
    );
}

void QCDSP_EvtIoReadQueueReady
(
    WDFQUEUE   Queue,
    WDFCONTEXT Context
)
{
    UNREFERENCED_PARAMETER(Queue);
    PDEVICE_CONTEXT pDevContext = Context;

    QCUTIL_PrintNewLine(QCSER_DBG_MASK_CONTROL, QCSER_DBG_LEVEL_TRACE, pDevContext);
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        (QCSER_DBG_LEVEL_TRACE),
        ("<%ws> RIRP: QCDSP_EvtIoReadQueueReady device function: %lu\n", pDevContext->PortName, pDevContext->DeviceFunction)
    );

#ifdef QCUSB_MUX_PROTOCOL
    if (pDevContext->DeviceFunction == QCUSB_DEV_FUNC_VI)
    {
        KeSetEvent(&pDevContext->SessionTotalSetEvent, IO_NO_INCREMENT, FALSE);
    }
    else
#endif
    {
        KeSetEvent(&pDevContext->ReadRequestArriveEvent, IO_NO_INCREMENT, FALSE);
    }
}

void QCDSP_EvtIoWriteQueueReady
(
    WDFQUEUE   Queue,
    WDFCONTEXT Context
)
{
    UNREFERENCED_PARAMETER(Queue);
    PDEVICE_CONTEXT pDevContext = Context;

    QCUTIL_PrintNewLine(QCSER_DBG_MASK_CONTROL, QCSER_DBG_LEVEL_TRACE, pDevContext);
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_WRITE,
        (QCSER_DBG_LEVEL_TRACE),
        ("<%ws> WIRP: QCDSP_EvtIoWriteQueueReady\n", pDevContext->PortName)
    );

    KeSetEvent(&pDevContext->WriteRequestArriveEvent, IO_NO_INCREMENT, FALSE);
}