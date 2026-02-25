/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B D E V . C

GENERAL DESCRIPTION
    WDF file-object callbacks for the QDSS USB function driver. Handles
    file create, close, and cleanup events to associate USB pipe handles.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "QDBMAIN.h"
#include "QDBDEV.h"
#include "QDBRD.h"

#ifdef EVENT_TRACING
#include "QDBDEV.tmh"
#endif

/****************************************************************************
 *
 * function: QDBDEV_EvtDeviceFileCreate
 *
 * purpose:  WDF file-create callback. Validates the file name against
 *           the known channel names (TRACE, DEBUG, or DPL) and associates
 *           the corresponding USB pipe handles with the file context.
 *
 * arguments:Device     = WDF device handle
 *           Request    = WDF request handle for the create request
 *           FileObject = WDF file object being created
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBDEV_EvtDeviceFileCreate
(
    WDFDEVICE     Device,    // handle to a framework device object
    WDFREQUEST    Request,   // handle to request object representing file creation req
    WDFFILEOBJECT FileObject // handle to a framework file obj describing a file
)
{
    NTSTATUS        ntStatus = STATUS_UNSUCCESSFUL;
    PUNICODE_STRING fileName;
    PFILE_CONTEXT   pFileContext;
    PDEVICE_CONTEXT pDevContext;
    UNICODE_STRING  ucTraceFile, ucDebugFile, ucDplFile;

    pDevContext = QdbDeviceGetContext(Device);
    pFileContext = QdbFileGetContext(FileObject);

    QDB_DbgPrint
    (
        (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBDEV_EvtDeviceFileCreate: FileObject 0x%p\n", pDevContext->PortName, FileObject)
    );

    fileName = WdfFileObjectGetFileName(FileObject);

    QDB_DbgPrint
    (
        (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
        QDB_DBG_LEVEL_TRACE,
        ("<%s> QDBDEV_EvtDeviceFileCreate: <%wZ>\n", pDevContext->PortName, fileName)
    );

    if (0 == fileName->Length)
    {
        ntStatus = STATUS_INVALID_DEVICE_REQUEST;
        QDB_DbgPrint
        (
            (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBDEV_EvtDeviceFileCreate: no channel(TRACE/DEBUG) provided by app\n", pDevContext->PortName)
        );
    }
    else
    {
        pFileContext->DeviceContext = pDevContext;

        RtlInitUnicodeString(&ucTraceFile, QDSS_TRACE_FILE);
        RtlInitUnicodeString(&ucDebugFile, QDSS_DEBUG_FILE);
        RtlInitUnicodeString(&ucDplFile, QDSS_DPL_FILE);

        if (RtlCompareUnicodeString(&ucTraceFile, fileName, TRUE) == 0)
        {
            pFileContext->Type = QDB_FILE_TYPE_TRACE;
            pFileContext->TraceIN = pDevContext->TraceIN;
            ntStatus = STATUS_SUCCESS;
            QDB_DbgPrint
            (
                (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
                QDB_DBG_LEVEL_DETAIL,
                ("<%s> QDBDEV_EvtDeviceFileCreate: TRACE channel opened\n", pDevContext->PortName)
            );
        }
        else if (RtlCompareUnicodeString(&ucDebugFile, fileName, TRUE) == 0)
        {
            pFileContext->Type = QDB_FILE_TYPE_DEBUG;
            pFileContext->DebugIN = pDevContext->DebugIN;
            pFileContext->DebugOUT = pDevContext->DebugOUT;
            ntStatus = STATUS_SUCCESS;
            QDB_DbgPrint
            (
                (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
                QDB_DBG_LEVEL_DETAIL,
                ("<%s> QDBDEV_EvtDeviceFileCreate: DEBUG channel opened\n", pDevContext->PortName)
            );
        }
        else if (RtlCompareUnicodeString(&ucDplFile, fileName, TRUE) == 0)
        {
            pFileContext->Type = QDB_FILE_TYPE_DPL;
            pFileContext->TraceIN = pDevContext->TraceIN;
            ntStatus = STATUS_SUCCESS;
            QDB_DbgPrint
            (
                (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
                QDB_DBG_LEVEL_DETAIL,
                ("<%s> QDBDEV_EvtDeviceFileCreate: DPL channel opened\n", pDevContext->PortName)
            );
        }
        else
        {
            ntStatus = STATUS_INVALID_DEVICE_REQUEST;
            QDB_DbgPrint
            (
                (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
                QDB_DBG_LEVEL_ERROR,
                ("<%s> QDBDEV_EvtDeviceFileCreate: unrecognized channel name\n", pDevContext->PortName)
            );
        }

    }

    WdfRequestComplete(Request, ntStatus);

    QDB_DbgPrint
    (
        (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBDEV_EvtDeviceFileCreate: FileObject 0x%p ST 0x%X\n",
        pDevContext->PortName, FileObject, ntStatus)
    );

    return;
}  // QDBDEV_EvtDeviceFileCreate

/****************************************************************************
 *
 * function: QDBDEV_EvtDeviceFileClose
 *
 * purpose:  WDF file-close callback. Resumes DPL pipe draining after the
 *           application closes the file handle.
 *
 * arguments:FileObject = WDF file object being closed
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBDEV_EvtDeviceFileClose(WDFFILEOBJECT FileObject)
{
    PDEVICE_CONTEXT pDevContext;
    PFILE_CONTEXT   pFileContext;
    PUNICODE_STRING fileName;

    pFileContext = QdbFileGetContext(FileObject);
    pDevContext = pFileContext->DeviceContext;
    fileName = WdfFileObjectGetFileName(FileObject);

    QDB_DbgPrint
    (
        (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBDEV_EvtDeviceFileClose <%wZ> FileObject 0x%p\n",
        pDevContext->PortName, fileName, FileObject)
    );

    QDBRD_PipeDrainStart(pDevContext);

    QDB_DbgPrint
    (
        (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBDEV_EvtDeviceFileClose <%wZ> FileObject 0x%p\n",
        pDevContext->PortName, fileName, FileObject)
    );
    return;
}  // QDBDEV_EvtDeviceFileClose

/****************************************************************************
 *
 * function: QDBDEV_EvtDeviceFileCleanup
 *
 * purpose:  WDF file-cleanup callback. Resumes DPL pipe draining after
 *           the last handle to the file object is closed.
 *
 * arguments:FileObject = WDF file object being cleaned up
 *
 * returns:  VOID
 *
 ****************************************************************************/
VOID QDBDEV_EvtDeviceFileCleanup(WDFFILEOBJECT FileObject)
{
    PDEVICE_CONTEXT pDevContext;
    PFILE_CONTEXT   pFileContext;
    PUNICODE_STRING fileName;

    pFileContext = QdbFileGetContext(FileObject);
    pDevContext = pFileContext->DeviceContext;
    fileName = WdfFileObjectGetFileName(FileObject);

    QDB_DbgPrint
    (
        (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
        QDB_DBG_LEVEL_TRACE,
        ("<%s> -->QDBDEV_EvtDeviceFileCleanup <%wZ> FileObject 0x%p\n",
        pDevContext->PortName, fileName, FileObject)
    );

    QDBRD_PipeDrainStart(pDevContext);

    QDB_DbgPrint
    (
        (QDB_DBG_MASK_READ | QDB_DBG_MASK_WRITE),
        QDB_DBG_LEVEL_TRACE,
        ("<%s> <--QDBDEV_EvtDeviceFileCleanup <%wZ> FileObject 0x%p\n",
        pDevContext->PortName, fileName, FileObject)
    );
    return;
}  // QDBDEV_EvtDeviceFileCleanup
