/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once

// QMI Service Types
#define QMICTL 0
#define QMIWDS 1
#define QMIDMS 2
#define QMIWDA 0x1A

// QMI Message Type
#define QMI_REQUEST_CONTROL_FLAG 0x00
#define QMI_RESPONSE_CONTROL_FLAG 0x02
#define QMI_INDICATION_CONTROL_FLAG 0x04

#define u8        unsigned char
#define u16       unsigned short
#define u32       unsigned int
#define u64       unsigned long long

#define bool      u8
#define true      1
#define false     0

#define ENOMEM    12
#define EFAULT    14
#define EINVAL    22
#define ENOMSG    42
#define ENODATA   61

/*=========================================================================*/
// Struct sQMUX
//
//    Structure that defines a QMUX header
/*=========================================================================*/
typedef struct sQMUX
{
   /* T\F, always 1 */
   u8         mTF;

   /* Size of message */
   u16        mLength;

   /* Control flag */
   u8         mCtrlFlag;
   
   /* Service Type */
   u8         mQMIService;
   
   /* Client ID */
   u8         mQMIClientID;

}__attribute__((__packed__)) sQMUX;

/*=========================================================================*/
// Generic QMUX functions
/*=========================================================================*/

// Remove QMUX headers from a buffer
int ParseQMUX(
   u16 *    pClientID,
   void *   pBuffer,
   u16      buffSize );

// Fill buffer with QMUX headers
int FillQMUX(
   u16      clientID,
   void *   pBuffer,
   u16      buffSize );

/*=========================================================================*/
// Generic QMI functions
/*=========================================================================*/

// Get data bufffer of a specified TLV from a QMI message
int GetTLV(
   void *   pQMIMessage,
   u16      messageLen,
   u8       type,
   void *   pOutDataBuf,
   u16      bufferLen );

// Check mandatory TLV in a QMI message
int ValidQMIMessage(
   void *   pQMIMessage,
   u16      messageLen );

// Get the message ID of a QMI message
int GetQMIMessageID(
   void *   pQMIMessage,
   u16      messageLen );

/*=========================================================================*/
// Get sizes of buffers needed by QMI requests
/*=========================================================================*/

// Get size of buffer needed for QMUX
u16 QMUXHeaderSize( void );

// Get size of buffer needed for QMUX + QMICTLGetClientIDReq
u16 QMICTLGetClientIDReqSize( void );

// Get size of buffer needed for QMUX + QMICTLReleaseClientIDReq
u16 QMICTLReleaseClientIDReqSize( void );

// Get size of buffer needed for QMUX + QMICTLReadyReq
u16 QMICTLReadyReqSize( void );

// Get size of buffer needed for QMUX + QMIWDSSetEventReportReq
u16 QMIWDSSetEventReportReqSize( void );

// Get size of buffer needed for QMUX + QMIWDSGetPKGSRVCStatusReq
u16 QMIWDSGetPKGSRVCStatusReqSize( void );

// Get size of buffer needed for QMUX + QMIDMSGetMEIDReq
u16 QMIDMSGetMEIDReqSize( void );

// Get size of buffer needed for QMUX + QMIWDASetDataFormatReq
u16 QMIWDASetDataFormatReqSize( void );
u16 QMIWDASetDataFormatReqSettingsSize( void );

u16 QMIWDSStartNetworkReqSize(void);
u16 QMIWDSGetRuntimeSettingsReqSize(void);

/*=========================================================================*/
// Fill Buffers with QMI requests
/*=========================================================================*/
int IPRoute(char *netAddr,
            unsigned int addr,
            char *subnetMask);

//Function for assigning IP address from kernel space
int QMIIPAssignment(
   unsigned int addr,
   sGobiUSBNet *pDev,
   char *subnetMask,
   sQMIDev *QMIDev
);

// Fill buffer with QMI CTL Get Client ID Request
int QMICTLGetClientIDReq(
   void *   pBuffer,
   u16      buffSize,
   u8       transactionID,
   u8       serviceType );

// Fill buffer with QMI CTL Release Client ID Request
int QMICTLReleaseClientIDReq(
   void *   pBuffer,
   u16      buffSize,
   u8       transactionID,
   u16      clientID );

// Fill buffer with QMI CTL Get Version Info Request
int QMICTLReadyReq(
   void *   pBuffer,
   u16      buffSize,
   u8       transactionID );

// Fill buffer with QMI WDS Set Event Report Request
int QMIWDSSetEventReportReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID );

// Fill buffer with QMI WDS Get PKG SRVC Status Request
int QMIWDSGetPKGSRVCStatusReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID );

// Fill buffer with QMI DMS Get Serial Numbers Request
int QMIDMSGetMEIDReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID );

// Fill buffer with QMI WDA QMAP Request
int QMIWDASetDataFormatReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID,
   sGobiUSBNet * pDev);

int QMIWDASetDataFormatReqSettings(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID );



int QMIWDSStartNetworkReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID,
   u8       profileid);

int QMIWDSGetRuntimeSettingsReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID );

int QMIWDSSetIPFamilyPrefReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID,
   u8       ipType);

int QMIWDSBindMuxPortReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID,
   sGobiUSBNet *pDev,
   sQMIDev *QMIDev);

int QMIWDSStopNetworkReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID,
   int      connectid);


/*=========================================================================*/
// Parse data from QMI responses
/*=========================================================================*/

// Parse the QMI CTL Get Client ID Resp
int QMICTLGetClientIDResp(
   void * pBuffer,
   u16    buffSize,
   u16 *  pClientID );

// Verify the QMI CTL Release Client ID Resp is valid
int QMICTLReleaseClientIDResp(
   void *   pBuffer,
   u16      buffSize );

// Parse the QMI WDS Set Event Report Resp/Indication or
//    QMI WDS Get PKG SRVC Status Resp/Indication
int QMIWDSEventResp(
   void *   pBuffer,
   u16      buffSize,
   u32 *    pTXOk,
   u32 *    pRXOk,
   u32 *    pTXErr,
   u32 *    pRXErr,
   u32 *    pTXOfl,
   u32 *    pRXOfl,
   u64 *    pTXBytesOk,
   u64 *    pRXBytesOk,
   bool *   pbLinkState,
   bool *   pbReconfigure );

int QMIWDASetDataFormatResp(
   void *   pBuffer,
   u16      buffSize,
   u32 *    DLDatagram,
   u32 *    DLDatagramSize,
   u32 *    ULDatagram,
   u32 *    ULDatagramSize );

// Parse the QMI DMS Get Serial Numbers Resp
int QMIDMSGetMEIDResp(
   void *   pBuffer,
   u16      buffSize,
   char *   pMEID,
   int      meidSize );

int QMIWDSStartNetworkResp(
   void *   pBuffer,
   u16      buffSize,
   char *   pMEID,
   int      meidSize );

int QMIWDSGetRuntimeSettingsResp(
   sGobiUSBNet *pDev,
   void *   pBuffer,
   u16      buffSize,
   sQMIDev *QMIDev);

int QMIWDSGetPktSrvcStatusInd (
   sGobiUSBNet *pDev,
   void *   pBuffer,
   u16      buffSize,
   sQMIDev *QMIDev,
   u16      clientID);

int QMIWDSExtendedIPConfigInd (
   sGobiUSBNet *pDev,
   void *   pBuffer,
   u16      buffSize,
   sQMIDev *QMIDev,
   u16      clientID);

int QMIWDSReverseIPTransportConnInd (
   sGobiUSBNet *pDev,
   void *   pBuffer,
   u16      buffSize,
   sQMIDev *QMIDev);

int QMIWDSStopNetworkResp(
   void *   pBuffer,
   u16      buffSize,
   char *   pMEID,
   int      meidSize );

int ConfigureQMAP(sGobiUSBNet *pDev);

int PreparePacket(struct usbnet *dev, struct sk_buff *skb, gfp_t flags);

void ProcessARP(struct usbnet *dev, struct sk_buff *skb);

void ArpResponse(struct usbnet *dev, struct sk_buff *skb);

unsigned short GetTransactionID(sQMIDev *QMIDev);

void PrintIPV6Addr(char *msg, ipv6_addr * addr);

