/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P Q C T L . C

GENERAL DESCRIPTION
    This file provides support for QMI QCTL Messaging.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "MPQMI.h"
#include "MPQCTL.h"
#include "MPMain.h"
#include "MPIOC.h"
#include "MPQMUX.h"
#include "MPUSB.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPQCTL.tmh"

#endif   // EVENT_TRACING

UCHAR MPQCTL_GetNextTransactionId(PMP_ADAPTER pAdapter)
{
    return GetPhysicalAdapterQCTLTransactionId(pAdapter);
} // MPQCTL_GetNextTransactionId

VOID MPQCTL_ProcessInboundQMICTL
(
    PMP_ADAPTER pAdapter,
    PQCQMI      qmi,
    ULONG       TotalDataLength
)
{
    PQCQMICTL_MSG            qmictl = (PQCQMICTL_MSG)&qmi->SDU;
    PQMICTL_TRANSACTION_ITEM item;
    PQMICTL_MSG              qmictl_msg;
    PUCHAR                 pQMIType = NULL;
    PUCHAR                 pClientId = NULL;
    UCHAR                  controlFlag;
    PMP_ADAPTER            pTempAdapter;
    UCHAR                  ucChannel = USBIF_GetDataInterfaceNumber(pAdapter->USBDo);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> --> ProcessQMICTL <=%u: Flags 0x%x TID 0x%x Type 0x%x Len %d\n",
        pAdapter->PortName,
        ucChannel,
        qmictl->CtlFlags,
        qmictl->TransactionId,
        qmictl->QMICTLType,
        qmictl->Length
    )
    );

    qmictl_msg = (PQMICTL_MSG)&qmi->SDU; // qmictl->Payload;
    controlFlag = (qmictl->CtlFlags & 0x03);

#ifdef QCMP_SUPPORT_CTRL_QMIC
    if (pAdapter->ExternalQCTLEnabled == TRUE)
    {
        if (((pAdapter->ExternalQMICtrlCid == 0) &&
            (qmictl->TransactionId > QCMP_QCTL_INTERNAL_MAX_TID)) || // support TID spacing
            (qmi->ClientId != 0) || // support non-zero CID
            (controlFlag == QMICTL_CTL_FLAG_IND))                     // broadcast IND
        {
            MPQMI_SendQCTLToExternalClient
            (
                pAdapter,
                qmi,
                TotalDataLength
            );

            // indication also deliver to internal client
            if (controlFlag != QMICTL_CTL_FLAG_IND)
            {
                return;
            }
        }
    }
#endif // QCMP_SUPPORT_CTRL_QMIC

    // Find transaction item in the QMICTLTransactionList
    //item = MPQCTL_FindQMICTLTransactionItem(pAdapter, qmictl->TransactionId, NULL);
    item = MPQCTL_FindQMICTLTransactionItem(pAdapter, qmictl->TransactionId, NULL, &pTempAdapter);
    if ((item == NULL) && (controlFlag == QMICTL_CTL_FLAG_RSP))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> ProcessQMICTL: no QMICTL item found for TID %d\n", pAdapter->PortName, qmictl->TransactionId)
        );
        return;
    }

    switch (controlFlag)
    {
        case QMICTL_CTL_FLAG_RSP:
        {
            switch (qmictl->QMICTLType)
            {
                case QMICTL_SET_INSTANCE_ID_RESP:
                {
                    MPQCTL_HandleSetInstanceIdRsp(pTempAdapter, qmi);
                    break;
                }
                case QMICTL_GET_VERSION_RESP:
                {
                    MPQCTL_HandleGetVersionRsp(pTempAdapter, qmi);
                    break;
                }

                case QMICTL_GET_CLIENT_ID_RESP:
                {
                    MPQCTL_HandleGetClientIdRsp(pTempAdapter, qmi, item);
                    break;
                }

                case QMICTL_RELEASE_CLIENT_ID_RESP:
                {
                    MPQCTL_HandleReleaseClientIdRsp(pTempAdapter, qmi, item);
                    break;
                }

                case QMICTL_SET_DATA_FORMAT_RESP:
                {
                    MPQCTL_HandleSetDataFormatRsp(pTempAdapter, qmi, item);
                    break;
                }

                case QMICTL_SYNC_RESP:
                {
                    MPQCTL_HandleSyncRsp(pTempAdapter, qmi, item);
                    break;
                }

                default:
                {
                    // wrong QMICTL response type
                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_ERROR,
                        ("<%s> ProcessQMICTL: wrong rsp type 0x%x\n", pAdapter->PortName, qmictl->QMICTLType)
                    );
                    break;
                }
            } // QMICTL response

            break;
        }  // QMICTL_CTL_FLAG_RSP

        case QMICTL_CTL_FLAG_IND:
        {
            switch (qmictl->QMICTLType)
            {
                case QMICTL_REVOKE_CLIENT_ID_IND:
                {
                    MPQCTL_HandleRevokeClientIdInd(pAdapter, qmi);
                    break;
                }

                case QMICTL_INVALID_CLIENT_ID_IND:
                {
                    MPQCTL_HandleInvalidClientIdInd(pAdapter, qmi);
                    break;
                }

                case QMICTL_SYNC_IND:
                {
                    MPQCTL_HandleSyncInd(pAdapter, qmi);
                    break;
                }

                default:
                {
                    // wrong QMICTL indication type
                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_ERROR,
                        ("<%s> ProcessQMICTL: wrong ind type 0x%x\n", pAdapter->PortName, qmictl->QMICTLType)
                    );
                    break;
                }
            }  // switch

            break;
        }  // QMICTL_CTL_FLAG_IND

        default:
        {
            // invalid control flag
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> ProcessQMICTL: wrong ctl flag 0x%x\n", pAdapter->PortName, qmictl->CtlFlags)
            );
            break;
        }
    } // switch (controlFlag)

    // release the item memory
    if (item != NULL)
    {
        ExFreePool(item);
    }

}  // MPQCTL_ProcessInboundQMICTL

NDIS_STATUS MPQCTL_SetInstanceId
(
    PMP_ADAPTER pAdapter
)
{
    NDIS_STATUS   ndisStatus = NDIS_STATUS_SUCCESS;
    LARGE_INTEGER timeoutValue;
    int           i;
    UCHAR         tid = 0;

    // clear signal
    KeClearEvent(&pAdapter->QMICTLInstanceIdReceivedEvent);

    for (i = 0; i < pAdapter->QmiInitRetries; i++)
    {
        if (pAdapter->Flags & fMP_ANY_FLAGS)
        {
            ndisStatus = NDIS_STATUS_FAILURE;
            break;
        }

        ndisStatus = MPQCTL_Request
        (
            pAdapter,
            QMUX_TYPE_CTL,
            0,
            QMICTL_SET_INSTANCE_ID_REQ,
            &tid,
            (PVOID)pAdapter
        );

        if (ndisStatus == NDIS_STATUS_SUCCESS)
        {
            NTSTATUS nts;

            timeoutValue.QuadPart = QMICTL_TIMEOUT_RX; // -(5 * 1000 * 1000);   // .5 sec

            // wait for signal
            nts = KeWaitForSingleObject
            (
                &pAdapter->QMICTLInstanceIdReceivedEvent,
                Executive,
                KernelMode,
                FALSE,
                &timeoutValue
            );
            KeClearEvent(&pAdapter->QMICTLInstanceIdReceivedEvent);

            if (nts == STATUS_TIMEOUT)
            {
                ndisStatus = NDIS_STATUS_FAILURE;

                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_ERROR,
                    ("<%s> MPQCTL_SetInstanceId: timeout, rm TR %u\n", pAdapter->PortName, tid)
                );
                MPQCTL_RemoveOldTransactionItem(pAdapter, tid);
            }
            else if (pAdapter->DeviceInstanceObtained == FALSE)
            {
                if (pAdapter->QMI_ID != MP_INVALID_QMI_ID)
                {
                    pAdapter->PermanentAddress[4] = (pAdapter->QMI_ID >> 8) & 0xFF;
                    pAdapter->PermanentAddress[5] = pAdapter->QMI_ID & 0xFF;
                    ETH_COPY_NETWORK_ADDRESS(pAdapter->CurrentAddress, pAdapter->PermanentAddress);
                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_DETAIL,
                        ("<%s> MAC Address = %02x-%02x-%02x-%02x-%02x-%02x\n", pAdapter->PortName,
                        pAdapter->PermanentAddress[0],
                        pAdapter->PermanentAddress[1],
                        pAdapter->PermanentAddress[2],
                        pAdapter->PermanentAddress[3],
                        pAdapter->PermanentAddress[4],
                        pAdapter->PermanentAddress[5]
                    )
                    );

#ifdef QC_IP_MODE
                    ETH_COPY_NETWORK_ADDRESS(pAdapter->MacAddress2, pAdapter->PermanentAddress);
                    pAdapter->MacAddress2[5] = pAdapter->DeviceId2 & 0xFF;
                    /***************
                    pAdapter->MacAddress2[0] = 0x02;
                    pAdapter->MacAddress2[1] = 0x50;
                    pAdapter->MacAddress2[2] = 0xF3;
                    pAdapter->MacAddress2[3] = 0x00;
                    pAdapter->MacAddress2[4] = 0x00;
                    pAdapter->MacAddress2[5] = 0x00;
                    ****************/
                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_DETAIL,
                        ("<%s> IPO: MAC Address2 = %02X-%02X-%02X-%02X-%02X-%02X\n", pAdapter->PortName,
                        pAdapter->MacAddress2[0],
                        pAdapter->MacAddress2[1],
                        pAdapter->MacAddress2[2],
                        pAdapter->MacAddress2[3],
                        pAdapter->MacAddress2[4],
                        pAdapter->MacAddress2[5]
                    )
                    );
#endif // QC_IP_MODE
                }
                break;
            }
            else
            {
                break;
            }
        }
        else
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> SetInstanceId: req failure 0x%x\n", pAdapter->PortName, ndisStatus)
            );
            break;
        }
    } // for

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> MPQCTL_SetInstanceId: remote 0x%x (ST 0x%x) local %d\n", pAdapter->PortName,
        pAdapter->QMI_ID & 0xFFFF, pAdapter->DeviceInstance,
        ndisStatus)
    );

    return ndisStatus;

}  // MPQCTL_SetInstanceId

NDIS_STATUS MPQCTL_GetQMICTLVersion
(
    PMP_ADAPTER pAdapter,
    BOOLEAN     Retries
)
{
    NDIS_STATUS   ndisStatus = NDIS_STATUS_SUCCESS;
    LARGE_INTEGER timeoutValue;
    int           i;
    UCHAR         tid = 0;

    // clear signal
    KeClearEvent(&pAdapter->QMICTLVersionReceivedEvent);

    for (i = 0; i < pAdapter->QmiInitRetries; i++)
    {
        if ((Retries == FALSE) && (i > 0))
        {
            break;
        }

        if (pAdapter->Flags & fMP_ANY_FLAGS)
        {
            ndisStatus = NDIS_STATUS_FAILURE;
            break;
        }

        if (USBIF_IsUsbBroken(pAdapter->USBDo) == TRUE)
        {
            ndisStatus = NDIS_STATUS_ADAPTER_REMOVED;
            break;
        }

        ndisStatus = MPQCTL_Request
        (
            pAdapter,
            QMUX_TYPE_CTL,
            0,
            QMICTL_GET_VERSION_REQ,
            &tid,
            (PVOID)pAdapter
        );

        if (ndisStatus == NDIS_STATUS_SUCCESS)
        {
            NTSTATUS nts;

            timeoutValue.QuadPart = QMICTL_TIMEOUT_RX; // -(10 * 1000 * 1000);   // 1.0 sec

            // wait for signal
            nts = KeWaitForSingleObject
            (
                &pAdapter->QMICTLVersionReceivedEvent,
                Executive,
                KernelMode,
                FALSE,
                &timeoutValue
            );
            KeClearEvent(&pAdapter->QMICTLVersionReceivedEvent);

            if (nts == STATUS_TIMEOUT)
            {
                ndisStatus = NDIS_STATUS_FAILURE;

                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_ERROR,
                    ("<%s> GetQMICTLVersion: timeout, rm TR %u (%u)\n", pAdapter->PortName, tid, i)
                );
                MPQCTL_RemoveOldTransactionItem(pAdapter, tid);
            }
            else
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_DETAIL,
                    ("<%s> GetQMICTLVersion: rsp received 0x%x\n", pAdapter->PortName, nts)
                );
                break;
            }
        }
        else
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> GetQMICTLVersion: req failure 0x%x\n", pAdapter->PortName, ndisStatus)
            );
            break;
        }
    } // for

    if (QMICTL_SUPPORTED_MAJOR_VERSION < pAdapter->QMUXVersion[QMUX_TYPE_CTL].Major)
    {
        ndisStatus = NDIS_STATUS_FAILURE;
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> GetQMICTLVersion: failed (0x%x)\n", pAdapter->PortName, ndisStatus)
        );
    }


    if (Retries == TRUE)
    {
        PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;
        if (ndisStatus == NDIS_STATUS_SUCCESS)
        {
            if ((pDevExt->MuxInterface.MuxEnabled == 0) ||
                (pDevExt->MuxInterface.InterfaceNumber == pDevExt->MuxInterface.PhysicalInterfaceNumber))
            {
                ndisStatus = MPQCTL_SendQMICTLSync(pAdapter);

                if (ndisStatus == NDIS_STATUS_SUCCESS)
                {
                    KeSetEvent(&pAdapter->MainAdapterQmiInitSuccessful, IO_NO_INCREMENT, FALSE);
                }
            }
            else
            {

                NTSTATUS nts;
                PMP_ADAPTER returnAdapter = NULL;

                timeoutValue.QuadPart = QMICTL_TIMEOUT_RX; // -(10 * 1000 * 1000);   // 1.0 sec

                DisconnectedAllAdapters(pAdapter, &returnAdapter);
                // wait for signal
                nts = KeWaitForSingleObject
                (
                    &returnAdapter->MainAdapterQmiInitSuccessful,
                    Executive,
                    KernelMode,
                    FALSE,
                    &timeoutValue
                );
                if (nts == STATUS_TIMEOUT)
                {
                    ndisStatus = NDIS_STATUS_FAILURE;

                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_ERROR,
                        ("<%s> MPQCTL_SendQMICTLSync for non muxed adapter timedout: timeout\n", pAdapter->PortName)
                    );
                }

            }
        }
    }

    return ndisStatus;

}  // MPQCTL_GetQMICTLVersion

NDIS_STATUS MPQCTL_GetClientId
(
    PMP_ADAPTER pAdapter,
    UCHAR       QMIType,
    PVOID       Context   // Adapter or IoDev
)
{
    NDIS_STATUS   ndisStatus = NDIS_STATUS_SUCCESS;
    LARGE_INTEGER timeoutValue;
    PKEVENT       pClientIdReceivedEvent;
    UCHAR         currentClientId;
    UCHAR         tid = 0;

    if (Context == (PVOID)pAdapter)
    {
        // for internal client
        pClientIdReceivedEvent = &pAdapter->ClientIdReceivedEvent;
        currentClientId = pAdapter->ClientId[QMIType];
    }
    else
    {
        // for external client
        PMPIOC_DEV_INFO pIoDev = (PMPIOC_DEV_INFO)Context;
        pClientIdReceivedEvent = &pIoDev->ClientIdReceivedEvent;
        currentClientId = pIoDev->ClientId;

        // External client cannot request QMUX_TYPE_CTL
        if (QMIType == QMUX_TYPE_CTL)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> GetInternalClientId: service type %u not available for client\n",
                pAdapter->PortName, QMIType)
            );
            return NDIS_STATUS_FAILURE;
        }
    }

    if (currentClientId != 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> GetInternalClientId: curr client id not released %u(0x%p)\n",
            pAdapter->PortName, currentClientId, Context)
        );
        return NDIS_STATUS_FAILURE;
    }

    // clear signal
    KeClearEvent(pClientIdReceivedEvent);

    ndisStatus = MPQCTL_Request
    (
        pAdapter,
        QMIType,
        0,
        QMICTL_GET_CLIENT_ID_REQ,
        &tid,
        Context // (PVOID)pAdapter
    );

    if (ndisStatus == NDIS_STATUS_SUCCESS)
    {
        NTSTATUS nts;

        timeoutValue.QuadPart = QMICTL_TIMEOUT_RX; // -(50 * 1000 * 1000);   // 5.0 sec

        // wait for signal
        nts = KeWaitForSingleObject
        (
            pClientIdReceivedEvent,
            Executive,
            KernelMode,
            FALSE,
            &timeoutValue
        );
        KeClearEvent(pClientIdReceivedEvent);

        if (nts == STATUS_TIMEOUT)
        {
            ndisStatus = NDIS_STATUS_FAILURE;

            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> GetInternalClientId: timeout, rm TR %u\n", pAdapter->PortName, tid)
            );
            MPQCTL_RemoveOldTransactionItem(pAdapter, tid);
        }
    }

    return ndisStatus;

}  // MPQCTL_GetClientId

NDIS_STATUS MPQCTL_ReleaseClientId
(
    PMP_ADAPTER pAdapter,
    UCHAR       QMIType,
    PVOID       Context   // Adapter or IoDev
)
{
    NDIS_STATUS   ndisStatus = NDIS_STATUS_SUCCESS;
    LARGE_INTEGER timeoutValue;
    PKEVENT       pClientIdReleasedEvent;
    UCHAR         currentClientId;
    UCHAR         tid = 0;

    if (Context == (PVOID)pAdapter)
    {
        // for internal client
        pClientIdReleasedEvent = &pAdapter->ClientIdReleasedEvent;
        currentClientId = pAdapter->ClientId[QMIType];
    }
    else
    {
        // for external client
        PMPIOC_DEV_INFO pIoDev = (PMPIOC_DEV_INFO)Context;
        pClientIdReleasedEvent = &pIoDev->ClientIdReleasedEvent;
        currentClientId = pIoDev->ClientId;
    }

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> _ReleaseClientId: releasing client id %u (0x%p)\n",
        pAdapter->PortName, currentClientId, Context)
    );

    if (currentClientId == 0)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> ReleaseClientId: client id already released (0x%p)\n", pAdapter->PortName, Context)
        );
        return NDIS_STATUS_FAILURE;
    }

    if (pAdapter->Flags & fMP_ADAPTER_SURPRISE_REMOVED)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> ReleaseClientId: dev removed (0x%p)\n", pAdapter->PortName, Context)
        );
        return NDIS_STATUS_FAILURE;
    }

    // clear signal
    KeClearEvent(pClientIdReleasedEvent);

    ndisStatus = MPQCTL_Request
    (
        pAdapter,
        QMIType,
        currentClientId,
        QMICTL_RELEASE_CLIENT_ID_REQ,
        &tid,
        Context
    );

    if (ndisStatus == NDIS_STATUS_SUCCESS)
    {
        NTSTATUS nts;

        if (pAdapter->IsQMIOutOfService == FALSE)
        {
            timeoutValue.QuadPart = QMICTL_TIMEOUT_RX; // -(50 * 1000 * 1000);   // 5.0 sec

            // wait for signal
            nts = KeWaitForSingleObject
            (
                pClientIdReleasedEvent,
                Executive,
                KernelMode,
                FALSE,
                &timeoutValue
            );
            KeClearEvent(pClientIdReleasedEvent);
        }
        else
        {
            nts = STATUS_TIMEOUT;

            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> ReleaseClientId: QMIOutOfService, fake timeout. TR %u\n", pAdapter->PortName, tid)
            );
        }

        if (nts == STATUS_TIMEOUT)
        {
            ndisStatus = NDIS_STATUS_FAILURE;

            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> ReleaseClientId: timeout, rm TR %u\n", pAdapter->PortName, tid)
            );
            MPQCTL_RemoveOldTransactionItem(pAdapter, tid);
        }
    }

    return ndisStatus;

}  // MPQCTL_ReleaseClientId

NDIS_STATUS MPQCTL_SetDataFormat
(
    PMP_ADAPTER pAdapter,
    PVOID       Context   // pAdapter or IoDev
)
{
    NDIS_STATUS   ndisStatus = NDIS_STATUS_SUCCESS;
    LARGE_INTEGER timeoutValue;
    PKEVENT       pDataFormatSetEvent;
    UCHAR         currentClientId;
    UCHAR         tid = 0;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> -->SetDataFormat (QosEnabled %u)\n", pAdapter->PortName, pAdapter->QosEnabled)
    );

#ifdef QC_IP_MODE
#ifdef MP_QCQOS_ENABLED

    if (Context == (PVOID)pAdapter)
    {
        pDataFormatSetEvent = &pAdapter->DataFormatSetEvent;
    }
    else
    {
        // for external client
        PMPIOC_DEV_INFO pIoDev = (PMPIOC_DEV_INFO)Context;
        pDataFormatSetEvent = &pIoDev->DataFormatSetEvent;

        if (pIoDev->Type != MP_DEV_TYPE_INTERNAL_QOS)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> SetDataFormat: service not available for this client 0x%p\n",
                pAdapter->PortName, pIoDev)
            );
            pAdapter->QosEnabled = FALSE;
            return NDIS_STATUS_FAILURE;
        }
    }

    // clear signal
    KeClearEvent(pDataFormatSetEvent);

    ndisStatus = MPQCTL_Request
    (
        pAdapter,
        QMUX_TYPE_CTL,
        0,
        QMICTL_SET_DATA_FORMAT_REQ,
        &tid,
        Context
    );

    if (ndisStatus == NDIS_STATUS_SUCCESS)
    {
        NTSTATUS nts;

        timeoutValue.QuadPart = QMICTL_TIMEOUT_RX; // -(20 * 1000 * 1000);   // 2.0 sec

        // wait for signal
        nts = KeWaitForSingleObject
        (
            pDataFormatSetEvent,
            Executive,
            KernelMode,
            FALSE,
            &timeoutValue
        );
        KeClearEvent(pDataFormatSetEvent);

        if (nts == STATUS_TIMEOUT)
        {
            ndisStatus = NDIS_STATUS_FAILURE;

            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> SetDataFormat: timeout, rm TR %u\n", pAdapter->PortName, tid)
            );
            MPQCTL_RemoveOldTransactionItem(pAdapter, tid);
            pAdapter->QosEnabled = FALSE;
        }
    }
    else
    {
        pAdapter->QosEnabled = FALSE;
    }

#endif // MP_QCQOS_ENABLED
#endif 

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> <--SetDataFormat (QosEnabled %u)\n", pAdapter->PortName, pAdapter->QosEnabled)
    );
    return ndisStatus;

}  // MPQCTL_SetDataFormat

// Find the desired QMICTL_TRANSACTION_ITEM and de-queue it
PQMICTL_TRANSACTION_ITEM MPQCTL_FindQMICTLTransactionItem
(
    PMP_ADAPTER pAdapter,
    UCHAR       TransactionId,
    PIRP        Irp,
    PMP_ADAPTER *pPtrAdapter
)
{
    PQMICTL_TRANSACTION_ITEM item = NULL;
    PLIST_ENTRY    listHead = NULL, thisEntry = NULL;
    PLIST_ENTRY    adapterlistHead = NULL, adapterthisEntry = NULL, adapterpeekEntry = NULL, adapterheadOfList = NULL;
    PMP_ADAPTER pTempAdapter;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;
    PDEVICE_EXTENSION pTempDevExt;

    *pPtrAdapter = pAdapter;

    QCNET_DbgPrint
    (
        QCUSB_DBG_MASK_CONTROL,
        QCUSB_DBG_LEVEL_TRACE,
        ("<%s> <--- pDevExt MUX_INTERFACE_INFO %d %d %d 0x%p \n",
        pDevExt->PortName, pDevExt->MuxInterface.InterfaceNumber,
        pDevExt->MuxInterface.PhysicalInterfaceNumber, pDevExt->MuxInterface.MuxEnabled, pDevExt->MuxInterface.FilterDeviceObj)
    );

    NdisAcquireSpinLock(&GlobalData.Lock);
    if (!IsListEmpty(&GlobalData.AdapterList))
    {
        adapterheadOfList = &GlobalData.AdapterList;
        adapterpeekEntry = adapterheadOfList->Flink;
        while (adapterpeekEntry != adapterheadOfList)
        {
            pTempAdapter = CONTAINING_RECORD
            (
                adapterpeekEntry,
                MP_ADAPTER,
                List
            );
            NdisReleaseSpinLock(&GlobalData.Lock);

            pTempDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;

            QCNET_DbgPrint
            (
                QCUSB_DBG_MASK_CONTROL,
                QCUSB_DBG_LEVEL_TRACE,
                ("<%s> <--- pTempDevExt MUX_INTERFACE_INFO %d %d %d 0x%p \n",
                pTempDevExt->PortName, pTempDevExt->MuxInterface.InterfaceNumber,
                pTempDevExt->MuxInterface.PhysicalInterfaceNumber, pTempDevExt->MuxInterface.MuxEnabled, pTempDevExt->MuxInterface.FilterDeviceObj)
            );

            if ((pDevExt->MuxInterface.PhysicalInterfaceNumber == pTempDevExt->MuxInterface.PhysicalInterfaceNumber) &&
                (pDevExt->MuxInterface.FilterDeviceObj == pTempDevExt->MuxInterface.FilterDeviceObj))
            {

                NdisAcquireSpinLock(&pTempAdapter->QMICTLLock);

                if (!IsListEmpty(&pTempAdapter->QMICTLTransactionList))
                {
                    listHead = &pTempAdapter->QMICTLTransactionList;
                    thisEntry = listHead->Flink;
                    item = CONTAINING_RECORD(thisEntry, QMICTL_TRANSACTION_ITEM, List);

                    if (Irp == NULL)
                    {
                        while ((thisEntry != listHead) &&
                               (item->TransactionId != TransactionId))
                        {
                            thisEntry = thisEntry->Flink;
                            item = CONTAINING_RECORD(thisEntry, QMICTL_TRANSACTION_ITEM, List);
                        }
                    }
                    else
                    {
                        while ((thisEntry != listHead) && (item->Irp != Irp))
                        {
                            thisEntry = thisEntry->Flink;
                            item = CONTAINING_RECORD(thisEntry, QMICTL_TRANSACTION_ITEM, List);
                        }
                    }

                    if (thisEntry == listHead)
                    {
                        item = NULL;
                    }
                }
            }
            // Dequeue thisEntry
            if (item != NULL)
            {
                RemoveEntryList(thisEntry);
                NdisReleaseSpinLock(&pTempAdapter->QMICTLLock);
                *pPtrAdapter = pTempAdapter;
                return item;
            }
            else
            {
                NdisReleaseSpinLock(&pTempAdapter->QMICTLLock);
                NdisAcquireSpinLock(&GlobalData.Lock);
                adapterpeekEntry = adapterpeekEntry->Flink;
            }

        }
    }
    NdisReleaseSpinLock(&GlobalData.Lock);
    return item;

}  // MPQCTL_FindQMICTLTransactionItem

VOID MPQCTL_CleanupQMICTLTransactionList(PMP_ADAPTER pAdapter)
{
    PLIST_ENTRY  pList;
    PQMICTL_TRANSACTION_ITEM item;

    NdisAcquireSpinLock(&pAdapter->QMICTLLock);
    while (!IsListEmpty(&pAdapter->QMICTLTransactionList))
    {
        pList = RemoveHeadList(&pAdapter->QMICTLTransactionList);
        item = CONTAINING_RECORD(pList, QMICTL_TRANSACTION_ITEM, List);
        ExFreePool(item);
    }
    NdisReleaseSpinLock(&pAdapter->QMICTLLock);

}  // MPQCTL_CleanupQMICTLTransactionList

NDIS_STATUS MPQCTL_Request
(
    PMP_ADAPTER pAdapter,
    UCHAR       QMIType,
    UCHAR       ClientId,
    USHORT      QMICTLType,
    PUCHAR      Tid,
    PVOID       Context    // Adapter or DeviceObject
)
{
    PQCQMI      qmi;
    PQMICTL_MSG qmictl;
    NDIS_STATUS ndisStatus = NDIS_STATUS_SUCCESS;
    PVOID       msgBuf = NULL;
    ULONG       msgLen = sizeof(QCQMI_HDR);
    UCHAR       transactionId;

    if (QMICTL_SUPPORTED_MAJOR_VERSION < pAdapter->QMUXVersion[QMUX_TYPE_CTL].Major)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTLRequest: QMICTL ver not supported - %d\n",
            pAdapter->PortName, pAdapter->QMUXVersion[QMUX_TYPE_CTL].Major)
        );
        return NDIS_STATUS_FAILURE;
    }

    if (USBIF_IsUsbBroken(pAdapter->USBDo) == TRUE)
    {
        return NDIS_STATUS_ADAPTER_REMOVED;
    }

    switch (QMICTLType)
    {
        case QMICTL_SET_INSTANCE_ID_REQ:
        {
            msgBuf = MPQCTL_HandleSetInstanceIdReq
            (
                pAdapter,
                &transactionId,
                &msgLen
            );

            if (msgBuf == NULL)
            {
                ndisStatus = NDIS_STATUS_RESOURCES;
                break;
            }

            break;
        }

        case QMICTL_GET_VERSION_REQ:
        {
            msgBuf = MPQCTL_HandleGetVersionReq
            (
                pAdapter,
                &transactionId,
                &msgLen
            );

            if (msgBuf == NULL)
            {
                ndisStatus = NDIS_STATUS_RESOURCES;
                break;
            }

            break;
        }

        case QMICTL_GET_CLIENT_ID_REQ:
        {
            msgBuf = MPQCTL_HandleGetClientIdReq
            (
                pAdapter,
                QMIType,
                &transactionId,
                &msgLen
            );
            if (msgBuf == NULL)
            {
                ndisStatus = NDIS_STATUS_RESOURCES;
                break;
            }
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_DETAIL,
                ("<%s> GetClientId for 0x%p TID %u\n", pAdapter->PortName, Context, transactionId)
            );
            break;
        }

        case QMICTL_RELEASE_CLIENT_ID_REQ:
        {
            msgBuf = MPQCTL_HandleReleaseClientIdReq
            (
                pAdapter,
                QMIType,
                ClientId,
                &transactionId,
                &msgLen
            );
            if (msgBuf == NULL)
            {
                ndisStatus = NDIS_STATUS_RESOURCES;
                break;
            }
            break;
        }

        case QMICTL_SET_DATA_FORMAT_REQ:
        {
            msgBuf = MPQCTL_HandleSetDataFormatReq
            (
                pAdapter,
                &transactionId,
                &msgLen
            );
            if (msgBuf == NULL)
            {
                ndisStatus = NDIS_STATUS_RESOURCES;
                break;
            }
            break;
        }

        case QMICTL_SYNC_REQ:
        {
            msgBuf = MPQCTL_HandleSyncReq
            (
                pAdapter,
                &transactionId,
                &msgLen
            );

            if (msgBuf == NULL)
            {
                ndisStatus = NDIS_STATUS_RESOURCES;
                break;
            }
            break;
        }

        default:
        {
            break;
        }
    }  // switch

    if (msgBuf != NULL)
    {
        PQMICTL_TRANSACTION_ITEM item;
        item = ExAllocatePool(NonPagedPool, sizeof(QMICTL_TRANSACTION_ITEM));
        if (item == NULL)
        {
            ndisStatus = NDIS_STATUS_RESOURCES;
        }
        else
        {
            item->TransactionId = transactionId;
            item->Context = Context;
            item->Irp = NULL;
            *Tid = transactionId;

            // Send to USB
            ndisStatus = MPQCTL_SendQMICTLRequest
            (
                pAdapter,
                msgBuf,      // freed in the IRP completion routine
                msgLen,
                (PVOID)item  // freed in the IRP completion routine on failure
            );
        }
    }

    return ndisStatus;

}  // MPQCTL_Request

NTSTATUS MPQCTL_SendIRPCompletion
(
    PDEVICE_OBJECT pDO,
    PIRP           pIrp,
    PVOID          pContext
)
{
    PQMICTL_TRANSACTION_ITEM item;
    PMP_ADAPTER              pAdapter;
    PMP_ADAPTER              pTempAdapter;

    pAdapter = (PMP_ADAPTER)pContext;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MPQCTL_SendIRPCompletion IRP 0x%p(0x%x)\n",
        pAdapter->PortName, pIrp, pIrp->IoStatus.Status)
    );

    if (pIrp->IoStatus.Status != STATUS_SUCCESS)
    {
        // find and free the item
        item = MPQCTL_FindQMICTLTransactionItem(pAdapter, 0, pIrp, &pTempAdapter);
        if (item != NULL) // was in queue
        {
            ExFreePool(item);
        }
        else
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> MPQCTL_SendIRPCompletion: completion too late\n", pAdapter->PortName)
            );
        }
    }
    else
    {
        // may nullify the item->Irp, but it does seem to be necessary
        // for now because the field is not used after IRP completion
    }

    if (pIrp->AssociatedIrp.SystemBuffer != NULL)
    {
        ExFreePool(pIrp->AssociatedIrp.SystemBuffer);
    }

    IoFreeIrp(pIrp);

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MPQCTL_SendIRPCompletion\n", pAdapter->PortName)
    );

    return STATUS_MORE_PROCESSING_REQUIRED;

}  // MPQCTL_SendIRPCompletion

NDIS_STATUS MPQCTL_SendQMICTLRequest
(
    PMP_ADAPTER pAdapter,
    PVOID       Buffer,
    ULONG       BufferLen,
    PVOID       Context
)
{
    PIRP pIrp = NULL;
    PIO_STACK_LOCATION nextstack;
    NTSTATUS Status;
    PMP_ADAPTER returnAdapter;
    PQMICTL_TRANSACTION_ITEM item = (PQMICTL_TRANSACTION_ITEM)Context;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> MPQCTL_SendQMICTLRequest\n", pAdapter->PortName)
    );

    pIrp = IoAllocateIrp((CCHAR)(pAdapter->USBDo->StackSize + 1), FALSE);
    if (pIrp == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> SendQMICTLRequest failed to allocate an IRP\n", pAdapter->PortName)
        );
        ExFreePool(Buffer);
        ExFreePool(Context);
        return NDIS_STATUS_FAILURE;
    }
    pIrp->AssociatedIrp.SystemBuffer = Buffer;
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    nextstack->Parameters.DeviceIoControl.IoControlCode = IOCTL_QCDEV_SEND_CONTROL;
    nextstack->Parameters.DeviceIoControl.InputBufferLength = BufferLen;

    NdisAcquireSpinLock(&pAdapter->QMICTLLock);
    item->Irp = pIrp;
    InsertTailList(&pAdapter->QMICTLTransactionList, &item->List);
    NdisReleaseSpinLock(&pAdapter->QMICTLLock);

    IoSetCompletionRoutine
    (
        pIrp,
        (PIO_COMPLETION_ROUTINE)MPQCTL_SendIRPCompletion,
        (PVOID)pAdapter, // Context,
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
        MPQCTL_SendIRPCompletion(pAdapter->USBDo, pIrp, pAdapter);
    }
    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> <--- MPQCTL_SendQMICTLRequest IRP 0x%p(0x%x)\n", pAdapter->PortName, pIrp, Status)
    );

    if (!NT_SUCCESS(Status))
    {
        return NDIS_STATUS_FAILURE;
    }

    return NDIS_STATUS_SUCCESS;
}  // MPQCTL_SendQMICTLRequest

PVOID MPQCTL_HandleSetInstanceIdReq
(
    PMP_ADAPTER pAdapter,
    PUCHAR      TransactionId,
    PULONG      MsgLen
)
{
    PQCQMI      qmi;
    PQMICTL_MSG qmictl;
    PVOID       msgBuf = NULL;

    *MsgLen += sizeof(QMICTL_SET_INSTANCE_ID_REQ_MSG);
    msgBuf = ExAllocatePool(NonPagedPool, *MsgLen);
    if (msgBuf == NULL)
    {
        return msgBuf;
    }

    *TransactionId = GetPhysicalAdapterQCTLTransactionId(pAdapter);
    qmi = (PQCQMI)msgBuf;
    qmictl = (PQMICTL_MSG)&qmi->SDU;

    qmi->IFType = USB_CTL_MSG_TYPE_QMI;
    qmi->Length = *MsgLen - QCUSB_CTL_MSG_HDR_SIZE;
    qmi->CtlFlags = QMICTL_CTL_FLAG_CMD;
    qmi->QMIType = QMUX_TYPE_CTL;
    qmi->ClientId = 0x00;  // no client id for qmictl

    qmictl->SetInstanceIdReq.CtlFlags = QMICTL_FLAG_REQUEST;
    qmictl->SetInstanceIdReq.TransactionId = *TransactionId;
    qmictl->SetInstanceIdReq.QMICTLType = QMICTL_SET_INSTANCE_ID_REQ;
    qmictl->SetInstanceIdReq.Length = 4; // TLV size
    qmictl->SetInstanceIdReq.TLVType = QCTLV_TYPE_REQUIRED_PARAMETER;
    qmictl->SetInstanceIdReq.TLVLength = 0x0001;
    qmictl->SetInstanceIdReq.Value = pAdapter->DeviceId;

    return msgBuf;
}  // MPQCTL_HandleSetInstanceIdReq

VOID MPQCTL_HandleSetInstanceIdRsp
(
    PMP_ADAPTER pAdapter,
    PQCQMI      qmi
)
{
    PQCQMICTL_MSG             qmictl = (PQCQMICTL_MSG)&qmi->SDU;
    PQMICTL_MSG               qmictl_msg;

    qmictl_msg = (PQMICTL_MSG)&qmi->SDU; // qmictl->Payload;

    if (qmictl_msg->SetInstanceIdRsp.QMIResult == QMI_RESULT_SUCCESS)
    {
        pAdapter->QMI_ID = qmictl_msg->SetInstanceIdRsp.QMI_ID;
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> QMICTL_SET_INSTANCE_ID_RESP (TID %d): QMI_ID=0x%04x\n",
            pAdapter->PortName,
            qmictl->TransactionId,
            qmictl_msg->SetInstanceIdRsp.QMI_ID)
        );
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_SET_INSTANCE_ID_RESP (TID %d): result 0x%x error 0x%x\n",
            pAdapter->PortName,
            qmictl->TransactionId,
            qmictl_msg->SetInstanceIdRsp.QMIResult,
            qmictl_msg->SetInstanceIdRsp.QMIError)
        );
    }

    KeSetEvent(&pAdapter->QMICTLInstanceIdReceivedEvent, IO_NO_INCREMENT, FALSE);

}  // MPQCTL_HandleSetInstanceIdRsp

PVOID MPQCTL_HandleGetVersionReq
(
    PMP_ADAPTER pAdapter,
    PUCHAR      TransactionId,
    PULONG      MsgLen
)
{
    PQCQMI      qmi;
    PQMICTL_MSG qmictl;
    PVOID       msgBuf = NULL;

    *MsgLen += sizeof(QMICTL_GET_VERSION_REQ_MSG);
    msgBuf = ExAllocatePool(NonPagedPool, *MsgLen);
    if (msgBuf == NULL)
    {
        return msgBuf;
    }

    *TransactionId = GetPhysicalAdapterQCTLTransactionId(pAdapter);
    qmi = (PQCQMI)msgBuf;
    qmictl = (PQMICTL_MSG)&qmi->SDU;

    qmi->IFType = USB_CTL_MSG_TYPE_QMI;
    qmi->Length = *MsgLen - QCUSB_CTL_MSG_HDR_SIZE;
    qmi->CtlFlags = QMICTL_CTL_FLAG_CMD;
    qmi->QMIType = QMUX_TYPE_CTL;
    qmi->ClientId = 0x00;  // no client id for qmictl

    qmictl->GetVersionReq.CtlFlags = QMICTL_FLAG_REQUEST;
    qmictl->GetVersionReq.TransactionId = *TransactionId;
    qmictl->GetVersionReq.QMICTLType = QMICTL_GET_VERSION_REQ;
    qmictl->GetVersionReq.Length = 4; // TLV size
    qmictl->GetVersionReq.TLVType = QCTLV_TYPE_REQUIRED_PARAMETER;
    qmictl->GetVersionReq.TLVLength = 0x0001;
    qmictl->GetVersionReq.QMUXTypes = QMUX_TYPE_ALL;

    return msgBuf;
}  // MPQCTL_HandleGetVersionReq

VOID MPQCTL_HandleGetVersionRsp
(
    PMP_ADAPTER pAdapter,
    PQCQMI      qmi
)
{
    PQCQMICTL_MSG             qmictl = (PQCQMICTL_MSG)&qmi->SDU;
    PQMICTL_MSG               qmictl_msg;
    PQMUX_TYPE_VERSION_STRUCT pQmuxTypeVersion;
    int                       msgLen;
    PQMI_TLV_HDR              tlvHdr;
    PCHAR                     pMsg;

    qmictl_msg = (PQMICTL_MSG)&qmi->SDU; // qmictl->Payload;
    msgLen = qmictl_msg->GetVersionRsp.Length;
    pMsg = (PCHAR)qmictl_msg + QCQMICTL_MSG_HDR_SIZE;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> QMICTL_GET_VERSION_RESP (Len %d)\n",
        pAdapter->PortName, msgLen)
    );
    if (qmictl_msg->GetVersionRsp.QMIResult == QMI_RESULT_SUCCESS)
    {
        USHORT i;

        // adjust by two mandatory TLVs
        msgLen -= sizeof(QMI_TLV_HDR);
        msgLen -= qmictl_msg->GetVersionRsp.TLVLength;
        msgLen -= sizeof(QMI_TLV_HDR);
        msgLen -= qmictl_msg->GetVersionRsp.TLV2Length;
        pMsg += sizeof(QMI_TLV_HDR);
        pMsg += qmictl_msg->GetVersionRsp.TLVLength;
        pMsg += sizeof(QMI_TLV_HDR);
        pMsg += qmictl_msg->GetVersionRsp.TLV2Length;

        pQmuxTypeVersion = (PQMUX_TYPE_VERSION_STRUCT) & (qmictl_msg->GetVersionRsp.TypeVersion);
        for (i = 0; i < qmictl_msg->GetVersionRsp.NumElements; i++)
        {
            // if (pQmuxTypeVersion->QMUXType < QMUX_TYPE_MAX)
            {
                pAdapter->QMUXVersion[pQmuxTypeVersion->QMUXType].Major =
                    pQmuxTypeVersion->MajorVersion;
                pAdapter->QMUXVersion[pQmuxTypeVersion->QMUXType].Minor =
                    pQmuxTypeVersion->MinorVersion;

                if ((pQmuxTypeVersion->QMUXType == QMUX_TYPE_QOS) && (pAdapter->MPDisableQoS < 2))
                {
                    pAdapter->IsQosPresent = TRUE;
                }

                if (pQmuxTypeVersion->QMUXType == QMUX_TYPE_WDS_ADMIN)
                {
                    pAdapter->IsWdsAdminPresent = TRUE;
                }

                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_TRACE,
                    ("<%s> QMICTL_GET_VERSION_RESP (TID %d): Type %u Ver %u:%u\n",
                    pAdapter->PortName,
                    qmictl->TransactionId,
                    pQmuxTypeVersion->QMUXType,
                    pQmuxTypeVersion->MajorVersion, pQmuxTypeVersion->MinorVersion)
                );
            }
            /***
            else
            {
               // warning
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL,
                  MP_DBG_LEVEL_ERROR,
                  ("<%s> QMICTL_GET_VERSION_RESP (TID %d)-wrong type: Type %u Ver %u:%u\n",
                    pAdapter->PortName,
                    qmictl->TransactionId,
                    pQmuxTypeVersion->QMUXType,
                    pQmuxTypeVersion->MajorVersion, pQmuxTypeVersion->MinorVersion)
               );
            }
            ***/
            pQmuxTypeVersion += 1;
        }

        // parse optional TLV(s)
        while (msgLen >= sizeof(QMI_TLV_HDR))
        {
            UCHAR numInstances;
            PADDENDUM_VERSION_PREAMBLE preamble;

            tlvHdr = (PQMI_TLV_HDR)pMsg;

            // DbgPrint("GET_VER: MsgLen=%d\n", msgLen);

            switch (tlvHdr->TLVType)
            {
                case QMICTL_GET_VERSION_RSP_TLV_TYPE_ADD_VERSION:
                {
                    PCHAR pAddendumVer;

                    msgLen -= sizeof(QMI_TLV_HDR);
                    msgLen -= tlvHdr->TLVLength;
                    if (msgLen < 0)
                    {
                        QCNET_DbgPrint
                        (
                            MP_DBG_MASK_CONTROL,
                            MP_DBG_LEVEL_ERROR,
                            ("<%s> QMICTL_GET_VERSION_RESP (TID %d)-AddendumVersion error TLVlen=%d\n",
                            pAdapter->PortName, qmictl->TransactionId, tlvHdr->TLVLength)
                        );
                        break;
                    }
                    pMsg += sizeof(QMI_TLV_HDR);
                    pAddendumVer = pMsg;
                    preamble = (PADDENDUM_VERSION_PREAMBLE)pAddendumVer;
                    pMsg += tlvHdr->TLVLength;

                    // store the label
                    RtlCopyMemory
                    (
                        (PVOID)pAdapter->AddendumVersionLabel,
                        (PVOID)&preamble->Label,
                        preamble->LabelLength
                    );

                    pAddendumVer += sizeof(UCHAR); //label length field 
                    pAddendumVer += preamble->LabelLength; // skip the label
                    numInstances = *((PUCHAR)pAddendumVer);
                    pAddendumVer += sizeof(UCHAR); // advance to the version section

                    pQmuxTypeVersion = (PQMUX_TYPE_VERSION_STRUCT)pAddendumVer;
                    for (i = 0; i < numInstances; i++)
                    {
                        pAdapter->QMUXVersion[pQmuxTypeVersion->QMUXType].AddendumMajor =
                            pQmuxTypeVersion->MajorVersion;
                        pAdapter->QMUXVersion[pQmuxTypeVersion->QMUXType].AddendumMinor =
                            pQmuxTypeVersion->MinorVersion;
                        QCNET_DbgPrint
                        (
                            MP_DBG_MASK_CONTROL,
                            MP_DBG_LEVEL_TRACE,
                            ("<%s> QMICTL_GET_VERSION_RESP (TID %d): Type %u Addendum %u:%u\n",
                            pAdapter->PortName,
                            qmictl->TransactionId,
                            pQmuxTypeVersion->QMUXType,
                            pQmuxTypeVersion->MajorVersion, pQmuxTypeVersion->MinorVersion)
                        );
                        pQmuxTypeVersion += 1;
                    }

                    break;
                }
                default:
                {
                    pMsg += sizeof(QMI_TLV_HDR);
                    pMsg += tlvHdr->TLVLength;
                    msgLen -= sizeof(QMI_TLV_HDR);
                    msgLen -= tlvHdr->TLVLength;

                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_ERROR,
                        ("<%s> QMICTL_GET_VERSION_RESP (TID %d)-wrong TLV: 0x%x TLVLen: %d\n",
                        pAdapter->PortName, qmictl->TransactionId, tlvHdr->TLVType, tlvHdr->TLVLength)
                    );

                    msgLen = -1;  // bail out
                    break;
                }
            }  // switch
        }  // while
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_GET_VERSION_RESP (TID %d): result 0x%x error 0x%x\n",
            pAdapter->PortName,
            qmictl->TransactionId,
            qmictl_msg->GetVersionRsp.QMIResult,
            qmictl_msg->GetVersionRsp.QMIError)
        );
    }

#ifdef QC_IP_MODE
    if ((pAdapter->QMUXVersion[QMUX_TYPE_CTL].Major >= 1) &&
        (pAdapter->QMUXVersion[QMUX_TYPE_CTL].Minor >= 3))
    {
        if (pAdapter->MPDisableIPMode == FALSE)
        {
            pAdapter->IsLinkProtocolSupported = TRUE;
        }
    }
    else
    {
        pAdapter->IsLinkProtocolSupported = FALSE;
    }
#endif // QC_IP_MODE

#ifdef QCMP_DL_TLP
    if ((pAdapter->QMUXVersion[QMUX_TYPE_CTL].Major >= 1) &&
        (pAdapter->QMUXVersion[QMUX_TYPE_CTL].Minor >= 6))
    {
        pAdapter->IsDLTLPSupported = TRUE;
    }
    else
    {
        pAdapter->IsDLTLPSupported = FALSE;
    }
#endif // QCMP_DL_TLP

    if ((pAdapter->QMUXVersion[QMUX_TYPE_WDS_ADMIN].Major >= 1) &&
        (pAdapter->QMUXVersion[QMUX_TYPE_WDS_ADMIN].Minor < 11))
    {
        pAdapter->MPEnableQMAPV1 = 0;
        pAdapter->MPEnableQMAPV4 = 0;
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
        pAdapter->MPEnableQMAPV2 = 0;
        pAdapter->MPEnableQMAPV3 = 0;
#endif
#endif      
    }
    if ((pAdapter->QMUXVersion[QMUX_TYPE_WDS_ADMIN].Major >= 1) &&
        (pAdapter->QMUXVersion[QMUX_TYPE_WDS_ADMIN].Minor >= 19))
    {
        pAdapter->WdsEnableUlParams = TRUE;
    }

    if ((pAdapter->QMUXVersion[QMUX_TYPE_NAS].Major >= 1) &&
        (pAdapter->QMUXVersion[QMUX_TYPE_NAS].Minor >= 8))
    {
        pAdapter->IsNASSysInfoPresent = TRUE;
    }

    KeSetEvent(&pAdapter->QMICTLVersionReceivedEvent, IO_NO_INCREMENT, FALSE);

}  // MPQCTL_HandleGetVersionRsp

PVOID MPQCTL_HandleGetClientIdReq
(
    PMP_ADAPTER pAdapter,
    UCHAR       QMIType,
    PUCHAR      TransactionId,
    PULONG      MsgLen
)
{
    PQCQMI      qmi;
    PQMICTL_MSG qmictl;
    PVOID       msgBuf = NULL;

    *MsgLen += sizeof(QMICTL_GET_CLIENT_ID_REQ_MSG);
    msgBuf = ExAllocatePool(NonPagedPool, *MsgLen);
    if (msgBuf == NULL)
    {
        return msgBuf;
    }

    *TransactionId = GetPhysicalAdapterQCTLTransactionId(pAdapter);
    qmi = (PQCQMI)msgBuf;
    qmictl = (PQMICTL_MSG)&qmi->SDU;

    qmi->IFType = USB_CTL_MSG_TYPE_QMI;
    qmi->Length = *MsgLen - QCUSB_CTL_MSG_HDR_SIZE;
    qmi->CtlFlags = QMICTL_CTL_FLAG_CMD;
    qmi->QMIType = QMUX_TYPE_CTL;
    qmi->ClientId = 0x00;  // no client id for qmictl

    qmictl->GetClientIdReq.CtlFlags = QMICTL_FLAG_REQUEST;
    qmictl->GetClientIdReq.TransactionId = *TransactionId;
    qmictl->GetClientIdReq.QMICTLType = QMICTL_GET_CLIENT_ID_REQ;
    qmictl->GetClientIdReq.Length = sizeof(QMICTL_GET_CLIENT_ID_REQ_MSG) -
        sizeof(QCQMICTL_MSG_HDR);

    qmictl->GetClientIdReq.TLVType = QCTLV_TYPE_REQUIRED_PARAMETER;
    qmictl->GetClientIdReq.TLVLength = 1;
    qmictl->GetClientIdReq.QMIType = QMIType;

    return msgBuf;

}  // MPQCTL_HandleGetClientIdReq

VOID MPQCTL_HandleGetClientIdRsp
(
    PMP_ADAPTER              pAdapter,
    PQCQMI                   qmi,
    PQMICTL_TRANSACTION_ITEM item
)
{
    PQCQMICTL_MSG          qmictl = (PQCQMICTL_MSG)&qmi->SDU;
    PQMICTL_MSG            qmictl_msg;
    PUCHAR                 pQMIType = NULL;
    PUCHAR                 pClientId = NULL;
    PKEVENT                pClientIdReceivedEvent;
    PMPIOC_DEV_INFO        pIoDev = NULL;

    qmictl_msg = (PQMICTL_MSG)&qmi->SDU; // qmictl->Payload;

    if (item == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_GET_CLIENT_ID_RESP (TID %d): no req found.\n",
            pAdapter->PortName, qmictl->TransactionId)
        );
        return;
    }

    if (item->Context == (PVOID)pAdapter)
    {
        // for internal client
        pClientIdReceivedEvent = &pAdapter->ClientIdReceivedEvent;
    }
    else
    {
        // for external client
        pIoDev = (PMPIOC_DEV_INFO)item->Context;
        pClientIdReceivedEvent = &pIoDev->ClientIdReceivedEvent;
    }

    if (qmictl_msg->GetClientIdRsp.QMIResult == QMI_RESULT_SUCCESS)
    {
        if (item->Context == (PVOID)pAdapter)
        {
            pAdapter->ClientId[qmictl_msg->GetClientIdRsp.QMIType] = qmictl_msg->GetClientIdRsp.ClientId;
            pAdapter->QMIType = qmictl_msg->GetClientIdRsp.QMIType;  // useless
        }
        else
        {
            pIoDev->QMIType = qmictl_msg->GetClientIdRsp.QMIType;
            pIoDev->ClientId = qmictl_msg->GetClientIdRsp.ClientId;
        }

        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_GET_CLIENT_ID_RESP (TID %d): CID %u type %u\n",
            pAdapter->PortName,
            qmictl->TransactionId,
            qmictl_msg->GetClientIdRsp.ClientId,
            qmictl_msg->GetClientIdRsp.QMIType)
        );
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_GET_CLIENT_ID_RESP (TID %d): result 0x%x error 0x%x\n",
            pAdapter->PortName,
            qmictl->TransactionId,
            qmictl_msg->GetClientIdRsp.QMIResult,
            qmictl_msg->GetClientIdRsp.QMIError)
        );
    }

    KeSetEvent(pClientIdReceivedEvent, IO_NO_INCREMENT, FALSE);

}  // MPQCTL_HandleGetClientIdRsp

PVOID MPQCTL_HandleReleaseClientIdReq
(
    PMP_ADAPTER pAdapter,
    UCHAR       QMIType,
    UCHAR       ClientId,
    PUCHAR      TransactionId,
    PULONG      MsgLen
)
{
    PQCQMI      qmi;
    PQMICTL_MSG qmictl;
    PVOID       msgBuf = NULL;

    *MsgLen += sizeof(QMICTL_RELEASE_CLIENT_ID_REQ_MSG);
    msgBuf = ExAllocatePool(NonPagedPool, *MsgLen);
    if (msgBuf == NULL)
    {
        return msgBuf;
    }

    *TransactionId = GetPhysicalAdapterQCTLTransactionId(pAdapter);
    qmi = (PQCQMI)msgBuf;
    qmictl = (PQMICTL_MSG)&qmi->SDU;

    qmi->IFType = USB_CTL_MSG_TYPE_QMI;
    qmi->Length = *MsgLen - QCUSB_CTL_MSG_HDR_SIZE;
    qmi->CtlFlags = QMICTL_CTL_FLAG_CMD;
    qmi->QMIType = QMUX_TYPE_CTL;
    qmi->ClientId = 0x00;  // no client id for qmictl

    qmictl->ReleaseClientIdReq.CtlFlags = QMICTL_FLAG_REQUEST;
    qmictl->ReleaseClientIdReq.TransactionId = *TransactionId;
    qmictl->ReleaseClientIdReq.QMICTLType = QMICTL_RELEASE_CLIENT_ID_REQ;
    qmictl->ReleaseClientIdReq.Length = sizeof(QMICTL_RELEASE_CLIENT_ID_REQ_MSG) -
        sizeof(QCQMICTL_MSG_HDR);

    qmictl->ReleaseClientIdReq.TLVType = QCTLV_TYPE_REQUIRED_PARAMETER;
    qmictl->ReleaseClientIdReq.TLVLength = 2;
    qmictl->ReleaseClientIdReq.QMIType = QMIType;
    qmictl->ReleaseClientIdReq.ClientId = ClientId;

    return msgBuf;
}  // MPQCTL_HandleReleaseClientIdReq

VOID MPQCTL_HandleReleaseClientIdRsp
(
    PMP_ADAPTER              pAdapter,
    PQCQMI                   qmi,
    PQMICTL_TRANSACTION_ITEM item
)
{
    PQCQMICTL_MSG          qmictl = (PQCQMICTL_MSG)&qmi->SDU;
    PQMICTL_MSG            qmictl_msg;
    PUCHAR                 pQMIType = NULL;
    PUCHAR                 pClientId = NULL;
    PKEVENT                pClientIdReleasedEvent;
    BOOLEAN                bMatch = FALSE;


    qmictl_msg = (PQMICTL_MSG)&qmi->SDU; // qmictl->Payload;

    if (item == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_RELEASE_CLIENT_ID_RESP (TID %d): no req found.\n",
            pAdapter->PortName, qmictl->TransactionId)
        );
        return;
    }

    if (qmictl_msg->ReleaseClientIdRsp.QMIResult != QMI_RESULT_SUCCESS)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_RELEASE_CLIENT_ID_RESP (TID %d): result 0x%x error 0x%x\n",
            pAdapter->PortName,
            qmictl->TransactionId,
            qmictl_msg->ReleaseClientIdRsp.QMIResult,
            qmictl_msg->ReleaseClientIdRsp.QMIError)
        );

        // Reset CID and type so that bMatch is FALSE
        qmictl_msg->ReleaseClientIdRsp.ClientId = 0;
        qmictl_msg->ReleaseClientIdRsp.QMIType = 0;
    }

    if (item->Context == (PVOID)pAdapter)
    {
        // for internal client
        pQMIType = &pAdapter->QMIType;
        pClientId = &(pAdapter->ClientId[qmictl_msg->ReleaseClientIdRsp.QMIType]);
        pClientIdReleasedEvent = &pAdapter->ClientIdReleasedEvent;
        if (*pClientId == qmictl_msg->ReleaseClientIdRsp.ClientId)
        {
            bMatch = TRUE;
        }
    }
    else
    {
        // for external client
        PMPIOC_DEV_INFO pIoDev = (PMPIOC_DEV_INFO)item->Context;
        pQMIType = &pIoDev->QMIType;
        pClientId = &pIoDev->ClientId;
        pClientIdReleasedEvent = &pIoDev->ClientIdReleasedEvent;
        if ((*pClientId == qmictl_msg->ReleaseClientIdRsp.ClientId) &&
            (*pQMIType == qmictl_msg->ReleaseClientIdRsp.QMIType))
        {
            bMatch = TRUE;
        }
    }

    if (bMatch == TRUE)
    {
        *pClientId = 0;
        *pQMIType = 0;

        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> QMICTL_RELEASE_CLIENT_ID_RESP (TID %d): CID %u type %u\n",
            pAdapter->PortName,
            qmictl->TransactionId,
            qmictl_msg->ReleaseClientIdRsp.ClientId,
            qmictl_msg->ReleaseClientIdRsp.QMIType)
        );
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_RELEASE_CLIENT_ID_RESP (TID %d): wrong CID %u type %u\n",
            pAdapter->PortName,
            qmictl->TransactionId,
            qmictl_msg->ReleaseClientIdRsp.ClientId,
            qmictl_msg->ReleaseClientIdRsp.QMIType)
        );
    }

    // notify the client
    KeSetEvent(pClientIdReleasedEvent, IO_NO_INCREMENT, FALSE);

}  // MPQCTL_HandleReleaseClientIdRsp

VOID MPQCTL_HandleRevokeClientIdInd
(
    PMP_ADAPTER              pAdapter,
    PQCQMI                   qmi
)
{
    PQCQMICTL_MSG          qmictl = (PQCQMICTL_MSG)&qmi->SDU;
    PQMICTL_MSG            qmictl_msg;
    PUCHAR                 pQMIType = NULL;
    PUCHAR                 pClientId = NULL;
    PMPIOC_DEV_INFO        pIoDev;

    qmictl_msg = (PQMICTL_MSG)&qmi->SDU; // qmictl->Payload;

    // search for correct client instance
    if (qmictl_msg->RevokeClientIdInd.ClientId == pAdapter->ClientId[qmictl_msg->RevokeClientIdInd.QMIType])
    {
        pQMIType = &pAdapter->QMIType;
        pClientId = &(pAdapter->ClientId[qmictl_msg->RevokeClientIdInd.QMIType]);
    }
    else
    {
        pIoDev = MPIOC_FindIoDevice(pAdapter, NULL, qmi, NULL, NULL, 0);
        if (pIoDev != NULL)
        {
            pQMIType = &pIoDev->QMIType;
            pClientId = &pIoDev->ClientId;
        }
        else
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> QMICTL_REVOKE_CLIENT_ID_IND (TID %d): CID %u type %u - err: no IoDev\n",
                pAdapter->PortName,
                qmictl->TransactionId,
                qmictl_msg->RevokeClientIdInd.ClientId,
                qmictl_msg->RevokeClientIdInd.QMIType)
            );
            return;
        }
    }

    *pClientId = 0;
    *pQMIType = MP_DEV_TYPE_CLIENT;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> QMICTL_REVOKE_CLIENT_ID_IND (TID %d): CID %u type %u\n",
        pAdapter->PortName,
        qmictl->TransactionId,
        qmictl_msg->RevokeClientIdInd.ClientId,
        qmictl_msg->RevokeClientIdInd.QMIType)
    );
}  // MPQCTL_HandleRevokeClientIdInd

VOID MPQCTL_HandleInvalidClientIdInd
(
    PMP_ADAPTER              pAdapter,
    PQCQMI                   qmi
)
{
    PQCQMICTL_MSG          qmictl = (PQCQMICTL_MSG)&qmi->SDU;
    PQMICTL_MSG            qmictl_msg;

    qmictl_msg = (PQMICTL_MSG)&qmi->SDU; // qmictl->Payload;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_ERROR,
        ("<%s> QMICTL_INVALID_CLIENT_ID_IND (TID %d): CID 0x%d type %d\n",
        pAdapter->PortName,
        qmictl->TransactionId,
        qmictl_msg->InvalidClientIdInd.ClientId,
        qmictl_msg->InvalidClientIdInd.QMIType)
    );
}  // MPQCTL_HandleInvalidClientIdInd

PVOID MPQCTL_HandleSetDataFormatReq
(
    PMP_ADAPTER pAdapter,
    PUCHAR      TransactionId,
    PULONG      MsgLen
)
{
    PQCQMI      qmi;
    PQMICTL_MSG qmictl;
    PVOID       msgBuf = NULL;
    PUCHAR      bufPtr;

    *MsgLen += sizeof(QMICTL_SET_DATA_FORMAT_REQ_MSG);

#ifdef QC_IP_MODE
    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_ERROR,
        ("<%s> IPO: _HandleSetDataFormatReq: LinkProtocol %d\n",
        pAdapter->PortName, pAdapter->IsLinkProtocolSupported)
    );

    if (pAdapter->IsLinkProtocolSupported == TRUE)
    {
        *MsgLen += sizeof(QMICTL_SET_DATA_FORMAT_TLV_LINK_PROT);
    }
#endif // QC_IP_MODE      

#ifdef QCMP_UL_TLP
    if (pAdapter->MPEnableTLP != 0) // registry setting
    {
        *MsgLen += sizeof(QMICTL_SET_DATA_FORMAT_TLV_UL_TLP);
    }

#ifdef QCMP_DL_TLP
    if ((pAdapter->IsDLTLPSupported == TRUE) && (pAdapter->MPEnableDLTLP != 0)) // registry setting
    {
        *MsgLen += sizeof(QMICTL_SET_DATA_FORMAT_TLV_DL_TLP);
    }
#endif // QCMP_DL_TLP

#endif // QCMP_UL_TLP
    msgBuf = ExAllocatePool(NonPagedPool, *MsgLen);
    if (msgBuf == NULL)
    {
        return msgBuf;
    }

    *TransactionId = GetPhysicalAdapterQCTLTransactionId(pAdapter);
    qmi = (PQCQMI)msgBuf;
    qmictl = (PQMICTL_MSG)&qmi->SDU;

    qmi->IFType = USB_CTL_MSG_TYPE_QMI;
    qmi->Length = *MsgLen - QCUSB_CTL_MSG_HDR_SIZE;
    qmi->CtlFlags = QMICTL_CTL_FLAG_CMD;
    qmi->QMIType = QMUX_TYPE_CTL;
    qmi->ClientId = 0x00;  // no client id for qmictl

    qmictl->SetDataFormatReq.CtlFlags = QMICTL_FLAG_REQUEST;
    qmictl->SetDataFormatReq.TransactionId = *TransactionId;
    qmictl->SetDataFormatReq.QMICTLType = QMICTL_SET_DATA_FORMAT_REQ;
    qmictl->SetDataFormatReq.Length = sizeof(QMICTL_SET_DATA_FORMAT_REQ_MSG) -
        sizeof(QCQMICTL_MSG_HDR);

    qmictl->SetDataFormatReq.TLVType = QCTLV_TYPE_REQUIRED_PARAMETER;
    qmictl->SetDataFormatReq.TLVLength = 1;
    if (pAdapter->IsQosPresent == TRUE)
    {
        qmictl->SetDataFormatReq.DataFormat = 1;  // Enable QoS header
    }
    else
    {
        qmictl->SetDataFormatReq.DataFormat = 0;  // Disable QoS
    }

    bufPtr = (PUCHAR) & (qmictl->SetDataFormatReq.DataFormat);
    bufPtr += qmictl->SetDataFormatReq.TLVLength;

#ifdef QC_IP_MODE
    if (pAdapter->IsLinkProtocolSupported == TRUE)
    {
        PQMICTL_SET_DATA_FORMAT_TLV_LINK_PROT linkProto;

        linkProto = (PQMICTL_SET_DATA_FORMAT_TLV_LINK_PROT)bufPtr;
        linkProto->TLVType = SET_DATA_FORMAT_TLV_TYPE_LINK_PROTO;
        linkProto->TLVLength = 2;
        linkProto->LinkProt = (SET_DATA_FORMAT_LINK_PROTO_IP | SET_DATA_FORMAT_LINK_PROTO_ETH);
        qmictl->SetDataFormatReq.Length += sizeof(QMICTL_SET_DATA_FORMAT_TLV_LINK_PROT);
        bufPtr = (PUCHAR) & (linkProto->LinkProt);
        bufPtr += linkProto->TLVLength;
    }
#endif // QC_IP_MODE

#ifdef QCMP_UL_TLP
    pAdapter->TLPEnabled = FALSE;
    if (pAdapter->MPEnableTLP != 0) // registry setting
    {
        PQMICTL_SET_DATA_FORMAT_TLV_UL_TLP ulTlp;

        ulTlp = (PQMICTL_SET_DATA_FORMAT_TLV_UL_TLP)bufPtr;
        ulTlp->TLVType = SET_DATA_FORMAT_TLV_TYPE_UL_TLP;
        ulTlp->TLVLength = 1;
        ulTlp->UlTlpSetting = 1;
        qmictl->SetDataFormatReq.Length += sizeof(QMICTL_SET_DATA_FORMAT_TLV_UL_TLP);
        bufPtr = (PUCHAR) & (ulTlp->UlTlpSetting);
        bufPtr += ulTlp->TLVLength;
    }
#endif // QCMP_UL_TLP

#ifdef QCMP_DL_TLP
    pAdapter->TLPDLEnabled = FALSE;
    if ((pAdapter->IsDLTLPSupported == TRUE) && (pAdapter->MPEnableDLTLP != 0)) // registry setting
    {
        PQMICTL_SET_DATA_FORMAT_TLV_DL_TLP dlTlp;

        dlTlp = (PQMICTL_SET_DATA_FORMAT_TLV_DL_TLP)bufPtr;
        dlTlp->TLVType = SET_DATA_FORMAT_TLV_TYPE_DL_TLP;
        dlTlp->TLVLength = 1;
        dlTlp->DlTlpSetting = 1;
        qmictl->SetDataFormatReq.Length += sizeof(QMICTL_SET_DATA_FORMAT_TLV_DL_TLP);
    }
#endif // QCMP_DL_TLP

    return msgBuf;

}  // MPQCTL_HandleSetDataFormatReq

VOID MPQCTL_HandleSetDataFormatRsp
(
    PMP_ADAPTER              pAdapter,
    PQCQMI                   qmi,
    PQMICTL_TRANSACTION_ITEM item
)
{
    PQCQMICTL_MSG          qmictl = (PQCQMICTL_MSG)&qmi->SDU;
    PQMICTL_MSG            qmictl_msg;
    PUCHAR                 pClientId = NULL;
    PKEVENT                pDataFormatSetEvent;
    PQCQMUX_TLV            tlv;
    LONG                   remainingLength = 0;

    qmictl_msg = (PQMICTL_MSG)&qmi->SDU; // qmictl->Payload;

    if (item == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_SET_DATA_FORMAT_RESP (TID %d): no req found.\n",
            pAdapter->PortName, qmictl->TransactionId)
        );
        return;
    }

    if (item->Context == (PVOID)pAdapter)
    {
        // this should not happen
        pClientId = &pAdapter->ClientId[QMUX_TYPE_CTL];  // useless
        pDataFormatSetEvent = &pAdapter->DataFormatSetEvent;
    }
    else
    {
        PMPIOC_DEV_INFO pIoDev = (PMPIOC_DEV_INFO)item->Context;
        pClientId = &pIoDev->ClientId;
        pDataFormatSetEvent = &pIoDev->DataFormatSetEvent;
    }

    if (qmictl_msg->SetDataFormatRsp.QMIResult == QMI_RESULT_SUCCESS)
    {
        if (pAdapter->IsQosPresent == TRUE)
        {
            pAdapter->QosEnabled = TRUE;
        }

        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_SET_DATA_FORMAT_RESP (TID %d): CID %u QOS %u\n",
            pAdapter->PortName, qmictl->TransactionId, *pClientId, pAdapter->QosEnabled)
        );
    }
    else
    {
        pAdapter->QosEnabled = FALSE;
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_SET_DATA_FORMAT_RESP (TID %d): result 0x%x error 0x%x QOS %u\n",
            pAdapter->PortName, qmictl->TransactionId,
            qmictl_msg->SetDataFormatRsp.QMIResult,
            qmictl_msg->SetDataFormatRsp.QMIError, pAdapter->QosEnabled)
        );
    }

    // point to the first TLV
    tlv = (PQCQMUX_TLV) & (qmictl_msg->SetDataFormatRsp.TLVType);
    remainingLength = qmictl_msg->SetDataFormatRsp.Length; // length of all TLVs
    tlv = MPQMI_GetNextTLV(pAdapter, tlv, &remainingLength);

    while (tlv != NULL)
    {
        switch (tlv->Type)
        {
#ifdef QC_IP_MODE
            case SET_DATA_FORMAT_TLV_TYPE_LINK_PROTO:
            {
                PQMICTL_SET_DATA_FORMAT_TLV_LINK_PROT linkProto;

                if (pAdapter->IsLinkProtocolSupported == TRUE)
                {
                    linkProto = (PQMICTL_SET_DATA_FORMAT_TLV_LINK_PROT)tlv;
                    pAdapter->IPModeEnabled = (linkProto->LinkProt == SET_DATA_FORMAT_LINK_PROTO_IP);

                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_DETAIL,
                        ("<%s> IPO: QMICTL_SET_DATA_FORMAT_RESP (TID %d CID %u): IP_MODE %d\n",
                        pAdapter->PortName, qmictl->TransactionId, *pClientId,
                        pAdapter->IPModeEnabled)
                    );
                }
                else
                {
                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_ERROR,
                        ("<%s> IPO: QMICTL_SET_DATA_FORMAT_RESP (TID %d CID %u): wrong LINK_PROTO\n",
                        pAdapter->PortName, qmictl->TransactionId, *pClientId)
                    );
                }
                break;
            }
#endif // QC_IP_MODE

#ifdef QCMP_UL_TLP
            case SET_DATA_FORMAT_TLV_TYPE_UL_TLP:
            {
                PQMICTL_SET_DATA_FORMAT_TLV_UL_TLP tlp;

                tlp = (PQMICTL_SET_DATA_FORMAT_TLV_UL_TLP)tlv;
                if (tlp->UlTlpSetting == 1)
                {
                    pAdapter->TLPEnabled = TRUE;
                    pAdapter->MPQuickTx = 1;
                }
                else if (tlp->UlTlpSetting == 0)
                {
                    pAdapter->TLPEnabled = FALSE;
                    // pAdapter->MPQuickTx = 0;
                }
                break;
            }
#endif // QCMP_UL_TLP

#ifdef QCMP_DL_TLP
            case SET_DATA_FORMAT_TLV_TYPE_DL_TLP:
            {
                PQMICTL_SET_DATA_FORMAT_TLV_DL_TLP tlp;

                tlp = (PQMICTL_SET_DATA_FORMAT_TLV_DL_TLP)tlv;
                if (tlp->DlTlpSetting == 1)
                {
                    pAdapter->TLPDLEnabled = TRUE;
                }
                else if (tlp->DlTlpSetting == 0)
                {
                    pAdapter->TLPDLEnabled = FALSE;
                }
                break;
            }
#endif // QCMP_DL_TLP

#ifdef MP_QCQOS_ENABLED
            case SET_DATA_FORMAT_TLV_TYPE_QOS_SETTING:
            {
                PQMICTL_SET_DATA_FORMAT_TLV_QOS_SETTING qos;

                qos = (PQMICTL_SET_DATA_FORMAT_TLV_QOS_SETTING)tlv;
                if ((qos->QosSetting == 1) && (pAdapter->IsQosPresent == TRUE))
                {
                    pAdapter->QosEnabled = TRUE;
                }
                else
                {
                    pAdapter->QosEnabled = FALSE;
                }
                break;
            }
#endif // MP_QCQOS_ENABLED

            default:
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                    ("<%s> SET_DATA_FORMAT_RESP: unknown TLV 0x%x\n",
                    pAdapter->PortName, tlv->Type)
                );
                break;
            }
        } // switch
        tlv = MPQMI_GetNextTLV(pAdapter, tlv, &remainingLength);
    } // while

#ifdef QC_IP_MODE
#ifdef QCMP_UL_TLP
#ifdef QCMP_DL_TLP
    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
        ("<%s> SET_DATA_FORMAT_RESP: QOS %d IP_mode %d UL_TLV %d DL_TLP %d\n", pAdapter->PortName,
        pAdapter->QosEnabled, pAdapter->IPModeEnabled, pAdapter->TLPEnabled, pAdapter->TLPDLEnabled)
    );
#endif
#endif
#endif
    KeSetEvent(pDataFormatSetEvent, IO_NO_INCREMENT, FALSE);

}  // MPQCTL_HandleSetDataFormatRsp

NDIS_STATUS MPQCTL_RemoveOldTransactionItem
(
    PMP_ADAPTER pAdapter,
    UCHAR       Tid
)
{
    PQMICTL_TRANSACTION_ITEM item;
    PMP_ADAPTER pTempAdapter;

    item = MPQCTL_FindQMICTLTransactionItem(pAdapter, Tid, NULL, &pTempAdapter);
    if (item == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> RemoveOldTransactionItem: no QMICTL item found for TID %d\n",
            pAdapter->PortName, Tid)
        );
        return NDIS_STATUS_FAILURE;
    }

    ExFreePool(item);

    return NDIS_STATUS_SUCCESS;

}  // MPQCTL_RemoveOldTransactionItem

#ifdef QCMP_SUPPORT_CTRL_QMIC

VOID MPQMI_SendQCTLToExternalClient
(
    PMP_ADAPTER pAdapter,
    PQCQMI      qmi,
    ULONG       TotalDataLength
)
{
    PQCQMUX         qmux;
    PQMUX_MSG       qmux_msg;           // TLV's
    PMPIOC_DEV_INFO pIocDev = NULL;
    BOOLEAN         bDone = FALSE;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_TRACE,
        ("<%s> ---> SendQCTLToExternalClient\n", pAdapter->PortName)
    );

    qmux = (PQCQMUX)&qmi->SDU;
    qmux_msg = (PQMUX_MSG)&qmux->Message;

    QCNET_DbgPrint
    (
        (MP_DBG_MASK_CONTROL | MP_DBG_MASK_QOS),
        MP_DBG_LEVEL_DETAIL,
        ("<%s> SendQCTLToExternalClient: CID=%u\n", pAdapter->PortName, qmi->ClientId)
    );

    while (bDone == FALSE)
    {
        bDone = TRUE;  // may use this for broadcast in the future

        pIocDev = MPIOC_FindIoDevice(pAdapter, NULL, qmi, NULL, NULL, 0); // based on QMIType & ClientId
        if (pIocDev == NULL)
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> QCTL: no match, dropped.\n", pAdapter->PortName)
            );

            bDone = TRUE;
        }
        else
        {
            BOOLEAN          bDataFilled = FALSE;
            PMPIOC_READ_ITEM rdItem;
            ULONG            len;

            // sanity checking
            if (TotalDataLength <= sizeof(QCQMI_HDR))
            {
                // data too short
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_ERROR,
                    ("<%s> QCTL: 1-data too short for external client %d type %d, dropped.\n",
                    pAdapter->PortName, qmi->ClientId, qmi->QMIType)
                );
                return;
            }
            len = TotalDataLength - sizeof(QCQMI_HDR);
            if (len < QCQMUX_HDR_SIZE)
            {
                // data too short
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_ERROR,
                    ("<%s> QCTL: 2-data too short for external client %d type %d, dropped.\n",
                    pAdapter->PortName, qmi->ClientId, qmi->QMIType)
                );
                return;
            }

            // Try to fill the read IRP in ReadIrpQueue,
            // otherwise, put data item to pIocDev->ReadDataQueue
            NdisAcquireSpinLock(&pIocDev->IoLock);

            // fill a read IRP
            while (!IsListEmpty(&pIocDev->ReadIrpQueue))
            {
                ULONG              outlen;
                PIO_STACK_LOCATION irpStack;
                PLIST_ENTRY        headOfList;
                PIRP               pIrp;

                DbgPrint("<%s> QCTLtoExternalClient: get read request from client\n", pAdapter->PortName);

                headOfList = RemoveHeadList(&pIocDev->ReadIrpQueue);
                pIrp = CONTAINING_RECORD(headOfList, IRP, Tail.Overlay.ListEntry);

                if (IoSetCancelRoutine(pIrp, NULL) != NULL)
                {
                    irpStack = IoGetCurrentIrpStackLocation(pIrp);
                    outlen = irpStack->Parameters.Read.Length;

                    if (outlen >= len)
                    {
                        NdisMoveMemory
                        (
                            (PVOID)pIrp->AssociatedIrp.SystemBuffer,
                            (PVOID)qmux,
                            len
                        );
                        pIrp->IoStatus.Information = len;
                        pIrp->IoStatus.Status = STATUS_SUCCESS;
                        bDataFilled = TRUE;

                        QCNET_DbgPrint
                        (
                            MP_DBG_MASK_CONTROL,
                            MP_DBG_LEVEL_DETAIL,
                            ("<%s> QCTL: msg delivered to client %u\n", pAdapter->PortName, pIocDev->ClientId)
                        );
                    }
                    else
                    {
                        QCNET_DbgPrint
                        (
                            MP_DBG_MASK_CONTROL,
                            MP_DBG_LEVEL_ERROR,
                            ("<%s> QCTL: RD IRP buf too small for external client %d type %d, data queued.\n",
                            pAdapter->PortName, qmi->ClientId, qmi->QMIType)
                        );
                        pIrp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
                    }

                    InsertTailList(&pIocDev->IrpCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
                    KeSetEvent(&pIocDev->EmptyCompletionQueueEvent, IO_NO_INCREMENT, FALSE);

                    if (bDataFilled == TRUE)
                    {
                        break; // out of the loop
                    }
                }
                else
                {
                    // ignore the IRP which is in cancellation
                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_ERROR,
                        ("<%s> QCTL: read IRP in cancallation 0x%p\n", pAdapter->PortName, pIrp)
                    );

                    // we should continue IRP completion as the cancelRoutine does not proceed when IRP is not in queue
                    pIrp->IoStatus.Status = STATUS_CANCELLED;
                    pIrp->IoStatus.Information = 0;
                    InsertTailList(&pIocDev->IrpCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
                    KeSetEvent(&pIocDev->EmptyCompletionQueueEvent, IO_NO_INCREMENT, FALSE);

                    // Go around the loop to get the next read IRP
                }
            }  // while (!IsListEmpty(&pIocDev->ReadIrpQueue))

            DbgPrint("<%s> QCTLtoExternalClient: bDataFilled = %d\n", pAdapter->PortName, bDataFilled);

            if (bDataFilled == FALSE)
            {
                // put QMI Data to external client read queue
                // Create MPIOC_READ_ITEM, and its read buffer
                rdItem = ExAllocatePool(NonPagedPool, sizeof(MPIOC_READ_ITEM));
                if (rdItem != NULL)
                {
                    rdItem->Buffer = ExAllocatePool(NonPagedPool, len);
                    if (rdItem->Buffer != NULL)
                    {
                        rdItem->Length = len;

                        // Copy QMI data into the item
                        NdisMoveMemory((PVOID)rdItem->Buffer, (PVOID)qmux, len);
                        InsertTailList(&pIocDev->ReadDataQueue, &rdItem->List);

                        if (pIocDev->Type != MP_DEV_TYPE_INTERNAL_QOS)
                        {
                            MPMAIN_PrintBytes
                            (
                                rdItem->Buffer, len, len, "QmiDataRx", pAdapter,
                                MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_VERBOSE
                            );
                        }
                        else
                        {
                            MPMAIN_PrintBytes
                            (
                                rdItem->Buffer, len, len, "QmiDataRx", pAdapter,
                                MP_DBG_MASK_DATA_QOS, MP_DBG_LEVEL_VERBOSE
                            );
                        }

                        DbgPrint("<%s> QCTLtoExternalClient: en-queue the response for client to fetch: IO 0x%p (QMIType %d)\n",
                                 pAdapter->PortName, pIocDev, pIocDev->QMIType);

                        // notify internal client's read thread
                        KeSetEvent(&pIocDev->ReadDataAvailableEvent, IO_NO_INCREMENT, FALSE);
                    }
                    else
                    {
                        // data discarded
                        QCNET_DbgPrint
                        (
                            MP_DBG_MASK_CONTROL,
                            MP_DBG_LEVEL_ERROR,
                            ("<%s> QCTL: 1-out of memory for queuing data for external client %d type %d, discarded\n",
                            pAdapter->PortName, qmi->ClientId, qmi->QMIType)
                        );
                        ExFreePool(rdItem);
                    }
                }
                else
                {
                    // data discarded
                    QCNET_DbgPrint
                    (
                        MP_DBG_MASK_CONTROL,
                        MP_DBG_LEVEL_ERROR,
                        ("<%s> QCTL: 2-out of memory for queuing data for external client %d type %d, discarded\n",
                        pAdapter->PortName, qmi->ClientId, qmi->QMIType)
                    );
                }
            }

            NdisReleaseSpinLock(&pIocDev->IoLock);
        }  // if we got IOCDEV

    }  // while (bDone == FALSE)

}  // MPQMI_SendQCTLToExternalClient

#endif // QCMP_SUPPORT_CTRL_QMIC

NDIS_STATUS MPQCTL_SendQMICTLSync
(
    PMP_ADAPTER pAdapter
)
{
    NDIS_STATUS   ndisStatus = NDIS_STATUS_SUCCESS;
    UCHAR         tid = 0;
    LARGE_INTEGER timeoutValue;
    int           i;

    if ((pAdapter->QMUXVersion[QMUX_TYPE_CTL].Major < 1) ||
        (pAdapter->QMUXVersion[QMUX_TYPE_CTL].Minor < 4))
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> SyncReq: not supported\n", pAdapter->PortName)
        );
        return NDIS_STATUS_SUCCESS;
    }

    // clear signal
    KeClearEvent(&pAdapter->QMICTLSyncReceivedEvent);

    for (i = 0; i < pAdapter->QmiInitRetries; i++)
    {
        if (pAdapter->Flags & fMP_ANY_FLAGS)
        {
            ndisStatus = NDIS_STATUS_FAILURE;
            break;
        }

        if (USBIF_IsUsbBroken(pAdapter->USBDo) == TRUE)
        {
            ndisStatus = NDIS_STATUS_ADAPTER_REMOVED;
            break;
        }

        ndisStatus = MPQCTL_Request
        (
            pAdapter,
            QMUX_TYPE_CTL,
            0,
            QMICTL_SYNC_REQ,
            &tid,
            (PVOID)pAdapter
        );

        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> SyncReq: req sent 0x%x\n", pAdapter->PortName, ndisStatus)
        );

        if (ndisStatus == NDIS_STATUS_SUCCESS)
        {
            NTSTATUS nts;

            timeoutValue.QuadPart = QMICTL_TIMEOUT_RX; // -(10 * 1000 * 1000);   // 1.0 sec

            // wait for signal
            nts = KeWaitForSingleObject
            (
                &pAdapter->QMICTLSyncReceivedEvent,
                Executive,
                KernelMode,
                FALSE,
                &timeoutValue
            );
            KeClearEvent(&pAdapter->QMICTLSyncReceivedEvent);

            if (nts == STATUS_TIMEOUT)
            {
                ndisStatus = NDIS_STATUS_FAILURE;

                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_ERROR,
                    ("<%s> SyncReq: timeout, rm TR %u (%u)\n", pAdapter->PortName, tid, i)
                );
                MPQCTL_RemoveOldTransactionItem(pAdapter, tid);
            }
            else
            {
                QCNET_DbgPrint
                (
                    MP_DBG_MASK_CONTROL,
                    MP_DBG_LEVEL_DETAIL,
                    ("<%s> SyncReq: rsp received 0x%x\n", pAdapter->PortName, nts)
                );
                break;
            }
        }
        else
        {
            QCNET_DbgPrint
            (
                MP_DBG_MASK_CONTROL,
                MP_DBG_LEVEL_ERROR,
                ("<%s> SyncReq: req failure 0x%x\n", pAdapter->PortName, ndisStatus)
            );
            break;
        }
    }  // for

    return ndisStatus;

}  // MPQCTL_SendQMICTLSync

PVOID MPQCTL_HandleSyncReq
(
    PMP_ADAPTER pAdapter,
    PUCHAR      TransactionId,
    PULONG      MsgLen
)
{
    PQCQMI      qmi;
    PQMICTL_MSG qmictl;
    PVOID       msgBuf = NULL;

    *MsgLen += sizeof(QMICTL_SYNC_REQ_MSG);
    msgBuf = ExAllocatePool(NonPagedPool, *MsgLen);
    if (msgBuf == NULL)
    {
        return msgBuf;
    }

    *TransactionId = GetPhysicalAdapterQCTLTransactionId(pAdapter);

    qmi = (PQCQMI)msgBuf;
    qmictl = (PQMICTL_MSG)&qmi->SDU;

    qmi->IFType = USB_CTL_MSG_TYPE_QMI;
    qmi->Length = *MsgLen - QCUSB_CTL_MSG_HDR_SIZE;
    qmi->CtlFlags = QMICTL_CTL_FLAG_CMD;
    qmi->QMIType = QMUX_TYPE_CTL;
    qmi->ClientId = 0x00;  // no client id for qmictl

    qmictl->ReleaseClientIdReq.CtlFlags = QMICTL_FLAG_REQUEST;
    qmictl->ReleaseClientIdReq.TransactionId = *TransactionId;
    qmictl->ReleaseClientIdReq.QMICTLType = QMICTL_SYNC_REQ;
    qmictl->ReleaseClientIdReq.Length = sizeof(QMICTL_SYNC_REQ_MSG) -
        sizeof(QCQMICTL_MSG_HDR);
    return msgBuf;
}  // MPQCTL_HandleSyncReq

VOID MPQCTL_HandleSyncRsp
(
    PMP_ADAPTER              pAdapter,
    PQCQMI                   qmi,
    PQMICTL_TRANSACTION_ITEM item
)
{
    PQCQMICTL_MSG          qmictl = (PQCQMICTL_MSG)&qmi->SDU;
    PQMICTL_MSG            qmictl_msg;

    qmictl_msg = (PQMICTL_MSG)&qmi->SDU; // qmictl->Payload;

    if (item == NULL)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_SYNC_RESP (TID %d): no req found.\n",
            pAdapter->PortName, qmictl->TransactionId)
        );
        return;
    }

    if (qmictl_msg->SyncRsp.QMIResult != QMI_RESULT_SUCCESS)
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> QMICTL_SYNC_RESP (TID %d): result 0x%x error 0x%x\n",
            pAdapter->PortName,
            qmictl->TransactionId,
            qmictl_msg->SyncRsp.QMIResult,
            qmictl_msg->SyncRsp.QMIError)
        );
    }
    else
    {
        QCNET_DbgPrint
        (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_DETAIL,
            ("<%s> SyncRsp: OK\n", pAdapter->PortName)
        );

        pAdapter->IsQMIOutOfService = FALSE;
        InterlockedExchange(&pAdapter->QmiSyncNeeded, 0);
        KeSetEvent(&pAdapter->QMICTLSyncReceivedEvent, IO_NO_INCREMENT, FALSE);
    }
}  // MPQCTL_HandleSyncReq

VOID MPQCTL_HandleSyncInd
(
    PMP_ADAPTER              pAdapter,
    PQCQMI                   qmi
)
{
    PQCQMICTL_MSG          qmictl = (PQCQMICTL_MSG)&qmi->SDU;
    PQMICTL_MSG            qmictl_msg;
    PDEVICE_EXTENSION      pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;

    QCNET_DbgPrint
    (
        MP_DBG_MASK_CONTROL,
        MP_DBG_LEVEL_DETAIL,
        ("<%s> SyncInd: restarting with SYNC_REQ Mux %d\n", pAdapter->PortName, pDevExt->MuxInterface.MuxEnabled)
    );

    pAdapter->IsQMIOutOfService = FALSE;

    if ((pDevExt->MuxInterface.MuxEnabled == 0x00) ||
        (pDevExt->MuxInterface.InterfaceNumber == pDevExt->MuxInterface.PhysicalInterfaceNumber))
    {
        IncrementAllQmiSync(pAdapter);
    }
}  // MPQCTL_HandleSyncInd

