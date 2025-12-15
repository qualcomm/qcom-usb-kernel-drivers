/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "QCMAIN.h"
#include "QCPNP.h"
#include "QCSER.h"
#include "QCINT.h"
#include "QCUSB.h"

#ifdef EVENT_TRACING
#include "QCWPP.h"
#include "QCINT.tmh"
#endif

NTSTATUS QCINT_InitInterruptPipe(PDEVICE_CONTEXT pDevContext)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    LARGE_INTEGER delayValue;
    delayValue.QuadPart = WDF_REL_TIMEOUT_IN_MS(QCPNP_THREAD_INIT_TIMEOUT_MS);

    ntStatus = QCPNP_CreateWorkerThread
    (
        pDevContext,
        QCINT_ReadInterruptPipe,
        &pDevContext->IntThreadStartedEvent,
        &delayValue,
        &pDevContext->interruptThread
    );
    if (!NT_SUCCESS(ntStatus))
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%ws> QCINT thread create FAILED status: 0x%x\n", pDevContext->PortName, ntStatus)
        );
        pDevContext->interruptThread = NULL;
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCINT_InitInterruptPipe completed with status: 0x%x", pDevContext->PortName, ntStatus)
    );
    return ntStatus;
}


VOID QCINT_ReadInterruptPipe(PVOID pContext)
{
    NTSTATUS         status = STATUS_SUCCESS;
    ULONG            errcnt = 0;
    WDFREQUEST       request = NULL;
    PDEVICE_CONTEXT  pDevContext = pContext;
    WDFUSBPIPE       pipeInt;
    PKWAIT_BLOCK     pWaitBlock = ExAllocatePoolUninitialized(NonPagedPoolNx, (INT_PIPE_EVENT_COUNT) * sizeof(KWAIT_BLOCK), '7gaT');
    BOOLEAN          bRunning = TRUE; //loop flag
    BOOLEAN          D0ExitState = FALSE, D0EntryState = FALSE; 
    WDFMEMORY        outputMemory = NULL;
    WDFIOTARGET      ioTarget = NULL;
    WDF_OBJECT_ATTRIBUTES requestAttr;
    WDF_OBJECT_ATTRIBUTES memoryAttr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&requestAttr, REQUEST_CONTEXT);
    WDF_OBJECT_ATTRIBUTES_INIT(&memoryAttr);
    WDF_REQUEST_REUSE_PARAMS reuseParam;
    PUSB_NOTIFICATION_STATUS pNotificationStatus;

    if (pWaitBlock == NULL)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCINT: QCINT_ReadInterruptPipe No-Mem pWaitBlock\n", pDevContext->PortName)
        );
        WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
        bRunning = FALSE;
    }
    else if (pDevContext->InterruptInPipe != NULL)
    {
        ioTarget = WdfUsbTargetPipeGetIoTarget(pDevContext->InterruptInPipe);
        requestAttr.ParentObject = pDevContext->UsbDevice;
        status = WdfRequestCreate
        (
            &requestAttr,
            ioTarget,
            &request
        );
        if (!NT_SUCCESS(status))
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_READ,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> QCINT: QCINT_ReadInterruptPipe pre-read request create FAILED status: 0x%x\n", pDevContext->PortName, status)
            );
            WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
            bRunning = FALSE;
        }
        else
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_READ,
                QCSER_DBG_LEVEL_TRACE,
                ("<%ws> QCINT: QCINT_ReadInterruptPipe pre-read request created status: 0x%x\n", pDevContext->PortName, status)
            );
            memoryAttr.ParentObject = request;
            status = WdfMemoryCreate
            (
                &memoryAttr,
                NonPagedPoolNx,
                0,
                pDevContext->MaxIntPacketSize,
                &outputMemory,
                &(pDevContext->pInterruptBuffer)
            );
            if (!NT_SUCCESS(status))
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_ERROR,
                    ("<%ws> QCINT: QCINT_ReadInterruptPipe pre-read memory create FAILED status: 0x%x\n", pDevContext->PortName, status)
                );
                WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
                bRunning = FALSE;
            }
            else
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_TRACE,
                    ("<%ws> QCINT: QCINT_ReadInterruptPipe pre-read memory created status: 0x%x\n", pDevContext->PortName, status)
                );
                KeClearEvent(&pDevContext->IntThreadD0EntryReadyEvent);
                KeClearEvent(&pDevContext->IntThreadD0ExitReadyEvent);
                KeSetEvent(&pDevContext->IntThreadStartedEvent, IO_NO_INCREMENT, FALSE);
            }
        }
    }
    else
    {
        KeSetEvent(&pDevContext->IntThreadStartedEvent, IO_NO_INCREMENT, FALSE);
    }

    while (bRunning)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_TRACE,
            ("<%ws> QCINT: QCINT_ReadInterruptPipe wait for event...\n", pDevContext->PortName)
        );
        status = KeWaitForMultipleObjects
        (
            INT_PIPE_EVENT_COUNT,
            pDevContext->pInterruptPipeEvents,
            WaitAny,
            Executive,
            KernelMode,
            FALSE,
            NULL,
            pWaitBlock
        );
        switch (status)
        {
            case INT_LPC_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> QCINT: INT_LPC_EVENT\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->IntThreadLpcEvent);
#ifdef QCUSB_MUX_PROTOCOL
                if (pDevContext->DeviceFunction == QCUSB_DEV_FUNC_LPC)
                {
                    LARGE_INTEGER timeoutValue;
                    timeoutValue.QuadPart = -(1 * 1000 * 1000); // 100ms
                    KeDelayExecutionThread(KernelMode, FALSE, &timeoutValue);
                    QCUSB_SVEFlowEntry(pDevContext);
                }
#endif
                break;
            }
            case INT_COMPLETION_EVENT_INDEX:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> QCINT: INT_COMPLETION, D0EntryState: %u, D0ExitState: %u\n",
                        pDevContext->PortName, D0EntryState, D0ExitState)
                );
                KeClearEvent(&pDevContext->InterruptCompletion);

                if (D0ExitState == TRUE)
                {
                    D0ExitState = FALSE;
                    KeSetEvent(&pDevContext->IntThreadD0ExitReadyEvent, IO_NO_INCREMENT, FALSE);
                    break;
                }

                if (pDevContext->pInterruptBuffer == NULL)
                {
                    break;
                }

                //checking status of request
                if (request != NULL)
                {
                    status = WdfRequestGetStatus(request);
                    if (NT_SUCCESS(status))
                    {
                        errcnt = 0;
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_TRACE,
                            ("<%ws> QCINT: WdfRequestGetStatus returned Success (INT_COMPLETION): 0x%x\n", pDevContext->PortName, status)
                        );
                    }
                    else
                    {
                        errcnt++;
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_CIRP,
                            QCSER_DBG_LEVEL_ERROR,
                            ("<%ws> QCINT: reset interrupt pipe FAILED_ (INT_COMPLETION): 0x%x, error count: %lu\n", pDevContext->PortName, status, errcnt)
                        );
                    }
                }

                pNotificationStatus = (PUSB_NOTIFICATION_STATUS)pDevContext->pInterruptBuffer;
                switch (pNotificationStatus->bNotification)
                {
                case CDC_NOTIFICATION_SERIAL_STATE:
                    QCINT_HandleSerialStateNotification(pDevContext);
                    break;
                case CDC_NOTIFICATION_RESPONSE_AVAILABLE:
                    //QCINT_HandleResponseAvailableNotification(pDevContext);
                    break;
                case CDC_NOTIFICATION_NETWORK_CONNECTION:
                    //QCINT_HandleNetworkConnectionNotification(pDevContext);
                    break;
                case CDC_NOTIFICATION_CONNECTION_SPD_CHG:
                    //QCINT_HandleConnectionSpeedChangeNotification(pDevContext);
                    break;
                default:
                    break;
                }
                KeSetEvent(&pDevContext->InterruptStartService, IO_NO_INCREMENT, FALSE);
                break;
            }
            case INT_START_SERVICE_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> QCINT: INT_START\n", pDevContext->PortName)
                );

                KeClearEvent(&pDevContext->InterruptStartService);

                if (pDevContext->InterruptInPipe != NULL)
                {
                    if (errcnt >= gVendorConfig.NumOfRetriesOnError)
                    {
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_CIRP,
                            QCSER_DBG_LEVEL_ERROR,
                            ("<%ws> QCINT: interrupt pipe request FAILED after %d retries: 0x%x\n", pDevContext->PortName, errcnt, status)
                        );
                        KeSetEvent(&pDevContext->IntThreadD0EntryReadyEvent, IO_NO_INCREMENT, FALSE);
                        WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
                        break;
                    }

                    pipeInt = pDevContext->InterruptInPipe;
                    WDF_REQUEST_REUSE_PARAMS_INIT(&reuseParam, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_SUCCESS);
                    status = WdfRequestReuse(request, &reuseParam);
                    if (NT_SUCCESS(status))
                    {
                        WdfRequestSetCompletionRoutine(request, QCINT_InterruptPipeCompletion, pDevContext);
                        status = WdfUsbTargetPipeFormatRequestForRead
                        (
                            pipeInt,
                            request,
                            outputMemory,
                            0
                        );
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_TRACE,
                            ("<%ws> QCINT: ReadInterruptPipe WdfRequestReuse success: 0x%x\n", pDevContext->PortName, status)
                        );
                    }
                    else
                    {
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_ERROR,
                            ("<%ws> QCINT: ReadInterruptPipe WdfRequestReuse FAILED status: 0x%x\n", pDevContext->PortName, status)
                        );
                        KeSetEvent(&pDevContext->IntThreadD0EntryReadyEvent, IO_NO_INCREMENT, FALSE);
                        WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
                        break;
                    }

                    if (WdfRequestSend(request, ioTarget, WDF_NO_SEND_OPTIONS) == FALSE)
                    {
                        status = WdfRequestGetStatus(request);
                        QCSER_DbgPrint
                        (
                            QCSER_DBG_MASK_READ,
                            QCSER_DBG_LEVEL_ERROR,
                            ("<%ws> QCINT: ReadInterruptPipe WdfRequestSend FAILED status: 0x%x, error count: %lu\n", pDevContext->PortName, status, errcnt++)
                        );
                        while (errcnt < gVendorConfig.NumOfRetriesOnError)
                        {
                            status = QCPNP_ResetUsbPipe(pDevContext, pDevContext->InterruptInPipe, 500);
                            if (!NT_SUCCESS(status))
                            {
                                QCSER_DbgPrint
                                (
                                    QCSER_DBG_MASK_CIRP,
                                    QCSER_DBG_LEVEL_ERROR,
                                    ("<%ws> QCINT: reset interrupt pipe FAILED: 0x%x\n", pDevContext->PortName, status)
                                );
                            }
                            else
                            {
                                QCSER_DbgPrint
                                (
                                    QCSER_DBG_MASK_CIRP,
                                    QCSER_DBG_LEVEL_ERROR,
                                    ("<%ws> QCINT: reset interrupt pipe Success: 0x%x\n", pDevContext->PortName, status)
                                );
                                break;
                            }
                            errcnt++;
                        }
                        if (!NT_SUCCESS(status))
                        {
                            QCSER_DbgPrint
                            (
                                QCSER_DBG_MASK_CIRP,
                                QCSER_DBG_LEVEL_ERROR,
                                ("<%ws> QCINT: reset interrupt pipe FAILED after %d retries: 0x%x\n", pDevContext->PortName, errcnt, status)
                            );
                            KeSetEvent(&pDevContext->IntThreadD0EntryReadyEvent, IO_NO_INCREMENT, FALSE);
                            WdfDeviceSetFailed(pDevContext->Device, WdfDeviceFailedNoRestart);
                            break;
                        }
                        QCMAIN_Wait(pDevContext, -(500 * 1000L));
                        KeSetEvent(&pDevContext->InterruptStartService, IO_NO_INCREMENT, FALSE);
                        break;
                    }
                    else
                    {
                        if (D0EntryState == TRUE)
                        {
                            D0EntryState = FALSE;
                            KeSetEvent(&pDevContext->IntThreadD0EntryReadyEvent, IO_NO_INCREMENT, FALSE);
                        }
                    }
                    break;
                }
                break;
            }
            case INT_STOP_SERVICE_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> QCINT: INT_STOP_SERVICE_EVENT\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->InterruptStopServiceEvent);
                if (ioTarget != NULL)
                {
                    WdfIoTargetStop(ioTarget, WdfIoTargetCancelSentIo);
                }
                bRunning = FALSE;
                break;
            }
            case INT_DEVICE_D0_ENTRY_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> QCINT: INT_DEVICE_D0_ENTRY_EVENT\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->IntThreadD0EntryEvent);
                D0EntryState = TRUE;
                D0ExitState = FALSE;
                if (ioTarget == NULL)
                {
                    D0EntryState = FALSE;
                    KeSetEvent(&pDevContext->IntThreadD0EntryReadyEvent, IO_NO_INCREMENT, FALSE);
                }
                else
                {
                    WdfIoTargetStart(ioTarget);
                    KeSetEvent(&pDevContext->InterruptStartService, IO_NO_INCREMENT, FALSE);
                }
                break;
            }
            case INT_DEVICE_D0_EXIT_EVENT:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> QCINT: INT_DEVICE_D0_EXIT_EVENT\n", pDevContext->PortName)
                );
                KeClearEvent(&pDevContext->IntThreadD0ExitEvent);
                D0ExitState = TRUE;
                D0EntryState = FALSE;
                if (ioTarget != NULL)
                {
                    WdfIoTargetStop(ioTarget, WdfIoTargetCancelSentIo);
                }
                else
                {
                    D0ExitState = FALSE;
                }
                KeSetEvent(&pDevContext->IntThreadD0ExitReadyEvent, IO_NO_INCREMENT, FALSE);
                break;
            }
            default:
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_READ,
                    QCSER_DBG_LEVEL_DETAIL,
                    ("<%ws> QCINT: DEFAULT\n", pDevContext->PortName)
                );
                break;
            }
        }
    }

    if (pWaitBlock != NULL)
    {
        ExFreePoolWithTag(pWaitBlock, '7gaT');
        pWaitBlock = NULL;
    }
    if (request != NULL)
        WdfObjectDelete(request);
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCINT: exit status: 0x%x\n", pDevContext->PortName, status)
    );
    KeSetEvent(&pDevContext->IntThreadD0EntryReadyEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&pDevContext->IntThreadD0ExitReadyEvent, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(status);
}

NTSTATUS StopInterruptService
(
    PDEVICE_CONTEXT pDevContext,
    BOOLEAN           bWait
)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCINT: stop\n", pDevContext->PortName)
    );
    if (pDevContext->interruptThread == NULL)
    {
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%ws> QCINT: not started\n", pDevContext->PortName)
        );
        return ntStatus;
    }
    KeSetEvent
    (
        &pDevContext->InterruptStopServiceEvent,
        IO_NO_INCREMENT,
        FALSE
    );
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCINT: StopInterruptService. Waiting for Stop response event\n", pDevContext->PortName)
    );
    if (bWait == TRUE)
    {
        KeWaitForSingleObject
        (
            pDevContext->interruptThread,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        ObDereferenceObject(pDevContext->interruptThread);
        pDevContext->interruptThread = NULL;
    }
    return ntStatus;
} 

VOID QCINT_HandleSerialStateNotification(PDEVICE_CONTEXT pDevContext)
{
    PUSB_NOTIFICATION_STATUS pUartStatus;
    USHORT usStatusBits;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCINT_HandleSerialStateNotification\n", pDevContext->PortName)
    );

    usStatusBits = 0;
    pUartStatus = (PUSB_NOTIFICATION_STATUS)pDevContext->pInterruptBuffer;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCINT: Req 0x%x Noti 0x%x Val 0x%x Idx 0x%x Len %d usVal 0x%x\n",
            pDevContext->PortName, pUartStatus->bmRequestType, pUartStatus->bNotification,
            pUartStatus->wValue, pUartStatus->wIndex, pUartStatus->wLength,
            pUartStatus->usValue)
    );

    if (pUartStatus->usValue & USB_CDC_INT_RX_CARRIER)
    {
        usStatusBits |= SERIAL_EV_RLSD; // carrier-detection
    }
    if (pUartStatus->usValue & USB_CDC_INT_TX_CARRIER)
    {
        usStatusBits |= SERIAL_EV_DSR;  // data-set-ready
    }
    if (pUartStatus->usValue & USB_CDC_INT_BREAK)
    {
        usStatusBits |= SERIAL_EV_BREAK;  // break
    }
    if (pUartStatus->usValue & USB_CDC_INT_RING)
    {
        usStatusBits |= SERIAL_EV_RING;  // ring-detection
    }
    if (pUartStatus->usValue & USB_CDC_INT_FRAME_ERROR)
    {
        usStatusBits |= SERIAL_EV_ERR;  // line-error
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCINT: USB frame err\n", pDevContext->PortName)
        );
    }
    if (pUartStatus->usValue & USB_CDC_INT_PARITY_ERROR)
    {
        usStatusBits |= SERIAL_EV_ERR;  // line-error
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%ws> QCINT: USB parity err\n", pDevContext->PortName)
        );
    }
    // usStatusBits = pUartStatus->usValue & US_BITS_MODEM_RAW;
    usStatusBits &= US_BITS_MODEM_RAW;
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCINT: status 0x%x / 0x%x\n", pDevContext->PortName, usStatusBits, pUartStatus->usValue)
    );

    if (!(pDevContext->UartStateInitialized))
    {
        pDevContext->CurrUartState &= ~US_BITS_MODEM;
    }
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCINT: CURRENT UART 0x%x\n", pDevContext->PortName, pDevContext->CurrUartState)
    );

    QCSER_ProcessNewUartState
    (
        pDevContext,
        usStatusBits,
        US_BITS_MODEM_RAW
    );
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_READ,
        QCSER_DBG_LEVEL_DETAIL,
        ("<%ws> QCINT: new UART 0x%x\n", pDevContext->PortName, pDevContext->CurrUartState)
    );
}

void QCINT_InterruptPipeCompletion
(
    WDFREQUEST  Request,
    WDFIOTARGET Target,
    PWDF_REQUEST_COMPLETION_PARAMS Params,
    WDFCONTEXT  Context
)
{
    UNREFERENCED_PARAMETER(Params);
    UNREFERENCED_PARAMETER(Target);

    PDEVICE_CONTEXT pDevContext = Context;
    KeSetEvent(&pDevContext->InterruptCompletion, IO_NO_INCREMENT, FALSE);
    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCINT_InterruptPipeCompletion status: 0x%x\n", pDevContext->PortName, WdfRequestGetStatus(Request))
    );
}
