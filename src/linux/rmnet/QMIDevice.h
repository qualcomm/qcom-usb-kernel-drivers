/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once

#include "Structs.h"
#include "QMI.h"
#include "qtiDevInf.h"
#include "../version.h"
#include <linux/rtnetlink.h>

/*=========================================================================*/
// Generic functions
/*=========================================================================*/

// Basic test to see if device memory is valid
bool IsDeviceValid( sGobiUSBNet * pDev );

// Print Hex data, for debug purposes
void PrintHex(
   const void *   pBuffer,
   u16            bufSize);
void PrintHex_RX(
   void *         pBuffer,
   u16            bufSize );


// Sets mDownReason and turns carrier off
void GobiSetDownReason(
   sGobiUSBNet *    pDev,
   u8                 reason );

// Clear mDownReason and may turn carrier on
void GobiClearDownReason(
   sGobiUSBNet *    pDev,
   u8                 reason );

// Tests mDownReason and returns whether reason is set
bool GobiTestDownReason(
   sGobiUSBNet *    pDev,
   u8                 reason );

/*=========================================================================*/
// Driver level asynchronous read functions
/*=========================================================================*/

// Resubmit interrupt URB, re-using same values
int ResubmitIntURB( struct urb * pIntURB );

// Read callback
//    Put the data in storage and notify anyone waiting for data
void ReadCallback( struct urb * pReadURB );

// Inturrupt callback
//    Data is available, start a read URB
void IntCallback( struct urb * pIntURB );

// Start continuous read "thread"
int StartRead( sGobiUSBNet * pDev );

// Kill continuous read "thread"
void KillRead( sGobiUSBNet * pDev );

/*=========================================================================*/
// Internal read/write functions
/*=========================================================================*/

// Start asynchronous read
//     Reading client's data store, not device
int ReadAsync(
   sGobiUSBNet *    pDev,
   u16                clientID,
   u16                transactionID,
   void               (*pCallback)(sGobiUSBNet *, u16, void *, sQMIDev *),
   void *             pData,
   sQMIDev *QMIDev);

// Notification function for synchronous read
void UpSem( 
   sGobiUSBNet *    pDev,
   u16                clientID,
   void *             pData,
   sQMIDev *QMIDev );

// Start synchronous read
//     Reading client's data store, not device
int ReadSync(
   sGobiUSBNet *    pDev,
   void **            ppOutBuffer,
   u16                clientID,
   u16                transactionID,
   sQMIDev *QMIDev );

// Start synchronous read with timeout
//     Reading client's data store, not device
int ReadSyncTimeout(
   sGobiUSBNet *    pDev,
   void **            ppOutBuffer,
   u16                clientID,
   u16                transactionID,
   sQMIDev *QMIDev );


// Write callback
void WriteSyncCallback( struct urb * pWriteURB );

// Start synchronous write
int WriteSync(
   sGobiUSBNet *    pDev,
   char *             pInWriteBuffer,
   int                size,
   u16                clientID,
   sQMIDev *QMIDev );

/*=========================================================================*/
// Internal memory management functions
/*=========================================================================*/

// Create client and allocate memory
int GetClientID( 
   sGobiUSBNet *      pDev,
   u8                 serviceType,
   sQMIDev *          QMIDev );

// Release client and free memory
void ReleaseClientID(
   sGobiUSBNet *      pDev,
   u16                  clientID,
   sQMIDev *QMIDev );

// Find this client's memory
sClientMemList * FindClientMem(
   sGobiUSBNet *      pDev,
   u16                  clientID,
   sQMIDev *QMIDev );

// Add Data to this client's ReadMem list
bool AddToReadMemList( 
   sGobiUSBNet *      pDev,
   u16                  clientID,
   u16                  transactionID,
   void *               pData,
   u16                  dataSize,
   sQMIDev *QMIDev );

// Remove data from this client's ReadMem list if it matches 
// the specified transaction ID.
bool PopFromReadMemList( 
   sGobiUSBNet *      pDev,
   u16                  clientID,
   u16                  transactionID,
   void **              ppData,
   u16 *                pDataSize,
   sQMIDev *QMIDev );

// Add Notify entry to this client's notify List
bool AddToNotifyList( 
   sGobiUSBNet *      pDev,
   u16                  clientID,
   u16                  transactionID,
   void                 (* pNotifyFunct)(sGobiUSBNet *, u16, void *, sQMIDev *QMIDev),
   void *               pData,
   sQMIDev *QMIDev );

// Remove first Notify entry from this client's notify list 
//    and Run function
bool NotifyAndPopNotifyList( 
   sGobiUSBNet *      pDev,
   u16                  clientID,
   u16                  transactionID,
   sQMIDev *QMIDev );

// Add URB to this client's URB list
bool AddToURBList( 
   sGobiUSBNet *      pDev,
   u16                  clientID,
   struct urb *         pURB,
   sQMIDev *QMIDev );

// Remove URB from this client's URB list
struct urb * PopFromURBList( 
   sGobiUSBNet *      pDev,
   u16                  clientID,
   sQMIDev *QMIDev );

/*=========================================================================*/
// Userspace wrappers
/*=========================================================================*/

// Userspace open
int UserspaceOpen( 
   struct inode *   pInode, 
   struct file *    pFilp );

// Userspace ioctl
int UserspaceIOCTL( 
   struct inode *    pUnusedInode, 
   struct file *     pFilp,
   unsigned int      cmd, 
   unsigned long     arg );

// Userspace close
int UserspaceClose( 
   struct file *       pFilp,
   fl_owner_t          unusedFileTable );

// Userspace read (synchronous)
ssize_t UserspaceRead( 
   struct file *        pFilp,
   char __user *        pBuf, 
   size_t               size,
   loff_t *             pUnusedFpos );

// Userspace write (synchronous)
ssize_t UserspaceWrite(
   struct file *        pFilp, 
   const char __user *  pBuf, 
   size_t               size,
   loff_t *             pUnusedFpos );

unsigned int UserspacePoll(
   struct file *                  pFilp,
   struct poll_table_struct *     pPollTable );

/*=========================================================================*/
// Initializer and destructor
/*=========================================================================*/

// QMI Device initialization function
int RegisterQMIDevice( sGobiUSBNet * pDev );

// QMI Device cleanup function
void DeregisterQMIDevice( sGobiUSBNet * pDev );

int sysfs_entry(sGobiUSBNet * dev, char *device_name);
void sysfs_destroy(sGobiUSBNet * dev);

/*=========================================================================*/
// Driver level client management
/*=========================================================================*/

// Check if QMI is ready for use
bool QMIReady(
   sGobiUSBNet *    pDev,
   u32                timeout );

// QMI WDS callback function
void QMIWDSCallback(
   sGobiUSBNet *    pDev,
   u16                clientID,
   void *             pData,
   sQMIDev *QMIDev);

// Fire off reqests and start async read for QMI WDS callback
int SetupQMIWDSCallback( sGobiUSBNet * pDev, sQMIDev *QMIDev );

// Register client, send req and parse MEID response, release client
int QMIDMSGetMEID( sGobiUSBNet * pDev, sQMIDev *QMIDev );

// Register client, Set QMAP req and parse response, release client
int QMIWDASetQMAP( sGobiUSBNet * pDev , u16 ClientID, sQMIDev *QMIDev);
int QMIWDASetQMAPSettings( sGobiUSBNet * pDev , u16 ClientID, sQMIDev *QMIDev);

int WDSConnect(sGobiUSBNet *pDev, sQMIDev *QMIDev, u8 profileid, int *ipv6id, u32 *connectid, u32 *connectipv6id);
void WDSDisConnect(int id, sGobiUSBNet *pDev, sQMIDev *QMIDev, int ipv6id, int connectid, int connectipv6id);

