/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                        U T I L S . C

GENERAL DESCRIPTION
    This file implements utility functions including logging, device
    service file retrieval, and notification registration helpers for
    the Qualcomm MTU configuration service.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <conio.h>
#include <string.h>
#include <winioctl.h>
#include "utils.h"

BOOL WINAPI GetServiceFile
(
    HANDLE hd,
    PCHAR  ServiceFileName,
    UCHAR  ServiceType
)
{
    DWORD bytesReturned = 0;

    if (DeviceIoControl(
        hd,
        IOCTL_QCDEV_GET_SERVICE_FILE,
        (LPVOID)&ServiceType,
        (DWORD)sizeof(UCHAR),
        (LPVOID)ServiceFileName,
        (DWORD)SERVICE_FILE_BUF_LEN,
        &bytesReturned,
        NULL
        )
        )
    {
        printf("Got service file <%s>\n", ServiceFileName);

        return TRUE;
    }

    printf("GetServiceFile failure (error code 0x%x)\n", GetLastError());

    return FALSE;
}  // GetServiceFile

DWORD WINAPI RegisterNotification(PVOID Context)
{
    ULONG      event, rtnBytes = 0, ulNotification = 0;
    OVERLAPPED ov;
    BOOL       bResult;
    DWORD      dwStatus = NO_ERROR, dwReturnBytes = 0;
    PNOTIFICATION_CONTEXT ctxt = (PNOTIFICATION_CONTEXT)Context;

    ZeroMemory(&ov, sizeof(ov));
    ov.Offset = 0;
    ov.OffsetHigh = 0;
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL)
    {
        printf("RegisterDeviceNotification failure, evt error %u\n", GetLastError());
        ctxt->CallBack(ctxt->ServiceHandle, 0, ctxt->Context);
        free(ctxt);
        return 0;
    }

    QCMTU_Print("RegisterNotification: calling DeviceIoControl (handle 0x%x)\n", ctxt->ServiceHandle);
    bResult = DeviceIoControl
    (
        ctxt->ServiceHandle,
        IOCTL_QCDEV_WAIT_NOTIFY,
        NULL,
        0,
        &event,
        sizeof(event),
        &rtnBytes,
        &ov
    );
    QCMTU_Print("RegisterNotification: post-DeviceIoControl (handle 0x%x)\n", ctxt->ServiceHandle);
    if (bResult == TRUE)
    {
        ctxt->CallBack(ctxt->ServiceHandle, event, ctxt->Context);
    }
    else
    {
        dwStatus = GetLastError();

        if (ERROR_IO_PENDING != dwStatus)
        {
            printf("RegisterNotification failure, error %u\n", dwStatus);
            ctxt->CallBack(ctxt->ServiceHandle, 0, ctxt->Context);
        }
        else
        {
            bResult = GetOverlappedResult
            (
                ctxt->ServiceHandle,
                &ov,
                &dwReturnBytes,
                TRUE  // no return until operaqtion completes
            );

            if (bResult == FALSE)
            {
                dwStatus = GetLastError();
                printf("RegisterNotification/GetOverlappedResult failure, error %u\n", dwStatus);
                ctxt->CallBack(ctxt->ServiceHandle, 0, ctxt->Context);
            }
            else
            {
                ctxt->CallBack(ctxt->ServiceHandle, event, ctxt->Context);
            }
        }
    }

    if (ov.hEvent != NULL)
    {
        CloseHandle(ov.hEvent);
    }

    free(ctxt);

    return 0;

}  // RegisterDeviceNotification

// ================= Byte Ordering Functions ===============
USHORT rv16(PUSHORT s)
{
    USHORT v = *s;

    return (*s = ((v & 0xff00) >> 8) | ((v & 0x00ff) << 8));
}

ULONG rv32(PULONG l)
{
    ULONG v = *l;

    return (*l = ((v & 0xff000000) >> 24) |
            ((v & 0x00ff0000) >> 8) |
            ((v & 0x0000ff00) << 8) |
            ((v & 0x000000ff) << 24));
}

USHORT rv16v(USHORT v)
{
    return (((v & 0xff00) >> 8) | ((v & 0x00ff) << 8));
}

ULONG rv32v(ULONG v)
{
    return (((v & 0xff000000) >> 24) |
            ((v & 0x00ff0000) >> 8) |
            ((v & 0x0000ff00) << 8) |
            ((v & 0x000000ff) << 24));
}

// Formatted binary output
VOID USBUTL_PrintBytes(PVOID Buf, ULONG len, ULONG PktLen, char *info)
{
    char  buf[1024], *p, cBuf[128], *cp;
    char *buffer;
    ULONG count = 0, lastCnt = 0, spaceNeeded;
    ULONG i, s;

#define SPLIT_CHAR '|'

    buffer = (char *)Buf;
    RtlZeroMemory(buf, 1024);
    p = buf;
    cp = cBuf;

    if (PktLen < len)
    {
        len = PktLen;
    }

    sprintf(p, "\r\n\t   --- <%s> DATA %u/%u BYTES ---\r\n", info, len, PktLen);
    p += strlen(p);

    for (i = 1; i <= len; i++)
    {
        if (i % 16 == 1)
        {
            sprintf(p, "  %04u:  ", i - 1);
            p += 9;
        }

        sprintf(p, "%02X ", (UCHAR)buffer[i - 1]);
        if (isprint(buffer[i - 1]) && (!isspace(buffer[i - 1])))
        {
            sprintf(cp, "%c", buffer[i - 1]);
        }
        else
        {
            sprintf(cp, ".");
        }

        p += 3;
        cp += 1;

        if ((i % 16) == 8)
        {
            sprintf(p, "  ");
            p += 2;
        }
        if (i % 16 == 0)
        {
            if (i % 64 == 0)
            {
                sprintf(p, " %c  %s\r\n\r\n", SPLIT_CHAR, cBuf);
            }
            else
            {
                sprintf(p, " %c  %s\r\n", SPLIT_CHAR, cBuf);
            }

            QCSER_DbgPrint
            (
                QCSER_DBG_MASK_CONTROL,
                QCSER_DBG_LEVEL_FORCE,
                (buf)
            );
            RtlZeroMemory(buf, 1024);
            p = buf;
            cp = cBuf;
        }
    }

    lastCnt = i % 16;

    if (lastCnt == 0)
    {
        lastCnt = 16;
    }

    if (lastCnt != 1)
    {
        // 10 + 3*8 + 2 + 3*8 = 60 (full line bytes)
        spaceNeeded = (16 - lastCnt + 1) * 3;

        if (lastCnt <= 8)
        {
            spaceNeeded += 2;
        }
        for (s = 0; s < spaceNeeded; s++)
        {
            sprintf(p++, " ");
        }
        sprintf(p, " %c  %s\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n",
                SPLIT_CHAR, cBuf, info, len, PktLen);
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_FORCE,
            (buf)
        );
    }
    else
    {
        sprintf(buf, "\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n", info, len, PktLen);
        QCSER_DbgPrint
        (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_FORCE,
            (buf)
        );
    }
}  //USBUTL_PrintBytes

PCHAR GetCharArray
(
    PCHAR  InBuffer,
    PCHAR  CharArray,
    UCHAR  MaxElements,
    PUCHAR ActualElements
)
{
    PCHAR p = InBuffer;
    CHAR  valBuf[1024];
    PCHAR pv;
    int   v, n, idx = 0;

    ZeroMemory(valBuf, 1024);
    pv = valBuf;

    // search for '['
    while ((*p != '[') && (*p != 0))
    {
        p++;
    }

    if (*p == 0)
    {
        // syntax error
        *ActualElements = 0;
        return NULL;
    }

    p++;
    // copy the values in to buffer
    while ((*p != ']') && (*p != 0))
    {
        *pv++ = *p++;
    }

    // if no ']' found, syntax error
    if (*p == 0)
    {
        return NULL;
    }

    // ++p is the return value

    // process valBuf
    pv = valBuf;

    while (TRUE)
    {
        char valStr[512];

        n = sscanf(pv, "%s", valStr);
        if ((n == 0) || (n == EOF) || (idx >= MaxElements))
        {
            break;
        }

        sscanf(valStr, "%d", &v);
        CharArray[idx++] = (char)v;
        pv += (strlen(valStr) + 1);
    }

    *ActualElements = (UCHAR)idx;

    return ++p;

}  // GetCharArray

#ifdef QC_SERVICE

#include <stdio.h>
#include <stdarg.h>
#include <wtypes.h>

VOID _cdecl QCMTU_Print(PCHAR Format, ...)
{
    va_list arguments;
    UCHAR   msgBuffer[DBG_MSG_MAX_SZ];

    va_start(arguments, Format);
    _vsnprintf_s(msgBuffer, DBG_MSG_MAX_SZ, _TRUNCATE, Format, arguments);
    va_end(arguments);

    OutputDebugString(msgBuffer);
}

#endif // QC_SERVICE

