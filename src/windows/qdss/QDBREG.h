/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B R E G . H

GENERAL DESCRIPTION
    Provides prototypes for shared KMDF registry utility functions.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef QDBREG_H
#define QDBREG_H

#include <ntddk.h>
#include <wdf.h>

/****************************************************************************
 * QDBREG_SetDriverRegistryStringW
 *
 * Writes a REG_SZ Unicode string to the driver's software registry key.
 *
 * Arguments:
 *   device     - WDF device handle
 *   pValueName - registry value name
 *   pValue     - null-terminated Unicode string to write
 *
 * Returns: NT status
 ****************************************************************************/
NTSTATUS QDBREG_SetDriverRegistryStringW
(
    _In_ WDFDEVICE    device,
    _In_ PWCHAR const pValueName,
    _In_ PWCHAR const pValue
);

/****************************************************************************
 * QDBREG_GetDriverRegistryStringW
 *
 * Reads a REG_SZ Unicode string from the driver's software registry key
 * into a caller-supplied buffer.
 *
 * Arguments:
 *   device     - WDF device handle
 *   pValueName - registry value name
 *   pOutBuf    - caller-supplied output buffer
 *   bufLen     - size of pOutBuf in bytes
 *
 * Returns: NT status
 ****************************************************************************/
NTSTATUS QDBREG_GetDriverRegistryStringW
(
    _In_                        WDFDEVICE    device,
    _In_                        PWCHAR const pValueName,
    _Out_writes_bytes_(bufLen)  PWCHAR       pOutBuf,
    _In_                        ULONG        bufLen
);

/****************************************************************************
 * QDBREG_SetDriverRegistryStringA
 *
 * Writes a REG_SZ value to the driver's software registry key from a
 * null-terminated ANSI string.  The string is converted to Unicode
 * before being written so that the registry entry is stored as REG_SZ.
 *
 * Arguments:
 *   device     - WDF device handle
 *   pValueName - registry value name
 *   pValue     - null-terminated ANSI string to write
 *
 * Returns: NT status
 ****************************************************************************/
NTSTATUS QDBREG_SetDriverRegistryStringA
(
    _In_ WDFDEVICE   device,
    _In_ PWCHAR const pValueName,
    _In_ PCHAR  const pValue
);

/****************************************************************************
 * QDBREG_SetDriverRegistryDword
 *
 * Writes a REG_DWORD value to the driver's software registry key.
 *
 * Arguments:
 *   device     - WDF device handle
 *   pValueName - registry value name
 *   value      - DWORD value to write
 *
 * Returns: NT status
 ****************************************************************************/
NTSTATUS QDBREG_SetDriverRegistryDword
(
    _In_ WDFDEVICE    device,
    _In_ PWCHAR const pValueName,
    _In_ ULONG        value
);

/****************************************************************************
 * QDBREG_GetDriverRegistryDword
 *
 * Reads a REG_DWORD value from the driver's software registry key.
 *
 * Arguments:
 *   device     - WDF device handle
 *   pValueName - registry value name
 *   pValue     - pointer to ULONG to receive the value
 *
 * Returns: NT status
 ****************************************************************************/
NTSTATUS QDBREG_GetDriverRegistryDword
(
    _In_  WDFDEVICE    device,
    _In_  PWCHAR const pValueName,
    _Out_ PULONG       pValue
);

/****************************************************************************
 * QDBREG_DeleteDriverRegistryValue
 *
 * Removes a value from the driver's software registry key.
 *
 * Arguments:
 *   device     - WDF device handle
 *   pValueName - registry value name to delete
 *
 * Returns: NT status
 ****************************************************************************/
NTSTATUS QDBREG_DeleteDriverRegistryValue
(
    _In_ WDFDEVICE    device,
    _In_ PWCHAR const pValueName
);

#endif  // QDBREG_H
