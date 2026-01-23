/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P R X . C

GENERAL DESCRIPTION
    This module contains the code specific to receiving packets
    Most of the work of receiving packets is contained in the
    MPWork and MPUsb modules.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "MPMain.h"
#include "MPRX.h"
#include "MPUsb.h"
#include "MPWork.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPRX.tmh"

#endif   // EVENT_TRACING

void MPRX_MiniportReturnPacket(NDIS_HANDLE  MiniportAdapterContext, PNDIS_PACKET Packet)
{

    PMP_ADAPTER     pAdapter = (PMP_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS     status;

    PLIST_ENTRY      pList;
    PNDIS_PACKET    pNdisPacket;

    UINT            dwTotalBufferCount;
    PNDIS_BUFFER    pCurrentNdisBuffer;
    UINT            dwPacketLength;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MPRX_MiniportReturnPacket 0x%p\n", pAdapter->PortName, Packet)
    );

    // Take control of the Receive lists
    NdisAcquireSpinLock(&pAdapter->RxLock);

    pList = (PLIST_ENTRY)&Packet->MiniportReserved[0];

    // Move the packet to the RxFree list list
    InsertTailList(&pAdapter->RxFreeList, pList);
    InterlockedDecrement(&(pAdapter->nBusyRx));

    NdisReleaseSpinLock(&pAdapter->RxLock);

    pNdisPacket = ListItemToPacket(pList);
    NdisQueryPacket(pNdisPacket,
                    NULL,
                    &dwTotalBufferCount,
                    &pCurrentNdisBuffer,
                    &dwPacketLength);

    NdisAdjustBufferLength(pCurrentNdisBuffer, MAX_RX_BUFFER_SIZE);
    NdisRecalculatePacketCounts(pNdisPacket);

    // Kick the worker thread to wake-up and put any free items back down to the 
    // QC USB
    // MPWork_ScheduleWorkItem( pAdapter );
    MPWork_PostDataReads(pAdapter);  // blocking/direct post (to avoid scheduling delay)

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MPRX_MiniportReturnPacket\n", pAdapter->PortName)
    );
    return;
}  // MPRX_MiniportReturnPacket

#ifdef NDIS60_MINIPORT

VOID MPRX_MiniportReturnNetBufferLists
(
    IN NDIS_HANDLE      MiniportAdapterContext,
    IN PNET_BUFFER_LIST NetBufferLists,
    IN ULONG            ReturnFlags
)
{
    PMP_ADAPTER      pAdapter = (PMP_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST netBufList;
    PNET_BUFFER_LIST nextNetBufList;
    PNET_BUFFER      netBuffer;
    PMPUSB_RX_NBL    pRxNBL;
    int              cnt = 0;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_FORCE,
        ("<%s> -->MPRX_MiniportReturnNetBufferLists 0x%p (N%d F%d U%d)\n", pAdapter->PortName,
        NetBufferLists,
        pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb)
    );

    DbgPrint("<%s> MPRX_MiniportReturnNetBufferLists 0x%p (N%d F%d U%d)\n", pAdapter->PortName,
             NetBufferLists,
             pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb);

    if (pAdapter->nRxHeldByNdis > (pAdapter->MaxDataReceives - 30))
    {
        DbgPrint("<%s> MPRX_MiniportReturnNetBufferLists (NDIS BUFFERS USAGE at 75 percent %d)\n", pAdapter->PortName, pAdapter->nRxHeldByNdis);
    }

    if (pAdapter->nRxHeldByNdis > (pAdapter->MaxDataReceives - 2))
    {
        DbgPrint("<%s> MPRX_MiniportReturnNetBufferLists (NDIS BUFFERS USAGE at almost 100 percent %d)\n", pAdapter->PortName, pAdapter->nRxHeldByNdis);
    }

    // TODO: 1) add more sanity check; 2) to utilize ReturnFlags

    NdisAcquireSpinLock(&pAdapter->RxLock);

    for (netBufList = NetBufferLists; netBufList != NULL; netBufList = nextNetBufList)
    {
        nextNetBufList = NET_BUFFER_LIST_NEXT_NBL(netBufList);
        pRxNBL = (PMPUSB_RX_NBL)(netBufList->MiniportReserved[0]);

        // sanity checking
        if (pRxNBL->RxNBL != netBufList)
        {
            // error
            QCNET_DbgPrint
            (
                MP_DBG_MASK_READ,
                MP_DBG_LEVEL_CRITICAL,
                ("<%s> MPRX_MiniportReturnNetBufferLists: wrong NBL 0x%p/0x%p\n", pAdapter->PortName,
                nextNetBufList, pRxNBL->RxNBL)
            );
            NdisReleaseSpinLock(&pAdapter->RxLock);

#ifdef DBG
            DbgBreakPoint();
#else
            return;
#endif // DBG
        }

        InsertTailList(&pAdapter->RxFreeList, &(pRxNBL->List));
        InterlockedDecrement(&(pAdapter->nBusyRx));
        InterlockedDecrement(&(pAdapter->nRxHeldByNdis));
        InterlockedIncrement(&(pAdapter->nRxFreeInMP));
        ++cnt;
    }

    NdisReleaseSpinLock(&pAdapter->RxLock);

    // MPWork_ScheduleWorkItem(pAdapter); // non-blocking post
    MPWork_PostDataReads(pAdapter);    // blocking/direct post (to avoid scheduling delay)

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--MPRX_MiniportReturnNetBufferLists: %d (N%d F%d U%d)\n", pAdapter->PortName, cnt,
        pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb)
    );
}  // MPRX_MiniportReturnNetBufferLists

// This function is for debugging only
VOID MPRCV_QnblInfo
(
    PMP_ADAPTER   pAdapter,
    PMPUSB_RX_NBL RxNBL,
    int           Index,
    PCHAR         Msg
)
{
    PNET_BUFFER_LIST nbl;
    PNET_BUFFER      nb;
    PMDL             mdl;
    PVOID            pSrc;
    ULONG            mdlLen = 0;
    int              i;

    nbl = RxNBL->RxNBL;
    nb = NET_BUFFER_LIST_FIRST_NB(nbl);
    mdl = NET_BUFFER_FIRST_MDL(nb);

    DbgPrint("<qnet_test> %s QNBL 0x%p[%d] NBL 0x%p NB 0x%p\n", Msg, RxNBL, Index, nbl, nb);
    DbgPrint("<qnet_test> NextNBL 0x%p NextNB 0x%p\n", NET_BUFFER_LIST_NEXT_NBL(nbl), NET_BUFFER_NEXT_NB(nb));

    DbgPrint("<qnet_test> QNB's MDL 0x%p/0x%p\n", RxNBL->RxMdl, NET_BUFFER_FIRST_MDL(nb));
    DbgPrint("<qnet_test> QNB's DataLen/Offset %d/%d\n", NET_BUFFER_DATA_LENGTH(nb), NET_BUFFER_DATA_OFFSET(nb));
    DbgPrint("<qnet_test> QNB's currMDL 0x%p (Offset %d)\n", NET_BUFFER_CURRENT_MDL(nb), NET_BUFFER_CURRENT_MDL_OFFSET(nb));

    NdisQueryMdl(mdl, &pSrc, &mdlLen, NormalPagePriority);
    DbgPrint("<qnet_test> QNB's MDL addr 0x%p/0x%p/0x%p/0x%p (%dB)\n", pSrc, RxNBL->BVA, RxNBL->BvaPtr, &(RxNBL->BvaPtr), mdlLen);

    for (i = 0; i < MaxNetBufferListInfo; i++)
    {
        if (nbl->NetBufferListInfo[i] != NULL)
        {
            DbgPrint("<qnet_test> QNBL's NetBufferListInfo[%d]=0x%p\n", i, nbl->NetBufferListInfo[i]);
        }
    }

    DbgPrint("<qnet_test> QNBL's Context=0x%p\n", nbl->Context);

}  // MPRCV_QnblInfo

NTSTATUS MPRX_StartRxThread(PMP_ADAPTER pAdapter, INT Index)
{
    NTSTATUS          ntStatus;
    OBJECT_ATTRIBUTES objAttr;

    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPRX_StartRxThread - wrong IRQL::%d\n", pAdapter->PortName, KeGetCurrentIrql())
        );
        return STATUS_UNSUCCESSFUL;
    }

    if ((pAdapter->pRxThread[Index] != NULL) || (pAdapter->hRxThreadHandle[Index] != NULL))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPRX_StartRxThread: RxThread up/resumed [%d]\n", pAdapter->PortName, Index)
        );
        return STATUS_SUCCESS;
    }

    KeClearEvent(&pAdapter->RxThreadStartedEvent[Index]);
    InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    ntStatus = PsCreateSystemThread
    (
        OUT & pAdapter->hRxThreadHandle[Index],
        IN THREAD_ALL_ACCESS,
        IN & objAttr,         // POBJECT_ATTRIBUTES
        IN NULL,             // HANDLE  ProcessHandle
        OUT NULL,            // PCLIENT_ID  ClientId
        IN(PKSTART_ROUTINE)MPRX_RxThread,
        IN(PVOID) & (pAdapter->RxThreadContext[Index])
    );
    if ((!NT_SUCCESS(ntStatus)) || (pAdapter->hRxThreadHandle[Index] == NULL))
    {
        pAdapter->hRxThreadHandle[Index] = NULL;
        pAdapter->pRxThread[Index] = NULL;
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_CRITICAL,
            ("<%s> RxTh failure [%d]\n", pAdapter->PortName, Index)
        );
        return ntStatus;
    }
    ntStatus = KeWaitForSingleObject
    (
        &pAdapter->RxThreadStartedEvent[Index],
        Executive,
        KernelMode,
        FALSE,
        NULL
    );

    ntStatus = ObReferenceObjectByHandle
    (
        pAdapter->hRxThreadHandle[Index],
        THREAD_ALL_ACCESS,
        NULL,
        KernelMode,
        (PVOID *)&pAdapter->pRxThread[Index],
        NULL
    );
    if (!NT_SUCCESS(ntStatus))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_CRITICAL,
            ("<%s> RxTh: ObReferenceObjectByHandle failed 0x%x\n", pAdapter->PortName, ntStatus)
        );
        pAdapter->pRxThread[Index] = NULL;
    }
    else
    {
        if (STATUS_SUCCESS != (ntStatus = ZwClose(pAdapter->hRxThreadHandle[Index])))
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_READ,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> RxTh ZwClose failed - 0x%x\n", pAdapter->PortName, ntStatus)
            );
        }
        else
        {
            pAdapter->hRxThreadHandle[Index] = NULL;
        }
        ntStatus = STATUS_SUCCESS;
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> RxTh handle[%d]=0x%p thOb=0x%p\n", pAdapter->PortName, Index,
        pAdapter->hRxThreadHandle[Index], pAdapter->pRxThread[Index])
    );

    return ntStatus;

}  // MPRX_StartRxThread

NTSTATUS MPRX_CancelRxThread(PMP_ADAPTER pAdapter, INT Index)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    LARGE_INTEGER delayValue;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> -->RxTh Cxl [%d]\n", pAdapter->PortName, Index)
    );

    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_CRITICAL,
            ("<%s> RxTh Cxl: wrong IRQL\n", pAdapter->PortName)
        );
        return STATUS_UNSUCCESSFUL;
    }

    if (InterlockedIncrement(&pAdapter->RxThreadCancelStarted) > 1)
    {
        while ((pAdapter->hRxThreadHandle[Index] != NULL) || (pAdapter->pRxThread[Index] != NULL))
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_READ,
                MP_DBG_LEVEL_ERROR,
                ("<%s> RxTh cxl in pro [%d]\n", pAdapter->PortName, Index)
            );
            MPMAIN_Wait(-(3 * 1000 * 1000));
        }
        InterlockedDecrement(&pAdapter->RxThreadCancelStarted);
        return STATUS_SUCCESS;
    }

    if ((pAdapter->hRxThreadHandle[Index] != NULL) || (pAdapter->pRxThread[Index] != NULL))
    {
        KeClearEvent(&pAdapter->RxThreadClosedEvent[Index]);
        KeSetEvent
        (
            &pAdapter->RxThreadCancelEvent[Index],
            IO_NO_INCREMENT,
            FALSE
        );

        if (pAdapter->pRxThread[Index] != NULL)
        {
            ntStatus = KeWaitForSingleObject
            (
                pAdapter->pRxThread[Index],
                Executive,
                KernelMode,
                FALSE,
                NULL
            );
            ObDereferenceObject(pAdapter->pRxThread[Index]);
            KeClearEvent(&pAdapter->RxThreadClosedEvent[Index]);
            ZwClose(pAdapter->hRxThreadHandle[Index]);
            pAdapter->pRxThread[Index] = NULL;
        }
        else  // best effort
        {
            ntStatus = KeWaitForSingleObject
            (
                &pAdapter->RxThreadClosedEvent[Index],
                Executive,
                KernelMode,
                FALSE,
                NULL
            );
            KeClearEvent(&pAdapter->RxThreadClosedEvent[Index]);
            ZwClose(pAdapter->hRxThreadHandle[Index]);
        }
        pAdapter->hRxThreadHandle[Index] = NULL;
    }

    InterlockedDecrement(&pAdapter->RxThreadCancelStarted);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> <--RxTh Cxl [%d]\n", pAdapter->PortName, Index)
    );

    return ntStatus;
}  // MPRX_CancelRxThread

VOID MPRX_RxThread(PVOID Context)
{
    PRX_THREAD_CONTEXT pContext = (PRX_THREAD_CONTEXT)Context;
    PMP_ADAPTER  pAdapter;
    NTSTATUS     ntStatus;
    BOOLEAN      bKeepRunning = TRUE;
    BOOLEAN      bThreadStarted = FALSE;
    PKWAIT_BLOCK pwbArray = NULL;
    INT          idx;

    pAdapter = (PMP_ADAPTER)pContext->AdapterContext;
    idx = pContext->Index;

    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_CRITICAL,
            ("<%s> MPRX_RxThread: wrong IRQL\n", pAdapter->PortName)
        );
        ZwClose(pAdapter->hRxThreadHandle[idx]);
        pAdapter->hRxThreadHandle[idx] = NULL;
        KeSetEvent(&pAdapter->RxThreadStartedEvent[idx], IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
    }

    // allocate a wait block array for the multiple wait
    pwbArray = ExAllocatePool
    (
        NonPagedPool,
        (RX_THREAD_EVENT_COUNT + 1) * sizeof(KWAIT_BLOCK)
    );
    if (pwbArray == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_CRITICAL,
            ("<%s> MPRX_RxThread: NO_MEM [%d]\n", pAdapter->PortName, idx)
        );
        ZwClose(pAdapter->hRxThreadHandle[idx]);
        pAdapter->hRxThreadHandle[idx] = NULL;
        KeSetEvent(&pAdapter->RxThreadStartedEvent[idx], IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_NO_MEMORY);
    }

    KeSetPriorityThread(KeGetCurrentThread(), (HIGH_PRIORITY - 2));

    while (bKeepRunning == TRUE)
    {
        if (bThreadStarted == FALSE)
        {
            bThreadStarted = TRUE;
            KeSetEvent(&pAdapter->RxThreadStartedEvent[idx], IO_NO_INCREMENT, FALSE);
        }

        ntStatus = KeWaitForMultipleObjects
        (
            RX_THREAD_EVENT_COUNT,
            (VOID **)&pAdapter->RxThreadEvent[idx],
            WaitAny,
            Executive,
            KernelMode,
            FALSE,             // non-alertable
            NULL,
            pwbArray
        );
        switch (ntStatus)
        {
            // RX_PROCESS_EVENT first to ensure queue is empty before exiting thread
            case RX_PROCESS_EVENT_INDEX:
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_READ,
                    MP_DBG_LEVEL_DETAIL,
                    ("<%s> RX_PROCESS [%d]\n", pAdapter->PortName, idx)
                );
                KeClearEvent(&pAdapter->RxThreadProcessEvent[idx]);
                MPRX_ProcessRxChain(pAdapter, idx);
                break;
            }

            case RX_CANCEL_EVENT_INDEX:
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_READ,
                    MP_DBG_LEVEL_DETAIL,
                    ("<%s> RX_CANCEL [%d]\n", pAdapter->PortName, idx)
                );

                KeClearEvent(&pAdapter->RxThreadCancelEvent[idx]);
                bKeepRunning = FALSE;
                break;
            }

            default:
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_READ,
                    MP_DBG_LEVEL_ERROR,
                    ("<%s> MPRX_RxThread: unexpected evt-0x%x [%d]\n", pAdapter->PortName, ntStatus, idx)
                );
                break;
            }
        }
    }  // while

    if (pwbArray != NULL)
    {
        ExFreePool(pwbArray);
    }

    MPRX_RxChainCleanup(pAdapter, idx);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_FORCE,
        ("<%s> MPRX_RxThread: OUT (0x%x) [%d]\n", pAdapter->PortName, ntStatus, idx)
    );

    KeSetEvent(&pAdapter->RxThreadClosedEvent[idx], IO_NO_INCREMENT, FALSE);

    PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}  // MPRX_RxThread

VOID MPRX_RxChainCleanup(PMP_ADAPTER pAdapter, INT Index)
{
    PMPUSB_RX_NBL pRxNBL;
    PLIST_ENTRY   pList;

    NdisAcquireSpinLock(&pAdapter->RxIndLock[Index]);

    while (!IsListEmpty(&pAdapter->RxNblChain[Index]))
    {
        pList = RemoveHeadList(&pAdapter->RxNblChain[Index]);
        pRxNBL = CONTAINING_RECORD(pList, MPUSB_RX_NBL, List);
        InterlockedDecrement(&(pAdapter->nRxInNblChain));

        NdisReleaseSpinLock(&pAdapter->RxIndLock[Index]);

        NdisAcquireSpinLock(&pAdapter->RxLock);
        InsertTailList(&pAdapter->RxFreeList, &pRxNBL->List);
        NdisReleaseSpinLock(&pAdapter->RxLock);

        InterlockedDecrement(&(pAdapter->nBusyRx));
        InterlockedIncrement(&(pAdapter->nRxFreeInMP));

        NdisAcquireSpinLock(&pAdapter->RxIndLock[Index]);
    }

    NdisReleaseSpinLock(&pAdapter->RxIndLock[Index]);

}  // MPRX_RxChainCleanup

VOID MPRX_ProcessRxChain(PMP_ADAPTER pAdapter, INT Index)
{
    PMPUSB_RX_NBL pRxNBL;
    PLIST_ENTRY   pList;
    PNET_BUFFER_LIST nblHead = NULL;
    PNET_BUFFER_LIST nblTail = NULL;
    ULONG numOfNBLs = 0;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> -->MPRX_ProcessRxChain [%d]\n", pAdapter->PortName, Index)
    );

    NdisAcquireSpinLock(&pAdapter->RxIndLock[Index]);

    while (!IsListEmpty(&pAdapter->RxNblChain[Index]))
    {
        pList = RemoveHeadList(&pAdapter->RxNblChain[Index]);
        pRxNBL = CONTAINING_RECORD(pList, MPUSB_RX_NBL, List);
        ++numOfNBLs;
        InterlockedDecrement(&(pAdapter->nRxInNblChain));

        if (nblHead == NULL)
        {
            nblHead = nblTail = pRxNBL->RxNBL;
        }
        else
        {
            NET_BUFFER_LIST_NEXT_NBL(nblTail) = pRxNBL->RxNBL;
            nblTail = pRxNBL->RxNBL;
        }

        InterlockedIncrement(&(pAdapter->nRxHeldByNdis));

        if (numOfNBLs >= pAdapter->RxIndClusterSize)
        {
            break;
        }
    }

    // to continue after indicating to protocol layer
    if (!IsListEmpty(&pAdapter->RxNblChain[Index]))
    {
        KeSetEvent(&pAdapter->RxThreadProcessEvent[Index], IO_NO_INCREMENT, FALSE);
    }

    NdisReleaseSpinLock(&pAdapter->RxIndLock[Index]);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_FORCE,
        ("<%s> MPRX_ProcessRxChain[%d]: ind %lu NBLs, pending %ld\n", pAdapter->PortName, Index, numOfNBLs, pAdapter->nRxInNblChain)
    );

    if (nblHead != NULL)
    {
        NET_BUFFER_LIST_NEXT_NBL(nblTail) = NULL;

        NdisAcquireSpinLock(&pAdapter->RxProtocolLock[Index]);
        NdisMIndicateReceiveNetBufferLists
        (
            pAdapter->AdapterHandle,
            nblHead,
            NDIS_DEFAULT_PORT_NUMBER,
            numOfNBLs,
            NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL
        );
        NdisReleaseSpinLock(&pAdapter->RxProtocolLock[Index]);
    }
    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_FORCE,
        ("<%s> MPRX_ProcessRxChain: ind done [%d]\n", pAdapter->PortName, Index)
    );

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--MPRX_ProcessRxChain[%d] - %u\n", pAdapter->PortName, Index, numOfNBLs)
    );
}  // MPRX_ProcessRxChain

#endif // NDIS60_MINIPORT
