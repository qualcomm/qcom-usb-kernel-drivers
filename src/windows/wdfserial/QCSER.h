/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q C S E R . H

GENERAL DESCRIPTION
    Serial IOCTL handlers, UART state, and modem control declarations.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef QCSER_H
#define QCSER_H

#include "QCMAIN.h"

// User-defined IOCTL code range: 2048-4095
#define QCOMSER_IOCTL_INDEX                 2048
#define QCOMSER_REMOVAL_NOTIFICATION        0x00000001L
#define QCOMSER_DUPLICATED_NOTIFICATION_REQ 0x00000002L

// User-defined IOCTL code range: 2048-4095
#define QCOMSER_IOCTL_INDEX                 2048

#define IOCTL_QCOMSER_WAIT_NOTIFY       CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+1, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#define IOCTL_QCDEV_GET_PARENT_DEV_NAME CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+1306, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#define IOCTL_QCUSB_SET_DBG_UMSK        CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+30, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#define IOCTL_QCUSB_SYSTEM_POWER        CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+20, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#define IOCTL_QCUSB_DEVICE_POWER        CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+21, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#define IOCTL_QCUSB_QCDEV_NOTIFY        CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+22, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#define IOCTL_QCOMSER_DRIVER_ID         CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+1285, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#define IOCTL_QCSER_GET_SERVICE_KEY     CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+1286, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#define IOCTL_QCDEV_REQUEST_DEVICEID    CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+1304, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#define IOCTL_QCUSB_CDC_SEND_ENCAPSULATED CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+1287, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#define IOCTL_QCUSB_SEND_CONTROL        CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+11, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#ifdef QCUSB_MUX_PROTOCOL
#define IOCTL_QCUSB_SET_SESSION_TOTAL   CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+31, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)

#define IOCTL_VIUSB_CONFIG_DEVICE       CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+32, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)
#endif

#define IOCTL_QCUSB_REPORT_DEV_NAME     CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                            QCOMSER_IOCTL_INDEX+34, \
                                            METHOD_BUFFERED, \
                                            FILE_ANY_ACCESS)

/*
 * Lengths of data fields.
 */
#define UART_STATE_SIZE                 sizeof(USHORT)
#define UART_CONFIG_SIZE                7

 /*
 * bRequest field function codes...
 */
#define CDC_SEND_ENCAPSULATED_CMD       0x00
#define CDC_GET_ENCAPSULATED_RSP        0x01
#define CDC_SET_LINE_CODING             0x20
#define CDC_GET_LINE_CODING             0x21
#define CDC_SET_CONTROL_LINE_STATE      0x22
#define CDC_SEND_BREAK                  0x23
#define CDC_SET_COMM_FEATURE            0x02

 /*
 * wValue field definitions.
 */
#define CDC_CONTROL_LINE_RTS            0x02 // LIMIT: RTS always asserted
#define CDC_CONTROL_LINE_DTR            0x01
#define CDC_ABSTRACT_STATE              0x01

 /*
 * Read timeout cases
 */
#define QCSER_READ_TIMEOUT_UNDEF        0x00
#define QCSER_READ_TIMEOUT_CASE_1       0x01
#define QCSER_READ_TIMEOUT_CASE_2       0x02
#define QCSER_READ_TIMEOUT_CASE_3       0x03
#define QCSER_READ_TIMEOUT_CASE_4       0x04
#define QCSER_READ_TIMEOUT_CASE_5       0x05
#define QCSER_READ_TIMEOUT_CASE_6       0x06
#define QCSER_READ_TIMEOUT_CASE_7       0x07
#define QCSER_READ_TIMEOUT_CASE_8       0x08
#define QCSER_READ_TIMEOUT_CASE_9       0x09
#define QCSER_READ_TIMEOUT_CASE_10      0x10
#define QCSER_READ_TIMEOUT_CASE_11      0x11

 /*
 * Windows-defined modem status register bits
 */
#define SERIAL_MSR_DCTS  0x01  // clr to send
#define SERIAL_MSR_DDSR  0x02  // dataset ready
#define SERIAL_MSR_TERI  0x04  // ring ind
#define SERIAL_MSR_DDCD  0x08  // carrier detect
#define SERIAL_MSR_CTS   0x10  // clr to send
#define SERIAL_MSR_DSR   0x20  // dataset ready
#define SERIAL_MSR_RI    0x40  // ring ind
#define SERIAL_MSR_DCD   0x80  // carrier detect / rcv line sig detect(RLSD)

#define US_BITS_MODEM     (SERIAL_EV_CTS  | \
                           SERIAL_EV_DSR  | \
                           SERIAL_EV_RLSD | \
                           SERIAL_EV_BREAK| \
                           SERIAL_EV_ERR  | \
                           SERIAL_EV_RING)

#define US_BITS_MODEM_RAW (SERIAL_EV_DSR  | \
                           SERIAL_EV_RLSD | \
                           SERIAL_EV_BREAK| \
                           SERIAL_EV_RING)

VOID QCSER_InitUartStateFromModem
(
    PDEVICE_CONTEXT pDevContext
);

VOID QCSER_ProcessNewUartState
(
    PDEVICE_CONTEXT pDevContext,
    USHORT usNewUartState,
    USHORT usBitsMask
);

NTSTATUS QCSER_GetModemConfig
(
    PDEVICE_CONTEXT pDevContext,
    PMODEM_INFO outModemInfo
);

NTSTATUS QCSER_SetModemConfig
(
    PDEVICE_CONTEXT pDevContext,
    PMODEM_INFO newModemInfo
);

NTSTATUS QCSER_GetStats
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_ClearStats
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCSER_GetProperties
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_GetModemStatus
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_GetCommStatus
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_ResetDevice
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCSER_Purge
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_LsrMstInsert
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCSER_GetBaudRate
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_SetBaudRate
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_SetQueueSize
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_GetLineControl
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_SetLineControl
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_GetWaitMask
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_SetWaitMask
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_WaitOnMask
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_GetChars
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_SetChars
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_GetHandflow
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_SetBreak
(
    PDEVICE_CONTEXT pDevContext,
    USHORT          SetValue
);

NTSTATUS QCSER_SetHandflow
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_GetTimeout
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_SetTimeout
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_ImmediateChar
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_XoffCounter
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCSER_SerialSetDtr
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCSER_SerialClrDtr
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCSER_SerialSetRts
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCSER_SerialClrRts
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCSER_GetDtrRts
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_SetXon
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCSER_SetXoff
(
    PDEVICE_CONTEXT pDevContext
);

NTSTATUS QCSER_SetDebugMask
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_WaitOnDeviceRemoval
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_GetDriverGUIDString
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_GetServiceKey
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          OutputBufferLength
);

NTSTATUS QCSER_GetDeviceId
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request
);

VOID QCSER_CompleteWomRequest
(
    PDEVICE_CONTEXT pDevContext,
    NTSTATUS outStatus,
    ULONG outValue
);

#ifdef QCUSB_MUX_PROTOCOL
NTSTATUS QCSER_SetSessionTotal
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

NTSTATUS QCSER_ViUsbConfigDevice
(
    PDEVICE_CONTEXT pDevContext,
    WDFREQUEST      Request,
    size_t          InputBufferLength
);

#endif // QCUSB_MUX_PROTOCOL

#endif // QCSER_H
