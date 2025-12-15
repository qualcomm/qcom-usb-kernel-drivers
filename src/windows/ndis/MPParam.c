/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P P A R A M . C

GENERAL DESCRIPTION
    This module contains functions for registry configuration.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <ndis.h>
#include "MPMain.h"
#include "MPParam.h"
#include "USBUTL.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPParam.tmh"

#endif   // EVENT_TRACING

#define QCMP_ERR_ID (ULONG)'RECQ'  // QCER
#define QCMP_DNS_REG_PV4 L"Tcpip\\Parameters\\Interfaces\\"
#define QCMP_DNS_REG_PV6 L"Tcpip6\\Parameters\\Interfaces\\"

typedef enum _MP_REG_INDEX
{
   MP_REG_MAX_DATA_SNDS =   0,
   MP_REG_MAX_DATA_RCVS, // 1
   MP_REG_MAX_CTRL_SNDS, // 2
   MP_REG_CTRL_SND_SIZE, // 3
   MP_REG_MAX_CTRL_RCVS, // 4
   MP_REG_CTRL_RCV_SIZE, // 5
   MP_REG_LLH_HDR_LEN,   // 6
   MP_REG_DBG_MSK,       // 7
   MP_REG_NUM_CLIENT,    // 8
   MP_REG_RECONF_DELAY,  // 9
   MP_REG_NET_CFG_ID,    // 10
   MP_REG_VLAN_ID,       // 11
   MP_PENDING_BYTES,     // 12

   #ifdef NDIS620_MINIPORT
   MP_REG_NET_LUID_IDX,
   #endif // NDIS620_MINIPORT

   #ifdef QCMP_TEST_MODE
   MP_REG_TEST_CONN_DELAY,
   #endif // QCMP_TEST_MODE

#ifdef NDIS620_MINIPORT
   MP_REG_MODEL_ID,
#endif // NDIS620_MINIPORT
   #ifdef QCUSB_MUX_PROTOCOL
   MP_REG_MUX_CTRL_DLCI,
   MP_REG_MUX_DATA_DLCI,
   MP_REG_DL_PAUSE_PKT_CNT,   
   #endif // QCUSB_MUX_PROTOCOL

   #ifdef QCMP_SUPPORT_CTRL_QMIC
   MP_REG_EXT_QCTL_CID,
   MP_REG_EXT_QCTL_FUZZING,
   #endif // QCMP_SUPPORT_CTRL_QMIC

   MP_DISABLE_QOS,
   MP_DISABLE_IP_MODE,

#ifdef QCMP_UL_TLP
   MP_UL_NUM_BUF,
   MP_UL_TLP_SIZE,
   MP_ENABLE_TLP,
#endif // QCMP_UL_TLP

   MP_QUICK_TX,
#if defined(QCMP_MBIM_UL_SUPPORT)
   MP_ENABLE_MBIM_UL,
#endif // 
#if defined(QCMP_MBIM_DL_SUPPORT)
   MP_ENABLE_MBIM_DL,
#endif // 

#ifdef QCMP_DL_TLP
   MP_ENABLE_DL_TLP,
#endif // QCMP_DL_TLP

#ifdef NDIS620_MINIPORT
   MP_REG_DEVICE_CLASS,
#endif // NDIS620_MINIPORT

   MP_IGNORE_ERRORS,
   
   MP_MTU_SIZE,

   MP_TRANSMIT_TIMER,

   MP_QC_DUAL_IP_FC,

   MP_UL_TLP_MAX_PACKETS,

   MP_DEREGISTER,

   MP_DISABLE_TIMER_RESOLUTION,

#if defined(QCMP_QMAP_V1_SUPPORT)
      MP_ENABLE_QMAP_V4,
#endif // 

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
      MP_ENABLE_QMAP_V3,
#endif // 
#endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
      MP_ENABLE_QMAP_V2,
#endif // 
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
         MP_ENABLE_QMAP_V1,
#endif // 

#ifdef QCMP_DISABLE_QMI
            MP_DISABLE_QMI,
#endif // 

   MP_NDP_SIGNATURE,
   
   MP_MUX_ID,
   
   MP_DL_MAX_PACKETS,

   MP_DL_AGGREGATION_SIZE,

   MP_ENABLE_MBIM,

#if defined(QCMP_QMAP_V1_SUPPORT)
   MP_DL_QMAP_MIN_PADDING,
#endif

   MP_BIND_IF_ID,

   MP_BIND_EP_TYPE,

   MP_DISABLE_QMAP_FC,

   MP_MAX_PENDING_QMI_REQ,
   
   MP_ENABLE_SS_DISCONNECT_TIMER,

#ifdef QCUSB_MUX_PROTOCOL
   MP_FAKE_IMSI,

   MP_FAKE_ICCID,
#endif

   MP_ENABLE_DATA5G,

   MP_RX_IND_CLUSTER_SZ,

   MP_RX_STREAMS,

   MP_REG_INDEX_MAX
} MP_REG_INDEX;

NDIS_STRING MPRegString[] =
{
   NDIS_STRING_CONST("MPMaxDataSends"),
   NDIS_STRING_CONST("MPMaxDataReceives"),
   NDIS_STRING_CONST("MPMaxCtrlSends"),
   NDIS_STRING_CONST("MPCtrlSendSize"),
   NDIS_STRING_CONST("MPMaxCtrlReceives"),
   NDIS_STRING_CONST("MPCtrlReceiveSize"),
   NDIS_STRING_CONST("MPLLHeaderBytes"),
   NDIS_STRING_CONST("QcDriverDebugMask"),
   NDIS_STRING_CONST("MPNumClients"),
   NDIS_STRING_CONST("MPReconfigDelay"),
   NDIS_STRING_CONST("NetCfgInstanceId"),
   NDIS_STRING_CONST("VlanId"),
   NDIS_STRING_CONST("MPQosPendingPackets"),

   #ifdef NDIS620_MINIPORT
   NDIS_STRING_CONST("NetLuidIndex"),
   #endif // NDIS620_MINIPORT

   #ifdef QCMP_TEST_MODE
   NDIS_STRING_CONST("MPTestConnectDelay"),
   #endif // QCMP_TEST_MODE

#ifdef NDIS620_MINIPORT
   NDIS_STRING_CONST("MPModelId"),
#endif // NDIS620_MINIPORT
   #ifdef QCUSB_MUX_PROTOCOL
   NDIS_STRING_CONST("QCDevControlDLCI"),
   NDIS_STRING_CONST("QCDevDataDLCI"),
   NDIS_STRING_CONST("QCDevDLPausePktCount"),
   #endif // QCUSB_MUX_PROTOCOL

   #ifdef QCMP_SUPPORT_CTRL_QMIC
   NDIS_STRING_CONST("QMICtrlCid"),
   NDIS_STRING_CONST("QMIFuzzingOn"),
   #endif // QCMP_SUPPORT_CTRL_QMIC

   NDIS_STRING_CONST("QCDevDisableQoS"),
   NDIS_STRING_CONST("QCDevDisableIPMode"),

#ifdef QCMP_UL_TLP
   NDIS_STRING_CONST("QCDriverNumTLPBuffers"),
   NDIS_STRING_CONST("QCDriverTLPSize"),
   NDIS_STRING_CONST("QCMPEnableTLP"),
#endif // QCMP_UL_TLP

   NDIS_STRING_CONST("QCMPQuickTx"),

#if defined(QCMP_MBIM_UL_SUPPORT)
   NDIS_STRING_CONST("QCMPEnableMBIMUL"),
#endif // 
#if defined(QCMP_MBIM_DL_SUPPORT)
      NDIS_STRING_CONST("QCMPEnableMBIMDL"),
#endif // 

#ifdef QCMP_DL_TLP
   NDIS_STRING_CONST("QCMPEnableDLTLP"),
#endif // QCMP_DL_TLP

#ifdef NDIS620_MINIPORT
   NDIS_STRING_CONST("QCRuntimeDeviceClass"),
#endif // NDIS620_MINIPORT

   NDIS_STRING_CONST("QCIgnoreErrors"),

   NDIS_STRING_CONST("QCDriverMTUSize"),

   NDIS_STRING_CONST("QCDriverTransmitTimer"),

   NDIS_STRING_CONST("QCMPEnableQCDualIpFc"),

   NDIS_STRING_CONST("QCDriverTLPMaxPackets"),

   NDIS_STRING_CONST("QCDriverDeregister"),

   NDIS_STRING_CONST("QCDriverDisableTimerResolution"),

#if defined(QCMP_QMAP_V1_SUPPORT)
   NDIS_STRING_CONST("QCMPEnableQMAPV4"),
#endif // 

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   NDIS_STRING_CONST("QCMPEnableQMAPV3"),
#endif // 
#endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   NDIS_STRING_CONST("QCMPEnableQMAPV2"),
#endif // 
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
      NDIS_STRING_CONST("QCMPEnableQMAPV1"),
#endif // 
#ifdef QCMP_DISABLE_QMI
   NDIS_STRING_CONST("QCMPDisableQMI"),
#endif

   NDIS_STRING_CONST("QCMPNDPSignature"),

   NDIS_STRING_CONST("QCMPMuxId"),

   NDIS_STRING_CONST("QCDriverDLMaxPackets"),
   
   NDIS_STRING_CONST("QCDriverDLAggregationSize"),

   NDIS_STRING_CONST("QCDriverEnableMBIM"),

#if defined(QCMP_QMAP_V1_SUPPORT)
   NDIS_STRING_CONST("QCMPQMAPDLMinPadding"),
#endif

   NDIS_STRING_CONST("QCMPBindIFId"),
   NDIS_STRING_CONST("QCMPBindEPType"),

   NDIS_STRING_CONST("QCMPDisableQMAPFC"),

   NDIS_STRING_CONST("QCMPMaxPendingQMIReqs"),

   NDIS_STRING_CONST("QCMPEnableSigStrDisconnectTimer"),

#ifdef QCUSB_MUX_PROTOCOL
   NDIS_STRING_CONST("QCMPFakeDeviceIMSI"),
   NDIS_STRING_CONST("QCMPFakeDeviceICCID"),
#endif

   NDIS_STRING_CONST("QCMPEnableData5G"),
   NDIS_STRING_CONST("QCMPRxIndClusterSize"),
   NDIS_STRING_CONST("QCMPRxStreams")
};


NDIS_STATUS MPParam_GetConfigValues
(
   IN NDIS_HANDLE AdapterHandle,
   IN NDIS_HANDLE WrapperConfigurationContext,
   PMP_ADAPTER    AdapterContext
)
{
   NDIS_HANDLE configurationHandle = NULL;
   NDIS_STATUS status = NDIS_STATUS_FAILURE;
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)AdapterContext;
   UNICODE_STRING cfgId;
   ULONG IgnoreErrors = 0;

   #ifdef NDIS60_MINIPORT

   NDIS_CONFIGURATION_OBJECT ConfigObject;

   ConfigObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
   ConfigObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
   ConfigObject.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
   ConfigObject.NdisHandle = pAdapter->AdapterHandle;
   ConfigObject.Flags = 0;

   #endif // NDIS60_MINIPORT

   // Assign default values first in case of failure access
   // to the registry
   pAdapter->MaxDataSends    = PARAM_MaxDataSends_DEFAULT;
   pAdapter->MaxDataReceives = PARAM_MaxDataReceives_DEFAULT;
   pAdapter->MaxCtrlSends    = PARAM_MaxCtrlSends_DEFAULT;
   pAdapter->CtrlSendSize    = PARAM_CtrlSendSize_DEFAULT;
   pAdapter->MaxCtrlReceives = PARAM_MaxCtrlReceives_DEFAULT;
   pAdapter->CtrlReceiveSize = PARAM_CtrlReceiveSize_DEFAULT;
   pAdapter->LLHeaderBytes   = PARAM_LLHeaderBytes_DEFAULT;
   pAdapter->DebugMask       = PARAM_DebugMask_DEFAULT;
   pAdapter->NumClients      = PARAM_NumClients_DEFAULT;
   pAdapter->ReconfigDelay   = PARAM_ReconfigDelay_DEFAULT;
   pAdapter->VlanId          = PARAM_VlanId_DEFAULT;
   #ifdef QCMP_TEST_MODE
   pAdapter->TestConnectDelay= PARAM_TestConnectDelay_DEFAULT;
   #endif // QCMP_TEST_MODE
   pAdapter->IgnoreQMICTLErrors = FALSE;

   #ifdef NDIS60_MINIPORT
   if (QCMP_NDIS6_Ok == TRUE)
   {
      status = NdisOpenConfigurationEx
               (
                  &ConfigObject,
                  &configurationHandle
               );
   }
   #else
   NdisOpenConfiguration
   (
      &status,
      &configurationHandle,
      WrapperConfigurationContext
   );
   #endif // NDIS60_MINIPORT

   if (status != NDIS_STATUS_SUCCESS)
   {
      QCNET_DbgPrintG(("<%s> ERROR: NdisOpenConfiguration failed (Status=0x%x)\n", gDeviceName, status));

      NdisWriteErrorLogEntry
      (
         AdapterHandle,
         NDIS_ERROR_CODE_UNSUPPORTED_CONFIGURATION,
         3,
         status,
         __LINE__,
         QCMP_ERR_ID
      );
      return NDIS_STATUS_FAILURE;
   }

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_MAX_DATA_SNDS,
      &pAdapter->MaxDataSends,
      PARAM_MaxDataSends_DEFAULT, PARAM_MaxDataReceives_MIN, PARAM_MaxDataSends_MAX,
      MP_REG_MAX_DATA_SNDS
   );
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_MAX_DATA_RCVS,
      &pAdapter->MaxDataReceives,
      PARAM_MaxDataReceives_DEFAULT, PARAM_MaxDataReceives_MIN, PARAM_MaxDataReceives_MAX,
      MP_REG_MAX_DATA_RCVS
   );
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_MAX_CTRL_SNDS,
      &pAdapter->MaxCtrlSends,
      PARAM_MaxCtrlSends_DEFAULT, PARAM_MaxCtrlSends_MIN, PARAM_MaxCtrlSends_MAX,
      MP_REG_MAX_CTRL_SNDS
   );
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_CTRL_SND_SIZE,
      &pAdapter->CtrlSendSize,
      PARAM_CtrlSendSize_DEFAULT, PARAM_CtrlSendSize_MIN, PARAM_CtrlSendSize_MAX,
      MP_REG_CTRL_SND_SIZE
   );
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_MAX_CTRL_RCVS,
      &pAdapter->MaxCtrlReceives,
      PARAM_MaxCtrlReceives_DEFAULT, PARAM_MaxCtrlReceives_MIN, PARAM_MaxCtrlReceives_MAX,
      MP_REG_MAX_CTRL_RCVS
   );
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_CTRL_RCV_SIZE,
      &pAdapter->CtrlReceiveSize,
      PARAM_CtrlReceiveSize_DEFAULT, PARAM_CtrlReceiveSize_MIN, PARAM_CtrlReceiveSize_MAX,
      MP_REG_CTRL_RCV_SIZE
   );
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_LLH_HDR_LEN,
      &pAdapter->LLHeaderBytes,
      PARAM_LLHeaderBytes_DEFAULT, PARAM_LLHeaderBytes_MIN, PARAM_LLHeaderBytes_MAX,
      MP_REG_LLH_HDR_LEN
   );
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_DBG_MSK,
      &pAdapter->DebugMask,
      PARAM_DebugMask_DEFAULT, PARAM_DebugMask_MIN, PARAM_DebugMask_MAX,
      MP_REG_DBG_MSK
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_NUM_CLIENT,
      &pAdapter->NumClients,
      PARAM_NumClients_DEFAULT, PARAM_NumClients_MIN, PARAM_NumClients_MAX,
      MP_REG_NUM_CLIENT
   );
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_RECONF_DELAY,
      &pAdapter->ReconfigDelay,
      PARAM_ReconfigDelay_DEFAULT, PARAM_ReconfigDelay_MIN, PARAM_ReconfigDelay_MAX,
      MP_REG_RECONF_DELAY
   );

   pAdapter->ReconfigDelayIPv6 = pAdapter->ReconfigDelay;
   
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_VLAN_ID,
      &pAdapter->VlanId,
      PARAM_VlanId_DEFAULT, PARAM_VlanId_MIN, PARAM_VlanId_MAX,
      MP_REG_VLAN_ID
   );
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_PENDING_BYTES,
      &pAdapter->MPPendingPackets,
      PARAM_MPPendingPackets_DEFAULT, PARAM_MPPendingPackets_MIN, PARAM_MPPendingPackets_MAX,
      MP_PENDING_BYTES
   );

   #ifdef QCMP_TEST_MODE
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_TEST_CONN_DELAY,
      &pAdapter->TestConnectDelay,
      PARAM_TestConnectDelay_DEFAULT, PARAM_TestConnectDelay_MIN, PARAM_TestConnectDelay_MAX,
      MP_REG_TEST_CONN_DELAY
   );
   #endif // QCMP_TEST_MODE

   #ifdef QCUSB_MUX_PROTOCOL
   pAdapter->MuxEnabled = FALSE;
   status = MPPARAM_ConfigurationGetValue
            (
               configurationHandle,
               (UCHAR)MP_REG_MUX_CTRL_DLCI,
               &pAdapter->MuxCtrlDLCI,
               PARAM_MuxCtrl_DEFAULT, PARAM_MuxCtrl_MIN, PARAM_MuxCtrl_MAX,
               MP_REG_MUX_CTRL_DLCI
            );
   if (status == NDIS_STATUS_SUCCESS)
   {
      status = MPPARAM_ConfigurationGetValue
               (
                  configurationHandle,
                  (UCHAR)MP_REG_MUX_DATA_DLCI,
                  &pAdapter->MuxDataDLCI,
                  PARAM_MuxData_DEFAULT, PARAM_MuxData_MIN, PARAM_MuxData_MAX,
                  MP_REG_MUX_DATA_DLCI
               );
      if (status == NDIS_STATUS_SUCCESS)
      {
         pAdapter->MuxEnabled = TRUE;
         DbgPrint("<%s> [MUX] CtrlDLCI=%d  DataDLCI=%d\n", pAdapter->PortName, pAdapter->MuxCtrlDLCI, pAdapter->MuxDataDLCI);
      }
   }

   pAdapter->DLPauseEnabled = FALSE;
   status = MPPARAM_ConfigurationGetValue
            (
               configurationHandle,
               (UCHAR)MP_REG_DL_PAUSE_PKT_CNT,
               &pAdapter->DLPausePktCount,
               PARAM_DLPausePktCount_DEFAULT, PARAM_DLPausePktCount_MIN, PARAM_DLPausePktCount_MAX,
               MP_REG_DL_PAUSE_PKT_CNT
            );
   if (status == NDIS_STATUS_SUCCESS)
   {
      if (pAdapter->DLPausePktCount > 0)
      {
         pAdapter->DLPauseEnabled = TRUE;
      }
   }
   
   #endif // QCUSB_MUX_PROTOCOL

   #ifdef QCMP_SUPPORT_CTRL_QMIC
   status = MPPARAM_ConfigurationGetValue
            (
               configurationHandle,
               (UCHAR)MP_REG_EXT_QCTL_CID,
               &pAdapter->ExternalQMICtrlCid,
               PARAM_QMICtrlCid_DEFAULT, PARAM_QMICtrlCid_MIN, PARAM_QMICtrlCid_MAX,
               MP_REG_EXT_QCTL_CID
            );
   if (status == NDIS_STATUS_SUCCESS)
   {
      pAdapter->ExternalQCTLEnabled = TRUE;

      status = MPPARAM_ConfigurationGetValue
               (
                  configurationHandle,
                  (UCHAR)MP_REG_EXT_QCTL_FUZZING,
                  &pAdapter->QMIFuzzing,
                  PARAM_QMIFuzzing_DEFAULT, PARAM_Fuzzing_MIN, PARAM_Fuzzing_MAX,
                  MP_REG_EXT_QCTL_FUZZING
               );
      if (status != NDIS_STATUS_SUCCESS)
      {
         pAdapter->QMIFuzzing = 0;
      }
   }
   else
   {
      pAdapter->ExternalQCTLEnabled = FALSE;
   }
   #endif // QCMP_SUPPORT_CTRL_QMIC

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_DISABLE_QOS,
      &pAdapter->MPDisableQoS,
      PARAM_DisableQoS_DEFAULT, PARAM_DisableQoS_MIN, PARAM_DisableQoS_MAX,
      MP_DISABLE_QOS
   );

#ifdef QC_IP_MODE
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_DISABLE_IP_MODE,
      &pAdapter->MPDisableIPMode,
      PARAM_DisableIPMode_DEFAULT, PARAM_DisableIPMode_MIN, PARAM_DisableIPMode_MAX,
      MP_DISABLE_IP_MODE
   );
#endif

#ifdef QCMP_UL_TLP

   status = MPPARAM_ConfigurationGetValue
            (
               configurationHandle,
               (UCHAR)MP_UL_NUM_BUF,
               &pAdapter->NumTLPBuffers,
               PARAM_NumTLPBuffers_DEFAULT, PARAM_NumTLPBuffers_MIN, PARAM_NumTLPBuffers_MAX,
               MP_UL_NUM_BUF
            );
   if (status == NDIS_STATUS_SUCCESS)
   {
      pAdapter->NumTLPBuffersConfigured = TRUE;
   }
   else
   {
      pAdapter->NumTLPBuffersConfigured = FALSE;
   }

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_UL_TLP_SIZE,
      &pAdapter->UplinkTLPSize,
      PARAM_TLPSize_DEFAULT, PARAM_TLPSize_MIN, PARAM_TLPSize_MAX,
      MP_UL_TLP_SIZE
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_ENABLE_TLP,
      &pAdapter->MPEnableTLP,
      PARAM_MPEnableTLP_DEFAULT, PARAM_MPEnableTLP_MIN, PARAM_MPEnableTLP_MAX,
      MP_ENABLE_TLP
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_UL_TLP_MAX_PACKETS,
      &pAdapter->MaxTLPPackets,
      PARAM_TLPMaxPackets_DEFAULT, PARAM_TLPMaxPackets_MIN, PARAM_TLPMaxPackets_MAX,
      MP_UL_TLP_MAX_PACKETS
   );

#endif // QCMP_UL_TLP

#ifdef QCMP_DL_TLP

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_ENABLE_DL_TLP,
      &pAdapter->MPEnableDLTLP,
      PARAM_MPEnableDLTLP_DEFAULT, PARAM_MPEnableDLTLP_MIN, PARAM_MPEnableDLTLP_MAX,
      MP_ENABLE_DL_TLP
   );

#endif // QCMP_DL_TLP

#if defined(QCMP_MBIM_UL_SUPPORT)

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_ENABLE_MBIM_UL,
      &pAdapter->MPEnableMBIMUL,
      PARAM_MPEnableMBIMUL_DEFAULT, PARAM_MPEnableMBIMUL_MIN, PARAM_MPEnableMBIMUL_MAX,
      MP_ENABLE_MBIM_UL
   );

#endif //

#if defined(QCMP_MBIM_DL_SUPPORT)
   
      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_ENABLE_MBIM_DL,
         &pAdapter->MPEnableMBIMDL,
         PARAM_MPEnableMBIMDL_DEFAULT, PARAM_MPEnableMBIMDL_MIN, PARAM_MPEnableMBIMDL_MAX,
         MP_ENABLE_MBIM_DL
      );
   
#endif //

#if defined(QCMP_QMAP_V1_SUPPORT)
   
      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_ENABLE_QMAP_V4,
         &pAdapter->MPEnableQMAPV4,
         PARAM_MPEnableQMAPV4_DEFAULT, PARAM_MPEnableQMAPV4_MIN, PARAM_MPEnableQMAPV4_MAX,
         MP_ENABLE_QMAP_V4
      );

#endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   
      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_ENABLE_QMAP_V3,
         &pAdapter->MPEnableQMAPV3,
         PARAM_MPEnableQMAPV3_DEFAULT, PARAM_MPEnableQMAPV3_MIN, PARAM_MPEnableQMAPV3_MAX,
         MP_ENABLE_QMAP_V3
      );

#endif //
#endif


#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
   
      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_ENABLE_QMAP_V2,
         &pAdapter->MPEnableQMAPV2,
         PARAM_MPEnableQMAPV2_DEFAULT, PARAM_MPEnableQMAPV2_MIN, PARAM_MPEnableQMAPV2_MAX,
         MP_ENABLE_QMAP_V2
      );

#endif //
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
   
      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_ENABLE_QMAP_V1,
         &pAdapter->MPEnableQMAPV1,
         PARAM_MPEnableQMAPV1_DEFAULT, PARAM_MPEnableQMAPV1_MIN, PARAM_MPEnableQMAPV1_MAX,
         MP_ENABLE_QMAP_V1
      );
   
      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_DL_QMAP_MIN_PADDING,
         &pAdapter->QMAPDLMinPadding,
         PARAM_QMAPDLMinPadding_DEFAULT, PARAM_QMAPDLMinPadding_MIN, PARAM_QMAPDLMinPadding_MAX,
         MP_DL_QMAP_MIN_PADDING
      );

      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_BIND_IF_ID,
         (PULONG)&pAdapter->BindIFId,
         PARAM_MPBindIFId_DEFAULT, PARAM_MPBindIFId_MIN, PARAM_MPBindIFId_MAX,
         MP_BIND_IF_ID
      );

      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_BIND_EP_TYPE,
         (PULONG)&pAdapter->BindEPType,
         PARAM_MPBindEPType_DEFAULT, PARAM_MPBindEPType_MIN, PARAM_MPBindEPType_MAX,
         MP_BIND_EP_TYPE
      );
      
#endif //

#ifdef QC_DUAL_IP_FC
      
         MPPARAM_ConfigurationGetValue
         (
            configurationHandle,
            (UCHAR)MP_QC_DUAL_IP_FC,
            &pAdapter->MPEnableQCDualIpFc,
            PARAM_MPEnableDUALIPFC_DEFAULT, PARAM_MPEnableDUALIPFC_MIN, PARAM_MPEnableDUALIPFC_MAX,
            MP_QC_DUAL_IP_FC
         );
      
#endif // QC_DUAL_IP_FC
   
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_DISABLE_QMAP_FC,
      &pAdapter->DisableQMAPFC,
      PARAM_DisableQMAPFC_DEFAULT, PARAM_DisableQMAPFC_MIN, PARAM_DisableQMAPFC_MAX,
      MP_DISABLE_QMAP_FC
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_MAX_PENDING_QMI_REQ,
      &pAdapter->MaxPendingQMIReqs,
      PARAM_MaxPendingQMIReqs_DEFAULT, PARAM_MaxPendingQMIReqs_MIN, PARAM_MaxPendingQMIReqs_MAX,
      MP_MAX_PENDING_QMI_REQ
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_ENABLE_SS_DISCONNECT_TIMER,
      &pAdapter->EnableSSDisconnectTimer,
      PARAM_EnableSigStrDisconTimer_DEFAULT, PARAM_EnableSigStrDisconTimer_MIN, PARAM_EnableSigStrDisconTimer_MAX,
      MP_ENABLE_SS_DISCONNECT_TIMER
   );

   /***
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_QUICK_TX,
      &pAdapter->MPQuickTx,
      PARAM_MPQuickTx_DEFAULT, PARAM_MPQuickTx_MIN, PARAM_MPQuickTx_MAX,
      MP_QUICK_TX
   );
   ***/
   if (pAdapter->MPDisableQoS >= 3)
   {
      pAdapter->MPQuickTx = 1;
   }
   else
   {
      pAdapter->MPQuickTx = 0;
   }

   // Just try an experiment  to see if we can enable this by default
   pAdapter->MPQuickTx = 1;
   
   #ifdef NDIS620_MINIPORT
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_REG_DEVICE_CLASS,
      &pAdapter->RuntimeDeviceClass,
      PARAM_DeviceClass_DEFAULT, PARAM_DeviceClass_MIN, PARAM_DeviceClass_MAX,
      MP_REG_DEVICE_CLASS
   );
   #endif // NDIS620_MINIPORT

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_IGNORE_ERRORS,
      &IgnoreErrors,
      PARAM_IgnoreErrors_DEFAULT, PARAM_IgnoreErrors_MIN, PARAM_IgnoreErrors_MAX,
      MP_IGNORE_ERRORS
   );

   if (IgnoreErrors & IGNORE_QMI_CONTROL_ERRORS)
   {
      pAdapter->IgnoreQMICTLErrors = TRUE;
   }

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_MTU_SIZE,
      &pAdapter->MTUSize,
      PARAM_MTUSize_DEFAULT, PARAM_MTUSize_MIN, PARAM_MTUSize_MAX,
      MP_MTU_SIZE
   );

   
#if defined(QCMP_UL_TLP) || defined(QCMP_MBIM_UL_SUPPORT)
   
      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_TRANSMIT_TIMER,
         &pAdapter->TransmitTimerValue,
         PARAM_TransmitTimer_DEFAULT, PARAM_TransmitTimer_MIN, PARAM_TransmitTimer_MAX,
         MP_TRANSMIT_TIMER
      );
   
#endif   

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_DEREGISTER,
      &pAdapter->Deregister,
      PARAM_Deregister_DEFAULT, PARAM_Deregister_MIN, PARAM_Deregister_MAX,
      MP_DEREGISTER
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_DISABLE_TIMER_RESOLUTION,
      &pAdapter->DisableTimerResolution,
      PARAM_DisableTimerResolution_DEFAULT, PARAM_DisableTimerResolution_MIN, PARAM_DisableTimerResolution_MAX,
      MP_DISABLE_TIMER_RESOLUTION
   );

#ifdef QCMP_DISABLE_QMI
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_DISABLE_QMI,
      &pAdapter->DisableQMI,
      PARAM_DisableQMI_DEFAULT, PARAM_DisableQMI_MIN, PARAM_DisableQMI_MAX,
      MP_DISABLE_QMI
   );
#endif

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_NDP_SIGNATURE,
      &pAdapter->ndpSignature,
      PARAM_MPNDPSignature_DEFAULT, PARAM_MPNDPSignature_MIN, PARAM_MPNDPSignature_MAX,
      MP_NDP_SIGNATURE
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_MUX_ID,
      (PULONG)&pAdapter->MuxId,
      PARAM_MPMuxId_DEFAULT, PARAM_MPMuxId_MIN, PARAM_MPMuxId_MAX,
      MP_MUX_ID
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_DL_AGGREGATION_SIZE,
      &pAdapter->DLAggSize,
      PARAM_DLAGGSize_DEFAULT, PARAM_DLAGGSize_MIN, PARAM_DLAGGSize_MAX,
      MP_DL_AGGREGATION_SIZE
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_DL_MAX_PACKETS,
      &pAdapter->DLAggMaxPackets,
      PARAM_DLAGGMaxPackets_DEFAULT, PARAM_DLAGGMaxPackets_MIN, PARAM_DLAGGMaxPackets_MAX,
      MP_DL_MAX_PACKETS
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_ENABLE_MBIM,
      &pAdapter->EnableMBIM,
      PARAM_EnableMBIM_DEFAULT, PARAM_EnableMBIM_MIN, PARAM_EnableMBIM_MAX,
      MP_ENABLE_MBIM
   );

   if (pAdapter->EnableMBIM == 1)
   {
      pAdapter->ndpSignature = 0x00535049; //IPS0
   }
   else
   {
      pAdapter->ndpSignature = 0x50444E51; //QNDP
   }

#ifdef QCUSB_MUX_PROTOCOL
   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_FAKE_IMSI,
      &pAdapter->FakeIMSI,
      PARAM_MPFakeIMSI_DEFAULT, PARAM_MPFakeIMSI_MIN, PARAM_MPFakeIMSI_MAX,
      MP_FAKE_IMSI
   );

   MPPARAM_ConfigurationGetValue
   (
      configurationHandle,
      (UCHAR)MP_FAKE_ICCID,
      &pAdapter->FakeICCID,
      PARAM_MPFakeICCID_DEFAULT, PARAM_MPFakeICCID_MIN, PARAM_MPFakeICCID_MAX,
      MP_FAKE_ICCID
   );
#endif
   status = MPPARAM_ConfigurationGetString
            (
               configurationHandle,
               (UCHAR)MP_REG_NET_CFG_ID,
               &(pAdapter->InterfaceGUID),
               MP_REG_NET_CFG_ID
            );
   if (status == NDIS_STATUS_SUCCESS)
   {
      UNICODE_STRING ucsPrefix;
      NTSTATUS ntStatus;

      RtlUnicodeStringToAnsiString
      (
         &pAdapter->NetCfgInstanceId,
         &(pAdapter->InterfaceGUID),
         TRUE
      );

      // construct IPV4 DNS registry path
      ntStatus = USBUTL_AllocateUnicodeString
                 (
                    &(pAdapter->DnsRegPathV4),
                    MAX_NAME_LEN,
                    "DnsRegP4"
                 );
      if (ntStatus != STATUS_SUCCESS)
      {
         pAdapter->DnsRegPathV4.Buffer        = NULL;
         pAdapter->DnsRegPathV4.MaximumLength = 0;
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<%s> ERROR: failed to alloc DnsRegPathV4 0x%x\n", gDeviceName, ntStatus)
         );
      }
      else
      {
         RtlInitUnicodeString(&ucsPrefix, QCMP_DNS_REG_PV4);
         RtlCopyUnicodeString(&(pAdapter->DnsRegPathV4), &ucsPrefix);
         ntStatus = RtlAppendUnicodeStringToString(&(pAdapter->DnsRegPathV4), &(pAdapter->InterfaceGUID));
         if (ntStatus != STATUS_SUCCESS)
         {
            RtlFreeUnicodeString(&(pAdapter->DnsRegPathV4));
            pAdapter->DnsRegPathV4.Buffer        = NULL;
            pAdapter->DnsRegPathV4.MaximumLength = 0;
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
               ("<%s> ERROR: failed to construct DnsRegPathV4 0x%x\n", gDeviceName, ntStatus)
            );
         }
      }

      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_ENABLE_DATA5G,
         &pAdapter->EnableData5G,
         PARAM_EnableData5G_DEFAULT, PARAM_EnableData5G_MIN, PARAM_EnableData5G_MAX,
         MP_ENABLE_DATA5G
      );

      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_RX_IND_CLUSTER_SZ,
         &pAdapter->RxIndClusterSize,
         PARAM_MPRxIndClusterSize_DEFAULT, PARAM_MPRxIndClusterSize_MIN, PARAM_MPRxIndClusterSize_MAX,
         MP_RX_IND_CLUSTER_SZ
      );

      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_RX_STREAMS,
         &pAdapter->RxStreams,
         PARAM_MPRxStreams_DEFAULT, PARAM_MPRxStreams_MIN, PARAM_MPRxStreams_MAX,
         MP_RX_STREAMS
      );

      if (pAdapter->EnableData5G != 0)
      {
         if (pAdapter->MaxDataReceives < 400) // min for 5G
         {
            // allow adjustment between 400 and PARAM_MaxDataReceives_MAX
            pAdapter->MaxDataReceives = PARAM_MaxDataReceives_5G_DEFAULT;
         }
         if (pAdapter->RxIndClusterSize < 128) // min for 5G
         {
            // allow adjustment between 128 and PARAM_MPRxIndClusterSize_MAX
            pAdapter->RxIndClusterSize = PARAM_MPRxIndClusterSize_5G_DEFAULT;
         }
      }

      // construct IPV6 DNS registry path
      ntStatus = USBUTL_AllocateUnicodeString
                 (
                    &(pAdapter->DnsRegPathV6),
                    MAX_NAME_LEN,
                    "DnsRegP6"
                 );
      if (ntStatus != STATUS_SUCCESS)
      {
         pAdapter->DnsRegPathV6.Buffer        = NULL;
         pAdapter->DnsRegPathV6.MaximumLength = 0;
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<%s> ERROR: failed to alloc DnsRegPathV6 0x%x\n", gDeviceName, ntStatus)
         );
      }
      else
      {
         RtlInitUnicodeString(&ucsPrefix, QCMP_DNS_REG_PV6);
         RtlCopyUnicodeString(&(pAdapter->DnsRegPathV6), &ucsPrefix);
         ntStatus = RtlAppendUnicodeStringToString(&(pAdapter->DnsRegPathV6), &(pAdapter->InterfaceGUID));
         if (ntStatus != STATUS_SUCCESS)
         {
            RtlFreeUnicodeString(&(pAdapter->DnsRegPathV6));
            pAdapter->DnsRegPathV6.Buffer        = NULL;
            pAdapter->DnsRegPathV6.MaximumLength = 0;
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
               ("<%s> ERROR: failed to construct DnsRegPathV6 0x%x\n", gDeviceName, ntStatus)
            );
         }
      }
   }

   #ifdef NDIS620_MINIPORT

   if (QCMP_NDIS620_Ok == TRUE)
   {
      MPPARAM_ConfigurationGetValue
      (
         configurationHandle,
         (UCHAR)MP_REG_NET_LUID_IDX,
         &pAdapter->NetLuidIndex,
         PARAM_MPNetLuidIndex_DEFAULT, PARAM_MPNetLuidIndex_MIN, PARAM_MPNetLuidIndex_MAX,
         MP_REG_NET_LUID_IDX
      );

      MPPARAM_ConfigurationGetString
      (
         configurationHandle,
         (UCHAR)MP_REG_MODEL_ID,
         &(pAdapter->RegModelId),
         MP_REG_MODEL_ID
      );
   }   
   
   #endif // NDIS620_MINIPORT

   NdisCloseConfiguration(configurationHandle);

   return NDIS_STATUS_SUCCESS;
}  // MPParam_GetConfigValues

NTSTATUS MPPARAM_ConfigurationGetValue
(
   NDIS_HANDLE       ConfigurationHandle,
   UCHAR             EntryIdx,
   PULONG            Value,
   ULONG             Default,
   ULONG             Min,
   ULONG             Max,
   UCHAR             Cookie
)
{
   NDIS_STATUS                     ndisStatus;
   NDIS_HANDLE                     configurationHandle;
   PNDIS_CONFIGURATION_PARAMETER   pReturnedValue = NULL;
   NDIS_CONFIGURATION_PARAMETER    ReturnedValue;

   // #define NDIS_STRING_CONST(x) {sizeof(L##x)-2, sizeof(L##x), L##x}

   NdisReadConfiguration
   (
      &ndisStatus,
      &pReturnedValue,
      ConfigurationHandle,
      &MPRegString[EntryIdx],
      NdisParameterInteger  // NdisParameterHexInteger
   );

   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      *Value = Default;
      return STATUS_UNSUCCESSFUL;
   }
   if (pReturnedValue->ParameterData.IntegerData < Min)
   {
      *Value = Min;
   }
   else if (pReturnedValue->ParameterData.IntegerData > Max)
   {
      *Value = Max;
   }
   else
   {
      *Value = pReturnedValue->ParameterData.IntegerData;
   }

   return STATUS_SUCCESS;
}  // MPPARAM_ConfigurationGetValue

NTSTATUS MPPARAM_ConfigurationGetString
(
   NDIS_HANDLE       ConfigurationHandle,  // registry handle
   UCHAR             EntryIdx,
   PUNICODE_STRING   StringText,
   UCHAR             Cookie
)
{
   NDIS_STATUS                     ndisStatus;
   PNDIS_CONFIGURATION_PARAMETER   pReturnedValue = NULL;
   NDIS_CONFIGURATION_PARAMETER    ReturnedValue;
   NTSTATUS                        ntStatus;

   // #define NDIS_STRING_CONST(x) {sizeof(L##x)-2, sizeof(L##x), L##x}

   NdisReadConfiguration
   (
      &ndisStatus,
      &pReturnedValue,
      ConfigurationHandle,
      &MPRegString[EntryIdx],
      NdisParameterString
   );

   if (ndisStatus != NDIS_STATUS_SUCCESS)
   {
      return STATUS_UNSUCCESSFUL;
   }

   ntStatus = USBUTL_AllocateUnicodeString
              (
                 StringText,
                 pReturnedValue->ParameterData.StringData.MaximumLength,
                 "RegString"
              );

   if (ntStatus != STATUS_SUCCESS)
   {
      return ntStatus;
   }

   RtlCopyUnicodeString(StringText, &(pReturnedValue->ParameterData.StringData));

   return STATUS_SUCCESS;
}  // MPPARAM_ConfigurationGetString
