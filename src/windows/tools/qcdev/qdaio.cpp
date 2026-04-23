/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D A I O . C P P

GENERAL DESCRIPTION
    This file implements asynchronous I/O operations for Qualcomm USB device
    communication, including device open/close, send/receive, and I/O thread
    management.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "qdpublic.h"
#include "scandev.h"
#include "qdaio.h"
#include <new>

using namespace QDAIO;
using namespace QcDevice;

namespace QDAIO
{
    QDAIODEV AioDevice[QDA_MAX_DEV];
    LONG     DevInitialized = 0;
    CRITICAL_SECTION MasterLock;

    //   namespace  // unnamed
    //   {
    VOID InitDevExtension(PQDAIODEV IoDev)
    {
        PQDAIODEV_EXTENSION pDevExt = (PQDAIODEV_EXTENSION)IoDev->DevExtension;
        PQDAIO_THREAD_CONTEXT pThreadContext;
        PAIO_ITEM pIoItem;
        int ioType;
        int ioItemIdx;

        // initialize extension

        // QIO_RX (0)  QIO_TX (1)
        for (ioType = 0; ioType < 2; ioType++)
        {
            // RX/TX idle queues
            InitializeListHead(&(pDevExt->IoItemIdleQueue[ioType]));
            pIoItem = pDevExt->IoPool[ioType] = new (std::nothrow) AIO_ITEM[IoDev->IoItemPoolSize];
            if (pIoItem != NULL)
            {
                for (ioItemIdx = 0; ioItemIdx < IoDev->IoItemPoolSize; ioItemIdx++)
                {
                    pIoItem->State = IO_ITEM_IDLE;
                    pIoItem->Index = ioItemIdx;
                    ZeroMemory(&(pIoItem->OverlappedContext), sizeof(OVERLAPPED));
                    pIoItem->OverlappedContext.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                    InsertTailList(&(pDevExt->IoItemIdleQueue[ioType]), &(pIoItem->List));
                    pIoItem++;
                }
            }
            InitializeCriticalSection(&(pDevExt->IoPoolLock[ioType]));

            // RX/TX queues
            InitializeListHead(&(pDevExt->DispatchQueue[ioType]));
            InitializeListHead(&(pDevExt->PendingQueue[ioType]));
            InitializeListHead(&(pDevExt->CompletionQueue[ioType]));

            // RX/TX thread handles
            pDevExt->IoThreadHandle[ioType] = INVALID_HANDLE_VALUE;
            pDevExt->CompletionThreadHandle[ioType] = INVALID_HANDLE_VALUE;

            // RX/TX thread events
            pDevExt->IoThreadStartedEvt[ioType] = CreateEvent(NULL, FALSE, FALSE, NULL);
            pDevExt->IoThreadTerminatedEvt[ioType] = CreateEvent(NULL, FALSE, FALSE, NULL);
            pDevExt->CompletionThreadStartedEvt[ioType] = CreateEvent(NULL, FALSE, FALSE, NULL);
            pDevExt->CompletionThreadTerminatedEvt[ioType] = CreateEvent(NULL, FALSE, FALSE, NULL);
            pDevExt->CancelCompletionThreadEvt[ioType] = CreateEvent(NULL, FALSE, FALSE, NULL);

            // RX/TX action events
            pDevExt->DispatchEvt[ioType] = CreateEvent(NULL, FALSE, FALSE, NULL);
            pDevExt->CompletionEvt[ioType] = CreateEvent(NULL, FALSE, FALSE, NULL);
            pDevExt->CancelEvt[ioType] = CreateEvent(NULL, FALSE, FALSE, NULL);

            // Locks
            InitializeCriticalSection(&(pDevExt->IoLock[ioType]));
            InitializeCriticalSection(&(pDevExt->CompletionLock[ioType]));

            // thread context
            pThreadContext = &(pDevExt->ThreadContext[ioType]);
            pThreadContext->Dev = IoDev;
            pThreadContext->IoType = ioType;

            // stats
            pDevExt->OutstandingIo[ioType] = 0;
        }
    }  // InitDevExtension

    VOID ResetDevExtension(PQDAIODEV IoDev)
    {
        PQDAIODEV_EXTENSION pDevExt = (PQDAIODEV_EXTENSION)IoDev->DevExtension;
        int i;

        QCD_Printf("QDAIO::ResetDevExtension 0x%p\n", pDevExt);

        for (i = 0; i < 2; i++)
        {
            // RX/TX thread handles
            pDevExt->IoThreadHandle[i] = INVALID_HANDLE_VALUE;
            pDevExt->CompletionThreadHandle[i] = INVALID_HANDLE_VALUE;
        }
    }  // ResetDevExtension

    // RX/TX threads should be cancelled before calling this
    VOID PurgeDevExtension(PQDAIODEV Dev)
    {
        PAIO_ITEM pIoItem;
        int ioItemIdx;
        PQDAIODEV_EXTENSION pDevExt = (PQDAIODEV_EXTENSION)Dev->DevExtension;
        int ioType;

        QCD_Printf("QDAIO::PurgeDevExtension[%02d] 0x%p\n", Dev->AppHandle, pDevExt);

        EnterCriticalSection(&MasterLock);

        Dev->DevHandle = INVALID_HANDLE_VALUE;

        for (ioType = 0; ioType < 2; ioType++)
        {
            pIoItem = pDevExt->IoPool[ioType];
            for (ioItemIdx = 0; ioItemIdx < Dev->IoItemPoolSize; ioItemIdx++)
            {
                CloseHandle(pIoItem->OverlappedContext.hEvent);
            }
            CloseHandle(pDevExt->DispatchEvt[ioType]);
            CloseHandle(pDevExt->CompletionEvt[ioType]);
            CloseHandle(pDevExt->CancelEvt[ioType]);
            CloseHandle(pDevExt->IoThreadStartedEvt[ioType]);
            CloseHandle(pDevExt->IoThreadTerminatedEvt[ioType]);
            CloseHandle(pDevExt->CompletionThreadStartedEvt[ioType]);
            CloseHandle(pDevExt->CompletionThreadTerminatedEvt[ioType]);
            CloseHandle(pDevExt->CancelCompletionThreadEvt[ioType]);
            pDevExt->IoThreadHandle[ioType] = INVALID_HANDLE_VALUE;
            pDevExt->CompletionThreadHandle[ioType] = INVALID_HANDLE_VALUE;
            DeleteCriticalSection(&(pDevExt->IoLock[ioType]));
            DeleteCriticalSection(&(pDevExt->CompletionLock[ioType]));
            DeleteCriticalSection(&(pDevExt->IoPoolLock[ioType]));
            delete[] pDevExt->IoPool[ioType];
        }
        LeaveCriticalSection(&MasterLock);
    }  // PurgeDevExtension

    // To cancel all if Context == NULL
    int FindAndCancel(PQDAIODEV Dev, int IoType, PVOID UserContext)
    {
        PQDAIODEV pIoDev = Dev;
        PQDAIODEV_EXTENSION pDevExt = (PQDAIODEV_EXTENSION)pIoDev->DevExtension;
        PAIO_ITEM item;
        PCRITICAL_SECTION ioLock;
        PLIST_ENTRY dispatchQueue;
        PLIST_ENTRY pendingQueue;
        PLIST_ENTRY peekItem;
        HANDLE dispatchEvt;
        HANDLE completionEvt;
        int itemFound = 0;

        QCD_Printf("QDAIO::FindAndCancel IoDev 0x%p Ext 0x%p Ctxt 0x%p\n", pIoDev->DevExtension, UserContext);

        if (IoType < 0 || IoType > 1)
        {
            return itemFound;
        }

        if (UserContext == NULL)
        {
            // cancel all
            CancelIoEx(pIoDev->DevHandle, NULL);
            itemFound = 1;
            return itemFound;
        }

        ioLock = &(pDevExt->IoLock[IoType]);
        dispatchQueue = &(pDevExt->DispatchQueue[IoType]);
        pendingQueue = &(pDevExt->PendingQueue[IoType]);
        dispatchEvt = pDevExt->DispatchEvt[IoType];
        completionEvt = pDevExt->CompletionEvt[IoType];

        EnterCriticalSection(ioLock);
        if (!IsListEmpty(dispatchQueue))
        {
            // find item and en-queue to CancelQueue
            peekItem = dispatchQueue->Flink;
            while (peekItem != dispatchQueue)
            {
                item = CONTAINING_RECORD(peekItem, AIO_ITEM, List);
                peekItem = peekItem->Flink;
                if (item->UserContext == UserContext)
                {
                    itemFound = 1;
                    item->Status = QDAIO_STATUS_CANCELLED;
                    item->IoSize = 0;
                    CancelIoEx(pIoDev->DevHandle, &item->OverlappedContext);
                    break;
                }
            }  // while
        } // if
        LeaveCriticalSection(ioLock);

        return itemFound;
    }  // FindAndCancel

    DWORD WINAPI IoThread(PVOID Context)
    {
        PQDAIO_THREAD_CONTEXT ioContext = (PQDAIO_THREAD_CONTEXT)Context;
        PQDAIODEV pIoDev;
        PQDAIODEV_EXTENSION pDevExt;
        PAIO_ITEM ioItem;
        PLIST_ENTRY dispatchQueue, completionQueue;
        HANDLE dispatchEvt;
        HANDLE completionEvt;
        PCRITICAL_SECTION ioLock;
        PCRITICAL_SECTION completionLock;
        PLIST_ENTRY pEntry;
        DWORD waitIdx;
        BOOL bNotDone = TRUE;

        pIoDev = ioContext->Dev;
        pDevExt = (PQDAIODEV_EXTENSION)pIoDev->DevExtension;

        if ((ioContext->IoType != QIO_RX) && (ioContext->IoType != QIO_TX))
        {
            QCD_Printf("QDAIO::IoThread: invalid IoType %d\n", ioContext->IoType);
            return 0;
        }

        QCD_Printf("-->QDAIO::IoThread: Dev[%d] 0x%p IoType %d\n", pIoDev->AppHandle, pIoDev, ioContext->IoType);

        ioLock = &(pDevExt->IoLock[ioContext->IoType]);
        completionLock = &pDevExt->CompletionLock[ioContext->IoType];
        dispatchEvt = pDevExt->DispatchEvt[ioContext->IoType];
        completionEvt = pDevExt->CompletionEvt[ioContext->IoType];
        dispatchQueue = &(pDevExt->DispatchQueue[ioContext->IoType]);
        completionQueue = &(pDevExt->CompletionQueue[ioContext->IoType]);

        SetEvent(pDevExt->IoThreadStartedEvt[ioContext->IoType]);

        while (bNotDone == TRUE)
        {
            DWORD waitState;
            BOOL  bWait;

            EnterCriticalSection(ioLock);

            if ((pIoDev->DevState == QDAIO_DEV_STATE_CLOSE) && (IsListEmpty(dispatchQueue)))
            {
                LeaveCriticalSection(ioLock);
                bNotDone = FALSE;  // to exit thread
                continue;
            }

            if ((IsListEmpty(dispatchQueue)) && (pIoDev->DevState == QDAIO_DEV_STATE_OPEN))
            {
                bWait = TRUE;
            }
            else
            {
                bWait = FALSE;
            }
            LeaveCriticalSection(ioLock);

            if (bWait == TRUE)
            {
                // QCD_Printf("QDAIO::IoThread[%d]: wait 1s for dispatch\n", ioContext->IoType); // LOG
                waitState = WaitForSingleObject(dispatchEvt, 1000);  // 1s timeout
                ResetEvent(dispatchEvt);
                if (waitState == WAIT_OBJECT_0)
                {
                    // QCD_Printf("QDAIO::IoThread[%d]: req in queue\n", ioContext->IoType); // LOG
                }
                else
                {
                    QCD_Printf("QDAIO::IoThread[%d]: Warning: WAIT 0x%x\n", ioContext->IoType, waitState);
                    continue;
                }
            }

            EnterCriticalSection(ioLock);

            while (!IsListEmpty(dispatchQueue))
            {
                // dequeue the head item to wait for
                pEntry = RemoveHeadList(dispatchQueue);
                ioItem = CONTAINING_RECORD(pEntry, AIO_ITEM, List);
                ioItem->State = IO_ITEM_PENDING;
                LeaveCriticalSection(ioLock);

                waitIdx = WAIT_TIMEOUT;
                while (waitIdx == WAIT_TIMEOUT)
                {
                    waitIdx = WaitForSingleObject(ioItem->OverlappedContext.hEvent, 1000);  // 1s timeout
                    // QCD_Printf("QDAIO::IoThread[%d]: wait IO[%d] 0x%p\n", ioContext->IoType, ioItem->Index, ioItem);
                    if (waitIdx == WAIT_TIMEOUT)
                    {
                        QCD_Printf("QDAIO::IoThread[%d]: timeout, Idx %d\n", ioContext->IoType, ioItem->Index);
                    }
                }
                if (waitIdx == WAIT_OBJECT_0)
                {
                    // QCD_Printf("QDAIO::IoThread[%d]: IO to be back\n", ioContext->IoType); // LOG
                }
                else
                {
                    QCD_Printf("QDAIO::IoThread[%d]: IO anomaly 0x%x\n", ioContext->IoType, waitIdx);
                }

                if (TRUE)
                {
                    DWORD lastErr;
                    BOOLEAN ioResult;

                    // QCD_Printf("QDAIO::IoThread[%d]: wait OV 0x%p\n", ioContext->IoType, &(ioItem->OverlappedContext)); // LOG
                    ioResult = GetOverlappedResult
                    (
                        pIoDev->DevHandle,
                        &(ioItem->OverlappedContext),
                        &(ioItem->IoSize),
                        TRUE  // no return until operaqtion completes
                    );
                    lastErr = GetLastError();

                    if (ioResult == TRUE)
                    {
                        // QCD_Printf("IoThread: OV success, 0x%p\n", &(ioItem->OverlappedContext));  // LOG
                        ioItem->Status = QDAIO_STATUS_SUCCESS;
                        ioItem->State = IO_ITEM_COMPLETED;
                    }
                    else if (lastErr == ERROR_IO_PENDING)
                    {
                        QCD_Printf("IoThread: OV 0x%p still pending after wait???\n", &(ioItem->OverlappedContext));
                        waitIdx = WAIT_TIMEOUT;
                        continue;
                    }
                    else
                    {
                        // QCD_Printf("IoThread: OV 0x%p failed\n", &(ioItem->OverlappedContext));
                        ioItem->Status = QDAIO_STATUS_FAILURE;
                        ioItem->State = IO_ITEM_COMPLETED;
                        QCD_Printf("QDAIO::IoThread[%d]: failure, DevIdx %02ld [%03d] Outstanding %d err %d\n",
                                   ioContext->IoType, pIoDev->AppHandle, ioItem->Index,
                                   (pDevExt->OutstandingIo[ioContext->IoType] - 1), lastErr);
                    }
                    if (ioItem->State == IO_ITEM_COMPLETED)
                    {
                        InterlockedDecrement(&(pDevExt->OutstandingIo[ioContext->IoType]));
                        // enqueue to completionQueue
                        EnterCriticalSection(completionLock);
                        InsertTailList(completionQueue, &(ioItem->List));
                        LeaveCriticalSection(completionLock);
                        SetEvent(completionEvt);
                    }
                }
                EnterCriticalSection(ioLock);
            }  // while -- DispatchQueue
            LeaveCriticalSection(ioLock);
        }  // while (bNotDone == TRUE)

        SetEvent(pDevExt->IoThreadTerminatedEvt[ioContext->IoType]);

        QCD_Printf("<--QDAIO::IoThread: Dev[%d] 0x%p IoType %d\n", pIoDev->AppHandle, pIoDev, ioContext->IoType);

        return 0;
    }  // Function -- IoThread

    BOOL WINAPI StartIoThread(PQDAIODEV IoDev)
    {
        BOOL bResult = TRUE;
        PQDAIODEV_EXTENSION   pDevExt;
        PQDAIO_THREAD_CONTEXT pRx, pTx;
        DWORD waitIdx;

        pDevExt = (PQDAIODEV_EXTENSION)IoDev->DevExtension;
        pRx = &(pDevExt->ThreadContext[QIO_RX]);
        pTx = &(pDevExt->ThreadContext[QIO_TX]);

        QCD_Printf("-->StartIoThread: 0x%p\n", IoDev);

        pDevExt->IoThreadHandle[QIO_RX] = ::CreateThread(NULL, 0, IoThread, (LPVOID)pRx, NULL, 0);
        if (pDevExt->IoThreadHandle[QIO_RX] != INVALID_HANDLE_VALUE)
        {
            waitIdx = WaitForSingleObject(pDevExt->IoThreadStartedEvt[QIO_RX], 500);
            ResetEvent(pDevExt->IoThreadStartedEvt[QIO_RX]);
            QCD_Printf("QDAIO::StartIoThread: RX 0x%x\n", waitIdx);

            pDevExt->IoThreadHandle[QIO_TX] = ::CreateThread(NULL, 0, IoThread, (LPVOID)pTx, NULL, 0);
            if (pDevExt->IoThreadHandle[QIO_TX] != INVALID_HANDLE_VALUE)
            {
                waitIdx = WaitForSingleObject(pDevExt->IoThreadStartedEvt[QIO_TX], 500);
                ResetEvent(pDevExt->IoThreadStartedEvt[QIO_TX]);
                QCD_Printf("QDAIO::StartIoThread: TX 0x%x\n", waitIdx);
            }
            else
            {
                bResult = FALSE;
                QCD_Printf("<--StartIoThread: TX thread creation failure, dev 0x%p\n", IoDev);
            }
        }
        else
        {
            bResult = FALSE;
            QCD_Printf("<--StartIoThread: RX thread creation failure, dev 0x%p\n", IoDev);
        }

        return bResult;
    }  // StartIoThread

    VOID WINAPI CancelIoThread(PQDAIODEV IoDev)
    {
        PQDAIODEV_EXTENSION pDevExt;
        DWORD waitIdx;

        pDevExt = (PQDAIODEV_EXTENSION)IoDev->DevExtension;

        // Cancel RX
        QCD_Printf("CancelIoThread: wait RX... Dev 0x%p\n", IoDev);
        SetEvent(pDevExt->DispatchEvt[QIO_RX]);  // wake up
        waitIdx = WaitForSingleObject(pDevExt->IoThreadTerminatedEvt[QIO_RX], 2000);
        QCD_Printf("QDAIO::CancelIoThread: RX 0x%x\n", waitIdx);

        // Cancel TX
        QCD_Printf("CancelIoThread: wait TX... Dev 0x%p\n", IoDev);
        SetEvent(pDevExt->DispatchEvt[QIO_TX]);  // wake up
        waitIdx = WaitForSingleObject(pDevExt->IoThreadTerminatedEvt[QIO_TX], 2000);
        QCD_Printf("QDAIO::CancelIoThread: TX 0x%x\n", waitIdx);
    }  // CancelIoThread

    PAIO_ITEM GetIoItem(PQDAIODEV Dev, int IoType)
    {
        PQDAIODEV_EXTENSION pDevExt;
        PAIO_ITEM pItem = NULL;
        PLIST_ENTRY pEntry;

        pDevExt = (PQDAIODEV_EXTENSION)Dev->DevExtension;

        EnterCriticalSection(&(pDevExt->IoPoolLock[IoType]));

        if (!IsListEmpty(&(pDevExt->IoItemIdleQueue[IoType])))
        {
            pEntry = RemoveHeadList(&(pDevExt->IoItemIdleQueue[IoType]));
            pItem = CONTAINING_RECORD(pEntry, AIO_ITEM, List);
        }

        LeaveCriticalSection(&(pDevExt->IoPoolLock[IoType]));

        return pItem;
    }  // Function -- GetIoItem

    VOID RecycleIoItem(PQDAIODEV Dev, PAIO_ITEM IoItem, int IoType)
    {
        PQDAIODEV_EXTENSION pDevExt;

        pDevExt = (PQDAIODEV_EXTENSION)Dev->DevExtension;

        IoItem->Buf = NULL;
        IoItem->IoSize = 0;
        IoItem->IoCallback = NULL;
        IoItem->UserContext = NULL;
        IoItem->Status = 0;

        IoItem->OverlappedContext.Internal = 0;
        IoItem->OverlappedContext.InternalHigh = 0;
        IoItem->OverlappedContext.Pointer = 0;
        IoItem->OverlappedContext.Offset = 0;
        IoItem->OverlappedContext.OffsetHigh = 0;

        EnterCriticalSection(&(pDevExt->IoPoolLock[IoType]));
        IoItem->State = IO_ITEM_IDLE;
        InsertTailList(&(pDevExt->IoItemIdleQueue[IoType]), &(IoItem->List));
        LeaveCriticalSection(&(pDevExt->IoPoolLock[IoType]));
    }  // Function -- RecycleIoItem

    VOID WINAPI PurgeCompletionQueue(PQDAIO_THREAD_CONTEXT pContext)
    {
        PQDAIODEV pIoDev;
        PQDAIODEV_EXTENSION pDevExt;
        PCRITICAL_SECTION pCompLock;
        PLIST_ENTRY completionQueue, pEntry;
        PAIO_ITEM ioItem;

        pIoDev = pContext->Dev;
        pDevExt = (PQDAIODEV_EXTENSION)pIoDev->DevExtension;
        pCompLock = &(pDevExt->CompletionLock[pContext->IoType]);
        completionQueue = &(pDevExt->CompletionQueue[pContext->IoType]);

        EnterCriticalSection(pCompLock);

        while (!IsListEmpty(completionQueue))
        {
            pEntry = RemoveHeadList(completionQueue);
            LeaveCriticalSection(pCompLock);

            ioItem = CONTAINING_RECORD(pEntry, AIO_ITEM, List);
            ioItem->IoCallback
            (
                pIoDev->AppHandle, ioItem->UserContext,
                ioItem->Status, ioItem->IoSize
            );
            RecycleIoItem(pIoDev, ioItem, pContext->IoType);

            EnterCriticalSection(pCompLock);
        }

        LeaveCriticalSection(pCompLock);

    }  // PurgeCompletionQueue

    DWORD WINAPI CompletionThread(PVOID Context)
    {
        PQDAIO_THREAD_CONTEXT pContext = (PQDAIO_THREAD_CONTEXT)Context;
        PQDAIODEV pIoDev;
        PQDAIODEV_EXTENSION pDevExt;
        PCRITICAL_SECTION pCompLock;
        PLIST_ENTRY completionQueue;
        BOOL notDone = TRUE;
        HANDLE hEvents[2];
        DWORD waitStatus;

        pIoDev = pContext->Dev;
        pDevExt = (PQDAIODEV_EXTENSION)pIoDev->DevExtension;
        pCompLock = &(pDevExt->CompletionLock[pContext->IoType]);
        completionQueue = &(pDevExt->CompletionQueue[pContext->IoType]);
        hEvents[0] = pDevExt->CancelCompletionThreadEvt[pContext->IoType];
        hEvents[1] = pDevExt->CompletionEvt[pContext->IoType];

        SetEvent(pDevExt->CompletionThreadStartedEvt[pContext->IoType]);

        while (notDone == TRUE)
        {
            // wait for events
            waitStatus = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
            switch (waitStatus)
            {
                case WAIT_OBJECT_0: // cancel
                {
                    QCD_Printf("QDAIO::CompletionThread: app %d CxlEvt\n", pIoDev->AppHandle);
                    ResetEvent(hEvents[0]);
                    // purge completionQueue
                    PurgeCompletionQueue(pContext);
                    notDone = FALSE;
                    break;
                }
                case WAIT_OBJECT_0 + 1: // completion
                {
                    // QCD_Printf("QDAIO::CompletionThread: app %d CompletionEvt\n", pIoDev->AppHandle); // LOG
                    ResetEvent(hEvents[1]);
                    PurgeCompletionQueue(pContext);
                    break;
                }
                default:
                {
                    break;
                }
            }  // switch
        }  // while (notDone == TRUE)

        PurgeCompletionQueue(pContext);  // final cleanup
        SetEvent(pDevExt->CompletionThreadTerminatedEvt[pContext->IoType]);
        return 0;
    }  // CompletionThread

    BOOL WINAPI StartCompletionThread(PQDAIODEV IoDev)
    {
        BOOL bResult = TRUE;
        PQDAIODEV_EXTENSION pDevExt;
        PQDAIO_THREAD_CONTEXT pRx, pTx;
        DWORD waitIdx;

        pDevExt = (PQDAIODEV_EXTENSION)IoDev->DevExtension;
        pRx = &(pDevExt->ThreadContext[QIO_RX]);
        pTx = &(pDevExt->ThreadContext[QIO_TX]);

        QCD_Printf("-->QDAIO::StartCompletionThread: Dev 0x%p\n", IoDev);

        pDevExt->CompletionThreadHandle[QIO_RX] = ::CreateThread(NULL, 0, CompletionThread, (LPVOID)pRx, NULL, 0);
        if (pDevExt->CompletionThreadHandle[QIO_RX] != INVALID_HANDLE_VALUE)
        {
            waitIdx = WaitForSingleObject(pDevExt->CompletionThreadStartedEvt[QIO_RX], 500);
            ResetEvent(pDevExt->CompletionThreadStartedEvt[QIO_RX]);
            QCD_Printf("QDAIO::StartCompletionThread: RX 0x%x\n", waitIdx);

            pDevExt->CompletionThreadHandle[QIO_TX] = ::CreateThread(NULL, 0, CompletionThread, (LPVOID)pTx, NULL, 0);
            if (pDevExt->CompletionThreadHandle[QIO_TX] != INVALID_HANDLE_VALUE)
            {
                waitIdx = WaitForSingleObject(pDevExt->CompletionThreadStartedEvt[QIO_TX], 500);
                ResetEvent(pDevExt->CompletionThreadStartedEvt[QIO_TX]);
                QCD_Printf("QDAIO::StartCompletionThread: TX 0x%x\n", waitIdx);
            }
            else
            {
                QCD_Printf("QDAIO::StartCompletionThread: TX thread creation failure, Dev 0x%p\n", IoDev);
                bResult = FALSE;
            }
        }
        else
        {
            QCD_Printf("QDAIO::StartCompletionThread: RX thread creation failure, Dev 0x%p\n", IoDev);
            bResult = FALSE;
        }
        QCD_Printf("<--QDAIO::StartCompletionThread: Dev 0x%p\n", IoDev);

        return bResult;
    }  // StartCompletionThread

    VOID WINAPI CancelCompletionThread(PQDAIODEV IoDev)
    {
        PQDAIODEV_EXTENSION pDevExt;
        DWORD waitIdx;

        pDevExt = (PQDAIODEV_EXTENSION)IoDev->DevExtension;

        // Cancel RX
        SetEvent(pDevExt->CancelCompletionThreadEvt[QIO_RX]);
        waitIdx = WaitForSingleObject(pDevExt->CompletionThreadTerminatedEvt[QIO_RX], 2000);
        QCD_Printf("QDAIO::CancelCompletionThread: RX 0x%x\n", waitIdx);

        // Cancel TX
        SetEvent(pDevExt->CancelCompletionThreadEvt[QIO_TX]);
        waitIdx = WaitForSingleObject(pDevExt->CompletionThreadTerminatedEvt[QIO_TX], 2000);
        QCD_Printf("QDAIO::CancelCompletionThread: TX 0x%x\n", waitIdx);

    }  // CancelCompletionThread

    int WINAPI ConfigureCommChannel(UCHAR DevType, HANDLE DeviceHandle)
    {
        int retVal = 0;

        if (DevType == QC_DEV_TYPE_PORTS)
        {
            DCB dcb;
            COMMTIMEOUTS commTimeouts;

            dcb.DCBlength = sizeof(DCB);
            if (GetCommState(DeviceHandle, &dcb) == TRUE)
            {
                //dcb.BaudRate    = 3000000;
                dcb.BaudRate = 4800;
                dcb.ByteSize = 8;
                dcb.Parity = NOPARITY;
                dcb.StopBits = ONESTOPBIT;
                if (SetCommState(DeviceHandle, &dcb) == FALSE)
                {
                    QCD_Printf("[Type_%d] SetCommState failed on handle 0x%x, error 0x%x\n",
                               DevType, DeviceHandle, GetLastError());
                    retVal = 1;
                }
                else
                {
                    QCD_Printf("[Type_%d] SetCommState done", DevType);
                }
            }
            else
            {
                QCD_Printf("[Type_%d] GetCommState failed on handle 0x%x, error 0x%x\n",
                           DevType, DeviceHandle, GetLastError());
                retVal = 2;
            }

            commTimeouts.ReadIntervalTimeout = MAXDWORD;
            commTimeouts.ReadTotalTimeoutMultiplier = 0;
            commTimeouts.ReadTotalTimeoutConstant = 2000;
            commTimeouts.WriteTotalTimeoutMultiplier = 0;
            commTimeouts.WriteTotalTimeoutConstant = 3000;
            if (SetCommTimeouts(DeviceHandle, &commTimeouts) == false)
            {
                QCD_Printf("[Type_%d] SetCommTimeouts failed on handle 0x%x Error 0x%x\n",
                           DevType, DeviceHandle, GetLastError());
                retVal = 3;
            }
            else
            {
                QCD_Printf("[Type_%d] SetCommTimeouts done", DevType);
            }
        }
        return retVal;
    }  // ConfigureCommChannel

 //   }  // namespace  // unnamed
}  // namespace QDAIO

QCDEVLIB_API BOOL QDAIO::Initialize(VOID)
{
    LONG i;

    if (InterlockedIncrement(&DevInitialized) > 1)
    {
        InterlockedDecrement(&DevInitialized);
        return FALSE;
    }

    InitializeCriticalSection(&MasterLock);
    for (i = 0; i < QDA_MAX_DEV; i++)
    {
        AioDevice[i].AppHandle = i;  // psudo handle
        AioDevice[i].DevState = QDAIO_DEV_STATE_CLOSE;
        AioDevice[i].IoItemPoolSize = QDAIO_IO_POOL_SIZE; // TODO: seetable in the future

        AioDevice[i].DevExtension = new (std::nothrow) QDAIODEV_EXTENSION;
        if (AioDevice[i].DevExtension == NULL)
        {
            QCD_Printf("QDAIO::Initialize: error - no mem at idx %d\n", i);
            QDAIO::Cleanup();
            return FALSE;
        }
        InitDevExtension(&AioDevice[i]);
    }
    QCD_Printf("<--QDAIO::Initialize -- InitDevExtension (%d)\n", i);
    return TRUE;
}  // Initialize

QCDEVLIB_API VOID QDAIO::Cleanup(VOID)
{
    PQDAIODEV_EXTENSION pDevExt;
    LONG devIdx;

    // TODO: make sure all devices are closed and extension purged
    for (devIdx = 0; devIdx < QDA_MAX_DEV; devIdx++)
    {
        pDevExt = (PQDAIODEV_EXTENSION)AioDevice[devIdx].DevExtension;
        if (pDevExt != NULL)
        {
            PurgeDevExtension(&AioDevice[devIdx]);
            delete AioDevice[devIdx].DevExtension;
            AioDevice[devIdx].DevExtension = NULL;
        }
        else
        {
            // NULL is set when Initialize fails to allocate memory
            break;
        }
    }
    DeleteCriticalSection(&MasterLock);
    QCD_Printf("<--QDAIO::Cleanup -- PurgeDevExtension (%d)\n", devIdx);
}

QCDEVLIB_API LONG QDAIO::OpenDevice(PVOID DeviceName)
{
    LONG i, appHdl = -1;
    PQDAIODEV pIoDev = NULL;
    PQDAIODEV_EXTENSION pDevExt = NULL;
    HANDLE hDevice = INVALID_HANDLE_VALUE;  // -1

    if (DeviceName == NULL)
    {
        return QDAIO_STATUS_INVALID_DEVICE;
    }

    EnterCriticalSection(&MasterLock);
    if (DevInitialized == 0)
    {
        LeaveCriticalSection(&MasterLock);
        return QDAIO_STATUS_DEVICE_NOT_INIT;
    }

    // locate a free slot, start with index 1 to avoid conflict with QDAIO_STATUS_SUCCESS
    for (i = 1; i < QDA_MAX_DEV; i++)
    {
        if (AioDevice[i].DevState == QDAIO_DEV_STATE_CLOSE)
        {
            appHdl = i;
            pIoDev = (PQDAIODEV) & (AioDevice[i]);
            pDevExt = (PQDAIODEV_EXTENSION)pIoDev->DevExtension;
            break;
        }
    }
    LeaveCriticalSection(&MasterLock);

    if ((appHdl >= QDA_MAX_DEV) || (appHdl < 0))
    {
        return QDAIO_STATUS_BAD_HANDLE;
    }
    if (pIoDev == NULL || pDevExt == NULL)
    {
        return QDAIO_STATUS_NO_DEVICE;
    }

    // initialize extension
    ResetDevExtension(pIoDev);

    hDevice = ::CreateFileA
    (
        (PCHAR)DeviceName,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, // async operation
        NULL
    );
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        QCD_Printf("QDAIO::OpenDevice: Error 0x%x <%s>\n", GetLastError(), (PCHAR)DeviceName);
        return QDAIO_STATUS_HANDLE_FAILURE;
    }
    else
    {
        pIoDev->DevHandle = hDevice;
        ConfigureCommChannel(QC_DEV_TYPE_PORTS, hDevice);
        QCD_Printf("QDAIO::OpenDevice: Handle 0x%x/0x%x\n", appHdl, hDevice);

        pIoDev->DevState = QDAIO_DEV_STATE_OPEN;
        StartIoThread(pIoDev);
        StartCompletionThread(pIoDev);
    }

    return appHdl;
}  // OpenDevice


QCDEVLIB_API LONG QDAIO::OpenDevice(PVOID DeviceName, DWORD Baudrate, BOOL isLegacyTimeoutConfig)
{
    LONG i, appHdl = -1;
    PQDAIODEV pIoDev = NULL;
    PQDAIODEV_EXTENSION pDevExt = NULL;
    HANDLE hDevice = INVALID_HANDLE_VALUE;  // -1

    if (DeviceName == NULL)
    {
        return QDAIO_STATUS_INVALID_DEVICE;
    }

    EnterCriticalSection(&MasterLock);
    if (DevInitialized == 0)
    {
        LeaveCriticalSection(&MasterLock);
        return QDAIO_STATUS_DEVICE_NOT_INIT;
    }

    // locate a free slot, start with index 1 to avoid conflict with QDAIO_STATUS_SUCCESS
    for (i = 1; i < QDA_MAX_DEV; i++)
    {
        if (AioDevice[i].DevState == QDAIO_DEV_STATE_CLOSE)
        {
            appHdl = i;
            pIoDev = (PQDAIODEV) & (AioDevice[i]);
            pDevExt = (PQDAIODEV_EXTENSION)pIoDev->DevExtension;
            break;
        }
    }
    LeaveCriticalSection(&MasterLock);

    if ((appHdl >= QDA_MAX_DEV) || (appHdl < 0))
    {
        return QDAIO_STATUS_BAD_HANDLE;
    }
    if (pIoDev == NULL || pDevExt == NULL)
    {
        return QDAIO_STATUS_NO_DEVICE;
    }

    // initialize extension
    ResetDevExtension(pIoDev);

    hDevice = QcDevice::OpenDevice(DeviceName, Baudrate, isLegacyTimeoutConfig);

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        QCD_Printf("QDAIO::OpenDevice: Error 0x%x <%s>\n", GetLastError(), (PCHAR)DeviceName);
        return QDAIO_STATUS_HANDLE_FAILURE;
    }
    else
    {
        pIoDev->DevHandle = hDevice;
        ConfigureCommChannel(QC_DEV_TYPE_PORTS, hDevice);
        QCD_Printf("QDAIO::OpenDevice: Handle 0x%x/0x%x\n", appHdl, hDevice);

        pIoDev->DevState = QDAIO_DEV_STATE_OPEN;
        StartIoThread(pIoDev);
        StartCompletionThread(pIoDev);
    }

    return appHdl;
}  // openDevice

QCDEVLIB_API LONG QDAIO::CloseDevice(LONG AppHandle)
{
    PQDAIODEV pIoDev;

    if ((AppHandle >= QDA_MAX_DEV) || (AppHandle < 0))
    {
        QCD_Printf("QDAIO::CloseDevice: Invalid appHdl %d\n", AppHandle);
        return QDAIO_STATUS_BAD_HANDLE;
    }

    pIoDev = &(AioDevice[AppHandle]);

    if (pIoDev->DevState != QDAIO_DEV_STATE_OPEN)
    {
        QCD_Printf("QDAIO::CloseDevice: dev handle 0x%x not open\n", AppHandle);
        return QDAIO_STATUS_NO_DEVICE;
    }

    QCD_Printf("QDAIO::CloseDevice: handle 0x%x/0x%x\n", AppHandle, pIoDev->DevHandle);

    pIoDev->DevState = QDAIO_DEV_STATE_CLOSE;

    CloseHandle(pIoDev->DevHandle);

    CancelIoThread(pIoDev);
    CancelCompletionThread(pIoDev);

    ResetDevExtension(pIoDev);

    return QDAIO_STATUS_SUCCESS;
}  // CloseDevice

QCDEVLIB_API LONG QDAIO::Send(LONG AppHandle, PCHAR Buffer, ULONG Length, PAIO_CONTEXT Context)
{
    PAIO_ITEM  ioItem;
    BOOL       bResult = FALSE;
    DWORD      numBytesSent = 0;
    PQDAIODEV  pIoDev;
    PQDAIODEV_EXTENSION pDevExt;

    if ((AppHandle >= QDA_MAX_DEV) || (AppHandle < 0))
    {
        QCD_Printf("QDAIO::Send: Invalid appHdl %d\n", AppHandle);
        return QDAIO_STATUS_BAD_HANDLE;
    }

    pIoDev = &(AioDevice[AppHandle]);

    if (pIoDev->DevExtension == NULL)
    {
        QCD_Printf("QDAIO::Send: non-exist appHdl %d\n", AppHandle);
        return QDAIO_STATUS_NO_DEVICE;
    }
    pDevExt = (PQDAIODEV_EXTENSION)pIoDev->DevExtension;

    if (pIoDev->DevState != QDAIO_DEV_STATE_OPEN)
    {
        QCD_Printf("QDAIO::Send: dev not open: appHdl %d\n", AppHandle);
        return QDAIO_STATUS_FAILURE;
    }

    ioItem = GetIoItem(pIoDev, QIO_TX);
    if (ioItem == NULL)
    {
        QCD_Printf("QDAIO::Send: out of memory for appHdl 0x%x\n", AppHandle);
        return QDAIO_STATUS_NO_MEMORY;
    }

    // en-queue request
    ioItem->Buf = Buffer;
    ioItem->IoSize = Length;;
    ioItem->UserContext = (PVOID)Context;
    ioItem->IoCallback = Context->Callback;
    if (ioItem->OverlappedContext.hEvent == NULL)
    {
        // TODO: do we need to call CB?
        QCD_Printf("QDAIO::Send: event error %u\n", GetLastError());
        RecycleIoItem(pIoDev, ioItem, QIO_TX);
        return QDAIO_STATUS_FAILURE;
    }

    bResult = ::WriteFile
    (
        pIoDev->DevHandle,
        Buffer,
        Length,
        &numBytesSent,
        &(ioItem->OverlappedContext)
    );

    EnterCriticalSection(&(pDevExt->IoLock[QIO_TX]));
    InsertTailList(&(pDevExt->DispatchQueue[QIO_TX]), &(ioItem->List));
    SetEvent(pDevExt->DispatchEvt[QIO_TX]);
    LeaveCriticalSection(&(pDevExt->IoLock[QIO_TX]));
    InterlockedIncrement(&(pDevExt->OutstandingIo[QIO_TX]));

    // QCD_Printf("<--QDAIO::Send[%d] Ctx 0x%p OV 0x%p bResult %d \n", AppHandle, Context, &(ioItem->OverlappedContext), bResult);  // LOG

    return QDAIO_STATUS_PENDING;

}  // Send

QCDEVLIB_API LONG QDAIO::Read(LONG AppHandle, PCHAR Buffer, ULONG Length, PAIO_CONTEXT Context)
{
    PAIO_ITEM  ioItem;
    BOOL       bResult = FALSE;
    DWORD      numBytesRead = 0;
    PQDAIODEV  pIoDev;
    PQDAIODEV_EXTENSION pDevExt;

    if ((AppHandle >= QDA_MAX_DEV) || (AppHandle < 0))
    {
        QCD_Printf("QDAIO::Read: Invalid appHdl %d\n", AppHandle);
        return QDAIO_STATUS_BAD_HANDLE;
    }

    pIoDev = &(AioDevice[AppHandle]);

    if (pIoDev->DevExtension == NULL)
    {
        QCD_Printf("QDAIO::Read: non-exist appHdl %d\n", AppHandle);
        return QDAIO_STATUS_NO_DEVICE;
    }
    pDevExt = (PQDAIODEV_EXTENSION)pIoDev->DevExtension;

    if (pIoDev->DevState != QDAIO_DEV_STATE_OPEN)
    {
        QCD_Printf("QDAIO::Read: dev not open: appHdl %d\n", AppHandle);
        return QDAIO_STATUS_FAILURE;
    }

    ioItem = GetIoItem(pIoDev, QIO_RX);
    if (ioItem == NULL)
    {
        QCD_Printf("QDAIO::Read: out of memory for appHdl %d\n", AppHandle);
        return QDAIO_STATUS_NO_MEMORY;
    }

    // en-queue request
    ioItem->Buf = Buffer;
    ioItem->IoSize = Length;
    ioItem->UserContext = (PVOID)Context;
    ioItem->IoCallback = Context->Callback;
    if (ioItem->OverlappedContext.hEvent == NULL)
    {
        QCD_Printf("QDAIO::Read: event error %u\n", GetLastError());
        RecycleIoItem(pIoDev, ioItem, QIO_RX);
        return QDAIO_STATUS_FAILURE;
    }

    bResult = ::ReadFile
    (
        pIoDev->DevHandle,
        Buffer,
        Length,
        &numBytesRead,
        &(ioItem->OverlappedContext)
    );

    EnterCriticalSection(&(pDevExt->IoLock[QIO_RX]));
    InsertTailList(&(pDevExt->DispatchQueue[QIO_RX]), &(ioItem->List));
    SetEvent(pDevExt->DispatchEvt[QIO_RX]);
    LeaveCriticalSection(&(pDevExt->IoLock[QIO_RX]));
    InterlockedIncrement(&(pDevExt->OutstandingIo[QIO_RX]));

    // QCD_Printf("<--QDAIO::Read[%d--%d] Len %d Context 0x%p/0x%p OV 0x%p, bResult %d\n",
    //             AppHandle, ioItem->Index, Length, Context, ioItem, &(ioItem->OverlappedContext), bResult); // LOG
    return QDAIO_STATUS_PENDING;
}  // Read

// cancell all RX/TX if Context == NULL
QCDEVLIB_API LONG QDAIO::Cancel(LONG AppHandle, PAIO_CONTEXT Context)
{
    PQDAIODEV   pIoDev;

    if ((AppHandle >= QDA_MAX_DEV) || (AppHandle < 0))
    {
        QCD_Printf("QDAIO::Cancel: Invalid appHdl 0x%x\n", AppHandle);
        return QDAIO_STATUS_BAD_HANDLE;
    }

    pIoDev = &(AioDevice[AppHandle]);

    if (pIoDev->DevExtension == NULL)
    {
        QCD_Printf("QDAIO::Cancel: non-exist appHdl 0x%x\n", AppHandle);
        return QDAIO_STATUS_NO_DEVICE;
    }

    if (pIoDev->DevState != QDAIO_DEV_STATE_OPEN)
    {
        QCD_Printf("QDAIO::Cancel: dev not open, appHdl 0x%x\n", AppHandle);
        return QDAIO_STATUS_FAILURE;
    }

    QCD_Printf("QDAIO::Cancel[%d]: 0x%p\n", AppHandle, Context);

    if (FindAndCancel(pIoDev, QIO_RX, Context) == 0)
    {
        FindAndCancel(pIoDev, QIO_TX, Context);
    }

    return QDAIO_STATUS_SUCCESS;  // success from CancelIoEx
}  // Cancel

QCDEVLIB_API BOOL QDAIO::SetSessionTotal(LONG AppHandle, LONGLONG SessionTotal)
{
    PQDAIODEV   pIoDev;
    DWORD bytesReturned = 0;

    if ((AppHandle >= QDA_MAX_DEV) || (AppHandle < 0))
    {
        QCD_Printf("QDAIO::SetSessionTotal: Invalid appHdl 0x%x\n", AppHandle);
        return FALSE;
    }

    pIoDev = &(AioDevice[AppHandle]);

    if (pIoDev->DevExtension == NULL)
    {
        QCD_Printf("QDAIO::SetSessionTotal: non-exist appHdl 0x%x\n", AppHandle);
        return FALSE;
    }

    if (pIoDev->DevState != QDAIO_DEV_STATE_OPEN)
    {
        QCD_Printf("QDAIO::SetSessionTotal: dev not open, appHdl 0x%x\n", AppHandle);
        return FALSE;
    }

    QCD_Printf("QDAIO::SetSessionTotal[%d]\n", AppHandle);

    if (DeviceIoControl(
        pIoDev->DevHandle,
        IOCTL_QCUSB_SET_SESSION_TOTAL,
        (LPVOID)&SessionTotal,
        (DWORD)sizeof(LONGLONG),
        (LPVOID)NULL,
        (DWORD)0,
        &bytesReturned,
        NULL
        ) == FALSE)
    {
        QCD_Printf("QDAIO::SetSessionTotal: failure 0x%x\n", GetLastError());
        return FALSE;
    }

    QCD_Printf("QDAIO::SetSessionTotal: %I64d bytes of session total (%u bytes)\n", SessionTotal, bytesReturned);

    return TRUE;
}  // SetSessionTotal
