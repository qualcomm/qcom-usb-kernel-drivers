/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             M P U S B . C

GENERAL DESCRIPTION
    This module contains miniport functions that interact with the QC USB layer
    Everything above this module is "generic" to the miniport driver and all
    data passed to this module is in "native formats" (packets, Queries, Sets).
    This module then is responsible for creating IRPs, reformating data, making
    calls to QCIOCall driver and will also contain the callback routines.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "MPMain.h"
#include "MPWork.h"
#include "USBIOC.h"
#ifdef MP_QCQOS_ENABLED
#include "MPQOS.h"
#endif // MP_QCQOS_ENABLED
#include "MPOID.h"
#include "MPUSB.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPUsb.tmh"

#endif  // EVENT_TRACING

#undef USE_MDL 
//#define USE_MDL 

#ifndef USE_MDL

ULONG MPUSB_CopyNdisPacketToBuffer
(
    IN PNDIS_PACKET pPacket,
    IN PUCHAR pBuffer,
    IN ULONG dwLeadingBytesToSkip
)
{
    PNDIS_BUFFER pCurrentNdisBuffer;
    PUCHAR pCurrentBuffer;
    ULONG dwCurrentBufferLength;
    ULONG dwTotalNdisBuffer;
    ULONG dwTotalCopied = 0x00;
    ULONG dwTotalPacketLength;
    ULONG dwSkipBytes;

    dwSkipBytes = 0;

    ///
    //  Get the first buffer.
    //

    NdisQueryPacket
    (
        pPacket,
        NULL,
        &dwTotalNdisBuffer,
        &pCurrentNdisBuffer,
        &dwTotalPacketLength
    );

    while (dwTotalNdisBuffer-- && (pCurrentNdisBuffer != NULL))
    {
        //
        //  Get the buffer...
        //
        NdisQueryBufferSafe(
            pCurrentNdisBuffer,
            &pCurrentBuffer,
            &dwCurrentBufferLength,
            NormalPagePriority);

        if (pCurrentBuffer)
        {
            if (dwLeadingBytesToSkip <= dwCurrentBufferLength)
            {
                // There are enough bytes in this buffer to satisfy the the skip spec
                dwSkipBytes = dwLeadingBytesToSkip;
                dwLeadingBytesToSkip = 0;
            }
            else
            {
                // There are not enough byte to skip, skip all these and decriment the skip count
                dwSkipBytes = dwCurrentBufferLength;
                dwLeadingBytesToSkip -= dwCurrentBufferLength;
            }
            NdisMoveMemory((pBuffer + dwTotalCopied), (pCurrentBuffer + dwSkipBytes), (dwCurrentBufferLength - dwSkipBytes));
            dwTotalCopied += (dwCurrentBufferLength - dwSkipBytes);
        }

        NdisGetNextBuffer(pCurrentNdisBuffer, &pCurrentNdisBuffer);
    }

    // return ( dwTotalCopied == (dwTotalPacketLength - dwLeadingBytesToSkip) );
    return dwTotalCopied;
}
#endif // USE_MDL


NTSTATUS MP_TxIRPComplete(PDEVICE_OBJECT pDO, PIRP pIrp, PVOID pContext)
{
    PMP_ADAPTER  pAdapter = (PMP_ADAPTER)pContext;
    NDIS_STATUS  status;
    PLIST_ENTRY  pList;
    PNDIS_PACKET pNdisPacket;
#ifdef USE_MDL
    PMDL         pCurMdl, pNextMdl;
#else
    PIO_STACK_LOCATION nextstack;
#endif

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_TxIRPComplete, (IoStatus: 0x%x)\n", pAdapter->PortName,
        pIrp->IoStatus.Status)
    );

    NdisAcquireSpinLock(&pAdapter->TxLock);
    // Get control of the Tx variables
    if (!IsListEmpty(&pAdapter->TxBusyList))
    {

        // Move the packet from the busy list onto the Tx completed list
        pList = RemoveHeadList(&pAdapter->TxBusyList);
        pNdisPacket = ListItemToPacket(pList);
        if (NT_SUCCESS(pIrp->IoStatus.Status))
        {
            NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_SUCCESS);
        }
        else
        {
            NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
        }
        InsertTailList(&pAdapter->TxCompleteList, pList);
        NdisReleaseSpinLock(&pAdapter->TxLock);

        MPWork_ScheduleWorkItem(pAdapter);
    }
    else
    {
        NdisReleaseSpinLock(&pAdapter->TxLock);
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_TxIRPComplete: Empty BusyList ???\n", pAdapter->PortName)
        );
    }

#ifdef USE_MDL
    // Free the Mdls that were created to 
    // RWD ??? It may be sufficient to free the to Mdl here
    pCurMdl = pIrp->MdlAddress;

    if (pAdapter->LLHeaderBytes > 0)
    {
        NdisInterlockedInsertTailList(
            &pAdapter->LHFreeList,
            (PLIST_ENTRY)(CONTAINING_RECORD(pCurMdl->MappedSystemVa, LLHeader, List)),
            &pAdapter->TxLock);
    }

    while (pCurMdl)
    {
        pNextMdl = pCurMdl->Next;
        IoFreeMdl(pCurMdl);
        pCurMdl = pNextMdl;
    }
#else
    // nextstack = IoGetNextIrpStackLocation(pIrp);
    // Free the buffer we copied the packet data to

    ExFreePool(pIrp->AssociatedIrp.SystemBuffer);

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        InterlockedDecrement(&(pAdapter->QosPendingPackets));

#ifdef MPQOS_PKT_CONTROL
        if (pAdapter->QosPendingPackets < pAdapter->MPPendingPackets)
        {
            KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT, FALSE);
        }
#else
        KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT, FALSE);
#endif // MPQOS_PKT_CONTROL
    }
#endif // MP_QCQOS_ENABLED
#endif

    IoFreeIrp(pIrp);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_TxIRPComplete (nBusyTx: %d/%dP)\n",
        pAdapter->PortName, pAdapter->nBusyTx, pAdapter->QosPendingPackets)
    );
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*****************************************************************************
 *****************************************************************************
 *****************************************************************************
 *****************************************************************************/

void MP_USBTxPacket
(
    PMP_ADAPTER  pAdapter,
    ULONG        QosFlowId,
    BOOLEAN      IsQosPacket,
    PLIST_ENTRY  pList
)
{
    PIRP                pIrp;
    PIO_STACK_LOCATION  nextstack;
    NDIS_STATUS         Status;

    PNDIS_BUFFER pCurrentNdisBuffer;
    UINT         dwTotalBufferCount;
    UINT         dwPacketLength;
    PUCHAR       pCurrentBuffer;
    ULONG        dwCurrentBufferLength;
#ifdef USE_MDL
    BOOLEAN     bSecondaryBuffer;
    PMDL        pMdl;
    pLLHeader   pHead;
#endif

    PUCHAR      usbBuffer = NULL;
#ifdef MP_QCQOS_ENABLED
    PMPQOS_HDR  qosHdr;
    PMPQOS_HDR8  qosHdr8;
#endif // MP_QCQOS_ENABLED
    long        sendBytes;
    ULONG       copyOk;
    BOOLEAN     kicker = FALSE;
    NDIS_STATUS ndisSt = NDIS_STATUS_FAILURE;
    PNDIS_PACKET pNdisPacket = ListItemToPacket(pList);
#ifdef QCUSB_MUX_PROTOCOL
    PUCHAR muxBuffer = NULL;
#endif // QCUSB_MUX_PROTOCOL
    PUCHAR pDataPkt;
    PMP_ADAPTER returnAdapter;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_USBTxPacket 0x%p\n", pAdapter->PortName, pList)
    );

    NdisQueryPacket(pNdisPacket,
                    NULL,
                    &dwTotalBufferCount,
                    &pCurrentNdisBuffer,
                    &dwPacketLength);
    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> TNDISP => 0x%p (%dB)\n", pAdapter->PortName, pNdisPacket, dwPacketLength)
    );

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        sendBytes = dwPacketLength;
        if (pAdapter->QosHdrFmt == 0x02)
        {
            sendBytes += sizeof(MPQOS_HDR8);
        }
        else
        {
            sendBytes += sizeof(MPQOS_HDR);
        }
    }
    else
#endif // MP_QCQOS_ENABLED
    {
        sendBytes = dwPacketLength;
    }

#ifdef USE_MDL
    pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_USBTxPacket failed to allocate an IRP\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_WRITE;

    //    pIrp->MdlAddress = pCurrentNdisBuffer;
    bSecondaryBuffer = FALSE;
    if (pAdapter->LLHeaderBytes > 0)
    {
        pHead = (pLLHeader)NdisInterlockedRemoveHeadList(&pAdapter->LHFreeList, &pAdapter->TxLock);
        pMdl = IoAllocateMdl(&pHead->Buffer,
                             pAdapter->LLHeaderBytes,
                             bSecondaryBuffer,
                             FALSE,
                             pIrp
        );

        MmBuildMdlForNonPagedPool(pMdl);
        bSecondaryBuffer = TRUE;
    }

    while (dwTotalBufferCount--)
    {
        NdisQueryBufferSafe(
            pCurrentNdisBuffer,
            &pCurrentBuffer,
            &dwCurrentBufferLength,
            NormalPagePriority);

        pMdl = IoAllocateMdl(pCurrentBuffer,
                             dwCurrentBufferLength,
                             bSecondaryBuffer,
                             FALSE,
                             pIrp
        );
        MmBuildMdlForNonPagedPool(pMdl);
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MP_USBTxPacket BufCnt: %d CurBufLen: %d\n", pAdapter->PortName,
            dwTotalBufferCount, dwCurrentBufferLength)
        );

        NdisGetNextBuffer(pCurrentNdisBuffer, &pCurrentNdisBuffer);
        bSecondaryBuffer = TRUE;
    }
#else

#ifndef QCUSB_MUX_PROTOCOL

    usbBuffer = ExAllocatePoolWithTag
    (
        NonPagedPool,
        sendBytes, // (dwPacketLength + pAdapter->LLHeaderBytes),
        ((ULONG)'usbB')
    );
    if (usbBuffer == NULL)
    {
        Status = NDIS_STATUS_FAILURE;
    }
    else
    {
        Status = NDIS_STATUS_SUCCESS;
    }

#else  // QCUSB_MUX_PROTOCOL

    muxBuffer = ExAllocatePoolWithTag
    (
        NonPagedPool,
        (sendBytes + 64),  // alloc enough bytes for mux header and trailer
        ((ULONG)'usbB')
    );
    usbBuffer = muxBuffer;
    if (muxBuffer == NULL)
    {
        Status = NDIS_STATUS_FAILURE;
    }
    else
    {
        if (pAdapter->MuxEnabled == TRUE)
        {
            usbBuffer = muxBuffer + 16;  // leave enough room for mux header
        }
        Status = NDIS_STATUS_SUCCESS;
    }

#endif // QCUSB_MUX_PROTOCOL

    if (Status != NDIS_STATUS_SUCCESS)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_USBTxPacket failed to allocate buffer\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            pDataPkt = (PUCHAR)(usbBuffer + sizeof(MPQOS_HDR8));
        }
        else
        {
            pDataPkt = (PUCHAR)(usbBuffer + sizeof(MPQOS_HDR));
        }
        copyOk = MPUSB_CopyNdisPacketToBuffer(pNdisPacket, pDataPkt, 0);
    }
    else
#endif // MP_QCQOS_ENABLED
    {
        pDataPkt = (PUCHAR)usbBuffer;
        copyOk = MPUSB_CopyNdisPacketToBuffer(pNdisPacket, pDataPkt, 0);
    }
    if (copyOk == 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_USBTxPacket failed to copy pkt\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }

#ifdef QC_IP_MODE
    if (pAdapter->IPModeEnabled == TRUE)
    {
        if (FALSE == MPUSB_PreparePacket(pAdapter, usbBuffer, (PULONG)&sendBytes))
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> IPO: _PreparePacket: local process %dB\n", pAdapter->PortName, sendBytes)
            );
            kicker = TRUE;
            ndisSt = NDIS_STATUS_SUCCESS;
            goto Tx_Exit;
        }
        else
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_TRACE,
                ("<%s> IPO: _PreparePacket: to USB %dB\n", pAdapter->PortName, sendBytes)
            );
        }
    }
#endif // QC_IP_MODE

    // Prepare IRP
    pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_USBTxPacket failed to alloc IRP\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }
#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            qosHdr8 = (PMPQOS_HDR8)usbBuffer;
            qosHdr8->Version = 1;
            qosHdr8->Reserved = 0;
            if (IsQosPacket == TRUE)
            {
                qosHdr8->FlowId = QosFlowId;
            }
            else
            {
                qosHdr8->FlowId = 0;
            }
            qosHdr8->Rsvd[0] = 0;
            qosHdr8->Rsvd[1] = 0;
        }
        else
        {
            qosHdr = (PMPQOS_HDR)usbBuffer;
            qosHdr->Version = 1;
            qosHdr->Reserved = 0;
            if (IsQosPacket == TRUE)
            {
                qosHdr->FlowId = QosFlowId;
            }
            else
            {
                qosHdr->FlowId = 0;
            }
        }
        InterlockedIncrement(&pAdapter->QosPendingPackets);
    }
#endif // MP_QCQOS_ENABLED

#ifdef QCUSB_MUX_PROTOCOL

    if (pAdapter->MuxEnabled == TRUE)
    {
        ULONG muxLength = 0;

        USBIF_EncodeMuxFrame
        (
            pAdapter->USBDo,
            usbBuffer,
            sendBytes,
            muxBuffer,
            &muxLength,
            TRUE
        );
        usbBuffer = muxBuffer;

        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> TX [MUX]: sending mux frame %d/%dB\n", pAdapter->PortName, muxLength, sendBytes)
        );
        sendBytes = muxLength;
    }

#endif // QCUSB_MUX_PROTOCOL

    if (pAdapter->IPModeEnabled == TRUE)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPUSB: sending UL-IP seq num (0x%02X, 0x%02X)\n", pAdapter->PortName, pDataPkt[4], pDataPkt[5])
        );
        MPMAIN_PrintBytes
        (
            usbBuffer, 64, sendBytes, "IPO: IP->D", pAdapter,
            MP_DBG_MASK_DATA_WT, MP_DBG_LEVEL_VERBOSE
        );
    }
    else if (sendBytes > 26)
    {
        // Ethernet mode
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPUSB: sending UL-IP seq num (0x%02X, 0x%02X)\n", pAdapter->PortName, pDataPkt[18], pDataPkt[19])
        );
    }
    pIrp->MdlAddress = NULL;
    pIrp->AssociatedIrp.SystemBuffer = usbBuffer;
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_WRITE;
    nextstack->Parameters.Write.Length = sendBytes; // dwPacketLength;

#endif

    // Init IRP
    IoSetCompletionRoutine(
        pIrp,
        MP_TxIRPComplete,
        pAdapter,
        TRUE, TRUE, TRUE
    );
    returnAdapter = FindStackDeviceObject(pAdapter);
    if (returnAdapter != NULL)
    {
        Status = QCIoCallDriver(returnAdapter->USBDo, pIrp);
    }
    else
    {
        Status = STATUS_UNSUCCESSFUL;
        pIrp->IoStatus.Status = Status;
        MP_TxIRPComplete(pAdapter->USBDo, pIrp, pAdapter);
    }
Tx_Exit:

#ifdef QCUSB_MUX_PROTOCOL
    if (pAdapter->MuxEnabled == TRUE)
    {
        usbBuffer = muxBuffer;
    }
#endif // QCUSB_MUX_PROTOCOL

    if (kicker == TRUE)
    {
#ifdef QC_IP_MODE
        if (pAdapter->IPModeEnabled == TRUE)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_TRACE,
                ("<%s> IPO: MP_USBTxPacket: complete 0x%p (0x%x)\n", pAdapter->PortName,
                pNdisPacket, ndisSt)
            );
        }
#endif // QC_IP_MODE
        NdisAcquireSpinLock(&pAdapter->TxLock);
        // remove from BusyList
        RemoveEntryList(pList);
        NDIS_SET_PACKET_STATUS(pNdisPacket, ndisSt);
        InsertTailList(&pAdapter->TxCompleteList, pList);
        NdisReleaseSpinLock(&pAdapter->TxLock);
        MPWork_ScheduleWorkItem(pAdapter);
        if (usbBuffer != NULL)
        {
            ExFreePool(usbBuffer);
        }
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_USBTxPacket (nBusyTx: %d/%dP/%dP)\n", pAdapter->PortName,
        pAdapter->nBusyTx, pAdapter->QosPendingPackets, pAdapter->MPPendingPackets)
    );
    return;
}  // MP_USBTxPacket

/*****************************************************************************
 *****************************************************************************
 *****************************************************************************
 *****************************************************************************/

NTSTATUS MP_RxIRPCompletion(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    PMP_ADAPTER         pAdapter = (PMP_ADAPTER)pContext;
    PNDIS_PACKET        pNdisPacket;
    PLIST_ENTRY         pList;
    BOOLEAN             indicate = FALSE;
    PNDIS_BUFFER        pCurrentNdisBuffer;
    ULONG               dwTotalBufferCount, dwPacketLength;
    PNDIS_PACKET_OOB_DATA OOBData;

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QMI_OVER_DATA
    {
        BOOLEAN processed;

        processed = MPUSB_DispatchQMI
        (
            pAdapter,
            pIrp->AssociatedIrp.SystemBuffer,
            pIrp->IoStatus.Information,
            pIrp->IoStatus.Status
        );
        if (processed == TRUE)
        {
            IoFreeIrp(pIrp);
            return STATUS_MORE_PROCESSING_REQUIRED;
        }
    }
#endif // QMI_OVER_DATA
#endif // QCUSB_MUX_PROTOCOL

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_RxIRPCompletionRoutine (IoStatus: 0x%x[%u]) IRQL %d\n",
        pAdapter->PortName, pIrp->IoStatus.Status, pAdapter->GoodReceives,
        KeGetCurrentIrql())
    );

    NdisAcquireSpinLock(&pAdapter->RxLock);

    // Get the packet off the RxPendinging list
    pList = RemoveHeadList(&pAdapter->RxPendingList);
    InterlockedDecrement(&(pAdapter->nRxPendingInUsb));

    pNdisPacket = ListItemToPacket(pList);

    if ((!NT_SUCCESS(pIrp->IoStatus.Status)) ||
        (pAdapter->PacketFilter == 0) ||
        (pAdapter->ToPowerDown == TRUE))  // no filter 
    {
        // This was not a successful receive so just relink this receive to the free list
        InsertTailList(&pAdapter->RxFreeList, pList);

        if (!NT_SUCCESS(pIrp->IoStatus.Status))
        {
            ++(pAdapter->BadReceives);
        }
        else
        {
            ++(pAdapter->DroppedReceives);
        }
        InterlockedDecrement(&(pAdapter->nBusyRx));

        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_RxIRPCompletionRoutine: Rx Packet discarded\n", pAdapter->PortName)
        );
    }
    else
    {
        indicate = TRUE;
        ++(pAdapter->GoodReceives);
    }
    NdisReleaseSpinLock(&pAdapter->RxLock);

    if (indicate == TRUE)
    {
        NdisQueryPacket(pNdisPacket,
                        NULL,
                        &dwTotalBufferCount,
                        &pCurrentNdisBuffer,
                        &dwPacketLength);
        NdisAdjustBufferLength(pCurrentNdisBuffer, pIrp->IoStatus.Information);


        OOBData = NDIS_OOB_DATA_FROM_PACKET(pNdisPacket);
        NdisZeroMemory(OOBData, sizeof(NDIS_PACKET_OOB_DATA));

        OOBData->HeaderSize = ETH_HEADER_SIZE;
        OOBData->SizeMediaSpecificInfo = 0;
        OOBData->MediaSpecificInformation = NULL;
        OOBData->Status = NDIS_STATUS_SUCCESS;
        NdisRecalculatePacketCounts(pNdisPacket);

        if ((pAdapter->DebugMask & MP_DBG_MASK_READ) != 0)
        {
            NdisQueryPacket(pNdisPacket,
                            NULL,
                            &dwTotalBufferCount,
                            &pCurrentNdisBuffer,
                            &dwPacketLength);
            QCNET_DbgPrint
            (
                MP_DBG_MASK_READ,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> MP_RxIRPCompletionRoutine: 0x%p indicated PacketLen: %dB[%u]\n",
                pAdapter->PortName, pIrp, dwPacketLength, pAdapter->GoodReceives)
            );
#ifdef QC_IP_MODE
            if (pIrp->AssociatedIrp.SystemBuffer != NULL)
            {
                if (pAdapter->IPModeEnabled == TRUE)
                {
                    MPMAIN_PrintBytes
                    (
                        pIrp->AssociatedIrp.SystemBuffer,
                        64,
                        pIrp->IoStatus.Information,
                        "IPO: IN-DATA",
                        pAdapter,
                        MP_DBG_MASK_WRITE, MP_DBG_LEVEL_VERBOSE
                    );
                }
            }
#endif // QC_IP_MODE
        }

        NdisMIndicateReceivePacket
        (
            pAdapter->AdapterHandle,
            &pNdisPacket,
            1
        );

#ifdef QCUSB_MUX_PROTOCOL
        if (pAdapter->DLPauseEnabled == TRUE)
        {
            InterlockedIncrement(&(pAdapter->DLPktCount));
            if (pAdapter->DLPktCount >= pAdapter->DLPausePktCount)
            {
                KeSetEvent(&pAdapter->MPThreadDLPauseEvent, IO_NO_INCREMENT, FALSE);
            }
        }
#endif // #ifdef QCUSB_MUX_PROTOCOL        
    }
#ifdef USE_MDL
    // Free the Mdls that were created to 
    // RWD ??? It may be sufficient to free the to Mdl here
    pCurMdl = pIrp->MdlAddress;
    while (pCurMdl)
    {
        pNextMdl = pCurMdl->Next;
        IoFreeMdl(pCurMdl);
        pCurMdl = pNextMdl;
    }
#endif

    IoFreeIrp(pIrp);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_RxIRPCompletionRoutine (nBusyRx: %d)\n", pAdapter->PortName, pAdapter->nBusyRx)
    );
    return STATUS_MORE_PROCESSING_REQUIRED;
}

void MP_USBPostPacketRead(PMP_ADAPTER  pAdapter, PLIST_ENTRY  pList)
{
    PNDIS_PACKET        pNdisPacket;
    PIRP                pIrp = NULL;
    PIO_STACK_LOCATION  nextstack;
    NTSTATUS            Status;
    PNDIS_BUFFER        pCurrentNdisBuffer;
    UINT                dwTotalBufferCount;
    UINT                dwPacketLength;
    PUCHAR              pCurrentBuffer;
    ULONG               dwCurrentBufferLength;
#ifdef USE_MDL
    BOOLEAN     bSecondaryBuffer;
    PMDL        pMdl;
#endif

#ifdef NDIS60_MINIPORT
    MP_USBPostPacketReadEx(pAdapter, pList);
    return;
#endif // NDIS60_MINIPORT

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_USBPostPacketRead\n", pAdapter->PortName)
    );

    pNdisPacket = ListItemToPacket(pList);

    NdisQueryPacket(pNdisPacket,
                    NULL,
                    &dwTotalBufferCount,
                    &pCurrentNdisBuffer,
                    &dwPacketLength);
#ifdef USE_MDL
    pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_ERROR,
            ("<%s> USBPostPacketRead failed to allocate an IRP\n", pAdapter->PortName)
        );
        return;
    }
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_READ;

    bSecondaryBuffer = FALSE;
    while (dwTotalBufferCount-- && (pCurrentNdisBuffer != NULL))
    {
        NdisQueryBufferSafe(
            pCurrentNdisBuffer,
            &pCurrentBuffer,
            &dwCurrentBufferLength,
            NormalPagePriority);


        pMdl = IoAllocateMdl(pCurrentBuffer,
                             dwCurrentBufferLength,
                             bSecondaryBuffer,
                             FALSE,
                             pIrp
        );
        MmBuildMdlForNonPagedPool(pMdl);

        NdisGetNextBuffer(pCurrentNdisBuffer, &pCurrentNdisBuffer);
        bSecondaryBuffer = TRUE;
    }

#else
    NdisQueryBufferSafe(
        pCurrentNdisBuffer,
        &pCurrentBuffer,
        &dwCurrentBufferLength,
        NormalPagePriority);


    pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_ERROR,
            ("<%s> USBPostPacketRead failed to allocate an IRP-2\n", pAdapter->PortName)
        );
        return;
    }
    pIrp->MdlAddress = NULL;
    pIrp->AssociatedIrp.SystemBuffer = pCurrentBuffer;
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_READ;
    nextstack->Parameters.Read.Length = dwCurrentBufferLength;

#endif


    IoSetCompletionRoutine(
        pIrp,
        (PIO_COMPLETION_ROUTINE)MP_RxIRPCompletion,
        (PVOID)pAdapter,
        TRUE, TRUE, TRUE
    );

    Status = QCIoCallDriver(pAdapter->USBDo, pIrp);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_USBPostPacketRead (nBusyRx: %d)\n", pAdapter->PortName, pAdapter->nBusyRx)
    );
    return;
}

/*****************************************************************************
 *****************************************************************************
 *****************************************************************************
 *****************************************************************************/

NTSTATUS MP_RcIRPCompletion(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    PMP_ADAPTER    pAdapter = (PMP_ADAPTER)pContext;
    PLIST_ENTRY    pList;
    NDIS_STATUS    status;
    PMP_OID_READ   pOID;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_RcIRPCompletionRoutine (IRP 0x%p St 0x%x)\n", pAdapter->PortName, pIrp, pIrp->IoStatus.Status)
    );

    NdisAcquireSpinLock(&pAdapter->CtrlReadLock);

    if (IsListEmpty(&pAdapter->CtrlReadPendingList))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_RcIRPCompletionRoutine: error-empty list!\n", pAdapter->PortName)
        );
        NdisReleaseSpinLock(&pAdapter->CtrlReadLock);
        IoFreeIrp(pIrp);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    // Get the completed OID off the RxPendinging list
    pList = RemoveHeadList(&pAdapter->CtrlReadPendingList);

    pOID = CONTAINING_RECORD(pList, MP_OID_READ, List);
    pOID->IrpDataLength = pIrp->IoStatus.Information;
    pOID->IrpStatus = pIrp->IoStatus.Status;

    if (pIrp->IoStatus.Status == STATUS_CANCELLED)
    {
        // No processing required, just pass this on to the free list
        InterlockedDecrement(&(pAdapter->nBusyRCtrl));
        InsertTailList(&pAdapter->CtrlReadFreeList, pList);
    }
    else
    {

        /*  Copy the data back */
#ifndef QCQMI_SUPPORT
        RtlCopyMemory(&(pOID->OID), pIrp->AssociatedIrp.SystemBuffer, pOID->IrpDataLength);
#else
        RtlCopyMemory(&(pOID->QMI), pIrp->AssociatedIrp.SystemBuffer, pOID->IrpDataLength);
#endif

        // The USB has completed this control read, now queue it to
        // be processed up to NDIS
        InsertTailList(&pAdapter->CtrlReadCompleteList, pList);
    }
    NdisReleaseSpinLock(&pAdapter->CtrlReadLock);

    MPWork_ScheduleWorkItem(pAdapter);

    IoFreeIrp(pIrp);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_RcIRPCompletionRoutine (nBusyRCtrl: %d)\n", pAdapter->PortName, pAdapter->nBusyRCtrl)
    );

    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*****************************************************************************
 *****************************************************************************
 *****************************************************************************
 *****************************************************************************/
 //TODO:  ONLY POST FOR PRIMARY ADAPTER.
void MP_USBPostControlRead
(
    PMP_ADAPTER  pAdapter,
#ifndef QCQMI_SUPPORT
    pControlRx   pOID
#else
    PQCQMI       pOID
#endif
)
{
    PIRP pIrp = NULL;
    PIO_STACK_LOCATION nextstack;
    NTSTATUS Status;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_USBPostControlRead\n", pAdapter->PortName)
    );

    pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);

    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_USBPostControlRead failed to allocate an IRP\n", pAdapter->PortName)
        );
        return;
    }
    pIrp->AssociatedIrp.SystemBuffer = pOID;
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    nextstack->Parameters.DeviceIoControl.IoControlCode = IOCTL_QCDEV_READ_CONTROL;
    nextstack->Parameters.Read.Length = sizeof(ControlRx) + pAdapter->CtrlReceiveSize;

    IoSetCompletionRoutine(
        pIrp,
        (PIO_COMPLETION_ROUTINE)MP_RcIRPCompletion,
        (PVOID)pAdapter,
        TRUE, TRUE, TRUE
    );

    Status = QCIoCallDriver(pAdapter->USBDo, pIrp);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_USBPostControlRead IRP 0x%p (Status: %d nBusyRCtrl: %d)\n",
        pAdapter->PortName, pIrp, Status, pAdapter->nBusyRCtrl)
    );

    return;
}


/*****************************************************************************
 *****************************************************************************
 *****************************************************************************
 *****************************************************************************/

NTSTATUS MP_WcIRPCompletion(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    PMP_ADAPTER    pAdapter = (PMP_ADAPTER)pContext;
    PLIST_ENTRY    pList;
    NDIS_STATUS    status;
    BOOLEAN        process = FALSE;
    PMP_OID_WRITE  pListOID, pIrpOID;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_WcIRPCompletion (IRP 0x%p, St 0x%x)\n", pAdapter->PortName, pIrp, pIrp->IoStatus.Status)
    );

    pIrpOID = (PMP_OID_WRITE)pIrp->AssociatedIrp.SystemBuffer;

    NdisAcquireSpinLock(&pAdapter->OIDLock);

    // Get the completed OID off the RxPendinging list
    if (!IsListEmpty(&pAdapter->OIDPendingList))
    {
        pList = RemoveHeadList(&pAdapter->OIDPendingList);

        pListOID = CONTAINING_RECORD(pList, MP_OID_WRITE, List);
        ASSERT(pIrpOID == pListOID);

        // If the write IRP status is success, then move the request and wait for the data
        if (NT_SUCCESS(pIrp->IoStatus.Status))
        {
            InsertTailList(&pAdapter->OIDWaitingList, pList);
            NdisReleaseSpinLock(&pAdapter->OIDLock);
        }
        else
        {
            status = NDIS_STATUS_FAILURE;
            // But if the write irp status is not success, indicate complete with that status to NDIS
            MPQMI_FreeQMIRequestQueue(pListOID);
            MPOID_CleanupOidCopy(pAdapter, pListOID);
            InsertTailList(&pAdapter->OIDFreeList, pList);
            InterlockedDecrement(&(pAdapter->nBusyOID));
            NdisReleaseSpinLock(&pAdapter->OIDLock);

            if (pListOID->OID.OidType == fMP_QUERY_OID)
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_ERROR,
                    ("<%s> MP_WcIRPCompletion (Ind Query Complete IRP failed, nBusyOID = %d) (Oid: 0x%x, Status: 0x%x)\n",
                    pAdapter->PortName, pAdapter->nBusyOID, pListOID->OID.Oid, pIrp->IoStatus.Status)
                );
                MPOID_CompleteOid(pAdapter, status, pListOID, pListOID->OID.OidType, FALSE);
            }
            else
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_ERROR,
                    ("<%s> MP_WcIRPCompletion (Ind Set Complete IRP failed, nBusyOID = %d) (Oid: 0x%x, Status: 0x%x)\n",
                    pAdapter->PortName, pAdapter->nBusyOID, pListOID->OID.Oid, pIrp->IoStatus.Status)
                );
                MPOID_CompleteOid(pAdapter, status, pListOID, pListOID->OID.OidType, FALSE);
            }
        }
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_WcIRPCompletion: error-Nothing pending?\n", pAdapter->PortName)
        );
        NdisReleaseSpinLock(&pAdapter->OIDLock);
    }
    IoFreeIrp(pIrp);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_WcIRPCompletion (nBusyOID: %d)\n", pAdapter->PortName, pAdapter->nBusyOID)
    );

    return STATUS_MORE_PROCESSING_REQUIRED;
}


/*****************************************************************************
 *****************************************************************************
 *****************************************************************************
 *****************************************************************************/

NDIS_STATUS MP_USBRequest
(
    PMP_ADAPTER pAdapter,
    PVOID       pOID,
    ULONG       dwBufferLen,
    PLIST_ENTRY pList
)
{

    NTSTATUS Status;
    PQCWRITEQUEUE QMIElement;
    PQCQMI QMI;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_USBRequest\n", pAdapter->PortName)
    );

    Status = NdisAllocateMemoryWithTag(&QMIElement, sizeof(QCWRITEQUEUE) + dwBufferLen, QUALCOMM_TAG);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_OID_QMI,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_USBRequest error: Failed to allocate memory for QMI Writes\n", pAdapter->PortName)
        );
        return NDIS_STATUS_FAILURE;
    }
    QMIElement->QMILength = dwBufferLen;
    RtlCopyMemory(QMIElement->Buffer, pOID, dwBufferLen);
    QMI = (PQCQMI)QMIElement->Buffer;
    NdisAcquireSpinLock(&pAdapter->CtrlWriteLock);
    if ((QMI->QMIType != QMUX_TYPE_CTL) && (QMI->ClientId == 0x00))
    {
        InsertTailList(&pAdapter->CtrlWritePendingList, &(QMIElement->QCWRITERequest));
    }
    else
    {
        InsertTailList(&pAdapter->CtrlWriteList, &(QMIElement->QCWRITERequest));
    }
    InterlockedIncrement(&(pAdapter->nBusyWrite));
    NdisReleaseSpinLock(&pAdapter->CtrlWriteLock);

    InsertTailList(&pAdapter->OIDWaitingList, pList);

    MPWork_ScheduleWorkItem(pAdapter);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_USBRequest (nBusyOID: %d)\n", pAdapter->PortName, pAdapter->nBusyOID)
    );

    return NDIS_STATUS_PENDING;
}

/*****************************************************************************
 *****************************************************************************
 *****************************************************************************
 *****************************************************************************/

NTSTATUS MP_CleanUpIRPCompletion(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{

    //    IoReuseIrp(pIrp, STATUS_SUCCESS);
    IoFreeIrp(pIrp);

    return STATUS_MORE_PROCESSING_REQUIRED;


}

void MP_USBCleanUp(PMP_ADAPTER pAdapter)
{
    PIRP pIrp = NULL;
    PIO_STACK_LOCATION nextstack;
    NTSTATUS Status;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_USBCleanUp\n", pAdapter->PortName)
    );

    pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> USBCleanup failed to allocate an IRP\n", pAdapter->PortName)
        );
        return;
    }
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_CLEANUP;

    IoSetCompletionRoutine(
        pIrp,
        (PIO_COMPLETION_ROUTINE)MP_CleanUpIRPCompletion,
        (PVOID)pAdapter,
        TRUE, TRUE, TRUE
    );

    Status = QCIoCallDriver(pAdapter->USBDo, pIrp);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_USBCleanUp (IRP 0x%p)\n", pAdapter->PortName, pIrp)
    );

    return;
}

NDIS_STATUS MP_USBSendControl
(
    PMP_ADAPTER pAdapter,
    PVOID       Buffer,
    ULONG       BufferLength
)
{
    PQCWRITEQUEUE QMIElement;
    NDIS_STATUS Status;
    PQCQMI QMI;
    Status = NdisAllocateMemoryWithTag(&QMIElement, sizeof(QCWRITEQUEUE) + BufferLength, QUALCOMM_TAG);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_OID_QMI,
            MP_DBG_LEVEL_ERROR,
            ("<%s> error: Failed to allocate memory for QMI Writes\n", pAdapter->PortName)
        );
        return NDIS_STATUS_FAILURE;
    }
    QMIElement->QMILength = BufferLength;
    RtlCopyMemory(QMIElement->Buffer, Buffer, BufferLength);
    QMI = (PQCQMI)QMIElement->Buffer;
    NdisAcquireSpinLock(&pAdapter->CtrlWriteLock);
    if ((QMI->QMIType != QMUX_TYPE_CTL) && (QMI->ClientId == 0x00))
    {
        InsertTailList(&pAdapter->CtrlWritePendingList, &(QMIElement->QCWRITERequest));
    }
    else
    {
        InsertTailList(&pAdapter->CtrlWriteList, &(QMIElement->QCWRITERequest));
    }
    InterlockedIncrement(&(pAdapter->nBusyWrite));
    NdisReleaseSpinLock(&pAdapter->CtrlWriteLock);
    MPWork_ScheduleWorkItem(pAdapter);
    return NDIS_STATUS_PENDING;
}

NDIS_STATUS MP_USBSendControlRequest
(
    PMP_ADAPTER pAdapter,
    PVOID       Buffer,
    ULONG       BufferLength
)
{

    PIRP pIrp = NULL;
    PIO_STACK_LOCATION nextstack;
    NTSTATUS Status;
    PQCQMI QMI;
    PMP_ADAPTER returnAdapter;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_USBSendControl\n", pAdapter->PortName)
    );

    pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> USBSendControl failed to allocate an IRP\n", pAdapter->PortName)
        );
        return NDIS_STATUS_FAILURE;
    }

    pIrp->AssociatedIrp.SystemBuffer = ExAllocatePool(NonPagedPool, BufferLength);
    if (pIrp->AssociatedIrp.SystemBuffer == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> USBSendControl failed to allocate buffer\n", pAdapter->PortName)
        );
        IoFreeIrp(pIrp);
        return NDIS_STATUS_FAILURE;
    }

    NdisMoveMemory
    (
        pIrp->AssociatedIrp.SystemBuffer,
        Buffer,
        BufferLength
    );

    QMI = (PQCQMI)Buffer;
    InterlockedIncrement(&(pAdapter->PendingCtrlRequests[QMI->QMIType]));

    QCNET_DbgPrint
    (
        (MP_DBG_MASK_OID_QMI),
        MP_DBG_LEVEL_DETAIL,
        ("<%s> MP_USBSendControlRequest : Number of IOC QMI requests pending for service %d is %d\n",
        pAdapter->PortName,
        QMI->QMIType,
        pAdapter->PendingCtrlRequests[QMI->QMIType]
    )
    );

    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    nextstack->Parameters.DeviceIoControl.IoControlCode = IOCTL_QCDEV_SEND_CONTROL;
    nextstack->Parameters.DeviceIoControl.InputBufferLength = BufferLength;

    IoSetCompletionRoutine
    (
        pIrp,
        (PIO_COMPLETION_ROUTINE)MP_SendControlCompletion,
        (PVOID)pAdapter,
        TRUE, TRUE, TRUE
    );
    returnAdapter = FindStackDeviceObject(pAdapter);
    if (returnAdapter != NULL)
    {
        Status = QCIoCallDriver(returnAdapter->USBDo, pIrp);
    }
    else
    {
        Status = STATUS_UNSUCCESSFUL;
        pIrp->IoStatus.Status = Status;
        MP_SendControlCompletion(pAdapter->USBDo, pIrp, pAdapter);
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_USBSendControl (IRP 0x%p)\n", pAdapter->PortName, pIrp)
    );

    return NDIS_STATUS_PENDING;
}  // MP_USBSendControl

NTSTATUS MP_SendControlCompletion
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    PMP_ADAPTER    pAdapter = (PMP_ADAPTER)pContext;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> MP_SendControlCompletion (IRP 0x%p IoStatus: 0x%x)\n", pAdapter->PortName, pIrp, pIrp->IoStatus.Status)
    );

    if (pIrp->AssociatedIrp.SystemBuffer != NULL)
    {
        ExFreePool(pIrp->AssociatedIrp.SystemBuffer);
    }
    IoFreeIrp(pIrp);

    return STATUS_MORE_PROCESSING_REQUIRED;
}  // MP_SendControlCompletion


NDIS_STATUS MP_USBSendCustomCommand
(
    PMP_ADAPTER pAdapter,
    ULONG       IoControlCode,
    PVOID       Buffer,
    ULONG       BufferLength,
    ULONG *pReturnBufferLength
)
{

    PIRP pIrp = NULL;
    PIO_STACK_LOCATION nextstack;
    NTSTATUS Status;
    KEVENT Event;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;

    //
    // Set up the event we'll use.
    //

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);


    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_USBSendCustomCommand\n", pAdapter->PortName)
    );

    pIrp = IoAllocateIrp((CCHAR)(pDevExt->StackDeviceObject->StackSize + 2), FALSE);
    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_USBSendCustomCommand failed to allocate an IRP\n", pAdapter->PortName)
        );
        return NDIS_STATUS_FAILURE;
    }

    pIrp->AssociatedIrp.SystemBuffer = ExAllocatePool(NonPagedPool, 4096);
    if (pIrp->AssociatedIrp.SystemBuffer == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MP_USBSendCustomCommand failed to allocate buffer\n", pAdapter->PortName)
        );
        IoFreeIrp(pIrp);
        return NDIS_STATUS_FAILURE;
    }

    NdisMoveMemory
    (
        pIrp->AssociatedIrp.SystemBuffer,
        Buffer,
        BufferLength
    );

    // set the event
    pIrp->UserEvent = &Event;

    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    nextstack->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    nextstack->Parameters.DeviceIoControl.InputBufferLength = BufferLength;

    IoSetCompletionRoutine
    (
        pIrp,
        (PIO_COMPLETION_ROUTINE)MP_SendCustomCommandCompletion,
        (PVOID)pAdapter,
        TRUE, TRUE, TRUE
    );

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_USBSendCustomCommand (IRP 0x%p)\n", pAdapter->PortName, pIrp)
    );

    Status = IoCallDriver(pDevExt->StackDeviceObject, pIrp);

    KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, 0);

    Status = pIrp->IoStatus.Status;
    if (Status == STATUS_SUCCESS)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_TRACE,
            ("<%s> <--- MP_USBSendCustomCommand (Command response: 0x%p Length: %d Status: %x)\n", pAdapter->PortName, pIrp->AssociatedIrp.SystemBuffer,
            pIrp->IoStatus.Information, Status)
        );
        if (pIrp->IoStatus.Information > BufferLength)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            *pReturnBufferLength = pIrp->IoStatus.Information;
            if (*pReturnBufferLength > 0)
            {
                RtlCopyMemory((CHAR *)Buffer, pIrp->AssociatedIrp.SystemBuffer, pIrp->IoStatus.Information);
            }
        }
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_TRACE,
            ("<%s> <--- MP_USBSendCustomCommand (Command failed Status: %x)\n", pAdapter->PortName, Status)
        );
    }
    if (pIrp->AssociatedIrp.SystemBuffer != NULL)
    {
        ExFreePool(pIrp->AssociatedIrp.SystemBuffer);
    }
    IoFreeIrp(pIrp);

    return Status;
}

NTSTATUS MP_SendCustomCommandCompletion
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    PMP_ADAPTER    pAdapter = (PMP_ADAPTER)pContext;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> MP_SendCustomCommandCompletion (IRP 0x%p IoStatus: 0x%x)\n", pAdapter->PortName, pIrp, pIrp->IoStatus.Status)
    );
#if 0
    if (pIrp->PendingReturned)
    {
        IoMarkIrpPending(pIrp);
    }
    else
    {
        KeSetEvent(pIrp->UserEvent, 0, FALSE);
    }
#else
    KeSetEvent(pIrp->UserEvent, 0, FALSE);
#endif
    return STATUS_MORE_PROCESSING_REQUIRED;
    }



#ifdef QC_IP_MODE

// process ARP locally or remove ETH hdr
// Assumption: pAdapter->IPModeEnabled == TRUE
// Return Values:
//    FALSE: to be locally processed
//    TRUE:  to be sent to the device
BOOLEAN MPUSB_PreparePacket
(
    PMP_ADAPTER pAdapter,
    PCHAR       EthPkt,
    PULONG      Length  // include QOS hdr when QOS enabled
)
{
    PCHAR       dataPtr;
    PQC_ETH_HDR ethHdr;
    ULONG       ethLen;

    // find the start of ETH packet
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            dataPtr = (PCHAR)EthPkt + sizeof(MPQOS_HDR8);
            ethLen = *Length - sizeof(MPQOS_HDR8);
        }
        else
        {
            dataPtr = (PCHAR)EthPkt + sizeof(MPQOS_HDR);
            ethLen = *Length - sizeof(MPQOS_HDR);
        }
    }
    else
    {
        dataPtr = (PCHAR)EthPkt;
        ethLen = *Length;
    }
    ethHdr = (PQC_ETH_HDR)dataPtr;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> IPO: -->_PreparePacket: EtherType 0x%04X\n", pAdapter->PortName, ethHdr->EtherType)
    );

    // examine Ethernet header
    switch (ntohs(ethHdr->EtherType))
    {
        case ETH_TYPE_ARP:  // && IPV4
        {
            MPMAIN_PrintBytes
            (
                dataPtr, *Length, *Length, "IPO: ARP->D", pAdapter,
                MP_DBG_MASK_DATA_WT, MP_DBG_LEVEL_VERBOSE
            );
            // locally process ARP under IPV4
            if (pAdapter->IPSettings.IPV4.Address != 0)
            {
                MPUSB_ProcessARP(pAdapter, dataPtr, ethLen);
                return FALSE;
            }
            else if (pAdapter->IPSettings.IPV6.Flags != 0)
            {
                // drop the ARP
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_WRITE,
                    MP_DBG_LEVEL_DETAIL,
                    ("<%s> IPO: _PreparePacket: IPV6 ARP, drop it\n", pAdapter->PortName)
                );
                return FALSE;
            }
            else
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_WRITE,
                    MP_DBG_LEVEL_DETAIL,
                    ("<%s> IPO: _PreparePacket: unknown ARP, drop\n", pAdapter->PortName)
                );
                return FALSE;
            }
        }
        case ETH_TYPE_IPV4:
        {
            PCHAR ipPkt = dataPtr + sizeof(QC_ETH_HDR);
            ULONG ipLen;

            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> IPO: _PreparePacket: IP 0x%04X\n", pAdapter->PortName,
                ntohs(ethHdr->EtherType))
            );
            MPMAIN_PrintBytes
            (
                dataPtr, 64, *Length, "IPO: IP->D", pAdapter,
                MP_DBG_MASK_WRITE, MP_DBG_LEVEL_VERBOSE
            );

            if (pAdapter->IPSettings.IPV4.Address != 0)
            {
                // remove ETH header
                ipLen = ethLen - sizeof(QC_ETH_HDR);
                *Length -= sizeof(QC_ETH_HDR);
                RtlMoveMemory(dataPtr, ipPkt, ipLen);
                return TRUE;
            }
            else
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_WRITE,
                    MP_DBG_LEVEL_DETAIL,
                    ("<%s> IPO: _PreparePacket: no V4 addr, drop pkt\n", pAdapter->PortName)
                );
                return FALSE;
            }
        }

        case ETH_TYPE_IPV6:
        {
            PCHAR ipPkt = dataPtr + sizeof(QC_ETH_HDR);
            ULONG ipLen;

            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> IPO: _PreparePacket: IP 0x%04X\n", pAdapter->PortName,
                ntohs(ethHdr->EtherType))
            );
            MPMAIN_PrintBytes
            (
                dataPtr, 64, *Length, "IPO: IP->D", pAdapter,
                MP_DBG_MASK_WRITE, MP_DBG_LEVEL_VERBOSE
            );

            if (pAdapter->IPSettings.IPV6.Flags != 0)
            {
                // remove ETH header
                ipLen = ethLen - sizeof(QC_ETH_HDR);
                *Length -= sizeof(QC_ETH_HDR);
                RtlMoveMemory(dataPtr, ipPkt, ipLen);
                return TRUE;
            }
            else
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_WRITE,
                    MP_DBG_LEVEL_DETAIL,
                    ("<%s> IPO: _PreparePacket: V6 mode, drop V4 pkt\n", pAdapter->PortName)
                );
                return FALSE;
            }
        }
        default:
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_ERROR,
                ("<%s> IPO: <--_PreparePacket: unknown EtherType 0x%04X, drop\n",
                pAdapter->PortName, ntohs(ethHdr->EtherType))
            );
            return FALSE;
        }
    }
}  // MPUSB_PreparePacket

VOID MPUSB_ProcessARP
(
    PMP_ADAPTER pAdapter,  // MP adapter
    PCHAR       EthPkt,    // ETH pkt containing ARP
    ULONG       Length     // length of the ETH pkt
)
{
    PQC_ARP_HDR arpHdr;
    UCHAR       tempHA[ETH_LENGTH_OF_ADDRESS] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    SIZE_T      sz;
    BOOLEAN     bRespond = FALSE;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> IPO: -->ProcessARP: %dB\n", pAdapter->PortName, Length)
    );

    arpHdr = (PQC_ARP_HDR)(EthPkt + sizeof(QC_ETH_HDR));

    // Ignore non-Ethernet HW type and non-request Operation
    if ((ntohs(arpHdr->HardwareType) != 1) || (ntohs(arpHdr->Operation) != 1))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPO: ProcessARP: ignore HW %d OP %d\n", pAdapter->PortName,
            arpHdr->HardwareType, arpHdr->Operation)
        );
        return;
    }

    // Ignore non-IPV4 protocol type
    if (ntohs(arpHdr->ProtocolType) != ETH_TYPE_IPV4)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPO: ProcessARP: ignore protocol %d\n", pAdapter->PortName,
            arpHdr->ProtocolType)
        );
        return;
    }

    // Validate HLEN and PLEN
    if (arpHdr->HLEN != ETH_LENGTH_OF_ADDRESS)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPO: ProcessARP: wrong HLEN %d\n", pAdapter->PortName,
            arpHdr->HLEN)
        );
        return;
    }
    if (arpHdr->PLEN != 4)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPO: ProcessARP: wrong PLEN %d\n", pAdapter->PortName,
            arpHdr->PLEN)
        );
        return;
    }

    // Ignore gratuitous ARP
    if (arpHdr->SenderIP == arpHdr->TargetIP)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPO: ProcessARP: ignore gratuitous ARP (IP 0x%x)\n",
            pAdapter->PortName, arpHdr->TargetIP)
        );
        return;
    }

    // Request for HA
    sz = RtlCompareMemory(arpHdr->TargetHA, tempHA, ETH_LENGTH_OF_ADDRESS);

    // DAD
    if ((arpHdr->SenderIP == 0) && (arpHdr->TargetIP != pAdapter->IPSettings.IPV4.Address))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> IPO: ProcessARP: DAD (IP 0x%x)\n", pAdapter->PortName, arpHdr->TargetIP)
        );
        bRespond = TRUE;
    }
    else if ((arpHdr->SenderIP != 0) && (ETH_LENGTH_OF_ADDRESS == sz))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> IPO: ProcessARP: req for HA\n", pAdapter->PortName)
        );
        bRespond = TRUE;
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPO: ProcessARP: Ignore\n", pAdapter->PortName)
        );
    }

    if (bRespond == TRUE)
    {
        // respond with canned ARP
        MPUSB_ArpResponse(pAdapter, EthPkt, Length);
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> IPO: <--ProcessARP: local rsp %d\n", pAdapter->PortName, bRespond)
    );
}  // MPUSB_ProcessARP

NTSTATUS MPUSB_LoopbackIRPCompletion
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    PMP_ADAPTER pAdapter = (PMP_ADAPTER)pContext;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> IPO: MPUSB_LoopbackIRPCompletion (IRP 0x%p ST 0x%x)\n",
        pAdapter->PortName, pIrp, pIrp->IoStatus.Status)
    );

    if (pIrp->AssociatedIrp.SystemBuffer != NULL)
    {
        ExFreePool(pIrp->AssociatedIrp.SystemBuffer);
    }
    IoFreeIrp(pIrp);

    return STATUS_MORE_PROCESSING_REQUIRED;
}  // MPUSB_LoopbackIRPCompletion

VOID MPUSB_ArpResponse
(
    PMP_ADAPTER pAdapter,  // MP adapter
    PCHAR       EthPkt,    // ETH pkt containing ARP
    ULONG       Length     // length of the ETH pkt
)
{
    PIO_STACK_LOCATION nextstack;
    NTSTATUS           ntStatus;
    PQC_ETH_HDR        ethHdr;
    PQC_ARP_HDR        arpHdr;
    PVOID              dataPkt;
    PIRP               pIrp;
    PCHAR              p;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> IPO: -->MPUSB_ArpResponse: ETH_Len %dB\n", pAdapter->PortName, Length)
    );

    // 1. Allocate new buffer for the response
    dataPkt = ExAllocatePoolWithTag
    (
        NonPagedPool,
        Length,
        (ULONG)'1000'
    );
    if (dataPkt == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPO: _ArpResponse: NO_MEM\n", pAdapter->PortName)
        );
        return;
    }

    // 2. Allocate IRP
    pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> IPO: _ArpResponse: NO_MEM IRP\n", pAdapter->PortName)
        );
        ExFreePool(dataPkt);
        return;
    }

    // 3. Formulate the response
    // Target: arpHdr->SenderHA & arpHdr->SenderIP
    // Sender: pAdapter->MacAddress2 & pAdapter->IPSettings.IPV4.Address
    RtlCopyMemory(dataPkt, EthPkt, Length);
    p = (PCHAR)dataPkt;

    // Ethernet header
    ethHdr = (PQC_ETH_HDR)p;
    RtlCopyMemory(ethHdr->DstMacAddress, ethHdr->SrcMacAddress, ETH_LENGTH_OF_ADDRESS);
    RtlCopyMemory(ethHdr->SrcMacAddress, pAdapter->MacAddress2, ETH_LENGTH_OF_ADDRESS);

    // ARP Header
    arpHdr = (PQC_ARP_HDR)(p + sizeof(QC_ETH_HDR));

    // target/requestor MAC and IP
    RtlCopyMemory(arpHdr->TargetHA, arpHdr->SenderHA, ETH_LENGTH_OF_ADDRESS);
    arpHdr->SenderIP = arpHdr->TargetIP;

    // sender/remote MAC and IP
    RtlCopyMemory(arpHdr->SenderHA, pAdapter->MacAddress2, ETH_LENGTH_OF_ADDRESS);
    arpHdr->TargetIP = pAdapter->IPSettings.IPV4.Address;

    // Operation: reply
    arpHdr->Operation = ntohs(0x0002);

    // 4. Prepare IRP and send to USB with LOOPBACK ioctl
    pIrp->AssociatedIrp.SystemBuffer = dataPkt;
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    nextstack->Parameters.DeviceIoControl.IoControlCode = IOCTL_QCDEV_LOOPBACK_DATA_PKT;
    nextstack->Parameters.DeviceIoControl.OutputBufferLength = Length;

    IoSetCompletionRoutine
    (
        pIrp,
        (PIO_COMPLETION_ROUTINE)MPUSB_LoopbackIRPCompletion,
        (PVOID)pAdapter,
        TRUE, TRUE, TRUE
    );

    MPMAIN_PrintBytes
    (
        dataPkt, 128, Length, "IPO: LPBK", pAdapter,
        MP_DBG_MASK_READ, MP_DBG_LEVEL_VERBOSE
    );

    ntStatus = QCIoCallDriver(pAdapter->USBDo, pIrp);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> IPO: <--MPUSB_ArpResponse IRP 0x%p (St 0x%x)\n",
        pAdapter->PortName, pIrp, ntStatus)
    );

    // 5. Free the buffer in the completion routine
    // 6. Free the IRP in the completion routine

}  // MPUSB_ArpResponse

#endif // QC_IP_MODE

#ifdef NDIS60_MINIPORT

NTSTATUS MP_TxIRPCompleteExStub
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    return MP_TxIRPCompleteEx(pDO, pIrp, pContext, TRUE);
}

VOID MP_USBPostPacketReadEx(PMP_ADAPTER pAdapter, PLIST_ENTRY pList)
{
    PIRP                pIrp = NULL;
    PIO_STACK_LOCATION  nextstack;
    PUCHAR              pCurrentBuffer;
    ULONG               dwCurrentBufferLength;
    PMPUSB_RX_NBL       pRxNBL;

    pRxNBL = CONTAINING_RECORD(pList, MPUSB_RX_NBL, List);
    pCurrentBuffer = pRxNBL->BvaPtr = pRxNBL->BVA;
    dwCurrentBufferLength = QCMP_MAX_DATA_PKT_SIZE;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> -->MP_USBPostPacketReadEx(N%d F%d U%d) => 0x%p[%d]\n", pAdapter->PortName,
        pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb,
        pRxNBL, pRxNBL->Index)
    );

    pIrp = pRxNBL->Irp;
    if (pIrp == NULL)
    {
        pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
        pRxNBL->Irp = pIrp;
    }
    else
    {
        IoReuseIrp(pIrp, STATUS_SUCCESS);
    }

    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_ERROR,
            ("<%s> USBPostPacketReadEx failed to allocate an IRP-2\n", pAdapter->PortName)
        );
        return;
    }

    pIrp->MdlAddress = NULL;
    pIrp->AssociatedIrp.SystemBuffer = pCurrentBuffer;
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_READ;
    nextstack->Parameters.Read.Length = dwCurrentBufferLength;

    IoSetCompletionRoutine
    (
        pIrp,
        (PIO_COMPLETION_ROUTINE)MP_RxIRPCompletionEx,
        (PVOID)pAdapter,
        TRUE, TRUE, TRUE
    );

    QCIoCallDriver(pAdapter->USBDo, pIrp);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_USBPostPacketReadEx(nBusyRx %d N%d F%d U%d)\n", pAdapter->PortName,
        pAdapter->nBusyRx, pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb)
    );
    return;
}  // MP_USBPostPacketReadEx

NTSTATUS MP_RxIRPCompletionEx(PDEVICE_OBJECT pDO, PIRP pIrp, PVOID pContext)
{
    PMP_ADAPTER      pAdapter = (PMP_ADAPTER)pContext;
    PLIST_ENTRY      pList;
    BOOLEAN          indicate = FALSE;
    PMPUSB_RX_NBL    pRxNBL;
    PNET_BUFFER_LIST nbl = NULL;
    PNET_BUFFER      nb = NULL;
    BOOLEAN          bPause;
    PMDL             mdl;
    ULONG            rxFlags;
    KIRQL            irql = KeGetCurrentIrql();
    BOOLEAN          bIndChain = TRUE;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> -->MP_RxIRPCompletionEx IRP 0x%p (0x%x[%I64u]) Filter 0x%x\n",
        pAdapter->PortName, pIrp, pIrp->IoStatus.Status, pAdapter->GoodReceives,
        pAdapter->PacketFilter)
    );

    NdisAcquireSpinLock(&pAdapter->RxLock);

    pRxNBL = MPUSB_FindRxRequest(pAdapter, pIrp);
    if (pRxNBL == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_CRITICAL,
            ("<%s> RxIRPCompletionEx: panic - no pending!!!\n", pAdapter->PortName)
        );

        NdisReleaseSpinLock(&pAdapter->RxLock);

#ifdef DBG
        DbgBreakPoint();
#else
        // will result in memory leak and system instability
        return STATUS_MORE_PROCESSING_REQUIRED;
#endif // DBG
    }

    bPause = MPMAIN_InPauseState(pAdapter);

    // DbgPrint("<%s> MP_RxIRPCompletionEx: IRP 0x%p QNBL 0x%p[%d] filter 0x%x pause %d\n",
    //        pAdapter->PortName, pIrp, pRxNBL, pRxNBL->Index, pAdapter->PacketFilter, bPause);

    if ((!NT_SUCCESS(pIrp->IoStatus.Status)) ||
        (pAdapter->PacketFilter == 0) ||   // no filter
        (pAdapter->ToPowerDown == TRUE) ||
        (bPause == TRUE))
    {
        // This was not a successful receive so just relink this receive to the free list
        InsertTailList(&pAdapter->RxFreeList, &pRxNBL->List);
        InterlockedDecrement(&(pAdapter->nBusyRx));
        InterlockedIncrement(&(pAdapter->nRxFreeInMP));

        if (!NT_SUCCESS(pIrp->IoStatus.Status))
        {
            ++(pAdapter->BadReceives);
        }
        else
        {
            ++(pAdapter->DroppedReceives);
        }

        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_ERROR,
            ("<%s> RxIRPCompletionEx: Rx Packet discarded. FIlter %X Pause %d\n",
            pAdapter->PortName, pAdapter->PacketFilter, bPause)
        );
    }
    else
    {
        indicate = TRUE;
        ++(pAdapter->GoodReceives);
        pAdapter->RxBytesGood += pIrp->IoStatus.Information;
        nbl = pRxNBL->RxNBL;
        nb = NET_BUFFER_LIST_FIRST_NB(nbl);
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> RxIRPCompletionEx (N%d F%d U%d)\n", pAdapter->PortName,
        pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb)
    );

#ifdef QCUSB_MUX_PROTOCOL
    // #ifdef QCUSB_DROP_DATA_TEST
    if ((indicate == TRUE) && (pAdapter->DropDataTest == 1))
    {
        indicate = FALSE;

        // TEST -- dropping receive to (keep NDIS out of the picture) so just relink this receive to the free list
        InsertTailList(&pAdapter->RxFreeList, &pRxNBL->List);
        InterlockedDecrement(&(pAdapter->nBusyRx));
        InterlockedIncrement(&(pAdapter->nRxFreeInMP));
        pAdapter->DroppedReceives++;

        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_ERROR,
            ("<%s> RxIRPCompletionEx: TEST - Rx Packet discarded, USB %d\n", pAdapter->PortName, pAdapter->nRxPendingInUsb)
        );
        MPWork_ScheduleWorkItem(pAdapter);
    }
    // #endif // QCUSB_DROP_DATA_TEST
#endif // QCUSB_MUX_PROTOCOL

    NdisReleaseSpinLock(&pAdapter->RxLock);

    if ((indicate == TRUE) && (pAdapter->nRxHeldByNdis > pAdapter->RxIndClusterSize))  // throttling SS mode
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_FORCE,
            ("<%s> RxIRPCompletionEx: IRQL-%d YIELD (N%d F%d U%d)\n", pAdapter->PortName,
            irql, pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb)
        );
        if (irql == PASSIVE_LEVEL)
        {
            LARGE_INTEGER delayValue;

            delayValue.QuadPart = -(5 * 100L);

            // MPMAIN_Wait(-(1 * 1000L)); // 0.1ms
            KeDelayExecutionThread(KernelMode, FALSE, &delayValue);  // 50us
        }
    }
    if (indicate == TRUE)
    {
        if ((pAdapter->DebugMask & MP_DBG_MASK_READ) != 0)
        {
#ifdef QC_IP_MODE
            if (pIrp->AssociatedIrp.SystemBuffer != NULL)
            {
                if (pAdapter->IPModeEnabled == TRUE)
                {
                    if ((QCMP_NDIS620_Ok == FALSE) || (pAdapter->NdisMediumType == NdisMedium802_3))
                    {
                        MPMAIN_PrintBytes
                        (
                            pIrp->AssociatedIrp.SystemBuffer,
                            64,
                            pIrp->IoStatus.Information,
                            "IPO: IN-DATA",
                            pAdapter,
                            MP_DBG_MASK_READ, MP_DBG_LEVEL_VERBOSE
                        );
                    }
                }
            }
#endif // QC_IP_MODE
        }

        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MP_RxIRPCompletionEx: 0x%p indicated PacketLen: %dB[%I64u]\n",
            pAdapter->PortName, pIrp, pIrp->IoStatus.Information, pAdapter->GoodReceives)
        );

        // setup NBL
        NET_BUFFER_LIST_STATUS(nbl) = NDIS_STATUS_SUCCESS;
        NET_BUFFER_LIST_NEXT_NBL(nbl) = NULL;
        NET_BUFFER_LIST_INFO(nbl, Ieee8021QNetBufferListInfo) = NULL; // pRxNBL->OOB.Value;
        NET_BUFFER_LIST_INFO(nbl, MediaSpecificInformation) = NULL;
        NET_BUFFER_LIST_INFO(nbl, NetBufferListFrameType) = NULL;
        nbl->SourceHandle = pAdapter->AdapterHandle;

        // setup NB
        NET_BUFFER_DATA_LENGTH(nb) = pIrp->IoStatus.Information;
        NET_BUFFER_DATA_OFFSET(nb) = 0;
        NET_BUFFER_NEXT_NB(nb) = NULL;

        // setup MDL
        NET_BUFFER_CURRENT_MDL(nb) = mdl = NET_BUFFER_FIRST_MDL(nb);
        NET_BUFFER_CURRENT_MDL_OFFSET(nb) = 0;
        NdisAdjustMdlLength(mdl, pIrp->IoStatus.Information); // set MDL len
        mdl->Next = NULL;

        nbl->MiniportReserved[0] = (PVOID)pRxNBL;

#ifdef NDIS620_MINIPORT
#ifdef QC_IP_MODE

        if ((pAdapter->NdisMediumType == NdisMediumWirelessWan) &&
            (pAdapter->IPModeEnabled == TRUE))

        {
            MPUSB_SetIPType
            (
                pIrp->AssociatedIrp.SystemBuffer,
                pIrp->IoStatus.Information,
                nbl
            );
        }

#endif //QC_IP_MODE
#endif // NDIS620_MINIPORT

        if (bIndChain == TRUE)
        {
            INT idx;

            idx = MPUSB_GetRxStreamIndex(pAdapter, pIrp->AssociatedIrp.SystemBuffer, pIrp->IoStatus.Information); // odd or even port

            // add to RX NBL chain
            NdisAcquireSpinLock(&pAdapter->RxIndLock[idx]);
            InsertTailList(&pAdapter->RxNblChain[idx], &pRxNBL->List);
            InterlockedIncrement(&(pAdapter->nRxInNblChain));
            KeSetEvent(&pAdapter->RxThreadProcessEvent[idx], IO_NO_INCREMENT, FALSE);
            NdisReleaseSpinLock(&pAdapter->RxIndLock[idx]);
        }
        else
        {
            rxFlags = NDIS_RECEIVE_FLAGS_RESOURCES;  // 0

            InterlockedIncrement(&(pAdapter->nRxHeldByNdis));

            NdisMIndicateReceiveNetBufferLists
            (
                pAdapter->AdapterHandle,
                nbl,
                NDIS_DEFAULT_PORT_NUMBER,
                1,  // NumberOfNetBufferLists 
                rxFlags   // flags
            );

            if ((rxFlags | NDIS_RECEIVE_FLAGS_RESOURCES) != 0)
            {
                // reclaim RX IRP

                NdisAcquireSpinLock(&pAdapter->RxLock);
                InsertTailList(&pAdapter->RxFreeList, &pRxNBL->List);
                NdisReleaseSpinLock(&pAdapter->RxLock);
                InterlockedDecrement(&(pAdapter->nBusyRx));
                InterlockedIncrement(&(pAdapter->nRxFreeInMP));
                InterlockedDecrement(&(pAdapter->nRxHeldByNdis));

                QCNET_DbgPrint
                (
                    MP_DBG_MASK_READ,
                    MP_DBG_LEVEL_FORCE,
                    ("<%s> RxIRPCompletionEx: reclaim RX NBL nBusyRx: %d\n", pAdapter->PortName, pAdapter->nBusyRx)
                );
                MPWork_ScheduleWorkItem(pAdapter);
            }  // not chain
        }
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_ERROR,  // MP_DBG_LEVEL_FORCE
        ("<%s> <--MP_RxIRPCompletionEx (nBusyRx %d N%d F%d U%d CH%d)\n", pAdapter->PortName,
        pAdapter->nBusyRx, pAdapter->nRxHeldByNdis, pAdapter->nRxFreeInMP, pAdapter->nRxPendingInUsb,
        pAdapter->nRxInNblChain)
    );

    return STATUS_MORE_PROCESSING_REQUIRED;
}  // MP_RxIRPCompletionEx

// This function is called with in spin lock
PMPUSB_RX_NBL MPUSB_FindRxRequest(PMP_ADAPTER pAdapter, PIRP Irp)
{
    PMPUSB_RX_NBL pRxNBL = NULL, pRxItem;
    PLIST_ENTRY   headOfList, peekEntry;
    int           i = 0;

    if (!IsListEmpty(&pAdapter->RxPendingList))
    {
        headOfList = &pAdapter->RxPendingList;
        peekEntry = headOfList->Flink;
        while (peekEntry != headOfList)
        {
            ++i;
            pRxItem = CONTAINING_RECORD(peekEntry, MPUSB_RX_NBL, List);
            if (pRxItem->Irp == Irp)
            {
                pRxNBL = pRxItem;
                break;
            }
            peekEntry = peekEntry->Flink;
        }
    }

    if (pRxNBL != NULL)
    {
        RemoveEntryList(&pRxNBL->List);
        InterlockedDecrement(&(pAdapter->nRxPendingInUsb));
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_TRACE,
        ("<%s> MPUSB_FindRxRequest: 0x%p RxNBL at [%d] for %p\n",
        pAdapter->PortName, pRxNBL, i, Irp)
    );

    return pRxNBL;
}  // MPUSB_FindRxRequest

VOID MP_USBTxPacketEx
(
    PMP_ADAPTER  pAdapter,
    PLIST_ENTRY  pList,
    ULONG        QosFlowId,
    BOOLEAN      IsQosPacket
)
{
    PIRP                pIrp;
    PIO_STACK_LOCATION  nextstack;
    NDIS_STATUS         Status;
    UINT                dwPacketLength;
    PVOID               usbBuffer = NULL;
#ifdef MP_QCQOS_ENABLED
    PMPQOS_HDR          qosHdr;
    PMPQOS_HDR8         qosHdr8;
#endif // MP_QCQOS_ENABLED
    long                sendBytes;
    BOOLEAN             kicker = FALSE;
    PNET_BUFFER_LIST      netBufferList;
    PNET_BUFFER           netBuffer;
    PMPUSB_TX_CONTEXT_NBL txContext = NULL;
    PMPUSB_TX_CONTEXT_NB  nbContext;
    int                   nbIdx, numNBs;
    PUCHAR                dataPkt;
    BOOLEAN               bAbort = FALSE;
    PMP_ADAPTER           returnAdapter;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MP_USBTxPacketEx: 0x%p\n", pAdapter->PortName, pList)
    );

    txContext = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NBL, List);
    netBufferList = txContext->NBL;
    nbContext = (PMPUSB_TX_CONTEXT_NB)((PCHAR)txContext + sizeof(MPUSB_TX_CONTEXT_NBL));

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> MP_USBTxPacketEx: NBL 0x%p QNBL 0x%p\n", pAdapter->PortName,
        netBufferList, txContext)
    );

    numNBs = txContext->TotalNBs;

    for (nbIdx = 0; nbIdx < numNBs; nbIdx++)
    {
        netBuffer = nbContext[nbIdx].NB;

        if ((netBuffer == NULL) || (bAbort == TRUE))
        {
            break;
        }

        if (txContext->AbortFlag != 0)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_ERROR,
                ("<%s> MP_USBTxPacketEx: AbortFlag set for QNBL 0x%p\n",
                pAdapter->PortName, txContext)
            );
            nbContext[nbIdx].Completed = 1;
            bAbort = TRUE;
            txContext->TxFramesDropped += (txContext->TotalNBs - nbIdx - 1);
            continue;
        }

        // 1. Retrieve NDIS packet
        dataPkt = MPUSB_RetriveDataPacket(pAdapter, netBuffer, &dwPacketLength, NULL);
        if (dataPkt == NULL)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_ERROR,
                ("<%s> MP_USBTxPacketEx: failed to get pkt for QNBL 0x%p\n",
                pAdapter->PortName, txContext)
            );
            nbContext[nbIdx].Completed = 1;
            bAbort = TRUE;
            txContext->TxFramesDropped += (txContext->TotalNBs - nbIdx - 1);
            continue;
        }

        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> TxPacketEx: =>NBL 0x%p (%dB)\n", pAdapter->PortName, netBuffer, dwPacketLength)
        );

        nbContext[nbIdx].DataLength = dwPacketLength;

#ifdef MP_QCQOS_ENABLED
        if (pAdapter->QosEnabled == TRUE)
        {
            if (pAdapter->QosHdrFmt == 0x02)
            {
                sendBytes = dwPacketLength + sizeof(MPQOS_HDR8);
            }
            else
            {
                sendBytes = dwPacketLength + sizeof(MPQOS_HDR);
            }
        }
        else
#endif // MP_QCQOS_ENABLED
        {
            sendBytes = dwPacketLength;
        }

        usbBuffer = dataPkt;

#ifdef QC_IP_MODE
        if (pAdapter->IPModeEnabled == TRUE)
        {
            if ((QCMP_NDIS620_Ok == FALSE) || (pAdapter->NdisMediumType == NdisMedium802_3))
            {
                if (FALSE == MPUSB_PreparePacket(pAdapter, usbBuffer, (PULONG)&sendBytes))
                {
                    // TODO - revisit (this is local processing, not error!)
                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_WRITE,
                        MP_DBG_LEVEL_DETAIL,
                        ("<%s> IPO: _PreparePacket: local process %dB\n", pAdapter->PortName, sendBytes)
                    );
                    nbContext[nbIdx].Completed = 1;
                    txContext->TxFramesDropped += (txContext->TotalNBs - nbIdx - 1);
                    bAbort = TRUE;
                    continue;
                }
                else
                {
                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_WRITE,
                        MP_DBG_LEVEL_TRACE,
                        ("<%s> IPO: _PreparePacket: to USB %dB\n", pAdapter->PortName, sendBytes)
                    );
                }
            }
        }
#endif // QC_IP_MODE

        // Prepare IRP
        pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
        if (pIrp == NULL)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_ERROR,
                ("<%s> MP_USBTxPacketEx: IRP NO_MEM for QNBL 0x%p\n",
                pAdapter->PortName, txContext)
            );
            kicker = TRUE;
            if (usbBuffer != NULL)
            {
                ExFreePool(usbBuffer);
                usbBuffer = NULL;
            }

            bAbort = TRUE;
            txContext->TxFramesDropped += (txContext->TotalNBs - nbIdx - 1);
            continue;
        }  // if (pIrp == NULL)

        nbContext[nbIdx].Irp = pIrp; // for information purpose

        InterlockedIncrement(&(txContext->NumProcessed));

#ifdef MP_QCQOS_ENABLED
        if (pAdapter->QosEnabled == TRUE)
        {
            if (pAdapter->QosHdrFmt == 0x02)
            {
                qosHdr8 = (PMPQOS_HDR8)usbBuffer;
                qosHdr8->Version = 1;
                qosHdr8->Reserved = 0;
                if (IsQosPacket == TRUE)
                {
                    qosHdr8->FlowId = QosFlowId;
                }
                else
                {
                    qosHdr8->FlowId = 0;
                }
                qosHdr8->Rsvd[0] = 0;
                qosHdr8->Rsvd[1] = 0;
            }
            else
            {
                qosHdr = (PMPQOS_HDR)usbBuffer;
                qosHdr->Version = 1;
                qosHdr->Reserved = 0;
                if (IsQosPacket == TRUE)
                {
                    qosHdr->FlowId = QosFlowId;
                }
                else
                {
                    qosHdr->FlowId = 0;
                }
            }
            InterlockedIncrement(&pAdapter->QosPendingPackets);
        }
#endif // MP_QCQOS_ENABLED
        pIrp->MdlAddress = NULL;
        pIrp->AssociatedIrp.SystemBuffer = usbBuffer;
        nextstack = IoGetNextIrpStackLocation(pIrp);
        nextstack->MajorFunction = IRP_MJ_WRITE;
        nextstack->Parameters.Write.Length = sendBytes; // dwPacketLength;

        // Init IRP
        IoSetCompletionRoutine
        (
            pIrp,
            MP_TxIRPCompleteExStub,
            &(nbContext[nbIdx]),
            TRUE, TRUE, TRUE
        );

        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> MP_USBTxPacketEx: =>NBL 0x%p sending QNB[%d] 0x%p/0x%p\n", pAdapter->PortName,
            txContext->NBL, nbIdx, &(nbContext[nbIdx]), txContext)
        );

        returnAdapter = FindStackDeviceObject(pAdapter);
        if (returnAdapter != NULL)
        {
            Status = QCIoCallDriver(returnAdapter->USBDo, pIrp);
        }
        else
        {
            Status = STATUS_UNSUCCESSFUL;
            pIrp->IoStatus.Status = Status;
            MP_TxIRPCompleteEx(pAdapter->USBDo, pIrp, &(nbContext[nbIdx]), TRUE);
        }

        // NOTE: From this point on, the NBL might not be accessible because
        //       it could be completed by USB before this loop goes back to beginning

    }  // for

Tx_Exit:

    if (bAbort == TRUE)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> MP_USBTxPacketEx: NBL abort! 0x%p[%d] QNB 0x%p QNBL 0x%p\n", pAdapter->PortName,
            txContext->NBL, nbIdx, nbContext, txContext)
        );
        // to avoid race condition, complete NBL if txContext is still on TxBusyList
        // txContext is removed from TxBusyList in following scenarios:
        //    1) completion routine (at least 1 NB has been sent)
        //    2) in this function due to error condition (bAbort == TRUE)

        // set abort flag and complete the NBL if it's still in TxBusyList
        txContext->AbortFlag = 1;
        if (txContext->NumCompleted == txContext->NumProcessed) // all at home?
        {
            // complete NBL if txContext is still on TxBusyList
            MPUSB_TryToCompleteNBL
            (
                pAdapter,
                txContext,
                NDIS_STATUS_REQUEST_ABORTED,
                TRUE,
                TRUE
            );
        }
    }

    if (kicker == TRUE)
    {
        MPWork_ScheduleWorkItem(pAdapter);
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--MP_USBTxPacketEx (nBusyTx: %d/%dP/%dP)\n", pAdapter->PortName,
        pAdapter->nBusyTx, pAdapter->QosPendingPackets, pAdapter->MPPendingPackets)
    );
    return;
}  // MP_USBTxPacketEx

VOID MPUSB_TryToCompleteNBL
(
    PMP_ADAPTER           pAdapter,
    PMPUSB_TX_CONTEXT_NBL TxNbl,
    NDIS_STATUS           NdisStatus,
    BOOLEAN               ToAcquireSpinLock,
    BOOLEAN               ToUseBusyList
)
{
    PLIST_ENTRY headOfList, peekEntry;
    BOOLEAN     onBusyList = FALSE;
    PMPUSB_TX_CONTEXT_NBL nblContext = NULL;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> -->TryToCompleteNBL QNBL 0x%p (nBusyTx: %d/%dP/%dP)\n", pAdapter->PortName,
        TxNbl, pAdapter->nBusyTx, pAdapter->QosPendingPackets, pAdapter->MPPendingPackets)
    );

    if (ToAcquireSpinLock == TRUE)
    {
        NdisAcquireSpinLock(&pAdapter->TxLock);
    }

    if (ToUseBusyList == TRUE)
    {
        if (!IsListEmpty(&pAdapter->TxBusyList))
        {
            headOfList = &pAdapter->TxBusyList;
            peekEntry = headOfList->Flink;
            while (peekEntry != headOfList)
            {
                nblContext = CONTAINING_RECORD
                (
                    peekEntry,
                    MPUSB_TX_CONTEXT_NBL,
                    List
                );
                if (nblContext == TxNbl)
                {
                    onBusyList = TRUE;
                    break;
                }
                peekEntry = peekEntry->Flink;
            }
        }

        if (onBusyList == TRUE)
        {

            // for reference: NDIS5.1: does this in MPWork_IndicateCompleteTx
            InterlockedDecrement(&(pAdapter->nBusyTx));

            RemoveEntryList(&(nblContext->List));  // removed from TxBusyList

            NdisReleaseSpinLock(&pAdapter->TxLock);
            MPUSB_CompleteNetBufferList
            (
                pAdapter,
                nblContext->NBL,
                NdisStatus,
                0,
                nblContext
            );
            if (ToAcquireSpinLock == FALSE)
            {
                NdisAcquireSpinLock(&pAdapter->TxLock);
            }
        }
    }
    else
    {
        NdisReleaseSpinLock(&pAdapter->TxLock);
        MPUSB_CompleteNetBufferList
        (
            pAdapter,
            TxNbl->NBL,
            NdisStatus,
            0,
            TxNbl
        );
        if (ToAcquireSpinLock == FALSE)
        {
            NdisAcquireSpinLock(&pAdapter->TxLock);
        }
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--TryToCompleteNBL QNBL 0x%p (nBusyTx: %d/%dP/%dP): %d\n",
        pAdapter->PortName, TxNbl, pAdapter->nBusyTx,
        pAdapter->QosPendingPackets, pAdapter->MPPendingPackets, onBusyList)
    );
}  // MPUSB_TryToCompleteNBL

PUCHAR MPUSB_RetriveDataPacket
(
    PMP_ADAPTER pAdapter,
    PNET_BUFFER NetBuffer,
    PULONG      Length,
    PVOID       PktBuffer
)
{
    ULONG  mdlLen, bytesCopied = 0, offset, pktLen, bufLen;
    PUCHAR pktBuf, pSrc, pDest;
    PMDL   mdl;
    int    i = 0;
    PUCHAR pDataPkt;

    mdl = NET_BUFFER_FIRST_MDL(NetBuffer);
    offset = NET_BUFFER_DATA_OFFSET(NetBuffer);
    bufLen = pktLen = NET_BUFFER_DATA_LENGTH(NetBuffer);

    *Length = 0;

    if (pktLen == 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPUSB_RetriveDataPacket: empty pkt\n", pAdapter->PortName)
        );
        return NULL;
    }

    if (mdl == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPUSB_RetriveDataPacket: NULL MDL\n", pAdapter->PortName)
        );
        return NULL;
    }

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            bufLen += sizeof(MPQOS_HDR8);
        }
        else
        {
            bufLen += sizeof(MPQOS_HDR);
        }
    }
#endif // MP_QCQOS_ENABLED

    // allocate destination buffer. TODO - pre-alloc
    if (PktBuffer == NULL)
    {
        pktBuf = ExAllocatePoolWithTag(NonPagedPool, bufLen, (ULONG)'106N');
        if (pktBuf == NULL)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_ERROR,
                ("<%s> MPUSB_RetriveDataPacket: NO_MEM\n", pAdapter->PortName)
            );
            return NULL;
        }
    }
    else
    {
        pktBuf = (PUCHAR)PktBuffer;
    }

    pDest = pktBuf;

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            pDest += sizeof(MPQOS_HDR8);
        }
        else
        {
            pDest += sizeof(MPQOS_HDR);
        }
    }
#endif // MP_QCQOS_ENABLED

    pDataPkt = pDest;

    while (mdl != NULL)
    {
        ++i;

        NdisQueryMdl(mdl, &pSrc, &mdlLen, NormalPagePriority);
        if (pSrc == 0)
        {
            bytesCopied = 0;
            break;
        }
        if (mdlLen > offset)
        {
            pSrc += offset;
            mdlLen -= offset;

            // this should not happen
            if (mdlLen > pktLen)
            {
                mdlLen = pktLen;
            }
            pktLen -= mdlLen;
            NdisMoveMemory(pDest, pSrc, mdlLen);
            bytesCopied += mdlLen;
            pDest += mdlLen;
            offset = 0;  // offset skipped
        }
        else
        {
            offset -= mdlLen;
        }
        mdl = mdl->Next;  // go to next MDL
    }

    if ((bytesCopied > 0) && (bytesCopied < pktLen))
    {
        NdisZeroMemory(pDest, pktLen - bytesCopied);
    }

    if (bytesCopied == 0)
    {
        if (pDest != NULL)
        {
            if (PktBuffer == NULL)
            {
                ExFreePool(pktBuf);
                pktBuf = NULL;
            }
        }
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPUSB_RetriveDataPacket: no data\n", pAdapter->PortName)
        );
    }
    else
    {
#ifdef MP_QCQOS_ENABLED
        if (pAdapter->QosEnabled == TRUE)
        {
            if (pAdapter->QosHdrFmt == 0x02)
            {
                *Length = (bytesCopied + sizeof(MPQOS_HDR8));
            }
            else
            {
                *Length = (bytesCopied + sizeof(MPQOS_HDR));
            }
        }
        else
#endif // MP_QCQOS_ENABLED
        {
            *Length = bytesCopied;
        }
    }

    if (bytesCopied > 6)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> MPUSB_RetriveDataPacket: Retrieved %d/%d/%dB UL-IP seq num (0x%02X, 0x%02X)\n", pAdapter->PortName,
            *Length, bytesCopied, bufLen, pDataPkt[4], pDataPkt[5])
        );
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> MPUSB_RetriveDataPacket: Retrieved %d/%d/%dB\n", pAdapter->PortName,
            *Length, bytesCopied, bufLen)
        );
    }

    return pktBuf;

}  // MPUSB_RetriveDataPacket

ULONG MPUSB_QueryDataPacket
(
    PMP_ADAPTER pAdapter,
    PNET_BUFFER NetBuffer,
    PULONG      Length
)
{
    ULONG  mdlLen, bytesCopied = 0, offset, pktLen, bufLen;
    PUCHAR pktBuf, pSrc;
    PMDL   mdl;
    int    i = 0;

    mdl = NET_BUFFER_FIRST_MDL(NetBuffer);
    offset = NET_BUFFER_DATA_OFFSET(NetBuffer);
    bufLen = pktLen = NET_BUFFER_DATA_LENGTH(NetBuffer);

    *Length = 0;

    if (pktLen == 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPUSB_QueryDataPacket: empty pkt\n", pAdapter->PortName)
        );
        return 0;
    }

    if (mdl == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPUSB_QueryDataPacket: NULL MDL\n", pAdapter->PortName)
        );
        return 0;
    }

    while (mdl != NULL)
    {
        ++i;

        NdisQueryMdl(mdl, &pSrc, &mdlLen, NormalPagePriority);
        if (pSrc == 0)
        {
            bytesCopied = 0;
            break;
        }
        if (mdlLen > offset)
        {
            pSrc += offset;
            mdlLen -= offset;

            // this should not happen
            if (mdlLen > pktLen)
            {
                mdlLen = pktLen;
            }
            pktLen -= mdlLen;
            bytesCopied += mdlLen;
            offset = 0;  // offset skipped
        }
        else
        {
            offset -= mdlLen;
        }
        mdl = mdl->Next;  // go to next MDL
    }

    if (bytesCopied == 0)
    {
    }
    else
    {
#ifdef MP_QCQOS_ENABLED
        if (pAdapter->QosEnabled == TRUE)
        {
            if (pAdapter->QosHdrFmt == 0x02)
            {
                *Length = (bytesCopied + sizeof(MPQOS_HDR8));
            }
            else
            {
                *Length = (bytesCopied + sizeof(MPQOS_HDR));
            }
        }
        else
#endif // MP_QCQOS_ENABLED
        {
            *Length = bytesCopied;
        }
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> MPUSB_QueryDataPacket: Retrieved %d/%d/%dB\n", pAdapter->PortName,
        *Length, bytesCopied, bufLen)
    );

    return *Length;

}  // MPUSB_RetriveDataPacket

VOID MPUSB_CompleteNetBufferList
(
    PMP_ADAPTER      pAdapter,
    PNET_BUFFER_LIST NetBufferList,
    NDIS_STATUS      NdisStatus,
    ULONG            SendCompleteFlags,
    PVOID            NBLContext
)
{
    PMPUSB_TX_CONTEXT_NBL txContext = NULL;
    int                   bytesSent = 0;

    // TODO - statistics should be done within spinlock
    if (NBLContext != NULL)
    {
        txContext = (PMPUSB_TX_CONTEXT_NBL)NBLContext;

        if (NdisStatus == NDIS_STATUS_SUCCESS)
        {
            ++(pAdapter->GoodTransmits);
            pAdapter->TxBytesGood += txContext->TxBytesSucceeded;
            pAdapter->TxBytesBad += txContext->TxBytesFailed;
            bytesSent = txContext->TxBytesSucceeded;
        }
        else
        {
            ++(pAdapter->BadTransmits);
            pAdapter->TxFramesFailed += txContext->TxFramesFailed;
            pAdapter->TxFramesDropped += txContext->TxFramesDropped;
            // this may not be precise
            pAdapter->TxBytesBad += txContext->TxBytesFailed;
        }
    }
    else
    {
        // failure, best effort for statistics
        ++(pAdapter->BadTransmits);

        // we do not have information about bytes
        // ...
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> TNBL (C 0x%p) ST 0x%x (%dB)\n", pAdapter->PortName,
        NetBufferList, NdisStatus, bytesSent)
    );

    NET_BUFFER_LIST_STATUS(NetBufferList) = NdisStatus;
    NdisMSendNetBufferListsComplete
    (
        pAdapter->AdapterHandle,
        NetBufferList,
        SendCompleteFlags
    );

    if (NBLContext != NULL)
    {
        ExFreePool(NBLContext);
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> -->MPUSB_CompleteNetBufferList (nBusyTx: %d/%dP)\n",
        pAdapter->PortName, pAdapter->nBusyTx, pAdapter->QosPendingPackets)
    );
}  // MPUSB_CompleteNetBufferList

NTSTATUS MP_TxIRPCompleteEx
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext,
    BOOLEAN        acquire
)
{
    PMPUSB_TX_CONTEXT_NB nbContext, nbx;
    PMPUSB_TX_CONTEXT_NBL nblContext;
    PMP_ADAPTER  pAdapter;
    NDIS_STATUS  status;
    int          i;
    PIO_STACK_LOCATION nextstack;
    BOOLEAN      allReturned = TRUE;
    BOOLEAN      furtherCompletionLocking = FALSE; // MPUSB_TryToCompleteNBL is called within TxLock in this function

    nbContext = (PMPUSB_TX_CONTEXT_NB)pContext;
    nblContext = (PMPUSB_TX_CONTEXT_NBL)nbContext->NBL; // on TxBusyList
    pAdapter = nblContext->Adapter;

    if (acquire == TRUE)
    {
        NdisAcquireSpinLock(&pAdapter->TxLock);
    }

    if (pIrp != NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> -->MP_TxIRPCompleteEx: IRP 0x%p(0x%x) NBL 0x%p/0x%p(%dNBs) %dB\n", pAdapter->PortName, pIrp,
            pIrp->IoStatus.Status, nblContext->NBL, nblContext, nblContext->TotalNBs, nbContext->DataLength)
        );

        if (pIrp->IoStatus.Status == STATUS_SUCCESS)
        {
            nblContext->TxBytesSucceeded += nbContext->DataLength;
        }
        else
        {
            ++(nblContext->TxFramesFailed);
            nblContext->TxBytesFailed += nbContext->DataLength;
        }
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_TRACE,
            ("<%s> -->MP_TxIRPCompleteEx: IRP 0x%p NBL 0x%p/0x%p(%dNBs) %dB\n", pAdapter->PortName, pIrp,
            nblContext->NBL, nblContext, nblContext->TotalNBs, nbContext->DataLength)
        );
        nblContext->TxBytesSucceeded += nbContext->DataLength;
    }

    if (pIrp == NULL)
    {
        InterlockedDecrement(&(pAdapter->nBusyTx));
    }

    InterlockedIncrement(&(nblContext->NumCompleted));
    nbContext->Completed = 1;

    if (nblContext->AbortFlag != 0)
    {
        if (nblContext->NumCompleted == nblContext->NumProcessed)
        {
            if (pIrp == NULL)
            {
                MPUSB_TryToCompleteNBL
                (
                    pAdapter,
                    nblContext,
                    NDIS_STATUS_REQUEST_ABORTED,
                    furtherCompletionLocking,
                    FALSE
                );
            }
            else
            {
                MPUSB_TryToCompleteNBL
                (
                    pAdapter,
                    nblContext,
                    NDIS_STATUS_REQUEST_ABORTED,
                    furtherCompletionLocking,
                    TRUE
                );
            }
        }
        else
        {
            // do nothing, wait for all processed to return
        }
    }
    else
    {
        nbx = (PMPUSB_TX_CONTEXT_NB)((PCHAR)nblContext + sizeof(MPUSB_TX_CONTEXT_NBL));
        for (i = 0; i < nblContext->TotalNBs; i++)
        {
            if (nbx[i].Completed == 0)
            {
                allReturned = FALSE;
                break;
            }
        } // for

        if ((i > 0) && (allReturned == TRUE))
        {
            if (pIrp == NULL)
            {
                MPUSB_TryToCompleteNBL
                (
                    pAdapter,
                    nblContext,
                    nblContext->NdisStatus,
                    furtherCompletionLocking,
                    FALSE
                );
            }
            else
            {
                MPUSB_TryToCompleteNBL
                (
                    pAdapter,
                    nblContext,
                    NDIS_STATUS_SUCCESS,
                    furtherCompletionLocking,
                    TRUE
                );
            }
        }
    }

    if (acquire == TRUE)
        NdisReleaseSpinLock(&pAdapter->TxLock);


    // do we need to involve the CompletionList???
    // For NDIS5.1:
    //    - Move packet from BusyList to CompleteList // NDIS6 ???
    //    - Free IO buffer in the IRP
    //    - Schedle work item
    //    - Adjust statistics  // NDIS6 --> just did
    //    - Free the IRP

    if (pIrp != NULL)
    {
        nextstack = IoGetNextIrpStackLocation(pIrp);

        // Free the buffer we copied the packet data to
        NdisFreeMemory(pIrp->AssociatedIrp.SystemBuffer, nextstack->Parameters.Write.Length, 0);
    }
#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        InterlockedDecrement(&(pAdapter->QosPendingPackets));

        if (pAdapter->QosPendingPackets < pAdapter->MPPendingPackets)
        {
            KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT, FALSE);
        }
    }
#endif // MP_QCQOS_ENABLED

    if (pIrp != NULL)
    {
        IoFreeIrp(pIrp);
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_TxIRPCompleteEx (nBusyTx: %d/%dP)\n",
        pAdapter->PortName, pAdapter->nBusyTx, pAdapter->QosPendingPackets)
    );

    return STATUS_MORE_PROCESSING_REQUIRED;
}  // MP_TxIRPCompleteEx

VOID MPUSB_SetIPType
(
    PUCHAR Packet,
    ULONG  Length,
    PVOID  NetBufferList
)
{
    UCHAR ipVer = 4;
    PNET_BUFFER_LIST nbl = (PNET_BUFFER_LIST)NetBufferList;

    if (Length > 0)
    {
        ipVer = (*Packet & 0xF0) >> 4;
    }

    if (ipVer == 4)
    {
        NdisSetNblFlag(nbl, NDIS_NBL_FLAGS_IS_IPV4);
        NET_BUFFER_LIST_INFO(nbl, NetBufferListFrameType) = (PVOID)ntohs(ETH_TYPE_IPV4);
    }
    else if (ipVer == 6)
    {
        NdisSetNblFlag(nbl, NDIS_NBL_FLAGS_IS_IPV6);
        NET_BUFFER_LIST_INFO(nbl, NetBufferListFrameType) = (PVOID)ntohs(ETH_TYPE_IPV6);
    }
}  // MPUSB_SetIPType

INT MPUSB_GetRxStreamIndex(PMP_ADAPTER pAdapter, PUCHAR Packet, ULONG  Length)
{
    INT           idx = 0;  // default
    UCHAR         ipVer = 0;
    REF_IPV4_HDR  ipv4Hdr;
    REF_IPV6_HDR  ipv6Hdr;

    if (Length > 20)  // min hdr size
    {
        ipVer = (*Packet & 0xF0) >> 4;
    }
    else
    {
        return idx;
    }

    if (ipVer == 4)
    {
        MPQOS_GetIPHeaderV4(pAdapter, Packet, &ipv4Hdr);
        idx = ipv4Hdr.DestPort % pAdapter->RxStreams;
    }
    if (ipVer == 6)
    {
        QCQOS_GetIPHeaderV6(pAdapter, Packet, &ipv6Hdr);
        idx = ipv6Hdr.DestPort % pAdapter->RxStreams;
    }

    pAdapter->ActiveRxSlot[idx] = 1; // for debugging

    return idx;

}  // MPUSB_GetRxStreamIndex

#endif // NDIS60_MINIPORT

#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT)

NDIS_STATUS MPUSB_InitializeTLP(PMP_ADAPTER pAdapter)
{
    int i;
    int numItems = MP_NUM_UL_TLP_ITEMS_DEFAULT;

    // use more buffers if there's no data aggregation
    if ((pAdapter->MPDisableQoS >= 3) &&
        (pAdapter->TLPEnabled == FALSE) && (pAdapter->MBIMULEnabled == FALSE) &&
        (pAdapter->QMAPEnabledV1 == FALSE)
        && (pAdapter->QMAPEnabledV4 == FALSE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
        && (pAdapter->QMAPEnabledV2 == FALSE)
        && (pAdapter->QMAPEnabledV3 == FALSE)
#endif
#endif
        )
    {
        numItems = MP_NUM_UL_TLP_ITEMS;
    }

    if (pAdapter->MaxTLPPackets == 0x01)
    {
        numItems = MP_NUM_UL_TLP_ITEMS;
    }

    // Over write, always set the TLP Buffers to 64
    numItems = MP_NUM_UL_TLP_ITEMS;

    // however if number of buffers is set in the registry, we use the configured value
    if (pAdapter->NumTLPBuffersConfigured == TRUE)
    {
        numItems = pAdapter->NumTLPBuffers;
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_CRITICAL,
        ("<%s> [TLP] InitializeTLP: %d items\n", pAdapter->PortName, numItems)
    );

    NdisAcquireSpinLock(&pAdapter->TxLock);

    if (!IsListEmpty(&pAdapter->UplinkFreeBufferQueue))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_CRITICAL,
            ("<%s> [TLP] InitializeTLP: already done, no action %d\n", pAdapter->PortName, numItems)
        );
        NdisReleaseSpinLock(&pAdapter->TxLock);
        return NDIS_STATUS_SUCCESS;
    }

    for (i = 0; i < numItems; i++)
    {
        InitializeListHead(&(pAdapter->UlTlpItem[i].PacketList));

        pAdapter->UlTlpItem[i].Buffer = ExAllocatePoolWithTag
        (
            NonPagedPool,
            (pAdapter->UplinkTLPSize + 1600),
            (ULONG)'1001'
        );
        if (pAdapter->UlTlpItem[i].Buffer != NULL)
        {
            pAdapter->UlTlpItem[i].Irp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
            if (pAdapter->UlTlpItem[i].Irp == NULL)
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_WRITE,
                    MP_DBG_LEVEL_ERROR,
                    ("<%s> [TLP] InitializeTLP: NO_MEM IRP[%d]\n", pAdapter->PortName, i)
                );
                ExFreePool(pAdapter->UlTlpItem[i].Buffer);
                pAdapter->UlTlpItem[i].Buffer = NULL;
                break;
            }

            pAdapter->UlTlpItem[i].CurrentPtr = pAdapter->UlTlpItem[i].Buffer;
            pAdapter->UlTlpItem[i].DataLength = 0;
            pAdapter->UlTlpItem[i].RemainingCapacity = pAdapter->UplinkTLPSize;
            pAdapter->UlTlpItem[i].AggregationCount = 0;
            pAdapter->UlTlpItem[i].Index = i;

            RtlZeroMemory(&(pAdapter->UlTlpItem[i].NdpHeader), sizeof(NDP_16BIT_HEADER) + sizeof(pAdapter->UlTlpItem[i].DataGrams));
            pAdapter->UlTlpItem[i].Adapter = pAdapter;
            InsertTailList
            (
                &pAdapter->UplinkFreeBufferQueue,
                &pAdapter->UlTlpItem[i].List
            );
        }
        else
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> [TLP] InitializeTLP: NO_MEM [%d]\n", pAdapter->PortName, i)
            );
        }
    }

    NdisReleaseSpinLock(&pAdapter->TxLock);

    if (IsListEmpty(&pAdapter->UplinkFreeBufferQueue))
    {
        return NDIS_STATUS_FAILURE;
    }

    return NDIS_STATUS_SUCCESS;

} // MPUSB_InitializeTLP

VOID MPUSB_PurgeTLPQueues(PMP_ADAPTER pAdapter, BOOLEAN FreeMemory)
{
    PLIST_ENTRY   headOfList, peekEntry, nextEntry;
    PTLP_BUF_ITEM tlpItem;
    PNDIS_PACKET  pNdisPacket;
    int i = 0, nPkt = 0;

    /* Before cleanup check for TLP Tx empty */
    while (TRUE)
    {
        NdisAcquireSpinLock(&pAdapter->TxLock);
        if (pAdapter->TxTLPPendinginUSB != 0)
        {
            NdisReleaseSpinLock(&pAdapter->TxLock);
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> [TLP] PurgeTLPQueues: TxTLPPendinginUSB not empty %d\n", pAdapter->PortName, pAdapter->TxTLPPendinginUSB)
            );

            NdisMSleep(2000);
            continue;
        }
        else
        {
            NdisReleaseSpinLock(&pAdapter->TxLock);
            break;
        }
    }

    NdisAcquireSpinLock(&pAdapter->TxLock);

    if (!IsListEmpty(&pAdapter->UplinkFreeBufferQueue))
    {
        headOfList = &pAdapter->UplinkFreeBufferQueue;
        peekEntry = headOfList->Flink;

        while (peekEntry != headOfList)
        {
            i++;
            tlpItem = CONTAINING_RECORD(peekEntry, TLP_BUF_ITEM, List);
            tlpItem->DataLength = 0;
            if (FreeMemory == TRUE)
            {
                if (tlpItem->Buffer != NULL)
                {
                    ExFreePool(tlpItem->Buffer);
                }
                if (tlpItem->Irp != NULL)
                {
                    IoFreeIrp(tlpItem->Irp);
                    tlpItem->Irp = NULL;
                }
                tlpItem->Buffer = NULL;
                tlpItem->CurrentPtr = NULL;
                tlpItem->RemainingCapacity = 0;
                tlpItem->AggregationCount = 0;
                nextEntry = peekEntry->Flink;
                RemoveEntryList(peekEntry);
                peekEntry = nextEntry;
            }
            else
            {
                tlpItem->CurrentPtr = tlpItem->Buffer;
                tlpItem->RemainingCapacity = pAdapter->UplinkTLPSize;
                tlpItem->AggregationCount = 0;
                peekEntry = peekEntry->Flink;
            }
            while (!IsListEmpty(&tlpItem->PacketList))
            {
                headOfList = RemoveHeadList(&tlpItem->PacketList);
                pNdisPacket = ListItemToPacket(headOfList);
                NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
                InsertTailList(&pAdapter->TxCompleteList, headOfList);
                nPkt++;
            }
        } // while
    } // if

    // Check TxBusyList
    while (!IsListEmpty(&pAdapter->TxBusyList))
    {
        PNDIS_PACKET pNdisPacket;
        PLIST_ENTRY  pList;

        pList = RemoveHeadList(&pAdapter->TxBusyList);
        pNdisPacket = ListItemToPacket(pList);
        NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
        InsertTailList(&pAdapter->TxCompleteList, pList);
    }

    // Check TxPendingList
    while (!IsListEmpty(&pAdapter->TxPendingList))
    {
        PNDIS_PACKET pNdisPacket;
        PLIST_ENTRY  pList;

        pList = RemoveHeadList(&pAdapter->TxPendingList);
        pNdisPacket = ListItemToPacket(pList);
        NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
        InsertTailList(&pAdapter->TxCompleteList, pList);
    }

    NdisReleaseSpinLock(&pAdapter->TxLock);

    if (nPkt != 0)
    {
        MPWork_IndicateCompleteTx(pAdapter);
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> [TLP] PurgeTLPQueues: Purged %d/%d\n", pAdapter->PortName, i, nPkt)
    );
}  // MPUSB_PurgeTLPQueues

INT MPUSB_AggregationAvailable(PMP_ADAPTER pAdapter, BOOLEAN UseSpinLock, ULONG SizeOfPacket)
{
    INT         result = TLP_AGG_NO_BUF;
    PLIST_ENTRY headOfList, peekEntry;
    PTLP_BUF_ITEM tlpItem;

    if (UseSpinLock == TRUE)
    {
        NdisAcquireSpinLock(&pAdapter->TxLock);
    }

    if ((pAdapter->TLPEnabled == FALSE) && (pAdapter->MBIMULEnabled == FALSE) &&
        (pAdapter->QMAPEnabledV1 == FALSE)
        && (pAdapter->MPQuickTx != 0)
        && (pAdapter->QMAPEnabledV4 == FALSE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
        && (pAdapter->QMAPEnabledV2 == FALSE)
        && (pAdapter->QMAPEnabledV3 == FALSE)
#endif
#endif
        )
    {
        if (IsListEmpty(&pAdapter->UplinkFreeBufferQueue))
        {
            if (UseSpinLock == TRUE)
            {
                NdisReleaseSpinLock(&pAdapter->TxLock);
            }
            return TLP_AGG_NO_BUF;
        }
        if (UseSpinLock == TRUE)
        {
            NdisReleaseSpinLock(&pAdapter->TxLock);
        }
        return TLP_AGG_MORE; // trigger pkt to be de-queued
    }

    if (IsListEmpty(&pAdapter->UplinkFreeBufferQueue))
    {
        result = TLP_AGG_NO_BUF;
    }
    else
    {
        headOfList = &pAdapter->UplinkFreeBufferQueue;
        peekEntry = headOfList->Flink;
        tlpItem = CONTAINING_RECORD(peekEntry, TLP_BUF_ITEM, List);
        // need to be changed to the actual size...


#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
        if ((pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE))
        {
            if ((pAdapter->MaxTLPPackets > 0) && (tlpItem->AggregationCount >= pAdapter->MaxTLPPackets))
            {
                result = TLP_AGG_SEND;
            }
            else if (tlpItem->RemainingCapacity <= (SizeOfPacket + 8 + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM)))
            {
                result = TLP_AGG_SEND;
            }
            else
            {
                result = TLP_AGG_MORE;
            }
        }
        else
#endif
#endif
            if (pAdapter->QMAPEnabledV4 == TRUE)
            {
                if ((pAdapter->MaxTLPPackets > 0) && (tlpItem->AggregationCount >= pAdapter->MaxTLPPackets))
                {
                    result = TLP_AGG_SEND;
                }
                else if (tlpItem->RemainingCapacity <= (SizeOfPacket + 8 + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM)))
                {
                    result = TLP_AGG_SEND;
                }
                else
                {
                    result = TLP_AGG_MORE;
                }
            }
        if (pAdapter->QMAPEnabledV1 == TRUE)
        {
            if ((pAdapter->MaxTLPPackets > 0) && (tlpItem->AggregationCount >= pAdapter->MaxTLPPackets))
            {
                result = TLP_AGG_SEND;
            }
            else if (tlpItem->RemainingCapacity <= (SizeOfPacket + 8 + sizeof(QMAP_STRUCT)))
            {
                result = TLP_AGG_SEND;
            }
            else
            {
                result = TLP_AGG_MORE;
            }
        }
        else if (pAdapter->MBIMULEnabled == TRUE)
        {
            if ((pAdapter->MaxTLPPackets > 0) && (tlpItem->AggregationCount >= pAdapter->MaxTLPPackets))
            {
                result = TLP_AGG_SEND;
            }
            else if (tlpItem->RemainingCapacity <= (SizeOfPacket +
                     sizeof(NDP_16BIT_HEADER) +
                     sizeof(DATAGRAM_STRUCT) * (tlpItem->AggregationCount + 4) + 8))
            {
                result = TLP_AGG_SEND;
            }
            else
            {
                result = TLP_AGG_MORE;
            }
        }
        else
        {
            if ((pAdapter->MaxTLPPackets > 0) && (tlpItem->AggregationCount >= pAdapter->MaxTLPPackets))
            {
                result = TLP_AGG_SEND;
            }
            else if (tlpItem->RemainingCapacity <= SizeOfPacket + 8)
            {
                result = TLP_AGG_SEND;
            }
            else
            {
                result = TLP_AGG_MORE;
            }
        }
    }

    if (UseSpinLock == TRUE)
    {
        NdisReleaseSpinLock(&pAdapter->TxLock);
    }

    return result;

}  // MPUSB_AggregationAvailable

INT MPUSB_AggregatePacket
(
    PMP_ADAPTER pAdapter,
    PUCHAR      Packet,
    USHORT      PacketLength,
    PLIST_ENTRY PacketEntry,
    BOOLEAN     UseSpinLock
)
{
    PLIST_ENTRY headOfList, peekEntry;
    PTLP_BUF_ITEM tlpItem;
    int result = 0;

    if (UseSpinLock == TRUE)
    {
        NdisAcquireSpinLock(&pAdapter->TxLock);
    }

    if (IsListEmpty(&pAdapter->UplinkFreeBufferQueue))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> [TLP] AggregatePacket: error-empty queue\n", pAdapter->PortName)
        );
        result = TLP_AGG_NO_BUF;
    }
    else
    {
        headOfList = &pAdapter->UplinkFreeBufferQueue;
        peekEntry = headOfList->Flink;
        tlpItem = CONTAINING_RECORD(peekEntry, TLP_BUF_ITEM, List);
        if (tlpItem->RemainingCapacity < MP_MAX_PKT_SZIE)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> [TLP] AggregatePacket: error-empty queue\n", pAdapter->PortName)
            );
            result = TLP_AGG_SMALL_BUF;
        }
        else
        {
            RemoveEntryList(&tlpItem->List);
            RtlCopyMemory
            (
                tlpItem->CurrentPtr,
                Packet,
                PacketLength
            );
            tlpItem->CurrentPtr += PacketLength;
            tlpItem->RemainingCapacity -= PacketLength;

            InsertTailList(&tlpItem->PacketList, PacketEntry);

            if (tlpItem->RemainingCapacity >= MP_MAX_PKT_SZIE)
            {
                result = TLP_AGG_MORE;
            }
            else
            {
                result = TLP_AGG_SEND;
            }
        }
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_ERROR,
        ("<%s> [TLP] AggregatePacket: %d\n", pAdapter->PortName, result)
    );

    if (UseSpinLock == TRUE)
    {
        NdisReleaseSpinLock(&pAdapter->TxLock);
    }

    return result;

}  // MPUSB_AggregatePacket

NTSTATUS MPUSB_TLPTxIRPComplete
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    PTLP_BUF_ITEM tlpItem = (PTLP_BUF_ITEM)pContext;
    PMP_ADAPTER   pAdapter;
    NDIS_STATUS   status;
    PLIST_ENTRY   pList;
    PNDIS_PACKET  pNdisPacket;
    INT           nPkt = 0;

    pAdapter = (PMP_ADAPTER)tlpItem->Adapter;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> -->TLPTxIRPComplete [%d(%d)] 0x%p (0x%x)\n", pAdapter->PortName,
        tlpItem->Index, tlpItem->AggregationCount, pIrp, pIrp->IoStatus.Status)
    );

    NdisAcquireSpinLock(&pAdapter->TxLock);

    // tlpItem->PacketList is empty if MPQuickTx is TRUE
    while (!IsListEmpty(&tlpItem->PacketList))
    {
        pList = RemoveHeadList(&tlpItem->PacketList);
        pNdisPacket = ListItemToPacket(pList);
        if (NT_SUCCESS(pIrp->IoStatus.Status))
        {
            NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_SUCCESS);
        }
        else
        {
            NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
        }
        InsertTailList(&pAdapter->TxCompleteList, pList);
        nPkt++;
        if (pAdapter->QosEnabled == TRUE)
        {
            pAdapter->QosPendingPackets--;
        }
    }

    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->MPQuickTx != 0)
        {
            pAdapter->QosPendingPackets -= tlpItem->AggregationCount;
            // use the following instead of an assignment statement for error
            // verification purpose (to make sure if tlpItem->PacketList
            // is actually empty
            nPkt += tlpItem->AggregationCount;
        }
    }

    // recycle TLP item
    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> [TLP] _TLPTxIRPComplete: restore item [%d]\n", pAdapter->PortName,
        tlpItem->Index)
    );
    tlpItem->CurrentPtr = tlpItem->Buffer;
    tlpItem->DataLength = 0;
    tlpItem->RemainingCapacity = pAdapter->UplinkTLPSize;
    tlpItem->AggregationCount = 0;
    RtlZeroMemory(&(tlpItem->NdpHeader), sizeof(NDP_16BIT_HEADER) + sizeof(tlpItem->DataGrams));

    InsertTailList(&pAdapter->UplinkFreeBufferQueue, &tlpItem->List);

    InterlockedDecrement(&(pAdapter->TxTLPPendinginUSB));

    NdisReleaseSpinLock(&pAdapter->TxLock);

    if (nPkt != 0)
    {
        MPWork_ScheduleWorkItem(pAdapter);
    }

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
#ifdef MPQOS_PKT_CONTROL
        if (pAdapter->QosPendingPackets < pAdapter->MPPendingPackets)
        {
            KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT, FALSE);
        }
#else
        KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT, FALSE);
#endif // MPQOS_PKT_CONTROL
        }
    else
#endif // MP_QCQOS_ENABLED
    {
        // buffer available, kick TX
        KeSetEvent(&pAdapter->MPThreadTxEvent, IO_NO_INCREMENT, FALSE);
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_TLPTxIRPComplete (nBusyTx: %d/%dP) done %d\n",
        pAdapter->PortName, pAdapter->nBusyTx, pAdapter->QosPendingPackets, nPkt)
    );
    return STATUS_MORE_PROCESSING_REQUIRED;
    }  // MPUSB_TLPTxIRPComplete


VOID TransmitTimerDpc
(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2
)
{
    PMP_ADAPTER pAdapter;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    pAdapter = (PMP_ADAPTER)DeferredContext;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
        ("<%s> ---> TransmitTimerDpc\n", pAdapter->PortName)
    );

    KeSetEvent(&pAdapter->MPThreadTxTimerEvent, IO_NO_INCREMENT, FALSE);

    QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 777);
}

VOID SetTransmitTimer(PMP_ADAPTER pAdapter)
{
    NTSTATUS nts = STATUS_SUCCESS;
    LARGE_INTEGER          dueTime;
    dueTime.QuadPart = (LONGLONG)(-10000) * pAdapter->TransmitTimerValue;

    nts = IoAcquireRemoveLock(pAdapter->pMPRmLock, NULL);
    if (nts != STATUS_SUCCESS)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> ---> SetTransmitTimer RemoveLock Acquire failed\n", pAdapter->PortName)
        );
        return;
    }

    if ((KeReadStateTimer(&pAdapter->TransmitTimer) == TRUE) ||
        (pAdapter->TransmitTimerLaunched == FALSE))
    {

        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> ---> SetTransmitTimer Timer set value %d\n", pAdapter->PortName, pAdapter->TransmitTimerValue)
        );

        pAdapter->TransmitTimerLaunched = TRUE;
        QcStatsIncrement(pAdapter, MP_CNT_TIMER, 777);

        // Set the timer resolution
        // ExSetTimerResolution(10000, TRUE);

        KeSetTimer(&pAdapter->TransmitTimer, dueTime, &pAdapter->TransmitTimerDPC);
    }
    else
    {
        IoReleaseRemoveLock(pAdapter->pMPRmLock, NULL);
    }
}

VOID CancelTransmitTimer(PMP_ADAPTER pAdapter)
{
    /* Cancel the TransmitTimer DPC here */

    if (KeCancelTimer(&pAdapter->TransmitTimer) == TRUE)
    {

        // Reset the resolution
        // ExSetTimerResolution(0, FALSE);

        // timer cancelled, release remove lock
        QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_CNT_TIMER, 777);

        // Re-initialize to non-signal state
        KeInitializeTimer(&pAdapter->TransmitTimer);
        pAdapter->TransmitTimerLaunched = FALSE;

        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> TransmitTimer Cancel Successful\n", pAdapter->PortName)
        );
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> TransmitTimer is running or already stopped\n", pAdapter->PortName)
        );
    }

}

VOID MPUSB_TLPTxPacket
(
    PMP_ADAPTER  pAdapter,
    ULONG        QosFlowId,
    BOOLEAN      IsQosPacket,
    PLIST_ENTRY  pList
)
{
    PIRP                pIrp;
    PIO_STACK_LOCATION  nextstack;
    NDIS_STATUS         Status;
    PNDIS_BUFFER pCurrentNdisBuffer;
    UINT         dwTotalBufferCount;
    UINT         dwPacketLength;
    PUCHAR       pCurrentBuffer;
    ULONG        dwCurrentBufferLength;
    PUCHAR       usbBuffer = NULL;
#ifdef MP_QCQOS_ENABLED
    PMPQOS_HDR  qosHdr;
    PMPQOS_HDR8  qosHdr8;
#endif // MP_QCQOS_ENABLED
    long        sendBytes;
    ULONG       copyOk;
    BOOLEAN     kicker = FALSE;
    NDIS_STATUS ndisSt = NDIS_STATUS_FAILURE;
    PNDIS_PACKET pNdisPacket = NULL;
    PTLP_BUF_ITEM tlpItem = NULL;
    PLIST_ENTRY   headOfList;
    PUCHAR pDataPkt;
    PMP_ADAPTER returnAdapter;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> --->[TLP] TLPTxPacket 0x%p\n", pAdapter->PortName, pList)
    );

    // obtain TLP item
    NdisAcquireSpinLock(&pAdapter->TxLock);

    if (!IsListEmpty(&pAdapter->UplinkFreeBufferQueue))
    {
        headOfList = RemoveHeadList(&pAdapter->UplinkFreeBufferQueue);
        tlpItem = CONTAINING_RECORD(headOfList, TLP_BUF_ITEM, List);
    }

    NdisReleaseSpinLock(&pAdapter->TxLock);

    if (pList == NULL) // send
    {
        goto PrepareIrp;
    }

    pNdisPacket = ListItemToPacket(pList);
    NdisQueryPacket
    (
        pNdisPacket,
        NULL,
        &dwTotalBufferCount,
        &pCurrentNdisBuffer,
        &dwPacketLength
    );

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            sendBytes = dwPacketLength + sizeof(MPQOS_HDR8);
        }
        else
        {
            sendBytes = dwPacketLength + sizeof(MPQOS_HDR);
        }
    }
    else
#endif // MP_QCQOS_ENABLED
    {
        sendBytes = dwPacketLength;
    }

    // obtain buffer
    if (tlpItem == NULL)
    {
        usbBuffer = NULL;
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [TLP] TLPTxPacket: no more TLP buffer (0x%p)\n", pAdapter->PortName, pNdisPacket)
        );
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [TLP] TLPTxPacket => 0x%p (%dB/%dB/%dB)\n", pAdapter->PortName, pNdisPacket,
            dwPacketLength, sendBytes, tlpItem->RemainingCapacity)
        );
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
        if ((pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE))
        {
            if (tlpItem->RemainingCapacity > (sendBytes + 8 + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM)))
            {
                usbBuffer = tlpItem->CurrentPtr + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM); // room for length
            }
            else
            {
                usbBuffer = NULL;
            }
        }
        else
#endif
#endif
            if ((pAdapter->QMAPEnabledV4 == TRUE))
            {
                if (tlpItem->RemainingCapacity > (sendBytes + 8 + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM)))
                {
                    usbBuffer = tlpItem->CurrentPtr + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM); // room for length
                }
                else
                {
                    usbBuffer = NULL;
                }
            }
            else
                if (pAdapter->QMAPEnabledV1 == TRUE)
                {
                    if (tlpItem->RemainingCapacity > (sendBytes + 8 + sizeof(QMAP_STRUCT)))
                    {
                        usbBuffer = tlpItem->CurrentPtr + sizeof(QMAP_STRUCT); // room for length
                    }
                    else
                    {
                        usbBuffer = NULL;
                    }
                }
                else if (pAdapter->MBIMULEnabled == TRUE)
                {
                    if (tlpItem->Buffer == tlpItem->CurrentPtr)
                    {
                        if ((tlpItem->RemainingCapacity > sendBytes +
                            sizeof(NTB_16BIT_HEADER) +
                            sizeof(NDP_16BIT_HEADER) +
                            sizeof(DATAGRAM_STRUCT) * (tlpItem->AggregationCount + 2) + 8) &&
                            (tlpItem->AggregationCount < (MP_NUM_MBIM_UL_DATAGRAM_ITEMS_DEFAULT - 2)))
                        {
                            usbBuffer = tlpItem->CurrentPtr + sizeof(NTB_16BIT_HEADER);
                        }
                        else
                        {
                            usbBuffer = NULL;
                        }
                    }
                    else if ((tlpItem->RemainingCapacity > sendBytes +
                             sizeof(NDP_16BIT_HEADER) +
                             sizeof(DATAGRAM_STRUCT) * (tlpItem->AggregationCount + 2) + 8) &&
                             (tlpItem->AggregationCount < (MP_NUM_MBIM_UL_DATAGRAM_ITEMS_DEFAULT - 2)))
                    {
                        usbBuffer = tlpItem->CurrentPtr;
                    }
                    else
                    {
                        usbBuffer = NULL;
                    }
                }
                else if (tlpItem->RemainingCapacity > sendBytes + 8)
                {
                    if (pAdapter->TLPEnabled == TRUE)
                    {
                        usbBuffer = tlpItem->CurrentPtr + sizeof(USHORT); // room for length
                    }
                    else
                    {
                        usbBuffer = tlpItem->CurrentPtr;
                    }
                }
                else
                {
                    usbBuffer = NULL;
                }
    }

    if (usbBuffer == NULL)
    {
        Status = NDIS_STATUS_FAILURE;
    }
    else
    {
        Status = NDIS_STATUS_SUCCESS;
    }

    if (Status != NDIS_STATUS_SUCCESS)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPUSB_TLPTxPacket failed to obtain buffer\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            pDataPkt = (PUCHAR)(usbBuffer + sizeof(MPQOS_HDR8));
        }
        else
        {
            pDataPkt = (PUCHAR)(usbBuffer + sizeof(MPQOS_HDR));
        }
        copyOk = MPUSB_CopyNdisPacketToBuffer
        (
            pNdisPacket,
            pDataPkt,
            0
        );
    }
    else
#endif // MP_QCQOS_ENABLED
    {
        pDataPkt = (PUCHAR)usbBuffer;
        copyOk = MPUSB_CopyNdisPacketToBuffer(pNdisPacket, pDataPkt, 0);
    }
    if (copyOk == 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> _TLPTxPacket failed to copy pkt\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }

#ifdef QC_IP_MODE
    if (pAdapter->IPModeEnabled == TRUE)
    {
        if (FALSE == MPUSB_PreparePacket(pAdapter, usbBuffer, (PULONG)&sendBytes))
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> IPO: _PreparePacket: local process %dB(0x%p)\n", pAdapter->PortName,
                sendBytes, pNdisPacket)
            );
            kicker = TRUE;
            ndisSt = NDIS_STATUS_SUCCESS;
            goto Tx_Exit;
        }
        else
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_TRACE,
                ("<%s> IPO: _PreparePacket: to USB %dB(0x%p)\n", pAdapter->PortName,
                sendBytes, pNdisPacket)
            );
        }
    }
#endif // QC_IP_MODE

    if (pAdapter->IPModeEnabled == TRUE)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE, MP_DBG_LEVEL_DETAIL,
            ("<%s> _TLPTxPacket: aggregated UL-IP seq num (0x%02X, 0x%02X)\n", pAdapter->PortName, pDataPkt[4], pDataPkt[5])
        );
    }
    else if (sendBytes > 26)
    {
        // Ethernet mode
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE, MP_DBG_LEVEL_DETAIL,
            ("<%s> _TLPTxPacket: aggregated UL-IP seq num (0x%02X, 0x%02X)\n", pAdapter->PortName, pDataPkt[18], pDataPkt[19])
        );
    }

    // Update TLP item
    {

        if ((pAdapter->QMAPEnabledV1 == TRUE)
            || (pAdapter->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
            || (pAdapter->QMAPEnabledV2 == TRUE)
            || (pAdapter->QMAPEnabledV3 == TRUE)
#endif
#endif
            )
        {
            PQMAP_STRUCT Qmap;
            UCHAR paddingBytes = 0;
            Qmap = (PQMAP_STRUCT)(tlpItem->CurrentPtr);
            RtlZeroMemory(Qmap, sizeof(QMAP_STRUCT));
            Qmap->PadCD = 0x00; //TODO: check the order
            // Padding //TODO: check the order
            if (pAdapter->QMAPEnabledV4 != TRUE)
            {
                if ((sendBytes % 4) > 0)
                {
                    paddingBytes = 4 - (sendBytes % 4);
                    RtlZeroMemory((PUCHAR)((PUCHAR)usbBuffer + sendBytes), paddingBytes);
                }
            }
            Qmap->PadCD |= (paddingBytes);
            Qmap->MuxId = pAdapter->MuxId;
            sendBytes += paddingBytes;
            Qmap->PacketLen = RtlUshortByteSwap(sendBytes);
            if (pAdapter->QMAPEnabledV4 == TRUE)
            {
                PQMAP_UL_CHECKSUM pULCheckSum = (PQMAP_UL_CHECKSUM)((PUCHAR)Qmap + sizeof(QMAP_STRUCT));
                RtlZeroMemory(pULCheckSum, sizeof(QMAP_UL_CHECKSUM));
                tlpItem->CurrentPtr += (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
                tlpItem->DataLength += (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
                tlpItem->RemainingCapacity -= (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
            }
            else
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
                if ((pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE))
                {
                    PUCHAR ipPtr = usbBuffer;
                    UCHAR ipVer;
                    USHORT CkOffset = 0;
                    PUSHORT ckOffsetPtr;
                    PQMAP_UL_CHECKSUM pULCheckSum = (PQMAP_UL_CHECKSUM)((PUCHAR)Qmap + sizeof(QMAP_STRUCT));

#ifdef MP_QCQOS_ENABLED
                    if (pAdapter->QosEnabled == TRUE)
                    {
                        if (pAdapter->QosHdrFmt == 0x02)
                        {
                            ipPtr += sizeof(MPQOS_HDR8);
                        }
                        else
                        {
                            ipPtr += sizeof(MPQOS_HDR);
                        }
                    }
#endif // MP_QCQOS_ENABLED

                    RtlZeroMemory(pULCheckSum, sizeof(QMAP_UL_CHECKSUM));
                    ipVer = (*ipPtr & 0xF0) >> 4;
                    if (ipVer == 4)
                    {
                        PQMAP_IPV4 ipV4;
                        IPV4_FRAG_PKT fragPkt;
                        QMAP_PSEUDO_CHECKSUM pseudoChkSum;
                        UCHAR FragPacket = 0x00;
                        /* IP v4 */
                        pULCheckSum->CksumStartOffset = RtlUshortByteSwap((*ipPtr & 0x0F) * 4);
                        /* Bit 0 CHK enable */
                        pULCheckSum->CksumInsertOffsetCU = 0x8000;
                        /* Bit 1 IPv4/UDP enable */
                        pULCheckSum->CksumInsertOffsetCU |= 0x4000;

                        ipV4 = (PQMAP_IPV4)ipPtr;
                        pseudoChkSum.DestAddr = ipV4->DestAddr;
                        pseudoChkSum.SrcAddr = ipV4->SrcAddr;
                        pseudoChkSum.Zeros = 0x00;
                        pseudoChkSum.Protocol = ipV4->Protocol;
                        pseudoChkSum.Length = RtlUshortByteSwap(RtlUshortByteSwap(ipV4->TotaLlen) - ((*ipPtr & 0x0F) * 4));
                        if (ipV4->Protocol == 0x06) /* TCP */
                        {
                            CkOffset = ((*ipPtr & 0x0F) * 4) + (sizeof(QMAP_TCP) - sizeof(USHORT));
                        }
                        else if (ipV4->Protocol == 0x11) /* UDP */
                        {
                            CkOffset = ((*ipPtr & 0x0F) * 4) + (sizeof(QMAP_UDP) - sizeof(USHORT));
                        }
                        else if (ipV4->Protocol == 0x01) /* ICMP */
                        {
                            CkOffset = ((*ipPtr & 0x0F) * 4) + (sizeof(QMAP_ICMP) - sizeof(USHORT));
                        }

                        fragPkt.Rsvd2 = RtlUshortByteSwap(ipV4->Rsvd2);

                        if (fragPkt.Flags & 0x01)
                        {
                            FragPacket = 0x01;
                        }
                        if (fragPkt.Offset != 0)
                        {
                            FragPacket = 0x01;
                        }

                        QCNET_DbgPrint
                        (
                            MP_DBG_MASK_READ,
                            MP_DBG_LEVEL_DETAIL,
                            ("<%s> MPUSB_TLPTxPacket: FragPacket Flag 0x%x and Offset 0x%x\n", pAdapter->PortName, fragPkt.Flags, fragPkt.Offset)
                        );

                        if ((CkOffset != 0x00) && (FragPacket != 0x01))
                        {
                            ckOffsetPtr = (PUSHORT)(ipPtr + CkOffset);
                            pULCheckSum->CksumInsertOffsetCU |= CkOffset;
                            pULCheckSum->CksumInsertOffsetCU = RtlUshortByteSwap(pULCheckSum->CksumInsertOffsetCU);
                            if (ipV4->Protocol != 0x01) /* NOT ICMP */
                            {
                                *ckOffsetPtr = CheckSum((PUSHORT)(&pseudoChkSum), sizeof(pseudoChkSum)); /* TODO: big endian*/
                            }
                            else
                            {
                                *ckOffsetPtr = 0;
                            }
                            QCNET_DbgPrint
                            (
                                MP_DBG_MASK_WRITE,
                                MP_DBG_LEVEL_ERROR,
                                ("<%s> MPUSB_TLPTxPacket: Checksumoffsetptr 0x%p, Checksum Value 0x%x offset 0x%x\n",
                                pAdapter->PortName, ckOffsetPtr, *ckOffsetPtr, CkOffset)
                            );
                        }
                        else
                        {
                            /* TODO: debug message */
                            QCNET_DbgPrint
                            (
                                MP_DBG_MASK_WRITE,
                                MP_DBG_LEVEL_ERROR,
                                ("<%s> MPUSB_TLPTxPacket: Bad IP packet or fragmented type 0x%x\n",
                                pAdapter->PortName, ipV4->Protocol)
                            );

                            /* Bit 0 CHK enable clear */
                            pULCheckSum->CksumInsertOffsetCU &= 0x7FFF;
                        }
                    }
                    else
                    {
                        PQMAP_IPV6 ipV6;
                        QMAP_PSEUDO_CHECKSUMV6 pseudoChkSum;
                        /* IP v6 */
                        pULCheckSum->CksumStartOffset = RtlUshortByteSwap(sizeof(QMAP_IPV6));
                        /* Bit 0 CHK enable */
                        pULCheckSum->CksumInsertOffsetCU = 0x8000;

                        ipV6 = (PQMAP_IPV6)ipPtr;
                        RtlCopyMemory(pseudoChkSum.DestAddr, ipV6->DestAddr, sizeof(ipV6->DestAddr));
                        RtlCopyMemory(pseudoChkSum.SrcAddr, ipV6->SrcAddr, sizeof(ipV6->SrcAddr));
                        RtlZeroMemory(pseudoChkSum.Zeros, 0x03);
                        pseudoChkSum.payloadLen = ipV6->payloadLen;
                        pseudoChkSum.NextHdr = ipV6->nextHdr;

                        if (ipV6->nextHdr == 0x06)  /* TCP */
                        {
                            /* Assuming that IP header is fixed to 40Bytes */
                            CkOffset = sizeof(QMAP_IPV6) + (sizeof(QMAP_TCP) - sizeof(USHORT));
                        }
                        else if (ipV6->nextHdr == 0x11) /* UDP */
                        {
                            CkOffset = sizeof(QMAP_IPV6) + (sizeof(QMAP_UDP) - sizeof(USHORT));
                            /* Bit 1 IPv4/UDP enable */
                            pULCheckSum->CksumInsertOffsetCU |= 0x4000;
                        }
                        else if (ipV6->nextHdr == 0x01) /* ICMP */
                        {
                            CkOffset = sizeof(QMAP_IPV6) + (sizeof(QMAP_ICMP) - sizeof(USHORT));
                        }
                        if (CkOffset != 0x00)
                        {
                            ckOffsetPtr = (PUSHORT)(ipPtr + CkOffset);
                            pULCheckSum->CksumInsertOffsetCU |= CkOffset;
                            pULCheckSum->CksumInsertOffsetCU = RtlUshortByteSwap(pULCheckSum->CksumInsertOffsetCU);
                            *ckOffsetPtr = CheckSum((PUSHORT)(&pseudoChkSum), sizeof(pseudoChkSum));
                        }
                        else
                        {
                            /* TODO: debug message */
                            QCNET_DbgPrint
                            (
                                MP_DBG_MASK_WRITE,
                                MP_DBG_LEVEL_ERROR,
                                ("<%s> MPUSB_TLPTxPacket: Bad IP packet or fragmented type 0x%x\n",
                                pAdapter->PortName, ipV6->nextHdr)
                            );

                            /* Bit 0 CHK enable clear */
                            pULCheckSum->CksumInsertOffsetCU &= 0x7FFF;
                        }
                    }
                    tlpItem->CurrentPtr += (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
                    tlpItem->DataLength += (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
                    tlpItem->RemainingCapacity -= (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
                    // update the checsum header here...
                }
                else
#endif
#endif
                {
                    tlpItem->CurrentPtr += (sendBytes + sizeof(QMAP_STRUCT));
                    tlpItem->DataLength += (sendBytes + sizeof(QMAP_STRUCT));
                    tlpItem->RemainingCapacity -= (sendBytes + sizeof(QMAP_STRUCT));
                }
        }
        else if (pAdapter->MBIMULEnabled == TRUE)
        {
            if (tlpItem->Buffer == tlpItem->CurrentPtr)
            {
                tlpItem->pNtbHeader = (PNTB_16BIT_HEADER)tlpItem->CurrentPtr;
                tlpItem->pNtbHeader->dwSignature = 0x484D434E; //NCMH
                tlpItem->pNtbHeader->wHeaderLength = sizeof(NTB_16BIT_HEADER);
                tlpItem->pNtbHeader->wSequence = (USHORT)pAdapter->ntbSequence;
                tlpItem->pNtbHeader->wBlockLength = sizeof(NTB_16BIT_HEADER);
                tlpItem->pNtbHeader->wNdpIndex = 0;
                tlpItem->CurrentPtr += sizeof(NTB_16BIT_HEADER);
                tlpItem->DataLength += sizeof(NTB_16BIT_HEADER);
                tlpItem->RemainingCapacity -= sizeof(NTB_16BIT_HEADER);

                // tlpItem->NdpHeader.dwSignature = 0x50444E51; //QNDP
                // tlpItem->NdpHeader.dwSignature = 0x344D434E; //NCM4
                // tlpItem->NdpHeader.dwSignature = 0x00535049; //IPS0
                tlpItem->NdpHeader.dwSignature = pAdapter->ndpSignature;

                if ((tlpItem->NdpHeader.dwSignature & 0x00FFFFFF) == 0x00535049)
                {
                    ULONG MuxId = pAdapter->MuxId;
                    MuxId = (MuxId << 24);
                    tlpItem->NdpHeader.dwSignature = ((tlpItem->NdpHeader.dwSignature & 0x00FFFFFF) | MuxId);
                }

                tlpItem->NdpHeader.wNextNdpIndex = 0x00;

                if (InterlockedIncrement(&pAdapter->ntbSequence) >= 0xFFFF)
                {
                    pAdapter->ntbSequence = 0x00;
                }
            }
            tlpItem->DataGrams[tlpItem->AggregationCount].DatagramPtr = (tlpItem->CurrentPtr - tlpItem->Buffer);
            tlpItem->DataGrams[tlpItem->AggregationCount].DatagramLen = sendBytes;
            tlpItem->CurrentPtr += sendBytes;
            tlpItem->DataLength += sendBytes;
            tlpItem->RemainingCapacity -= sendBytes;

            tlpItem->pNtbHeader->wBlockLength += sendBytes;

        }
        else if (pAdapter->TLPEnabled == TRUE)
        {
            PUSHORT tlpLen;
            tlpLen = (PUSHORT)(tlpItem->CurrentPtr);
            *tlpLen = sendBytes; // write pkt length
            tlpItem->CurrentPtr += (sendBytes + sizeof(USHORT));
            tlpItem->DataLength += (sendBytes + sizeof(USHORT));
            tlpItem->RemainingCapacity -= (sendBytes + sizeof(USHORT));
        }
        else
        {
            tlpItem->CurrentPtr += sendBytes;
            tlpItem->DataLength += sendBytes;
            tlpItem->RemainingCapacity -= sendBytes;
        }
        if (pAdapter->MPQuickTx == 0)
        {
            InsertTailList(&tlpItem->PacketList, pList);
        }
    }

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            qosHdr8 = (PMPQOS_HDR8)usbBuffer;
            qosHdr8->Version = 1;
            qosHdr8->Reserved = 0;
            if (IsQosPacket == TRUE)
            {
                qosHdr8->FlowId = QosFlowId;
            }
            else
            {
                qosHdr8->FlowId = 0;
            }
            qosHdr8->Rsvd[0] = 0;
            qosHdr8->Rsvd[1] = 0;
        }
        else
        {
            qosHdr = (PMPQOS_HDR)usbBuffer;
            qosHdr->Version = 1;
            qosHdr->Reserved = 0;
            if (IsQosPacket == TRUE)
            {
                qosHdr->FlowId = QosFlowId;
            }
            else
            {
                qosHdr->FlowId = 0;
            }
        }
        NdisAcquireSpinLock(&pAdapter->TxLock);
        pAdapter->QosPendingPackets++;
        NdisReleaseSpinLock(&pAdapter->TxLock);
    }
#endif // MP_QCQOS_ENABLED

    tlpItem->AggregationCount++;

    /* TODO: size of checksum offload */
    if ((pAdapter->QMAPEnabledV1 == TRUE)
        || (pAdapter->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
        || (pAdapter->QMAPEnabledV2 == TRUE)
        || (pAdapter->QMAPEnabledV3 == TRUE)
#endif
#endif
        )
    {
        // stay for aggregation
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPUSB_TLPTxPacket: stay for aggregation, restore item [%d] %dB(%d)\n",
            pAdapter->PortName, tlpItem->Index, tlpItem->DataLength, tlpItem->AggregationCount)
        );
        NdisAcquireSpinLock(&pAdapter->TxLock);
        InsertHeadList(&pAdapter->UplinkFreeBufferQueue, &tlpItem->List);
        NdisReleaseSpinLock(&pAdapter->TxLock);

        // Set the transmit timer
        SetTransmitTimer(pAdapter);

        if (pAdapter->MPQuickTx != 0)
        {
            NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_SUCCESS);
            MPUSB_TLPIndicateCompleteTx(pAdapter, pNdisPacket, 1);
        }

        goto Tx_Exit;
    }
    else if ((tlpItem->AggregationCount < (MP_NUM_MBIM_UL_DATAGRAM_ITEMS_DEFAULT - 2)) && (pAdapter->MBIMULEnabled == TRUE))
    {
        // stay for aggregation
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPUSB_TLPTxPacket: stay for aggregation, restore item [%d] %dB(%d)\n",
            pAdapter->PortName, tlpItem->Index, tlpItem->DataLength, tlpItem->AggregationCount)
        );
        NdisAcquireSpinLock(&pAdapter->TxLock);
        InsertHeadList(&pAdapter->UplinkFreeBufferQueue, &tlpItem->List);
        NdisReleaseSpinLock(&pAdapter->TxLock);

        // Set the transmit timer
        SetTransmitTimer(pAdapter);

        if (pAdapter->MPQuickTx != 0)
        {
            NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_SUCCESS);
            MPUSB_TLPIndicateCompleteTx(pAdapter, pNdisPacket, 1);
        }

        goto Tx_Exit;
    }
    else if ((pAdapter->TLPEnabled == TRUE))
    {
        // stay for aggregation
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPUSB_TLPTxPacket: stay for aggregation, restore item [%d] %dB(%d)\n",
            pAdapter->PortName, tlpItem->Index, tlpItem->DataLength, tlpItem->AggregationCount)
        );
        NdisAcquireSpinLock(&pAdapter->TxLock);
        InsertHeadList(&pAdapter->UplinkFreeBufferQueue, &tlpItem->List);
        NdisReleaseSpinLock(&pAdapter->TxLock);

        // Set the transmit timer
        SetTransmitTimer(pAdapter);

        if (pAdapter->MPQuickTx != 0)
        {
            NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_SUCCESS);
            MPUSB_TLPIndicateCompleteTx(pAdapter, pNdisPacket, 1);
        }

        goto Tx_Exit;
    }
    else
    {
        if (pAdapter->MPQuickTx != 0)
        {
            NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_SUCCESS);
            MPUSB_TLPIndicateCompleteTx(pAdapter, pNdisPacket, 2);
        }

        if ((pAdapter->TLPEnabled == TRUE) ||
            (pAdapter->MBIMULEnabled == TRUE) ||
            (pAdapter->QMAPEnabledV1 == TRUE)
            || (pAdapter->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
            || (pAdapter->QMAPEnabledV2 == TRUE)
            || (pAdapter->QMAPEnabledV3 == TRUE)
#endif
#endif
            )
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE, MP_DBG_LEVEL_DETAIL,
                ("<%s> ---> Flush due to Buffer Full %d\n", pAdapter->PortName, pAdapter->FlushBufferBufferFull++)
            );
            CancelTransmitTimer(pAdapter);
        }

    }

PrepareIrp:

    if (tlpItem == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPUSB_TLPTxPacket: nothing to send\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }

    if (tlpItem->DataLength == 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPUSB_TLPTxPacket: 0 TX data, extra flush\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }

    // Copy the NDP header and update the lengths correctly
    if (pAdapter->MBIMULEnabled == TRUE)
    {
        ULONG Length = (sizeof(NDP_16BIT_HEADER) + ((tlpItem->AggregationCount + 1) * sizeof(DATAGRAM_STRUCT)));
        tlpItem->NdpHeader.wLength += Length;
        RtlCopyMemory(tlpItem->CurrentPtr, (PUCHAR)&tlpItem->NdpHeader, Length);
        tlpItem->DataLength += Length;
        tlpItem->RemainingCapacity -= Length;
        tlpItem->pNtbHeader->wBlockLength += Length;
        tlpItem->pNtbHeader->wNdpIndex = (tlpItem->CurrentPtr - tlpItem->Buffer);

        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [TLP] MBIM_UL => (%d)\n", pAdapter->PortName, tlpItem->AggregationCount)
        );

        MPMAIN_PrintBytes
        (
            &tlpItem->NdpHeader, 64, Length, "IPO: MBIM_UL_NDP->D", pAdapter,
            MP_DBG_MASK_DATA_WT, MP_DBG_LEVEL_VERBOSE
        );

    }

    // Prepare IRP
    pIrp = tlpItem->Irp;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_VERBOSE,
        ("<%s> [TLP] TLPTxPacket: sending [%d] %dB/%dB\n", pAdapter->PortName,
        tlpItem->Index, tlpItem->DataLength, tlpItem->RemainingCapacity)
    );
    if (pAdapter->IPModeEnabled == TRUE)
    {
        MPMAIN_PrintBytes
        (
            tlpItem->Buffer, 64, tlpItem->DataLength, "IPO: TLPIP->D", pAdapter,
            MP_DBG_MASK_DATA_WT, MP_DBG_LEVEL_VERBOSE
        );
    }

    IoReuseIrp(pIrp, STATUS_SUCCESS);

    pIrp->MdlAddress = NULL;
    pIrp->AssociatedIrp.SystemBuffer = tlpItem->Buffer;
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_WRITE;
    nextstack->Parameters.Write.Length = tlpItem->DataLength;

    // Init IRP
    IoSetCompletionRoutine
    (
        pIrp,
        MPUSB_TLPTxIRPComplete,
        tlpItem,
        TRUE, TRUE, TRUE
    );

    NdisAcquireSpinLock(&pAdapter->TxLock);
    InterlockedIncrement(&(pAdapter->TxTLPPendinginUSB));
    NdisReleaseSpinLock(&pAdapter->TxLock);

    returnAdapter = FindStackDeviceObject(pAdapter);
    if (returnAdapter != NULL)
    {
        Status = QCIoCallDriver(returnAdapter->USBDo, pIrp);
    }
    else
    {
        Status = STATUS_UNSUCCESSFUL;
        pIrp->IoStatus.Status = Status;
        MPUSB_TLPTxIRPComplete(pAdapter->USBDo, pIrp, tlpItem);
    }

Tx_Exit:

    if (kicker == TRUE)
    {
#ifdef QC_IP_MODE
        if (pAdapter->IPModeEnabled == TRUE)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_TRACE,
                ("<%s> IPO: _TLPTxPacket: complete 0x%p (0x%x)\n", pAdapter->PortName,
                pNdisPacket, ndisSt)
            );
        }
#endif // QC_IP_MODE

        if (pList != NULL)
        {
            NdisAcquireSpinLock(&pAdapter->TxLock);

            if (pAdapter->MPQuickTx == 0)
            {
                // remove from TLP PacketList (if it's ever en-queued)
                RemoveEntryList(pList);
            }
            NDIS_SET_PACKET_STATUS(pNdisPacket, ndisSt);
            InsertTailList(&pAdapter->TxCompleteList, pList);
            NdisReleaseSpinLock(&pAdapter->TxLock);
            MPWork_ScheduleWorkItem(pAdapter);
        }
        if (tlpItem != NULL)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> [TLP] TLPTxPacket: restore item [%d]\n", pAdapter->PortName,
                tlpItem->Index)
            );
            NdisAcquireSpinLock(&pAdapter->TxLock);
            InsertHeadList(&pAdapter->UplinkFreeBufferQueue, &tlpItem->List);
            NdisReleaseSpinLock(&pAdapter->TxLock);
        }
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--MPUSB_TLPTxPacket (nBusyTx: %d/%dP/%dP) %d\n", pAdapter->PortName,
        pAdapter->nBusyTx, pAdapter->QosPendingPackets, pAdapter->MPPendingPackets, kicker)
    );
    return;

}  // MPUSB_TLPTxPacket

BOOLEAN MPUSB_TLPProcessPendingTxQueue(PMP_ADAPTER pAdapter)
{
    PLIST_ENTRY pList;
    UINT        processed;
    NDIS_STATUS status;
    BOOLEAN     kicker = FALSE, bCapacityReached = FALSE;
    INT         aggStatus;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> [TLP] TLPProcessPendingTxQueue\n", pAdapter->PortName)
    );

    processed = 0;

    NdisAcquireSpinLock(&pAdapter->TxLock);

    while (!IsListEmpty(&pAdapter->TxPendingList))
    {
        ULONG SizeofPacket = GetNextTxPacketSize(pAdapter, &pAdapter->TxPendingList);
        aggStatus = MPUSB_AggregationAvailable(pAdapter, FALSE, SizeofPacket);
        if (TLP_AGG_MORE == aggStatus)
        {
            // pList will be part of the IRP context
            pList = RemoveHeadList(&pAdapter->TxPendingList);

            if (((pAdapter->Flags & fMP_ANY_FLAGS) == 0) &&
                (pAdapter->ToPowerDown == FALSE) &&
                (MPMAIN_InPauseState(pAdapter) == FALSE))
            {
                NdisReleaseSpinLock(&pAdapter->TxLock);
#ifdef NDIS620_MINIPORT
                MPUSB_TLPTxPacketEx(pAdapter, 0, FALSE, pList);
#else
                MPUSB_TLPTxPacket(pAdapter, 0, FALSE, pList);
#endif
                ++processed;
            }
            else
            {
                kicker = TRUE;
                // If the Busy list is empty we can just complete this packet
                // This is to complete the TX pkt in order?
                // However, we don't use TxBusyList for TLP
                if (IsListEmpty(&pAdapter->TxBusyList))
                {

#ifdef NDIS620_MINIPORT
                    PMPUSB_TX_CONTEXT_NB txNb;
                    txNb = CONTAINING_RECORD
                    (
                        pList,
                        MPUSB_TX_CONTEXT_NB,
                        List
                    );

#if 0               
                    MPUSB_CompleteNetBufferList
                    (
                        pAdapter,
                        txNb->NBL->NBL,
                        NDIS_STATUS_FAILURE,
                        NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
                        txNb->NBL
                    );
#endif
                    NdisReleaseSpinLock(&pAdapter->TxLock);
                    ((PMPUSB_TX_CONTEXT_NBL)txNb->NBL)->NdisStatus = NDIS_STATUS_FAILURE;
                    MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, txNb, TRUE);
#else               
                    PNDIS_PACKET pNdisPacket;
                    pNdisPacket = ListItemToPacket(pList);
                    NDIS_SET_PACKET_STATUS(pNdisPacket, NDIS_STATUS_FAILURE);
                    InsertTailList(&pAdapter->TxCompleteList, pList);
                    NdisReleaseSpinLock(&pAdapter->TxLock);
#endif               
                }
                else
                {
                    // But if the BusyList is not empty we need to wait it out and try again next time.
                    InsertHeadList(&pAdapter->TxPendingList, pList);

                    // shall we wait a bit here?
                    NdisReleaseSpinLock(&pAdapter->TxLock);
                    MPMAIN_Wait(-(100 * 1000L)); // 10ms
                }
                }
            }
        else if (TLP_AGG_SEND == aggStatus)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> [TLP] TLPProcessPendingTxQueue: sending\n", pAdapter->PortName)
            );
            NdisReleaseSpinLock(&pAdapter->TxLock);
#ifdef NDIS620_MINIPORT
            MPUSB_TLPTxPacketEx(pAdapter, 0, FALSE, NULL);
#else
            MPUSB_TLPTxPacket(pAdapter, 0, FALSE, NULL);
#endif
        }
        else
        {
            // no more TLP capacity, exit and wait for next Tx event
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> [TLP] TLPProcessPendingTxQueue: out of capacity, wait ST 0x%x\n",
                pAdapter->PortName, aggStatus)
            );
            bCapacityReached = TRUE;
            break;
        }

        NdisAcquireSpinLock(&pAdapter->TxLock);
        }  // while

    NdisReleaseSpinLock(&pAdapter->TxLock);

    // no pending TX, send current TLP (when no aggregation)
    if ((pAdapter->MPDisableQoS >= 3) &&
        (pAdapter->TLPEnabled == FALSE) && (pAdapter->MBIMULEnabled == FALSE) &&
        (pAdapter->QMAPEnabledV1 == FALSE)
        && (pAdapter->QMAPEnabledV4 == FALSE)
#ifdef QCUSB_MUX_PROTOCOL                        
#if defined(QCMP_QMAP_V2_SUPPORT)
        && (pAdapter->QMAPEnabledV2 == FALSE)
        && (pAdapter->QMAPEnabledV3 == FALSE)
#endif
#endif
        )
    {
        // no pending TX, send current TLP
#ifdef NDIS620_MINIPORT
        MPUSB_TLPTxPacketEx(pAdapter, 0, FALSE, NULL);
#else
        MPUSB_TLPTxPacket(pAdapter, 0, FALSE, NULL);
#endif
    }

    if (bCapacityReached == TRUE)
    {
        // pause briefly
        MPMAIN_Wait(-(2 * 1000L)); // 0.2ms
    }

    if (kicker)
    {
        MPWork_ScheduleWorkItem(pAdapter);
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--[TLP] TLPProcessPendingTxQueue(processed: %d)\n", pAdapter->PortName, processed)
    );

    return (processed > 0);
    } // MPUSB_TLPProcessPendingTxQueue

#ifdef NDIS620_MINIPORT

VOID MPUSB_PurgeTLPQueuesEx(PMP_ADAPTER pAdapter, BOOLEAN FreeMemory)
{
    PLIST_ENTRY   headOfList, peekEntry, nextEntry;
    PTLP_BUF_ITEM tlpItem;
    PNET_BUFFER_LIST      netBufferList;
    PMPUSB_TX_CONTEXT_NB nbContext, nbx;
    PMPUSB_TX_CONTEXT_NBL nblContext;
    int i = 0, nPkt = 0;

    /* Before cleanup check for TLP Tx empty */
    while (TRUE)
    {
        NdisAcquireSpinLock(&pAdapter->TxLock);
        if (pAdapter->TxTLPPendinginUSB != 0)
        {
            NdisReleaseSpinLock(&pAdapter->TxLock);
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> [TLP] PurgeTLPQueues: TxTLPPendinginUSB not empty %d\n", pAdapter->PortName, pAdapter->TxTLPPendinginUSB)
            );

            NdisMSleep(2000);
            continue;
        }
        else
        {
            NdisReleaseSpinLock(&pAdapter->TxLock);
            break;
        }
    }

    NdisAcquireSpinLock(&pAdapter->TxLock);

    if (!IsListEmpty(&pAdapter->UplinkFreeBufferQueue))
    {
        headOfList = &pAdapter->UplinkFreeBufferQueue;
        peekEntry = headOfList->Flink;

        while (peekEntry != headOfList)
        {
            i++;
            tlpItem = CONTAINING_RECORD(peekEntry, TLP_BUF_ITEM, List);
            tlpItem->DataLength = 0;
            if (FreeMemory == TRUE)
            {
                if (tlpItem->Buffer != NULL)
                {
                    ExFreePool(tlpItem->Buffer);
                }
                if (tlpItem->Irp != NULL)
                {
                    IoFreeIrp(tlpItem->Irp);
                    tlpItem->Irp = NULL;
                }
                tlpItem->Buffer = NULL;
                tlpItem->CurrentPtr = NULL;
                tlpItem->RemainingCapacity = 0;
                tlpItem->AggregationCount = 0;
                nextEntry = peekEntry->Flink;
                RemoveEntryList(peekEntry);
                peekEntry = nextEntry;
            }
            else
            {
                tlpItem->CurrentPtr = tlpItem->Buffer;
                tlpItem->RemainingCapacity = pAdapter->UplinkTLPSize;
                tlpItem->AggregationCount = 0;
                peekEntry = peekEntry->Flink;
            }
            while (!IsListEmpty(&tlpItem->PacketList))
            {
                headOfList = RemoveHeadList(&tlpItem->PacketList);
                nbContext = CONTAINING_RECORD(headOfList, MPUSB_TX_CONTEXT_NB, List);
#if 0            
                if (nblContext->AbortFlag != 0)
                {
                    MPUSB_CompleteNetBufferList
                    (
                        pAdapter,
                        nblContext->NBL,
                        NDIS_STATUS_REQUEST_ABORTED,
                        NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
                        nblContext
                    );
                }
                else
                {
                    MPUSB_CompleteNetBufferList
                    (
                        pAdapter,
                        nblContext->NBL,
                        NDIS_STATUS_SUCCESS,
                        NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
                        nblContext
                    );
                }
#endif
                ((PMPUSB_TX_CONTEXT_NBL)nbContext->NBL)->NdisStatus = NDIS_STATUS_SUCCESS;
                MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, FALSE);
                nPkt++;
                }
            } // while
        } // if

        // Check TxBusyList
    while (!IsListEmpty(&pAdapter->TxBusyList))
    {
        PLIST_ENTRY  pList;
        pList = RemoveHeadList(&pAdapter->TxBusyList);
        nbContext = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NB, List);
#if 0      
        if (nblContext->AbortFlag != 0)
        {
            MPUSB_CompleteNetBufferList
            (
                pAdapter,
                nblContext->NBL,
                NDIS_STATUS_REQUEST_ABORTED,
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
                nblContext
            );
        }
        else
        {
            MPUSB_CompleteNetBufferList
            (
                pAdapter,
                nblContext->NBL,
                NDIS_STATUS_SUCCESS,
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
                nblContext
            );
        }
#endif
        ((PMPUSB_TX_CONTEXT_NBL)nbContext->NBL)->NdisStatus = NDIS_STATUS_SUCCESS;
        MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, FALSE);
        }

    // Check TxPendingList
    while (!IsListEmpty(&pAdapter->TxPendingList))
    {
        PLIST_ENTRY  pList;
        pList = RemoveHeadList(&pAdapter->TxPendingList);
        nbContext = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NB, List);
#if 0      
        if (nblContext->AbortFlag != 0)
        {
            MPUSB_CompleteNetBufferList
            (
                pAdapter,
                nblContext->NBL,
                NDIS_STATUS_REQUEST_ABORTED,
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
                nblContext
            );
        }
        else
        {
            MPUSB_CompleteNetBufferList
            (
                pAdapter,
                nblContext->NBL,
                NDIS_STATUS_SUCCESS,
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
                nblContext
            );
        }
#endif
        ((PMPUSB_TX_CONTEXT_NBL)nbContext->NBL)->NdisStatus = NDIS_STATUS_SUCCESS;
        MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, FALSE);
        }

    NdisReleaseSpinLock(&pAdapter->TxLock);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> [TLP] PurgeTLPQueues: Purged %d/%d\n", pAdapter->PortName, i, nPkt)
    );
    }  // MPUSB_PurgeTLPQueues

VOID MPUSB_TLPTxPacketEx
(
    PMP_ADAPTER  pAdapter,
    ULONG        QosFlowId,
    BOOLEAN      IsQosPacket,
    PLIST_ENTRY  pList
)
{

    PIRP                pIrp;
    PIO_STACK_LOCATION  nextstack;
    NDIS_STATUS         Status;
    UINT                dwPacketLength;
    PVOID               usbBuffer = NULL;
#ifdef MP_QCQOS_ENABLED
    PMPQOS_HDR          qosHdr;
    PMPQOS_HDR8         qosHdr8;
#endif // MP_QCQOS_ENABLED
    long                sendBytes;
    BOOLEAN             kicker = FALSE;
    PNET_BUFFER_LIST      netBufferList;
    PNET_BUFFER           netBuffer;
    PMPUSB_TX_CONTEXT_NBL txContext = NULL;
    PMPUSB_TX_CONTEXT_NB  nbContext;
    PUCHAR                dataPkt;
    BOOLEAN               bAbort = FALSE;

    PNDIS_BUFFER pCurrentNdisBuffer;
    UINT         dwTotalBufferCount;
    PUCHAR       pCurrentBuffer;
    ULONG        dwCurrentBufferLength;
    ULONG       copyOk;
    NDIS_STATUS ndisSt = NDIS_STATUS_FAILURE;
    PTLP_BUF_ITEM tlpItem = NULL;
    PLIST_ENTRY   headOfList;
    PMP_ADAPTER returnAdapter;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> --->[TLP] TLPTxPacketEx 0x%p\n", pAdapter->PortName, pList)
    );

    // obtain TLP item
    NdisAcquireSpinLock(&pAdapter->TxLock);

    if (!IsListEmpty(&pAdapter->UplinkFreeBufferQueue))
    {
        headOfList = RemoveHeadList(&pAdapter->UplinkFreeBufferQueue);
        tlpItem = CONTAINING_RECORD(headOfList, TLP_BUF_ITEM, List);
    }

    NdisReleaseSpinLock(&pAdapter->TxLock);

    if (pList == NULL) // send
    {
        goto PrepareIrp;
    }

    nbContext = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NB, List);
    txContext = nbContext->NBL;
    netBufferList = txContext->NBL;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> TLPTxPacketEx: NBL 0x%p QNBL 0x%p\n", pAdapter->PortName,
        netBufferList, txContext)
    );

    netBuffer = nbContext->NB;

    // Take it out.... TODO : review....
    if ((netBuffer == NULL) || (bAbort == TRUE))
    {
        kicker = TRUE;
        goto Tx_Exit;
    }

    InterlockedIncrement(&(txContext->NumProcessed));

    if (txContext->AbortFlag != 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> TLPTxPacketEx: AbortFlag set for QNBL 0x%p\n",
            pAdapter->PortName, txContext)
        );
        nbContext->Completed = 1;
        bAbort = TRUE;
        txContext->TxFramesDropped += 1;
        kicker = TRUE;
        goto Tx_Exit;
    }

    // 1. Query NDIS packet
    copyOk = MPUSB_QueryDataPacket(pAdapter, netBuffer, &dwPacketLength);
    if (copyOk == 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> TLPTxPacketEx: failed to get pkt for QNBL 0x%p\n",
            pAdapter->PortName, txContext)
        );
        nbContext->Completed = 1;
        bAbort = TRUE;
        txContext->TxFramesDropped += 1;
        kicker = TRUE;
        goto Tx_Exit;
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> TLPTxPacketEx: =>NB 0x%p (%dB)\n", pAdapter->PortName, netBuffer, dwPacketLength)
    );
    nbContext->DataLength = dwPacketLength;

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            sendBytes = dwPacketLength + sizeof(MPQOS_HDR8);
        }
        else
        {
            sendBytes = dwPacketLength + sizeof(MPQOS_HDR);
        }
    }
    else
#endif // MP_QCQOS_ENABLED
    {
        sendBytes = dwPacketLength;
    }

    // obtain buffer
    if (tlpItem == NULL)
    {
        usbBuffer = NULL;
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [TLP] TLPTxPacketEx: no more TLP buffer \n", pAdapter->PortName)
        );
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [TLP] TLPTxPacketEx => (%dB/%dB/%dB)\n", pAdapter->PortName,
            dwPacketLength, sendBytes, tlpItem->RemainingCapacity)
        );
        if (pAdapter->QMAPEnabledV4 == TRUE)
        {
            if (tlpItem->RemainingCapacity > (sendBytes + 8 + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM)))
            {
                usbBuffer = tlpItem->CurrentPtr + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM); // room for length
            }
            else
            {
                usbBuffer = NULL;
            }
        }
        else
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
            if ((pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE))
            {
                if (tlpItem->RemainingCapacity > (sendBytes + 8 + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM)))
                {
                    usbBuffer = tlpItem->CurrentPtr + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM); // room for length
                }
                else
                {
                    usbBuffer = NULL;
                }
            }
            else
#endif
#endif
                if (pAdapter->QMAPEnabledV1 == TRUE)
                {
                    if (tlpItem->RemainingCapacity > (sendBytes + 8 + sizeof(QMAP_STRUCT)))
                    {
                        usbBuffer = tlpItem->CurrentPtr + sizeof(QMAP_STRUCT); // room for length
                    }
                    else
                    {
                        usbBuffer = NULL;
                    }
                }
                else if (pAdapter->MBIMULEnabled == TRUE)
                {
                    if (tlpItem->Buffer == tlpItem->CurrentPtr)
                    {
                        if ((tlpItem->RemainingCapacity > sendBytes +
                            sizeof(NTB_16BIT_HEADER) +
                            sizeof(NDP_16BIT_HEADER) +
                            sizeof(DATAGRAM_STRUCT) * (tlpItem->AggregationCount + 2) + 8) &&
                            (tlpItem->AggregationCount < (MP_NUM_MBIM_UL_DATAGRAM_ITEMS_DEFAULT - 2)))
                        {
                            usbBuffer = tlpItem->CurrentPtr + sizeof(NTB_16BIT_HEADER);
                        }
                        else
                        {
                            usbBuffer = NULL;
                        }
                    }
                    else if ((tlpItem->RemainingCapacity > sendBytes +
                             sizeof(NDP_16BIT_HEADER) +
                             sizeof(DATAGRAM_STRUCT) * (tlpItem->AggregationCount + 2) + 8) &&
                             (tlpItem->AggregationCount < (MP_NUM_MBIM_UL_DATAGRAM_ITEMS_DEFAULT - 2)))
                    {
                        usbBuffer = tlpItem->CurrentPtr;
                    }
                    else
                    {
                        usbBuffer = NULL;
                    }
                }
                else if (tlpItem->RemainingCapacity > sendBytes + 8)
                {
                    if (pAdapter->TLPEnabled == TRUE)
                    {
                        usbBuffer = tlpItem->CurrentPtr + sizeof(USHORT); // room for length
                    }
                    else
                    {
                        usbBuffer = tlpItem->CurrentPtr;
                    }
                }
                else
                {
                    usbBuffer = NULL;
                }
    }

    if (usbBuffer == NULL)
    {
        Status = NDIS_STATUS_FAILURE;
    }
    else
    {
        Status = NDIS_STATUS_SUCCESS;
    }

    if (Status != NDIS_STATUS_SUCCESS)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> TLPTxPacketEx failed to obtain buffer\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }

    // 1. Retrieve NDIS packet
    dataPkt = MPUSB_RetriveDataPacket(pAdapter, netBuffer, &dwPacketLength, usbBuffer);
    if (dataPkt == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> TLPTxPacketEx: failed to get pkt for QNBL 0x%p\n",
            pAdapter->PortName, txContext)
        );
        nbContext->Completed = 1;
        bAbort = TRUE;
        txContext->TxFramesDropped += 1;
        kicker = TRUE;
        goto Tx_Exit;
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> TLPTxPacketEx: =>NBL 0x%p (%dB)\n", pAdapter->PortName, netBuffer, dwPacketLength)
    );

    nbContext->DataLength = dwPacketLength;

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            sendBytes = dwPacketLength + sizeof(MPQOS_HDR8);
        }
        else
        {
            sendBytes = dwPacketLength + sizeof(MPQOS_HDR);
        }
    }
    else
#endif // MP_QCQOS_ENABLED
    {
        sendBytes = dwPacketLength;
    }

#ifdef QC_IP_MODE
    if (pAdapter->IPModeEnabled == TRUE)
    {
        if ((QCMP_NDIS620_Ok == FALSE) || (pAdapter->NdisMediumType == NdisMedium802_3))
        {
            if (FALSE == MPUSB_PreparePacket(pAdapter, usbBuffer, (PULONG)&sendBytes))
            {
                // TODO - revisit (this is local processing, not error!)
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_WRITE,
                    MP_DBG_LEVEL_DETAIL,
                    ("<%s> IPO: _PreparePacket: local process %dB\n", pAdapter->PortName, sendBytes)
                );
                nbContext->Completed = 1;
                txContext->TxFramesDropped += 1;
                bAbort = TRUE;
            }
            else
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_WRITE,
                    MP_DBG_LEVEL_TRACE,
                    ("<%s> IPO: _PreparePacket: to USB %dB\n", pAdapter->PortName, sendBytes)
                );
            }
        }
    }
#endif // QC_IP_MODE


    // Update TLP item
    {

        if ((pAdapter->QMAPEnabledV1 == TRUE)
            || (pAdapter->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
            || (pAdapter->QMAPEnabledV2 == TRUE)
            || (pAdapter->QMAPEnabledV3 == TRUE)
#endif
#endif
            )
        {
            PQMAP_STRUCT Qmap;
            UCHAR paddingBytes = 0;
            Qmap = (PQMAP_STRUCT)(tlpItem->CurrentPtr);
            RtlZeroMemory(Qmap, sizeof(QMAP_STRUCT));
            Qmap->PadCD = 0x00; //TODO: check the order
            // Padding //TODO: check the order
            if (pAdapter->QMAPEnabledV4 != TRUE)
            {
                if ((sendBytes % 4) > 0)
                {
                    paddingBytes = 4 - (sendBytes % 4);
                    RtlZeroMemory((PUCHAR)((PUCHAR)usbBuffer + sendBytes), paddingBytes);
                }
            }
            Qmap->PadCD |= (paddingBytes);
            Qmap->MuxId = pAdapter->MuxId;
            sendBytes += paddingBytes;
            Qmap->PacketLen = RtlUshortByteSwap(sendBytes);
            if (pAdapter->QMAPEnabledV4 == TRUE)
            {
                PQMAP_UL_CHECKSUM pULCheckSum = (PQMAP_UL_CHECKSUM)((PUCHAR)Qmap + sizeof(QMAP_STRUCT));
                RtlZeroMemory(pULCheckSum, sizeof(QMAP_UL_CHECKSUM));
                tlpItem->CurrentPtr += (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
                tlpItem->DataLength += (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
                tlpItem->RemainingCapacity -= (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
            }
            else
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
                if ((pAdapter->QMAPEnabledV2 == TRUE) || (pAdapter->QMAPEnabledV3 == TRUE))
                {
                    PUCHAR ipPtr = usbBuffer;
                    UCHAR ipVer;
                    USHORT CkOffset = 0;
                    PUSHORT ckOffsetPtr;
                    PQMAP_UL_CHECKSUM pULCheckSum = (PQMAP_UL_CHECKSUM)((PUCHAR)Qmap + sizeof(QMAP_STRUCT));

#ifdef MP_QCQOS_ENABLED
                    if (pAdapter->QosEnabled == TRUE)
                    {
                        if (pAdapter->QosHdrFmt == 0x02)
                        {
                            ipPtr += sizeof(MPQOS_HDR8);
                        }
                        else
                        {
                            ipPtr += sizeof(MPQOS_HDR);
                        }
                    }
#endif // MP_QCQOS_ENABLED

                    RtlZeroMemory(pULCheckSum, sizeof(QMAP_UL_CHECKSUM));
                    ipVer = (*ipPtr & 0xF0) >> 4;
                    if (ipVer == 4)
                    {
                        PQMAP_IPV4 ipV4;
                        IPV4_FRAG_PKT fragPkt;
                        QMAP_PSEUDO_CHECKSUM pseudoChkSum;
                        UCHAR FragPacket = 0x00;
                        /* IP v4 */
                        pULCheckSum->CksumStartOffset = RtlUshortByteSwap((*ipPtr & 0x0F) * 4);
                        /* Bit 0 CHK enable */
                        pULCheckSum->CksumInsertOffsetCU = 0x8000;
                        /* Bit 1 IPv4/UDP enable */
                        pULCheckSum->CksumInsertOffsetCU |= 0x4000;


                        MPMAIN_PrintBytes
                        (
                            ipPtr, 64, sendBytes, "IPO: QMAP : IPPKT", pAdapter,
                            MP_DBG_MASK_DATA_WT, MP_DBG_LEVEL_VERBOSE
                        );

                        ipV4 = (PQMAP_IPV4)ipPtr;
                        pseudoChkSum.DestAddr = ipV4->DestAddr;
                        pseudoChkSum.SrcAddr = ipV4->SrcAddr;
                        pseudoChkSum.Zeros = 0x00;
                        pseudoChkSum.Protocol = ipV4->Protocol;
                        pseudoChkSum.Length = RtlUshortByteSwap(RtlUshortByteSwap(ipV4->TotaLlen) - ((*ipPtr & 0x0F) * 4));

                        MPMAIN_PrintBytes
                        (
                            &pseudoChkSum, 64, sizeof(QMAP_PSEUDO_CHECKSUM), "IPO: QMAP : PSEUDOHEADER", pAdapter,
                            MP_DBG_MASK_DATA_WT, MP_DBG_LEVEL_VERBOSE
                        );

                        if (ipV4->Protocol == 0x06) /* TCP */
                        {
                            CkOffset = ((*ipPtr & 0x0F) * 4) + (sizeof(QMAP_TCP) - sizeof(USHORT));
                        }
                        else if (ipV4->Protocol == 0x11) /* UDP */
                        {
                            CkOffset = ((*ipPtr & 0x0F) * 4) + (sizeof(QMAP_UDP) - sizeof(USHORT));
                        }
                        else if (ipV4->Protocol == 0x01) /* ICMP */
                        {
                            CkOffset = ((*ipPtr & 0x0F) * 4) + (sizeof(QMAP_ICMP) - sizeof(USHORT));
                        }

                        fragPkt.Rsvd2 = RtlUshortByteSwap(ipV4->Rsvd2);

                        if (fragPkt.Flags & 0x01)
                        {
                            FragPacket = 0x01;
                        }
                        if (fragPkt.Offset != 0)
                        {
                            FragPacket = 0x01;
                        }

                        QCNET_DbgPrint
                        (
                            MP_DBG_MASK_READ,
                            MP_DBG_LEVEL_DETAIL,
                            ("<%s> MPUSB_TLPTxPacketEx: FragPacket Flag 0x%x and Offset 0x%x\n", pAdapter->PortName, fragPkt.Flags, fragPkt.Offset)
                        );

                        if ((CkOffset != 0x00) && (FragPacket != 0x01))
                        {
                            ckOffsetPtr = (PUSHORT)(ipPtr + CkOffset);
                            pULCheckSum->CksumInsertOffsetCU |= CkOffset;
                            pULCheckSum->CksumInsertOffsetCU = RtlUshortByteSwap(pULCheckSum->CksumInsertOffsetCU);
                            if (ipV4->Protocol != 0x01) /* NOT ICMP */
                            {
                                *ckOffsetPtr = CheckSum((PUSHORT)(&pseudoChkSum), sizeof(pseudoChkSum)); /* TODO: big endian*/
                            }
                            else
                            {
                                *ckOffsetPtr = 0;
                            }

                            QCNET_DbgPrint
                            (
                                MP_DBG_MASK_WRITE,
                                MP_DBG_LEVEL_ERROR,
                                ("<%s> MPUSB_TLPTxPacket: Checksumoffsetptr 0x%p, Checksum Value 0x%x offset 0x%x\n",
                                pAdapter->PortName, ckOffsetPtr, *ckOffsetPtr, CkOffset)
                            );

                        }
                        else
                        {
                            /* TODO: debug message */
                            QCNET_DbgPrint
                            (
                                MP_DBG_MASK_WRITE,
                                MP_DBG_LEVEL_ERROR,
                                ("<%s> MPUSB_TLPTxPacket: Bad IP packet or fragmented type 0x%x\n",
                                pAdapter->PortName, ipV4->Protocol)
                            );

                            /* Bit 0 CHK enable clear */
                            pULCheckSum->CksumInsertOffsetCU &= 0x7FFF;
                        }
                    }
                    else
                    {
                        PQMAP_IPV6 ipV6;
                        QMAP_PSEUDO_CHECKSUMV6 pseudoChkSum;
                        /* IP v6 */
                        pULCheckSum->CksumStartOffset = RtlUshortByteSwap(sizeof(QMAP_IPV6));
                        /* Bit 0 CHK enable */
                        pULCheckSum->CksumInsertOffsetCU = 0x8000;

                        ipV6 = (PQMAP_IPV6)ipPtr;
                        RtlCopyMemory(pseudoChkSum.DestAddr, ipV6->DestAddr, sizeof(ipV6->DestAddr));
                        RtlCopyMemory(pseudoChkSum.SrcAddr, ipV6->SrcAddr, sizeof(ipV6->SrcAddr));
                        RtlZeroMemory(pseudoChkSum.Zeros, 0x03);
                        pseudoChkSum.payloadLen = ipV6->payloadLen;
                        pseudoChkSum.NextHdr = ipV6->nextHdr;

                        if (ipV6->nextHdr == 0x06)  /* TCP */
                        {
                            /* Assuming that IP header is fixed to 40Bytes */
                            CkOffset = sizeof(QMAP_IPV6) + (sizeof(QMAP_TCP) - sizeof(USHORT));
                        }
                        else if (ipV6->nextHdr == 0x11) /* UDP */
                        {
                            CkOffset = sizeof(QMAP_IPV6) + (sizeof(QMAP_UDP) - sizeof(USHORT));
                            /* Bit 1 IPv4/UDP enable */
                            pULCheckSum->CksumInsertOffsetCU |= 0x4000;
                        }
                        else if (ipV6->nextHdr == 0x01) /* ICMP */
                        {
                            CkOffset = sizeof(QMAP_IPV6) + (sizeof(QMAP_ICMP) - sizeof(USHORT));
                        }
                        if (CkOffset != 0x00)
                        {
                            ckOffsetPtr = (PUSHORT)(ipPtr + CkOffset);
                            pULCheckSum->CksumInsertOffsetCU |= CkOffset;
                            pULCheckSum->CksumInsertOffsetCU = RtlUshortByteSwap(pULCheckSum->CksumInsertOffsetCU);
                            *ckOffsetPtr = CheckSum((PUSHORT)(&pseudoChkSum), sizeof(pseudoChkSum));
                        }
                        else
                        {
                            /* TODO: debug message */
                            QCNET_DbgPrint
                            (
                                MP_DBG_MASK_WRITE,
                                MP_DBG_LEVEL_ERROR,
                                ("<%s> TLPTxPacketEx: Bad IP packet or fragmented type 0x%x\n",
                                pAdapter->PortName, ipV6->nextHdr)
                            );

                            /* Bit 0 CHK enable clear */
                            pULCheckSum->CksumInsertOffsetCU &= 0x7FFF;
                        }
                    }
                    tlpItem->CurrentPtr += (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
                    tlpItem->DataLength += (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
                    tlpItem->RemainingCapacity -= (sendBytes + sizeof(QMAP_STRUCT) + sizeof(QMAP_UL_CHECKSUM));
                    // update the checsum header here...
                }
                else
#endif
#endif
                {
                    tlpItem->CurrentPtr += (sendBytes + sizeof(QMAP_STRUCT));
                    tlpItem->DataLength += (sendBytes + sizeof(QMAP_STRUCT));
                    tlpItem->RemainingCapacity -= (sendBytes + sizeof(QMAP_STRUCT));
                }
        }
        else if (pAdapter->MBIMULEnabled == TRUE)
        {
            if (tlpItem->Buffer == tlpItem->CurrentPtr)
            {
                tlpItem->pNtbHeader = (PNTB_16BIT_HEADER)tlpItem->CurrentPtr;
                tlpItem->pNtbHeader->dwSignature = 0x484D434E; //NCMH
                tlpItem->pNtbHeader->wHeaderLength = sizeof(NTB_16BIT_HEADER);
                tlpItem->pNtbHeader->wSequence = (USHORT)pAdapter->ntbSequence;
                tlpItem->pNtbHeader->wBlockLength = sizeof(NTB_16BIT_HEADER);
                tlpItem->pNtbHeader->wNdpIndex = 0;
                tlpItem->CurrentPtr += sizeof(NTB_16BIT_HEADER);
                tlpItem->DataLength += sizeof(NTB_16BIT_HEADER);
                tlpItem->RemainingCapacity -= sizeof(NTB_16BIT_HEADER);

                // tlpItem->NdpHeader.dwSignature = 0x50444E51; //QNDP
                // tlpItem->NdpHeader.dwSignature = 0x344D434E; //NCM4
                // tlpItem->NdpHeader.dwSignature = 0x00535049; //IPS0
                tlpItem->NdpHeader.dwSignature = pAdapter->ndpSignature;

                if ((tlpItem->NdpHeader.dwSignature & 0x00FFFFFF) == 0x00535049)
                {
                    ULONG MuxId = pAdapter->MuxId;
                    MuxId = (MuxId << 24);
                    tlpItem->NdpHeader.dwSignature = ((tlpItem->NdpHeader.dwSignature & 0x00FFFFFF) | MuxId);
                }

                tlpItem->NdpHeader.wNextNdpIndex = 0x00;

                if (InterlockedIncrement(&pAdapter->ntbSequence) >= 0xFFFF)
                {
                    pAdapter->ntbSequence = 0x00;
                }
            }
            tlpItem->DataGrams[tlpItem->AggregationCount].DatagramPtr = (tlpItem->CurrentPtr - tlpItem->Buffer);
            tlpItem->DataGrams[tlpItem->AggregationCount].DatagramLen = sendBytes;
            tlpItem->CurrentPtr += sendBytes;
            tlpItem->DataLength += sendBytes;
            tlpItem->RemainingCapacity -= sendBytes;

            tlpItem->pNtbHeader->wBlockLength += sendBytes;

        }
        else if (pAdapter->TLPEnabled == TRUE)
        {
            PUSHORT tlpLen;
            tlpLen = (PUSHORT)(tlpItem->CurrentPtr);
            *tlpLen = sendBytes; // write pkt length
            tlpItem->CurrentPtr += (sendBytes + sizeof(USHORT));
            tlpItem->DataLength += (sendBytes + sizeof(USHORT));
            tlpItem->RemainingCapacity -= (sendBytes + sizeof(USHORT));
        }
        else
        {
            tlpItem->CurrentPtr += sendBytes;
            tlpItem->DataLength += sendBytes;
            tlpItem->RemainingCapacity -= sendBytes;
        }
        if (pAdapter->MPQuickTx == 0)
        {
            // only one NBL should be added to the packet lists
            InsertTailList(&tlpItem->PacketList, pList);
        }
    }

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->QosHdrFmt == 0x02)
        {
            qosHdr8 = (PMPQOS_HDR8)usbBuffer;
            qosHdr8->Version = 1;
            qosHdr8->Reserved = 0;
            if (IsQosPacket == TRUE)
            {
                qosHdr8->FlowId = QosFlowId;
            }
            else
            {
                qosHdr8->FlowId = 0;
            }
            qosHdr8->Rsvd[0] = 0;
            qosHdr8->Rsvd[1] = 0;
        }
        else
        {
            qosHdr = (PMPQOS_HDR)usbBuffer;
            qosHdr->Version = 1;
            qosHdr->Reserved = 0;
            if (IsQosPacket == TRUE)
            {
                qosHdr->FlowId = QosFlowId;
            }
            else
            {
                qosHdr->FlowId = 0;
            }
        }
        NdisAcquireSpinLock(&pAdapter->TxLock);
        pAdapter->QosPendingPackets++;
        NdisReleaseSpinLock(&pAdapter->TxLock);
    }
#endif // MP_QCQOS_ENABLED

    tlpItem->AggregationCount++;
    if ((pAdapter->QMAPEnabledV1 == TRUE)
        || (pAdapter->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
        || (pAdapter->QMAPEnabledV2 == TRUE)
        || (pAdapter->QMAPEnabledV3 == TRUE)
#endif
#endif
        )
    {
        // stay for aggregation
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> TLPTxPacketEx: stay for aggregation, restore item [%d] %dB(%d)\n",
            pAdapter->PortName, tlpItem->Index, tlpItem->DataLength, tlpItem->AggregationCount)
        );
        NdisAcquireSpinLock(&pAdapter->TxLock);
        InsertHeadList(&pAdapter->UplinkFreeBufferQueue, &tlpItem->List);
        NdisReleaseSpinLock(&pAdapter->TxLock);

        // Set the transmit timer
        SetTransmitTimer(pAdapter);

        if (pAdapter->MPQuickTx != 0)
        {
            ((PMPUSB_TX_CONTEXT_NBL)nbContext->NBL)->NdisStatus = NDIS_STATUS_SUCCESS;
            MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, TRUE);
        }
        goto Tx_Exit;

    }
    else if ((tlpItem->AggregationCount < (MP_NUM_MBIM_UL_DATAGRAM_ITEMS_DEFAULT - 2)) && (pAdapter->MBIMULEnabled == TRUE))
    {
        // stay for aggregation
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> TLPTxPacketEx: stay for aggregation, restore item [%d] %dB(%d)\n",
            pAdapter->PortName, tlpItem->Index, tlpItem->DataLength, tlpItem->AggregationCount)
        );
        NdisAcquireSpinLock(&pAdapter->TxLock);
        InsertHeadList(&pAdapter->UplinkFreeBufferQueue, &tlpItem->List);
        NdisReleaseSpinLock(&pAdapter->TxLock);

        // Set the transmit timer
        SetTransmitTimer(pAdapter);

        if (pAdapter->MPQuickTx != 0)
        {
            ((PMPUSB_TX_CONTEXT_NBL)nbContext->NBL)->NdisStatus = NDIS_STATUS_SUCCESS;
            MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, TRUE);
        }
        goto Tx_Exit;
    }
    else if ((pAdapter->TLPEnabled == TRUE))
    {
        // stay for aggregation
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> TLPTxPacketEx: stay for aggregation, restore item [%d] %dB(%d)\n",
            pAdapter->PortName, tlpItem->Index, tlpItem->DataLength, tlpItem->AggregationCount)
        );
        NdisAcquireSpinLock(&pAdapter->TxLock);
        InsertHeadList(&pAdapter->UplinkFreeBufferQueue, &tlpItem->List);
        NdisReleaseSpinLock(&pAdapter->TxLock);

        // Set the transmit timer
        SetTransmitTimer(pAdapter);

        if (pAdapter->MPQuickTx != 0)
        {
            ((PMPUSB_TX_CONTEXT_NBL)nbContext->NBL)->NdisStatus = NDIS_STATUS_SUCCESS;
            MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, TRUE);
        }
        goto Tx_Exit;
    }
    else
    {
        if (pAdapter->MPQuickTx != 0)
        {
            ((PMPUSB_TX_CONTEXT_NBL)nbContext->NBL)->NdisStatus = NDIS_STATUS_SUCCESS;
            MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, TRUE);
        }

        if ((pAdapter->TLPEnabled == TRUE) ||
            (pAdapter->MBIMULEnabled == TRUE) ||
            (pAdapter->QMAPEnabledV1 == TRUE)
            || (pAdapter->QMAPEnabledV4 == TRUE)
#ifdef QCUSB_MUX_PROTOCOL                        
#if defined(QCMP_QMAP_V2_SUPPORT)
            || (pAdapter->QMAPEnabledV2 == TRUE)
            || (pAdapter->QMAPEnabledV3 == TRUE)
#endif
#endif
            )
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE, MP_DBG_LEVEL_DETAIL,
                ("<%s> ---> TLPTxPacketEx: Flush due to Buffer Full %d\n", pAdapter->PortName, pAdapter->FlushBufferBufferFull++)
            );
            CancelTransmitTimer(pAdapter);
        }
    }

PrepareIrp:

    if (tlpItem == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> TLPTxPacketEx: nothing to send\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }

    if (tlpItem->DataLength == 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> TLPTxPacketEx: 0 TX data, extra flush\n", pAdapter->PortName)
        );
        kicker = TRUE;
        goto Tx_Exit;
    }

    // Copy the NDP header and update the lengths correctly
    if (pAdapter->MBIMULEnabled == TRUE)
    {
        ULONG Length = (sizeof(NDP_16BIT_HEADER) + ((tlpItem->AggregationCount + 1) * sizeof(DATAGRAM_STRUCT)));
        tlpItem->NdpHeader.wLength += Length;
        RtlCopyMemory(tlpItem->CurrentPtr, (PUCHAR)&tlpItem->NdpHeader, Length);
        tlpItem->DataLength += Length;
        tlpItem->RemainingCapacity -= Length;
        tlpItem->pNtbHeader->wBlockLength += Length;
        tlpItem->pNtbHeader->wNdpIndex = (tlpItem->CurrentPtr - tlpItem->Buffer);

        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> [TLP] MBIM_UL => (%d)\n", pAdapter->PortName, tlpItem->AggregationCount)
        );

        MPMAIN_PrintBytes
        (
            &tlpItem->NdpHeader, 64, Length, "IPO: MBIM_UL_NDP->D", pAdapter,
            MP_DBG_MASK_DATA_WT, MP_DBG_LEVEL_VERBOSE
        );

    }

    // Prepare IRP
    pIrp = tlpItem->Irp;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_VERBOSE,
        ("<%s> [TLP] TLPTxPacketEx: sending [%d] %dB/%dB\n", pAdapter->PortName,
        tlpItem->Index, tlpItem->DataLength, tlpItem->RemainingCapacity)
    );
    if (pAdapter->IPModeEnabled == TRUE)
    {
        MPMAIN_PrintBytes
        (
            tlpItem->Buffer, 64, tlpItem->DataLength, "IPO: TLPIP->D", pAdapter,
            MP_DBG_MASK_DATA_WT, MP_DBG_LEVEL_VERBOSE
        );
    }

    IoReuseIrp(pIrp, STATUS_SUCCESS);

    pIrp->MdlAddress = NULL;
    pIrp->AssociatedIrp.SystemBuffer = tlpItem->Buffer;
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_WRITE;
    nextstack->Parameters.Write.Length = tlpItem->DataLength;

    // Init IRP
    IoSetCompletionRoutine
    (
        pIrp,
        MPUSB_TLPTxIRPCompleteEx,
        tlpItem,
        TRUE, TRUE, TRUE
    );

    // use spin lock to synchronize with the queue manipulation
    NdisAcquireSpinLock(&pAdapter->TxLock);
    InterlockedIncrement(&(pAdapter->TxTLPPendinginUSB));
    NdisReleaseSpinLock(&pAdapter->TxLock);

    returnAdapter = FindStackDeviceObject(pAdapter);
    if (returnAdapter != NULL)
    {
        Status = QCIoCallDriver(returnAdapter->USBDo, pIrp);
    }
    else
    {
        Status = STATUS_UNSUCCESSFUL;
        pIrp->IoStatus.Status = Status;
        MPUSB_TLPTxIRPCompleteEx(pAdapter->USBDo, pIrp, tlpItem);
    }

Tx_Exit:

    if (kicker == TRUE)
    {
#ifdef QC_IP_MODE
        if (pAdapter->IPModeEnabled == TRUE)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_TRACE,
                ("<%s> IPO: TLPTxPacketEx: complete (0x%x)\n", pAdapter->PortName,
                ndisSt)
            );
        }
#endif // QC_IP_MODE

        if (pList != NULL)
        {
            NdisAcquireSpinLock(&pAdapter->TxLock);

            if (pAdapter->MPQuickTx == 0)
            {
                // remove from TLP PacketList (if it's ever en-queued)
                RemoveEntryList(pList);
            }

            NdisReleaseSpinLock(&pAdapter->TxLock);

            nbContext = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NB, List);
            ((PMPUSB_TX_CONTEXT_NBL)nbContext->NBL)->NdisStatus = NDIS_STATUS_SUCCESS;
            MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, TRUE);
        }
        if (tlpItem != NULL)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_WRITE,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> [TLP] TLPTxPacketEx: restore item [%d]\n", pAdapter->PortName,
                tlpItem->Index)
            );
            NdisAcquireSpinLock(&pAdapter->TxLock);
            InsertHeadList(&pAdapter->UplinkFreeBufferQueue, &tlpItem->List);
            NdisReleaseSpinLock(&pAdapter->TxLock);
        }
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--TLPTxPacketEx (nBusyTx: %d/%dP/%dP) %d\n", pAdapter->PortName,
        pAdapter->nBusyTx, pAdapter->QosPendingPackets, pAdapter->MPPendingPackets, kicker)
    );
    return;
}  // MPUSB_TLPTxPacket

NTSTATUS MPUSB_TLPTxIRPCompleteEx
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    PTLP_BUF_ITEM tlpItem = (PTLP_BUF_ITEM)pContext;
    PMP_ADAPTER   pAdapter;
    NDIS_STATUS   status;
    PLIST_ENTRY   pList;
    INT           nPkt = 0;

    pAdapter = (PMP_ADAPTER)tlpItem->Adapter;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> -->TLPTxIRPComplete [%d(%d)] 0x%p (0x%x)\n", pAdapter->PortName,
        tlpItem->Index, tlpItem->AggregationCount, pIrp, pIrp->IoStatus.Status)
    );

    NdisAcquireSpinLock(&pAdapter->TxLock);

    // tlpItem->PacketList is empty if MPQuickTx is TRUE
    while (!IsListEmpty(&tlpItem->PacketList))
    {

        PMPUSB_TX_CONTEXT_NB nbContext = NULL;
        pList = RemoveHeadList(&tlpItem->PacketList);
        nbContext = CONTAINING_RECORD(pList, MPUSB_TX_CONTEXT_NB, List);
        nPkt++;
        if (NT_SUCCESS(pIrp->IoStatus.Status))
        {
#if 0      
            MPUSB_CompleteNetBufferList
            (
                pAdapter,
                nblContext->NBL,
                NDIS_STATUS_SUCCESS,
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
                nblContext
            );
#endif
            ((PMPUSB_TX_CONTEXT_NBL)nbContext->NBL)->NdisStatus = NDIS_STATUS_SUCCESS;
            MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, FALSE);
        }
        else
        {
#if 0      
            MPUSB_CompleteNetBufferList
            (
                pAdapter,
                nblContext->NBL,
                NDIS_STATUS_FAILURE,
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL,
                nblContext
            );
#endif
            ((PMPUSB_TX_CONTEXT_NBL)nbContext->NBL)->NdisStatus = NDIS_STATUS_FAILURE;
            MP_TxIRPCompleteEx(pAdapter->USBDo, NULL, nbContext, FALSE);
        }
        nPkt++;
        if (pAdapter->QosEnabled == TRUE)
        {
            pAdapter->QosPendingPackets--;
        }
    }

    if (pAdapter->QosEnabled == TRUE)
    {
        if (pAdapter->MPQuickTx != 0)
        {
            // pAdapter->QosPendingPackets -= tlpItem->AggregationCount; //murali TLP

            // use the following instead of an assignment statement for error
            // verification purpose (to make sure if tlpItem->PacketList
            // is actually empty
            nPkt += tlpItem->AggregationCount;
        }
    }

    // recycle TLP item
    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> [TLP] _TLPTxIRPComplete: restore item [%d]\n", pAdapter->PortName,
        tlpItem->Index)
    );
    tlpItem->CurrentPtr = tlpItem->Buffer;
    tlpItem->DataLength = 0;
    tlpItem->RemainingCapacity = pAdapter->UplinkTLPSize;
    tlpItem->AggregationCount = 0;
    RtlZeroMemory(&(tlpItem->NdpHeader), sizeof(NDP_16BIT_HEADER) + sizeof(tlpItem->DataGrams));

    InsertTailList(&pAdapter->UplinkFreeBufferQueue, &tlpItem->List);
    InterlockedDecrement(&(pAdapter->TxTLPPendinginUSB));

    NdisReleaseSpinLock(&pAdapter->TxLock);

    if (nPkt != 0)
    {
        MPWork_ScheduleWorkItem(pAdapter);
    }

#ifdef MP_QCQOS_ENABLED
    if (pAdapter->QosEnabled == TRUE)
    {
#ifdef MPQOS_PKT_CONTROL
        if (pAdapter->QosPendingPackets < pAdapter->MPPendingPackets)
        {
            KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT, FALSE);
        }
#else
        KeSetEvent(&pAdapter->QosDispThreadTxEvent, IO_NO_INCREMENT, FALSE);
#endif // MPQOS_PKT_CONTROL
        }
    else
#endif // MP_QCQOS_ENABLED
    {
        // buffer available, kick TX
        KeSetEvent(&pAdapter->MPThreadTxEvent, IO_NO_INCREMENT, FALSE);
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MP_TLPTxIRPComplete (nBusyTx: %d/%dP) done %d\n",
        pAdapter->PortName, pAdapter->nBusyTx, pAdapter->QosPendingPackets, nPkt)
    );
    return STATUS_MORE_PROCESSING_REQUIRED;
    }  // MPUSB_TLPTxIRPComplete

#endif
BOOLEAN MPUSB_TLPIndicateCompleteTx(PMP_ADAPTER pAdapter, PNDIS_PACKET pNdisPacket, UCHAR Cookie)
{
    NDIS_STATUS  ndisStatus;

    InterlockedDecrement(&(pAdapter->nBusyTx));
    ndisStatus = NDIS_GET_PACKET_STATUS(pNdisPacket);

    if (ndisStatus == NDIS_STATUS_SUCCESS)
    {
        ++(pAdapter->GoodTransmits);
    }
    else
    {
        ++(pAdapter->BadTransmits);
    }
    NdisMSendComplete(pAdapter->AdapterHandle, pNdisPacket, ndisStatus);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_WRITE,
        MP_DBG_LEVEL_TRACE,
        ("<%s> TLPIndicateCompleteTx completed-%d 0x%p (nBusyTx %d)\n",
        pAdapter->PortName, Cookie, pNdisPacket, pAdapter->nBusyTx)
    );
    return TRUE;
}  // MPUSB_TLPIndicateCompleteTx

#endif // QCMP_UL_TLP || QCMP_MBIM_UL_SUPPORT

#ifdef QCUSB_MUX_PROTOCOL
#ifdef QMI_OVER_DATA

BOOLEAN MPUSB_DispatchQMI
(
    PMP_ADAPTER pAdapter,
    PVOID       DataBuffer,
    ULONG       DataLength,
    NTSTATUS    Status
)
{
    PQMI_PREAMBLE preamble;
    PUCHAR        qmiData;
    ULONG         qmiLength;
    PLIST_ENTRY   pList;
    PMP_OID_READ  pOID;
    ULONG         rsv3;
    int           minLength;
    static UCHAR  qmiPreamble[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> -->_DispatchQMI (nBusyRCtrl: %d)\n", pAdapter->PortName, pAdapter->nBusyRCtrl)
    );

#ifdef QC_IP_MODE
    if (pAdapter->IPModeEnabled == TRUE)
    {
        // the USB layer has added Eth hdr
        minLength = sizeof(QC_ETH_HDR) + QMI_PREAMBLE_LENGTH + QCQMI_HDR_SIZE;
    }
    else
#endif // QC_IP_MODE
    {
        minLength = QMI_PREAMBLE_LENGTH + QCQMI_HDR_SIZE;
    }

    if ((Status != STATUS_SUCCESS) || (DataLength < minLength))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> <--_DispatchQMI pkt failed or too short %dB (0x%x))\n",
            pAdapter->PortName, DataLength, Status)
        );
        return FALSE;
    }

    qmiData = (PUCHAR)DataBuffer;

#ifdef QC_IP_MODE
    if (pAdapter->IPModeEnabled == TRUE)
    {
        // skip the ETH header added by the USB layer
        qmiData += sizeof(QC_ETH_HDR);
        qmiLength = DataLength - sizeof(QC_ETH_HDR);
    }
    else
#endif // QC_IP_MODE
    {
        qmiLength = DataLength;
    }

    /***
    preamble = (PQMI_PREAMBLE)qmiData;
    rsv3 = (preamble->Reserved3 >> 16);

    if ((preamble->Reserved0 != 0) ||
        (preamble->Reserved1 != 0) ||
        (preamble->Reserved2 != 0) ||
        (rsv3 != 0))
    ***/
    if (RtlCompareMemory(qmiPreamble, qmiData, QMI_PREAMBLE_LENGTH) != QMI_PREAMBLE_LENGTH)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_TRACE,
            ("<%s> <--_DispatchQMI: data pkt\n", pAdapter->PortName)
        );
        return FALSE;
    }

    qmiData += QMI_PREAMBLE_LENGTH;
    qmiLength -= QMI_PREAMBLE_LENGTH;

    // Recycle the RX data buffer
    NdisAcquireSpinLock(&pAdapter->RxLock);

    pList = RemoveHeadList(&pAdapter->RxPendingList);
    InsertTailList(&pAdapter->RxFreeList, pList);
    InterlockedDecrement(&(pAdapter->nBusyRx));
    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> _DispatchQMI: Rx pkt recycled\n", pAdapter->PortName)
    );

    NdisReleaseSpinLock(&pAdapter->RxLock);

    // Dispatch QMI
    NdisAcquireSpinLock(&pAdapter->CtrlReadLock);

    if (IsListEmpty(&pAdapter->CtrlReadPendingList))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> <--_DispatchQMI: error-empty list, drop QMI, nBusyRx %d\n",
            pAdapter->PortName, pAdapter->nBusyRx)
        );
        NdisReleaseSpinLock(&pAdapter->CtrlReadLock);
        MPWork_ScheduleWorkItem(pAdapter);
        return TRUE;  // drop the packet
    }

    // Get the completed OID off the RxPendinging list
    pList = RemoveHeadList(&pAdapter->CtrlReadPendingList);

    pOID = CONTAINING_RECORD(pList, MP_OID_READ, List);
    pOID->IrpDataLength = DataLength - QMI_PREAMBLE_LENGTH;
    pOID->IrpStatus = Status;

    if (Status == STATUS_CANCELLED)
    {
        // No processing required, just pass this on to the free list
        InterlockedDecrement(&(pAdapter->nBusyRCtrl));
        InsertTailList(&pAdapter->CtrlReadFreeList, pList);
    }
    else
    {
        RtlCopyMemory(&(pOID->QMI), qmiData, qmiLength);
        InsertTailList(&pAdapter->CtrlReadCompleteList, pList);
    }
    NdisReleaseSpinLock(&pAdapter->CtrlReadLock);

    MPWork_ScheduleWorkItem(pAdapter);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--_DispatchQMI: nBusyRCtrl: %d nBusyRx %d\n",
        pAdapter->PortName, pAdapter->nBusyRCtrl, pAdapter->nBusyRx)
    );

    return TRUE;

}  // MPUSB_DispatchQMI

#endif // QMI_OVER_DATA
#endif // QCUSB_MUX_PROTOCOL

#ifdef QCUSB_SHARE_INTERRUPT

NDIS_STATUS MP_RequestDeviceID
(
    PMP_ADAPTER pAdapter
)
{
    NDIS_STATUS status = NDIS_STATUS_FAILURE;
    ULONG ReturnBufferLength = 0;
    PDEVICE_OBJECT pDeviceId;
    PDEVICE_EXTENSION pDevExt;
    pDevExt = (PDEVICE_EXTENSION)(pAdapter->USBDo->DeviceExtension);

    // Register the device here
    status = MP_USBSendCustomCommand(pAdapter, IOCTL_QCDEV_REQUEST_DEVICEID,
                                     &pDeviceId, sizeof(PDEVICE_OBJECT), &ReturnBufferLength);

    if (status != NDIS_STATUS_SUCCESS)
    {
        return status;
    }
    else
    {
        if (ReturnBufferLength > 0)
        {
            PDeviceReadControlEvent rdCtlElement = NULL;

            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                ("<%s> MP_RequestDeviceID: DeviceId for Adapter(%p) is %p \n", pAdapter->PortName,
                pAdapter,
                pDeviceId)
            );
            pDevExt->DeviceId = pDeviceId;

            rdCtlElement = USBSHR_AddReadControlElement(pDevExt->DeviceId);
            if (rdCtlElement == NULL)
            {
                status = STATUS_UNSUCCESSFUL;
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_INFO,
                    ("<%s> RequestDeviceID: failed to get id element\n", pDevExt->PortName)
                );
            }
            else
            {
                USHORT interface = pDevExt->DataInterface;
                pDevExt->pReadControlEvent = &(rdCtlElement->ReadControlEvent[interface]);
                pDevExt->pRspAvailableCount = &(rdCtlElement->RspAvailableCount[interface]);
                pDevExt->bEnableResourceSharing = TRUE;
                pDevExt->pDispatchEvents[DSP_READ_CONTROL_EVENT_INDEX] = pDevExt->pReadControlEvent;
            }
        }
    }

    return NDIS_STATUS_SUCCESS;

}  // MP_RequestDeviceID
#endif

PMP_ADAPTER FindStackDeviceObject(PMP_ADAPTER pAdapter)
{
    PLIST_ENTRY     headOfList, peekEntry, nextEntry;
    PMP_ADAPTER pTempAdapter;
    PDEVICE_EXTENSION pTempDevExt;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;
    if (pDevExt->MuxInterface.MuxEnabled == 0)
    {
        return pAdapter;
    }
    else if (pDevExt->MuxInterface.InterfaceNumber == pDevExt->MuxInterface.PhysicalInterfaceNumber)
    {
        return pAdapter;
    }
    else
    {

        NdisAcquireSpinLock(&GlobalData.Lock);
        if (!IsListEmpty(&GlobalData.AdapterList))
        {
            headOfList = &GlobalData.AdapterList;
            peekEntry = headOfList->Flink;
            while (peekEntry != headOfList)
            {
                pTempAdapter = CONTAINING_RECORD
                (
                    peekEntry,
                    MP_ADAPTER,
                    List
                );
                pTempDevExt = (PDEVICE_EXTENSION)pTempAdapter->USBDo->DeviceExtension;
                if ((pDevExt->MuxInterface.MuxEnabled == 0x01) &&
                    (pDevExt->MuxInterface.PhysicalInterfaceNumber == pTempDevExt->MuxInterface.InterfaceNumber) &&
                    (pDevExt->MuxInterface.FilterDeviceObj == pTempDevExt->MuxInterface.FilterDeviceObj))
                {
                    NdisReleaseSpinLock(&GlobalData.Lock);
                    return pTempAdapter;
                }
                peekEntry = peekEntry->Flink;
            }
        }
        NdisReleaseSpinLock(&GlobalData.Lock);

    }
    return NULL;
}

LONG GetPhysicalAdapterQCTLTransactionId(PMP_ADAPTER pAdapter)
{
    PLIST_ENTRY     headOfList, peekEntry, nextEntry;
    PMP_ADAPTER pTempAdapter;
    PDEVICE_EXTENSION pTempDevExt;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;
    if (pDevExt->MuxInterface.MuxEnabled == 0)
    {
        return GetQCTLTransactionId(pAdapter);
    }
    else
    {

        NdisAcquireSpinLock(&GlobalData.Lock);
        if (!IsListEmpty(&GlobalData.AdapterList))
        {
            headOfList = &GlobalData.AdapterList;
            peekEntry = headOfList->Flink;
            while (peekEntry != headOfList)
            {
                pTempAdapter = CONTAINING_RECORD
                (
                    peekEntry,
                    MP_ADAPTER,
                    List
                );
                pTempDevExt = (PDEVICE_EXTENSION)pTempAdapter->USBDo->DeviceExtension;
                if ((pDevExt->MuxInterface.MuxEnabled == 0x01) &&
                    (pDevExt->MuxInterface.PhysicalInterfaceNumber == pTempDevExt->MuxInterface.InterfaceNumber) &&
                    (pDevExt->MuxInterface.FilterDeviceObj == pTempDevExt->MuxInterface.FilterDeviceObj))
                {
                    NdisReleaseSpinLock(&GlobalData.Lock);
                    return GetQCTLTransactionId(pTempAdapter);
                }
                peekEntry = peekEntry->Flink;
            }
        }
        NdisReleaseSpinLock(&GlobalData.Lock);

    }
    return  pAdapter->QMICTLTransactionId; // shall we use 0x0 to indicate the unexpected?
}

LONG GetQCTLTransactionId(PMP_ADAPTER pAdapter)
{
    UCHAR tid;

    tid = (UCHAR)InterlockedIncrement(&(pAdapter->QMICTLTransactionId));

#ifdef QCMP_SUPPORT_CTRL_QMIC
    if (pAdapter->ExternalQCTLEnabled == TRUE)
    {
        while ((tid & 0x7F) == 0)
        {
            tid = (UCHAR)InterlockedIncrement(&(pAdapter->QMICTLTransactionId));
        }
        return (LONG)(tid & 0x7F);
    }
    else
#endif // QCMP_SUPPORT_CTRL_QMIC
    {
        while (tid == 0)
        {
            tid = (UCHAR)InterlockedIncrement(&(pAdapter->QMICTLTransactionId));
        }
    }
    return (LONG)tid;
} // GetQCTLTransactionId

VOID IncrementAllQmiSync(PMP_ADAPTER pAdapter)
{
    PLIST_ENTRY     headOfList, peekEntry, nextEntry;
    PMP_ADAPTER pTempAdapter;
    PDEVICE_EXTENSION pTempDevExt;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> -->IncrementAllQmiSync 0x%p\n", pAdapter->PortName, pAdapter)
    );
    NdisAcquireSpinLock(&GlobalData.Lock);
    if (!IsListEmpty(&GlobalData.AdapterList))
    {
        headOfList = &GlobalData.AdapterList;
        peekEntry = headOfList->Flink;
        while (peekEntry != headOfList)
        {
            pTempAdapter = CONTAINING_RECORD
            (
                peekEntry,
                MP_ADAPTER,
                List
            );

            pTempDevExt = (PDEVICE_EXTENSION)pTempAdapter->USBDo->DeviceExtension;
            if ((pDevExt->MuxInterface.PhysicalInterfaceNumber == pTempDevExt->MuxInterface.PhysicalInterfaceNumber) &&
                (pDevExt->MuxInterface.FilterDeviceObj == pTempDevExt->MuxInterface.FilterDeviceObj))
            {
                if (pTempAdapter->QmiSyncNeeded == 0)
                {
                    InterlockedIncrement(&pTempAdapter->QmiSyncNeeded);
                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_READ,
                        MP_DBG_LEVEL_DETAIL,
                        ("<%s> IncrementAllQmiSync 0x%p: %d\n", pAdapter->PortName, pTempAdapter, pTempAdapter->QmiSyncNeeded)
                    );
                }
            }
            peekEntry = peekEntry->Flink;
        }
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_READ,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> IncrementAllQmiSync: adapter list empty\n", pAdapter->PortName)
        );
    }
    NdisReleaseSpinLock(&GlobalData.Lock);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_READ,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> <--IncrementAllQmiSync 0x%p\n", pAdapter->PortName, pAdapter)
    );
}

BOOLEAN DisconnectedAllAdapters(PMP_ADAPTER pAdapter, PMP_ADAPTER *returnAdapter)
{
    PLIST_ENTRY     headOfList, peekEntry, nextEntry;
    PMP_ADAPTER pTempAdapter;
    PDEVICE_EXTENSION pTempDevExt;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;
    ULONG ConnectCount = 0;

    *returnAdapter = pAdapter;

    NdisAcquireSpinLock(&GlobalData.Lock);
    if (!IsListEmpty(&GlobalData.AdapterList))
    {
        headOfList = &GlobalData.AdapterList;
        peekEntry = headOfList->Flink;
        while (peekEntry != headOfList)
        {
            pTempAdapter = CONTAINING_RECORD
            (
                peekEntry,
                MP_ADAPTER,
                List
            );

            pTempDevExt = (PDEVICE_EXTENSION)pTempAdapter->USBDo->DeviceExtension;
            if ((pDevExt->MuxInterface.PhysicalInterfaceNumber == pTempDevExt->MuxInterface.PhysicalInterfaceNumber) &&
                (pDevExt->MuxInterface.FilterDeviceObj == pTempDevExt->MuxInterface.FilterDeviceObj) &&
                (pTempAdapter->ulMediaConnectStatus == NdisMediaStateConnected))
            {
                ConnectCount += 1;
            }
            if ((pDevExt->MuxInterface.MuxEnabled == 0x01) &&
                (pDevExt->MuxInterface.PhysicalInterfaceNumber == pTempDevExt->MuxInterface.InterfaceNumber) &&
                (pDevExt->MuxInterface.FilterDeviceObj == pTempDevExt->MuxInterface.FilterDeviceObj))
            {
                *returnAdapter = pTempAdapter;
            }
            peekEntry = peekEntry->Flink;
        }
        NdisReleaseSpinLock(&GlobalData.Lock);
    }

    if (ConnectCount > 0)
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
    return  FALSE;
}

LONG GetQMUXTransactionId(PMP_ADAPTER pAdapter)
{
    USHORT tid;

    tid = (USHORT)InterlockedIncrement(&(pAdapter->QMUXTransactionId));
    while (tid == 0)
    {
        tid = (USHORT)InterlockedIncrement(&(pAdapter->QMUXTransactionId));
    }

    return tid;
}

ULONG GetNextTxPacketSize(PMP_ADAPTER pAdapter, PLIST_ENTRY listEntry)
{
    ULONG dwPacketLength = 0;
    PLIST_ENTRY peekEntry = listEntry->Flink;
#ifdef NDIS60_MINIPORT
    ULONG copyOk = 0;
    PNET_BUFFER_LIST      netBufferList;
    PNET_BUFFER           netBuffer;
    PMPUSB_TX_CONTEXT_NBL txContext = NULL;
    PMPUSB_TX_CONTEXT_NB  nbContext = CONTAINING_RECORD(peekEntry, MPUSB_TX_CONTEXT_NB, List);
    txContext = nbContext->NBL;
    netBufferList = txContext->NBL;
    netBuffer = nbContext->NB;

    copyOk = MPUSB_QueryDataPacket(pAdapter, netBuffer, &dwPacketLength);
    if (copyOk == 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_WRITE,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPUSB_QueryDataPacketfailed to get pkt for QNBL 0x%p\n",
            pAdapter->PortName, txContext)
        );
        return 0;
    }
    return dwPacketLength;
#else
    PNDIS_BUFFER pCurrentNdisBuffer;
    UINT         dwTotalBufferCount;
    PNDIS_PACKET pNdisPacket = ListItemToPacket(peekEntry);
    NdisQueryPacket
    (
        pNdisPacket,
        NULL,
        &dwTotalBufferCount,
        &pCurrentNdisBuffer,
        &dwPacketLength
    );
    return dwPacketLength;
#endif   
}
