/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef STRUCTS_H
#define STRUCTS_H

#pragma once

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/version.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/list.h>
// For Aio
#include <linux/poll.h>
#include <linux/uio.h>
#include <linux/aio.h>
#include <linux/kthread.h>
#include <linux/mmu_context.h>
#include <linux/usb/cdc.h>
#include <linux/proc_fs.h>  /* Necessary because we use the proc fs */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0))
#include <linux/sched.h>
#else
#include <linux/sched/signal.h>
#endif

#define MAX_MUX_DEVICES 3
#define QMIDEV_MAX_VALUE_NAME 256            /**< Maximum length of string */

#define MIN_PACKET_SIZE 100
#define SKB_TAIL_ROOM   4

#define UL_AGGREGATION_MAX_SIZE      0x7C00
#define UL_AGGREGATION_MAX_DATAGRAMS   0x20
#define DL_AGGREGATION_MAX_SIZE      0x7C00
#define DL_AGGREGATION_MAX_DATAGRAMS   0x20
#define RX_URB_SIZE                  DL_AGGREGATION_MAX_SIZE

#define READ_TIMEOUT 60*30 //(30 seconds)    
#define WRITE_TIMEOUT 60*30 //(30 seconds)    

/* Restart the timer, if amount of datagrams is less than given value */
#define	CDC_NCM_RESTART_TIMER_DATAGRAM_CNT	3
#define	CDC_NCM_TIMER_PENDING_CNT		2
#define CDC_NCM_TIMER_INTERVAL_MAX		(U32_MAX / NSEC_PER_MSEC)

#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,24 ))
   #include "usbnet.h"
#else
   #include <linux/usb/usbnet.h>
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,25 ))
   #include <linux/fdtable.h>
#else
   #include <linux/file.h>
#endif

#include "qtiDevInf.h"

// Used in recursion, defined later below
struct sGobiUSBNet;

struct sQMIDev;

typedef struct ipv6_addr
{
   u8         ipv6addr[16];
   u8         prefix;
}__attribute__((__packed__)) ipv6_addr;


/*=========================================================================*/
// Struct sReadMemList
//
//    Structure that defines an entry in a Read Memory linked list
/*=========================================================================*/
typedef struct sReadMemList
{
   /* Data buffer */
   void *                     mpData;
   
   /* Transaction ID */
   u16                        mTransactionID;

   /* Size of data buffer */
   u16                        mDataSize;

   /* Next entry in linked list */
   struct list_head   node;

} sReadMemList;

/*=========================================================================*/
// Struct sNotifyList
//
//    Structure that defines an entry in a Notification linked list
/*=========================================================================*/
typedef struct sNotifyList
{
   /* Function to be run when data becomes available */
   void                  (* mpNotifyFunct)(struct sGobiUSBNet *, u16, void *, struct sQMIDev *);
   
   /* Transaction ID */
   u16                   mTransactionID;

   /* Data to provide as parameter to mpNotifyFunct */
   void *                mpData;
   
   /* Next entry in linked list */
   struct list_head   node;

} sNotifyList;

/*=========================================================================*/
// Struct sURBList
//
//    Structure that defines an entry in a URB linked list
/*=========================================================================*/
typedef struct sURBList
{
   /* The current URB */
   struct urb *       mpURB;

   /* Next entry in linked list */
   struct list_head   node;

} sURBList;

/*=========================================================================*/
// Struct sClientMemList
//
//    Structure that defines an entry in a Client Memory linked list
//      Stores data specific to a Service Type and Client ID
/*=========================================================================*/
typedef struct sClientMemList
{
   /* Client ID for this Client */
   u16                          mClientID;

   /* Linked list of Read entries */
   /*    Stores data read from device before sending to client */
   sReadMemList *               mpList;

   /* list head for sReadMemList */
   struct list_head            mList;
   
   /* Linked list of Notification entries */
   /*    Stores notification functions to be run as data becomes 
         available or the device is removed */
   sNotifyList *                mpReadNotifyList;

   /* list head for sNotifyList */
   struct list_head            mReadNotifyList;

   /* Linked list of URB entries */
   /*    Stores pointers to outstanding URBs which need canceled 
         when the client is deregistered or the device is removed */
   sURBList *                   mpURBList;

   /* list head for sURBList */
   struct list_head            mURBList;
   
   /* Next entry in linked list */
   struct list_head   node;

   /* Wait queue object for poll() */
   wait_queue_head_t    mWaitQueue;

   //

} sClientMemList;

/*=========================================================================*/
// Struct sURBSetupPacket
//
//    Structure that defines a USB Setup packet for Control URBs
//    Taken from USB CDC specifications
/*=========================================================================*/
typedef struct sURBSetupPacket
{
   /* Request type */
   u8    mRequestType;

   /* Request code */
   u8    mRequestCode;

   /* Value */
   u16   mValue;

   /* Index */
   u16   mIndex;

   /* Length of Control URB */
   u16   mLength;

} sURBSetupPacket;

// Common value for sURBSetupPacket.mLength
#define DEFAULT_READ_URB_LENGTH 0x1000


/*=========================================================================*/
// Struct sAutoPM
//
//    Structure used to manage AutoPM thread which determines whether the
//    device is in use or may enter autosuspend.  Also submits net 
//    transmissions asynchronously.
/*=========================================================================*/
typedef struct sAutoPM
{
   struct list_head node;

   /* Thread for atomic autopm function */
   struct task_struct *       mpThread;

   /* Signal for completion when it's time for the thread to work */
   struct completion          mThreadDoWork;

   /* Time to exit? */
   bool                       mbExit;

   /* List of URB's queued to be sent to the device */
   sURBList *                 mpURBList;

   /* list head for sURBList */
   struct list_head            mURBList;

   /* URB list lock (for adding and removing elements) */
   spinlock_t                 mURBListLock;

   /* Length of the URB list */
   atomic_t                   mURBListLen;
   
   /* Active URB */
   struct urb *               mpActiveURB;

   /* Active URB lock (for adding and removing elements) */
   spinlock_t                 mActiveURBLock;
   
   /* Duplicate pointer to USB device interface */
   struct usb_interface *     mpIntf;

} sAutoPM;

/*=========================================================================*/
// Struct sQMIDev
//
//    Structure that defines the data for the QMI device
/*=========================================================================*/
typedef struct sQMIDev
{
   struct list_head   node;
   
   /* Device number */
   dev_t                      mDevNum;

   /* Device class */
   struct class *             mpDevClass;

   /* cdev struct */
   struct cdev                mCdev;

   /* is mCdev initialized? */
   bool                       mbCdevIsInitialized;

   /* Pointer to read URB */
   struct urb *               mpReadURB;

   /* Read setup packet */
   sURBSetupPacket *          mpReadSetupPacket;

   /* Read buffer attached to current read URB */
   void *                     mpReadBuffer;
   
   /* Inturrupt URB */
   /*    Used to asynchronously notify when read data is available */
   struct urb *               mpIntURB;

   /* Buffer used by Inturrupt URB */
   void *                     mpIntBuffer;
   
   /* Pointer to memory linked list for all clients */
   sClientMemList *           mpClientMemList;

   /* list head for ClientMemList */
   struct list_head            mClientMemList;
   
   /* Spinlock for client Memory entries */
   spinlock_t                 mClientMemLock;

   /* Transaction ID associated with QMICTL "client" */
   atomic_t                   mQMICTLTransactionID;

   /* Transaction ID associated with QMI "client" */
   unsigned short             mQMITransactionID;

   /*MuxId*/
   unsigned short             MuxId;

   unsigned int               debug;

   unsigned int               logLevel;/*Adding to store log level for the adapter*/

   char                       mdeviceName[QMIDEV_MAX_VALUE_NAME];

   unsigned int               IPv4Addr;

   unsigned int               IPv4SubnetMask;

   unsigned int               IPv4Gateway;

   unsigned int               IPv4PrimaryDNS;

   unsigned int               IPv4SecondaryDNS;

   ipv6_addr                  ipv6_address;

   ipv6_addr                  ipv6_gateway;

   ipv6_addr                  ipv6_primaydns;

   ipv6_addr                  ipv6_secondarydns;

} sQMIDev;

/*=========================================================================*/
// Struct sProcessInd
//
//    Structure that holds the data for Indication
/*=========================================================================*/
typedef struct sIndDataInfo
{
    /* Pointer to the Response data */
    char                *mpData;

    /* Length of response data */
    int                 mDataLen;

    /* value of message Id  */
    int                 mMsgId; 

    /* Store the ClientId for WDS transactions */
    u16                 mClientId; 

    /* QMI "device" memory */
    sQMIDev             *mQMIDev;

    /* Pointer to the next */
    struct sIndDataInfo *mpNext;

} sIndDataInfo;

/*=========================================================================*/
// Struct sProcessInd
//
//    Structure that holds the data for Indication
/*=========================================================================*/
typedef struct sProcessInd
{
   /* Semaphore */
   struct semaphore     *mpResponseReadSem;

   /* Response list lock (for adding and removing elements) */
   spinlock_t           *mpResponeListLock;

   /* List of data to be addressed */
   sIndDataInfo         *mpIndDataInfoList;

} sProcessInd;

/*=========================================================================*/
// Struct sEndpoints
//
//    Structure that defines the endpoints of the device
/*=========================================================================*/
typedef struct sEndpoints
{
   /* Interface number */
   unsigned               mIntfNum;

   /* Interrupt in endpoint */
   unsigned               mIntInEndp;

   /* Interrupt in endpoint max buff size*/
   unsigned               mIntInEndpMaxPacketSize;

   /* Bulk in endpoint */
   unsigned               mBlkInEndp;

   /* Bulk out endpoint */
   unsigned               mBlkOutEndp;

} sEndpoints;

struct rm_cdc_ncm_ctx {

    struct hrtimer tx_timer;
    struct tasklet_struct bh;

    struct sk_buff *tx_curr_skb;
    struct sk_buff *tx_rem_skb;

    spinlock_t mtx;
    atomic_t stop;

    long long timer_interval;
    long long tx_timer_pending;

    u16 tx_max_datagrams;
    u32 tx_curr_frame_num;
    u32 tx_max;
    u32 tx_curr_size;
    u32 max_datagram_size;

    /* statistics */
    u32 tx_curr_frame_payload;
    u32 tx_reason_max_datagram;
    u64 tx_overhead;
};

typedef struct sDevinfInfo {
    deviceClass         mClassType;     /* INF/Config file class  */
    struct class        *mpDevClass;    /* Store the class */
    devInfo_t           mDevInfInfo;    /* Device info got from INF parser */
}sDevinfInfo;

/*=========================================================================*/
// Struct sGobiUSBNet
//
//    Structure that defines the data associated with the QTI USB device
/*=========================================================================*/
typedef struct sGobiUSBNet
{
#define TX_AGGR
//#define SEQNO_TESTING
#ifdef TX_AGGR
    struct rm_cdc_ncm_ctx tx_aggr_ctx;
#endif
   /* Device info */
   sDevinfInfo         mDevInfo;

   /* Net device structure */
   struct usbnet *        mpNetDev;
#define VIRTUAL_USB_CODE
#ifdef VIRTUAL_USB_CODE
   struct usbnet *        mpNetMUXDev[MAX_MUX_DEVICES];
#endif
   struct list_head   node;
   /* Usb device interface */
   struct usb_interface * mpIntf;

   /* Endpoint numbers */
   sEndpoints *           mpEndpoints;

   /* Pointers to usbnet_open and usbnet_stop functions */
   int                  (* mpUSBNetOpen)(struct net_device *);
   int                  (* mpUSBNetStop)(struct net_device *);

   /* Reason(s) why interface is down */
   /* Used by Gobi*DownReason */
   unsigned long          mDownReason;
#define NO_NDIS_CONNECTION    0
#define CDC_CONNECTION_SPEED  1
#define DRIVER_SUSPENDED      2
#define NET_IFACE_STOPPED     3

   /* QMI "device" status */
   bool                   mbQMIValid;

   /* QMI "device ready" status */
   bool                   mbQMIReadyStatus;

   /* QMI "device" memory */
   sQMIDev                mQMIDev;

   /* QMI "device" memory for MUXING*/
   sQMIDev                mQMIMUXDev[MAX_MUX_DEVICES];
   /* Device MEID */
   char                   mMEID[14];

   /* AutoPM thread */
   sAutoPM                mAutoPM;

   /*QMAP DL Aggregation information */
   unsigned int    DLAggregationMaxDatagram;
   unsigned int    DLAggregationMaxSize;
#ifdef TX_AGGR
   /*QMAP UL Aggregation information */
   unsigned int    ULAggregationMaxDatagram;
   unsigned int    ULAggregationMaxSize;
#endif
   /*WDS Connect/Disconnect kobj*/
   struct kobject *kobj_gobi;

   sProcessInd                mProcessIndData;

   struct workqueue_struct *mpWorkQ;   /* Work queue */

   struct task_struct     *pGobiWaitThread;

   spinlock_t             responeListLock;
   
   struct semaphore       responseReadSem;

} sGobiUSBNet;

/*=========================================================================*/
// Struct sQMIFilpStorage
//
//    Structure that defines the storage each file handle contains
//       Relates the file handle to a client
/*=========================================================================*/
typedef struct sQMIFilpStorage
{  
   /* Client ID */
   u16                  mClientID;

   /* Device pointer */
   sGobiUSBNet *        mpDev;
   
   /* QMIDevice */
   sQMIDev *QMIDev;

} sQMIFilpStorage;

typedef struct qtidev_aio_data
{
    bool    aio;
    bool    read;
    //bool    toCancel;
    int     len;
    char    *buf;
    void    *userData;
    sGobiUSBNet    *pDev;
    struct urb          *urb;
    const void          *to_free;
    struct kiocb        *kiocb;
    struct iov_iter     data;
    struct semaphore    readSem;
    struct mm_struct    *mm;
    struct work_struct  work;
    struct work_struct  cancellation_work;
    struct work_struct  submit_work;
    void                *mpData;
    unsigned long       mDataLen;
    void* (*complete)(struct kiocb *kiocb, void *userData);
    struct aio_data_ctx *mpNext;
} sIoData;

#define GET_QMIDEV(DEV_CONTEXT)  _GET_QMIDEV(DEV_CONTEXT)

static inline __attribute__((always_inline)) sQMIDev * _GET_QMIDEV(sGobiUSBNet * DEV_CONTEXT)
{
    if(DEV_CONTEXT==NULL) 
        return NULL;
    else 
        return ((sQMIDev *)&(DEV_CONTEXT->mQMIDev));
}

#define GET_QMIMUXDEV(DEV_CONTEXT, i)  _GET_QMIMUXDEV(DEV_CONTEXT, i)

static inline __attribute__((always_inline)) sQMIDev * _GET_QMIMUXDEV(sGobiUSBNet * DEV_CONTEXT, int i)
{
    if(DEV_CONTEXT==NULL) 
        return NULL;
    else 
        return ((sQMIDev *)&(DEV_CONTEXT->mQMIMUXDev[i]));
}

#define GET_QMIDEV_QMIFILP(DEV_CONTEXT)  _GET_QMIDEV_QMIFILP(DEV_CONTEXT)

static inline __attribute__((always_inline)) sQMIDev * _GET_QMIDEV_QMIFILP(sQMIFilpStorage * DEV_CONTEXT)
{
    if(DEV_CONTEXT==NULL) 
        return NULL;
    else 
        return ((sQMIDev *)(DEV_CONTEXT->QMIDev));
}

// Note: Use QC_KZALLOC macro for tracking memory address and allocated size. Useful to find memory leaks
#define QC_KZALLOC(DEV_CONTEXT, size, flags, args...) RMNET_KZALLOC(DEV_CONTEXT, size, flags, __FUNCTION__, __LINE__)

static inline __attribute__((always_inline)) void *RMNET_KZALLOC(sQMIDev *DEV_CONTEXT, size_t size, gfp_t flag, const char *func, int lineNo)
{
    void* PTR = kzalloc(size, flag);
    if((DEV_CONTEXT->debug == 1 || DEV_CONTEXT->debug == 0xFF) && (PTR != NULL))
    {
        printk( KERN_INFO "%s %s:%d rmnet_kzallocAddr = 0x%px and ksize = %ld ", DEV_CONTEXT->mdeviceName, func, lineNo, PTR, ksize(PTR));
    }
    else if (DEV_CONTEXT == NULL && PTR != NULL)
    {
        printk( KERN_INFO "%s:%d rmnet_kzallocAddr = 0x%px and ksize = %ld ", func, lineNo, PTR, ksize(PTR));
    }
    else 
    {
        printk( KERN_ERR "%s:%d PTR is NULL." ,func, lineNo);
    }

    return PTR;
}

// Note: Replace kfree API with QC_KFREE for tracking memory address and allocated size.
#define QC_KFREE(DEV_CONTEXT, PTR, args...) RMNET_KFREE(DEV_CONTEXT, PTR, __FUNCTION__, __LINE__)

static inline __attribute__((always_inline)) void RMNET_KFREE(sQMIDev *DEV_CONTEXT, void *PTR, const char *func, int lineNo)
{
    if((DEV_CONTEXT->debug == 1 || DEV_CONTEXT->debug == 0xFF) && (PTR != NULL))
    {
        printk( KERN_INFO "%s %s:%d rmnet_kfreeAddr = 0x%px and ksize = %ld", DEV_CONTEXT->mdeviceName, func, lineNo, PTR, ksize(PTR));
    }
    else if (DEV_CONTEXT == NULL && PTR != NULL)
    {
        printk( KERN_INFO "%s:%d rmnet_kfreeAddr = 0x%px and ksize = %ld ", func, lineNo, PTR, ksize(PTR));
    }
    else
    {
        printk( KERN_ERR "%s:%d PTR is NULL." , func, lineNo);
    }
    kfree(PTR);
}


#endif
