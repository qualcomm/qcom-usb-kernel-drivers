
/*===========================================================================

   QMIDevice.c

DESCRIPTION:
   Functions related to the QMI interface device
   
FUNCTIONS:
   Generic functions
      IsDeviceValid
      PrintHex
      GobiSetDownReason
      GobiClearDownReason
      GobiTestDownReason

   Driver level asynchronous read functions
      ResubmitIntURB
      ReadCallback
      IntCallback
      StartRead
      KillRead

   Internal read/write functions
      ReadAsync
      UpSem
      ReadSync
      ReadSyncTimeout
      WriteSyncCallback
      WriteSync

   Internal memory management functions
      GetClientID
      ReleaseClientID
      FindClientMem
      AddToReadMemList
      PopFromReadMemList
      AddToNotifyList
      NotifyAndPopNotifyList
      AddToURBList
      PopFromURBList

   Userspace wrappers
      UserspaceOpen
      UserspaceIOCTL
      UserspaceClose
      UserspaceRead
      UserspaceWrite
      UserspacePoll

   Initializer and destructor
      RegisterQMIDevice
      DeregisterQMIDevice

   Driver level client management
      QMIReady
      QMIWDSCallback
      SetupQMIWDSCallback
      QMIDMSGetMEID

Copyright (c) 2019,2020 The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
==========================================================================*/
//---------------------------------------------------------------------------
// Include Files
//---------------------------------------------------------------------------
#include "QMIDevice.h"

//-----------------------------------------------------------------------------
// Definitions
//-----------------------------------------------------------------------------

extern int debug_g;
extern int interruptible;
extern int use_down_timeout;
extern sGobiUSBNet * gpGobiDev;
static struct mutex IoMutex;

long UnlockedUserspaceIOCTL(struct file *, unsigned int, unsigned long);
int WriteAsync(sGobiUSBNet *,char *, int, u16, sQMIDev *, sIoData *);
void QMIAIOReadcallback(sGobiUSBNet *, u16, void *, sQMIDev *);
int GobiSuspend(struct usb_interface *, pm_message_t); /*Prototype to GobiSuspend function*/
static int GobiWaitForResponses(void *);
void WriteAsyncCallback(struct urb *);

// UnlockedUserspacecIOCTL is a simple passthrough to UserspacecIOCTL
long UnlockedUserspaceIOCTL(
   struct file *     pFilp,
   unsigned int      cmd,
   unsigned long     arg )
{
   return UserspaceIOCTL( NULL, pFilp, cmd, arg );
}

// IOCTL to generate a client ID for this service type
#define IOCTL_QMI_GET_SERVICE_FILE 0x8BE0 + 1

// IOCTL to get the VIDPID of the device
#define IOCTL_QMI_GET_DEVICE_VIDPID 0x8BE0 + 2

// IOCTL to get the MEID of the device
#define IOCTL_QMI_GET_DEVICE_MEID 0x8BE0 + 3

// CDC GET_ENCAPSULATED_RESPONSE packet
#define CDC_GET_ENCAPSULATED_RESPONSE 0x01A1ll

// CDC CONNECTION_SPEED_CHANGE indication packet
#define CDC_CONNECTION_SPEED_CHANGE 0x08000000002AA1ll

#define IOCTL_QMIREADY 0x8BE0 + 4

#define IOCTL_QMI_BIND_MUX_ID 0x8BE0 + 5

static ssize_t UserspaceWriteIter(struct kiocb *kiocb, struct iov_iter *iov);
static ssize_t UserspaceReadIter(struct kiocb *kiocb, struct iov_iter *iov);
void WriteAsyncCallback( struct urb * pWriteURB );
/*=========================================================================*/
// UserspaceQMIFops
//    QMI device's userspace file operations
/*=========================================================================*/
struct file_operations UserspaceQMIFops = 
{
   .owner     = THIS_MODULE,
   .read      = UserspaceRead,
   .write     = UserspaceWrite,
   .read_iter = UserspaceReadIter,
   .write_iter= UserspaceWriteIter,
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,35 ))
   .ioctl     = UserspaceIOCTL,
#else
   .unlocked_ioctl = UnlockedUserspaceIOCTL,
#endif
   .open      = UserspaceOpen,
   .flush     = UserspaceClose,
   .poll      = UserspacePoll,
};

/*=========================================================================*/
// Generic functions
/*=========================================================================*/

/*===========================================================================
METHOD:
   IsDeviceValid (Public Method)

DESCRIPTION:
   Basic test to see if device memory is valid

PARAMETERS:
   pDev     [ I ] - Device specific memory

RETURN VALUE:
   bool
===========================================================================*/
bool IsDeviceValid( sGobiUSBNet * pDev )
{
   if (pDev == NULL)
   {
      return false;
   }

   if (pDev->mbQMIValid == false)
   {
      return false;
   }
   
   return true;
} 

/*===========================================================================
METHOD:
   PrintHex (Public Method)

DESCRIPTION:
   Print Hex data, for debug purposes

PARAMETERS:
   pBuffer       [ I ] - Data buffer
   bufSize       [ I ] - Size of data buffer

RETURN VALUE:
   None
===========================================================================*/
void PrintHex(
   const void *   pBuffer,
   u16            bufSize)
{
  //  if(debug)
    {
        char * pPrintBuf;
        u16 pos;
        int status;

        pPrintBuf = kzalloc( bufSize * 3 + 1, GFP_ATOMIC );
        if (pPrintBuf == NULL)
        {
           QC_LOG_GLOBAL("Unable to allocate buffer\n" );
            return;
        }
        memset( pPrintBuf, 0 , bufSize * 3 + 1 );

        for (pos = 0; pos < bufSize; pos++)
        {
            status = snprintf( (pPrintBuf + (pos * 3)), 
                    4, 
                    "%02X ", 
                    *(u8 *)(pBuffer + pos) );
            if (status != 3)
            {
               QC_LOG_GLOBAL( "snprintf error %d\n", status );
                kfree( pPrintBuf );
                return;
            }
        }

        printk(KERN_NOTICE "   : %s\n", pPrintBuf );

        kfree( pPrintBuf );
        pPrintBuf = NULL;
    }
    return;   
}

/*===========================================================================
METHOD:
   GobiSetDownReason (Public Method)

DESCRIPTION:
   Sets mDownReason and turns carrier off

PARAMETERS
   pDev     [ I ] - Device specific memory
   reason   [ I ] - Reason device is down

RETURN VALUE:
   None
===========================================================================*/
void GobiSetDownReason(
   sGobiUSBNet *    pDev,
   u8                 reason )
{
   if (pDev == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"failed to get QMIDevice\n" );
      return;
   }
   usbnet_link_change(pDev->mpNetDev, 0, 0);
   QC_LOG_DBG(GET_QMIDEV(pDev),"\n");
}

/*===========================================================================
METHOD:
   GobiClearDownReason (Public Method)

DESCRIPTION:
   Clear mDownReason and may turn carrier on

PARAMETERS
   pDev     [ I ] - Device specific memory
   reason   [ I ] - Reason device is no longer down

RETURN VALUE:
   None
===========================================================================*/
void GobiClearDownReason(
   sGobiUSBNet *    pDev,
   u8                 reason )
{
   if (IsDeviceValid(pDev) == false)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device!\n" );
      return;
   }
   QC_LOG_DBG(GET_QMIDEV(pDev), "GobiClearDownReason\n");
   if (pDev->mDownReason == 0)
   {
      usbnet_link_change(pDev->mpNetDev, 1, 0);
   }
}

/*===========================================================================
METHOD:
   GobiTestDownReason (Public Method)

DESCRIPTION:
   Test mDownReason and returns whether reason is set

PARAMETERS
   pDev     [ I ] - Device specific memory
   reason   [ I ] - Reason device is down

RETURN VALUE:
   bool
===========================================================================*/
bool GobiTestDownReason(
   sGobiUSBNet *    pDev,
   u8                 reason )
{
   if (IsDeviceValid(pDev) == false)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device!\n" );
      return false;
   }
   return test_bit( reason, &pDev->mDownReason );
}

/*=========================================================================*/
// Driver level asynchronous read functions
/*=========================================================================*/

/*===========================================================================
METHOD:
   ResubmitIntURB (Public Method)

DESCRIPTION:
   Resubmit interrupt URB, re-using same values

PARAMETERS
   pIntURB       [ I ] - Interrupt URB 

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int ResubmitIntURB( struct urb * pIntURB )
{
   int status;
   int interval;
   // Sanity test
   if ( (pIntURB == NULL)
   ||   (pIntURB->dev == NULL) )
   {
      return -EINVAL;
   }
 
   // Interval needs reset after every URB completion
   interval = 9;

   // Reschedule interrupt URB
   usb_fill_int_urb( pIntURB,
                     pIntURB->dev,
                     pIntURB->pipe,
                     pIntURB->transfer_buffer,
                     pIntURB->transfer_buffer_length,
                     pIntURB->complete,
                     pIntURB->context,
                     interval );
   status = usb_submit_urb( pIntURB, GFP_ATOMIC );
   if (status != 0)
   {
      QC_LOG_GLOBAL( "Error re-submitting Int URB %d\n", status );
   }
   return status;
}

static void AddToProcessIndList(sGobiUSBNet *pDev, sQMIDev *QMIDev, int MsgId, char *pData, int dataSize, u16 clientId)
{
    sIndDataInfo *pIndDataInfo;
    sIndDataInfo *pIndDataInfoList;
    char *pDataCopy;
    unsigned long flags;
    spinlock_t  *pResponeListLock;

    if (!pDev || !QMIDev || !pData || !dataSize)
    {
        QC_LOG_ERR(QMIDev,"Invalid Response data to process\n");
        return;
    }
    pResponeListLock = pDev->mProcessIndData.mpResponeListLock;

    pIndDataInfo = kzalloc(sizeof(sIndDataInfo), GFP_ATOMIC);
    if (pIndDataInfo == NULL)
    {
        QC_LOG_ERR(QMIDev, "Unable to allocate memory\n");
        return;
    }
    pDataCopy = kzalloc(dataSize, GFP_ATOMIC);
    if (pDataCopy == NULL)
    {
        QC_LOG_ERR(QMIDev, "Unable to allocate memory\n");
        kfree(pIndDataInfo);
        return;
    }
    memcpy(pDataCopy, pData, dataSize);

    pIndDataInfo->mMsgId    = MsgId;
    pIndDataInfo->mpData    = pDataCopy;
    pIndDataInfo->mpNext    = NULL;
    pIndDataInfo->mDataLen  = dataSize;
    pIndDataInfo->mClientId = clientId; 
    pIndDataInfo->mQMIDev   = QMIDev; 

    /* Lock here */
    spin_lock_irqsave(pResponeListLock, flags);
    pIndDataInfoList = pDev->mProcessIndData.mpIndDataInfoList;

    if (pIndDataInfoList == NULL)
    {
        pDev->mProcessIndData.mpIndDataInfoList = pIndDataInfo;
    } else
    {
        /* Add at the end of list */
        while (pIndDataInfoList->mpNext != NULL)
        {
            pIndDataInfoList = pIndDataInfoList->mpNext;
        }
        pIndDataInfoList->mpNext = pIndDataInfo;
    }

    /* Unlock here */
    spin_unlock_irqrestore(pResponeListLock, flags);

    return;
}

static void processIndResponses(char *pDataCopy, sGobiUSBNet *pDev, sQMIDev *QMIDev, u16 dataSize, u16 clientId)
{
    u8 cntlFlg;
    struct semaphore *pReadSem;
    memcpy(&cntlFlg, pDataCopy+QMUXHeaderSize(), sizeof(uint8_t));
	
    if (IsDeviceValid(pDev) == false)
    {
       QC_LOG_ERR(QMIDev,"Invalid device!\n" );
       return;
    }
    pReadSem = pDev->mProcessIndData.mpResponseReadSem;

    //To check Indication msg and 
    if ( ((sQMUX *)(pDataCopy))->mQMIService == QMIWDS )
    {
#define QMI_WDS_GET_RUNTIME_SETTINGS_RESP   0x002D
#define QMI_WDS_PKT_SRVC_STATUS_IND         0x0022
#define QMI_WDS_EXTENDED_IP_CONFIG_IND      0x008C
#define QMI_WDS_REVERSE_IP_TRANSPORT_CONNECTION_IND 0x008E
        int MsgId;
        u8 offset = sizeof( sQMUX ) + 3;

        MsgId = GetQMIMessageID( pDataCopy + offset, dataSize - offset );

        if (cntlFlg == QMI_INDICATION_CONTROL_FLAG)
        {
            switch (MsgId)
            {
                case QMI_WDS_PKT_SRVC_STATUS_IND:
                    QC_LOG_INFO(QMIDev, "QMI_WDS_PKT_SRVC_STATUS_IND\n");
                    AddToProcessIndList(pDev, QMIDev, MsgId, pDataCopy, dataSize, clientId);
                    up(pReadSem);
                    QC_LOG_INFO(QMIDev,"Triggered semaphore\n");
                    break;
                case QMI_WDS_EXTENDED_IP_CONFIG_IND:
                    QC_LOG_INFO(QMIDev,"QMI_WDS_EXTENDED_IP_CONFIG_IND\n");
                    AddToProcessIndList(pDev, QMIDev, MsgId, pDataCopy, dataSize, clientId);
                    up(pReadSem);
                    QC_LOG_INFO(QMIDev,"Triggered semaphore\n");
                    break;
                case QMI_WDS_REVERSE_IP_TRANSPORT_CONNECTION_IND:
                    QC_LOG_INFO(QMIDev,"QMI_WDS_REVERSE_IP_TRANSPORT_CONNECTION_IND\n");
                    AddToProcessIndList(pDev, QMIDev, MsgId, pDataCopy, dataSize, clientId);
                    up(pReadSem);
                    QC_LOG_INFO(QMIDev,"Triggered semaphore\n");
                    break;
                default:
                    QC_LOG_INFO(QMIDev,"Some Ind triggerd : %X\n", MsgId);
                    break;
            }
        } else if (cntlFlg == QMI_RESPONSE_CONTROL_FLAG)
        {
            switch (MsgId)
            {
                case QMI_WDS_GET_RUNTIME_SETTINGS_RESP:
                    QC_LOG_INFO(QMIDev,"QMI_WDS_GET_RUNTIME_SETTINGS_RESP\n");
                    AddToProcessIndList(pDev, QMIDev, MsgId, pDataCopy, dataSize, clientId);
                    up(pReadSem);
                    QC_LOG_INFO(QMIDev,"Triggered semaphore\n");
                    break;
                default:
                    break;
            }
        }
    }
    return;
}

/*===========================================================================
METHOD:
   ReadCallback (Public Method)

DESCRIPTION:
   Put the data in storage and notify anyone waiting for data

PARAMETERS
   pReadURB       [ I ] - URB this callback is run for

RETURN VALUE:
   None
===========================================================================*/
void ReadCallback( struct urb * pReadURB )
{
   sGobiUSBNet * pDev = NULL;
   int result;
   u16 clientID;
   void * pData;
   void * pDataCopy;
   u16 dataSize;
   u16 transactionID;
   int i;
   struct sClientMemList * pClientMemIter;
   if (pReadURB == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pDev), "bad read URB\n" );
      return;
   }
   
   pDev = pReadURB->context;
   
   if (IsDeviceValid( pDev ) == false)
   {
     QC_LOG_ERR(GET_QMIDEV(pDev), "Invalid device!\n" );
      return;
   }   
   if (pReadURB->status != 0)
   {
     QC_LOG_DBG(GET_QMIDEV(pDev), "Read status = %d\n", pReadURB->status );

      // Resubmit the interrupt URB
      ResubmitIntURB( pDev->mQMIDev.mpIntURB );

      return;
   }
  QC_LOG_INFO(GET_QMIDEV(pDev), "Read %d bytes\n", pReadURB->actual_length );
   
   pData = pReadURB->transfer_buffer;
   dataSize = pReadURB->actual_length;

   PrintHex( pData, dataSize );

   result = ParseQMUX( &clientID,
                       pData,
                       dataSize );
   if (result < 0)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev), "Read error parsing QMUX %d\n", result );

      // Resubmit the interrupt URB
      ResubmitIntURB( pDev->mQMIDev.mpIntURB );

      return;
   }
   
   // Grab transaction ID

   // Data large enough?
   if (dataSize < result + 3)
   {
     QC_LOG_ERR(GET_QMIDEV(pDev), "Data buffer too small to parse\n" );

      // Resubmit the interrupt URB
      ResubmitIntURB( pDev->mQMIDev.mpIntURB );

      return;
   }
   
   // Transaction ID size is 1 for QMICTL, 2 for others
   if (clientID == QMICTL)
   {
      transactionID = *(u8*)(pData + result + 1);
   }
   else
   {
      transactionID = *(u16*)(pData + result + 1);
   }

   // Find memory storage for this service and Client ID
   // Not using FindClientMem because it can't handle broadcasts
   list_for_each_entry( pClientMemIter, &pDev->mQMIDev.mClientMemList, node){
      if (pClientMemIter->mClientID == clientID 
      ||  (pClientMemIter->mClientID | 0xff00) == clientID)
      {
         // Make copy of pData
         pDataCopy = kzalloc( dataSize, GFP_ATOMIC );
         memcpy( pDataCopy, pData, dataSize );

    processIndResponses(pDataCopy, pDev, &pDev->mQMIDev, dataSize, clientID);

         if (AddToReadMemList( pDev,
                               pClientMemIter->mClientID,
                               transactionID,
                               pDataCopy,
                               dataSize,
                               &pDev->mQMIDev) == false)
         {
           QC_LOG_ERR(GET_QMIDEV(pDev), "Error allocating pReadMemListEntry read will be discarded\n" );
            kfree( pDataCopy );
            // Resubmit the interrupt URB
            ResubmitIntURB( pDev->mQMIDev.mpIntURB );

            return;
         }

         // Success
        QC_LOG_INFO(GET_QMIDEV(pDev), "Creating new readListEntry for client 0x%x, TID %x\n", clientID, transactionID );

         // Notify this client data exists
        QC_LOG_DBG(GET_QMIDEV(pDev), "Now notify client that data exists\n ");
         NotifyAndPopNotifyList( pDev,
                                 pClientMemIter->mClientID,
                                 transactionID,
                                 &pDev->mQMIDev);

         // Possibly notify poll() that data exists
         wake_up_interruptible( &pClientMemIter->mWaitQueue );

         // Not a broadcast
         if (clientID >> 8 != 0xff)
         {
            break;
         }
      }
   }

   for (i = 0 ; i < MAX_MUX_DEVICES; i++)
   {
      sQMIDev *QMIDev = (sQMIDev *)&pDev->mQMIMUXDev[i];

       // Find memory storage for this service and Client ID
       // Not using FindClientMem because it can't handle broadcasts
   list_for_each_entry( pClientMemIter, &QMIDev->mClientMemList, node){
          if (pClientMemIter->mClientID == clientID 
          ||  (pClientMemIter->mClientID | 0xff00) == clientID)
          {
             // Make copy of pData
             pDataCopy = kzalloc( dataSize, GFP_ATOMIC );
             memcpy( pDataCopy, pData, dataSize );

        processIndResponses(pDataCopy, pDev, QMIDev, dataSize, clientID);

             if (AddToReadMemList( pDev,
                                   pClientMemIter->mClientID,
                                   transactionID,
                                   pDataCopy,
                                   dataSize,
                                   QMIDev) == false)
             {
               QC_LOG_ERR(QMIDev,  "Error allocating pReadMemListEntry. read will be discarded\n" );
                kfree( pDataCopy );

                // Resubmit the interrupt URB
                // ResubmitIntURB( pDev->mQMIDev.mpIntURB );

                return;
             }

             // Success
         QC_LOG_INFO(QMIDev,"Creating new readListEntry for client 0x%x, TID %x\n", clientID, transactionID );

         QC_LOG_DBG(QMIDev, "Now notify client that data exists\n ");
             // Notify this client data exists
             NotifyAndPopNotifyList( pDev,
                                     pClientMemIter->mClientID,
                                     transactionID,
                                     QMIDev);

             // Possibly notify poll() that data exists
             wake_up_interruptible( &pClientMemIter->mWaitQueue );

             // Not a broadcast
             if (clientID >> 8 != 0xff)
             {
                break;
             }
          }
       }  
   }
   
   // Resubmit the interrupt URB
   ResubmitIntURB( pDev->mQMIDev.mpIntURB );

}

/*===========================================================================
METHOD:
   IntCallback (Public Method)

DESCRIPTION:
   Data is available, fire off a read URB

PARAMETERS
   pIntURB       [ I ] - URB this callback is run for

RETURN VALUE:
   None
===========================================================================*/
void IntCallback( struct urb * pIntURB )
{
   int status;
   sGobiUSBNet * pDev = NULL;
   struct usb_cdc_notification *dr;
   if (pIntURB == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pDev), "bad read URB\n" );
      return;
   }
   pDev = (sGobiUSBNet *)pIntURB->context;
   if (IsDeviceValid( pDev ) == false)
   {
     QC_LOG_ERR(GET_QMIDEV(pDev), "Invalid device!\n" );
      return;
   }
   // Verify this was a normal interrupt
   if (pIntURB->status != 0)
   {
     QC_LOG_DBG(GET_QMIDEV(pDev), "Int status = %d\n", pIntURB->status );
      
      // Ignore EOVERFLOW errors
      if (pIntURB->status != -EOVERFLOW)
      {
         // Read 'thread' dies here
         return;
      }
   }
   else
   {

#if 0
 // CDC GET_ENCAPSULATED_RESPONSE
      // Endpoint number is the wIndex value, which is the 5th byte in the
      // CDC GET_ENCAPSULTED_RESPONSE message
      if ((pIntURB->actual_length == 8)
      &&  (*(u64*)pIntURB->transfer_buffer == CDC_GET_ENCAPSULATED_RESPONSE
           + (pDev->mpEndpoints->mIntfNum * 0x100000000ll)))
      {
         // Time to read
         usb_fill_control_urb( pDev->mQMIDev.mpReadURB,
                               pDev->mpNetDev->udev,
                               usb_rcvctrlpipe( pDev->mpNetDev->udev, 0 ),
                               (unsigned char *)pDev->mQMIDev.mpReadSetupPacket,
                               pDev->mQMIDev.mpReadBuffer,
                               DEFAULT_READ_URB_LENGTH,
                               ReadCallback,
                               pDev );
         status = usb_submit_urb( pDev->mQMIDev.mpReadURB, GFP_ATOMIC );
         if (status != 0)
         {
            QC_LOG_ERR(GET_QMIDEV(pDev), "Error submitting Read URB %d\n", status );
         }

         // Int URB will be resubmitted during ReadCallback
         return;
      }
      // CDC CONNECTION_SPEED_CHANGE
      // Endpoint number is the wIndex value, which is the 5th byte in the
      // CDC CONNECTION_SPEED message
      else if ((pIntURB->actual_length == 16)
      &&       (*(u64*)pIntURB->transfer_buffer == CDC_CONNECTION_SPEED_CHANGE
                + (pDev->mpEndpoints->mIntfNum * 0x100000000ll)))
      {
         // if upstream or downstream is 0, stop traffic.  Otherwise resume it
         if ((*(u32*)(pIntURB->transfer_buffer + 8) == 0)
         ||  (*(u32*)(pIntURB->transfer_buffer + 12) == 0))
         {
            GobiSetDownReason( pDev, CDC_CONNECTION_SPEED );
            QC_LOG_DBG(GET_QMIDEV(pDev), "traffic stopping due to CONNECTION_SPEED_CHANGE\n" );
         }
         else
         {
            GobiClearDownReason( pDev, CDC_CONNECTION_SPEED );
            QC_LOG_DBG(GET_QMIDEV(pDev), "resuming traffic due to CONNECTION_SPEED_CHANGE\n" );
         }
      }
      else
      {
         QC_LOG_DBG(GET_QMIDEV(pDev), "ignoring invalid interrupt in packet\n" );
         PrintHex( pIntURB->transfer_buffer, pIntURB->actual_length );
      }
#else

      dr = (struct usb_cdc_notification *)pIntURB->transfer_buffer;

      switch (dr->bNotificationType) {
          case USB_CDC_NOTIFY_RESPONSE_AVAILABLE:
              usb_fill_control_urb( pDev->mQMIDev.mpReadURB,
                      pDev->mpNetDev->udev,
                      usb_rcvctrlpipe( pDev->mpNetDev->udev, 0 ),
                      (unsigned char *)pDev->mQMIDev.mpReadSetupPacket,
                      pDev->mQMIDev.mpReadBuffer,
                      DEFAULT_READ_URB_LENGTH,
                      ReadCallback,
                      pDev );
              status = usb_submit_urb( pDev->mQMIDev.mpReadURB, GFP_ATOMIC );
              if (status != 0)
              {
                 QC_LOG_ERR(GET_QMIDEV(pDev), "Error submitting Read URB %d\n", status );
                  break;
              }
              return;
          case USB_CDC_NOTIFY_SPEED_CHANGE:
              if ((pIntURB->actual_length == 16)
                      &&       (*(u64*)pIntURB->transfer_buffer == CDC_CONNECTION_SPEED_CHANGE
                          + (pDev->mpEndpoints->mIntfNum * 0x100000000ll)))
              {
                  // if upstream or downstream is 0, stop traffic.  Otherwise resume it
                  if ((*(u32*)(pIntURB->transfer_buffer + 8) == 0)
                          ||  (*(u32*)(pIntURB->transfer_buffer + 12) == 0))
                  {
                     GobiSetDownReason( pDev, CDC_CONNECTION_SPEED );
                     QC_LOG_ERR(GET_QMIDEV(pDev), "traffic stopping due to CONNECTION_SPEED_CHANGE\n" );
                  }
                  else
                  {
                     GobiClearDownReason( pDev, CDC_CONNECTION_SPEED );
                     QC_LOG_INFO(GET_QMIDEV(pDev), "resuming traffic due to CONNECTION_SPEED_CHANGE\n" );
                  }
              }
              break;
          default:
             QC_LOG_INFO(GET_QMIDEV(pDev), "ignoring invalid interrupt in packet\n" );
              PrintHex( pIntURB->transfer_buffer, pIntURB->actual_length );
              break;
      }
#endif
   }

   ResubmitIntURB( pIntURB );

   return;
}

/*===========================================================================
METHOD:
   StartRead (Public Method)

DESCRIPTION:
   Start continuous read "thread" (callback driven)

   Note: In case of error, KillRead() should be run
         to remove urbs and clean up memory.
   
PARAMETERS:
   pDev     [ I ] - Device specific memory

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/

static struct usb_endpoint_descriptor *GetEndpoint(
        struct usb_interface *pintf,
        int type,
        int dir )
{
    int i;
    struct usb_host_interface *iface = pintf->cur_altsetting;
    struct usb_endpoint_descriptor *pendp;

    for( i = 0; i < iface->desc.bNumEndpoints; i++)
    {
        pendp = &iface->endpoint[i].desc;
        if( ((pendp->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == dir)
                &&
                (usb_endpoint_type(pendp) == type) )
        {
            return pendp;
        }
    }

    return NULL;
}

int StartRead( sGobiUSBNet * pDev )
{
   int interval;
   struct usb_endpoint_descriptor *pendp;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device!\n" );
      return -ENXIO;
   }
   
   // Allocate URB buffers
   pDev->mQMIDev.mpReadURB = usb_alloc_urb( 0, GFP_KERNEL );
   if (pDev->mQMIDev.mpReadURB == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Error allocating read urb\n" );
      return -ENOMEM;
   }
   
   pDev->mQMIDev.mpIntURB = usb_alloc_urb( 0, GFP_KERNEL );
   if (pDev->mQMIDev.mpIntURB == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Error allocating int urb\n" );
      usb_free_urb( pDev->mQMIDev.mpReadURB );
      pDev->mQMIDev.mpReadURB = NULL;
      return -ENOMEM;
   }

   // Create data buffers
   pDev->mQMIDev.mpReadBuffer = kzalloc( DEFAULT_READ_URB_LENGTH, GFP_KERNEL );
   if (pDev->mQMIDev.mpReadBuffer == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Error allocating read buffer\n" );
      usb_free_urb( pDev->mQMIDev.mpIntURB );
      pDev->mQMIDev.mpIntURB = NULL;
      usb_free_urb( pDev->mQMIDev.mpReadURB );
      pDev->mQMIDev.mpReadURB = NULL;
      return -ENOMEM;
   }
   
   pDev->mQMIDev.mpIntBuffer = kzalloc( DEFAULT_READ_URB_LENGTH, GFP_KERNEL );
   if (pDev->mQMIDev.mpIntBuffer == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Error allocating int buffer\n" );
      kfree( pDev->mQMIDev.mpReadBuffer );
      pDev->mQMIDev.mpReadBuffer = NULL;
      usb_free_urb( pDev->mQMIDev.mpIntURB );
      pDev->mQMIDev.mpIntURB = NULL;
      usb_free_urb( pDev->mQMIDev.mpReadURB );
      pDev->mQMIDev.mpReadURB = NULL;
      return -ENOMEM;
   }      
   
   pDev->mQMIDev.mpReadSetupPacket = kzalloc( sizeof( sURBSetupPacket ), 
                                              GFP_KERNEL );
   if (pDev->mQMIDev.mpReadSetupPacket == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Error allocating setup packet buffer\n" );
      kfree( pDev->mQMIDev.mpIntBuffer );
      pDev->mQMIDev.mpIntBuffer = NULL;
      kfree( pDev->mQMIDev.mpReadBuffer );
      pDev->mQMIDev.mpReadBuffer = NULL;
      usb_free_urb( pDev->mQMIDev.mpIntURB );
      pDev->mQMIDev.mpIntURB = NULL;
      usb_free_urb( pDev->mQMIDev.mpReadURB );
      pDev->mQMIDev.mpReadURB = NULL;
      return -ENOMEM;
   }

   // CDC Get Encapsulated Response packet
   pDev->mQMIDev.mpReadSetupPacket->mRequestType = 0xA1;
   pDev->mQMIDev.mpReadSetupPacket->mRequestCode = 1;
   pDev->mQMIDev.mpReadSetupPacket->mValue = 0;
   pDev->mQMIDev.mpReadSetupPacket->mIndex = pDev->mpEndpoints->mIntfNum;
   pDev->mQMIDev.mpReadSetupPacket->mLength = DEFAULT_READ_URB_LENGTH;

   pendp = GetEndpoint(pDev->mpIntf, USB_ENDPOINT_XFER_INT, USB_DIR_IN);
   if (pendp == NULL)
   {
       QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid interrupt endpoint!\n" );
       kfree(pDev->mQMIDev.mpReadSetupPacket);
       pDev->mQMIDev.mpReadSetupPacket = NULL;
       kfree( pDev->mQMIDev.mpIntBuffer );
       pDev->mQMIDev.mpIntBuffer = NULL;
       kfree( pDev->mQMIDev.mpReadBuffer );
       pDev->mQMIDev.mpReadBuffer = NULL;
       usb_free_urb( pDev->mQMIDev.mpIntURB );
       pDev->mQMIDev.mpIntURB = NULL;
       usb_free_urb( pDev->mQMIDev.mpReadURB );
       pDev->mQMIDev.mpReadURB = NULL;
       return -ENXIO;
   }

   interval = 9;
   //interval = USB_SPEED_SUPER;
   //printk("Max value of intervel :%d\n", pendp->bInterval);
   // Interval needs reset after every URB completion
   //interval = max((int)(pendp->bInterval),
   //        (pDev->mpNetDev->udev->speed == USB_SPEED_HIGH) ? 7 : 3);

   
   QC_LOG_DBG(GET_QMIDEV(pDev),"StartRead submitted URB for read!\n");
   
   // Schedule interrupt URB
   usb_fill_int_urb( pDev->mQMIDev.mpIntURB,
                     pDev->mpNetDev->udev,
                     usb_rcvintpipe( pDev->mpNetDev->udev,
                                     pDev->mpEndpoints->mIntInEndp ),
                     pDev->mQMIDev.mpIntBuffer,
                     //8,
                     pDev->mpEndpoints->mIntInEndpMaxPacketSize,
                     IntCallback,
                     pDev,
                     interval );
   return usb_submit_urb( pDev->mQMIDev.mpIntURB, GFP_KERNEL );
}

/*===========================================================================
METHOD:
   KillRead (Public Method)

DESCRIPTION:
   Kill continuous read "thread"
   
PARAMETERS:
   pDev     [ I ] - Device specific memory

RETURN VALUE:
   None
===========================================================================*/
void KillRead( sGobiUSBNet * pDev )
{
   if (IsDeviceValid(pDev) == false)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device!\n" );
      return;
   }
   // Stop reading
   if (pDev->mQMIDev.mpReadURB != NULL)
   {
      QC_LOG_INFO(GET_QMIDEV(pDev),"Killng read URB\n" );
      usb_kill_urb( pDev->mQMIDev.mpReadURB );
   }

   if (pDev->mQMIDev.mpIntURB != NULL)
   {
      QC_LOG_INFO(GET_QMIDEV(pDev), "Killng int URB\n" );
      usb_kill_urb( pDev->mQMIDev.mpIntURB );
   }

   // Release buffers
   kfree( pDev->mQMIDev.mpReadSetupPacket );
   pDev->mQMIDev.mpReadSetupPacket = NULL;
   kfree( pDev->mQMIDev.mpReadBuffer );
   pDev->mQMIDev.mpReadBuffer = NULL;
   kfree( pDev->mQMIDev.mpIntBuffer );
   pDev->mQMIDev.mpIntBuffer = NULL;
   
   // Release URB's
   usb_free_urb( pDev->mQMIDev.mpReadURB );
   pDev->mQMIDev.mpReadURB = NULL;
   usb_free_urb( pDev->mQMIDev.mpIntURB );
   pDev->mQMIDev.mpIntURB = NULL;
}

/*=========================================================================*/
// Internal read/write functions
/*=========================================================================*/

/*===========================================================================
METHOD:
   ReadAsync (Public Method)

DESCRIPTION:
   Start asynchronous read
   NOTE: Reading client's data store, not device

PARAMETERS:
   pDev              [ I ] - Device specific memory
   clientID          [ I ] - Requester's client ID
   transactionID     [ I ] - Transaction ID or 0 for any
   pCallback         [ I ] - Callback to be executed when data is available
   pData             [ I ] - Data buffer that willl be passed (unmodified) 
                             to callback

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int ReadAsync(
   sGobiUSBNet *      pDev,
   u16                clientID,
   u16                transactionID,
   void               (*pCallback)(sGobiUSBNet*, u16, void *, sQMIDev *),
   void *             pData,
   sQMIDev *          QMIDev)
{
   sClientMemList * pClientMem;
   sReadMemList * ppReadMemList;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device!\n" );
      return -ENXIO;
   }
   QC_LOG_DBG(QMIDev, "<QMIDevice> Start\n");

   // Find memory storage for this client ID
   pClientMem = FindClientMem( pDev, clientID, QMIDev );
   if (pClientMem == NULL)
   {
      QC_LOG_ERR(QMIDev,"Could not find matching client ID 0x%x\n", clientID );
      return -ENXIO;
   }
   list_for_each_entry(ppReadMemList,&pClientMem->mList,node){
      if (transactionID == 0 
      ||  transactionID == (ppReadMemList)->mTransactionID)
      {
         // Run our own callback
         pCallback( pDev, clientID, pData, QMIDev );
         
         return 0;
      }
   }
   // Data not found, add ourself to list of waiters
   if (AddToNotifyList( pDev,
                        clientID,
                        transactionID, 
                        pCallback, 
                        pData,
                        QMIDev) == false)
   {
      QC_LOG_ERR(QMIDev,"Unable to register for notification\n" );
   }

   QC_LOG_DBG(QMIDev, "<QMIDevice> End!!\n");
   // Success
   return 0;
}

/*===========================================================================
METHOD:
   UpSem (Public Method)

DESCRIPTION:
   Notification function for synchronous read

PARAMETERS:
   pDev              [ I ] - Device specific memory
   clientID          [ I ] - Requester's client ID
   pData             [ I ] - Buffer that holds semaphore to be up()-ed

RETURN VALUE:
   None
===========================================================================*/
void UpSem( 
   sGobiUSBNet * pDev,
   u16             clientID,
   void *          pData,
   sQMIDev * QMIDev)
{
   if (IsDeviceValid(pDev) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device!\n" );
      return;
   }
   QC_LOG_DBG(QMIDev, "clientID: 0x%x\n", clientID );
   up( (struct semaphore *)pData );
   return;
}

/*===========================================================================
METHOD:
   ReadSync (Public Method)

DESCRIPTION:
   Start synchronous read
   NOTE: Reading client's data store, not device

PARAMETERS:
   pDev              [ I ] - Device specific memory
   ppOutBuffer       [I/O] - On success, will be filled with a 
                             pointer to read buffer
   clientID          [ I ] - Requester's client ID
   transactionID     [ I ] - Transaction ID or 0 for any

RETURN VALUE:
   int - size of data read for success
         negative errno for failure
===========================================================================*/
int ReadSync(
   sGobiUSBNet *    pDev,
   void **            ppOutBuffer,
   u16                clientID,
   u16                transactionID,
   sQMIDev *              QMIDev )
{
   int result;
   sClientMemList * pClientMem;
   sNotifyList * ppNotifyList;
   sNotifyList * pNotifyListSafe = NULL;
   struct semaphore readSem;
   void * pData;
   unsigned long flags;
   u16 dataSize;


   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device!\n" );
      return -ENXIO;
   }

   QC_LOG_DBG(QMIDev, "<QMIDevice> Inside \n");   

   // Find memory storage for this Client ID
   pClientMem = FindClientMem( pDev, clientID, QMIDev );
   if (pClientMem == NULL)
   {
      QC_LOG_ERR(QMIDev, "Could not find matching client ID 0x%x\n", clientID );
      return -ENXIO;
   }
   
   // Note: in cases where read is interrupted, 
   //    this will verify client is still valid
   while (PopFromReadMemList( pDev,
                              clientID,
                              transactionID,
                              &pData,
                              &dataSize,
                              QMIDev) == false)
   {
      // Data does not yet exist, wait
      QC_LOG_ERR(QMIDev, "<QMIDevice> PopFromReadMemList() Failed. Data doesn't exist yet\n");
      sema_init( &readSem, 0 );

      // Add ourself to list of waiters
      if (AddToNotifyList( pDev, 
                           clientID, 
                           transactionID, 
                           UpSem, 
                           &readSem,
                           QMIDev) == false)
      {
         QC_LOG_ERR(QMIDev, "unable to register for notification\n");
         return -EFAULT;
      }

      // Wait for notification
      QC_LOG_INFO(QMIDev, "down_interruptible: Now wait for notification...\n");
      result = down_interruptible( &readSem );
      if (result != 0)
      {
         QC_LOG_INFO(QMIDev, "Interrupted %d\n", result );

         // readSem will fall out of scope, 
         // remove from notify list so it's not referenced
/* Minimized the long hold of spin lock, for better concurrency and responsiveness of the system */
         spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
         list_for_each_entry_safe(ppNotifyList,pNotifyListSafe,&pClientMem->mReadNotifyList,node)
         {
            if (ppNotifyList->mpData == &readSem)
            {
               list_del(&ppNotifyList->node);
               kfree(ppNotifyList);
               QC_LOG_INFO(QMIDev, "Removed from notify list\n");
               break;
            }
         }
         spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
         return -EINTR;
      }
      
      // Verify device is still valid
      if (IsDeviceValid( pDev ) == false)
      {
         QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device!\n" );
         return -ENXIO;
      }
   }

   // Success
   *ppOutBuffer = pData;

   QC_LOG_INFO(QMIDev, "<QMIDevice> return dataSize successfully = %d\n",dataSize);
   return dataSize;
}

/*===========================================================================
METHOD:
   ReadSyncTimeout (Public Method)

DESCRIPTION:
   Start synchronous read
   NOTE: Reading client's data store, not device

PARAMETERS:
   pDev              [ I ] - Device specific memory
   ppOutBuffer       [I/O] - On success, will be filled with a 
                             pointer to read buffer
   clientID          [ I ] - Requester's client ID
   transactionID     [ I ] - Transaction ID or 0 for any

RETURN VALUE:
   int - size of data read for success
         negative errno for failure
===========================================================================*/
int ReadSyncTimeout(
   sGobiUSBNet *    pDev,
   void **            ppOutBuffer,
   u16                clientID,
   u16                transactionID,
   sQMIDev *              QMIDev )
{
   int result;
   sClientMemList * pClientMem;
   sNotifyList * ppNotifyList;
   sNotifyList * pNotifyListSafe = NULL;
   struct semaphore readSem;
   void * pData;
   unsigned long flags;
   u16 dataSize;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device!\n" );
      return -ENXIO;
   }
   QC_LOG_DBG(QMIDev, "<QMIDevice> Inside \n");

   // Find memory storage for this Client ID
   pClientMem = FindClientMem( pDev, clientID, QMIDev );
   if (pClientMem == NULL)
   {
      QC_LOG_ERR(QMIDev,"Could not find matching client ID 0x%x\n", clientID );
      return -ENXIO;
   }

   // Note: in cases where read is interrupted, 
   //    this will verify client is still valid
   while (PopFromReadMemList( pDev,
                              clientID,
                              transactionID,
                              &pData,
                              &dataSize,
                              QMIDev) == false)
   {
      QC_LOG_WARN(QMIDev, "<QMIDevice> PopFromReadMemList() Failed. Data doesn't exist yet\n");
      sema_init( &readSem, 0 );

      // Add ourself to list of waiters
      if (AddToNotifyList( pDev, 
                           clientID, 
                           transactionID, 
                           UpSem, 
                           &readSem,
                           QMIDev) == false)
      {
         QC_LOG_WARN(QMIDev,"unable to register for notification\n");
         return -EFAULT;
      }

      if (use_down_timeout != 0)
      {
          // Wait for notification
          QC_LOG_INFO(QMIDev, "down_timeout: Now wait for notification...\n");
          result = down_timeout( &readSem, READ_TIMEOUT);
      }
      else
      {
          // Wait for notification
          result = down_interruptible( &readSem );
      }

      
      if (result != 0)
      {
         // readSem will fall out of scope, 
         // remove from notify list so it's not referenced
/* Minimized the long hold of spin lock, for better concurrency and responsiveness of the system */
         spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
         list_for_each_entry_safe(ppNotifyList,pNotifyListSafe,&pClientMem->mReadNotifyList,node)
         {
            if (ppNotifyList->mpData == &readSem)
            {
               list_del(&ppNotifyList->node);
               kfree(ppNotifyList);
               QC_LOG_INFO(QMIDev, "removed from notify list\n");
               break;
            }
         }
         spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
         if (use_down_timeout != 0)         
            return -ETIME;         
         else         
            return -EINTR;
      }
      
      // Verify device is still valid
      if (IsDeviceValid( pDev ) == false)
      {
         QC_LOG_ERR(QMIDev,"Invalid device!\n" );
         return -ENXIO;
      }
   }

   // Success
   *ppOutBuffer = pData;
   
   QC_LOG_INFO(QMIDev,"<QMIDevice> return dataSize successfully = %d\n",dataSize);

   return dataSize;
}

/*===========================================================================
METHOD:
   WriteSyncCallback (Public Method)

DESCRIPTION:
   Write callback

PARAMETERS
   pWriteURB       [ I ] - URB this callback is run for

RETURN VALUE:
   None
===========================================================================*/
void WriteSyncCallback( struct urb * pWriteURB )
{
   if (pWriteURB == NULL)
   {
     QC_LOG_GLOBAL( "null urb\n" );
      return;
   }

  QC_LOG_GLOBAL( "Write status/size %d/%d\n", pWriteURB->status, pWriteURB->actual_length );

   // Notify that write has completed by up()-ing semeaphore
   up( (struct semaphore * )pWriteURB->context );
   
   return;
}

/*===========================================================================
METHOD:
   WriteSync (Public Method)

DESCRIPTION:
   Start synchronous write

PARAMETERS:
   pDev                 [ I ] - Device specific memory
   pWriteBuffer         [ I ] - Data to be written
   writeBufferSize      [ I ] - Size of data to be written
   clientID             [ I ] - Client ID of requester

RETURN VALUE:
   int - write size (includes QMUX)
         negative errno for failure
===========================================================================*/
int WriteSync(
   sGobiUSBNet *          pDev,
   char *                 pWriteBuffer,
   int                    writeBufferSize,
   u16                    clientID,
   sQMIDev *              QMIDev)
{
   int result;
   struct semaphore writeSem;
   struct urb * pWriteURB;
   //sURBSetupPacket writeSetup;
   sURBSetupPacket *writeSetup;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device!\n" );
      return -ENXIO;
   }
   
   
   pWriteURB = usb_alloc_urb( 0, GFP_KERNEL );
   if (pWriteURB == NULL)
   {
      QC_LOG_ERR(QMIDev,"URB mem error\n" );
      return -ENOMEM;
   }

   // Fill writeBuffer with QMUX
   result = FillQMUX( clientID, pWriteBuffer, writeBufferSize );
   if (result < 0)
   {
      usb_free_urb( pWriteURB );
      return result;
   }

   writeSetup = kzalloc(sizeof(sURBSetupPacket), GFP_KERNEL);
   if (writeSetup == NULL)
   {
      QC_LOG_ERR(QMIDev,"writeSetup mem error\n" );
      return -ENOMEM;
   }

   // CDC Send Encapsulated Request packet
   writeSetup->mRequestType = 0x21;
   writeSetup->mRequestCode = 0;
   writeSetup->mValue = 0;
   writeSetup->mIndex = pDev->mpEndpoints->mIntfNum;
   writeSetup->mLength = writeBufferSize;

   // Create URB   
   usb_fill_control_urb( pWriteURB,
                         pDev->mpNetDev->udev,
                         usb_sndctrlpipe( pDev->mpNetDev->udev, 0 ),
                         (unsigned char *)writeSetup,
                         (void*)pWriteBuffer,
                         writeBufferSize,
                         NULL,
                         pDev );

   QC_LOG_INFO(QMIDev,"Write size %d\n", writeSetup->mLength );
   PrintHex( pWriteBuffer, writeBufferSize );

   sema_init( &writeSem, 0 );
   
   pWriteURB->complete = WriteSyncCallback;
   pWriteURB->context = &writeSem;
   
   // Wake device
   //QC_LOG_GLOBAL( "<QMIDevice> Device Wakeup\n" );
   result = usb_autopm_get_interface( pDev->mpIntf );
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"unable to resume interface: %d\n", result );
      
      // Likely caused by device going from autosuspend -> full suspend
      if (result == -EPERM)
      {
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,33 ))
         pDev->mpNetDev->udev->auto_pm = 0;
#endif
         GobiSuspend( pDev->mpIntf, PMSG_SUSPEND );
      }

      usb_free_urb( pWriteURB );
      kfree(writeSetup);
      
      return result;
   }

   if (AddToURBList( pDev, clientID, pWriteURB, QMIDev ) == false)
   {

      usb_autopm_put_interface( pDev->mpIntf );
      usb_free_urb( pWriteURB );   
      kfree(writeSetup);

      return -EINVAL;
   }

   result = usb_submit_urb( pWriteURB, GFP_KERNEL );
   if (result < 0)
   {
      QC_LOG_DBG(QMIDev,"submit URB error %d\n", result );
      
      // Get URB back so we can destroy it
      if (PopFromURBList( pDev, clientID, QMIDev ) != pWriteURB)
      {
         // This shouldn't happen
         QC_LOG_EXCEPTION(QMIDev,"Didn't get write URB back\n" );
         
      }
      else
      {
         usb_free_urb( pWriteURB );
      }
      
      usb_autopm_put_interface( pDev->mpIntf );
      kfree(writeSetup);

      return result;
   }

   if (use_down_timeout != 0)
   {
       // Allow user interrupts
       result = down_timeout( &writeSem, WRITE_TIMEOUT);
   }
   else
   {
      // Wait for write to finish
      if (interruptible != 0)
      {
         // Allow user interrupts
         result = down_interruptible( &writeSem );
      }
      else
      {
         // Ignore user interrupts
         result = 0;
         down( &writeSem );
      }
   }

   // Write is done, release device
   usb_autopm_put_interface( pDev->mpIntf );
   // Verify device is still valid
   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device!\n" );

      kfree(writeSetup);
      return -ENXIO;
   }

   // Get URB back so we can destroy it
   if (PopFromURBList( pDev, clientID, QMIDev ) != pWriteURB)
   {
      // This shouldn't happen
      QC_LOG_EXCEPTION(QMIDev, "Didn't get write URB back\n" );
      // Add URB back in URBlist
      if (pWriteURB != NULL)
         AddToURBList( pDev, clientID, pWriteURB, QMIDev );
      kfree(writeSetup);
      return -EINVAL;
   }

   if (result == 0)
   {
      // Write is finished
      if (pWriteURB->status == 0)
      {
         // Return number of bytes that were supposed to have been written,
         //   not size of QMI request
         result = writeBufferSize;
      }
      else
      {
         QC_LOG_ERR(QMIDev,"bad status = %d\n", pWriteURB->status );
         
         // Return error value
         result = pWriteURB->status;
      }
   }
   else
   {
      // We have been forcibly interrupted
      QC_LOG_ERR(QMIDev, "Interrupted %d !!!\n", result );
      QC_LOG_ERR(QMIDev, "Device may be in bad state and need reset !!!\n" );

      // URB has not finished
      usb_kill_urb( pWriteURB );
   }

   usb_free_urb( pWriteURB );
   kfree(writeSetup);
   return result;
}

/*=========================================================================*/
// Internal memory management functions
/*=========================================================================*/

/*===========================================================================
METHOD:
   GetClientID (Public Method)

DESCRIPTION:
   Request a QMI client for the input service type and initialize memory
   structure

PARAMETERS:
   pDev           [ I ] - Device specific memory
   serviceType    [ I ] - Desired QMI service type

RETURN VALUE:
   int - Client ID for success (positive)
         Negative errno for error
===========================================================================*/
int GetClientID( 
   sGobiUSBNet *      pDev,
   u8                 serviceType,
   sQMIDev *          QMIDev 
   )
{
   u16 clientID;
   sClientMemList * ppNewEntryClientMem;
   int result;
   void * pWriteBuffer;
   u16 writeBufferSize;
   void * pReadBuffer;
   u16 readBufferSize;
   unsigned long flags;
   u8 transactionID;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev, "Invalid device!\n" );
      return -ENXIO;
   }

   // Run QMI request to be asigned a Client ID
   if (serviceType != 0)
   {
      writeBufferSize = QMICTLGetClientIDReqSize();
      pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
      if (pWriteBuffer == NULL)
      {
         return -ENOMEM;
      }

      transactionID = atomic_add_return( 1, &pDev->mQMIDev.mQMICTLTransactionID );
      if (transactionID == 0)
      {
         transactionID = atomic_add_return( 1, &pDev->mQMIDev.mQMICTLTransactionID );
      }
      result = QMICTLGetClientIDReq( pWriteBuffer, 
                                     writeBufferSize,
                                     transactionID,
                                     serviceType );

      QC_LOG_INFO(QMIDev,"QMICTLGetClientIDReq result = %d\n", result);
     

      if (result < 0)
      {
         kfree( pWriteBuffer );
         return result;
      }

      result = WriteSync( pDev,
                          pWriteBuffer,
                          writeBufferSize,
                          QMICTL,
                          &pDev->mQMIDev);
      kfree( pWriteBuffer );

      if (result < 0)
      {
         return result;
      }

      result = ReadSyncTimeout( pDev,
                         &pReadBuffer,
                         QMICTL,
                         transactionID,
                         &pDev->mQMIDev);
      if (result < 0)
      {
         QC_LOG_ERR(QMIDev, "bad read data %d\n", result );
         return result;
      }
      readBufferSize = result;

      result = QMICTLGetClientIDResp( pReadBuffer,
                                      readBufferSize,
                                      &clientID);
      kfree( pReadBuffer );

      if (result < 0)
      {
         return result;
      }
   }
   else
   {
      // QMI CTL will always have client ID 0
      clientID = 0;
   }

   // Verify client is not already allocated
   if (FindClientMem( pDev, clientID, QMIDev ) != NULL)
   {
      QC_LOG_ERR(QMIDev, "Client memory already exists\n" );
      return -ETOOMANYREFS;
   }

   // Create locations for read to place data into
   ppNewEntryClientMem = kzalloc( sizeof( sClientMemList ), GFP_ATOMIC );
   if (ppNewEntryClientMem == NULL)
   {
      QC_LOG_ERR(QMIDev, "Error allocating read list\n" );
      return -ENOMEM;
   }
      
   (ppNewEntryClientMem)->mClientID = clientID;
   (ppNewEntryClientMem)->mpList = NULL;
   (ppNewEntryClientMem)->mpReadNotifyList = NULL;
   (ppNewEntryClientMem)->mpURBList = NULL;
   INIT_LIST_HEAD(&ppNewEntryClientMem->mList);
   INIT_LIST_HEAD(&ppNewEntryClientMem->mReadNotifyList);
   INIT_LIST_HEAD(&ppNewEntryClientMem->mURBList);
   spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
   list_add_tail(&ppNewEntryClientMem->node, &QMIDev->mClientMemList);
   spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
   // kref_init ref_count
   // Initialize workqueue for poll()
   init_waitqueue_head( &(ppNewEntryClientMem)->mWaitQueue );
   
   QC_LOG_DBG(QMIDev,"<QMIDevice> OUT!\n");
   return clientID;
}

/*===========================================================================
METHOD:
   ReleaseClientID (Public Method)

DESCRIPTION:
   Release QMI client and free memory

PARAMETERS:
   pDev           [ I ] - Device specific memory
   clientID       [ I ] - Requester's client ID

RETURN VALUE:
   None
===========================================================================*/
void ReleaseClientID(
   sGobiUSBNet *    pDev,
   u16                clientID,
   sQMIDev *QMIDev)
{
   int result;
   sClientMemList * pClientMemIter, * pClientMemSafe = NULL;
   struct urb * pDelURB;
   void * pDelData;
   u16 dataSize;
   void * pWriteBuffer;
   u16 writeBufferSize;
   void * pReadBuffer;
   u16 readBufferSize;
   unsigned long flags;
   u8 transactionID;

   // Is device is still valid?
   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"invalid device\n" );
      return;
   }
   
   QC_LOG_INFO(QMIDev,"releasing CID: 0x%x\n", clientID );

   // Run QMI ReleaseClientID if this isn't QMICTL   
   if (clientID != QMICTL)
   {
      // Note: all errors are non fatal, as we always want to delete 
      //    client memory in latter part of function
      
      writeBufferSize = QMICTLReleaseClientIDReqSize();
      pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
      if (pWriteBuffer == NULL)
      {
         QC_LOG_ERR(QMIDev, "memory error\n" );
      }
      else
      {
         transactionID = atomic_add_return( 1, &pDev->mQMIDev.mQMICTLTransactionID );
         if (transactionID == 0)
         {
            transactionID = atomic_add_return( 1, &pDev->mQMIDev.mQMICTLTransactionID );
         }
         result = QMICTLReleaseClientIDReq( pWriteBuffer, 
                                            writeBufferSize,
                                            transactionID,
                                            clientID );
         if (result < 0)
         {
            kfree( pWriteBuffer );
            QC_LOG_ERR(QMIDev,"error %d filling req buffer\n", result );
         }
         else
         {
            result = WriteSync( pDev,
                                pWriteBuffer,
                                writeBufferSize,
                                QMICTL,
                                &pDev->mQMIDev);
            kfree( pWriteBuffer );

            if (result < 0)
            {
               QC_LOG_ERR(QMIDev,"bad write status %d\n", result );
            }
            else
            {
               result = ReadSyncTimeout( pDev,
                                  &pReadBuffer,
                                  QMICTL,
                                  transactionID,
                                  &pDev->mQMIDev);
               if (result < 0)
               {
                  QC_LOG_ERR(QMIDev,"bad read status %d\n", result );
               }
               else
               {
                  readBufferSize = result;
                  result = QMICTLReleaseClientIDResp( pReadBuffer,
                                                      readBufferSize );
                  kfree( pReadBuffer );

                  if (result < 0)
                  {
                     QC_LOG_ERR(QMIDev, "error %d parsing response\n", result );
                  }
               }
            }
         }
      }
   }

   // Cleaning up client memory
   //Always use list_for_each_entry_safe for performing list_del operations
   spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
   list_for_each_entry_safe( pClientMemIter,pClientMemSafe, &QMIDev->mClientMemList, node){
      if ((pClientMemIter)->mClientID == clientID)
      {
         spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
         // Notify all clients
         while (NotifyAndPopNotifyList( pDev,
                                        clientID,
                                        0,
                                        QMIDev) == true );         

         // Kill and free all URB's
         pDelURB = PopFromURBList( pDev, clientID, QMIDev );
         while (pDelURB != NULL)
         {
            usb_kill_urb( pDelURB );
            usb_free_urb( pDelURB );
            pDelURB = PopFromURBList( pDev, clientID, QMIDev );
         }

         // Free any unread data
         while (PopFromReadMemList( pDev, 
                                    clientID,
                                    0,
                                    &pDelData,
                                    &dataSize, 
                                    QMIDev ) == true )
         {
            kfree( pDelData );
         }

         // Delete client Mem
         spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
         list_del(&pClientMemIter->node);
         spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
         kfree(pClientMemIter);
      }
   }

   QC_LOG_DBG(QMIDev,"<QMIDevice> End!!\n");
   return;
}

/*===========================================================================
METHOD:
   FindClientMem (Public Method)

DESCRIPTION:
   Find this client's memory

   Caller MUST have lock on mClientMemLock

PARAMETERS:
   pDev           [ I ] - Device specific memory
   clientID       [ I ] - Requester's client ID

RETURN VALUE:
   sClientMemList - Pointer to requested sClientMemList for success
                    NULL for error
===========================================================================*/
sClientMemList * FindClientMem( 
   sGobiUSBNet *      pDev,
   u16              clientID,
   sQMIDev *QMIDev)
{
   sClientMemList * pClientMemIter;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device\n" );
      return NULL;
   }
   
   if(list_empty(&QMIDev->mClientMemList)){
      return NULL;
   } 
   list_for_each_entry( pClientMemIter, &QMIDev->mClientMemList, node){
      if (pClientMemIter->mClientID == clientID)
      {
         // Success
         QC_LOG_INFO(QMIDev,"Found client mem 0x%px wrt clientID = 0x%x\n",pClientMemIter, clientID);
         return pClientMemIter;
      }
   }

   QC_LOG_WARN(QMIDev,"Could not find client mem wrt ClientID = 0x%x\n",clientID);
   return NULL;
}

/*===========================================================================
METHOD:
   AddToReadMemList (Public Method)

DESCRIPTION:
   Add Data to this client's ReadMem list
   
   Caller MUST have lock on mClientMemLock

PARAMETERS:
   pDev           [ I ] - Device specific memory
   clientID       [ I ] - Requester's client ID
   transactionID  [ I ] - Transaction ID or 0 for any
   pData          [ I ] - Data to add
   dataSize       [ I ] - Size of data to add

RETURN VALUE:
   bool
===========================================================================*/
bool AddToReadMemList( 
   sGobiUSBNet *      pDev,
   u16              clientID,
   u16              transactionID,
   void *           pData,
   u16              dataSize,
   sQMIDev *          QMIDev)
{
   sReadMemList * ppThisReadMemList = NULL;
   sClientMemList * pClientMem = NULL;
   unsigned long flags;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device\n" );
      return false;
   }

   QC_LOG_DBG(QMIDev,"<QMIDevice> In\n");
   // Get this client's memory location
   spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
   pClientMem = FindClientMem( pDev, clientID, QMIDev );
   if (pClientMem == NULL)
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_WARN(QMIDev,"Could not find this client's memory wrt clientID 0x%x Exit!!\n", clientID );
      return false;
   }

   ppThisReadMemList = kzalloc( sizeof( sReadMemList ), GFP_ATOMIC);
   if (ppThisReadMemList == NULL)
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_ERR(QMIDev,"Mem error\n" );
      return false;
   }   

   (ppThisReadMemList)->mpData = pData;
   (ppThisReadMemList)->mDataSize = dataSize;
   (ppThisReadMemList)->mTransactionID = transactionID;
   list_add_tail(&ppThisReadMemList->node, &pClientMem->mList);
   spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
   QC_LOG_DBG(QMIDev, "<QMIDevice> End!\n");
   return true;
}

/*===========================================================================
METHOD:
   PopFromReadMemList (Public Method)

DESCRIPTION:
   Remove data from this client's ReadMem list if it matches 
   the specified transaction ID.
   
   Caller MUST have lock on mClientMemLock

PARAMETERS:
   pDev              [ I ] - Device specific memory
   clientID          [ I ] - Requester's client ID
   transactionID     [ I ] - Transaction ID or 0 for any
   ppData            [I/O] - On success, will be filled with a 
                             pointer to read buffer
   pDataSize         [I/O] - On succces, will be filled with the 
                             read buffer's size

RETURN VALUE:
   bool
===========================================================================*/
bool PopFromReadMemList( 
   sGobiUSBNet *      pDev,
   u16                  clientID,
   u16                  transactionID,
   void **              ppData,
   u16 *                pDataSize,
   sQMIDev *          QMIDev)
{
   sReadMemList *pReadMemListSafe = NULL;
   sReadMemList *pDelReadMemList = NULL;
   sReadMemList *ppReadMemList = NULL;
   sClientMemList *pClientMem = NULL;
   unsigned long flags;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device\n" );
      return false;
   }

   // Get this client's memory location
   spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
   pClientMem = FindClientMem( pDev, clientID, QMIDev );
   if (pClientMem == NULL)
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_ERR(QMIDev,"Could not find this client's memory wrt clientID 0x%x Exit!!\n", clientID );
      return false;
   }
   
   pDelReadMemList = NULL;
   list_for_each_entry_safe(ppReadMemList, pReadMemListSafe, &pClientMem->mList, node){

      if (transactionID == 0
      ||  transactionID == ppReadMemList->mTransactionID )
      {
         pDelReadMemList = ppReadMemList;
         break;
      }
      QC_LOG_DBG(QMIDev,"skipping 0x%x data TID = 0x%x\n", clientID, ppReadMemList->mTransactionID);
   }

   if (pDelReadMemList != NULL)
   {
      // Copy to output
      *ppData = pDelReadMemList->mpData;
      *pDataSize = pDelReadMemList->mDataSize;
      QC_LOG_DBG(QMIDev,"Deleting ReadMemListNode 0x%px of TID = 0x%x\n", pDelReadMemList, pDelReadMemList->mTransactionID);
      // Free memory
      list_del(&pDelReadMemList->node);
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      kfree(pDelReadMemList);
      return true;
   }
   else
   {
      QC_LOG_WARN(QMIDev,"No read memory to pop, ClientID 0x%x, TID = 0x%x\n",clientID, transactionID);
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      return false;
   }

   QC_LOG_DBG(QMIDev," OUT!!\n");
}

/*===========================================================================
METHOD:
   AddToNotifyList (Public Method)

DESCRIPTION:
   Add Notify entry to this client's notify List
   
   Caller MUST have lock on mClientMemLock

PARAMETERS:
   pDev              [ I ] - Device specific memory
   clientID          [ I ] - Requester's client ID
   transactionID     [ I ] - Transaction ID or 0 for any
   pNotifyFunct      [ I ] - Callback function to be run when data is available
   pData             [ I ] - Data buffer that willl be passed (unmodified) 
                             to callback

RETURN VALUE:
   bool
===========================================================================*/
bool AddToNotifyList( 
   sGobiUSBNet *      pDev,
   u16                  clientID,
   u16                  transactionID,
   void                 (* pNotifyFunct)(sGobiUSBNet *, u16, void *, sQMIDev *),
   void *               pData,
   sQMIDev *QMIDev)
{
   sNotifyList * ppThisNotifyList;
   sClientMemList * pClientMem;
   unsigned long flags;
   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device\n" );
      return false;
   }

   // Get this client's memory location
   spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
   pClientMem = FindClientMem( pDev, clientID, QMIDev );
   if (pClientMem == NULL)
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_ERR(QMIDev,"Could not find this client's memory wrt clientID 0x%x Exit!!\n", clientID );
      return false;
   }
   
   ppThisNotifyList = kzalloc( sizeof( sNotifyList ), GFP_ATOMIC);
   if (ppThisNotifyList == NULL)
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_ERR(QMIDev, "Mem error\n" );
      return false;
   }   
   
   (ppThisNotifyList)->mpNotifyFunct = pNotifyFunct;
   (ppThisNotifyList)->mpData = pData;
   (ppThisNotifyList)->mTransactionID = transactionID;
   list_add_tail(&ppThisNotifyList->node, &pClientMem->mReadNotifyList);
   spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
   return true;
}

/*===========================================================================
METHOD:
   NotifyAndPopNotifyList (Public Method)

DESCRIPTION:
   Remove first Notify entry from this client's notify list 
   and Run function
   
   Caller MUST have lock on mClientMemLock

PARAMETERS:
   pDev              [ I ] - Device specific memory
   clientID          [ I ] - Requester's client ID
   transactionID     [ I ] - Transaction ID or 0 for any

RETURN VALUE:
   bool
===========================================================================*/
bool NotifyAndPopNotifyList( 
   sGobiUSBNet *        pDev,
   u16                  clientID,
   u16                  transactionID,
   sQMIDev *QMIDev)
{
   sNotifyList * ppNotifyListSafe = NULL;
   sNotifyList * pDelNotifyList = NULL;
   sClientMemList * pClientMem = NULL;
   sNotifyList * ppNotifyList = NULL;
   unsigned long flags;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device\n" );
      return false;
   }

   // Get this client's memory location
   spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
   pClientMem = FindClientMem( pDev, clientID, QMIDev );
   if (pClientMem == NULL)
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_ERR(QMIDev,"Could not find this client's memory wrt clientID 0x%x, Exit!!\n", clientID);
      return false;
   }

   pDelNotifyList = NULL;
   if(!list_empty(&pClientMem->mReadNotifyList)){
   list_for_each_entry_safe(ppNotifyList, ppNotifyListSafe, &pClientMem->mReadNotifyList,node){
      if (transactionID == 0
      ||  (ppNotifyList)->mTransactionID == 0
      ||  transactionID == (ppNotifyList)->mTransactionID)
      {
         pDelNotifyList = ppNotifyList;
         break;
      }
      
      QC_LOG_INFO(QMIDev, "skipping data TID = 0x%x\n", (ppNotifyList)->mTransactionID );
   }
   }

   if (pDelNotifyList != NULL)
   {
      if (pDelNotifyList->mpNotifyFunct != NULL)
      {
         void (*notifyFunct)(struct sGobiUSBNet *, u16, void *, struct sQMIDev *) = pDelNotifyList->mpNotifyFunct;
         void *notifyData = pDelNotifyList->mpData;
         QC_LOG_WARN(QMIDev,"Deleting NotifyList node: 0x%px  of TID = 0x%x\n", &pDelNotifyList->node, pDelNotifyList->mTransactionID);
         list_del(&pDelNotifyList->node);
         spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
         if (notifyFunct != NULL)
         {
            notifyFunct( pDev, clientID, notifyData, QMIDev );
         }
         kfree(pDelNotifyList);
      }
      
      return true;
   }
   else
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_WARN(QMIDev,"No one to notify for Client 0x%x, TID = 0x%x\n",clientID, transactionID);
      return false;
   }
}

/*===========================================================================
METHOD:
   AddToURBList (Public Method)

DESCRIPTION:
   Add URB to this client's URB list
   
   Caller MUST have lock on mClientMemLock

PARAMETERS:
   pDev              [ I ] - Device specific memory
   clientID          [ I ] - Requester's client ID
   pURB              [ I ] - URB to be added

RETURN VALUE:
   bool
===========================================================================*/
bool AddToURBList( 
   sGobiUSBNet *      pDev,
   u16                  clientID,
   struct urb *     pURB,
   sQMIDev *          QMIDev)
{
   sClientMemList * pClientMem;
   sURBList * ppThisURBList;
   unsigned long flags;
   
   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device\n" );
      return false;
   }

   // Get this client's memory location
   spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
   pClientMem = FindClientMem( pDev, clientID, QMIDev );
   if (pClientMem == NULL)
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_ERR(QMIDev,"Could not find this client's memory wrt clientID 0x%x Exit!!\n", clientID );
      return false;
   }
   ppThisURBList = kzalloc( sizeof( sURBList ), GFP_ATOMIC);
   if (ppThisURBList == NULL)
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_ERR(QMIDev,"Mem error\n" );
      return false;
   }
   
   (ppThisURBList)->mpURB = pURB;
   list_add_tail(&ppThisURBList->node, &pClientMem->mURBList);
   spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
   QC_LOG_DBG(QMIDev, "Out!\n");
   return true;
}

/*===========================================================================
METHOD:
   PopFromURBList (Public Method)

DESCRIPTION:
   Remove URB from this client's URB list
   
   Caller MUST have lock on mClientMemLock

PARAMETERS:
   pDev           [ I ] - Device specific memory
   clientID       [ I ] - Requester's client ID

RETURN VALUE:
   struct urb - Pointer to requested client's URB
                NULL for error
===========================================================================*/
struct urb * PopFromURBList( 
   sGobiUSBNet *      pDev,
   u16                  clientID,
   sQMIDev *          QMIDev )
{
   sClientMemList * pClientMem;
   sURBList * pURBListEntry, *pURBListEntrySafe = NULL;
   unsigned long flags;
   struct urb * pURB;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device\n" );
      return NULL;
   }

   // Get this client's memory location
   spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
   pClientMem = FindClientMem( pDev, clientID, QMIDev );
   if (pClientMem == NULL)
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_ERR(QMIDev, "Could not find this client's memory wrt clientID 0x%x Exit!!\n", clientID );
      return NULL;
   }

   if(!list_empty(&pClientMem->mURBList)){
      list_for_each_entry_safe( pURBListEntry,pURBListEntrySafe, &pClientMem->mURBList, node){
         pURB = pURBListEntry->mpURB;
         list_del(&pURBListEntry->node);
         spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
         kfree(pURBListEntry); // Free URB list after list_del. Mandatory step
         QC_LOG_WARN(QMIDev, "return pURB and deleted pURBListEntry\n");
         return pURB;
      }
   } 
   else
   {
      spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );
      QC_LOG_WARN(QMIDev, "No URB's to pop for client's memory wrt clientID 0x%x Exit!!\n", clientID);
   }
   
   return NULL;
}

/*=========================================================================*/
// Userspace wrappers
/*=========================================================================*/

/*===========================================================================
METHOD:
   UserspaceOpen (Public Method)

DESCRIPTION:
   Userspace open
      IOCTL must be called before reads or writes

PARAMETERS
   pInode       [ I ] - kernel file descriptor
   pFilp        [ I ] - userspace file descriptor

RETURN VALUE:
   int - 0 for success
         Negative errno for failure
===========================================================================*/
int UserspaceOpen( 
   struct inode *         pInode, 
   struct file *          pFilp )
{
   sQMIFilpStorage *pFilpData = NULL;
   unsigned short   MuxId;
   sGobiUSBNet * pDev = NULL;
   sQMIDev * pQMIDev = NULL;

   if (pInode == NULL)
   {
     QC_LOG_ERR(pQMIDev, "Invalid inode\n" );
      return -ENXIO;;
   }
   // Optain device pointer from pInode
   pQMIDev = container_of( pInode->i_cdev,
                                     sQMIDev,
                                     mCdev );
   
   // Initialized to "pDev->mQMIDev.MuxId = 0x81;" 
   MuxId = pQMIDev->MuxId - 0x81;

   if (MuxId == 0x00)
   {
       pDev = container_of( pQMIDev, sGobiUSBNet, mQMIDev );
       if (memcmp(&pInode->i_cdev->dev, &pDev->mQMIDev.mDevNum, sizeof(dev_t)) != 0)
       {
          QC_LOG_ERR(pQMIDev, "Invalid device\n" );
           return -ENXIO;
       }
   }
   else if (MuxId <= MAX_MUX_DEVICES)
   {
       pDev = container_of( pQMIDev, sGobiUSBNet, mQMIMUXDev[MuxId - 1]);
       if (memcmp(&pInode->i_cdev->dev, &pDev->mQMIMUXDev[MuxId - 1].mDevNum, sizeof(dev_t)) != 0)
       {
           QC_LOG_ERR(pQMIDev, "Invalid device\n" );
           return -ENXIO;
       }
   }
   else
   {
      QC_LOG_ERR(pQMIDev, "Invalid device\n" );
      return -ENXIO;
   }

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(pQMIDev, "Invalid device\n" );
      return -ENXIO;
   }

#if 0 //redundent logic
   if (memcmp(&pInode->i_cdev->dev, &pDev->mQMIDev.mDevNum, sizeof(dev_t)) == 0)
   {
      pQMIDev = &pDev->mQMIDev;
   }
   else
   {
      for (i=0;i<MAX_MUX_DEVICES;i++)
      {
          if (memcmp(&pInode->i_cdev->dev, &pDev->mQMIMUXDev[i].mDevNum, sizeof(dev_t)) == 0)
          {
             pQMIDev = &pDev->mQMIMUXDev[i];
          }
      }
   }
#endif

   // Setup data in pFilp->private_data
   pFilp->private_data = kzalloc( sizeof( sQMIFilpStorage ), GFP_KERNEL );
   if (pFilp->private_data == NULL)
   {
     QC_LOG_ERR(pQMIDev, "Mem error\n" );
      return -ENOMEM;
   }

   pFilpData = (sQMIFilpStorage *)pFilp->private_data;
   pFilpData->mClientID = (u16)-1;
   pFilpData->mpDev = pDev;
   pFilpData->QMIDev = pQMIDev;

   QC_LOG_DBG(pQMIDev,"<QMIDevice> Out!!\n");

   return 0;
}

/*===========================================================================
METHOD:
   UserspaceIOCTL (Public Method)

DESCRIPTION:
   Userspace IOCTL functions

PARAMETERS
   pUnusedInode [ I ] - (unused) kernel file descriptor
   pFilp        [ I ] - userspace file descriptor
   cmd          [ I ] - IOCTL command
   arg          [ I ] - IOCTL argument

RETURN VALUE:
   int - 0 for success
         Negative errno for failure
===========================================================================*/
int UserspaceIOCTL( 
   struct inode *    pUnusedInode, 
   struct file *     pFilp,
   unsigned int      cmd, 
   unsigned long     arg )
{
   sQMIFilpStorage *pFilpData = NULL;
   int result;
   u32 devVIDPID;
   if (pFilp == NULL)
   {
      QC_LOG_GLOBAL( "Invalid file pointer!\n" );
      return -EBADF;
   }

   pFilpData = (sQMIFilpStorage *)pFilp->private_data;
   if (pFilpData == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Bad file data\n" );
      return -EBADF;
   }
   
   if (IsDeviceValid( pFilpData->mpDev ) == false)
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Invalid device! Updating f_ops\n" );
      pFilp->f_op = file_inode(pFilp)->i_fop;
      return -ENXIO;
   }
#if 0
    if (pFilpData->mpDev->mbQMIReadyStatus == false)
    {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Device is not ready\n" );
       return -ENXIO;
    }
#endif

   switch (cmd)
   {
      case IOCTL_QMI_GET_SERVICE_FILE:
      
        QC_LOG_INFO(GET_QMIDEV_QMIFILP(pFilpData), "<QMIDevice> Setting up QMI for service %lu\n", arg );
         if ((u8)arg == 0)
         {
           QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Cannot use QMICTL from userspace\n" );
            return -EINVAL;
         }
         
         if (pFilpData->mpDev->mbQMIReadyStatus == false)
         {
            QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Device QMI is not ready\n" );
             return -ENXIO;
         }

         // Connection is already setup
         if (pFilpData->mClientID != (u16)-1)
         {
            QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"<QMIDevice> Close the current connection before opening a new one\n" );
            return -EBADR;
         }
         
         result = GetClientID( pFilpData->mpDev, (u8)arg, pFilpData->QMIDev );
         
    if (result < 0)
         {
            return result;
         }
         pFilpData->mClientID = result;

         //QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData), "<QMIDevice> Exit IOCTL_QMI_GET_SERVICE_FILE \n");

         return 0;
         break;


      case IOCTL_QMI_GET_DEVICE_VIDPID:
         //QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData), "<QMIDevice> IOCTL_QMI_GET_DEVICE_VIDPID returned\n");
         if (arg == 0)
         {
           QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Bad VIDPID buffer\n" );
            return -EINVAL;
         }
         
         // Extra verification
         if (pFilpData->mpDev->mpNetDev == 0)
         {
           QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Bad mpNetDev\n" );
            return -ENOMEM;
         }
         if (pFilpData->mpDev->mpNetDev->udev == 0)
         {
           QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Bad udev\n" );
            return -ENOMEM;
         }

         devVIDPID = ((le16_to_cpu( pFilpData->mpDev->mpNetDev->udev->descriptor.idVendor ) << 16)
                     + le16_to_cpu( pFilpData->mpDev->mpNetDev->udev->descriptor.idProduct ) );

         result = copy_to_user( (unsigned int *)arg, &devVIDPID, 4 );
         if (result != 0)
         {
           QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Copy to userspace failure %d\n", result );
         }

         //QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"<QMIDevice> Exit IOCTL_QMI_GET_DEVICE_VIDPID: result = %d\n",result);

         return result;
                 
         break;

      case IOCTL_QMI_GET_DEVICE_MEID:
         //QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"<QMIDevice> Inside IOCTL_QMI_GET_DEVICE_MEID \n");
         if (arg == 0)
         {
           QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Bad MEID buffer\n" );
            return -EINVAL;
         }
         
         result = copy_to_user( (unsigned int *)arg, &pFilpData->mpDev->mMEID[0], 14 );
         if (result != 0)
         {
           QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Copy to userspace failure %d\n", result );
         }

         //QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"<QMIDevice> Exit IOCTL_QMI_GET_DEVICE_MEID \n");
         return result;
                 
         break;
      
      case IOCTL_QMIREADY: 
     // QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"File:QMIDevice.c Inside IOCTL_QMIREADY \n");
#if 0
          if (QMIReady( pFilpData->mpDev, 50000 ) == false)
          {
          QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Device unresponsive to QMI\n" );
          return -ETIMEDOUT;
          }
#else
         if (pFilpData->mpDev->mbQMIReadyStatus == false)
         {
            QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Device QMI is not ready\n" );
             return -ENXIO;
         }
#endif
   //QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"<QMIDevice> Exit IOCTL_QMIREADY \n");
    return 1;
    break;

      case IOCTL_QMI_BIND_MUX_ID:
         {
             void * pWriteBuffer;
             u16 writeBufferSize;
             void * pReadBuffer;
             int result;
             int id;

#if 0
             id = GetClientID( pFilpData->mpDev, QMIWDS, &pFilpData->mpDev->mQMIDev );
             //id = GetClientID( pFilpData->mpDev, (u8)QMIWDS, pFilpData->QMIDev );
             if (id < 0)
             {
                QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Error getting client id\n");
                 return id;
             }
#endif
            //QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"<QMIDevice> Inside IOCTL_QMI_BIND_MUX_ID \n");
             id = pFilpData->mClientID;

             // negotiate MUX ID as QMIDev->MuxId
             writeBufferSize = QMUXHeaderSize() + 29;
             pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
             if (pWriteBuffer == NULL)
             {
                 QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Mem alloc failed for bind mux port\n");
                 return -ENOMEM;
             }

             result = QMIWDSBindMuxPortReq( pWriteBuffer,
                     //writeBufferSize, GetTransactionID(pFilpData->QMIDev),
                     writeBufferSize, GetTransactionID(&pFilpData->mpDev->mQMIDev),
                     //writeBufferSize, 0,
                     pFilpData->mpDev, pFilpData->QMIDev);
             if (result < 0)
             {
                QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Request setup failed for bind mux port\n");
                 kfree( pWriteBuffer );
                 return result;
             }

             result = WriteSync( pFilpData->mpDev,
                     pWriteBuffer,
                     writeBufferSize,
                     pFilpData->mClientID,
                     pFilpData->QMIDev);
             kfree( pWriteBuffer );
             if (result < 0)
             {
                QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Bind mux port Failed\n");
                 return result;
             }

             result = ReadSync( pFilpData->mpDev, 
                     &pReadBuffer,
                     pFilpData->mClientID,
                     //pFilpData->mpDev->mQMIDev.mQMITransactionID,
                     0,
                     pFilpData->QMIDev); 
             if (result < 0)
             {
                 QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"ReadSync failed for bind mux port\n");
                 return result;
             }
             kfree(pReadBuffer);
             QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"Success in binding\n");
         }
         

    return 1;

    break;

         
      default:
         return -EBADRQC;       
   }

}

/*===========================================================================
METHOD:
   UserspaceClose (Public Method)

DESCRIPTION:
   Userspace close
      Release client ID and free memory

PARAMETERS
   pFilp           [ I ] - userspace file descriptor
   unusedFileTable [ I ] - (unused) file table

RETURN VALUE:
   int - 0 for success
         Negative errno for failure
===========================================================================*/
int UserspaceClose(
   struct file *       pFilp,
   fl_owner_t          unusedFileTable )
{
   struct task_struct * pEachTask = NULL;
   sQMIFilpStorage *pFilpData = NULL;
   struct fdtable * pFDT;
   int count = 0;
   int used = 0;
   if (pFilp == NULL)
   {
      QC_LOG_GLOBAL("bad file data\n" );
      return -EBADF;
   }
   pFilpData = (sQMIFilpStorage *)pFilp->private_data;

   mutex_trylock(&IoMutex);
   if (pFilpData == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "bad file data\n");
      mutex_unlock(&IoMutex);
      return -EBADF;
   }
   // Fallthough.  If f_count == 1 no need to do more checks
   // if (atomic_read( (const atomic_t*)&pFilp->f_count ) != 1)
   if (file_count(pFilp) != 1)
   {
      rcu_read_lock();
      for_each_process( pEachTask )
      {
         if (pEachTask == NULL || pEachTask->files == NULL)
         {
            // Some tasks may not have files (e.g. Xsession)
            continue;
         }
         pFDT = files_fdtable( pEachTask->files );
         for (count = 0; count < pFDT->max_fds; count++)
         {
            // Before this function was called, this file was removed
            // from our task's file table so if we find it in a file
            // table then it is being used by another task
            if (pFDT->fd[count] == pFilp)
            {
               used++;
               break;
            }
         }
      }
      rcu_read_unlock();
      
      if (used > 0)
      {
         QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "not closing, as this FD is open by %d other process\n", used );
         mutex_unlock(&IoMutex);
         return 0;
      }
   }

   if (IsDeviceValid( pFilpData->mpDev ) == false)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Invalid device! Updating f_ops\n" );
      pFilp->f_op = file_inode(pFilp)->i_fop;
      mutex_unlock(&IoMutex);
      return -ENXIO;
   }
   
  QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"CID: 0x%x\n", pFilpData->mClientID );
   
   // Disable pFilpData so they can't keep sending read or write 
   //    should this function hang
   // Note: memory pointer is still saved in pFilpData to be deleted later
   pFilp->private_data = NULL;

   if (pFilpData->mClientID != (u16)-1)
   {
      ReleaseClientID( pFilpData->mpDev,
                       pFilpData->mClientID,
                       pFilpData->QMIDev);
   }
   
   QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"<QMIDevice>, QUT!!");
   
   kfree( pFilpData );
   pFilpData = NULL;

   mutex_unlock(&IoMutex);
   return 0;
}

/*===========================================================================
METHOD:
   UserspaceRead (Public Method)

DESCRIPTION:
   Userspace read (synchronous)

PARAMETERS
   pFilp           [ I ] - userspace file descriptor
   pBuf            [ I ] - read buffer
   size            [ I ] - size of read buffer
   pUnusedFpos     [ I ] - (unused) file position

RETURN VALUE:
   ssize_t - Number of bytes read for success
             Negative errno for failure
===========================================================================*/
ssize_t UserspaceRead( 
   struct file *          pFilp,
   char __user *          pBuf, 
   size_t                 size,
   loff_t *               pUnusedFpos )
{
   sQMIFilpStorage *pFilpData = NULL;
   int result;
   void * pReadData = NULL;
   void * pSmallReadData;

   if (pFilp == NULL)
   {
      QC_LOG_GLOBAL("Bad file data\n" );
      return -EBADF;
   }
   pFilpData = (sQMIFilpStorage *)pFilp->private_data;
   if (pFilpData == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Bad file data\n" );
      return -EBADF;
   }
   if (IsDeviceValid( pFilpData->mpDev ) == false)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Invalid device! Updating f_ops\n" );
      pFilp->f_op = file_inode(pFilp)->i_fop;
      return -ENXIO;
   }

   if (pFilpData->mpDev->mbQMIReadyStatus == false)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Device QMI is not ready\n" );
      return -ENXIO;
   }
   
   if (pFilpData->mClientID == (u16)-1)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Client ID must be set before reading 0x%x\n", pFilpData->mClientID );
      return -EBADR;
   }

  QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"xxx Getting into ReadSync \n" );
   
   // Perform synchronous read
   result = ReadSync( pFilpData->mpDev,
                      &pReadData,
                      pFilpData->mClientID,
                      0,
                      pFilpData->QMIDev);
   if (result <= 0)
   {
      return result;
   }
   
   //processIndResponses(pReadData, pFilpData->mpDev, pFilpData->QMIDev, result);

  QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData), "xxx Out of ReadSync, result = %d \n", result);
   
   // Discard QMUX header
   result -= QMUXHeaderSize();
   pSmallReadData = pReadData + QMUXHeaderSize();

   if (result > size)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Read data is too large for amount user has requested\n" );
      kfree( pReadData );
      return -EOVERFLOW;
   }

   if (copy_to_user( pBuf, pSmallReadData, result ) != 0)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Error copying read data to user\n" );
      result = -EFAULT;
   }
   
   // Reader is responsible for freeing read buffer
   kfree( pReadData );
   return result;
}

/*===========================================================================
METHOD:
   UserspaceWrite (Public Method)

DESCRIPTION:
   Userspace write (synchronous)

PARAMETERS
   pFilp           [ I ] - userspace file descriptor
   pBuf            [ I ] - write buffer
   size            [ I ] - size of write buffer
   pUnusedFpos     [ I ] - (unused) file position

RETURN VALUE:
   ssize_t - Number of bytes read for success
             Negative errno for failure
===========================================================================*/
ssize_t UserspaceWrite(
   struct file *        pFilp, 
   const char __user *  pBuf, 
   size_t               size,
   loff_t *             pUnusedFpos )
{
   sQMIFilpStorage *pFilpData = NULL;
   int status;
   void * pWriteBuffer;

   if (pFilp == NULL)
   {
      QC_LOG_GLOBAL("Bad file\n" );
      return -EBADF;
   }
   pFilpData = (sQMIFilpStorage *)pFilp->private_data;
   
   if (pFilpData == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Bad file data\n" );
      return -EBADF;
   }
   if (IsDeviceValid( pFilpData->mpDev ) == false)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Invalid device! Updating f_ops\n" );
      pFilp->f_op = file_inode(pFilp)->i_fop;
      return -ENXIO;
   }

   if (pFilpData->mpDev->mbQMIReadyStatus == false)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Device QMI is not ready\n" );
      return -ENXIO;
   }
   
   if (pFilpData->mClientID == (u16)-1)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Client ID must be set before writing 0x%x\n", pFilpData->mClientID );
      return -EBADR;
   }
   
   // Copy data from user to kernel space
   pWriteBuffer = kzalloc( size + QMUXHeaderSize(), GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
      return -ENOMEM;
   }
   status = copy_from_user( pWriteBuffer + QMUXHeaderSize(), pBuf, size );
   if (status != 0)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Unable to copy data from userspace %d\n", status );
      kfree( pWriteBuffer );
      return status;
   }

   status = WriteSync( pFilpData->mpDev,
                       pWriteBuffer, 
                       size + QMUXHeaderSize(),
                       pFilpData->mClientID,
                       pFilpData->QMIDev);

   kfree( pWriteBuffer );
   
   // On success, return requested size, not full QMI reqest size
   if (status == size + QMUXHeaderSize())
   {
      QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"<QMIDevice>, size = %ld\n",size);
      return size;
   }
   else
   {
      QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"<QMIDevice>, status = %d\n",status);
      return status;
   }
}

/*===========================================================================
METHOD:
   UserspacePoll (Public Method)

DESCRIPTION:
   Used to determine if read/write operations are possible without blocking

PARAMETERS
   pFilp              [ I ] - userspace file descriptor
   pPollTable         [I/O] - Wait object to notify the kernel when data 
                              is ready

RETURN VALUE:
   unsigned int - bitmask of what operations can be done imediatly
===========================================================================*/
unsigned int UserspacePoll(
   struct file *                  pFilp,
   struct poll_table_struct *     pPollTable )
{
   unsigned long status = POLLOUT | POLLWRNORM;
   sQMIFilpStorage *pFilpData = NULL;
   sClientMemList * pClientMem = NULL;
   unsigned long flags;
   if (pFilp == NULL)
   {
      QC_LOG_GLOBAL("Bad file\n" );
      return -EBADF;
   }
   pFilpData = (sQMIFilpStorage *)pFilp->private_data;
   //QC_LOG_GLOBAL("<QMIDevice> Starts..\n");
   // Always ready to write

   if (pFilpData == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Bad file data\n" );
      return POLLERR;
   }
   if (IsDeviceValid( pFilpData->mpDev ) == false)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Invalid device! Updating f_ops\n" );
      pFilp->f_op = file_inode(pFilp)->i_fop;
      return POLLERR;
   }

   if (pFilpData->mpDev->mbQMIReadyStatus == false)
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Device QMI is not ready\n" );
      return -ENXIO;
   }

   if (pFilpData->mClientID == (u16)-1)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Client ID must be set before polling 0x%x\n", pFilpData->mClientID );
      return POLLERR;
   }

   // Critical section
   spin_lock_irqsave( &pFilpData->QMIDev->mClientMemLock, flags );

   // Get this client's memory location
   pClientMem = FindClientMem( pFilpData->mpDev, 
                               pFilpData->mClientID, pFilpData->QMIDev );
   if (pClientMem == NULL)
   {
      spin_unlock_irqrestore( &pFilpData->QMIDev->mClientMemLock, flags );
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Could not find this client's memory wrt clientID 0x%x Exit!!\n", pFilpData->mClientID );
      return POLLERR;
   }
   
   poll_wait( pFilp, &pClientMem->mWaitQueue, pPollTable );

   if (!list_empty(&pClientMem->mList))
   {
      status |= POLLIN | POLLRDNORM;
   }

   // End critical section
   spin_unlock_irqrestore( &pFilpData->QMIDev->mClientMemLock, flags );

   // Always ready to write 
   return (status | POLLOUT | POLLWRNORM);
}

/*===========================================================================
METHOD:
   WriteAsyncCallback (Public Method)

DESCRIPTION:
   Write callback

PARAMETERS
   pWriteURB       [ I ] - URB this callback is run for

RETURN VALUE:
   None
===========================================================================*/
void WriteAsyncCallback( struct urb * pWriteURB )
{
    sIoData *pAioDataCtx;
    sGobiUSBNet *pDev = NULL;
    sQMIFilpStorage * pFilpData = NULL;

    if (pWriteURB == NULL)
    {
       QC_LOG_ERR(GET_QMIDEV(pDev), "null urb\n" );
       return;
    }
   QC_LOG_DBG(GET_QMIDEV(pDev), "Write status/size %d/%d\n", pWriteURB->status, pWriteURB->actual_length );

    pAioDataCtx = pWriteURB->context;
    pDev = pAioDataCtx->pDev;
    pFilpData = pAioDataCtx->kiocb->ki_filp->private_data;
    
    // Write is done, release device
    usb_autopm_put_interface( pDev->mpIntf );

    // Verify device is still valid
    if (IsDeviceValid( pDev ) == false)
    {
       QC_LOG_ERR(GET_QMIDEV(pDev), "Invalid device!\n" );
        kfree(pWriteURB->transfer_buffer);
        usb_free_urb( pWriteURB );

      #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
         pAioDataCtx->kiocb->ki_complete(pAioDataCtx->kiocb, 0, -EINVAL);
      #else
         pAioDataCtx->kiocb->ki_complete(pAioDataCtx->kiocb, -EINVAL);
      #endif

        return; 
    }

    // Get URB back so we can destroy it
    if (PopFromURBList( pDev, pFilpData->mClientID, pFilpData->QMIDev ) != pWriteURB)
    {
        // This shouldn't happen
       QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Didn't get write URB back\n" );
        // Add URB back in URBlist
        if (pWriteURB != NULL)
            AddToURBList( pDev, pFilpData->mClientID, pWriteURB, pFilpData->QMIDev );
      #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
         pAioDataCtx->kiocb->ki_complete(pAioDataCtx->kiocb, 0, -EINVAL);
      #else
         pAioDataCtx->kiocb->ki_complete(pAioDataCtx->kiocb, 0);
      #endif

        return;
    }

   QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData), "Actual Write:\n" );
   #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
      pAioDataCtx->kiocb->ki_complete(pAioDataCtx->kiocb, (pWriteURB->actual_length - QMUXHeaderSize()), pWriteURB->status);
   #else
      pAioDataCtx->kiocb->ki_complete(pAioDataCtx->kiocb, (pWriteURB->actual_length - QMUXHeaderSize()));
   #endif

    kfree(pWriteURB->transfer_buffer);
    usb_free_urb( pWriteURB );
   QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData),"success in writing\n");

    return;
}

static void aio_cancel_worker(struct work_struct *work)
{
    sQMIFilpStorage * pFilpData = NULL;
    struct qtidev_aio_data *io_data = container_of(work, struct qtidev_aio_data,
            cancellation_work);

    pFilpData = io_data->kiocb->ki_filp->private_data;

    usb_kill_urb(io_data->urb);

   #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
      io_data->kiocb->ki_complete(io_data->kiocb, -1, -ETIMEDOUT);
   #else
      io_data->kiocb->ki_complete(io_data->kiocb, -1);
   #endif

    return;
}

static int aio_write_cancel(struct kiocb *iocb)
{
    int value = 0;
    struct qtidev_aio_data *io_data;

    io_data = iocb->private;

   QC_LOG_GLOBAL("Inside cancel \n");

    INIT_WORK(&io_data->cancellation_work, aio_cancel_worker);
    queue_work(io_data->pDev->mpWorkQ, &io_data->cancellation_work);
    value = -EINPROGRESS;

    return value;
}

/*===========================================================================
METHOD:
   WriteAsync (Public Method)

DESCRIPTION:
   Start Asynchronous write

PARAMETERS:
   pDev                 [ I ] - Device specific memory
   pWriteBuffer         [ I ] - Data to be written
   writeBufferSize      [ I ] - Size of data to be written
   clientID             [ I ] - Client ID of requester

RETURN VALUE:
   int - write size (includes QMUX)
         negative errno for failure
===========================================================================*/
int WriteAsync(
        sGobiUSBNet *          pDev,
        char *                 pWriteBuffer,
        int                    writeBufferSize,
        u16                    clientID,
        sQMIDev *              QMIDev,
        sIoData *              pAioDataCtx)
{
    int result;
    struct semaphore writeSem;
    struct urb * pWriteURB;
    //sURBSetupPacket writeSetup;
    sURBSetupPacket *writeSetup;

    if (IsDeviceValid( pDev ) == false)
    {
        QC_LOG_ERR(QMIDev,"Invalid device!\n" );
        return -ENXIO;
    }

    pWriteURB = usb_alloc_urb( 0, GFP_KERNEL );
    if (pWriteURB == NULL)
    {
        QC_LOG_ERR(QMIDev,"URB mem error\n" );
        return -ENOMEM;
    }

    // Fill writeBuffer with QMUX
    result = FillQMUX( clientID, pWriteBuffer, writeBufferSize );
    if (result < 0)
    {
        usb_free_urb( pWriteURB );
        return result;
    }

    writeSetup = kzalloc(sizeof(sURBSetupPacket), GFP_KERNEL);
    if (writeSetup == NULL)
    {
        QC_LOG_ERR(QMIDev,"writeSetup mem error\n" );
        return -ENOMEM;
    }

    // CDC Send Encapsulated Request packet
    writeSetup->mRequestType = 0x21;
    writeSetup->mRequestCode = 0;
    writeSetup->mValue = 0;
    writeSetup->mIndex = pDev->mpEndpoints->mIntfNum;
    writeSetup->mLength = writeBufferSize;

    // Create URB   
    usb_fill_control_urb( pWriteURB,
            pDev->mpNetDev->udev,
            usb_sndctrlpipe( pDev->mpNetDev->udev, 0 ),
            (unsigned char *)writeSetup,
            (void*)pWriteBuffer,
            writeBufferSize,
            WriteAsyncCallback,
            pAioDataCtx );

    QC_LOG_INFO(QMIDev,"Actual Write:\n");
    PrintHex( pWriteBuffer, writeBufferSize );

    sema_init( &writeSem, 0 );

    pWriteURB->complete = WriteAsyncCallback;
    pWriteURB->context = pAioDataCtx;

    // Wake device
    result = usb_autopm_get_interface( pDev->mpIntf );
    if (result < 0)
    {
        QC_LOG_ERR(QMIDev, "unable to resume interface: %d\n", result );

        // Likely caused by device going from autosuspend -> full suspend
        if (result == -EPERM)
        {
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,33 ))
            pDev->mpNetDev->udev->auto_pm = 0;
#endif
            GobiSuspend( pDev->mpIntf, PMSG_SUSPEND );
        }

        usb_free_urb( pWriteURB );
        kfree(writeSetup);

        return result;
    }

    if (AddToURBList( pDev, clientID, pWriteURB, QMIDev ) == false)
    {
        usb_autopm_put_interface( pDev->mpIntf );
        usb_free_urb( pWriteURB );
        kfree(writeSetup);
        return -EINVAL;
    }

    if (pAioDataCtx->aio)
    {
        kiocb_set_cancel_fn(pAioDataCtx->kiocb, aio_write_cancel);
    }

    result = usb_submit_urb( pWriteURB, GFP_KERNEL );
    if (result < 0)
    {
        QC_LOG_DBG(QMIDev, "submit URB error %d\n", result );

        // Get URB back so we can destroy it
        if (PopFromURBList( pDev, clientID, QMIDev ) != pWriteURB)
        {
            // This shouldn't happen
            QC_LOG_ERR(QMIDev, "Didn't get write URB back\n" );
        }
        else
        {
            usb_free_urb( pWriteURB );
        }

        usb_autopm_put_interface( pDev->mpIntf );
        kfree(writeSetup);
        return result;
    }

    kfree(writeSetup);
    QC_LOG_DBG(QMIDev,"<QMIDevice> Ends with result = %d\n",result);
    result = -EIOCBQUEUED;
    return result;
}

/*===========================================================================
METHOD:
   UserspaceWriteIter (Public Method)

DESCRIPTION:
   Userspace write (Asynchronous)

PARAMETERS
   pFilp           [ I ] - userspace file descriptor
   pBuf            [ I ] - write buffer
   size            [ I ] - size of write buffer
   pUnusedFpos     [ I ] - (unused) file position

RETURN VALUE:
   ssize_t - Number of bytes read for success
             Negative errno for failure
===========================================================================*/
static ssize_t UserspaceWriteIter(struct kiocb *kiocb, struct iov_iter *iov)
{
   int status = 0;
   void * pWriteBuffer;
   struct qtidev_aio_data *aioDataCtx;
   sQMIFilpStorage * pFilpData = NULL;
   int size = 0;
   //QC_LOG_GLOBAL("<QMIDevice> Starts..\n");
   if ((kiocb == NULL) || (kiocb->ki_filp == NULL))
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Bad kiocb data\n" );
      return -EBADF;
   }
   pFilpData = (sQMIFilpStorage *)kiocb->ki_filp->private_data;
  
   if (pFilpData == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Bad file data\n" );
      return -EBADF;
   }
   if (IsDeviceValid( pFilpData->mpDev ) == false)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Invalid device! Updating f_ops\n" );
      kiocb->ki_filp->f_op = file_inode(kiocb->ki_filp)->i_fop;
      return -ENXIO;
   }

   if (pFilpData->mpDev->mbQMIReadyStatus == false)
   {
     QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Device QMI is not ready\n" );
      return -ENXIO;
   }

   if (pFilpData->mClientID == (u16)-1)
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Client ID must be set before writing 0x%x\n", pFilpData->mClientID );
      return -EBADR;
   }

   if(!is_sync_kiocb(kiocb))
   {
       aioDataCtx = kzalloc(sizeof(struct qtidev_aio_data), GFP_KERNEL);
       if (unlikely(!aioDataCtx))
       {
          QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Failed to alloctate memory\n");
          return -ENOMEM;
       }
       aioDataCtx->aio = true;
   } else
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"No need to handle\n");
       return -EINVAL;
   }

   size = iov_iter_count(iov);
   aioDataCtx->read  = false;
   aioDataCtx->kiocb = kiocb;
   aioDataCtx->pDev  = pFilpData->mpDev;

   // Copy data from user to kernel space
   pWriteBuffer = kzalloc( size + QMUXHeaderSize(), GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
       return -ENOMEM;
   }

   if(!copy_from_iter( pWriteBuffer + QMUXHeaderSize(), size, iov ))
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Unable to copy data from userspace\n");
       kfree( pWriteBuffer );
       return -EINVAL; 
   }

   status = WriteAsync( pFilpData->mpDev,
                       pWriteBuffer, 
                       size + QMUXHeaderSize(),
                       pFilpData->mClientID,
                       pFilpData->QMIDev,
                       aioDataCtx);

   // On success, return requested size, not full QMI reqest size
   if (status == size + QMUXHeaderSize())
   {
      return size;
   }
   else
   {
      return status;
   }
}

static void aio_read_copy_worker(struct work_struct *work)
{
    //unsigned long flags;
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,9,16))
    mm_segment_t oldfs = get_fs();
    #endif
    struct kiocb *kiocb;
    sIoData *aioDataCtx = container_of(work, sIoData, submit_work);
    sQMIDev *QMIDev;
    int ret = 0;
    int status = -EINVAL;

    kiocb = aioDataCtx->kiocb;
    QMIDev = &aioDataCtx->pDev->mQMIDev;

    if (aioDataCtx->mpData)
    {
      #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,9,16))
        set_fs(USER_DS);
      #endif
        /* switch to the user context */
         #if (LINUX_VERSION_CODE >= KERNEL_VERSION( 5,8,0 ))
            kthread_use_mm(aioDataCtx->mm);
         #else
            use_mm(aioDataCtx->mm);
         #endif
        //printk("\n\ncopy_iter offset : 0x%X\n\n", (int)aioDataCtx->data.iov_offset);
        ret = copy_to_iter(aioDataCtx->mpData, min((unsigned long)aioDataCtx->mDataLen, iov_iter_count(&aioDataCtx->data)), &aioDataCtx->data);
        //printk("\n\ncopy_iter offset : 0x%X\n\n", (int)aioDataCtx->data.iov_offset);
        /* switch from the user context */
         #if (LINUX_VERSION_CODE >= KERNEL_VERSION( 5,8,0 ))
            kthread_unuse_mm(aioDataCtx->mm);
         #else
            unuse_mm(aioDataCtx->mm);
         #endif
      #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,9,16))
        set_fs(oldfs);
      #endif
        status = 0;
    }

   #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
      kiocb->ki_complete(kiocb, ret, status); //Status = 0, in case of success
   #else
      kiocb->ki_complete(kiocb, ret); // ret: number of bytes not copied
   #endif

    kfree(aioDataCtx);

    return;
}

void QMIAIOReadcallback(
        sGobiUSBNet *    pDev,
        u16                clientID,
        void *             pData,
        sQMIDev *QMIDev)

{
    bool bRet;
    sIoData *aioDataCtx;
    void * pReadBuffer;
    u16 readBufferSize;
    struct kiocb *kiocb;

    if (IsDeviceValid( pDev ) == false)
    {
        QC_LOG_ERR(QMIDev, "Invalid device\n" );
        return;
    }

    aioDataCtx = pData;
    kiocb = aioDataCtx->kiocb;

    bRet = PopFromReadMemList( pDev,
            clientID,
            0,
            &pReadBuffer,
            &readBufferSize,
            QMIDev);

    if (bRet == false)
    {
        aioDataCtx->mpData = NULL;
        aioDataCtx->mDataLen = 0;
        QC_LOG_ERR(QMIDev,"failed to get data\n");
    }
    else
    {
        //int i;
        // Discard QMUX header
        aioDataCtx->mDataLen = readBufferSize - QMUXHeaderSize();
        aioDataCtx->mpData = pReadBuffer + QMUXHeaderSize();

      //for (i = 0; i < readBufferSize; i++)
      //{
      //   printk("0x%X ", *(char *)(pReadBuffer + i));
      //}
      //printk("\n");
        //printk("%s:%d: mm :%p\n", __func__, __LINE__, aioDataCtx->mm);

   }

   INIT_WORK(&aioDataCtx->submit_work, aio_read_copy_worker);
   queue_work(aioDataCtx->pDev->mpWorkQ, &aioDataCtx->submit_work);

    return;
}

static ssize_t processIo(sIoData *aioDataCtx)
{
    ssize_t res;
    sQMIFilpStorage * pFilpData = NULL;
    if ((aioDataCtx == NULL) || (aioDataCtx->kiocb == NULL) || (aioDataCtx->kiocb->ki_filp == NULL))
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Bad sIoData data\n" );
      return -EBADF;
   }
    pFilpData = aioDataCtx->kiocb->ki_filp->private_data;

    res = ReadAsync( pFilpData->mpDev,
            pFilpData->mClientID,
            0,
            QMIAIOReadcallback,
            aioDataCtx,
            pFilpData->QMIDev);
    if (res != 0)
    {
       QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "unable to setup next async read\n" );
    } else
    {
        res = -EIOCBQUEUED;
    }
    return res;
}

static void aio_read_cancel_worker(struct work_struct *work)
{
    unsigned long flags;
    sQMIFilpStorage *pFilpData = NULL;
    sClientMemList * pClientMem;
    sNotifyList * ppNotifyList;//, * pDelNotifyListEntry;
    sNotifyList * pNotifyListSafe = NULL;
    sIoData *io_data = NULL;
    sQMIDev *QMIDev;
    sGobiUSBNet *pDev;
    u16 clientID;
    struct kiocb *kiocb;
    if (work == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Bad work_struct\n" );
      return;
   }
    io_data = container_of(work, sIoData, cancellation_work);
   
    mutex_trylock(&IoMutex);
    if ((io_data == NULL)||(io_data->kiocb==NULL)||(io_data->kiocb->ki_filp == NULL )||(io_data->kiocb->ki_filp->private_data == NULL)) {
       QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "pFilpData <private_data> is NULL\n");
       mutex_unlock(&IoMutex);
       return;
    }

    pFilpData = io_data->kiocb->ki_filp->private_data;
    QMIDev = pFilpData->QMIDev;
    pDev = pFilpData->mpDev;
    clientID = pFilpData->mClientID;
    kiocb = io_data->kiocb;

    // Find memory storage for this Client ID
    pClientMem = FindClientMem( pDev, clientID, QMIDev );
    if (pClientMem == NULL)
    {
       QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Could not find matching client ID 0x%x\n", clientID );
        mutex_unlock(&IoMutex);
        return;
    }
/* Minimized the long hold of spin lock, for better concurrency and responsiveness of the system */
   spin_lock_irqsave( &QMIDev->mClientMemLock, flags );
    list_for_each_entry_safe(ppNotifyList,pNotifyListSafe,&pClientMem->mReadNotifyList,node){
       if (ppNotifyList->mpData == io_data)
        {
            list_del(&ppNotifyList->node);
            break;
        }
    }
    spin_unlock_irqrestore( &QMIDev->mClientMemLock, flags );

   #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
      kiocb->ki_complete(kiocb, 0, -ETIMEDOUT);
   #else
      kiocb->ki_complete(kiocb, 0);
   #endif

    QC_LOG_DBG(GET_QMIDEV_QMIFILP(pFilpData), "Done\n");
    mutex_unlock(&IoMutex);
    return;
}

static int aio_read_cancel(struct kiocb *iocb)
{
    int value = 0;
    struct qtidev_aio_data *io_data;

    io_data = iocb->private;

    INIT_WORK(&io_data->cancellation_work, aio_read_cancel_worker);
    queue_work(io_data->pDev->mpWorkQ, &io_data->cancellation_work);
    value = -EINPROGRESS;

    return value;
}

static ssize_t UserspaceReadIter(struct kiocb *kiocb, struct iov_iter *iov)
{
    ssize_t res = 0;
    struct qtidev_aio_data *aioDataCtx;
    sQMIFilpStorage * pFilpData = NULL;

    aioDataCtx = kzalloc(sizeof(struct qtidev_aio_data), GFP_KERNEL);
    if (unlikely(!aioDataCtx))
    {
       QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Failed to alloctate memory\n");
        return -ENOMEM;
    }

    if(!is_sync_kiocb(kiocb))
    {
        aioDataCtx->aio = true;
    } 
    else
    {
       QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"No need to handle\n");
        aioDataCtx->aio = false;
        return -EINVAL;
    }

    if(aioDataCtx->aio)
    {
        aioDataCtx->to_free = dup_iter(&aioDataCtx->data, iov, GFP_KERNEL);
        if (!aioDataCtx)
        {
           QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Failed to duplicate iter\n");
            kfree(aioDataCtx);
            return -ENOMEM;
        }
    }
    else
    {
        aioDataCtx->data = *iov;
    }
   if ((kiocb == NULL) || (kiocb->ki_filp == NULL) || (kiocb->ki_filp->private_data == NULL))
   {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData),"Bad kiocb structure\n" );
      return -ENXIO;
   }
    pFilpData = kiocb->ki_filp->private_data;

    if ((pFilpData == NULL) || (pFilpData->mpDev == NULL) ||(pFilpData->mpDev->mbQMIReadyStatus == false))
    {
      QC_LOG_ERR(GET_QMIDEV_QMIFILP(pFilpData), "Device QMI is not ready\n" );
       kfree(aioDataCtx->to_free);
       kfree(aioDataCtx);
       return -ENXIO;
    }

    aioDataCtx->read  = true;
    aioDataCtx->kiocb = kiocb;
    aioDataCtx->pDev  = pFilpData->mpDev;
    kiocb->private = aioDataCtx;

    aioDataCtx->mm = current->mm;

    if (aioDataCtx->aio)
    {
        kiocb_set_cancel_fn(kiocb, aio_read_cancel);
    }

    res = processIo(aioDataCtx);   // it calls ReadAsync()
    if (res == -EIOCBQUEUED)
    {
       QC_LOG_GLOBAL("IO message has been queued\n");
        return res;
    }

    if (aioDataCtx->aio)
    {
        kfree(aioDataCtx->to_free);
        kfree(aioDataCtx);
    }
    else
    {
        *iov = aioDataCtx->data;
    }


    return res;
}

/*=========================================================================*/
// Initializer and destructor
/*=========================================================================*/

/*===========================================================================
METHOD:
   RegisterQMIDevice (Public Method)

DESCRIPTION:
   QMI Device initialization function

PARAMETERS:
   pDev     [ I ] - Device specific memory

RETURN VALUE:
   int - 0 for success
         Negative errno for failure
===========================================================================*/
int RegisterQMIDevice( sGobiUSBNet * pDev )
{
   int result;
   int GobiQMIIndex = 0;
   dev_t devno;
   dev_t devMUXno[MAX_MUX_DEVICES];
   char * pDevName = NULL;
   int i;
   static int Major;
   static int Minor;
   struct device *dev_ret;
   if (pDev == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device!\n" );
      return -ENXIO;
   }

   mutex_init(&IoMutex);
   
   QC_LOG_INFO(GET_QMIDEV(pDev)," Inside.\n");
   if (pDev->mQMIDev.mbCdevIsInitialized == true)
   {
      // Should never happen, but always better to check
      QC_LOG_EXCEPTION(GET_QMIDEV(pDev), "device already exists\n" );
      return -EEXIST;
   }

   pDev->mbQMIReadyStatus = false;
   pDev->mbQMIValid = true;

   // Send SetControlLineState request (USB_CDC)
   //   Required for Autoconnect
   result = usb_control_msg( pDev->mpNetDev->udev,
                             usb_sndctrlpipe( pDev->mpNetDev->udev, 0 ),
                             0x22,
                             0x21,
                             1, // DTR present
                             pDev->mpEndpoints->mIntfNum,
                             NULL,
                             0,
                             100 );
   if (result < 0)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Bad SetControlLineState status %d\n", result );
      return result;
   }

   // Set up for QMICTL
   //    (does not send QMI message, just sets up memory)
   result = GetClientID( pDev, QMICTL, &pDev->mQMIDev );
   if (result != 0)
   {
      pDev->mbQMIValid = false;
      return result;
   }
   atomic_set( &pDev->mQMIDev.mQMICTLTransactionID, 1 );
   // Start Async reading
   result = StartRead( pDev );
   if (result != 0)
   {
      pDev->mbQMIValid = false;
      return result;
   }

   pDev->mQMIDev.MuxId = 0x81;
   for (i = 0; i < MAX_MUX_DEVICES; i++)
   {
      pDev->mQMIMUXDev[i].MuxId = pDev->mQMIDev.MuxId + i + 1;
   }

   // Device is not ready for QMI connections right away
   //   Wait up to 3 seconds before moving the polling to thread
   if (QMIReady( pDev, 3500 ) == true)
   {
      //QC_LOG_DBG(GET_QMIDEV(pDev), "Device unresponsive to QMI\n" );
      //return -ETIMEDOUT;
      pDev->mbQMIReadyStatus = true;
      result = ConfigureQMAP(pDev);
      if (result != 0)
      {
          QC_LOG_ERR(GET_QMIDEV(pDev),"ConfigureQMAP failed\n");
          return result;
      }

      // Setup WDS callback
      result = SetupQMIWDSCallback( pDev, &pDev->mQMIDev );
      if (result != 0)
      {
          QC_LOG_ERR(GET_QMIDEV(pDev),"SetupQMIWDSCallback failed\n");
          return result;
      }

      // Fill MEID for device
      result = QMIDMSGetMEID( pDev, &pDev->mQMIDev );
      if (result != 0)
      {
          QC_LOG_ERR(GET_QMIDEV(pDev),"QMIDMSGetMEID failed\n");
          return result;
      }

       /* need to sub rx URBs for the aggregation size negotiated */
       pDev->mpNetDev->rx_urb_size = RX_URB_SIZE;

#ifdef VIRTUAL_USB_CODE
       for (i=0; i<MAX_MUX_DEVICES; i++)
       {
           /* need to sub rx URBs for the aggregation size negotiated */
           pDev->mpNetMUXDev[i]->rx_urb_size = RX_URB_SIZE;
       }
#endif
#ifdef TX_AGGR
       pDev->tx_aggr_ctx.tx_max = pDev->ULAggregationMaxSize;
       pDev->tx_aggr_ctx.tx_max_datagrams = pDev->ULAggregationMaxDatagram;
#endif
   }

   pDev->pGobiWaitThread = kthread_run(GobiWaitForResponses, pDev, "GobiNet/%d-%s", pDev->mpNetDev->udev->bus->busnum, pDev->mpNetDev->udev->devpath);
   if (IS_ERR(pDev->pGobiWaitThread))
   {
       QC_LOG_ERR(GET_QMIDEV(pDev),"Thread creation error : %ld\n", PTR_ERR(pDev->pGobiWaitThread));
       pDev->pGobiWaitThread = NULL;
   }

   //get_task_struct(pGobiWaitThread); // no need to increment counter at this moment
   // allocate and fill devno with numbers
   result = alloc_chrdev_region( &devno, 0, 1+MAX_MUX_DEVICES, "qtiqmi" );
   if (result < 0)
   {
      return result;
   }

   // Create cdev
   cdev_init( &pDev->mQMIDev.mCdev, &UserspaceQMIFops );
   pDev->mQMIDev.mCdev.owner = THIS_MODULE;
   pDev->mQMIDev.mCdev.ops = &UserspaceQMIFops;
   pDev->mQMIDev.mbCdevIsInitialized = true;

   result = cdev_add( &pDev->mQMIDev.mCdev, devno, 1 );
   if (result != 0)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"error adding cdev\n" );
      return result;
   }

   Major = MAJOR(devno);
   Minor = MINOR(devno);
   
   for (i=0;i<MAX_MUX_DEVICES;i++)
   {
       // Create cdev
       cdev_init( &pDev->mQMIMUXDev[i].mCdev, &UserspaceQMIFops );
       pDev->mQMIMUXDev[i].mCdev.owner = THIS_MODULE;
       pDev->mQMIMUXDev[i].mCdev.ops = &UserspaceQMIFops;
       pDev->mQMIMUXDev[i].mbCdevIsInitialized = true;
       devMUXno[i] = MKDEV(Major, Minor + 1 + i);
       result = cdev_add( &pDev->mQMIMUXDev[i].mCdev, devMUXno[i], 1 );
       if (result != 0)
       {
          QC_LOG_ERR(GET_QMIDEV(pDev),"error adding cdev\n" );
          return result;
       }
   }

   // Match interface number (usb#)
   pDevName = strstr( pDev->mpNetDev->net->name, "usb" );
   if (pDevName == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Bad net name: %s\n", pDev->mpNetDev->net->name );
      return -ENXIO;
   }
   pDevName += strlen( "usb" );
   GobiQMIIndex = simple_strtoul( pDevName, NULL, 10 );
   if (GobiQMIIndex < 0)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Bad minor number\n" );
      return -ENXIO;
   }

#ifndef VIRTUAL_USB_CODE
   // need to modify after the virtual usb is created
   GobiQMIIndex = GobiQMIIndex * 4;
#else
   //GobiQMIIndex -= 3;
   GobiQMIIndex = GobiQMIIndex;
#endif

   // Always print this output
   QC_LOG_INFO(GET_QMIDEV(pDev),"creating qtiqmi%d\n",
           GobiQMIIndex );

#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,27 ))
   // kernel 2.6.27 added a new fourth parameter to device_create
   //    void * drvdata : the data to be added to the device for callbacks
   if (IS_ERR(dev_ret = device_create( pDev->mQMIDev.mpDevClass,
                  &pDev->mpIntf->dev, 
                  devno,
                  NULL,
                  "%s:%d-%s:usb%d_%d", 
                  pDev->mDevInfo.mDevInfInfo.mpKey,
                  pDev->mpNetDev->udev->bus->busnum,
                  pDev->mpNetDev->udev->devpath,
                  GobiQMIIndex, 0))){
                     QC_LOG_ERR(GET_QMIDEV(pDev),"GobiNet: Device creation failed :%ld\n",PTR_ERR(dev_ret));
                     class_destroy(pDev->mQMIDev.mpDevClass);
                     unregister_chrdev_region(devno, 1);
                     return PTR_ERR(dev_ret);
                  }
#else
   if (IS_ERR(dev_ret = device_create( pDev->mQMIDev.mpDevClass,
                  &pDev->mpIntf->dev, 
                  devno,
                  "%s:%d-%s:usb%d_%d", 
                  pDev->mDevInfo.mDevInfInfo.mpKey,
                  pDev->mpNetDev->udev->bus->busnum,
                  pDev->mpNetDev->udev->devpath,
                  GobiQMIIndex, 0)))
                  {
                     QC_LOG_ERR(GET_QMIDEV(pDev),"GobiNet: Device creation failed :%ld\n",PTR_ERR(dev_ret));
                     class_destroy(pDev->mQMIDev.mpDevClass);
                     unregister_chrdev_region(devno, 1);
                     return PTR_ERR(dev_ret);
                  }
#endif
   
   pDev->mQMIDev.mDevNum = devno;

   if (pDev) {
      sprintf(pDev->mQMIDev.mdeviceName,"%s:%d-%s:usb%d_%d",pDev->mDevInfo.mDevInfInfo.mpKey,
      pDev->mpNetDev->udev->bus->busnum,
      pDev->mpNetDev->udev->devpath, GobiQMIIndex, 0);
   }

   QC_LOG_INFO(GET_QMIDEV(pDev),"pDev->mQMIDev.mdeviceName = %s\n",pDev->mQMIDev.mdeviceName);

   result = sysfs_entry(pDev, pDev->mQMIDev.mdeviceName);
   if (result != 0) {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Sysfs entry failed\n");
      return result;      
   }

   for (i=0;i<MAX_MUX_DEVICES;i++)
   {
#ifdef VIRTUAL_USB_CODE
      // Match interface number (usb#) extracted from MUXED adapters
      pDevName = strstr( pDev->mpNetMUXDev[i]->net->name, "usb" );
      if (pDevName == NULL)
      {
         QC_LOG_ERR(GET_QMIMUXDEV(pDev,i),"Bad net name: %s\n", pDev->mpNetMUXDev[i]->net->name );
         return -ENXIO;
      }
      pDevName += strlen( "usb" );
      GobiQMIIndex = simple_strtoul( pDevName, NULL, 10 );
      if (GobiQMIIndex < 0)
      {
         QC_LOG_ERR(GET_QMIMUXDEV(pDev,i),"Bad minor number\n" );
         return -ENXIO;
      }
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,27 ))
      // kernel 2.6.27 added a new fourth parameter to device_create
      //    void * drvdata : the data to be added to the device for callbacks
         if (IS_ERR(dev_ret = device_create( pDev->mQMIMUXDev[i].mpDevClass,
                         &pDev->mpIntf->dev, 
                         devMUXno[i],
                         NULL,
                         "%s:%d-%s:usb%d_%d", 
                         pDev->mDevInfo.mDevInfInfo.mpKey,
                         pDev->mpNetMUXDev[i]->udev->bus->busnum,
                         pDev->mpNetMUXDev[i]->udev->devpath,
                         GobiQMIIndex, i + 1))){
                           QC_LOG_ERR(GET_QMIMUXDEV(pDev,i),"GobiNet: Device creation failed :%ld\n",PTR_ERR(dev_ret));
                           class_destroy(pDev->mQMIMUXDev[i].mpDevClass);
                           unregister_chrdev_region(devMUXno[i], 1);
                           return PTR_ERR(dev_ret);
                         }
#else
          if (IS_ERR(dev_ret = device_create( pDev->mQMIMUXDev[i].mpDevClass,
                         &pDev->mpIntf->dev, 
                         devMUXno[i],
                         "%s:%d-%s:usb%d_%d",
                         pDev->mDevInfo.mDevInfInfo.mpKey,
                         pDev->mpNetMUXDev[i]->udev->bus->busnum,
                         pDev->mpNetMUXDev[i]->udev->devpath,
                         GobiQMIIndex, i + 1))){
                           QC_LOG_ERR(GET_QMIMUXDEV(pDev,i),"GobiNet: Device creation failed :%ld\n",PTR_ERR(dev_ret));
                           class_destroy(pDev->mQMIMUXDev[i].mpDevClass);
                           unregister_chrdev_region(devMUXno[i], 1);
                           return PTR_ERR(dev_ret);
                         }
#endif
          
          pDev->mQMIMUXDev[i].mDevNum = devMUXno[i];
      if (pDev) {
         sprintf(pDev->mQMIMUXDev[i].mdeviceName,"%s:%d-%s:usb%d_%d",pDev->mDevInfo.mDevInfInfo.mpKey,
         pDev->mpNetMUXDev[i]->udev->bus->busnum,
         pDev->mpNetMUXDev[i]->udev->devpath,
         GobiQMIIndex, i+1);
      }
      QC_LOG_INFO(GET_QMIMUXDEV(pDev,i),"pDev->mQMIMUXDev[%d].mdeviceName = %s\n",i,pDev->mQMIMUXDev[i].mdeviceName);
   }
   
   // Success
   return 0;
}

/*===========================================================================
METHOD:
   DeregisterQMIDevice (Public Method)

DESCRIPTION:
   QMI Device cleanup function
   
   NOTE: When this function is run the device is no longer valid

PARAMETERS:
   pDev     [ I ] - Device specific memory

RETURN VALUE:
   None
===========================================================================*/
void DeregisterQMIDevice( sGobiUSBNet * pDev )
{
   struct inode * pOpenInode;
   struct list_head * pInodeList;
   struct task_struct * pEachTask = NULL;
   struct fdtable * pFDT;
   struct file * pFilp;
   unsigned long flags;
   int count = 0;
   int tries = 0;
   int result = 0;
   int i;
   int retThread = 0;

   sClientMemList *pClientMemIter, *pClientMemSafe = NULL;

   // Should never happen, but check anyway
   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device\n" );
      return;
   }

   // Release all clients
   list_for_each_entry_safe( pClientMemIter,pClientMemSafe, &pDev->mQMIDev.mClientMemList, node){
      
      QC_LOG_INFO(GET_QMIDEV(pDev), "release 0x%x\n", pClientMemIter->mClientID );
   
      ReleaseClientID( pDev, pClientMemIter->mClientID, &pDev->mQMIDev);
   }

   for (i=0;i<MAX_MUX_DEVICES;i++)
   {
      sQMIDev *pQMIMUXDev = &(pDev->mQMIMUXDev[i]);
      // Release all clients

      list_for_each_entry_safe( pClientMemIter,pClientMemSafe, &pQMIMUXDev->mClientMemList, node){
      
      QC_LOG_INFO(GET_QMIDEV(pDev), "release 0x%x\n", pClientMemIter->mClientID );
   
      ReleaseClientID( pDev, pClientMemIter->mClientID, &(pDev->mQMIMUXDev[i]));
      }

   }
   
   // Stop all reads
   KillRead( pDev );

   /*<===============sysfs starts============>*/

   sysfs_destroy(pDev);

   /*<===============sysfs ends============>*/

   if (pDev->mQMIDev.mbCdevIsInitialized == false)
   {
      return;
   }

   // Find each open file handle, and manually close it
   
   // Generally there will only be only one inode, but more are possible
   list_for_each( pInodeList, &pDev->mQMIDev.mCdev.list )
   {
      // Get the inode
      pOpenInode = container_of( pInodeList, struct inode, i_devices );
      if (pOpenInode != NULL && (IS_ERR( pOpenInode ) == false))
      {
         // Look for this inode in each task

         rcu_read_lock();
         for_each_process( pEachTask )
         {
            if (pEachTask == NULL || pEachTask->files == NULL)
            {
               // Some tasks may not have files (e.g. Xsession)
               continue;
            }
            // For each file this task has open, check if it's referencing
            // our inode.
            pFDT = files_fdtable( pEachTask->files );
            for (count = 0; count < pFDT->max_fds; count++)
            {
               pFilp = pFDT->fd[count];
               //if (pFilp != NULL &&  pFilp->f_dentry != NULL)
            //vereddy: "(pFilp->f_path).dentry" this logic works for both 3.x, 4.x kernel versions
               if (pFilp != NULL &&  (pFilp->f_path).dentry != NULL)
               {
                  if (file_inode(pFilp) == pOpenInode)
                  {
                     // Close this file handle
                     rcu_assign_pointer( pFDT->fd[count], NULL );                     
                     
                      QC_LOG_ERR(GET_QMIDEV(pDev),"forcing close of open file handle\n" );
                     filp_close( pFilp, pEachTask->files );
                  }
               }
            }
         }
         rcu_read_unlock();
      }
   }

   for (i=0;i<MAX_MUX_DEVICES;i++)
   {
//      #ifdef VIRTUAL_USB_CODE
      // Generally there will only be only one inode, but more are possible
      list_for_each( pInodeList, &pDev->mQMIMUXDev[i].mCdev.list )
      {
         // Get the inode
         pOpenInode = container_of( pInodeList, struct inode, i_devices );
         if (pOpenInode != NULL && (IS_ERR( pOpenInode ) == false))
         {
            // Look for this inode in each task
   
            rcu_read_lock();
            for_each_process( pEachTask )
            {
               if (pEachTask == NULL || pEachTask->files == NULL)
               {
                  // Some tasks may not have files (e.g. Xsession)
                  continue;
               }
               // For each file this task has open, check if it's referencing
               // our inode.
               pFDT = files_fdtable( pEachTask->files );
               for (count = 0; count < pFDT->max_fds; count++)
               {
                  pFilp = pFDT->fd[count];
                  //if (pFilp != NULL &&  pFilp->f_dentry != NULL)
            //"(pFilp->f_path).dentry" this logic works for both 3.x, 4.x kernel versions
                  if (pFilp != NULL &&  pFilp->f_path.dentry != NULL)
                  {
                     if (file_inode(pFilp) == pOpenInode)
                     {
                        // Close this file handle
                        rcu_assign_pointer( pFDT->fd[count], NULL );                     
                        
                        QC_LOG_ERR(GET_QMIDEV(pDev),"forcing close of open file handle\n" );
                        filp_close( pFilp, pEachTask->files );
   
                     }
                  }
               }
            }
            rcu_read_unlock();
         }
      }
//      #endif
   }
   
   pDev->mbQMIValid = false;

   // Send SetControlLineState request (USB_CDC)
   result = usb_control_msg( pDev->mpNetDev->udev,
                             usb_sndctrlpipe( pDev->mpNetDev->udev, 0 ),
                             0x22,
                             0x21,
                             0, // DTR not present
                             pDev->mpEndpoints->mIntfNum,
                             NULL,
                             0,
                             100 );
   if (result < 0)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Bad SetControlLineState status %d\n", result );
   }

   /* Release the data present in response threads */
/* Minimized the long hold of spin lock, for better concurrency and responsiveness of the system */
   spin_lock_irqsave(pDev->mProcessIndData.mpResponeListLock, flags);
   /* Extract the data from begining */
   while(pDev->mProcessIndData.mpIndDataInfoList)
   {
       sIndDataInfo *pIndDataInfo = pDev->mProcessIndData.mpIndDataInfoList;
       pDev->mProcessIndData.mpIndDataInfoList = pIndDataInfo->mpNext;
       kfree(pIndDataInfo);
   }
   
   spin_unlock_irqrestore(pDev->mProcessIndData.mpResponeListLock, flags);
   
    /* Release lock */

   raw_spin_lock_irqsave(&pDev->mProcessIndData.mpResponseReadSem->lock, flags);
	   if ( !list_empty(&pDev->mProcessIndData.mpResponseReadSem->wait_list) ) {
         raw_spin_unlock_irqrestore(&pDev->mProcessIndData.mpResponseReadSem->lock, flags);

         up(pDev->mProcessIndData.mpResponseReadSem);

      } 
      else {
         raw_spin_unlock_irqrestore(&pDev->mProcessIndData.mpResponseReadSem->lock, flags);
         QC_LOG_INFO(GET_QMIDEV(pDev),"ResponseReadSem wait_list empty. UP no required\n");
      }

   // kthread_stop signals to gobiwaitforresponse thread and waits for the thread to exit
   if (pDev->pGobiWaitThread) {
      retThread = kthread_stop(pDev->pGobiWaitThread);
      if (retThread != -EINTR) {
         QC_LOG_INFO(GET_QMIDEV(pDev),"GobiWaitForResponses thread stopped, retThread= %d\n",retThread);
      }
      else {
         QC_LOG_INFO(GET_QMIDEV(pDev),"GobiWaitForResponses thread stopped, Interrupted ret= %d\n",retThread);
      }
   }
   else {
      QC_LOG_INFO(GET_QMIDEV(pDev),"GobiWaitForResponses thread already exited. No need for kthread_stop\n");
   }
   
   // Remove device (so no more calls can be made by users)
   if (IS_ERR( pDev->mQMIDev.mpDevClass ) == false)
   {
      device_destroy( pDev->mQMIDev.mpDevClass, 
                      pDev->mQMIDev.mDevNum );   
   }

   for (i=0;i<MAX_MUX_DEVICES;i++)
   { 
       // Remove device (so no more calls can be made by users)
       if (IS_ERR( pDev->mQMIMUXDev[i].mpDevClass ) == false)
       {
          device_destroy( pDev->mQMIMUXDev[i].mpDevClass, 
                          pDev->mQMIMUXDev[i].mDevNum );   
       }
   }

   // Hold onto cdev memory location until everyone is through using it.
   // Timeout after 30 seconds (10 ms interval).  Timeout should never happen,
   // but exists to prevent an infinate loop just in case.
   for (tries = 0; tries < 30 * 100; tries++)
   {
      int ref = atomic_read((const atomic_t *) &pDev->mQMIDev.mCdev.kobj.kref.refcount );
      if (ref > 1)
      {
         QC_LOG_ERR(GET_QMIDEV(pDev),"cdev in use by %d tasks\n", ref - 1 ); 
         msleep( 10 );
      }
      else
      {
         break;
      }
   }

   for (i=0;i<MAX_MUX_DEVICES;i++)
   {
       // Hold onto cdev memory location until everyone is through using it.
       // Timeout after 30 seconds (10 ms interval).  Timeout should never happen,
       // but exists to prevent an infinate loop just in case.
       for (tries = 0; tries < 30 * 100; tries++)
       {
          int ref = atomic_read((const atomic_t*)&pDev->mQMIMUXDev[i].mCdev.kobj.kref.refcount );
          if (ref > 1)
          {
             QC_LOG_ERR(GET_QMIMUXDEV(pDev,i),"cdev in use by %d tasks\n", ref - 1 ); 
             msleep( 10 );
          }
          else
          {
             break;
          }
       }
   }

   cdev_del( &pDev->mQMIDev.mCdev );

   for (i=0;i<MAX_MUX_DEVICES;i++)
   {
       cdev_del( &pDev->mQMIMUXDev[i].mCdev );
   }   
   /* unallocate char devices */
   unregister_chrdev_region( pDev->mQMIDev.mDevNum, MAX_MUX_DEVICES + 1 );

   QC_LOG_INFO(GET_QMIDEV(pDev),"gracefully exit\n");
   return;
}

/*=========================================================================*/
// Driver level client management
/*=========================================================================*/

/*===========================================================================
METHOD:
   QMIReady (Public Method)

DESCRIPTION:
   Send QMI CTL GET VERSION INFO REQ
   Wait for response or timeout

PARAMETERS:
   pDev     [ I ] - Device specific memory
   timeout  [ I ] - Milliseconds to wait for response

RETURN VALUE:
   bool
===========================================================================*/
bool QMIReady(
   sGobiUSBNet *    pDev,
   u32                timeout )
{
   int result;
   void * pWriteBuffer;
   u16 writeBufferSize;
   void * pReadBuffer;
   u16 readBufferSize;
   struct semaphore readSem;
   u32 curTime;
   u8 transactionID; /* size = 1 byte for CTL, 2 bytes for others*/

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device\n" );
      return false;
   }
   QC_LOG_INFO(GET_QMIDEV(pDev),"Inside\n");
   writeBufferSize = QMICTLReadyReqSize();
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
      return false;
   }

   // An implimentation of down_timeout has not been agreed on,
   //    so it's been added and removed from the kernel several times.
   //    We're just going to ignore it and poll the semaphore.

   // Send a write every 100 ms and see if we get a response
   for (curTime = 0; curTime < timeout; curTime += 500)
   {
      if (IsDeviceValid( pDev ) == false)
      {
         QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device\n" );
         return false;
      }
      // Start read
      sema_init( &readSem, 0 );
   
      transactionID = atomic_add_return( 1, &pDev->mQMIDev.mQMICTLTransactionID );
      if (transactionID == 0)
      {
         transactionID = atomic_add_return( 1, &pDev->mQMIDev.mQMICTLTransactionID );
      }
      
      result = ReadAsync( pDev, QMICTL, transactionID, UpSem, &readSem, &pDev->mQMIDev);
      if (result != 0)
      {
         return false;
      }

      // Fill buffer
      result = QMICTLReadyReq( pWriteBuffer, 
                               writeBufferSize,
                               transactionID );
      if (result < 0)
      {
         kfree( pWriteBuffer );
         return false;
      }
      WriteSync( pDev,
                 pWriteBuffer,
                 writeBufferSize,
                 QMICTL,
                 &pDev->mQMIDev);

      msleep( 500 );
      if (down_trylock( &readSem ) == 0)
      {
          int i;
          i = curTime;
#define GET_CTL_VERSION_WRITE_DELAY 3000
         /* Need to wait for 3 sec, before calling WriteSync() */
         for (; curTime <= (i + 3000); curTime += 500)
       {
          if (IsDeviceValid( pDev ) == false)
          {
             QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device\n" );
                 kfree(pWriteBuffer);
             return false;
          }
             msleep(500);
          if (IsDeviceValid( pDev ) == false)
          {
             QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device\n" );
                 kfree(pWriteBuffer);
             return false;
          }
             // Pop the read data
             if ( PopFromReadMemList( pDev,
                         QMICTL,
                         transactionID,
                         &pReadBuffer,
                         &readBufferSize,
                         &pDev->mQMIDev) == true)
             {
                 // Success
                 // We don't care about the result
                 kfree( pReadBuffer );

                 goto end;
                 //break;
             }
         }
      }
      else
      {
         //QC_LOG_ERR(GET_QMIDEV(pDev),"<QMIDevice> Timeout, remove the asyn read..\n");
         // Timeout, remove the async read
         NotifyAndPopNotifyList( pDev, QMICTL, transactionID, &pDev->mQMIDev );
      }
   }

   // Did we time out?   
   if (curTime >= timeout)
   {
    QC_LOG_ERR(GET_QMIDEV(pDev),"<QMIDevice> we timeout\n");
      kfree(pWriteBuffer);
      return false;
   }

end:
   kfree( pWriteBuffer );
   pDev->mbQMIReadyStatus = true;
   QC_LOG_INFO(GET_QMIDEV(pDev)," QMI Ready after %u milliseconds\n", curTime);

   // Success
   return true;
}

/*===========================================================================
METHOD:
   QMIWDSCallback (Public Method)

DESCRIPTION:
   QMI WDS callback function
   Update net stats or link state

PARAMETERS:
   pDev     [ I ] - Device specific memory
   clientID [ I ] - Client ID
   pData    [ I ] - Callback data (unused)

RETURN VALUE:
   None
===========================================================================*/
void QMIWDSCallback(
   sGobiUSBNet *    pDev,
   u16                clientID,
   void *             pData,
   sQMIDev *QMIDev)
{
   bool bRet;
   int result;
   void * pReadBuffer;
   u16 readBufferSize;

#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,31 ))
   struct net_device_stats * pStats = &(pDev->mpNetDev->stats);
#else
   struct net_device_stats * pStats = &(pDev->mpNetDev->net->stats);
#endif

   u32 TXOk = (u32)-1;
   u32 RXOk = (u32)-1;
   u32 TXErr = (u32)-1;
   u32 RXErr = (u32)-1;
   u32 TXOfl = (u32)-1;
   u32 RXOfl = (u32)-1;
   u64 TXBytesOk = (u64)-1;
   u64 RXBytesOk = (u64)-1;
   bool bLinkState;
   bool bReconfigure;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev, "Invalid device\n" );
      return;
   }
   
   bRet = PopFromReadMemList( pDev,
                              clientID,
                              0,
                              &pReadBuffer,
                              &readBufferSize,
                              QMIDev);
   
   if (bRet == false)
   {
      QC_LOG_ERR(QMIDev, "WDS callback failed to get data\n" );
      return;
   }
   
   // Default values
   bLinkState = ! GobiTestDownReason( pDev, NO_NDIS_CONNECTION );
   bReconfigure = false;

   result = QMIWDSEventResp( pReadBuffer,
                             readBufferSize,
                             &TXOk,
                             &RXOk,
                             &TXErr,
                             &RXErr,
                             &TXOfl,
                             &RXOfl,
                             &TXBytesOk,
                             &RXBytesOk,
                             &bLinkState,
                             &bReconfigure );
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev, "bad WDS packet\n" );
   }
   else
   {

      // Fill in new values, ignore max values
      if (TXOfl != (u32)-1)
      {
         pStats->tx_fifo_errors = TXOfl;
      }
      
      if (RXOfl != (u32)-1)
      {
         pStats->rx_fifo_errors = RXOfl;
      }

      if (TXErr != (u32)-1)
      {
         pStats->tx_errors = TXErr;
      }

      if (RXErr != (u32)-1)
      {
         pStats->rx_errors = RXErr;
      }

      if (TXOk != (u32)-1)
      {
         pStats->tx_packets = TXOk + pStats->tx_errors;
      }
      
      if (RXOk != (u32)-1)
      {
         pStats->rx_packets = RXOk + pStats->rx_errors;
      }

      if (TXBytesOk != (u64)-1)
      {
         pStats->tx_bytes = TXBytesOk;
      }
      
      if (RXBytesOk != (u64)-1)
      {
         pStats->rx_bytes = RXBytesOk;
      }

      if (bReconfigure == true)
      {
         QC_LOG_INFO(QMIDev, "Net device link reset\n" );
         GobiSetDownReason( pDev, NO_NDIS_CONNECTION );
         GobiClearDownReason( pDev, NO_NDIS_CONNECTION );
      }
      else 
      {
         if (bLinkState == true)
         {
            QC_LOG_INFO(QMIDev, "pQMIDevDBG:0x%px, Net device link is connected\n", QMIDev);
            GobiClearDownReason( pDev, NO_NDIS_CONNECTION );
         }
         else
         {
            QC_LOG_INFO(QMIDev, "pQMIDevDBG:0x%px, Net device link is disconnected\n", QMIDev);
            GobiSetDownReason( pDev, NO_NDIS_CONNECTION );
         }
      }
   }

   kfree( pReadBuffer );

   // Setup next read
   QC_LOG_DBG(QMIDev, "pQMIDevDBG:0x%px, Setup next read", QMIDev);
   result = ReadAsync( pDev,
                       clientID,
                       0,
                       QMIWDSCallback,
                       pData,
                       QMIDev);
   if (result != 0)
   {
      QC_LOG_ERR(QMIDev, "pQMIDevDBG:0x%px, unable to setup next async read\n",QMIDev);
   }

   return;
}

static int GobiWaitForResponses(void *pData)
{
    sGobiUSBNet     *pDev;
    sIndDataInfo    *pIndDataInfo;
    sQMIDev         *QMIDev;
    unsigned long   flags;
    int             retval;
    int             i;
    /* Static -> to make sure the variables are not cleared in case of unusual thread closure */

    pDev = pData;
    if (pDev == NULL)
    {
       QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device\n" );
        return -EINVAL;
    }
    QC_LOG_INFO(GET_QMIDEV(pDev), "Thread open\n");

    spin_lock_init(&pDev->responeListLock);

    pDev->mProcessIndData.mpResponeListLock = &pDev->responeListLock;

    sema_init(&pDev->responseReadSem, 0);

    pDev->mProcessIndData.mpResponseReadSem = &pDev->responseReadSem;

    allow_signal(SIGKILL);

   if (pDev->mbQMIReadyStatus == false)
   {
       // Device is not ready for QMI connections right away
       //   Wait up to 120 seconds before failing
       if (QMIReady( pDev, 120000 ) == false)
       {
         QC_LOG_ERR(GET_QMIDEV(pDev), "Device unresponsive to QMI\n");
         pDev->pGobiWaitThread = NULL;
         retval = -ETIMEDOUT;
         goto end; 
       }

       pDev->mbQMIReadyStatus = true;
       retval = ConfigureQMAP(pDev);
       if (retval != 0)
       {
         QC_LOG_ERR(GET_QMIDEV(pDev),"ConfigureQMAP failed\n");
         pDev->pGobiWaitThread = NULL;
         goto end;
       }

       // Setup WDS callback
       retval = SetupQMIWDSCallback( pDev, &pDev->mQMIDev );
       if (retval != 0)
       {
        QC_LOG_ERR(GET_QMIDEV(pDev),"SetupQMIWDSCallback failed\n");
         pDev->pGobiWaitThread = NULL;
         goto end;
       }

       // Fill MEID for device
       retval = QMIDMSGetMEID( pDev, &pDev->mQMIDev );
       if (retval != 0)
       {
         QC_LOG_ERR(GET_QMIDEV(pDev),"QMIDMSGetMEID failed\n");
         pDev->pGobiWaitThread = NULL;
         goto end;
       }

       /* need to sub rx URBs for the aggregation size negotiated */
       pDev->mpNetDev->rx_urb_size = RX_URB_SIZE;

#ifdef VIRTUAL_USB_CODE
       for (i=0; i<MAX_MUX_DEVICES; i++)
       {
           /* need to sub rx URBs for the aggregation size negotiated */
           pDev->mpNetMUXDev[i]->rx_urb_size = RX_URB_SIZE;
       }
#endif

#ifdef TX_AGGR
       pDev->tx_aggr_ctx.tx_max = pDev->ULAggregationMaxSize;
       pDev->tx_aggr_ctx.tx_max_datagrams = pDev->ULAggregationMaxDatagram;
#endif
   }
   

    set_current_state(TASK_INTERRUPTIBLE);
    while ( !kthread_should_stop() )
    {

        retval = down_interruptible(&pDev->responseReadSem);
        if (retval != 0)
        {
            retval = -EINTR;
            break;
        }
        if (retval == 0) {
         QC_LOG_INFO(GET_QMIDEV(pDev),"Successfully acquired responseReadSem\n");
        }

       if (signal_pending(pDev->pGobiWaitThread)) {
            QC_LOG_ERR(GET_QMIDEV(pDev),"signal to kill thread\n");
            break;
        }

        if (IsDeviceValid( pDev ) == false)
        {
            QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device\n" );
            retval = -ENXIO;
            break;
        }

        /* Acquire Lock */
        spin_lock_irqsave(&pDev->responeListLock, flags);

        pIndDataInfo = pDev->mProcessIndData.mpIndDataInfoList;
        /* Extract the data from begining */
        if (pIndDataInfo)
        {
            pDev->mProcessIndData.mpIndDataInfoList = pIndDataInfo->mpNext;
        }
        /* Release lock */
        spin_unlock_irqrestore(&pDev->responeListLock, flags);

        if (!pIndDataInfo)
        {
            /* This should never happen */
            QC_LOG_ERR(GET_QMIDEV(pDev),"%d-%s: No data available\n",pDev->mpNetDev->udev->bus->busnum, pDev->mpNetDev->udev->devpath);
            continue;
        }

        QMIDev = pIndDataInfo->mQMIDev;

        QC_LOG_DBG(QMIDev,"Got request : %d , %d\n", pIndDataInfo->mMsgId, pIndDataInfo->mDataLen);

        switch(pIndDataInfo->mMsgId)
        {
            case QMI_WDS_PKT_SRVC_STATUS_IND:
                QC_LOG_DBG(QMIDev,"QMI_WDS_PKT_SRVC_STATUS_IND\n");
                QMIWDSGetPktSrvcStatusInd(pDev, pIndDataInfo->mpData, pIndDataInfo->mDataLen, QMIDev, pIndDataInfo->mClientId);
                QC_LOG_DBG(QMIDev,"Need to set IP, Power Up/Down\n");
                break;
            case QMI_WDS_EXTENDED_IP_CONFIG_IND:
                QC_LOG_DBG(QMIDev,"QMI_WDS_EXTENDED_IP_CONFIG_IND\n");
                QMIWDSExtendedIPConfigInd(pDev, pIndDataInfo->mpData, pIndDataInfo->mDataLen, QMIDev, pIndDataInfo->mClientId);
                QC_LOG_DBG(QMIDev,"Need to set MTU\n");
                break;
            case QMI_WDS_REVERSE_IP_TRANSPORT_CONNECTION_IND:
                QC_LOG_DBG(QMIDev,"QMI_WDS_REVERSE_IP_TRANSPORT_CONNECTION_IND\n");
                QMIWDSReverseIPTransportConnInd(pDev, pIndDataInfo->mpData, pIndDataInfo->mDataLen, QMIDev);
                QC_LOG_DBG(QMIDev,"Need to set MTU\n");
                break;
            case QMI_WDS_GET_RUNTIME_SETTINGS_RESP:
                QC_LOG_DBG(QMIDev,"QMI_WDS_GET_RUNTIME_SETTINGS_RESP\n");
                QMIWDSGetRuntimeSettingsResp(pDev, pIndDataInfo->mpData, pIndDataInfo->mDataLen, QMIDev);
                QC_LOG_DBG(QMIDev,"Need to set IP, MTU, Runtime params\n");
                break;
            default:
                break;
        }

        kfree(pIndDataInfo->mpData);
        kfree(pIndDataInfo);

        set_current_state(TASK_INTERRUPTIBLE);
    }

end:
    set_current_state(TASK_RUNNING);
    QC_LOG_INFO(GET_QMIDEV(pDev)," Thread close: %d\n", retval);

    return retval;
}

/*===========================================================================
METHOD:
   SetupQMIWDSCallback (Public Method)

DESCRIPTION:
   Request client and fire off reqests and start async read for 
   QMI WDS callback

PARAMETERS:
   pDev     [ I ] - Device specific memory

RETURN VALUE:
   int - 0 for success
         Negative errno for failure
===========================================================================*/
int SetupQMIWDSCallback( sGobiUSBNet * pDev, sQMIDev *QMIDev )
{
   int result;
   void * pWriteBuffer;
   u16 writeBufferSize;
   u16 WDSClientID;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device\n" );
      return -EFAULT;
   }
   
   result = GetClientID( pDev, QMIWDS, QMIDev );
   if (result < 0)
   {
      return result;
   }
   WDSClientID = result;

   // QMI WDS Set Event Report
   writeBufferSize = QMIWDSSetEventReportReqSize();
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
      return -ENOMEM;
   }

   result = QMIWDSSetEventReportReq( pWriteBuffer, 
                                     writeBufferSize,
                                     GetTransactionID(QMIDev));
   if (result < 0)
   {
      kfree( pWriteBuffer );
      return result;
   }

   result = WriteSync( pDev,
                       pWriteBuffer,
                       writeBufferSize,
                       WDSClientID,
                       QMIDev);
   kfree( pWriteBuffer );

   if (result < 0)
   {
      return result;
   }

   // QMI WDS Get PKG SRVC Status
   writeBufferSize = QMIWDSGetPKGSRVCStatusReqSize();
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
      return -ENOMEM;
   }

   result = QMIWDSGetPKGSRVCStatusReq( pWriteBuffer, 
                                       writeBufferSize,
                                       GetTransactionID(QMIDev) );
   if (result < 0)
   {
      kfree( pWriteBuffer );
      return result;
   }
   
   result = WriteSync( pDev,
                       pWriteBuffer,
                       writeBufferSize,
                       WDSClientID,
                       QMIDev);
   kfree( pWriteBuffer );

   if (result < 0)
   {
      return result;
   }

   // Setup asnyc read callback
   result = ReadAsync( pDev,
                       WDSClientID,
                       0,
                       QMIWDSCallback,
                       NULL,
                       QMIDev);
   if (result != 0)
   {
      QC_LOG_ERR(QMIDev,"unable to setup async read\n" );
      return result;
   }

   // Send SetControlLineState request (USB_CDC)
   //   Required for Autoconnect
   result = usb_control_msg( pDev->mpNetDev->udev,
                             usb_sndctrlpipe( pDev->mpNetDev->udev, 0 ),
                             0x22,
                             0x21,
                             1, // DTR present
                             pDev->mpEndpoints->mIntfNum,
                             NULL,
                             0,
                             100 );
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"Bad SetControlLineState status %d\n", result );
      return result;
   }

   return 0;
}

int ConfigureQMAP(sGobiUSBNet *pDev)
{
   u16 WDAClientID;
   u16 result;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(GET_QMIDEV(pDev),"Invalid device\n" );
      return -EFAULT;
   }
   QC_LOG_DBG(GET_QMIDEV(pDev), "In \n");
   result = GetClientID( pDev, QMIWDA, &pDev->mQMIDev);
   if (result < 0)
   {
      return result;
   }
   WDAClientID = result;

   /* Set QMAP */
   result = QMIWDASetQMAP( pDev , WDAClientID, &pDev->mQMIDev);
   if (result != 0)
   {
      return result;
   }

   /* Set QMAP Aggregation size*/
   result = QMIWDASetQMAPSettings( pDev , WDAClientID, &pDev->mQMIDev);
   if (result != 0)
   {
      return result;
   }
   QC_LOG_DBG(GET_QMIDEV(pDev), "Out \n");
   return 0;
}

int QMIWDASetQMAPSettings( sGobiUSBNet * pDev , u16 WDAClientID, sQMIDev *QMIDev)
{
   int result;
   void * pWriteBuffer;
   u16 writeBufferSize;
   void * pReadBuffer;
   u16 readBufferSize;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev, "Invalid device\n" );
      return -EFAULT;
   }

   // QMI DMS Get Serial numbers Req
   writeBufferSize = QMIWDASetDataFormatReqSettingsSize();
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
      return -ENOMEM;
   }
   QC_LOG_INFO(QMIDev, "QMIWDASetDataFormatReqSettings\n");
   result = QMIWDASetDataFormatReqSettings( pWriteBuffer,
                              writeBufferSize,
                              GetTransactionID(QMIDev) );
   if (result < 0)
   {
      kfree( pWriteBuffer );
      return result;
   }

   result = WriteSync( pDev,
                       pWriteBuffer,
                       writeBufferSize,
                       WDAClientID,
                       QMIDev);
   kfree( pWriteBuffer );

   if (result < 0)
   {
      return result;
   }

   // QMI WDA QMAP Resp
   result = ReadSyncTimeout( pDev,
                      &pReadBuffer,
                      WDAClientID,
                      QMIDev->mQMITransactionID,
                      QMIDev);
   if (result < 0)
   {
      return result;
   }
   readBufferSize = result;
   PrintHex( pReadBuffer, readBufferSize );
   kfree( pReadBuffer );

   // Success
   return 0;
}


/*===========================================================================
METHOD:
   QMIWDASetQMAP (Public Method)

DESCRIPTION:
   Register WDA client
   set QMAP req and parse response
   Release WDA client

PARAMETERS:
   pDev     [ I ] - Device specific memory

RETURN VALUE:
   None
===========================================================================*/
int QMIWDASetQMAP( sGobiUSBNet * pDev , u16 WDAClientID, sQMIDev *QMIDev)
{
   int result;
   void * pWriteBuffer;
   u16 writeBufferSize;
   void * pReadBuffer;
   u16 readBufferSize;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device\n" );
      return -EFAULT;
   }

   // QMI DMS Get Serial numbers Req
   writeBufferSize = QMIWDASetDataFormatReqSize();
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
      return -ENOMEM;
   }
   QC_LOG_INFO(QMIDev, "QMIWDASetDataFormatReq\n");
   result = QMIWDASetDataFormatReq( pWriteBuffer,
                              writeBufferSize,
                              GetTransactionID(QMIDev), pDev );
   if (result < 0)
   {
      kfree( pWriteBuffer );
      return result;
   }

   result = WriteSync( pDev,
                       pWriteBuffer,
                       writeBufferSize,
                       WDAClientID,
                       QMIDev);
   kfree( pWriteBuffer );

   if (result < 0)
   {
      return result;
   }

   // QMI WDA QMAP Resp
   result = ReadSyncTimeout( pDev,
                      &pReadBuffer,
                      WDAClientID,
                      QMIDev->mQMITransactionID,
                      QMIDev);
   if (result < 0)
   {
      return result;
   }
   readBufferSize = result;
   PrintHex( pReadBuffer, readBufferSize );

   result = QMIWDASetDataFormatResp( pReadBuffer,
                             readBufferSize,
                             &pDev->DLAggregationMaxDatagram,
                             &pDev->DLAggregationMaxSize,
#ifdef TX_AGGR
                             &pDev->ULAggregationMaxDatagram,
                             &pDev->ULAggregationMaxSize);
#else
                             NULL,
                             NULL);
#endif

   if (result < 0)
   {
#ifdef TX_AGGR
   QC_LOG_ERR(QMIDev,"Error in Resp: UL Aggr Max Datagrams 0x%x Aggr Max Datagram Size 0x%x\n", pDev->ULAggregationMaxDatagram, pDev->ULAggregationMaxSize);
   QC_LOG_ERR(QMIDev,"Error in Resp: DL Aggr Max Datagrams 0x%x Aggr Max Datagram Size 0x%x\n", pDev->DLAggregationMaxDatagram, pDev->DLAggregationMaxSize);
#else
   QC_LOG_ERR(QMIDev,"Error in Resp: DL Aggr Max Datagrams 0x%x Aggr Max Datagram Size 0x%x\n", pDev->DLAggregationMaxDatagram, pDev->DLAggregationMaxSize);
#endif
   
   kfree( pReadBuffer );   
   return result;
   }
   
#ifdef TX_AGGR
   QC_LOG_DBG(QMIDev,"UL Aggr Max Datagrams 0x%x Aggr Max Datagram Size 0x%x\n", pDev->ULAggregationMaxDatagram, pDev->ULAggregationMaxSize);
   QC_LOG_DBG(QMIDev,"DL Aggr Max Datagrams 0x%x Aggr Max Datagram Size 0x%x\n", pDev->DLAggregationMaxDatagram, pDev->DLAggregationMaxSize);
#else
   QC_LOG_DBG(QMIDev,"DL Aggr Max Datagrams 0x%x Aggr Max Datagram Size 0x%x\n", pDev->DLAggregationMaxDatagram, pDev->DLAggregationMaxSize);
#endif

   kfree( pReadBuffer );

   // Success
   return 0;
}


/*===========================================================================
METHOD:
   QMIDMSGetMEID (Public Method)

DESCRIPTION:
   Register DMS client
   send MEID req and parse response
   Release DMS client

PARAMETERS:
   pDev     [ I ] - Device specific memory

RETURN VALUE:
   None
===========================================================================*/
int QMIDMSGetMEID( sGobiUSBNet * pDev, sQMIDev *QMIDev )
{
   int result;
   void * pWriteBuffer;
   u16 writeBufferSize;
   void * pReadBuffer;
   u16 readBufferSize;
   u16 DMSClientID;

   if (IsDeviceValid( pDev ) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device\n" );
      return -ENXIO;
   }

   result = GetClientID( pDev, QMIDMS, &pDev->mQMIDev );
   if (result < 0)
   {
      return result;
   }
   DMSClientID = result;

   // QMI DMS Get Serial numbers Req
   writeBufferSize = QMIDMSGetMEIDReqSize();
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
      return -ENOMEM;
   }

   result = QMIDMSGetMEIDReq( pWriteBuffer, 
                              writeBufferSize,
                              GetTransactionID(QMIDev) );
   if (result < 0)
   {
      kfree( pWriteBuffer );
      return result;
   }

   result = WriteSync( pDev,
                       pWriteBuffer,
                       writeBufferSize,
                       DMSClientID,
                       &pDev->mQMIDev);
   kfree( pWriteBuffer );

   if (result < 0)
   {
      return result;
   }

   // QMI DMS Get Serial numbers Resp
   result = ReadSyncTimeout( pDev,
                      &pReadBuffer,
                      DMSClientID,
                      QMIDev->mQMITransactionID,
                      &pDev->mQMIDev);
   if (result < 0)
   {
      return result;
   }
   readBufferSize = result;

   result = QMIDMSGetMEIDResp( pReadBuffer,
                               readBufferSize,
                               &pDev->mMEID[0],
                               14 );
   kfree( pReadBuffer );

   if (result < 0)
   {
      QC_LOG_WARN(QMIDev,"bad get MEID resp\n");
      
      // Non fatal error, device did not return any MEID
      //    Fill with 0's
      memset( &pDev->mMEID[0], '0', 14 );
   }

   ReleaseClientID( pDev, DMSClientID, &pDev->mQMIDev);

   // Success
   return 0;
}

int WDSConnect(sGobiUSBNet *pDev, sQMIDev *QMIDev, u8 profileid, int *ipv6id, u32 *connectid, u32 *connectipv6id) {

   int id;
   int ipv6ID;
   int result;
   void * pWriteBuffer;
   u16 writeBufferSize;
   void * pReadBuffer;
   u16 readBufferSize;
   char res_wds[4];

   if (IsDeviceValid(pDev) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device!\n" );
      return -ENXIO;
   }
   // get client id
   id = GetClientID( pDev, QMIWDS, QMIDev );

   if (id < 0)
       QC_LOG_ERR(QMIDev,"Error getting client id\n");

   // negotiate MUX ID as 0x81

   writeBufferSize = sizeof( sQMUX ) + 29;
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
       QC_LOG_ERR(QMIDev,"Mem alloc failed for bind mux port\n");
       return -ENOMEM;
   }

   result = QMIWDSBindMuxPortReq( pWriteBuffer,
         writeBufferSize,
         GetTransactionID(QMIDev), pDev, QMIDev);
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Request setup failed for bind mux port\n");
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
       QC_LOG_ERR(QMIDev,"Bind mux port Failed\n");
       return result;
   }

   result = ReadSync( pDev,
                      &pReadBuffer,
                      id,
                      QMIDev->mQMITransactionID,
                      QMIDev);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"ReadSync failed for bind mux port\n");
      return result;
   }
   kfree(pReadBuffer);

   // set ip family pref as IPV4
   writeBufferSize = sizeof( sQMUX ) + 11;
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
       QC_LOG_ERR(QMIDev,"Mem alloc failed for set ip famuly pref\n");
       return -ENOMEM;
   }

   result = QMIWDSSetIPFamilyPrefReq( pWriteBuffer,
         writeBufferSize,
         GetTransactionID(QMIDev),
         0x04);
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Request setup failed for ip family pref\n");
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
       QC_LOG_ERR(QMIDev,"set ip family pref Failed\n");
       return result;
   }

   result = ReadSync( pDev,
                      &pReadBuffer,
                      id,
                      QMIDev->mQMITransactionID,
                      QMIDev);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"ReadSync failed for set ip family pref\n");
      return result;
   }
   kfree(pReadBuffer);
   // start the network
   writeBufferSize = QMIWDSStartNetworkReqSize();
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
       QC_LOG_ERR(QMIDev,"Mem alloc failed\n");
       return -ENOMEM;
   }

   result = QMIWDSStartNetworkReq( pWriteBuffer,
         writeBufferSize,
         GetTransactionID(QMIDev), profileid);
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Request setup failed\n");
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
       QC_LOG_ERR(QMIDev,"Connect Failed\n");
       return result;
   }

   result = ReadSync( pDev,
                      &pReadBuffer,
                      id,
                      QMIDev->mQMITransactionID,
                      QMIDev);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"ReadSync failed\n");
      return result;
   }
   readBufferSize = result;
   result = QMIWDSStartNetworkResp( pReadBuffer,
                               readBufferSize,
                               res_wds,
                               4);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"QMIWDSStartNetworkResp failed\n");
   }

   *connectid = *(u32 *)res_wds;
   kfree( pReadBuffer );

   // Get IP Address Settings:
   writeBufferSize = QMIWDSGetRuntimeSettingsReqSize();
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
       QC_LOG_ERR(QMIDev,"Mem alloc failed\n");
       return -ENOMEM;
   }

   result = QMIWDSGetRuntimeSettingsReq( pWriteBuffer,
                        writeBufferSize,
                        GetTransactionID(QMIDev));
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Preparing Request Runttime settings failed\n");
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
       QC_LOG_ERR(QMIDev,"Request runtime settings Failed\n");
       return result;
   }

   result = ReadSync( pDev,
                      &pReadBuffer,
                      id,
                      QMIDev->mQMITransactionID,
                      QMIDev);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"ReadSync failed\n");
      return result;
   }
   readBufferSize = result;
   result = QMIWDSGetRuntimeSettingsResp( pDev, pReadBuffer,
                               readBufferSize,
                               QMIDev);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"QMIWDSGetRuntimeSettingsResp failed\n");
   }

   kfree( pReadBuffer );

   // get client id
   ipv6ID = GetClientID( pDev, QMIWDS, QMIDev );

   if (ipv6ID < 0)
       QC_LOG_ERR(QMIDev,"Error getting client id\n");

   // negotiate MUX ID as 0x81
   writeBufferSize = sizeof( sQMUX ) + 29;
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
       QC_LOG_ERR(QMIDev,"Mem alloc failed for bind mux port\n");
       return -ENOMEM;
   }

   result = QMIWDSBindMuxPortReq( pWriteBuffer,
         writeBufferSize,
         GetTransactionID(QMIDev), pDev, QMIDev);
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Request setup failed for bind mux port\n");
       kfree( pWriteBuffer );
       return result;
   }

   result = WriteSync( pDev,
         pWriteBuffer,
         writeBufferSize,
         ipv6ID,
         QMIDev);
   kfree( pWriteBuffer );
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Bind mux port Failed\n");
       return result;
   }

   result = ReadSync( pDev,
                      &pReadBuffer,
                      ipv6ID,
                      QMIDev->mQMITransactionID,
                      QMIDev);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"ReadSync failed for bind mux port\n");
      return result;
   }
   kfree(pReadBuffer);
   // set ip family pref as IPV6
   writeBufferSize = sizeof( sQMUX ) + 11;
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
       QC_LOG_ERR(QMIDev,"Mem alloc failed for set ip famuly pref\n");
       return -ENOMEM;
   }

   result = QMIWDSSetIPFamilyPrefReq( pWriteBuffer,
         writeBufferSize,
         GetTransactionID(QMIDev), 0x06);
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Request setup failed for ip family pref\n");
       kfree( pWriteBuffer );
       return result;
   }

   result = WriteSync( pDev,
         pWriteBuffer,
         writeBufferSize,
         ipv6ID,
         QMIDev);

   kfree( pWriteBuffer );
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"set ip family pref Failed\n");
       return result;
   }

   result = ReadSync( pDev,
                      &pReadBuffer,
                      ipv6ID,
                      QMIDev->mQMITransactionID,
                      QMIDev);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"ReadSync failed for set ip family pref\n");
      return result;
   }
   kfree(pReadBuffer);
   // start the network
   writeBufferSize = QMIWDSStartNetworkReqSize();
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
       QC_LOG_ERR(QMIDev,"Mem alloc failed\n");
       return -ENOMEM;
   }

   result = QMIWDSStartNetworkReq( pWriteBuffer,
         writeBufferSize,
         GetTransactionID(QMIDev), profileid);
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Request setup failed\n");
       kfree( pWriteBuffer );
       return result;
   }

   result = WriteSync( pDev,
         pWriteBuffer,
         writeBufferSize,
         ipv6ID,
         QMIDev);
   kfree( pWriteBuffer );
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Connect Failed\n");
       return result;
   }

   result = ReadSync( pDev,
                      &pReadBuffer,
                      ipv6ID,
                      QMIDev->mQMITransactionID,
                      QMIDev);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"ReadSync failed\n");
      return result;
   }
   readBufferSize = result;
   result = QMIWDSStartNetworkResp( pReadBuffer,
                               readBufferSize,
                               res_wds,
                               4);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"QMIWDSStartNetworkResp failed\n");
   }

   *connectipv6id = *(u32 *)res_wds;

   kfree( pReadBuffer );

   // Get IP Address Settings:
   writeBufferSize = QMIWDSGetRuntimeSettingsReqSize();
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
       QC_LOG_ERR(QMIDev,"Mem alloc failed\n");
       return -ENOMEM;
   }

   result = QMIWDSGetRuntimeSettingsReq( pWriteBuffer,
                        writeBufferSize,
                        GetTransactionID(QMIDev));
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Preparing Request Runttime settings failed\n");
       kfree( pWriteBuffer );
       return result;
   }

   result = WriteSync( pDev,
                        pWriteBuffer,
                        writeBufferSize,
                        ipv6ID,
                        QMIDev);
   kfree( pWriteBuffer );
   if (result < 0)
   {
       QC_LOG_ERR(QMIDev,"Request runtime settings Failed\n");
       return result;
   }

   result = ReadSync( pDev,
                      &pReadBuffer,
                      ipv6ID,
                      QMIDev->mQMITransactionID,
                      QMIDev);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"ReadSync failed\n");
      return result;
   }
   readBufferSize = result;
   result = QMIWDSGetRuntimeSettingsResp( pDev, pReadBuffer,
                               readBufferSize,
                               QMIDev);
   if (result < 0)
   {
      QC_LOG_ERR(QMIDev,"QMIWDSGetRuntimeSettingsResp failed\n");
   }

   kfree( pReadBuffer );   
   *ipv6id = ipv6ID;
   
   return id;

}

void WDSDisConnect(int id, sGobiUSBNet *pDev, sQMIDev *QMIDev, int ipv6id, int connectid, int connectipv6id)
{
   int result;
   void * pWriteBuffer;
   u16 writeBufferSize;
   void * pReadBuffer;
   //u16 readBufferSize;
   if (IsDeviceValid(pDev) == false)
   {
      QC_LOG_ERR(QMIDev,"Invalid device!\n" );
      return;
   }

   writeBufferSize = sizeof( sQMUX ) + 14;
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
      QC_LOG_ERR(QMIDev,"Mem alloc failed for set ip famuly pref\n");
      return;
   }
    
   result = QMIWDSStopNetworkReq( pWriteBuffer,
          writeBufferSize,
          GetTransactionID(QMIDev), connectid);
    if (result < 0)
    {
        QC_LOG_ERR(QMIDev,"Request setup failed for ip family pref\n");
        kfree( pWriteBuffer );
        return;
    }
    
    result = WriteSync( pDev,
          pWriteBuffer,
          writeBufferSize,
          id,
          QMIDev);
    kfree( pWriteBuffer );
    if (result < 0)
    {
        QC_LOG_ERR(QMIDev,"set ip family pref Failed\n");
        return;
    }
    
    result = ReadSync( pDev,
                       &pReadBuffer,
                       id,
                       QMIDev->mQMITransactionID,
                       QMIDev);
    if (result < 0)
    {
       QC_LOG_ERR(QMIDev,"ReadSync failed for set ip family pref\n");
       return;
    }
    kfree( pReadBuffer );   

   writeBufferSize = sizeof( sQMUX ) + 14;
   pWriteBuffer = kzalloc( writeBufferSize, GFP_KERNEL );
   if (pWriteBuffer == NULL)
   {
      QC_LOG_ERR(QMIDev,"Mem alloc failed for set ip family pref\n");
      return;
   }
    
   result = QMIWDSStopNetworkReq( pWriteBuffer,
          writeBufferSize,
          GetTransactionID(QMIDev), connectipv6id);
    if (result < 0)
    {
        QC_LOG_ERR(QMIDev,"Request setup failed for ip family pref\n");
        kfree( pWriteBuffer );
        return;
    }
    
    result = WriteSync( pDev,
          pWriteBuffer,
          writeBufferSize,
          ipv6id,
          QMIDev);
    kfree( pWriteBuffer );
    if (result < 0)
    {
        QC_LOG_ERR(QMIDev,"set ip family pref Failed\n");
        return;
    }
    
    result = ReadSync( pDev,
                       &pReadBuffer,
                       ipv6id,
                       QMIDev->mQMITransactionID,
                       QMIDev);
    if (result < 0)
    {
       QC_LOG_ERR(QMIDev,"ReadSync failed for set ip family pref\n");
       return;
    }
    kfree( pReadBuffer );   

   ReleaseClientID( pDev, id, QMIDev );
   ReleaseClientID( pDev, ipv6id, QMIDev );
}
