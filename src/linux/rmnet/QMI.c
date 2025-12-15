/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "QMIDevice.h"
#include "QMI.h"
/*=========================================================================*/
// Get sizes of buffers needed by QMI requests
/*=========================================================================*/
extern int debug_g;
#if 0
extern int RmnetIF;
#endif

void PrintIPAddr(char *, unsigned int);
int QMI_IP_Down(char *);

/*===========================================================================
METHOD:
   QMUXHeaderSize (Public Method)

DESCRIPTION:
   Get size of buffer needed for QMUX
 
RETURN VALUE:
   u16 - size of buffer
===========================================================================*/
u16 QMUXHeaderSize( void )
{
   return sizeof( sQMUX );
}

/*===========================================================================
METHOD:
   QMICTLGetClientIDReqSize (Public Method)

DESCRIPTION:
   Get size of buffer needed for QMUX + QMICTLGetClientIDReq
 
RETURN VALUE:
   u16 - size of buffer
===========================================================================*/
u16 QMICTLGetClientIDReqSize( void )
{
   return sizeof( sQMUX ) + 10;
}

/*===========================================================================
METHOD:
   QMICTLReleaseClientIDReqSize (Public Method)

DESCRIPTION:
   Get size of buffer needed for QMUX + QMICTLReleaseClientIDReq
 
RETURN VALUE:
   u16 - size of header
===========================================================================*/
u16 QMICTLReleaseClientIDReqSize( void )
{
   return sizeof( sQMUX ) + 11;
}

/*===========================================================================
METHOD:
   QMICTLReadyReqSize (Public Method)

DESCRIPTION:
   Get size of buffer needed for QMUX + QMICTLReadyReq
 
RETURN VALUE:
   u16 - size of buffer
===========================================================================*/
u16 QMICTLReadyReqSize( void )
{
   return sizeof( sQMUX ) + 10;
}

/*===========================================================================
METHOD:
   QMIWDSSetEventReportReqSize (Public Method)

DESCRIPTION:
   Get size of buffer needed for QMUX + QMIWDSSetEventReportReq
 
RETURN VALUE:
   u16 - size of buffer
===========================================================================*/
u16 QMIWDSSetEventReportReqSize( void )
{
   return sizeof( sQMUX ) + 15;
}

/*===========================================================================
METHOD:
   QMIWDSGetPKGSRVCStatusReqSize (Public Method)

DESCRIPTION:
   Get size of buffer needed for QMUX + QMIWDSGetPKGSRVCStatusReq
 
RETURN VALUE:
   u16 - size of buffer
===========================================================================*/
u16 QMIWDSGetPKGSRVCStatusReqSize( void )
{
   return sizeof( sQMUX ) + 7;
}

u16 QMIWDSStartNetworkReqSize( void )
{
   return sizeof( sQMUX ) + 7 + 4;
}
u16 QMIWDASetDataFormatReqSettingsSize( void )
{
   return sizeof( sQMUX ) + 11;
}
u16 QMIWDASetDataFormatReqSize( void )
{
#ifdef TX_AGGR
   return sizeof( sQMUX ) + 71;
#else
   return sizeof( sQMUX ) + 57;
#endif
}
u16 QMIWDSGetRuntimeSettingsReqSize( void )
{
   return sizeof( sQMUX ) + 14;
}
/*===========================================================================
METHOD:
   QMIDMSGetMEIDReqSize (Public Method)

DESCRIPTION:
   Get size of buffer needed for QMUX + QMIDMSGetMEIDReq
 
RETURN VALUE:
   u16 - size of buffer
===========================================================================*/
u16 QMIDMSGetMEIDReqSize( void )
{
   return sizeof( sQMUX ) + 7;
}

/*=========================================================================*/
// Generic QMUX functions
/*=========================================================================*/

/*===========================================================================
METHOD:
   ParseQMUX (Public Method)

DESCRIPTION:
   Remove QMUX headers from a buffer

PARAMETERS
   pClientID       [ O ] - On success, will point to Client ID
   pBuffer         [ I ] - Full Message passed in
   buffSize        [ I ] - Size of pBuffer

RETURN VALUE:
   int - Positive for size of QMUX header
         Negative errno for error
===========================================================================*/
int ParseQMUX(
   u16 *    pClientID,
   void *   pBuffer,
   u16      buffSize )
{
   sQMUX * pQMUXHeader;
   
   if (pBuffer == 0 || buffSize < 12)
   {
      return -ENOMEM;
   }

   // QMUX Header
   pQMUXHeader = (sQMUX *)pBuffer;

   if (pQMUXHeader->mTF != 1
   ||  pQMUXHeader->mLength != buffSize - 1
   ||  pQMUXHeader->mCtrlFlag != 0x80 )
   {
      return -EINVAL;
   }

   // Client ID   
   *pClientID = (pQMUXHeader->mQMIClientID << 8) 
              + pQMUXHeader->mQMIService;
   
   return sizeof( sQMUX );
}

/*===========================================================================
METHOD:
   FillQMUX (Public Method)

DESCRIPTION:
   Fill buffer with QMUX headers

PARAMETERS
   clientID        [ I ] - Client ID
   pBuffer         [ O ] - Buffer to be filled
   buffSize        [ I ] - Size of pBuffer (must be at least 6)

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int FillQMUX(
   u16      clientID,
   void *   pBuffer,
   u16      buffSize )
{
   sQMUX * pQMUXHeader;

   if (pBuffer == 0 ||  buffSize < sizeof( sQMUX ))
   {
      return -ENOMEM;
   }

   // QMUX Header
   pQMUXHeader = (sQMUX *)pBuffer;

   pQMUXHeader->mTF = 1;
   pQMUXHeader->mLength = buffSize - 1;
   pQMUXHeader->mCtrlFlag = 0;

   // Service and Client ID   
   pQMUXHeader->mQMIService = clientID & 0xff;
   pQMUXHeader->mQMIClientID = clientID >> 8;

   return 0;
}

/*=========================================================================*/
// Generic QMI functions
/*=========================================================================*/

/*===========================================================================
METHOD:
   GetTLV (Public Method)

DESCRIPTION:
   Get data bufffer of a specified TLV from a QMI message

   QMI Message shall NOT include SDU
   
PARAMETERS
   pQMIMessage    [ I ] - QMI Message buffer
   messageLen     [ I ] - Size of QMI Message buffer
   type           [ I ] - Desired Type
   pOutDataBuf    [ O ] - Buffer to be filled with TLV
   messageLen     [ I ] - Size of QMI Message buffer

RETURN VALUE:
   u16 - Size of TLV for success
         Negative errno for error
===========================================================================*/
int GetTLV(
   void *   pQMIMessage,
   u16      messageLen,
   u8       type,
   void *   pOutDataBuf,
   u16      bufferLen )
{
   u16 pos;
   u16 tlvSize = 0;
   u16 cpyCount;
   
   if (pQMIMessage == 0 || pOutDataBuf == 0)
   {
      return -ENOMEM;
   }   
   
   for (pos = 4; 
        pos + 3 < messageLen; 
        pos += tlvSize + 3)
   {
      tlvSize = *(u16 *)(pQMIMessage + pos + 1);
      if (*(u8 *)(pQMIMessage + pos) == type)
      {
         if (bufferLen < tlvSize)
         {
            return -ENOMEM;
         }
        
         /* replacement memcpy
            memcpy( pOutDataBuf,
                    pQMIMessage + pos + 3,
                    tlvSize ); */
         
         for (cpyCount = 0; cpyCount < tlvSize; cpyCount++)
         {
            *((char*)(pOutDataBuf + cpyCount)) = *((char*)(pQMIMessage + pos + 3 + cpyCount));
         }
         
         return tlvSize;
      }
   }
   
   return -ENOMSG;
}

/*===========================================================================
METHOD:
   ValidQMIMessage (Public Method)

DESCRIPTION:
   Check mandatory TLV in a QMI message

   QMI Message shall NOT include SDU

PARAMETERS
   pQMIMessage    [ I ] - QMI Message buffer
   messageLen     [ I ] - Size of QMI Message buffer

RETURN VALUE:
   int - 0 for success (no error)
         Negative errno for error
         Positive for QMI error code
===========================================================================*/
int ValidQMIMessage(
   void *   pQMIMessage,
   u16      messageLen )
{
   char mandTLV[4];

   if (GetTLV( pQMIMessage, messageLen, 2, &mandTLV[0], 4 ) == 4)
   {
      // Found TLV
      if (*(u16 *)&mandTLV[0] != 0)
      {
         return *(u16 *)&mandTLV[2];
      }
      else
      {
         return 0;
      }
   }
   else
   {
      return -ENOMSG;
   }
}      

/*===========================================================================
METHOD:
   GetQMIMessageID (Public Method)

DESCRIPTION:
   Get the message ID of a QMI message
   
   QMI Message shall NOT include SDU

PARAMETERS
   pQMIMessage    [ I ] - QMI Message buffer
   messageLen     [ I ] - Size of QMI Message buffer

RETURN VALUE:
   int - Positive for message ID
         Negative errno for error
===========================================================================*/
int GetQMIMessageID(
   void *   pQMIMessage,
   u16      messageLen )
{
   if (messageLen < 2)
   {
      return -ENODATA;
   }
   else
   {
      return *(u16 *)pQMIMessage;
   }
}

/*=========================================================================*/
// Fill Buffers with QMI requests
/*=========================================================================*/

/*===========================================================================
METHOD:
   QMICTLGetClientIDReq (Public Method)

DESCRIPTION:
   Fill buffer with QMI CTL Get Client ID Request

PARAMETERS
   pBuffer         [ 0 ] - Buffer to be filled
   buffSize        [ I ] - Size of pBuffer
   transactionID   [ I ] - Transaction ID
   serviceType     [ I ] - Service type requested

RETURN VALUE:
   int - Positive for resulting size of pBuffer
         Negative errno for error
===========================================================================*/
int QMICTLGetClientIDReq(
   void *   pBuffer,
   u16      buffSize,
   u8       transactionID,
   u8       serviceType )
{
   if (pBuffer == 0 || buffSize < QMICTLGetClientIDReqSize() )
   {
      return -ENOMEM;
   }

   // QMI CTL GET CLIENT ID
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))= 0x00;
   // Transaction ID
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 2) = 0x0022;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 4) = 0x0004;
      // QMI Service Type
      *(u8 *)(pBuffer + sizeof( sQMUX ) + 6)  = 0x01;
      // Size
      *(u16 *)(pBuffer + sizeof( sQMUX ) + 7) = 0x0001;
      // QMI svc type
      *(u8 *)(pBuffer + sizeof( sQMUX ) + 9)  = serviceType;

   // success
   return sizeof( sQMUX ) + 10;
}

/*===========================================================================
METHOD:
   QMICTLReleaseClientIDReq (Public Method)

DESCRIPTION:
   Fill buffer with QMI CTL Release Client ID Request

PARAMETERS
   pBuffer         [ 0 ] - Buffer to be filled
   buffSize        [ I ] - Size of pBuffer
   transactionID   [ I ] - Transaction ID
   clientID        [ I ] - Service type requested

RETURN VALUE:
   int - Positive for resulting size of pBuffer
         Negative errno for error
===========================================================================*/
int QMICTLReleaseClientIDReq(
   void *   pBuffer,
   u16      buffSize,
   u8       transactionID,
   u16      clientID )
{
   if (pBuffer == 0 || buffSize < QMICTLReleaseClientIDReqSize() )
   {
      return -ENOMEM;
   }

   // QMI CTL RELEASE CLIENT ID REQ
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 1 ) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 2) = 0x0023;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 4) = 0x0005;
      // Release client ID
      *(u8 *)(pBuffer + sizeof( sQMUX ) + 6)  = 0x01;
      // Size
      *(u16 *)(pBuffer + sizeof( sQMUX ) + 7) = 0x0002;
      // QMI svs type / Client ID
      *(u16 *)(pBuffer + sizeof( sQMUX ) + 9)  = clientID;
      
   // success
   return sizeof( sQMUX ) + 11;
}

/*===========================================================================
METHOD:
   QMICTLReadyReq (Public Method)

DESCRIPTION:
   Fill buffer with QMI CTL Get Version Info Request

PARAMETERS
   pBuffer         [ 0 ] - Buffer to be filled
   buffSize        [ I ] - Size of pBuffer
   transactionID   [ I ] - Transaction ID

RETURN VALUE:
   int - Positive for resulting size of pBuffer
         Negative errno for error
===========================================================================*/
int QMICTLReadyReq(
   void *   pBuffer,
   u16      buffSize,
   u8       transactionID )
{
   if (pBuffer == 0 || buffSize < QMICTLReadyReqSize() )
   {
      return -ENOMEM;
   }

   // QMI CTL GET VERSION INFO REQ
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 2) = 0x0021;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 4) = 0x0004;

   // Link Layer protocol
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 6)  = 0x01;
   // Size
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 7) = 0x0001;
   // IP is enabled
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 9) = 0xFF;
   // success
   return sizeof( sQMUX ) + 10;
}

/*===========================================================================
METHOD:
   QMIWDSSetEventReportReq (Public Method)

DESCRIPTION:
   Fill buffer with QMI WDS Set Event Report Request

PARAMETERS
   pBuffer         [ 0 ] - Buffer to be filled
   buffSize        [ I ] - Size of pBuffer
   transactionID   [ I ] - Transaction ID

RETURN VALUE:
   int - Positive for resulting size of pBuffer
         Negative errno for error
===========================================================================*/
int QMIWDSSetEventReportReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID )
{
   if (pBuffer == 0 || buffSize < QMIWDSSetEventReportReqSize() )
   {
      return -ENOMEM;
   }

   // QMI WDS SET EVENT REPORT REQ
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 3) = 0x0001;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0008;
      // Report channel rate TLV
      *(u8 *)(pBuffer + sizeof( sQMUX ) + 7)  = 0x11;
      // Size
      *(u16 *)(pBuffer + sizeof( sQMUX ) + 8) = 0x0005;
      // Stats period
      *(u8 *)(pBuffer + sizeof( sQMUX ) + 10)  = 0x01;
      // Stats mask
      *(u32 *)(pBuffer + sizeof( sQMUX ) + 11)  = 0x000000ff;

   // success
   return sizeof( sQMUX ) + 15;
}
int QMIWDASetDataFormatReqSettings(
      void *   pBuffer,
      u16      buffSize,
      u16      transactionID )
{
   if (pBuffer == 0 || buffSize < QMIWDASetDataFormatReqSettingsSize() )
   {
      return -ENOMEM;
   }

   // QMI WDA SET DATA FORMAT REQ
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 3) = 0x002B;
   // Size of TLV's
   //*(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x001C;
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0004;
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 7)  = 0x10;
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 8) = 0x0001;
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 10)  = 0x00;

   // success
   return sizeof( sQMUX ) + 11;
}


/*===========================================================================
METHOD:
   QMIWDASetDataFormatReq(Public Method)

DESCRIPTION:
   Negotiating QMAP UL Data Aggregation protocol

PARAMETERS
   pBuffer         [ 0 ] - Buffer to be filled
   buffSize        [ I ] - Size of pBuffer
   transactionID   [ I ] - Transaction ID

RETURN VALUE:
   int - Positive for resulting size of pBuffer
         Negative errno for error
===========================================================================*/
int QMIWDASetDataFormatReq(
      void *   pBuffer,
      u16      buffSize,
      u16      transactionID,
      sGobiUSBNet * pDev)
{
   if (pBuffer == 0 || buffSize < QMIWDASetDataFormatReqSize() )
   {
      return -ENOMEM;
   }

   // QMI WDA SET DATA FORMAT REQ
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 3) = 0x0020;
   // Size of TLV's
   //*(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x001C;
#ifdef TX_AGGR
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0040;
#else
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0032;
#endif
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 7)  = 0x10;
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 8) = 0x0001;
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 10)  = 0x00;
   // Link Layer protocol
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 11)  = 0x11;
   // Size
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 12) = 0x0004;
   // IP is enabled
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 14) = 0x00000002;
   // UL Data aggregation protocol
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 18)  = 0x12;
   // Size
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 19) = 0x0004;
   // UL QMAP is enabled
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 21) = 0x00000005;

   // DL Data aggregation protocol
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 25)  = 0x13;
   // Size
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 26) = 0x0004;
   // DL QMAP is enabled
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 28) = 0x00000005;
   // DL Data aggregation Max datagrams
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 32)  = 0x15;
   // Size
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 33) = 0x0004;
   // Datagram is set as 32768
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 35) = pDev->DLAggregationMaxDatagram;
   // DL Data aggregation Max datagrams
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 39)  = 0x16;
   // Size
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 40) = 0x0004;
   // Datagram is set as 32768
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 42) = pDev->DLAggregationMaxSize;

   // DL Data aggregation Max datagrams
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 46)  = 0x17;
   // Size
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 47) = 0x0008;
   // Datagram is set as 32768
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 49) = 0x00000002;
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 53) = pDev->mpEndpoints->mIntfNum;

#ifdef TX_AGGR
   // UL Data aggregation Max datagrams
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 57)  = 0x1B;
   // Size
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 58) = 0x0004;
   // Max Datagram set to 32
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 60) = pDev->ULAggregationMaxDatagram;

   // UL Data aggregation Max datagrams size
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 64)  = 0x1C;
   // Size
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 65) = 0x0004;
   // Datagram is set as 32768
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 67) = pDev->ULAggregationMaxSize;

   // success
   return sizeof( sQMUX ) + 71;
#else
   // success
   return sizeof( sQMUX ) + 57;
#endif
}

/*===========================================================================
METHOD:
   QMIWDSGetPKGSRVCStatusReq (Public Method)

DESCRIPTION:
   Fill buffer with QMI WDS Get PKG SRVC Status Request

PARAMETERS
   pBuffer         [ 0 ] - Buffer to be filled
   buffSize        [ I ] - Size of pBuffer
   transactionID   [ I ] - Transaction ID

RETURN VALUE:
   int - Positive for resulting size of pBuffer
         Negative errno for error
===========================================================================*/
int QMIWDSGetPKGSRVCStatusReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID )
{
   if (pBuffer == 0 || buffSize < QMIWDSGetPKGSRVCStatusReqSize() )
   {
      return -ENOMEM;
   }

   // QMI WDS Get PKG SRVC Status REQ
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 3) = 0x0022;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0000;

   // success
   return sizeof( sQMUX ) + 7;
}

/*===========================================================================
METHOD:
   QMIDMSGetMEIDReq (Public Method)

DESCRIPTION:
   Fill buffer with QMI DMS Get Serial Numbers Request

PARAMETERS
   pBuffer         [ 0 ] - Buffer to be filled
   buffSize        [ I ] - Size of pBuffer
   transactionID   [ I ] - Transaction ID

RETURN VALUE:
   int - Positive for resulting size of pBuffer
         Negative errno for error
===========================================================================*/
int QMIDMSGetMEIDReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID )
{
   if (pBuffer == 0 || buffSize < QMIDMSGetMEIDReqSize() )
   {
      return -ENOMEM;
   }

   // QMI DMS GET SERIAL NUMBERS REQ
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 3) = 0x0025;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0000;

  // success
  return sizeof( sQMUX ) + 7;
}

/*=========================================================================*/
// Parse data from QMI responses
/*=========================================================================*/

/*===========================================================================
METHOD:
   QMICTLGetClientIDResp (Public Method)

DESCRIPTION:
   Parse the QMI CTL Get Client ID Resp

PARAMETERS
   pBuffer         [ I ] - Buffer to be parsed
   buffSize        [ I ] - Size of pBuffer
   pClientID       [ 0 ] - Recieved client ID

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int QMICTLGetClientIDResp(
   void * pBuffer,
   u16    buffSize,
   u16 *  pClientID )
{
   int result;
   u8 offset = 0;
   // Ignore QMUX and SDU
   //    QMI CTL SDU is 2 bytes, not 3
   offset = sizeof( sQMUX ) + 2;

   if (pBuffer == 0 || buffSize < offset)
   {
      return -ENOMEM;
   }

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   result = GetQMIMessageID( pBuffer, buffSize );
   if (result != 0x22)
   {
      return -EFAULT;
   }

   result = ValidQMIMessage( pBuffer, buffSize );
   if (result != 0)
   {
      return -EFAULT;
   }

   result = GetTLV( pBuffer, buffSize, 0x01, pClientID, 2 );
   if (result != 2)
   {
      return -EFAULT;
   }

   return 0;
}

/*===========================================================================
METHOD:
   QMICTLReleaseClientIDResp (Public Method)

DESCRIPTION:
   Verify the QMI CTL Release Client ID Resp is valid

PARAMETERS
   pBuffer         [ I ] - Buffer to be parsed
   buffSize        [ I ] - Size of pBuffer

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int QMICTLReleaseClientIDResp(
   void *   pBuffer,
   u16      buffSize )
{
   int result;
   u8 offset = 0;
   // Ignore QMUX and SDU
   //    QMI CTL SDU is 2 bytes, not 3
   offset = sizeof( sQMUX ) + 2;

   if (pBuffer == 0 || buffSize < offset)
   {
      return -ENOMEM;
   }

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   result = GetQMIMessageID( pBuffer, buffSize );
   if (result != 0x23)
   {
      return -EFAULT;
   }

   result = ValidQMIMessage( pBuffer, buffSize );
   if (result != 0)
   {
      return -EFAULT;
   }

   return 0;
}

/*===========================================================================
METHOD:
   QMIWDSEventResp (Public Method)

DESCRIPTION:
   Parse the QMI WDS Set Event Report Resp/Indication or
      QMI WDS Get PKG SRVC Status Resp/Indication

   Return parameters will only be updated if value was received

PARAMETERS
   pBuffer         [ I ] - Buffer to be parsed
   buffSize        [ I ] - Size of pBuffer
   pTXOk           [ O ] - Number of transmitted packets without errors
   pRXOk           [ O ] - Number of recieved packets without errors
   pTXErr          [ O ] - Number of transmitted packets with framing errors
   pRXErr          [ O ] - Number of recieved packets with framing errors
   pTXOfl          [ O ] - Number of transmitted packets dropped due to overflow
   pRXOfl          [ O ] - Number of recieved packets dropped due to overflow
   pTXBytesOk      [ O ] - Number of transmitted bytes without errors
   pRXBytesOk      [ O ] - Number of recieved bytes without errors
   pbLinkState     [ 0 ] - Is the link active?
   pbReconfigure   [ 0 ] - Must interface be reconfigured? (reset IP address)

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
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
   bool *   pbReconfigure )
{
   int result;
   u8 pktStatusRead[2];
   u8 offset = 0;
   // Ignore QMUX and SDU
   offset = sizeof( sQMUX ) + 3;

   if (pBuffer == 0 
   || buffSize < offset
   || pTXOk == 0
   || pRXOk == 0
   || pTXErr == 0
   || pRXErr == 0
   || pTXOfl == 0
   || pRXOfl == 0
   || pTXBytesOk == 0
   || pRXBytesOk == 0
   || pbLinkState == 0
   || pbReconfigure == 0 )
   {
      return -ENOMEM;
   }

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   // Note: Indications.  No Mandatory TLV required

   result = GetQMIMessageID( pBuffer, buffSize );
   // QMI WDS Set Event Report Resp
   if (result == 0x01)
   {
      // TLV's are not mandatory
      GetTLV( pBuffer, buffSize, 0x10, (void*)pTXOk, 4 );
      GetTLV( pBuffer, buffSize, 0x11, (void*)pRXOk, 4 );
      GetTLV( pBuffer, buffSize, 0x12, (void*)pTXErr, 4 );
      GetTLV( pBuffer, buffSize, 0x13, (void*)pRXErr, 4 );
      GetTLV( pBuffer, buffSize, 0x14, (void*)pTXOfl, 4 );
      GetTLV( pBuffer, buffSize, 0x15, (void*)pRXOfl, 4 );
      GetTLV( pBuffer, buffSize, 0x19, (void*)pTXBytesOk, 8 );
      GetTLV( pBuffer, buffSize, 0x1A, (void*)pRXBytesOk, 8 );
   }
   // QMI WDS Get PKG SRVC Status Resp
   else if ((result == 0x22)|| (result == 0x20))
   {
      result = GetTLV( pBuffer, buffSize, 0x01, &pktStatusRead[0], 2 );
      // 1 or 2 bytes may be received
      if (result >= 1)
      {
         if (pktStatusRead[0] == 0x02)
         {
            *pbLinkState = true;
         }
         else
         {
            *pbLinkState = false;
         }
      }
      if (result == 2)
      {
         if (pktStatusRead[1] == 0x01)
         {
            *pbReconfigure = true;
         }
         else
         {
            *pbReconfigure = false;
         }
      }
      
      if (result < 0)
      {
         return result;
      }
   }

   else
   {
      return -EFAULT;
   }

   return 0;
}

/*===========================================================================
METHOD:
   QMIDMSGetMEIDResp (Public Method)

DESCRIPTION:
   Parse the QMI DMS Get Serial Numbers Resp

PARAMETERS
   pBuffer         [ I ] - Buffer to be parsed
   buffSize        [ I ] - Size of pBuffer
   pMEID           [ O ] - Device MEID
   meidSize        [ I ] - Size of MEID buffer (at least 14)

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int QMIDMSGetMEIDResp(
   void *   pBuffer,
   u16      buffSize,
   char *   pMEID,
   int      meidSize )
{
   int result;
   u8 offset = 0;
   // Ignore QMUX and SDU
   offset = sizeof( sQMUX ) + 3;

   if (pBuffer == 0 || buffSize < offset || meidSize < 14)
   {
      return -ENOMEM;
   }

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   result = GetQMIMessageID( pBuffer, buffSize );
   if (result != 0x25)
   {
      return -EFAULT;
   }

   result = ValidQMIMessage( pBuffer, buffSize );
   if (result != 0)
   {
      return -EFAULT;
   }

   result = GetTLV( pBuffer, buffSize, 0x12, (void*)pMEID, 14 );
   if (result != 14)
   {
      return -EFAULT;
   }
   return 0;
}

int QMIWDASetDataFormatResp(
   void *   pBuffer,
   u16      buffSize,
   u32 *    DLDatagram,
   u32 *    DLDatagramSize,
   u32 *    ULDatagram,
   u32 *    ULDatagramSize )
{
   int result;
   u8 offset = 0;
   // Ignore QMUX and SDU
   offset = sizeof( sQMUX ) + 3;

   if (pBuffer == 0 || buffSize < offset )
   {
      return -ENOMEM;
   }

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   result = GetQMIMessageID( pBuffer, buffSize );
   if (result != 0x20)
   {
      return -EFAULT;
   }

   result = ValidQMIMessage( pBuffer, buffSize );
   if (result != 0)
   {
      return -EFAULT;
   }

#ifdef TX_AGGR
   result = GetTLV( pBuffer, buffSize, 0x15, (void*)DLDatagram, 4 );
   if (result != 4)
   {
      return -EFAULT;
   }

   result = GetTLV( pBuffer, buffSize, 0x16, (void*)DLDatagramSize, 4 );
   if (result != 4)
   {
      return -EFAULT;
   }

   result = GetTLV( pBuffer, buffSize, 0x17, (void*)ULDatagram, 4 );
   if (result != 4)
   {
      return -EFAULT;
   }

   result = GetTLV( pBuffer, buffSize, 0x18, (void*)ULDatagramSize, 4 );
   if (result != 4)
   {
      return -EFAULT;
   }

   QC_LOG_GLOBAL("\n\nDLDatagram : 0X%X, DLDatagramSize : 0X%X\n\n", *DLDatagram, *DLDatagramSize);
   QC_LOG_GLOBAL("\n\n ULDatagram : 0X%X, ULDatagramSize : 0X%X\n\n", *ULDatagram, *ULDatagramSize);
#else
   result = GetTLV( pBuffer, buffSize, 0x15, (void*)DLDatagram, 4 );
   if (result != 4)
   {
      return -EFAULT;
   }

   result = GetTLV( pBuffer, buffSize, 0x16, (void*)DLDatagramSize, 4 );
   if (result != 4)
   {
      return -EFAULT;
   }

#endif
   return 0;
}
int QMIWDSStartNetworkReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID,
   u8 profileid)
{
   if (pBuffer == 0)
   {
      return -ENOMEM;
   }

   // QMI WDS Get PKG SRVC Status REQ
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 3) = 0x0020;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0004;

   // set Porfile Id
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 7) = 0x31;
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 8) = 0x0001;
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 10) = profileid;
   
   // success
   return sizeof( sQMUX ) + 11;
}

int QMIWDSStartNetworkResp(
   void *   pBuffer,
   u16      buffSize,
   char *   pMEID,
   int      meidSize )
{
   int result;
   u8 offset = 0;
   // Ignore QMUX and SDU
   offset = sizeof( sQMUX ) + 3;

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   result = GetQMIMessageID( pBuffer, buffSize );
   if (result != 0x20)
   {
      printk("GetQMIMessageID failed\n");
      return -EFAULT;
   }

   result = ValidQMIMessage( pBuffer, buffSize );
   if (result != 0)
   {
      printk("ValidQMIMessage failed\n");
      return -EFAULT;
   }

   result = GetTLV( pBuffer, buffSize, 0x1, (void*)pMEID, 4 );
   if (result < 0 )
   {
      printk("GetTLV\n");
      return result;
   }

   return 0;
}

int QMIWDSStopNetworkReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID,
   int      connectid)
{
   if (pBuffer == 0)
   {
      return -ENOMEM;
   }

   // QMI WDS Get PKG SRVC Status REQ
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 3) = 0x0021;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0007;

   // set Porfile Id
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 7) = 0x01;
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 8) = 0x0004;
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 10) = connectid;
   
   // success
   return sizeof( sQMUX ) + 14;
}

int QMIWDSStopNetworkResp(
   void *   pBuffer,
   u16      buffSize,
   char *   pMEID,
   int      meidSize )
{
   int result;
   u8 offset = 0;
   // Ignore QMUX and SDU
   offset = sizeof( sQMUX ) + 3;

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   result = GetQMIMessageID( pBuffer, buffSize );
   if (result != 0x20)
   {
      printk("GetQMIMessageID failed\n");
      return -EFAULT;
   }

   result = ValidQMIMessage( pBuffer, buffSize );
   if (result != 0)
   {
      printk("ValidQMIMessage failed\n");
      return -EFAULT;
   }

   return 0;
}

int QMIWDSGetRuntimeSettingsReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID )
{
   if (pBuffer == 0)
   {
      return -ENOMEM;
   }

   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 3) = 0x002D;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0007;

   *(u8 *)(pBuffer + sizeof( sQMUX ) + 7)  = 0x10;
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 8)  = 0x0004;
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 10)  = 0x00002310;
   // success
   return sizeof( sQMUX ) + 14;
}

void PrintIPAddr(char *msg, unsigned int addr)
{
   printk("%s : %d.%d.%d.%d",
        msg,
        addr >> 24,
        (addr >> 16) & 0xff,
        (addr >> 8) & 0xff,
        (addr ) & 0xff
        );
}

void PrintIPV6Addr(char *msg, ipv6_addr * addr)
{
    char str[40];
    sprintf(str,"%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
    (int)addr->ipv6addr[0], (int)addr->ipv6addr[1],
    (int)addr->ipv6addr[2], (int)addr->ipv6addr[3],
    (int)addr->ipv6addr[4], (int)addr->ipv6addr[5],
    (int)addr->ipv6addr[6], (int)addr->ipv6addr[7],
    (int)addr->ipv6addr[8], (int)addr->ipv6addr[9],
    (int)addr->ipv6addr[10], (int)addr->ipv6addr[11],
    (int)addr->ipv6addr[12], (int)addr->ipv6addr[13],
    (int)addr->ipv6addr[14], (int)addr->ipv6addr[15]);
    //printk("%s : %s\n",msg, str);
}


#include <linux/inetdevice.h>

//Function for down the IP address from kernel space
int QMI_IP_Down(char *InterfaceName)
{
   char *cmdArg1=kzalloc(30,GFP_KERNEL);
   char *cmdArg2=kzalloc(30,GFP_KERNEL);
   int ret = 0;

   sprintf(cmdArg1,"%s%s","sudo ip addr flush dev ",InterfaceName);
   printk("QMI_IP_Down cmd-1: %s",cmdArg1);
   char *argv[] = {"/opt/QTI/QUD/rmnet/IPAssignmentScript.sh", cmdArg1, NULL};
   char *envp[] = {"PATH=$PATH:/usr/bin:/sbin", NULL};

   ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
   if (ret)
   {
      kfree(cmdArg1);
      kfree(cmdArg2);
      ret = 0;
      printk("QMI_IP_Down cmd-1 Failed");
      return ret;
   } 
   else 
   {
      kfree(cmdArg1);
      ret = 1;
      printk("QMI_IP_Down cmd-1 Successfull");
   }

   sprintf(cmdArg2,"%s%s%s","sudo ifconfig ",InterfaceName," down");
   printk("QMI_IP_Down cmd-2: %s",cmdArg2);

   char *argv2[] = {"/opt/QTI/QUD/rmnet/IPAssignmentScript.sh",cmdArg2,NULL};
   ret = call_usermodehelper(argv[0], argv2, envp, UMH_WAIT_EXEC);
   if (ret)
   {
      kfree(cmdArg2);
      ret = 0;
      printk("QMI_IP_Down cmd-2 Failed");
   } 
   else 
   {
      kfree(cmdArg2);
      ret = 1;
      printk("QMI_IP_Down cmd-2 Successfull");
   }

   return ret;
}

//Function for assigning IP address from kernel space
int QMIIPAssignment(
   unsigned int addr,
   sGobiUSBNet *pDev,
   char *subnetMask,
   sQMIDev *QMIDev
)
{
   char *ip=kzalloc(16,GFP_KERNEL);
   char *cmdArg=kzalloc(65,GFP_KERNEL);
   int ret = 0;
   unsigned int MuxId;
   char InterfaceName[IFNAMSIZ];
   memset(InterfaceName, '\0', IFNAMSIZ);

   MuxId = QMIDev->MuxId - 0x81;
   if (MuxId == 0)
   {
      memcpy(InterfaceName,pDev->mpNetDev->net->name,IFNAMSIZ);
   }
   else if (MuxId <= MAX_MUX_DEVICES)
   {
      memcpy(InterfaceName,pDev->mpNetMUXDev[MuxId - 1]->net->name,IFNAMSIZ);
   }

   printk("\nInside QMIIPAssignment\n");
   //IP Assignment
   if((addr>>24)>0)
   {
      sprintf(ip,"%d.%d.%d.%d",(addr>>24),((addr>>16) & 0xff),((addr>>8) & 0xff),((addr) & 0xff));
      printk("IP Address :%s",ip);
      printk("Subnet mask :%s",subnetMask);
      sprintf(cmdArg,"%s%s %s%s%s%s","sudo ifconfig ",InterfaceName,ip," netmask ",subnetMask," up");
      printk("cmd: %s",cmdArg);
      char *argv[] = {"/opt/QTI/QUD/rmnet/IPAssignmentScript.sh",cmdArg,NULL};
      char *envp[] = {"PATH=$PATH:/usr/bin:/sbin", NULL};
      ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
      if (ret)
      {
         kfree (ip);
         kfree (cmdArg);
         ret = 0;
      } 
      else 
      {
         kfree (ip);
         kfree (cmdArg);
         ret = 1;
      }
   }

      return ret;
}

int IPRoute(char *netAddr,
            unsigned int addr,
            char *subnetMask)
{
   char *gatewayIP=kzalloc(16,GFP_KERNEL);
   char *cmdArg=kzalloc(90,GFP_KERNEL);
   int ret = 0;
   printk("\nInside IPRoute()\n");

   if((addr>>24)>0)
   {
      sprintf(gatewayIP,"%d.%d.%d.%d",(addr>>24),((addr>>16) & 0xff),((addr>>8) & 0xff),((addr) & 0xff));
      printk("gatewayIP :%s",gatewayIP);
      printk("network address :%s",netAddr);
      sprintf(cmdArg,"%s%s%s%s%s%s","sudo route add -net ",netAddr," netmask ",subnetMask," gw ",gatewayIP);
      printk("IPRoute cmd: %s",cmdArg);
      char *argv[] = {"/opt/QTI/QUD/rmnet/IPAssignmentScript.sh",cmdArg,NULL};
      char *envp[] = {"PATH=$PATH:/usr/bin:/sbin", NULL};
      ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
      if (ret)
      {
         kfree (gatewayIP);
         kfree (cmdArg);
         ret = 0;
      } 
      else 
      {
         kfree (gatewayIP);
         kfree (cmdArg);
         ret = 1;
      }
   }
   
   return ret;
}

int QMIWDSGetRuntimeSettingsResp(
   sGobiUSBNet *pDev,
   void *   pBuffer,
   u16      buffSize,
   sQMIDev *QMIDev)
{
   unsigned int addr_tmp = 0;
   int result;
   int mtu;
   unsigned int addr;
   ipv6_addr ipv6;
   char *subnetMask=kzalloc(16,GFP_KERNEL);
   char *netAddr=kzalloc(16,GFP_KERNEL);
   u8 offset = 0;
   // Ignore QMUX and SDU
   offset = sizeof( sQMUX ) + 3;

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   result = GetQMIMessageID( pBuffer, buffSize );
   if (result != 0x2D)
   {
      printk("GetQMIMessageID failed\n");
      return -EFAULT;
   }

   result = ValidQMIMessage( pBuffer, buffSize );
   if (result != 0)
   {
      printk("ValidQMIMessage failed\n");
      return -EFAULT;
   }

   result = GetTLV( pBuffer, buffSize, 0x21, (void*)&addr, 4 );
   if (result > 0 ) 
   {
      PrintIPAddr("\nMask : ", addr);
      if((addr>>24)>0) 
         sprintf(subnetMask,"%d.%d.%d.%d",(addr>>24),((addr>>16) & 0xff),((addr>>8) & 0xff),((addr) & 0xff));
      QMIDev->IPv4SubnetMask = addr;
   }

   result = GetTLV( pBuffer, buffSize, 0x1E, (void*)&addr, 4 );
   if (result > 0 ) 
   {
      PrintIPAddr("\nIPv4 Addr : ", addr);
      QMIDev->IPv4Addr = addr;
      printk("IP address : %x\n", QMIDev->IPv4Addr);

      if(QMIIPAssignment(addr,pDev,subnetMask,QMIDev)){
         printk("IP assignment Successfull");
      }
      else
      {
         printk("IP not assigned");
      }

   }

   result = GetTLV( pBuffer, buffSize, 0x20, (void*)&addr, 4 );
   if (result > 0 ) 
   {
      PrintIPAddr("\nGateway : ", addr);
      QMIDev->IPv4Gateway = addr;

      addr_tmp = QMIDev->IPv4Addr & QMIDev->IPv4SubnetMask;
      if((addr_tmp>>24)>0) 
         sprintf(netAddr,"%d.%d.%d.%d",(addr_tmp>>24),((addr_tmp>>16) & 0xff),((addr_tmp>>8) & 0xff),((addr_tmp) & 0xff));

      if (IPRoute(netAddr, addr, subnetMask)) {
         printk("IP route successfull");
      }
      else
      {
         printk("IP route not successfull");
      }

     kfree(subnetMask);
     kfree(netAddr);
   }

   result = GetTLV( pBuffer, buffSize, 0x15, (void*)&addr, 4 );
   if (result > 0 ) 
   {
      PrintIPAddr("\nPrimary DNS : ", addr);
      QMIDev->IPv4PrimaryDNS = addr;
   }

   result = GetTLV( pBuffer, buffSize, 0x16, (void*)&addr, 4 );
   if (result > 0 ) 
   {
      PrintIPAddr("\nSecondary DNS : ", addr);
      QMIDev->IPv4SecondaryDNS = addr;
   }

   result = GetTLV( pBuffer, buffSize, 0x25, (void*)&ipv6, sizeof(ipv6_addr) );
   if (result > 0 ) 
   {
      PrintIPV6Addr("\nIPV6 address : ", &ipv6);
      printk("Prefix : %d\n", ipv6.prefix);
      memcpy(&QMIDev->ipv6_address, &ipv6, sizeof(ipv6_addr));
   }

   result = GetTLV( pBuffer, buffSize, 0x26, (void*)&ipv6, sizeof(ipv6_addr) );
   if (result > 0 ) 
   {
      PrintIPV6Addr("\nIPV6 Gateway address : ", &ipv6);
      printk("Prefix : %d\n", ipv6.prefix);
      memcpy(&QMIDev->ipv6_gateway, &ipv6, sizeof(ipv6_addr));
   }

   result = GetTLV( pBuffer, buffSize, 0x27, (void*)&ipv6, sizeof(ipv6_addr) );
   if (result > 0 ) 
   {
      PrintIPV6Addr("\nIPV6 Primary DNS address : ", &ipv6);
      printk("Prefix : %d\n", ipv6.prefix);
      memcpy(&QMIDev->ipv6_primaydns, &ipv6, sizeof(ipv6_addr));
   }

   result = GetTLV( pBuffer, buffSize, 0x28, (void*)&ipv6, sizeof(ipv6_addr) );
   if (result > 0 ) 
   {
      PrintIPV6Addr("\nIPV6 Secondary DNS address : ", &ipv6);
      printk("Prefix : %d\n", ipv6.prefix);
      memcpy(&QMIDev->ipv6_secondarydns, &ipv6, sizeof(ipv6_addr));
   }

   result = GetTLV( pBuffer, buffSize, 0x29, (void*)&mtu, 4 );
   if (result > 0 ) 
   {
      unsigned int MuxId;
      printk("\nMTU : %d\n", mtu);

      MuxId = QMIDev->MuxId - 0x81;
      if (MuxId == 0)
      {
          if (pDev->mpNetDev->net->flags & IFF_UP)
          {
              usbnet_change_mtu(pDev->mpNetDev->net, mtu);
          }
      } else if (MuxId <= MAX_MUX_DEVICES){
          if (pDev->mpNetMUXDev[MuxId - 1]->net->flags & IFF_UP)
          {
              usbnet_change_mtu(pDev->mpNetMUXDev[MuxId - 1]->net, mtu);
          }
      }
   }

   return 0;
}

int QMIWDSSetIPFamilyPrefReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID,
   u8       ipType)
{
   if (pBuffer == 0)
   {
      return -ENOMEM;
   }

   // QMI WDS Set Ip family pref REQ
   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 3) = 0x004D;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0004;
   // IP pref msg id, len and value as IPv4
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 7)  = 0x01;
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 8)  = 0x0001;
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 10)  = ipType;

   // success
   return sizeof( sQMUX ) + 11;
}

int QMIWDSBindMuxPortReq(
   void *   pBuffer,
   u16      buffSize,
   u16      transactionID,
   sGobiUSBNet *pDev,
   sQMIDev *QMIDev)
{
   if (pBuffer == 0)
   {
      return -ENOMEM;
   }

   // QMI WDS BIND_MUX_DATA_PORT

   // Request
   *(u8 *)(pBuffer + sizeof( sQMUX ))  = 0x00;
   // Transaction ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 1) = transactionID;
   // Message ID
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 3) = 0x00a2;
   // Size of TLV's
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 5) = 0x0016;
   // ep type, len as 8 and value as HSUSB, Ifc number as 4
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 7)  = 0x10;
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 8)  = 0x0008;
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 10)  = 0x00000002; //HSUSB
   *(u32 *)(pBuffer + sizeof( sQMUX ) + 14)  = pDev->mpEndpoints->mIntfNum; //Interface Num
   // muxid , value is 0x81
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 18)  = 0x11;
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 19)  = 0x0001;
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 21)  = QMIDev->MuxId;

   // client type as tethered
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 22)  = 0x13;
   *(u16 *)(pBuffer + sizeof( sQMUX ) + 23)  = 0x0004;
   *(u8 *)(pBuffer + sizeof( sQMUX ) + 25)  = 0x00000001; //teathered


   // success
   return sizeof( sQMUX ) + 29;
}


unsigned short GetTransactionID(sQMIDev *QMIDev)
{
   unsigned short transactionID = atomic_add_return( 1, (atomic_t *)&QMIDev->mQMITransactionID );
   if (transactionID == 0)
   {
      transactionID = atomic_add_return( 1, (atomic_t *)&QMIDev->mQMITransactionID);
   }
   return transactionID;
}

static int WDSGetRuntimeSettings(sGobiUSBNet *pDev, sQMIDev *QMIDev, u16 clientID)
{
    int id;
    int result;
    void * pWriteBuffer;
    u16 writeBufferSize;

    // Get IP Address Settings:
    id = clientID;
    writeBufferSize = QMIWDSGetRuntimeSettingsReqSize();
    pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
    if (pWriteBuffer == NULL)
    {
        printk("Mem alloc failed\n");
        return -ENOMEM;
    }

    result = QMIWDSGetRuntimeSettingsReq( pWriteBuffer,
            writeBufferSize,
            GetTransactionID(QMIDev));
    if (result < 0)
    {
        printk("Preparing Request Runttime settings failed\n");
        kfree( pWriteBuffer );
        return result;
    }

    result = WriteSync( pDev,
            pWriteBuffer,
            writeBufferSize,
            id,
            QMIDev);
    kfree( pWriteBuffer );
    if (result < 0)
    {
        printk("Request runtime settings  Failed\n");
        return result;
    }

    return 0;
}

struct ConnStatusData{
    u8 connection_status;
    u8 reconfiguration_required;
#define WDS_CONNECTION_STATUS_DISCONNECTED      (0x01) //--  Disconnected 
#define WDS_CONNECTION_STATUS_CONNECTED         (0x02) //--  Connected 
#define WDS_CONNECTION_STATUS_SUSPENDED         (0x03) //--  Suspended
#define WDS_CONNECTION_STATUS_AUTHENTICATING    (0x04) //--  Authenciating 
};

int QMIWDSGetPktSrvcStatusInd (
   sGobiUSBNet *pDev,
   void *   pBuffer,
   u16      buffSize,
   sQMIDev *QMIDev,
   u16 clientID)
{
   struct net_device *net = NULL;
   struct ConnStatusData Status;
   char IFName[IFNAMSIZ];
   unsigned int MuxId;
   int result;
   int flags;
   u8 offset = 0;

   if (IsDeviceValid(pDev) == false)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device!\n" );
      return -ENXIO;;
   }

   // Ignore QMUX and SDU
   offset = sizeof( sQMUX ) + 3;

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   result = GetQMIMessageID( pBuffer, buffSize );
   if (result != 0x0022)
   {
      QC_LOG_DBG(QMIDev, "GetQMIMessageID failed\n");
      return -EFAULT;
   }

   result = GetTLV( pBuffer, buffSize, 0x01, &Status, sizeof(struct ConnStatusData) );
   if (result > 0 ) 
   {
      MuxId = QMIDev->MuxId - 0x81;
      memset(IFName, '\0', IFNAMSIZ);

      if (MuxId == 0)
      {
          net = pDev->mpNetDev->net;
      } else if (MuxId <= MAX_MUX_DEVICES) {
          net = pDev->mpNetMUXDev[MuxId - 1]->net;
      } else {
          return -EFAULT;
      }

      flags = net->flags;
      memcpy(IFName,net->name,IFNAMSIZ);

      QC_LOG_DBG(QMIDev, "Connection status : %X, recon: %X\n", Status.connection_status, Status.reconfiguration_required);
      if (Status.connection_status == WDS_CONNECTION_STATUS_CONNECTED)
      {
          /* To make interface up */
          if (!(flags & IFF_UP))
          {
             
#ifdef QCUSB_RHEL
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
              flags |= IFF_UP;
              rtnl_lock();
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              dev_change_flags( net, flags);
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              rtnl_unlock();
#else
              flags |= IFF_UP;
              rtnl_lock();
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              dev_change_flags( net, flags, NULL);
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              rtnl_unlock();
#endif
#endif

#ifndef QCUSB_RHEL
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              flags |= IFF_UP;
              rtnl_lock();
              dev_change_flags( net, flags);
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              rtnl_unlock();
#else
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              flags |= IFF_UP;
              rtnl_lock();
              dev_change_flags( net, flags, NULL);
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              rtnl_unlock();
#endif
#endif
              WDSGetRuntimeSettings(pDev, QMIDev, clientID);
          }
          QC_LOG_DBG(QMIDev, "Network connected :%d\n", rtnl_is_locked());
      } 
      else if (Status.connection_status == WDS_CONNECTION_STATUS_DISCONNECTED)
      {

      if(QMI_IP_Down(IFName)){
         printk("IP Down execution successfull");
      }
      else
      {
         printk("IP Down execution failed");
      }

#ifdef QCUSB_RHEL
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
          /* To make interface down */
          if (flags & IFF_UP)
          {
              rtnl_lock();
              flags = flags & ~IFF_UP;
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              dev_change_flags( net, flags);
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              rtnl_unlock();
          }
#else
          if (flags & IFF_UP)
          {
              rtnl_lock();
              flags = flags & ~IFF_UP;
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              dev_change_flags( net, flags, NULL);
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              rtnl_unlock();
          }
#endif
#endif

#ifndef QCUSB_RHEL
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
          /* To make interface down */
          if (flags & IFF_UP)
          {
              rtnl_lock();
              flags = flags & ~IFF_UP;
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              dev_change_flags( net, flags);
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              rtnl_unlock();
          }
#else
          if (flags & IFF_UP)
          {
              rtnl_lock();
              flags = flags & ~IFF_UP;
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              dev_change_flags( net, flags, NULL);
              QC_LOG_DBG(QMIDev, "flags : %X\n", net->flags);
              rtnl_unlock();
          }
#endif
#endif
         
         QC_LOG_GLOBAL("Network disconnected : %d\n", rtnl_is_locked());
      }
   }
   return 0;
}

int QMIWDSExtendedIPConfigInd (
   sGobiUSBNet *pDev,
   void *   pBuffer,
   u16      buffSize,
   sQMIDev *QMIDev,
   u16 clientID)
{
   int result;
   unsigned int status;
   u8 offset = 0;

   // Ignore QMUX and SDU
   offset = sizeof( sQMUX ) + 3;

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   result = GetQMIMessageID( pBuffer, buffSize );
   if (result != 0x008C)
   {
      printk("GetQMIMessageID failed\n");
      return -EFAULT;
   }
   /**<   Set bits to 1, corresponding to configuration changed. Values: \n
     - Bit 4  -- DNS address \n
     - Bit 9  -- Gateway information (address and subnet mask) \n
     - Bit 10 -- PCSCF address using PCO flag \n
     - Bit 11 -- PCSCF server address list \n
     - Bit 12 -- PCSCF domain name list \n
     - Bit 13 -- MTU \n
     - Bit 14 -- Domain name list \n
     - Bit 18 -- Operator reserved PCO \n
     - Bit 19 -- Operator reserved PCO list \n
     - Bit 20 -- MSISDN information
    */

   result = GetTLV( pBuffer, buffSize, 0x10, &status, sizeof(uint32_t) );
   if (result > 0 ) 
   {
      if (status & 0x00002000)
      {
          WDSGetRuntimeSettings(pDev, QMIDev, clientID);
          printk("Change in the MTU indication received\n");
      }
   }

   return 0;
}

int QMIWDSReverseIPTransportConnInd (
   sGobiUSBNet *pDev,
   void *   pBuffer,
   u16      buffSize,
   sQMIDev *QMIDev)
{
   int result;
   unsigned int mtu;
   u8 offset = 0;

   // Ignore QMUX and SDU
   offset = sizeof( sQMUX ) + 3;

   pBuffer = pBuffer + offset;
   buffSize -= offset;

   result = GetQMIMessageID( pBuffer, buffSize );
   if (result != 0x008E)
   {
      printk("GetQMIMessageID failed\n");
      return -EFAULT;
   }

   result = GetTLV( pBuffer, buffSize, 0x15, (void*)&mtu, sizeof(uint32_t));
   if (result > 0 )
   {
       unsigned int MuxId;
       printk("\nMTU : %d\n", mtu);

       MuxId = QMIDev->MuxId - 0x81;
       if (MuxId == 0)
       {
           if (pDev->mpNetDev->net->flags & IFF_UP)
           {
               usbnet_change_mtu(pDev->mpNetDev->net, mtu);
           }
       } else if (MuxId <= MAX_MUX_DEVICES)
       {
           if (pDev->mpNetMUXDev[MuxId - 1]->net->flags & IFF_UP)
           {
               usbnet_change_mtu(pDev->mpNetMUXDev[MuxId - 1]->net, mtu);
           }
       }
   }

   return 0;
}
