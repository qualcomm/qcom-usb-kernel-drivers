/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef QDSSDIAG_H
#define QDSSDIAG_H

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/uio.h>
#include <linux/aio.h>
#include <linux/kthread.h>
#include <linux/mmu_context.h>

#include <linux/cdev.h>

#include <linux/list.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,25 ))
   #include <linux/fdtable.h>
#else
   #include <linux/file.h>
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,13,16))
#include <linux/sched.h>
#else
#include <linux/sched/signal.h>
#endif

// #define QCUSB_TEST_ONLY

#include "qtiEvt.h"
#include "qtiDevInf.h"

#define BULK_URB_LIST            4
#define QTIDEV_TX_TIMEOUT        2000   // in milliseconds
#define QTIDEV_RX_TIMEOUT        2000  // in milliseconds
#define QTIDEV_BULK_BUF_LEN      1024*1024*3 // 4000000//1024*1024*3
#define QTIDEV_RETRY             3
#define QTIDEV_SLEEP_TIMER       2 // in milliseconds

#ifdef QCUSB_TEST_ONLY
#define QTIDEV_RX_SIZE           1024*512
#define QTIDEV_TX_SIZE           1024*128
#else
#define QTIDEV_RX_SIZE           1024*128
#define QTIDEV_TX_SIZE           1024*128
#endif

#define QTIDEV_RX_NOTIFY_POOL_SZ 32
#define QTIDEV_TX_BUF_POOL_SZ    32
#define QTIDEV_DRIVER_NAME       "QTIDEV_QDSS_DPL_DIAG_Subsystem"
#define QTIDEV_USB_CLASS_NAME    "GobiUSB"
#define QTIDEV_PORT_CLASS_NAME   "GobiPorts"
//#define QTIDEV_SERIAL_CLASS_NAME   "GobiSerial"

#define BULK_URB_INITIALIZED    (1 << 0)
#define BULK_URB_SUBMITTED      (1 << 1)
#define IS_URB_INITIALIZED(status)  (status & BULK_URB_INITIALIZED)
#define IS_URB_SUBMITTED(status)    (status & BULK_URB_SUBMITTED)

#define DEFAULT_READ_URB_LENGTH 0x1000
#define GOBI_SER_DTR       0x01
#define GOBI_SER_RTS       0x02

#ifdef QTIDEV_TIMEOUT
#define IOCTL_QTIDEV_SET_TIMEOUT      0x1001
#define IOCTL_QTIDEV_KILL_READ        0x1002
#define IOCTL_QTIDEV_READ_STATUS      0x1003
#define IOCTL_QTIDEV_GET_ASYNC_READ   0x1004
#endif
#ifdef QCUSB_TEST_ONLY
#define IOCTL_QTIDEV_SESSION_TOTAL    0x1005
#endif

#define QTIDEV_QTIOM_VID      0x05C6
#ifdef QCUSB_TEST_ONLY
#define QTIDEV_LPC_PID       0x9680
#define QTIDEV_LPC_IFACE_NUM 0
#define QTIDEV_LPC_DEV_NAME  "QTI_LPC_DEVICE"
#endif

#define QDSS_DIAG_MERGE
#define QDSS_INT_BULK_BUFFER

typedef enum eRxType
{
    QTI_RX_AIO = 0,
    QTI_RX_MEM,
    QTI_RX_ITER
} eRxType;

typedef struct qtidev_stats
{
   long RxCount;
   long TxCount;
   long ToUsrCnt;
   long USBRxCnt;
} sQTIDevStats;

typedef struct qtidev_tx_buf
{
    struct usb_anchor mTxAnchor; // for tracking/cancelling
    struct urb *mpTxUrb;   // URB
    void       *mpTxBuf;   // buffer
    size_t     mTxBufSize; // buffer size
    void       *mpTxSem;   // signal
    void       *mpDev;     // device
    int        mIndex;     // for tracking
} sQTIDevTxBuf;

typedef enum eTimerReadType
{
    QTI_DIAG_QDSS_SYNC_READ = 0,
    QTI_DIAG_QDSS_ASYNC_READ,
    QTI_DIAG_QDSS_MAX_READ_TYPE,
} eTimerReadType;

typedef enum eTimerReadStatus
{
    QTI_DIAG_QDSS_READ_IN_PROGRESS = 0,
    QTI_DIAG_QDSS_READ_SUCCESS,
    QTI_DIAG_QDSS_READ_FAILED,
    QTI_DIAG_QDSS_MAX_READ_STATUS,
} eTimerReadStatus;

struct sQTIDevUSB;
typedef struct qtidev_aio_data
{
    bool    aio;
    bool    read;
    //bool    toCancel;
    int     len;
    char    *buf;
    void    *userData;
    struct kref         mRefCount;
    struct sQTIDevUSB    *pDev;
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
    unsigned long       mActualLen;
    void* (*complete)(struct kiocb *kiocb, void *userData);
    struct aio_data_ctx *mpNext;
} sIoData;

typedef struct sIoReadList
{
    struct kiocb        *kiocb;
    struct list_head	node;
} sIoReadList;

typedef struct sBulkUrbList
{
    void                *Context;
    int                 mIndex;
    struct urb          *mBulk_in_urb;      /* the urb to read data with */
    unsigned char       *mBulk_in_buffer;   /* the buffer to receive data */
    __u8                mUrbStatus;         /* Status of urb            */
    struct list_head	node;
} sBulkUrbList;

#define QTI_RX_EVT_CANCEL 1
#define QTI_RX_EVT_FILL   2

typedef struct sNotifyList
{
    /* Function to be run when data becomes available */
    void                (*mpNotifyFunct)(struct sQTIDevUSB*, void *, int);
    /* Data to provide as parameter to mpNotifyFunct */
    void                *mpData;
    sQtiEvent           mEvent;
    /* User request size */
    size_t              mUsrReqSize;
    /* Index for tracking */
    int                 mIndex;
    /* status of being pricessed */
    int                 mInProcess;
    /* Next entry in linked list */
    struct sNotifyList  *mpNext;
} sNotifyList;

typedef struct sReadMemList
{
    unsigned char       *mBulk_in_buffer;    /* the buffer to receive data */
    int                  mDatalen;
    struct sReadMemList *mpNext;
} sReadMemList;

#ifdef QDSS_INT_BULK_BUFFER
typedef struct sReadMemListCircular
{
    unsigned char        *mBulk_in_buffer; /* the buffer to receive */
    unsigned int         mStartIdx;
    unsigned int         mEndIdx;
    unsigned int         mBufflen;
    bool                 mBufEmptyStatus;
} sReadMemListCircular;

#endif

typedef struct sBulkMemList
{
    //sReadMemList        *mpList;
    sReadMemListCircular mReadBuffCircular;
    sNotifyList         *mpReadNotifyList;
    /* Wait queue object for poll() */
    spinlock_t           mReadMemLock;       /* lock for I/O operations */
    wait_queue_head_t    mWaitQueue;
    //struct sBulkMemList *mpNext;
} sBulkMemList;

typedef struct sDevinfInfo {
    deviceClass         mClassType;     /* INF/Config file class  */
    struct class        *mpDevClass;    /* Store the class */
    devInfo_t           mDevInfInfo;    /* Device info got from INF parser */
}sDevinfInfo;

/* Structure to hold all of our device specific stuff */
typedef struct sQTIDevUSB {
    struct list_head	node;
    struct usb_device   *udev;		    /* the usb device for this device */
    struct usb_interface    *interface;     /* the interface for this device */
    struct usb_anchor   submitted;      /* in case we need to retract our submissions */
    struct urb *         mpIntURB;    /* Interrupt URB */
    void *               mpIntBuffer;  /* Buffer used by Interrupt URB */

    sNotifyList         mRxNotifyPool[QTIDEV_RX_NOTIFY_POOL_SZ];
    sQTIDevTxBuf        mTxBufferPool[QTIDEV_TX_BUF_POOL_SZ];
    sBulkUrbList        mBulkUrbList[BULK_URB_LIST];
    struct list_head	mBulkUrbBuffList;	/* list head for URB buffer */
    size_t		mBulkUrbBuffListSize;	/* active and valid URB buffer count */
    sBulkMemList        mBulkMemList;
    sIoReadList		mIoReadList[BULK_URB_LIST];
    struct list_head	mIoReadBuffList;    /* list head for IO Read LinkedList */

    struct list_head	mIoReadBuffListActive;    /* list head for IO Read LinkedList */
    size_t		mIoReadBuffListActiveSize;/* Active Read IO requests at an instance */
    struct list_head	mIoReadBuffListIdle;    /* list head for IO Read LinkedList */
    size_t		mIoReadBuffListIdleSize;/* Active Read IO requests at an instance */

    spinlock_t		mSpinReadBuffLock;
     
    unsigned            mIntfNum;   /* Interface number */
    unsigned            mIntInEndp;  /* Interrupt in endpoint */
    unsigned            mIntInEndpMaxPacketSize;  /* Interrupt in endpoint max buff size*/
    unsigned            mIntErrCnt;

    size_t              mBulkInSize;
    size_t              mBulkOutSize;
    size_t              mBulkInEndAddr;
    size_t              mBulkOutEndAddr;
    size_t              mLasterror;
    spinlock_t          mSpinErrLock;
    struct kref         mRefCount;
    struct mutex        mIoMutex;
    struct cdev         mCdev;          /* cdev struct */
    dev_t               mDevNum;        /* Device number */
    sDevinfInfo         mDevInfo;       /* Device info */
    unsigned int        mTimeout;       /* read timeout */
    __u8		        disconnected;
#ifdef QCUSB_TEST_ONLY
    __u8                mLpcRead;
    long long           mSessionTotal;
    spinlock_t          mTestLock;      /* lock for test IO */
#endif
    struct workqueue_struct *mpWorkQ;   /* Work queue */
    sQTIDevStats        mStats;
    struct kobject      *kobj_qdss; 
    int                 debug;
    char                pName[255]; 
    char                fqDevName[255]; /* Fully qualified Device node Name*/
} sQTIDevUSB;


// Note: Use QC_KZALLOC macro for tracking memory address and allocated size. Useful to find memory leaks
#define QC_KZALLOC(DEV_CONTEXT, size, flags, args...) QDSS_KZALLOC(DEV_CONTEXT, size, flags, __FUNCTION__, __LINE__)

static inline __attribute__((always_inline)) void *QDSS_KZALLOC(sQTIDevUSB *DEV_CONTEXT, size_t size, gfp_t flag, const char *func, int lineNo)
{
    void* PTR = qti_kmalloc(size, flag);
    if(DEV_CONTEXT->debug == 1 && PTR != NULL)
    {
        printk( KERN_INFO "%s %s:%d Qdss_kzallocAddr = 0x%px and ksize = %ld ", DEV_CONTEXT->mDevInfo.mDevInfInfo.mpKey, func, lineNo, PTR, ksize(PTR));
    }
    else if (DEV_CONTEXT == NULL && PTR != NULL)
    {
        printk( KERN_INFO "%s:%d Qdss_kzallocAddr = 0x%px and ksize = %ld ", func, lineNo, PTR, ksize(PTR));
    }
    else
    {
        printk( KERN_ERR "%s:%d PTR is NULL." ,func, lineNo);
    }

    return PTR;
}

// Note: Replace kfree API with QC_KFREE for tracking memory address and allocated size.
#define QC_KFREE(DEV_CONTEXT, PTR, args...) QDSS_KFREE(DEV_CONTEXT, PTR, __FUNCTION__, __LINE__)

static inline __attribute__((always_inline)) void QDSS_KFREE(sQTIDevUSB *DEV_CONTEXT, void *PTR, const char *func, int lineNo)
{
    if(DEV_CONTEXT->debug == 1 && PTR != NULL)
    {
        printk( KERN_INFO "%s %s:%d Qdss_kfreeAddr = 0x%px and ksize = %ld", DEV_CONTEXT->mDevInfo.mDevInfInfo.mpKey, func, lineNo, PTR, ksize(PTR));
    }
    else if (DEV_CONTEXT == NULL && PTR != NULL)
    {
        printk( KERN_INFO "%s:%d Qdss_kfreeAddr = 0x%px and ksize = %ld ", func, lineNo, PTR, ksize(PTR));
    }
    else
    {
        printk( KERN_ERR "%s:%d PTR is NULL." , func, lineNo);
    }
    qti_kfree(PTR);
}

#endif
