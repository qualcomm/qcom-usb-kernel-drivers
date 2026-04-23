/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                      Q D P U B L I C . H

GENERAL DESCRIPTION
    This file defines the public data types, constants, callback
    typedefs, and the QcDevice class interface for the Qualcomm
    device configuration library (qcdev).

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef QDPUB_H
#define QDPUB_H

#include <windows.h>

#ifdef QCDEVLIB_EXPORTS
#define QCDEVLIB_API __declspec(dllexport)
#else
#define QCDEVLIB_API __declspec(dllimport)
#endif

// constants for the Flag in callback
#define QC_DEV_TYPE_NONE   0x00
#define QC_DEV_TYPE_NET    0x01
#define QC_DEV_TYPE_PORTS  0x02
#define QC_DEV_TYPE_USB    0x03
#define QC_DEV_TYPE_MDM    0x04
#define QC_DEV_TYPE_ADB    0x05
#define QC_DEV_TYPE_MBIM   0x06
#define QC_DEV_TYPE_RNDIS  0x07

#define QC_DEV_STATE_DEPARTURE 0x00
#define QC_DEV_STATE_ARRIVAL   0x01

#define QC_FLAG_MASK_QC_DRIVER    0x0000000F
#define QC_FLAG_MASK_DEV_STATE    0x000000F0
#define QC_FLAG_MASK_DEV_TYPE     0x0000FF00
#define QC_FLAG_MASK_DEV_BUS      0x000F0000
#define QC_FLAG_MASK_DEV_PROTOCOL 0x000000FF
#define QC_FLAG_MASK_DEV_PROTOCOL_CATEGORY   0x000000F0

#define QC_DEV_BUS_TYPE_NONE 0  // none
#define QC_DEV_BUS_TYPE_USB  1  // USB
#define QC_DEV_BUS_TYPE_PCI  2  // PCI
#define QC_DEV_BUS_TYPE_PCIE 3  // QC_BUS

// configurable settings
#define DEV_FEATURE_INCLUDE_NONE_QC_PORTS 0x00000001
#define DEV_FEATURE_SCAN_USB_WITH_VID     0x00010000

#define DEV_CLASS_NET   0x00000001
#define DEV_CLASS_PORTS 0x00000002
#define DEV_CLASS_USB   0x00000004
#define DEV_CLASS_MDM   0x00000008
#define DEV_CLASS_ADB   0x00000010

// USB Protocol Number Assignment
#define DEV_PROTOCOL_UNKNOWN                       0x00
#define DEV_PROTOCOL_SAHARA                        0x10
#define DEV_PROTOCOL_SAHARA_PBL_EMERGENCY_DOWNLOAD 0x11
#define DEV_PROTOCOL_SAHARA_PBL_FLASHLESS_BOOT     0x12
#define DEV_PROTOCOL_SAHARA_SBL_XBL_RAMDUMP        0x13
#define DEV_PROTOCOL_SAHARA_TN_APPS_Remote_EFS     0x14
#define DEV_PROTOCOL_FIREHOSE                      0x20
#define DEV_PROTOCOL_DIAG                          0x30
#define DEV_PROTOCOL_DUN                           0x40
#define DEV_PROTOCOL_RMNET                         0x50
#define DEV_PROTOCOL_NMEA                          0x60
#define DEV_PROTOCOL_QDSS                          0x70
#define DEV_PROTOCOL_ADPL                          0x80
#define DEV_PROTOCOL_RESERVED                      0xFF
#define DEV_PROTOCOL_MAJOR_REVISION_MASK           0xF0
#define DEV_DEFAULT_BAUD_RATE                      38400

#pragma pack(push, 4)

typedef struct _DEV_FEATURE_SETTING
{
    ULONG Version;       // 1
    ULONG Settings;      // OR'ed feature masks
    ULONG DeviceClass;   // 0 - all classes
    PTSTR VID;
} DEV_FEATURE_SETTING, *PDEV_FEATURE_SETTING;

typedef struct _CB_PARAMS
{
    PWCHAR DevDesc;   // device description for display in unicode
    PCHAR  DevName;   // device name for I/O (normally the symbolic name)
    PWCHAR IfName;    // interface name for network adapter
    PWCHAR Loc;       // device's location on its bus
    PWCHAR DevPath;   // device's connection path
    PWCHAR SerNum;    // serial number from device
    PWCHAR SerNumMsm; // serial number from device
    ULONG  Mtu;       // MTU for network connection (data call)
    ULONG  Flag;      // flag to indicate device type, state, and availability of QC driver support
    ULONG  Protocol;  // protocol code (low 8 bits) of the USB function, 0 means unknown protocol
    PWCHAR HwId;      // device hardware ID
    PWCHAR ParentDev; // name of parent device if present
} CB_PARAMS, *PCB_PARAMS;

typedef enum _DEV_INFO_ERRNO
{
    DEV_INFO_OK = 0,
    DEV_INFO_DEV_DESC_LEN = 1,
    DEV_INFO_DEV_NAME_LEN = 2,
    DEV_INFO_IF_NAME_LEN = 3,
    DEV_INFO_LOC_LEN = 4,
    DEV_INFO_SER_NUM_LEN = 5,
    DEV_INFO_END = 255
} DEV_INFO_ERRNO;

typedef struct _DEV_PARAMS_N
{
    PVOID  DevDesc;         // device description for display in unicode
    ULONG  DevDescBufLen;   // in bytes
    PVOID  DevName;         // device name for I/O (normally the symbolic name), ANSI string
    ULONG  DevnameBufLen;   // in bytes
    PVOID  IfName;          // interface name for network adapter, in unicode
    ULONG  IfNameBufLen;    // in bytes
    PVOID  Loc;             // device's location on its bus, in unicode
    ULONG  LocBufLen;       // in bytes
    PVOID  DevPath;         // device's connection path
    ULONG  DevPathBufLen;   // in bytes
    PVOID  SerNum;          // serial number from device, in unicode
    ULONG  SerNumBufLen;    // in bytes
    PVOID  SerNumMsm;       // serial number from device, in unicode
    ULONG  SerNumMsmBufLen; // in bytes
    ULONG  Mtu;             // MTU for network connection (data call)
    ULONG  Flag;            // flag to indicate device type, state, and availability of QC driver support
    ULONG  Protocol;        // protocol code of the USB function, 0 means unknown protocol
} DEV_PARAMS_N, *PDEV_PARAMS_N;

#pragma pack(pop)

// Device change callback
typedef VOID(_stdcall *DEVICECHANGE_CALLBACK)(PCB_PARAMS CbParams, PVOID *Context);
typedef VOID(_stdcall *DEVICECHANGE_CALLBACK_N)(VOID);

// Logging callback
typedef VOID(_stdcall *QCD_LOGGING_CALLBACK)(PCHAR Message);  // NULL-terminated ANSI string

// APIs for device monitor/update
namespace QcDevice
{
    QCDEVLIB_API VOID _cdecl QCD_Printf(const char *Format, ...);
    QCDEVLIB_API VOID   SetLoggingCallback(QCD_LOGGING_CALLBACK Cb);
    QCDEVLIB_API VOID   SetDeviceChangeCallback(DEVICECHANGE_CALLBACK Cb);
    QCDEVLIB_API VOID   SetDeviceChangeCallback(DEVICECHANGE_CALLBACK Cb, PVOID AppContext);
    QCDEVLIB_API VOID   SetDeviceChangeCallback(DEVICECHANGE_CALLBACK_N Cb);
    QCDEVLIB_API VOID   SetFeature(PVOID Features);
    QCDEVLIB_API VOID   StartDeviceMonitor(VOID);
    QCDEVLIB_API VOID   StopDeviceMonitor(VOID);
    QCDEVLIB_API ULONG  GetDevice(PVOID UserBuffer);
    QCDEVLIB_API PCHAR  GetPortName(PVOID DeviveName);
    QCDEVLIB_API PCHAR  LibVersion(VOID);
    QCDEVLIB_API BOOL   FindParent(PVOID HwInstanceId, PVOID ParentDev, PVOID PotentialSerNum);
    QCDEVLIB_API ULONG  GetDeviceList(PVOID Buffer, ULONG BufferSize, PULONG ActualSize);
    QCDEVLIB_API HANDLE OpenDevice(PVOID DeviceName, DWORD Baudrate = DEV_DEFAULT_BAUD_RATE, BOOL isLegacyTimeoutConfig = false);
    QCDEVLIB_API VOID   CloseDevice(HANDLE hDevice);
    QCDEVLIB_API BOOL   ReadFromDevice
    (
        HANDLE hDevice,
        PVOID  RxBuffer,
        DWORD  NumBytesToRead,
        LPDWORD NumBytesReturned
    );
    QCDEVLIB_API BOOL SendToDevice
    (
        HANDLE hDevice,
        PVOID  TxBuffer,
        DWORD  NumBytesToSend,
        LPDWORD NumBytesSent
    );
}

#endif // QDPUB_H
