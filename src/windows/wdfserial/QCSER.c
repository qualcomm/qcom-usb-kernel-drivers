/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "QCSER.h"
#include "QCUSB.h"
#include "QCUTILS.h"

#ifdef EVENT_TRACING
#include "QCWPP.h"
#include "QCSER.tmh"
#endif

VOID QCSER_InitUartStateFromModem
(
    PDEVICE_CONTEXT pDevContext
)
{
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCSER_InitUartStateFromModem current uart state: %hu\n", pDevContext->PortName, pDevContext->CurrUartState)
    );

    // newUartState = modem bits from UART
    USHORT newUartState = pDevContext->CurrUartState & US_BITS_MODEM_RAW;
    // oldUartState = non-UART bits already set
    USHORT oldUartState = pDevContext->CurrUartState & ~US_BITS_MODEM;
    // newUartState = all bits now set
    newUartState |= oldUartState;
    // CurrUartState (before change) cleared, so all active bits generate events
    pDevContext->CurrUartState = 0;
    QCSER_ProcessNewUartState(pDevContext, newUartState, 0xffff);
}

VOID QCSER_ProcessNewUartState
(
    PDEVICE_CONTEXT pDevContext,
    USHORT usNewUartState,
    USHORT usBitsMask
)
{
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCSER_ProcessNewUartState new uart state: 0x%x, bit mask: 0x%x\n", pDevContext->PortName, usNewUartState, usBitsMask)
    );

    UCHAR    ucModemStatusOld, ucModemStatusNew;
    USHORT   usBitsNew, usBitsOld, usBitsEvent;
    ULONG    ulNewEvent;
    NTSTATUS status;

    usBitsNew = usBitsOld = pDevContext->CurrUartState;

    // put new state into current state
    usBitsNew &= ~usBitsMask;
    usBitsNew |= (usNewUartState & usBitsMask);

    if (usBitsNew & SERIAL_EV_DSR)
    {
        usBitsNew |= SERIAL_EV_CTS;
    }
    else
    {
        usBitsNew &= ~SERIAL_EV_CTS;
    }

    // check for modem change event bits
    usBitsEvent = (usBitsNew ^ usBitsOld) & US_BITS_MODEM;

    if (usBitsEvent)
    {
        ucModemStatusOld = pDevContext->ModemStatusReg;
        ucModemStatusNew = 0;

        if (usBitsEvent & SERIAL_EV_CTS)
            ucModemStatusNew |= SERIAL_MSR_DCTS;
        if (usBitsNew & SERIAL_EV_CTS)
            ucModemStatusNew |= SERIAL_MSR_CTS;

        if (usBitsEvent & SERIAL_EV_DSR)
            ucModemStatusNew |= SERIAL_MSR_DDSR;
        if (usBitsNew & SERIAL_EV_DSR)
            ucModemStatusNew |= SERIAL_MSR_DSR;

        if (usBitsEvent & SERIAL_EV_RING)
            ucModemStatusNew |= SERIAL_MSR_TERI;
        if (usBitsNew & SERIAL_EV_RING)
            ucModemStatusNew |= SERIAL_MSR_RI;

        if (usBitsEvent & SERIAL_EV_RLSD)
            ucModemStatusNew |= SERIAL_MSR_DDCD;
        if (usBitsNew & SERIAL_EV_RLSD)
            ucModemStatusNew |= SERIAL_MSR_DCD;

        pDevContext->ModemStatusReg = ucModemStatusNew;
    }

    // check for existing event bits
    usBitsMask = usBitsNew;                // existing events
    usBitsMask &= ~US_BITS_MODEM;          // not including modem events
    usBitsEvent |= usBitsMask;             // new non-modem bits, changed modem bits

    // update current state
    pDevContext->CurrUartState = usBitsNew;

    // check for flowin control processing
    if (usBitsEvent & SERIAL_EV_PERR) // input buffer full
    {
        pDevContext->CurrUartState &= ~SERIAL_EV_PERR;
        usBitsEvent &= ~SERIAL_EV_PERR;
    }

    ulNewEvent = (ULONG)usBitsEvent;
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCSER_ProcessNewUartState ulNewEvent: 0x%x, WaitMask: 0x%x, CurrUartState:0x%x\n",
            pDevContext->PortName, ulNewEvent, pDevContext->WaitMask, pDevContext->CurrUartState)
    );

    if (ulNewEvent & pDevContext->WaitMask)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCSER_ProcessNewUartState completing WaitOnMaskRequest ulNewEvent: 0x%x, WaitMask: 0x%x\n",
                pDevContext->PortName, ulNewEvent, pDevContext->WaitMask)
        );

        pDevContext->CurrUartState &= US_BITS_MODEM;
        ulNewEvent &= pDevContext->WaitMask;
        QCSER_CompleteWomRequest(pDevContext, STATUS_SUCCESS, pDevContext->WaitMask);
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCSER_ProcessNewUartState completed with current uart state: %hu\n", pDevContext->PortName, pDevContext->CurrUartState)
    );
}

NTSTATUS QCSER_SetModemConfig
(
    PDEVICE_CONTEXT pDevContext,
    PMODEM_INFO newModemInfo
)
{
    NTSTATUS status = STATUS_SUCCESS;

    if (newModemInfo == NULL)
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else if (pDevContext->UsbDeviceType == DEVICETYPE_CDC)
    {
        if (newModemInfo->ucDataBits == 2)
        {
            newModemInfo->ucDataBits = 7;
        }
        else
        {
            newModemInfo->ucDataBits = 8;
        }
        ULONG byteTransferred = 0;
        WDF_USB_CONTROL_SETUP_PACKET controlSetupPacket;
        WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS
        (
            &controlSetupPacket,
            BmRequestHostToDevice,
            BmRequestToInterface,
            CDC_SET_LINE_CODING,
            0,
            pDevContext->InterfaceIndex
        );
        controlSetupPacket.Packet.wLength = UART_CONFIG_SIZE;
        WDF_MEMORY_DESCRIPTOR memoryDescriptor;
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER
        (
            &memoryDescriptor,
            (PVOID)newModemInfo,
            sizeof(MODEM_INFO)
        );
        status = WdfUsbTargetDeviceSendControlTransferSynchronously
        (
            pDevContext->UsbDevice,
            NULL,
            WDF_NO_SEND_OPTIONS,
            &controlSetupPacket,
            &memoryDescriptor,
            &byteTransferred
        );
    }
    else if (pDevContext->UsbDeviceType == DEVICETYPE_SERIAL)
    {
        RtlCopyMemory(&pDevContext->ModemInfo, newModemInfo, sizeof(MODEM_INFO));
    }

    if (NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCSER_SetModemConfig DteRate: %lu, Parity: %u, StopBits: %u, DataBits: %u\n", pDevContext->PortName,
                newModemInfo->ulDteRate, newModemInfo->ucParityType, newModemInfo->ucStopBit, newModemInfo->ucDataBits)
        );
    }
    else
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCSER_SetModemConfig FAILED device type: %d, status: 0x%x\n", pDevContext->PortName, pDevContext->UsbDeviceType, status)
        );
    }

    return status;
}

NTSTATUS QCSER_GetModemConfig
(
    PDEVICE_CONTEXT pDevContext,
    PMODEM_INFO outModemInfo
)
{
    NTSTATUS status = STATUS_SUCCESS;

    if (outModemInfo == NULL)
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else if (pDevContext->UsbDeviceType == DEVICETYPE_CDC)
    {
        ULONG byteTransferred = 0;
        WDF_USB_CONTROL_SETUP_PACKET controlSetupPacket;
        WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS
        (
            &controlSetupPacket,
            BmRequestDeviceToHost,
            BmRequestToInterface,
            CDC_GET_LINE_CODING,
            0,
            pDevContext->InterfaceIndex
        );
        controlSetupPacket.Packet.wLength = UART_CONFIG_SIZE;
        WDF_MEMORY_DESCRIPTOR memoryDescriptor;
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER
        (
            &memoryDescriptor,
            (PVOID)outModemInfo,
            sizeof(MODEM_INFO)
        );
        status = WdfUsbTargetDeviceSendControlTransferSynchronously
        (
            pDevContext->UsbDevice,
            NULL,
            WDF_NO_SEND_OPTIONS,
            &controlSetupPacket,
            &memoryDescriptor,
            &byteTransferred
        );
        if (NT_SUCCESS(status))
        {
            if (outModemInfo->ucDataBits == 7)
            {
                outModemInfo->ucDataBits = 2;
            }
            else if (outModemInfo->ucDataBits == 8)
            {
                outModemInfo->ucDataBits = 3;
            }
            else
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CONTROL,
                    QCSER_DBG_LEVEL_ERROR,
                    ("<%ws> QCSER_GetModemConfig unexpected data bits %u\n", pDevContext->PortName, outModemInfo->ucDataBits)
                );
                outModemInfo->ucDataBits = 3;
            }
        }
        else
        {
            // restore default value
            outModemInfo->ulDteRate = 2 * 1024 * 1024;
            outModemInfo->ucParityType = 0;
            outModemInfo->ucStopBit = 0;
            outModemInfo->ucDataBits = 3;
        }
    }
    else if (pDevContext->UsbDeviceType == DEVICETYPE_SERIAL)
    {
        RtlCopyMemory(outModemInfo, &pDevContext->ModemInfo, sizeof(MODEM_INFO));
    }

    if (NT_SUCCESS(status))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_INFO,
            ("<%ws> QCSER_GetModemConfig DteRate: %lu, Parity: %u, StopBits: %u, DataBits: %u\n", pDevContext->PortName,
                outModemInfo->ulDteRate, outModemInfo->ucParityType, outModemInfo->ucStopBit, outModemInfo->ucDataBits)
        );
    }
    else
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCSER_GetModemConfig FAILED device type: %d, status: 0x%x\n", pDevContext->PortName, pDevContext->UsbDeviceType, status)
        );
    }

    return status;
}

NTSTATUS QCSER_GetStats
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;

    if (OutputBufferLength < sizeof(SERIALPERF_STATS))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        status = QCUTIL_RequestCopyFromBuffer
        (
            Request,
            &pDevContext->PerfStats,
            sizeof(SERIALPERF_STATS),
            pDevContext
        );
    }
    return status;
}

NTSTATUS QCSER_ClearStats
(
    PDEVICE_CONTEXT pDevContext
)
{
    RtlZeroMemory(&pDevContext->PerfStats, sizeof(SERIALPERF_STATS));
    return STATUS_SUCCESS;
}

NTSTATUS QCSER_GetProperties
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;
    SERIAL_COMMPROP commprop;

    if (OutputBufferLength < sizeof(SERIAL_COMMPROP))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        commprop.PacketLength = sizeof(SERIAL_COMMPROP);
        commprop.PacketVersion = 2;
        commprop.MaxRxQueue = pDevContext->UrbReadBufferSize;
        commprop.CurrentRxQueue = pDevContext->UrbReadBufferSize;
        commprop.MaxTxQueue = 0;
        commprop.CurrentTxQueue = 0;
        commprop.ServiceMask = SERIAL_SP_SERIALCOMM;
        commprop.MaxBaud = SERIAL_BAUD_USER;
        if (pDevContext->UsbDeviceType < DEVICETYPE_INVALID)
        {
            commprop.ProvSubType = SERIAL_SP_RS232;
        }
        else
        {
            commprop.ProvSubType = SERIAL_SP_UNSPECIFIED;
        }
        commprop.ProvCapabilities = SERIAL_PCF_DTRDSR |
            SERIAL_PCF_RTSCTS |
            SERIAL_PCF_CD |
            SERIAL_PCF_PARITY_CHECK/* |
            SERIAL_PCF_INTTIMEOUTS |
            SERIAL_PCF_TOTALTIMEOUTS*/;
        commprop.SettableParams = SERIAL_SP_BAUD |
            SERIAL_SP_PARITY |
            SERIAL_SP_DATABITS |
            SERIAL_SP_STOPBITS |
            SERIAL_SP_PARITY_CHECK |
            SERIAL_SP_HANDSHAKING |
            SERIAL_SP_CARRIER_DETECT;
        commprop.SettableStopParity = SERIAL_STOPBITS_10 |
            SERIAL_STOPBITS_15 |
            SERIAL_STOPBITS_20 |
            SERIAL_PARITY_NONE |
            SERIAL_PARITY_ODD |
            SERIAL_PARITY_EVEN |
            SERIAL_PARITY_MARK |
            SERIAL_PARITY_SPACE;
        commprop.SettableBaud = SERIAL_BAUD_USER + (SERIAL_BAUD_57600 - 1);
        commprop.SettableData = SERIAL_DATABITS_5 |
            SERIAL_DATABITS_6 |
            SERIAL_DATABITS_7 |
            SERIAL_DATABITS_8;
        status = QCUTIL_RequestCopyFromBuffer
        (
            Request,
            &commprop,
            sizeof(SERIAL_COMMPROP),
            pDevContext
        );
    }
    return status;
}

NTSTATUS QCSER_GetModemStatus
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;
    ULONG    retVal;

    if (OutputBufferLength < sizeof(ULONG))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        if (pDevContext->UartStateInitialized == FALSE)
        {
            pDevContext->UartStateInitialized = TRUE;
            QCSER_InitUartStateFromModem(pDevContext);
        }

        pDevContext->ModemStatusReg &= ~(SERIAL_MSR_DCTS | SERIAL_MSR_DDSR | SERIAL_MSR_TERI | SERIAL_MSR_DDCD);
        retVal = (ULONG)pDevContext->ModemStatusReg;
        status = QCUTIL_RequestCopyFromBuffer
        (
            Request,
            &retVal,
            sizeof(ULONG),
            pDevContext
        );
    }
    return status;
}

NTSTATUS QCSER_GetCommStatus
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;

    if (OutputBufferLength < sizeof(SERIAL_STATUS))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        pDevContext->SerialStatus.AmountInInQueue = (ULONG)pDevContext->AmountInInQueue;
        pDevContext->SerialStatus.AmountInOutQueue = (ULONG)pDevContext->WriteRequestPendingListDataSize;
        pDevContext->SerialStatus.HoldReasons = 0;
        pDevContext->SerialStatus.Errors = 0;
        pDevContext->SerialStatus.EofReceived = FALSE;
        status = QCUTIL_RequestCopyFromBuffer
        (
            Request,
            &pDevContext->SerialStatus,
            sizeof(SERIAL_STATUS),
            pDevContext
        );
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_INFO,
        ("<%ws> QCSER_GetCommStatus completed with status: 0x%x, Error: (%lu), Hold: (%lu), InQ: (%lu), OutQ: (%lu), EofRcvd: (%d), WaitForImm(%d)\n",
            pDevContext->PortName, status, pDevContext->SerialStatus.Errors, pDevContext->SerialStatus.HoldReasons,
            pDevContext->SerialStatus.AmountInInQueue, pDevContext->SerialStatus.AmountInOutQueue,
            pDevContext->SerialStatus.EofReceived, pDevContext->SerialStatus.WaitForImmediate)
    );

    return status;
}

NTSTATUS QCSER_ResetDevice
(
    PDEVICE_CONTEXT pDevContext
)
{
    // LIMIT: CDC has no MODEM_RESET command
    pDevContext->ModemInfo.ulDteRate = 2 * 1024 * 1024;
    pDevContext->ModemInfo.ucParityType = 0;
    pDevContext->ModemInfo.ucStopBit = 0;
    pDevContext->ModemInfo.ucDataBits = 3;
    return STATUS_SUCCESS;
}

NTSTATUS QCSER_Purge
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    ULONG    mask;
    NTSTATUS status;

    if (InputBufferLength < sizeof(ULONG))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        status = QCUTIL_RequestCopyToBuffer(Request, &mask, sizeof(ULONG), pDevContext);
        if (NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_INFO,
                ("<%ws> QCSER_Purge mask: 0x%x\n", pDevContext->PortName, mask)
            );
            if ((!mask) || (mask & (~(
                SERIAL_PURGE_TXABORT |
                SERIAL_PURGE_RXABORT |
                SERIAL_PURGE_TXCLEAR |
                SERIAL_PURGE_RXCLEAR )))
               )
            {
                status = STATUS_INVALID_PARAMETER;
            }
            else
            {
                if (mask & SERIAL_PURGE_TXABORT)
                {
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_CONTROL,
                        QCSER_DBG_LEVEL_DETAIL,
                        ("<%ws> QCSER_Purge SERIAL_PURGE_TXABORT\n", pDevContext->PortName)
                    );
                    // Cancel all pending writes
                    WdfIoQueueStopAndPurgeSynchronously(pDevContext->WriteQueue);
                    KeSetEvent(&pDevContext->WriteThreadPurgeEvent, IO_NO_INCREMENT, FALSE);
                    KeWaitForSingleObject
                    (
                        &pDevContext->WritePurgeCompletionEvent,
                        Executive,
                        KernelMode,
                        FALSE,
                        NULL
                    );
                    KeClearEvent(&pDevContext->WritePurgeCompletionEvent);
                    WdfIoQueueStart(pDevContext->WriteQueue);
                }
                if (mask & SERIAL_PURGE_TXCLEAR)
                {
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_CONTROL,
                        QCSER_DBG_LEVEL_DETAIL,
                        ("<%ws> QCSER_Purge SERIAL_PURGE_TXCLEAR\n", pDevContext->PortName)
                    );
                    // Nothing to do because we don't have write buffer
                }
                if (mask & SERIAL_PURGE_RXABORT)
                {
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_CONTROL,
                        QCSER_DBG_LEVEL_DETAIL,
                        ("<%ws> QCSER_Purge SERIAL_PURGE_RXABORT\n", pDevContext->PortName)
                    );
                    // Cancel all pending reads
                    WdfIoQueueStopAndPurgeSynchronously(pDevContext->ReadQueue);
                    WdfIoQueueStart(pDevContext->ReadQueue);
                }
                if (mask & SERIAL_PURGE_RXCLEAR)
                {
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_CONTROL,
                        QCSER_DBG_LEVEL_DETAIL,
                        ("<%ws> QCSER_Purge SERIAL_PURGE_RXCLEAR\n", pDevContext->PortName)
                    );
                    // Clear all read buffers
                    KeSetEvent(&pDevContext->ReadThreadClearBufferEvent, IO_NO_INCREMENT, FALSE);
                    KeWaitForSingleObject
                    (
                        &pDevContext->ReadPurgeCompletionEvent,
                        Executive,
                        KernelMode,
                        FALSE,
                        NULL
                    );
                    KeClearEvent(&pDevContext->ReadPurgeCompletionEvent);
                }
            }
        }
    }
    return status;
}

NTSTATUS QCSER_LsrMstInsert
(
    PDEVICE_CONTEXT pDevContext
)
{
    UNREFERENCED_PARAMETER(pDevContext);
    return STATUS_SUCCESS;
}

NTSTATUS QCSER_GetBaudRate
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;

    if (OutputBufferLength < sizeof(SERIAL_BAUD_RATE))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        MODEM_INFO modemInfo;
        status = QCSER_GetModemConfig(pDevContext, &modemInfo);
        if (NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> QCSER_GetBaudRate ulDteRate: %lu\n", pDevContext->PortName, modemInfo.ulDteRate)
            );
            status = QCUTIL_RequestCopyFromBuffer
            (
                Request,
                &modemInfo.ulDteRate,
                sizeof(SERIAL_BAUD_RATE),
                pDevContext
            );
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> QCSER_GetBaudRate completed with status: 0x%x, ulDteRate: %lu\n", pDevContext->PortName, status, modemInfo.ulDteRate)
            );
        }
    }
    return status;
}

NTSTATUS QCSER_SetBaudRate
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    NTSTATUS status;

    if (InputBufferLength != sizeof(SERIAL_BAUD_RATE))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        MODEM_INFO modemInfo;
        status = QCSER_GetModemConfig(pDevContext, &modemInfo);
        if (NT_SUCCESS(status))
        {
            status = QCUTIL_RequestCopyToBuffer
            (
                Request,
                &modemInfo.ulDteRate,
                sizeof(SERIAL_BAUD_RATE),
                pDevContext
            );
            if (NT_SUCCESS(status))
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CONTROL,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> QCSER_SetBaudRate ulDteRate: %lu\n", pDevContext->PortName, modemInfo.ulDteRate)
                );
                status = QCSER_SetModemConfig(pDevContext, &modemInfo);
            }
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> QCSER_SetBaudRate completed with status: 0x%x, ulDteRate: %lu\n", pDevContext->PortName, status, modemInfo.ulDteRate)
            );
        }
    }
    return status;
}

NTSTATUS QCSER_SetQueueSize
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    UNREFERENCED_PARAMETER(pDevContext);
    UNREFERENCED_PARAMETER(Request);
    if (InputBufferLength != sizeof(SERIAL_QUEUE_SIZE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS QCSER_GetLineControl
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;

    if (OutputBufferLength < sizeof(SERIAL_LINE_CONTROL))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        MODEM_INFO modemInfo;
        status = QCSER_GetModemConfig(pDevContext, &modemInfo);
        if (NT_SUCCESS(status))
        {
            PSERIAL_LINE_CONTROL outputBuffer;
            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_LINE_CONTROL), &outputBuffer, NULL);
            if (NT_SUCCESS(status))
            {
                outputBuffer->Parity = modemInfo.ucParityType;
                outputBuffer->StopBits = modemInfo.ucStopBit;
                outputBuffer->WordLength = (modemInfo.ucDataBits == 2) ? 7 : 8;
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CONTROL,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> QCSER_GetLineControl Parity: %u, StopBits: %u, WordLength: %u\n", pDevContext->PortName,
                        outputBuffer->Parity, outputBuffer->StopBits, outputBuffer->WordLength)
                );
            }
        }
    }
    return status;
}

NTSTATUS QCSER_SetLineControl
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    NTSTATUS status;

    if (InputBufferLength != sizeof(SERIAL_LINE_CONTROL))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        PSERIAL_LINE_CONTROL inputBuffer;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_LINE_CONTROL), &inputBuffer, NULL);
        if (NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> QCSER_SetLineControl Parity: %u, Stopbits: %u, Wordlength: %u\n", pDevContext->PortName,
                    inputBuffer->Parity, inputBuffer->StopBits, inputBuffer->WordLength)
            );
            MODEM_INFO modemInfo;
            status = QCSER_GetModemConfig(pDevContext, &modemInfo);
            if (NT_SUCCESS(status))
            {
                modemInfo.ucStopBit = inputBuffer->StopBits;
                modemInfo.ucParityType = inputBuffer->Parity;
                UCHAR ucWordLength = inputBuffer->WordLength;
                if (ucWordLength < 7 || ucWordLength > 8)
                {
                    status = STATUS_INVALID_PARAMETER;
                }
                else
                {
                    if (ucWordLength == 7)
                    {
                        ucWordLength = 2;
                    }
                    else
                    {
                        ucWordLength = 3;
                    }
                    modemInfo.ucDataBits = ucWordLength;
                    status = QCSER_SetModemConfig(pDevContext, &modemInfo);
                }
            }
        }
    }
    return status;
}

NTSTATUS QCSER_GetWaitMask
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;

    if (OutputBufferLength < sizeof(ULONG))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        status = QCUTIL_RequestCopyFromBuffer
        (
            Request,
            &pDevContext->WaitMask,
            sizeof(ULONG),
            pDevContext
        );
    }
    return status;
}

NTSTATUS QCSER_SetWaitMask
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    NTSTATUS status;

    if (InputBufferLength != sizeof(ULONG))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        status = QCUTIL_RequestCopyToBuffer
        (
            Request,
            &pDevContext->WaitMask,
            sizeof(ULONG),
            pDevContext
        );
        if (NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> QCSER_SetWaitMask new wait mask: %lu\n", pDevContext->PortName, pDevContext->WaitMask)
            );
            // complete the old wait_on_mask request
            QCSER_CompleteWomRequest(pDevContext, STATUS_SUCCESS, 0);
        }
    }
    return status;
}

NTSTATUS QCSER_WaitOnMask
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    WDFREQUEST currentWait = NULL;

    if (OutputBufferLength < sizeof(ULONG))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        if (pDevContext->UartStateInitialized == FALSE)
        {
            pDevContext->UartStateInitialized = TRUE;
            QCSER_InitUartStateFromModem(pDevContext);
        }

        if (pDevContext->WaitMask != 0)
        {
            if (QCUTIL_IsIoQueueEmpty(pDevContext->WaitOnMaskQueue) == FALSE)
            {
                // there is a pending wom request, reject the new one
                ULONG outputBuffer = 0;
                status = QCUTIL_RequestCopyFromBuffer
                (
                    Request,
                    &outputBuffer,
                    sizeof(ULONG),
                    pDevContext
                );
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CONTROL,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> QCSER_WaitOnMask reject new wom request: 0x%p\n", pDevContext->PortName, Request)
                );
            }
            else
            {
                status = WdfRequestForwardToIoQueue(Request, pDevContext->WaitOnMaskQueue);
                if (!NT_SUCCESS(status))
                {
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_CONTROL,
                        QCSER_DBG_LEVEL_DETAIL,
                        ("<%ws> QCSER_WaitOnMask forward to WaitOnMaskQueue FAILED status: 0x%x\n", pDevContext->PortName, status)
                    );
                }
                else
                {
                    QCSER_DbgPrint
                    (
                        QCSER_DBG_MASK_CONTROL,
                        QCSER_DBG_LEVEL_DETAIL,
                        ("<%ws> QCSER_WaitOnMask forward to WaitOnMaskQueue SUCCESSFUL\n", pDevContext->PortName)
                    );
                    status = STATUS_PENDING;
                }
            }
        }
        else
        {
            ULONG outputBuffer = 0;
            QCUTIL_RequestCopyFromBuffer
            (
                Request,
                &outputBuffer,
                sizeof(ULONG),
                pDevContext
            );
        }
    }
    return status;
}

NTSTATUS QCSER_GetChars
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;
    if (OutputBufferLength < sizeof(SERIAL_CHARS))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        status = QCUTIL_RequestCopyFromBuffer
        (
            Request,
            &pDevContext->Chars,
            sizeof(SERIAL_CHARS),
            pDevContext
        );

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCSER_GetChars get serial chars: %u, status: 0x%x\n", pDevContext->PortName, pDevContext->Chars.EventChar, status)
        );
    }
    return status;
}

NTSTATUS QCSER_SetChars
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    NTSTATUS status;
    if (InputBufferLength != sizeof(SERIAL_CHARS))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        status = QCUTIL_RequestCopyToBuffer
        (
            Request,
            &pDevContext->Chars,
            sizeof(SERIAL_CHARS),
            pDevContext
        );

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCSER_SetChars set serial chars: %u, status: 0x%x\n", pDevContext->PortName, pDevContext->Chars.EventChar, status)
        );
    }
    return status;
}

NTSTATUS QCSER_GetHandflow
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;
    if (OutputBufferLength < sizeof(SERIAL_HANDFLOW))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        status = QCUTIL_RequestCopyFromBuffer
        (
            Request,
            &pDevContext->HandFlow,
            sizeof(SERIAL_HANDFLOW),
            pDevContext
        );
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCSER_GetHandflow controlHandShake: 0x%x, flowReplace: 0x%x, XonLimit: 0x%x, XoffLimit: 0x%x\n",
                pDevContext->PortName, pDevContext->HandFlow.ControlHandShake, pDevContext->HandFlow.FlowReplace, pDevContext->HandFlow.XonLimit, pDevContext->HandFlow.XoffLimit)
        );
    }
    return status;
}

NTSTATUS QCSER_SetHandflow
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    NTSTATUS status;
    if (InputBufferLength != sizeof(SERIAL_HANDFLOW))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        PSERIAL_HANDFLOW inputBuffer;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_HANDFLOW), &inputBuffer, NULL);
        if (NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> QCSER_SetHandflow controlHandShake: 0x%x, flowReplace: 0x%x, XonLimit: 0x%x, XoffLimit: 0x%x\n",
                    pDevContext->PortName, inputBuffer->ControlHandShake, inputBuffer->FlowReplace, inputBuffer->XonLimit, inputBuffer->XoffLimit)
            );
            if (!(inputBuffer->ControlHandShake & SERIAL_CONTROL_INVALID) && !(inputBuffer->FlowReplace & SERIAL_FLOW_INVALID))
            {
                RtlCopyMemory(&pDevContext->HandFlow, inputBuffer, sizeof(SERIAL_HANDFLOW));
                if (inputBuffer->FlowReplace & SERIAL_RTS_HANDSHAKE)
                {
                    QCSER_SerialSetRts(pDevContext);
                }
                if (inputBuffer->ControlHandShake & SERIAL_DTR_HANDSHAKE)
                {
                    QCSER_SerialSetDtr(pDevContext);
                }
            }
            else
            {
                status = STATUS_INVALID_PARAMETER;
            }
        }
    }
    return status;
}

NTSTATUS QCSER_SetBreak
(
    PDEVICE_CONTEXT pDevContext,
    USHORT          SetValue
)
{
    NTSTATUS status = STATUS_SUCCESS;
    if (pDevContext->UsbDeviceType == DEVICETYPE_CDC)
    {
        WDF_USB_CONTROL_SETUP_PACKET controlSetupPacket;
        WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS
        (
            &controlSetupPacket,
            BmRequestHostToDevice,
            BmRequestToInterface,
            CDC_SEND_BREAK,
            SetValue,
            pDevContext->InterfaceIndex
        );
        status = WdfUsbTargetDeviceSendControlTransferSynchronously
        (
            pDevContext->UsbDevice,
            NULL,
            WDF_NO_SEND_OPTIONS,
            &controlSetupPacket,
            NULL,
            NULL
        );
    }
    return status;
}

NTSTATUS QCSER_GetTimeout
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;

    if (OutputBufferLength < sizeof(SERIAL_TIMEOUTS))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        status = QCUTIL_RequestCopyFromBuffer
        (
            Request,
            &pDevContext->Timeouts,
            sizeof(SERIAL_TIMEOUTS),
            pDevContext
        );
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCSER_GetTimeout status: 0x%x, RI=%u RM=%u RC=%u WM=%u WC=%u\n", pDevContext->PortName, status,
                pDevContext->Timeouts.ReadIntervalTimeout,
                pDevContext->Timeouts.ReadTotalTimeoutMultiplier,
                pDevContext->Timeouts.ReadTotalTimeoutConstant,
                pDevContext->Timeouts.WriteTotalTimeoutMultiplier,
                pDevContext->Timeouts.WriteTotalTimeoutConstant
            )
        );
    }
    return status;
}

NTSTATUS QCSER_SetTimeout
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    NTSTATUS status;

    if (InputBufferLength != sizeof(SERIAL_TIMEOUTS))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        status = QCUTIL_RequestCopyToBuffer
        (
            Request,
            &pDevContext->Timeouts,
            sizeof(SERIAL_TIMEOUTS),
            pDevContext
        );
        if (NT_SUCCESS(status))
        {
            if (pDevContext->Timeouts.ReadIntervalTimeout == 0)
            {
                // case 1/2: no timeout -- read until chars are received
                if ((pDevContext->Timeouts.ReadTotalTimeoutMultiplier == 0) &&
                    (pDevContext->Timeouts.ReadTotalTimeoutConstant == 0))
                {
                    pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_1;
                }
                else if ((pDevContext->Timeouts.ReadTotalTimeoutMultiplier == MAXULONG) ||
                    (pDevContext->Timeouts.ReadTotalTimeoutConstant == MAXULONG))
                {
                    pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_2;
                }
                // case 3: timeout after ReadTotalTimeout
                else
                {
                    pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_3;
                }
            }
            else if (pDevContext->Timeouts.ReadIntervalTimeout == MAXULONG)
            {
                // case 4: return immediately
                if ((pDevContext->Timeouts.ReadTotalTimeoutMultiplier == 0) &&
                    (pDevContext->Timeouts.ReadTotalTimeoutConstant == 0))
                {
                    pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_4;
                }
                else if (pDevContext->Timeouts.ReadTotalTimeoutMultiplier == MAXULONG)
                {
                    // case 5: special handling
                    if ((pDevContext->Timeouts.ReadTotalTimeoutConstant > 0) &&
                        (pDevContext->Timeouts.ReadTotalTimeoutConstant < MAXULONG))
                    {
                        pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_5;
                        pDevContext->ReadTimeout.bReturnOnAnyChars = TRUE;
                    }
                    // case 6: RI=MAX RM=MAX RC=MAX/0 => return immediately
                    else
                    {
                        pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_6;
                    }
                }
                // case 7: no timeout
                else if (pDevContext->Timeouts.ReadTotalTimeoutConstant == MAXULONG)
                {
                    pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_7;
                }
                // case 8: RI=MAX RM=[0..MAX) RC=[0..MAX), use read total timeout
                else
                {
                    pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_8;
                }
            }
            // case 9/10: RI==(0..MAXULONG), RI timeout
            else if ((pDevContext->Timeouts.ReadTotalTimeoutConstant == 0) &&
                (pDevContext->Timeouts.ReadTotalTimeoutMultiplier == 0))
            {
                pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_9;
                pDevContext->ReadTimeout.bUseReadInterval = TRUE;
            }
            else if ((pDevContext->Timeouts.ReadTotalTimeoutConstant == MAXULONG) ||
                (pDevContext->Timeouts.ReadTotalTimeoutMultiplier == MAXULONG))
            {
                pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_10;
                pDevContext->ReadTimeout.bUseReadInterval = TRUE;
            }
            // case 11: RI/RM/RC == (0..MAXULONG), choose a smaller timeout
            else
            {
                pDevContext->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_11;
            }

            if ((pDevContext->ReadTimeout.ucTimeoutType != QCSER_READ_TIMEOUT_CASE_9) &&
                (pDevContext->ReadTimeout.ucTimeoutType != QCSER_READ_TIMEOUT_CASE_10))
            {
                pDevContext->ReadTimeout.bUseReadInterval = FALSE;
            }

            if (pDevContext->ReadTimeout.ucTimeoutType != QCSER_READ_TIMEOUT_CASE_5)
            {
                pDevContext->ReadTimeout.bReturnOnAnyChars = FALSE;
            }
        }

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCSER_SetTimeout status: 0x%x, RI=%u RM=%u RC=%u WM=%u WC=%u\n", pDevContext->PortName, status,
                pDevContext->Timeouts.ReadIntervalTimeout,
                pDevContext->Timeouts.ReadTotalTimeoutMultiplier,
                pDevContext->Timeouts.ReadTotalTimeoutConstant,
                pDevContext->Timeouts.WriteTotalTimeoutMultiplier,
                pDevContext->Timeouts.WriteTotalTimeoutConstant
            )
        );
    }
    return status;
}

NTSTATUS QCSER_ImmediateChar
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(pDevContext);
    NTSTATUS status;

    if (InputBufferLength != sizeof(SERIAL_TIMEOUTS))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        // Currently Not Supported
        status = STATUS_UNSUCCESSFUL;
    }
    return status;
}

NTSTATUS QCSER_XoffCounter
(
    PDEVICE_CONTEXT pDevContext
)
{
    UNREFERENCED_PARAMETER(pDevContext);
    return STATUS_SUCCESS;
}

NTSTATUS QCSER_SerialSetDtr
(
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR ucSetValue = CDC_CONTROL_LINE_DTR;

    if (pDevContext->UsbDeviceType >= DEVICETYPE_SERIAL)
    {
        pDevContext->ModemControlReg |= SERIAL_DTR_STATE;
    }
    else
    {
        if (pDevContext->ModemControlReg & SERIAL_RTS_STATE)
        {
            ucSetValue |= CDC_CONTROL_LINE_RTS;
        }

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_INFO,
            ("<%ws> QCSER_SerialSetDtr value: 0x%x\n", pDevContext->PortName, ucSetValue)
        );

        if (pDevContext->UsbDeviceType == DEVICETYPE_CDC)
        {
            WDF_USB_CONTROL_SETUP_PACKET controlSetupPacket;
            WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS
            (
                &controlSetupPacket,
                BmRequestHostToDevice,
                BmRequestToInterface,
                CDC_SET_CONTROL_LINE_STATE,
                ucSetValue,
                pDevContext->InterfaceIndex
            );
            status = WdfUsbTargetDeviceSendControlTransferSynchronously
            (
                pDevContext->UsbDevice,
                NULL,
                WDF_NO_SEND_OPTIONS,
                &controlSetupPacket,
                NULL,
                NULL
            );
            if (NT_SUCCESS(status))
            {
                pDevContext->ModemControlReg |= SERIAL_DTR_STATE;
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_INFO,
                    ("<%ws> QCSER_SerialSetDtr ModemControlReg: 0x%x\n", pDevContext->PortName, pDevContext->ModemControlReg)
                );
            }
        }
        else
        {
            status = STATUS_UNSUCCESSFUL;
        }
    }
    return status;
}

NTSTATUS QCSER_SerialClrDtr
(
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR ucSetValue = 0;

    if (pDevContext->UsbDeviceType >= DEVICETYPE_SERIAL)
    {
        pDevContext->ModemControlReg &= ~SERIAL_DTR_STATE;
    }
    else if (pDevContext->UsbDeviceType == DEVICETYPE_CDC)
    {
        if (pDevContext->ModemControlReg & SERIAL_RTS_STATE)
        {
            ucSetValue = CDC_CONTROL_LINE_RTS;
        }
        WDF_USB_CONTROL_SETUP_PACKET controlSetupPacket;
        WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS
        (
            &controlSetupPacket,
            BmRequestHostToDevice,
            BmRequestToInterface,
            CDC_SET_CONTROL_LINE_STATE,
            ucSetValue,
            pDevContext->InterfaceIndex
        );
        status = WdfUsbTargetDeviceSendControlTransferSynchronously
        (
            pDevContext->UsbDevice,
            NULL,
            WDF_NO_SEND_OPTIONS,
            &controlSetupPacket,
            NULL,
            NULL
        );
        if (NT_SUCCESS(status))
        {
            pDevContext->ModemControlReg &= ~SERIAL_DTR_STATE;
        }
    }
    else
    {
        status = STATUS_UNSUCCESSFUL;
    }
    return status;
}

NTSTATUS QCSER_SerialSetRts
(
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR ucSetValue = CDC_CONTROL_LINE_RTS;

    if (pDevContext->UsbDeviceType >= DEVICETYPE_SERIAL)
    {
        pDevContext->ModemControlReg |= SERIAL_RTS_STATE;
    }
    else
    {
        if (pDevContext->ModemControlReg & SERIAL_DTR_STATE)
        {
            ucSetValue |= CDC_CONTROL_LINE_DTR;
        }

        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_INFO,
            ("<%ws> QCSER_SerialSetRts value: 0x%x\n", pDevContext->PortName, ucSetValue)
        );

        if (pDevContext->UsbDeviceType == DEVICETYPE_CDC)
        {
            WDF_USB_CONTROL_SETUP_PACKET controlSetupPacket;
            WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS
            (
                &controlSetupPacket,
                BmRequestHostToDevice,
                BmRequestToInterface,
                CDC_SET_CONTROL_LINE_STATE,
                ucSetValue,
                pDevContext->InterfaceIndex
            );
            status = WdfUsbTargetDeviceSendControlTransferSynchronously
            (
                pDevContext->UsbDevice,
                NULL,
                WDF_NO_SEND_OPTIONS,
                &controlSetupPacket,
                NULL,
                NULL
            );
            if (NT_SUCCESS(status))
            {
                pDevContext->ModemControlReg |= SERIAL_RTS_STATE;
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_INFO,
                    ("<%ws> QCSER_SerialSetRts ModemControlReg: 0x%x\n", pDevContext->PortName, pDevContext->ModemControlReg)
                );
            }
        }
        else
        {
            status = STATUS_UNSUCCESSFUL;
        }
    }
    return status;
}

NTSTATUS QCSER_SerialClrRts
(
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR ucSetValue = 0;

    if (pDevContext->UsbDeviceType >= DEVICETYPE_SERIAL)
    {
        pDevContext->ModemControlReg &= ~SERIAL_RTS_STATE;
    }
    else if (pDevContext->UsbDeviceType == DEVICETYPE_CDC)
    {
        if (pDevContext->ModemControlReg & SERIAL_DTR_STATE)
        {
            ucSetValue = CDC_CONTROL_LINE_DTR;
        }
        WDF_USB_CONTROL_SETUP_PACKET controlSetupPacket;
        WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS
        (
            &controlSetupPacket,
            BmRequestHostToDevice,
            BmRequestToInterface,
            CDC_SET_CONTROL_LINE_STATE,
            ucSetValue,
            pDevContext->InterfaceIndex
        );
        status = WdfUsbTargetDeviceSendControlTransferSynchronously
        (
            pDevContext->UsbDevice,
            NULL,
            WDF_NO_SEND_OPTIONS,
            &controlSetupPacket,
            NULL,
            NULL
        );
        if (NT_SUCCESS(status))
        {
            pDevContext->ModemControlReg &= ~SERIAL_RTS_STATE;
        }
    }
    else
    {
        status = STATUS_UNSUCCESSFUL;
    }
    return status;
}

NTSTATUS QCSER_GetDtrRts
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;
    if (OutputBufferLength < sizeof(ULONG))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        ULONG ModemControl = pDevContext->ModemControlReg;
        ModemControl &= (SERIAL_DTR_STATE | SERIAL_RTS_STATE);
        status = QCUTIL_RequestCopyFromBuffer
        (
            Request,
            &ModemControl,
            sizeof(ULONG),
            pDevContext
        );
    }
    return status;
}

NTSTATUS QCSER_SetXon
(
    PDEVICE_CONTEXT pDevContext
)
{
    UNREFERENCED_PARAMETER(pDevContext);
    // We don't support soft flow control
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS QCSER_SetXoff
(
    PDEVICE_CONTEXT pDevContext
)
{
    UNREFERENCED_PARAMETER(pDevContext);
    // We don't support soft flow control
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS QCSER_SetDebugMask
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    NTSTATUS status;
    PULONG   mask;

    if (InputBufferLength < sizeof(ULONG))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), &mask, NULL);
        if (NT_SUCCESS(status))
        {
            pDevContext->DebugMask = *mask ? 1 : 0;
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_DETAIL,
                ("<%ws> QCSER_SetDebugMask set debug mask: %lu\n", pDevContext->PortName, *mask)
            );
        }
    }
    return status;
}

NTSTATUS QCSER_WaitOnDeviceRemoval
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;
    PULONG   buffer;
    WDFREQUEST savedRequest = NULL;

    if (OutputBufferLength < sizeof(ULONG))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        status = WdfIoQueueRetrieveNextRequest
        (
            pDevContext->WaitOnDeviceRemovalQueue,
            &savedRequest
        );
        if (NT_SUCCESS(status))
        {
            // duplicate request, complete it immediately
            ULONG buffer = QCOMSER_DUPLICATED_NOTIFICATION_REQ;
            QCUTIL_RequestCopyFromBuffer(savedRequest, &buffer, sizeof(ULONG), pDevContext);
            WdfRequestComplete(savedRequest, STATUS_UNSUCCESSFUL);
        }

        // save the new request to manual queue
        status = WdfRequestForwardToIoQueue
        (
            Request,
            pDevContext->WaitOnDeviceRemovalQueue
        );
        if (NT_SUCCESS(status))
        {
            status = STATUS_PENDING;
        }
    }

    return status;
}


NTSTATUS QCSER_GetDriverGUIDString
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;
    PCHAR    driverGuidString;
    size_t   driverGuidStringLen;

    if (pDevContext->UsbDeviceType == DEVICETYPE_CDC)
    {
        driverGuidString = QCUSB_DRIVER_GUID_DATA_STR;
    }
    else if (pDevContext->UsbDeviceType == DEVICETYPE_SERIAL)
    {
        driverGuidString = QCUSB_DRIVER_GUID_DIAG_STR;
    }
    else
    {
        driverGuidString = QCUSB_DRIVER_GUID_UNKN_STR;
    }
    driverGuidStringLen = strlen(driverGuidString);

    if (OutputBufferLength < driverGuidStringLen)
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        status = QCUTIL_RequestCopyFromBuffer
        (
            Request,
            driverGuidString,
            driverGuidStringLen,
            pDevContext
        );
        if (NT_SUCCESS(status))
        {
            WdfRequestSetInformation(Request, driverGuidStringLen);
        }
        else
        {
            WdfRequestSetInformation(Request, 0);
        }
    }
    return status;
}

NTSTATUS QCSER_GetServiceKey
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
)
{
    NTSTATUS status;
    ULONG    serviceKeyLength = gServicePath.Length / 2;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCSER_GetServiceKey service key length: %lu\n", pDevContext->PortName, serviceKeyLength)
    );

    if (OutputBufferLength < ((size_t)serviceKeyLength + 1))
    {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        PCHAR buffer;
        status = WdfRequestRetrieveOutputBuffer(Request, (size_t)serviceKeyLength + 1, &buffer, NULL);
        if (NT_SUCCESS(status))
        {
            for (ULONG i = 0; i < serviceKeyLength; i++)
            {
                buffer[i] = (char)gServicePath.Buffer[i];
            }
            buffer[serviceKeyLength] = '\0';
            WdfRequestSetInformation(Request, serviceKeyLength);
        }
    }
    return status;
}

NTSTATUS QCSER_GetDeviceId
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request
)
{
    WDFIOTARGET deviceIoTarget = WdfDeviceGetIoTarget(pDevContext->Device);
    WDF_REQUEST_SEND_OPTIONS options;
    NTSTATUS status;

    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    WdfRequestFormatRequestUsingCurrentType(Request);
    if (WdfRequestSend(Request, deviceIoTarget, &options) == FALSE)
    {
        status = WdfRequestGetStatus(Request);
    }
    else
    {
        status = STATUS_PENDING;
    }
    return status;
}

VOID QCSER_CompleteWomRequest(PDEVICE_CONTEXT pDevContext, NTSTATUS outStatus, ULONG outValue)
{
    WDFREQUEST request = NULL;
    NTSTATUS   status  = STATUS_INVALID_PARAMETER;
    ULONG      info = 0;

    status = WdfIoQueueRetrieveNextRequest(pDevContext->WaitOnMaskQueue, &request);
    if (NT_SUCCESS(status))
    {
        status = QCUTIL_RequestCopyFromBuffer
        (
            request,
            &outValue,
            sizeof(ULONG),
            pDevContext
        );
        if (NT_SUCCESS(status))
        {
            info = sizeof(ULONG);
        }
        WdfRequestCompleteWithInformation(request, outStatus, info);
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCSER_CompleteWomRequest completed with status: 0x%x\n", pDevContext->PortName, status)
    );
}

#ifdef QCUSB_MUX_PROTOCOL
NTSTATUS QCSER_SetSessionTotal
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    NTSTATUS status;

    if (InputBufferLength < sizeof(ULONGLONG))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        status = QCUTIL_RequestCopyToBuffer
        (
            Request,
            &pDevContext->QcStats.SessionTotal,
            sizeof(ULONGLONG),
            pDevContext
        );
        if (NT_SUCCESS(status))
        {
            if (pDevContext->QcStats.SessionTotal > 0)
            {
                status = STATUS_SUCCESS;
                if (pDevContext->MaxBulkPacketSize >= QC_SS_BLK_PKT_SZ) // support superspeed usb only
                {
                    // wakeup read thread
                    KeSetEvent(&pDevContext->SessionTotalSetEvent, IO_NO_INCREMENT, FALSE);
                }
            }
            else
            {
                status = STATUS_INVALID_PARAMETER;
                pDevContext->QcStats.SessionTotal = 0;
            }
        }
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCSER_SetSessionTotal session total: %llu\n", pDevContext->PortName, pDevContext->QcStats.SessionTotal)
        );
    }
    return status;
}

NTSTATUS QCSER_ViUsbConfigDevice
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
)
{
    NTSTATUS status;

    if (InputBufferLength < sizeof(VI_CONFIG))
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        VI_CONFIG ViConfig;
        status = QCUTIL_RequestCopyToBuffer
        (
            Request,
            &ViConfig,
            sizeof(VI_CONFIG),
            pDevContext
        );
        if (NT_SUCCESS(status))
        {
            status = QCUSB_VIConfig(pDevContext, &ViConfig);
        }
    }
    return status;
}

#endif // QCUSB_MUX_PROTOCOL