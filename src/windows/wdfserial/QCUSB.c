/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "QCUSB.h"

#ifdef EVENT_TRACING
#include "QCWPP.h"
#include "QCUSB.tmh"
#endif

#ifdef QCUSB_MUX_PROTOCOL
NTSTATUS QCUSB_SVEFlowEntry
(
    PDEVICE_CONTEXT pDevContext
)
{
    NTSTATUS    status = STATUS_SUCCESS;
    WDFREQUEST  request = NULL;
    WDFIOTARGET ioTarget = WdfUsbTargetDeviceGetIoTarget(pDevContext->UsbDevice);
    PURB        pUrb = ExAllocatePoolZero(NonPagedPoolNx, sizeof(struct _URB_CONTROL_TRANSFER_EX) + sizeof(ULONG), '1brU');
    PULONG      pData = NULL;
    PUSB_DEFAULT_PIPE_REQUEST pSetupPacket;
    WDF_OBJECT_ATTRIBUTES objectAttributes;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCUSB_SVEFlowEntry\n", pDevContext->PortName)
    );

    if (pUrb != NULL)
    {
        WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
        objectAttributes.ParentObject = pDevContext->UsbDevice;
        status = WdfRequestCreate
        (
            &objectAttributes,
            ioTarget,
            &request
        );

        if (NT_SUCCESS(status))
        {
            pUrb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER_EX;
            pUrb->UrbHeader.Length = sizeof(struct _URB_CONTROL_TRANSFER_EX);
            pUrb->UrbControlTransferEx.PipeHandle = NULL; // NULL for default control endpoint
            pUrb->UrbControlTransferEx.TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;
            pUrb->UrbControlTransferEx.Timeout = 100;  // 10ms timeout

            pSetupPacket = (PUSB_DEFAULT_PIPE_REQUEST)pUrb->UrbControlTransferEx.SetupPacket;
            pSetupPacket->bmRequestType = 0x40;
            pSetupPacket->bRequest = 0xF0;
            pSetupPacket->wValue = 0xE1;
            pSetupPacket->wIndex = 0x0;
            pSetupPacket->wLength = sizeof(ULONG);
            pData = (PULONG) & (pSetupPacket->Data[0]);
            *pData = 0x00000001L;
            pUrb->UrbControlTransferEx.TransferBuffer = (PVOID)pData;
            pUrb->UrbControlTransferEx.TransferBufferLength = sizeof(ULONG);

            status = WdfUsbTargetDeviceSendUrbSynchronously
            (
                pDevContext->UsbDevice,
                request,
                NULL,
                pUrb
            );
            if (!NT_SUCCESS(status))
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CONTROL,
                    QCSER_DBG_LEVEL_ERROR,
                    ("<%ws> QCUSB_SVEFlowEntry urb send FAILED status: 0x%x\n", pDevContext->PortName, status)
                );
            }
            WdfObjectDelete(request);
        }
        else
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> QCUSB_SVEFlowEntry request create FAILED status: 0x%x\n", pDevContext->PortName, status)
            );
        }
        ExFreePoolWithTag(pUrb, '1brU');
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return status;
}

NTSTATUS QCUSB_VICommand
(
    PDEVICE_CONTEXT pDevContext,
    PVI_CONFIG      pViConfig,
    USHORT          Command
)
{
    NTSTATUS    status = STATUS_SUCCESS;
    WDFREQUEST  request = NULL;
    WDFIOTARGET ioTarget = WdfUsbTargetDeviceGetIoTarget(pDevContext->UsbDevice);
    PURB        pUrb = ExAllocatePoolZero(NonPagedPoolNx, sizeof(struct _URB_CONTROL_TRANSFER_EX) + 12, '0brU');
    PULONG      pData = NULL;
    PUSB_DEFAULT_PIPE_REQUEST pSetupPacket;
    WDF_OBJECT_ATTRIBUTES objectAttributes;
    LARGE_INTEGER startAddress;

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCUSB_VICommand address: %lld, datasize: 0x%x, direction: 0x%x\n",
        pDevContext->PortName, pViConfig->Address, pViConfig->DataSize, pViConfig->Direction)
    );

    if (pUrb != NULL)
    {
        WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
        objectAttributes.ParentObject = pDevContext->UsbDevice;
        status = WdfRequestCreate
        (
            &objectAttributes,
            ioTarget,
            &request
        );

        if (NT_SUCCESS(status))
        {
            pUrb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER_EX;
            pUrb->UrbHeader.Length = sizeof(struct _URB_CONTROL_TRANSFER_EX);
            pUrb->UrbControlTransferEx.PipeHandle = NULL; // NULL for default control endpoint
            pUrb->UrbControlTransferEx.TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;
            pUrb->UrbControlTransferEx.Timeout = 10;  // 10ms timeout

            pSetupPacket = (PUSB_DEFAULT_PIPE_REQUEST)pUrb->UrbControlTransferEx.SetupPacket;
            pSetupPacket->bmRequestType = 0x40;
            pSetupPacket->bRequest = 0xF0;
            pSetupPacket->wValue = Command;
            pSetupPacket->wIndex = 0x0;
            pData = (PULONG) & (pSetupPacket->Data[0]);
            *(pData++) = 0x00000000L;  // device instance, always 0
            startAddress.QuadPart = pViConfig->Address;

            switch (Command)
            {
                case VIUSB_CMD_SET_ADDR:
                {
                    pSetupPacket->wLength = 12;
                    *(pData++) = startAddress.LowPart;
                    *pData = startAddress.HighPart;
                    break;
                }
                case VIUSB_CMD_SET_SIZE:
                {
                    pSetupPacket->wLength = 8;
                    *pData = pViConfig->DataSize - 1;
                    break;
                }
                case VIUSB_CMD_SET_DIR:
                {
                    pSetupPacket->wLength = 8;
                    *pData = pViConfig->Direction;
                    break;
                }
                default:
                {
                    pSetupPacket->wLength = 0;
                    break;
                }
            }

            pUrb->UrbControlTransferEx.TransferBuffer = (PVOID) & (pSetupPacket->Data[0]);
            pUrb->UrbControlTransferEx.TransferBufferLength = pSetupPacket->wLength;

            status = WdfUsbTargetDeviceSendUrbSynchronously
            (
                pDevContext->UsbDevice,
                request,
                NULL,
                pUrb
            );
            if (NT_SUCCESS(status))
            {
                // remember current VI state (startAddress, dataSize, dataDirection) in the device
                // so that a same parameter does not need to be set again in the next round of operation
                switch (Command)
                {
                    case VIUSB_CMD_SET_ADDR:
                        pDevContext->QcStats.ViCurrentAddress = pViConfig->Address;
                        break;
                    case VIUSB_CMD_SET_SIZE:
                        pDevContext->QcStats.ViCurrentDataSize = pViConfig->DataSize;
                        break;
                    case VIUSB_CMD_SET_DIR:
                        pDevContext->QcStats.ViCurrentDirection = pViConfig->Direction;
                        break;
                }
            }
            else
            {
                QCSER_DbgPrint
                (
                    QCSER_DBG_MASK_CONTROL,
                    QCSER_DBG_LEVEL_ERROR,
                    ("<%ws> QCUSB_VICommand request completed FAILED status: 0x%x\n", pDevContext->PortName, status)
                );
            }
            WdfObjectDelete(request);
        }
        else
        {
            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_ERROR,
                ("<%ws> QCUSB_VICommand request create FAILED status: 0x%x\n", pDevContext->PortName, status)
            );
        }
        ExFreePoolWithTag(pUrb, '0brU');
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return status;
}

NTSTATUS QCUSB_VIConfig
(
    PDEVICE_CONTEXT pDevContext,
    PVI_CONFIG      pViConfig
)
{
    NTSTATUS status = STATUS_SUCCESS;

    if ((pDevContext->QcStats.ViCurrentAddress != pViConfig->Address) && NT_SUCCESS(status))
    {
        status = QCUSB_VICommand(pDevContext, pViConfig, VIUSB_CMD_SET_ADDR);
    }
    if ((pDevContext->QcStats.ViCurrentDataSize != pViConfig->DataSize) && NT_SUCCESS(status))
    {
        status = QCUSB_VICommand(pDevContext, pViConfig, VIUSB_CMD_SET_SIZE);
    }
    if ((pDevContext->QcStats.ViCurrentDirection != pViConfig->Direction) && NT_SUCCESS(status))
    {
        status = QCUSB_VICommand(pDevContext, pViConfig, VIUSB_CMD_SET_DIR);
    }

    QCSER_DbgPrint
    (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_TRACE,
        ("<%ws> QCUSB_VIConfig completed with status: 0x%x\n", pDevContext->PortName, status)
    );
    return status;
}
#endif
