/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "qtiDiag.h"
#include "../version.h"
#include <linux/poll.h>
#include <linux/vmalloc.h>

#define DRIVER_DESC "QTIDebugSubsystemUSB"
#define QDSS_INF_PATH "/opt/QTI/QUD/diag/qdbusb.inf"
#define DIAG_INF_PATH "/opt/QTI/QUD/diag/qtiser.inf"
#define MODEM_INF_PATH "/opt/QTI/QUD/diag/qtimdm.inf"

fileInfo_t  *gQdssFileInfo;
static char *gQdssInfFilePath = NULL;

fileInfo_t  *gModemFileInfo;
static char *gModemInfFilePath = NULL;

fileInfo_t  *gDiagFileInfo;
static char *gDiagInfFilePath = NULL;

int debug_g=0;//For global logging

int UrbRxSize=QTIDEV_RX_SIZE;//global RxSize variable
int UrbTxSize=QTIDEV_TX_SIZE;//global TxSize variable
int skip_open_handles = 0;

static struct usb_driver qtiDevDriver;
/* Class should be created during module init, so needs to be global */
static struct class *gpDiagClass;
static struct class *gpQdssClass;
static int gQdssCounter = 0;

/* Global device list */
#define QTI_NUM_DEVICES_DEFAULT 8
static struct list_head DeviceListIdle;
static struct list_head DeviceListActive;
static spinlock_t DevListLock;
static int DevicesIdle, DevicesActive;

static void InitializeTxBuffers(sQTIDevUSB *pDev);
static void DeinitializeTxBuffers(sQTIDevUSB *pDev);
static void UpSem(sQTIDevUSB *pDev, void *pData, int evt);
static unsigned int ReadBufferFreeSpace(sQTIDevUSB *pDev);
static int InitializeURB(sQTIDevUSB *pDev);
static void FinalizeURB(sQTIDevUSB *pDev);
static bool ClearReadMemList(sQTIDevUSB *pDev);
static void DeInitializeURB(sQTIDevUSB *pDev);
static int SubmitAllReadUrb(sQTIDevUSB *pDev);
static void StopRead(sQTIDevUSB *pDev);
static bool PopFromQueueAndSubmit(sQTIDevUSB *pDev);
static void AddToQueue(sQTIDevUSB *pDev, struct urb *pUrb);
static ssize_t ioData_dequeue(struct kiocb *kiocb, void *userData);
static void *io_async_complete(struct kiocb *kiocb, void *userData);
static ssize_t readFromRingbuff(struct qtidev_aio_data *pIoData);
static int CopyFromAioReadMemList(sQTIDevUSB *pDev, void **ppReadData, size_t *pDataSize);
static size_t cpyToTargetIter(sQTIDevUSB *pDev, void *to, void *from,
        unsigned long count, unsigned long *iflags, bool isIter);
static int CopyFromReadMemList(sQTIDevUSB *pDev, void **ppReadData, size_t *pDataSize,
        unsigned long *iflags, eRxType eType);
static int QtiInitializeDeviceContext(sQTIDevUSB *dev);
static void QtiResetDeviceContext(sQTIDevUSB *pDev);
static int QtiInitializeDeviceList(void);
static sQTIDevUSB *QtiAcquireDevice(struct usb_device *udev_temp, struct usb_interface *interface, char *mpKey);
static void QtiReleaseDevice(sQTIDevUSB *pDev);
static void QtiFreeDevices(void);
void PrintHex(void *pBuffer, u16 bufSize);
int ResubmitIntURB(struct urb *pIntUrb);
void IntCallback(struct urb * pIntURB);
void SetDtrRts(sQTIDevUSB *pDev, __u16 DtrRts);
void RemoveNotification(sQTIDevUSB *pDev, void *evt);
int PrepareTxUrbBuffer(sQTIDevUSB *pDev, size_t dataLen, void *pSem);
void *io_sync_complete(struct kiocb *kiocb, void *userData);
ssize_t ioData_enqueue(struct kiocb *kiocb);
ssize_t UserspaceQTIDevWriteIter(struct kiocb *kiocb, struct iov_iter *iov);

#ifdef QCUSB_TEST_ONLY
void LpcOutputBytes(unsigned char *buf, long cnt, int loc, char *tag);

void LpcOutputBytes(unsigned char *buf, long cnt, int loc, char *tag)
{
    if (cnt >= 4)
    {
        QC_LOG_GLOBAL("%s : %ld(0x%lx) : at[%d] %X %X %X %X\n", tag, cnt, cnt*1024, loc,
		       buf[0], buf[1], buf[2], buf[3]);
    }
}

static void QtiEnterSVEFlow(sQTIDevUSB *pDev)
{
    int result = 0;
    unsigned long *pData;
    void *buffer;

    buffer = (void *)qti_kmalloc(1024, GFP_KERNEL);
    if (buffer == NULL)
    {
        QC_LOG_WARN(pDev,"QTI-SVE %d-%s: MEM failure\n", pDev->udev->bus->busnum, pDev->udev->devpath);
        return;
    }
    pData = (unsigned long *)buffer;
    *pData = 0x00000001;
    result = usb_control_msg(pDev->udev,
                             usb_sndctrlpipe(pDev->udev, 0),
                             0xF0,    // request
                             0x40,    // req type
                             0x00E1,  // wValue
                             0,       // wIndex
                             buffer,  // data
                             4,       // size of data
                             10 );    // timeout in msecs
    if (result < 0)
    {
        QC_LOG_ERR( pDev, "Bad QtiEnterSVEFlow status %d\n", result );
    }

    if (debug_g == 0)
    {
        QC_LOG_INFO(pDev, "QTI %d-%s: <-- status %d (cmd 0x%lx)\n", pDev->udev->bus->busnum, pDev->udev->devpath, result, *pData);
    }
    else
    {
        QC_LOG_INFO(pDev,"QTI-SVE %d-%s: 0x%px (idle-active %d-%d) result %d\n",
                   pDev->udev->bus->busnum, pDev->udev->devpath, pDev, DevicesIdle, DevicesActive, result);
    }
    qti_kfree(buffer);
}
#endif 

static int QtiInitializeDeviceContext(sQTIDevUSB *dev)
{
    /* To maintain Ref count, can be avoided */
    kref_init(&dev->mRefCount);
    mutex_init(&dev->mIoMutex);
    spin_lock_init(&dev->mSpinReadBuffLock);
    spin_lock_init(&dev->mSpinErrLock);
    spin_lock_init(&dev->mBulkMemList.mReadMemLock);
    init_usb_anchor(&dev->submitted);
    /* Initialize TX buffer elements */
    InitializeTxBuffers(dev);

    dev->mpWorkQ = alloc_workqueue("qtiWQ", 0, 4);
    // dev->udev = usb_get_dev(interface_to_usbdev(interface));
    // dev->interface = interface;
    dev->mBulkMemList.mpReadNotifyList = NULL;
    dev->debug=QC_LOG_LVL_INFO/10;
    dev->disconnected = 0;
    
#ifdef QCUSB_TEST_ONLY
    dev->mLpcRead = 0;
    spin_lock_init(&dev->mTestLock);
#endif
#ifdef QDSS_INT_BULK_BUFFER
    dev->mBulkMemList.mReadBuffCircular.mBulk_in_buffer = vzalloc(QTIDEV_BULK_BUF_LEN+1024);
    if (!dev->mBulkMemList.mReadBuffCircular.mBulk_in_buffer) {
        QC_LOG_ERR(dev,"Failed to allocate vmalloc memory\n");
        return -ENOMEM;
    }
    dev->mBulkMemList.mReadBuffCircular.mBufflen = QTIDEV_BULK_BUF_LEN;
    dev->mBulkMemList.mReadBuffCircular.mStartIdx = 0;
    dev->mBulkMemList.mReadBuffCircular.mEndIdx = 0;
    dev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus = true;
#endif
    dev->mBulkInSize = UrbRxSize; // QTIDEV_RX_SIZE;
    QC_LOG_GLOBAL("URB Rx Size %zu\n", dev->mBulkInSize);
    return InitializeURB(dev);
}

static void QtiResetDeviceContext(sQTIDevUSB *pDev)
{
    int i;

    pDev->mBulkInEndAddr = 0;
    pDev->mBulkOutEndAddr = 0;
    memset(&(pDev->mDevInfo.mDevInfInfo), 0, sizeof(devInfo_t));
    pDev->mDevInfo.mClassType = QTIDEV_INF_CLASS_UNKNOWN;
    pDev->mDevInfo.mpDevClass = NULL;
    pDev->mDevInfo.mDevInfInfo.mDevType = QTIDEV_INF_TYPE_UNKNOWN;
#ifdef QCUSB_TEST_ONLY
    pDev->mLpcRead = 0;
#endif
    memset(&pDev->mStats, 0, sizeof(sQTIDevStats));

    pDev->mIoReadBuffListActiveSize = 0;
    pDev->mIoReadBuffListIdleSize = BULK_URB_LIST;
    ClearReadMemList(pDev); // pDev->mBulkUrbBuffListSize = 0;

    for (i = 0; i < BULK_URB_LIST; i++)
    {
        pDev->mIoReadList[i].kiocb = NULL;
        pDev->mBulkUrbList[i].mUrbStatus = BULK_URB_INITIALIZED;
    }
}

static int QtiInitializeDeviceList(void)
{
    sQTIDevUSB *pDev = NULL;
    int i;

    spin_lock_init(&DevListLock);
    INIT_LIST_HEAD(&DeviceListIdle);
    INIT_LIST_HEAD(&DeviceListActive);
    DevicesIdle = DevicesActive = 0;
    for (i = 0; i < QTI_NUM_DEVICES_DEFAULT; i++)
    {
        pDev = qti_kmalloc(sizeof(sQTIDevUSB), GFP_KERNEL);
        if (pDev == NULL)
        {
            break;
        }
        if (QtiInitializeDeviceContext(pDev))
        {
            QC_LOG_ERR(pDev,"Failed to initialize dev context\n");
            DeInitializeURB(pDev);
            qti_kfree(pDev);
            pDev = NULL;
            break;
        }
        list_add_tail(&pDev->node, &DeviceListIdle);
	++DevicesIdle;
    }
    QC_LOG_INFO(pDev,"initial device pool size: %d\n", DevicesIdle);
    if (DevicesIdle != 0)
    {
        return 0;
    }
    return 1;  // failure
}

static sQTIDevUSB *QtiAcquireDevice(struct usb_device *udev_temp, struct usb_interface *interface, char *mpKey)
{
    char fqDevName_temp[255];
    sQTIDevUSB *pDev = NULL;
    unsigned long flags;
    int success = 0;

    sprintf(fqDevName_temp,"%s_%d-%s:%d.%d",mpKey,
        udev_temp->bus->busnum,
        udev_temp->devpath,
        udev_temp->actconfig->desc.bConfigurationValue,
        interface->cur_altsetting->desc.bInterfaceNumber);  

    spin_lock_irqsave(&DevListLock, flags);
    if (list_empty(&DeviceListIdle) == true)
    {
        spin_unlock_irqrestore(&DevListLock, flags);
        pDev = qti_kmalloc(sizeof(sQTIDevUSB), GFP_KERNEL);
	if (pDev != NULL)
        {
            if (QtiInitializeDeviceContext(pDev))
            {
                QC_LOG_ERR(pDev,"Failed to initialize dev context\n");
                DeInitializeURB(pDev);
                qti_kfree(pDev);
                pDev = NULL;
            }
            else
            {
                list_add_tail(&pDev->node, &DeviceListActive);
                ++DevicesActive;
                success = 1;
            }
        }
    }
    else
    {
        int found=0;
        list_for_each_entry(pDev, &DeviceListIdle, node)
        {
            
         
            int res = strncmp(pDev->fqDevName,fqDevName_temp,255);
            if(res == 0)
            {
                /* printk(KERN_INFO "FOUND THE ENTRY\n"); */
                found=1;
                list_move_tail(&pDev->node, &DeviceListActive);
                --DevicesIdle;
                ++DevicesActive;
                success = 1;
                break;
            }
        }
        if(found == 0){/*Entry not found: Check if there is any empty list node (default entry) else Create new entry and add it to active list directly. We can't do blind move because entries are important if device comes back later (in case of multiple device)*/
            int findNULL = 0;/* Flag to record if any NULL entry found*/ 
            list_for_each_entry(pDev, &DeviceListIdle, node)
            {
                int res = strncmp(pDev->fqDevName,"",255);
                if(res == 0)
                {
                    /* printk(KERN_INFO "FOUND A NULL ENTRY\n");*/
                    findNULL = 1;
                    list_move_tail(&pDev->node, &DeviceListActive);
                    --DevicesIdle;
                    ++DevicesActive;
                    success = 1;
                    break;
                }
            }
            if(findNULL == 0) /* Now create a new entry*/
            {
                pDev = qti_kmalloc(sizeof(sQTIDevUSB), GFP_KERNEL);
                if (pDev != NULL)
                {
                    /*printk(KERN_INFO "CREATED A NEW ENTRY\n");*/
                    if (QtiInitializeDeviceContext(pDev))
                    {
                        QC_LOG_ERR(pDev,"Failed to initialize dev context\n");
                        DeInitializeURB(pDev);
                        qti_kfree(pDev);
                        pDev = NULL;
                    }
                    else
                    {
                        list_add_tail(&pDev->node, &DeviceListActive);
                        ++DevicesActive;
                        success = 1;
                    }
                }
            }
        }
        spin_unlock_irqrestore(&DevListLock, flags);
    }
    if (success == 0)
    {
        QC_LOG_WARN(pDev,"QTI-ALERT: failure to acquire Device (idle-active %d-%d)\n", DevicesIdle, DevicesActive);
    }
    return pDev;
}

static void QtiReleaseDevice(sQTIDevUSB *pDev)
{
    sQTIDevUSB *pDevOnRecord = NULL;
    unsigned long flags;
    int removed = 0;

    spin_lock_irqsave(&DevListLock, flags);
    list_for_each_entry(pDevOnRecord, &DeviceListActive, node)
    {
        if (pDevOnRecord == pDev)
	{
            QtiResetDeviceContext(pDev); /* ideally kref_init & init_waitqueue should be added here*/
            list_move_tail(&pDevOnRecord->node, &DeviceListIdle);
            ++DevicesIdle;
            --DevicesActive;
	        removed = 1;
            break;
	}
    }
    QC_LOG_INFO(pDev,"Device = 0x%px (idle-active %d-%d)\n", pDevOnRecord, DevicesIdle, DevicesActive);
    if (removed == 0)
    {
        QC_LOG_WARN(pDev,"QTI-ALERT %d-%s: failure to remove Device = 0x%px (idle-active %d-%d)\n", 
                  pDev->udev->bus->busnum, pDev->udev->devpath, pDev, DevicesIdle, DevicesActive);
    }
    spin_unlock_irqrestore(&DevListLock, flags);
    return;
}

static void QtiFreeDevices(void)
{
    sQTIDevUSB *pDevOnRecord = NULL;
    sQTIDevUSB *pDevSafe = NULL;
    unsigned long flags;
    int removed = 0;

    QC_LOG_GLOBAL("--> DevActive %d DevIdle %d\n", DevicesActive, DevicesIdle);
    // while (DevicesActive > 0)
    {
	// msleep(1000);
        spin_lock_irqsave(&DevListLock, flags);
        list_for_each_entry_safe(pDevOnRecord, pDevSafe, &DeviceListIdle, node)
        {
            if (pDevOnRecord != NULL)
            {
                list_del(&pDevOnRecord->node);
                spin_unlock_irqrestore(&DevListLock, flags);
                DeInitializeURB(pDevOnRecord);
                destroy_workqueue(pDevOnRecord->mpWorkQ);
                qti_kfree(pDevOnRecord);
                --DevicesIdle;
                ++removed;

                spin_lock_irqsave(&DevListLock, flags);
            }
        }
        QC_LOG_GLOBAL("Idle devices removed %d remaining %d (vs %d)\n", removed, DevicesActive, DevicesIdle);
        spin_unlock_irqrestore(&DevListLock, flags);

        spin_lock_irqsave(&DevListLock, flags);
        removed = 0;
        list_for_each_entry_safe(pDevOnRecord, pDevSafe, &DeviceListActive, node)
        {
            if (pDevOnRecord != NULL)
            {
                list_del(&pDevOnRecord->node);
                spin_unlock_irqrestore(&DevListLock, flags);
                DeInitializeURB(pDevOnRecord);
                destroy_workqueue(pDevOnRecord->mpWorkQ);
                qti_kfree(pDevOnRecord);
                --DevicesActive;
                ++removed;

                spin_lock_irqsave(&DevListLock, flags);
            }
        }
        QC_LOG_GLOBAL("Active devices removed %d remaining %d (vs %d)\n", removed, DevicesActive, DevicesIdle);
        spin_unlock_irqrestore(&DevListLock, flags);
    }
    QC_LOG_GLOBAL("<--\n");
    return;
}

static unsigned int ReadBufferFreeSpace(sQTIDevUSB *pDev)
{
    unsigned int freeSpace = 0;
    unsigned int buffLen = pDev->mBulkMemList.mReadBuffCircular.mBufflen;
    unsigned int startIdx = pDev->mBulkMemList.mReadBuffCircular.mStartIdx;
    unsigned int endIdx = pDev->mBulkMemList.mReadBuffCircular.mEndIdx;

    if (pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus == true)
    {
        freeSpace = QTIDEV_BULK_BUF_LEN;
    }
    else
    {
        if (startIdx < endIdx)
        {
            freeSpace = buffLen - (endIdx - startIdx);
        }
        else if (startIdx > endIdx)
        {
            freeSpace = startIdx - endIdx;
        }
    }
    return freeSpace;
}

static void ClearAioData(struct kref *mRefCount)
{
    struct qtidev_aio_data *aioDataCtx  = container_of(mRefCount, sIoData, mRefCount);
    QC_LOG_DBG(aioDataCtx->pDev,"\n");
    qti_kfree(aioDataCtx);
    return;
}

static void CallbackToRefCount(struct kref *mRefCount)
{
    struct qtidev_aio_data *aioDataCtx  = container_of(mRefCount, sIoData, mRefCount);
    QC_LOG_DBG(aioDataCtx->pDev,"\n");
    return;
}

static void QTIDevUSBDelete(struct kref *mRefCount)
{
    sQTIDevUSB *dev = container_of(mRefCount, sQTIDevUSB, mRefCount);

    QC_LOG_INFO(dev,"de-ref device\n");
    /* prevent more I/O from starting */
    usb_put_intf(dev->interface);
    dev->interface = NULL;
    usb_put_dev(dev->udev);

    return;
}

static void QTIDevUSBDrawDown(sQTIDevUSB *dev)
{
    int time;

    QC_LOG_DBG(dev," --> unlink urbs\n");
    DeinitializeTxBuffers(dev);

    time = usb_wait_anchor_empty_timeout(&dev->submitted, QTIDEV_TX_TIMEOUT);
    if (!time)
    {
        QC_LOG_ERR(dev,"timeout on URB anchor, kill URBs\n");
        usb_kill_anchored_urbs(&dev->submitted);
    }
    QC_LOG_DBG(dev,"<--\n");
    return;
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
   void *      pBuffer,
   u16         bufSize )
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
   ResubmitIntURB

DESCRIPTION:
   Re-submit interrupt URB to receive data over USB interrupt pipe

PARAMETERS:
   pIntUrb    [ I ] - URB

RETURN VALUE:
   int - negative error code on failure
         zero on success
===========================================================================*/
int ResubmitIntURB( struct urb * pIntUrb )
{
    sQTIDevUSB *dev = NULL;
    int status = 0;
    int interval = 0;

   // Sanity test
   if ( (pIntUrb == NULL)
   ||   (pIntUrb->dev == NULL) )
   {
      QC_LOG_ERR(dev," URB or dev is NULL \n");
      return -EINVAL;
   }
   
   dev = (sQTIDevUSB *)pIntUrb->context;

   // Interval needs reset after every URB completion
   interval = 9;

   // Reschedule interrupt URB
   usb_fill_int_urb
   (
      pIntUrb,
      pIntUrb->dev,
      pIntUrb->pipe,
      pIntUrb->transfer_buffer,
      pIntUrb->transfer_buffer_length,
      pIntUrb->complete,
      pIntUrb->context,
      interval);

   status = usb_submit_urb( pIntUrb, GFP_ATOMIC );
   if (status != 0)
   {
      QC_LOG_GLOBAL( "Error re-submitting Int URB %d\n", status );
   }

   return status;
}  // ResubmitIntURB

/*===========================================================================
METHOD:
   IntCallback

DESCRIPTION:
   Callback function for interrupt URB

PARAMETERS:
   pIntURB    [ I ] - URB

RETURN VALUE:
   none
===========================================================================*/
void IntCallback( struct urb * pIntURB )
{
    sQTIDevUSB *dev = NULL;

   if (pIntURB == NULL)
   {
      QC_LOG_ERR(dev," error, bad read URB \n");
      return;
   }

   dev = (sQTIDevUSB *)pIntURB->context;

   // Verify this was a normal interrupt
   if (pIntURB->status != 0)
   {
     QC_LOG_DBG(dev, "Int status = %d\n", pIntURB->status );
      // Ignore EOVERFLOW errors
      if (pIntURB->status != -EOVERFLOW)
      {
         QC_LOG_DBG(dev, "Int status = %d\n", pIntURB->status );
         dev->mIntErrCnt++;
         return;
      }
   }
   else
   {
      dev->mIntErrCnt = 0;
      QC_LOG_INFO(dev, "IntCallback: %d bytes\n", pIntURB->actual_length );
      PrintHex(pIntURB->transfer_buffer, pIntURB->actual_length);
   }

    QC_LOG_INFO(dev, "re-activate interrupt pipe 0x%p\n", pIntURB );
    ResubmitIntURB( pIntURB );

}  // IntCallback

/*===========================================================================
METHOD:
   SetDtrRts

DESCRIPTION:
   Set or clear DTR/RTS over the USB control pipe

PARAMETERS:
   context: [ I ] - private context for the serial device
   DtrRts:  [ I ] - DTR/RTS bits

RETURN VALUE:
   none
===========================================================================*/
void SetDtrRts(sQTIDevUSB *pDev, __u16 DtrRts)
{
   int dtrResult;

   QC_LOG_INFO(pDev, "--> 0x%x\n", DtrRts);
   
   dtrResult = usb_control_msg
               (
                  pDev->udev,
                  usb_sndctrlpipe( pDev->udev, 0 ),
                  0x22,
                  0x21,
                  DtrRts,
                  pDev->mIntfNum,
                  NULL,
                  0,
                  100
               );
   QC_LOG_INFO(pDev, "Set DTR/RTS 0x%x\n", DtrRts);
} // SetDtrRts

static int UserspaceQTIDevOpen(struct inode *inode, struct file *file)
{
    sQTIDevUSB *dev;
    struct usb_interface *interface = NULL;
    int retval = 0;
    int interval = 0;

    dev = container_of( inode->i_cdev, sQTIDevUSB, mCdev);
    if (!dev) {
        retval = -ENODEV;
        goto exit;
    }

    QC_LOG_INFO(dev, "PID = %u, Pname = %s, tgid= %u\n",task_pid_nr(current),current->comm, task_tgid_nr(current));
    strscpy(dev->pName, current->comm, 255);

    dev->mStats.RxCount = dev->mStats.TxCount = 0;
    dev->mStats.USBRxCnt = dev->mStats.ToUsrCnt = 0;

    interface = dev->interface;
    if (!interface) {
        QC_LOG_ERR(dev,"%s - error, can't find device \n", __func__);
        retval = -ENODEV;
        goto exit;
    }
    QC_LOG_INFO(dev," UserspaceQCDevOpen - Interface is %s \n",dev->fqDevName);
    retval = usb_autopm_get_interface(interface);
    if (retval)
        goto exit;

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,17))
    if (dev->mRefCount.refcount.counter > 1)
#else
    if (dev->mRefCount.refcount.refs.counter > 1)
#endif
    {
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,17))
        QC_LOG_ERR(dev,"device busy, open denied. RefCnt:%d\n",
                dev->mRefCount.refcount.counter);
#else
        QC_LOG_ERR(dev,"device busy, open denied. RefCnt:%d\n",
                dev->mRefCount.refcount.refs.counter);
#endif
        retval = -EIO;
        if (interface)
            usb_autopm_put_interface(interface);
        goto exit;
    }

    kref_init(&dev->mRefCount);
    /* increment our usage count for the device */
    kref_get(&dev->mRefCount);

    /* save our object in the file's private structure */
    file->private_data = dev;

    if ((dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_INT_IN) 
        && (dev->mpIntURB != NULL))
    {
        interval = 9;
        
        QC_LOG_INFO(dev,"<%s> start interrupt EP\n",dev->mDevInfo.mDevInfInfo.mpKey);

        dev->mIntErrCnt = 0;
        usb_fill_int_urb
        (
           dev->mpIntURB,
           dev->udev,
           dev->mIntInEndp,
           dev->mpIntBuffer,
           dev->mIntInEndpMaxPacketSize,
           IntCallback,
           dev,
           interval
        );

        usb_submit_urb( dev->mpIntURB, GFP_KERNEL );

        // set DTR/RTS
        SetDtrRts(dev, (GOBI_SER_DTR | GOBI_SER_RTS));
    }

	if ((dev->mDevInfo.mDevInfInfo.mDevType != QTIDEV_INF_TYPE_DPL)
#ifdef QCUSB_TEST_ONLY
            &&
            ((dev->mDevInfo.mDevInfInfo.mDevType != QTIDEV_INF_TYPE_LPC))
#endif
           )
    {
	    QC_LOG_INFO(dev," URB submitted\n");
        /* Start async reading */
        retval = SubmitAllReadUrb(dev);
        if (retval != 0)
        {
            QC_LOG_ERR(dev," Error in reading\n");
            kref_put(&dev->mRefCount, QTIDevUSBDelete);
        }
    }
    /* Initialize workqueue for poll() */
    init_waitqueue_head( &dev->mBulkMemList.mWaitQueue );
exit:
#ifdef QCUSB_TEST_ONLY
    if (dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC)
    {
        dev->mSessionTotal = 0LL;
    }
#endif
    QC_LOG_INFO(dev," <-- retval %d ref %d\n", retval,
                    #if (LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,17)) 
                        dev->mRefCount.refcount.counter
                    #else 
                        dev->mRefCount.refcount.refs.counter
                    #endif
                    );
    return retval;
}

static bool ClearNotifyList(sQTIDevUSB *pDev)
{
    sNotifyList *pThisNotifyList;

    QC_LOG_DBG(pDev,"");
    while (pDev->mBulkMemList.mpReadNotifyList != NULL)
    {
        pThisNotifyList = pDev->mBulkMemList.mpReadNotifyList;
        pDev->mBulkMemList.mpReadNotifyList = pThisNotifyList->mpNext;
        if (pThisNotifyList->mpNotifyFunct && pThisNotifyList->mpData)
            pThisNotifyList->mpNotifyFunct(pDev, pThisNotifyList->mpData, QTI_RX_EVT_CANCEL);
        pThisNotifyList->mpData = NULL;
        pThisNotifyList->mInProcess = 0;
    }
    return true;
}

static bool ClearReadMemList(sQTIDevUSB *pDev)
{
    sBulkUrbList *pBulkUrbTraverse=NULL;

    QC_LOG_DBG(pDev," mBulkUrbBuffListSize: %lu\n", pDev->mBulkUrbBuffListSize);
    list_for_each_entry(pBulkUrbTraverse, &pDev->mBulkUrbBuffList, node) {
        pBulkUrbTraverse->Context = NULL;
        pBulkUrbTraverse->mIndex = -1;
        pBulkUrbTraverse->mBulk_in_urb = NULL;
        pBulkUrbTraverse->mBulk_in_buffer = NULL;
        pBulkUrbTraverse->mUrbStatus = 0;
    }
    pDev->mBulkUrbBuffListSize = 0;

    if (pDev->mBulkMemList.mReadBuffCircular.mBulk_in_buffer) {
        QC_LOG_INFO(pDev,"reset vmalloc memory\n");
        memset(pDev->mBulkMemList.mReadBuffCircular.mBulk_in_buffer, 0, QTIDEV_BULK_BUF_LEN + 1024);
    }
    pDev->mBulkMemList.mReadBuffCircular.mStartIdx = 0;
    pDev->mBulkMemList.mReadBuffCircular.mEndIdx = 0;
    pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus = true;
    pDev->mBulkMemList.mReadBuffCircular.mBufflen = QTIDEV_BULK_BUF_LEN;

    return true;
}

static bool ClearAioList(sQTIDevUSB *pDev)
{
    sIoReadList *pIoReadTraverse;
    sIoReadList *psafePtr;
    struct qtidev_aio_data *io_data;

    QC_LOG_DBG(pDev,"");
    /*
     * list_for_each_entry_safe takes care of the deletion in the read_list
     * being done via ioData_dequeue.
     */
    list_for_each_entry_safe(pIoReadTraverse, psafePtr, &pDev->mIoReadBuffListActive, node ) {
	if (pIoReadTraverse && pIoReadTraverse->kiocb) {
	    io_data = pIoReadTraverse->kiocb->private;
	    QC_LOG_DBG(pDev,"%s io_data->read true and setting dequeue null \n",__func__);
	    ioData_dequeue(io_data->kiocb, NULL);
	}
	else
	    break;
    }
    return true;
}

static int UserspaceQTIDevRelease(struct inode *inode, struct file *file)
{
    sQTIDevUSB *pDev=NULL;
    unsigned long flags;

    if ((file == NULL) || (pDev = file->private_data) == NULL)
    {
        QC_LOG_ERR(pDev,"Invalid arg\n");
        return -ENODEV;
    }

    QC_LOG_INFO(pDev, "PID = %u, Pname = %s, tgid= %u\n",task_pid_nr(current),current->comm, task_tgid_nr(current));

    if (pDev->interface)
        usb_autopm_put_interface(pDev->interface);

    /* Stop async reading  */
    StopRead(pDev);
    spin_lock_irqsave(&pDev->mBulkMemList.mReadMemLock, flags);
    ClearReadMemList(pDev);
    spin_unlock_irqrestore(&pDev->mBulkMemList.mReadMemLock, flags);
    kref_put(&pDev->mRefCount, QTIDevUSBDelete);
    return 0;
}

static int UserspaceQTIDevFlush(struct file *pFile, fl_owner_t id)
{
    sQTIDevUSB *pDev=NULL;
    unsigned long flags;
    int res = 0;

    
    if ((pFile == NULL) || (pDev = pFile->private_data) == NULL)
    {
        QC_LOG_ERR(pDev,"invalid data\n");
        return -ENODEV;
    }
    QC_LOG_INFO(pDev,"PID = %u, Pname = %s, tgid= %u\n",task_pid_nr(current),current->comm, task_tgid_nr(current));
	/*To handle 2 close events - one might come from signal generated from application
	and other also when application still invokes closeDevice .*/
    #if (LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,17))
        if (pDev->mRefCount.refcount.counter < 2)
    #else
        if (pDev->mRefCount.refcount.refs.counter < 2)
    #endif
	{
		QC_LOG_INFO(pDev,"Already cleaned up \n");
		return res;
	}

    /* allow the device to be autosuspended */
    mutex_lock(&pDev->mIoMutex);
    QTIDevUSBDrawDown(pDev);
    spin_lock_irqsave(&pDev->mSpinErrLock, flags);
    if (pDev->mLasterror)
    {
        res = (pDev->mLasterror == -EPIPE) ? -EPIPE : -EIO;
    }
    pDev->mLasterror = 0;
    spin_unlock_irqrestore(&pDev->mSpinErrLock, flags);
    mutex_unlock(&pDev->mIoMutex);

    /* prevent more I/O from starting */
    if (pDev->mDevInfo.mDevInfInfo.mDevType != QTIDEV_INF_TYPE_DPL)
    {
        /* Stop async reading  */
        // StopRead(pDev,false);
        /* To clear read/notify list */
        spin_lock_irqsave(&pDev->mBulkMemList.mReadMemLock, flags);
        ClearNotifyList(pDev);
        ClearAioList(pDev);
        spin_unlock_irqrestore(&pDev->mBulkMemList.mReadMemLock, flags);
    }

    QC_LOG_INFO(pDev,"Cleaned up\n");
    return res;
}

static bool MoveDataToDestination(sQTIDevUSB *pDev,
        void *dest,
        void *from,
        size_t size,
        unsigned long *iflags,
        eRxType eType)
{
    bool bFailed = false;
    QC_LOG_DBG(pDev,"-->\n");
    if (size == 0)
    {
        return false;
    }
    else
    {
        QC_LOG_DBG(pDev,"dest 0x%px from 0x%px size %d / %ld type %d\n", dest, from, (int)size, pDev->mStats.ToUsrCnt, (int)eType);
    }

    switch (eType)
    {
        case QTI_RX_AIO:
        {
            bFailed = (memcpy(dest, from, size) == NULL);
            break;
        }
        case QTI_RX_MEM:
        {
            bFailed = (cpyToTargetIter(pDev, dest, from, size, iflags, false) != 0);
            break;
        }
        case QTI_RX_ITER:
        {
            bFailed = (cpyToTargetIter(pDev, dest, from, size, iflags, true) != 0);
            break;
        }
        default:
        {
            bFailed = true;
            break;
        }
    }  // switch

#ifdef QCUSB_TEST_ONLY
    if (pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC) {
        int bufLoc = (int)(from - (void *)pDev->mBulkMemList.mReadBuffCircular.mBulk_in_buffer);
        LpcOutputBytes((unsigned char *)from, pDev->mStats.ToUsrCnt, bufLoc, "LPC-USER");
    }
#endif
    if (bFailed == false)
    {
        pDev->mStats.ToUsrCnt += size;;
    }
    else
    {
        QC_LOG_ERR(pDev,"failure moving data to user buffer\n");
    }
    QC_LOG_DBG(pDev,"<--\n");
    return bFailed;
}  // MoveDataToDestination

static int CopyFromAioReadMemList(sQTIDevUSB *pDev,
        void **ppReadData,
        size_t *pDataSize)
{
    QC_LOG_DBG(pDev,"-->\n");
    return CopyFromReadMemList(pDev, ppReadData, pDataSize, 0, QTI_RX_AIO);
}

static int CopyFromReadMemList(sQTIDevUSB *pDev,
        void **ppReadData,
        size_t *pDataSize,
        unsigned long *iflags,
        eRxType eType)
{
    void *pBulkStartInBuffer;
    unsigned int endIdx;
    unsigned int startIdx;
    unsigned int buffLen;
    QC_LOG_DBG(pDev,"");

    pBulkStartInBuffer = pDev->mBulkMemList.mReadBuffCircular.mBulk_in_buffer;
    buffLen = pDev->mBulkMemList.mReadBuffCircular.mBufflen;
    startIdx = pDev->mBulkMemList.mReadBuffCircular.mStartIdx;
    endIdx = pDev->mBulkMemList.mReadBuffCircular.mEndIdx;

    QC_LOG_DBG(pDev,"->T%d: data present strt:<%d>, end:<%d>, requested:<%d>\n", eType,
            pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
            pDev->mBulkMemList.mReadBuffCircular.mEndIdx, (int)*pDataSize);
    if ((startIdx < endIdx))
    {
        // non-empty case
        if ((endIdx - startIdx) > *pDataSize)
        {
            if (true == MoveDataToDestination(pDev, *ppReadData,
                          pBulkStartInBuffer+startIdx, *pDataSize, iflags, eType))
            {
               QC_LOG_ERR(pDev,"T%d: Error copying read data to user (<---)\n", eType);
               return -EFAULT;
            }
            pDev->mBulkMemList.mReadBuffCircular.mStartIdx += *pDataSize;
            QC_LOG_DBG(pDev,"T%d: 1- data present strt:<%d>, end:<%d>, len:<%d>\n", eType,
                    pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
                    pDev->mBulkMemList.mReadBuffCircular.mEndIdx, (int)*pDataSize);
        } else
        {
            // to provide fewer or all data
            #ifdef QCUSB_TEST_ONLY
            if (pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC)
            {
                if ((endIdx - startIdx) < *pDataSize)
                {
                   QC_LOG_ERR(pDev," (<--) T%d: Insufficient data strt:<%d>, end:<%d>, len:<%d>, need:<%lu>\n", eType,
                       pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
                       pDev->mBulkMemList.mReadBuffCircular.mEndIdx,
                       (int)*pDataSize, (*pDataSize - (endIdx - startIdx)));
                   return false;
                }
            }
            #endif
            if (true == MoveDataToDestination(pDev, *ppReadData,
                           pBulkStartInBuffer + startIdx, (endIdx - startIdx), iflags, eType))
            {
               QC_LOG_ERR(pDev,"T%d: Error copying read data to user\n", eType);
               return -EFAULT;
            }

            *pDataSize = (endIdx - startIdx);  // adjus to smaller value
            pDev->mBulkMemList.mReadBuffCircular.mStartIdx = endIdx;
            QC_LOG_DBG(pDev,"T%d: 2- data present strt:<%d>, end:<%d>, len:<%d>\n", eType,
                    pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
                    pDev->mBulkMemList.mReadBuffCircular.mEndIdx, (int)*pDataSize);
	    if (pDev->mBulkMemList.mReadBuffCircular.mStartIdx == pDev->mBulkMemList.mReadBuffCircular.mEndIdx)
		pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus = true;
        }
    } else if ((startIdx >= endIdx) && (pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus == false))
    {
        // NOTE: checking mBufEmptyStatus == false for startIdx == endIdx to make sure it's buffer full

        if ((buffLen - startIdx) >= *pDataSize)
        {
            if (true == MoveDataToDestination(pDev, *ppReadData,
                           pBulkStartInBuffer + startIdx, *pDataSize, iflags, eType))
            {
               QC_LOG_ERR(pDev," (<--) T%d: Error copying read data to user\n", eType);
               return -EFAULT;
            }

            pDev->mBulkMemList.mReadBuffCircular.mStartIdx += *pDataSize;
            pDev->mBulkMemList.mReadBuffCircular.mStartIdx %= buffLen;
            QC_LOG_DBG(pDev,"T%d: 3- data present strt:<%d>, end:<%d>, len:<%d>\n", eType,
                    pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
                    pDev->mBulkMemList.mReadBuffCircular.mEndIdx, (int)*pDataSize);
        } else
        {
            int partialLen = 0;
            unsigned int leg1=0, leg2=0, len3=0;
            #ifdef QCUSB_TEST_ONLY
            unsigned int storedSize;
            #endif

            leg1 = buffLen - startIdx;

            #ifdef QCUSB_TEST_ONLY

            if (pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC)
            {
                storedSize = leg1 + endIdx;
                if (*pDataSize > storedSize)
                {
                   QC_LOG_ERR(pDev," (<--) T%d: Insufficient data-wrap strt:<%d>, end:<%d>, len:<%d>/%d\n", eType,
                       pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
                       pDev->mBulkMemList.mReadBuffCircular.mEndIdx,
                       storedSize, (int)*pDataSize);
                   return false;
                }
            }
            #endif

            if (true == MoveDataToDestination(pDev, *ppReadData,
                           pBulkStartInBuffer + startIdx, leg1, iflags, eType))
            {
               QC_LOG_ERR(pDev," (<--) T%d: Error copying read data to user - leg1 \n", eType);
               return -EFAULT;
            }

            partialLen = leg1; // (buffLen - startIdx);
            leg2 = *pDataSize - partialLen;

            // now in leg2 data moving, mEndIdx could be updated by BlkCallback
            // so need to re-sync endIdx
            endIdx = pDev->mBulkMemList.mReadBuffCircular.mEndIdx;

            if ( endIdx > leg2)
            {
                bool res;

                if (eType == QTI_RX_ITER)
                {
                    res = MoveDataToDestination(pDev, *ppReadData,
                              pBulkStartInBuffer, leg2, iflags, eType);
                }
                else
                {
                    res = MoveDataToDestination(pDev, *ppReadData + partialLen,
                               pBulkStartInBuffer, leg2, iflags, eType);
                }
                if (res == true)
                {
                   QC_LOG_ERR(pDev," (<--) T%d: Error copying read data to user - leg2 \n", eType);
                   return -EFAULT;
                }

                pDev->mBulkMemList.mReadBuffCircular.mStartIdx = *pDataSize - partialLen;
            } else
            {
                bool res;

                // return fewer or all bytes  -- empty buffer
                if (eType == QTI_RX_ITER)
                {
                    res = MoveDataToDestination(pDev, *ppReadData,
                               pBulkStartInBuffer, endIdx, iflags, eType);
                }
                else
                {
                    res = MoveDataToDestination(pDev, *ppReadData + partialLen,
                               pBulkStartInBuffer, endIdx, iflags, eType);
                }
                if (res == true)
                {
                   QC_LOG_ERR(pDev," (<--) T%d: Error copying read data to user - leg2a \n", eType);
                   return -EFAULT;
                }

                len3 = endIdx; // actual length
                *pDataSize = partialLen + endIdx;
                pDev->mBulkMemList.mReadBuffCircular.mStartIdx = endIdx;

                // need to inspec mEndIdx again
                if (endIdx == pDev->mBulkMemList.mReadBuffCircular.mEndIdx)  // if mEndIdx remains same
                {
                    pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus = true;
                }
            }
            QC_LOG_DBG(pDev," (<--) T%d: 4- data present strt:<%d>, end:<%d/%d>, len:<%d>(%d, %d, %d)\n", eType,
                    pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
                    endIdx, pDev->mBulkMemList.mReadBuffCircular.mEndIdx,
                    (int)*pDataSize, leg1, leg2, len3);
        }
    }
    else
    {
        QC_LOG_INFO(pDev," (<--) T%d: No data present strt:<%d>, end:<%d>, len:<%d> \n", eType,
                pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
                pDev->mBulkMemList.mReadBuffCircular.mEndIdx, (int)*pDataSize);
        return false;
    }

    /* To Pop out all the read queue */
    while(PopFromQueueAndSubmit(pDev));

    return true;
}  // CopyFromReadMemList

static void InitializeRxNotifyPool(sQTIDevUSB *pDev)
{
    int i;
    QC_LOG_DBG(pDev,"");
    for (i = 0; i < QTIDEV_RX_NOTIFY_POOL_SZ; i++)
    {
        qti_initialize_event(&(pDev->mRxNotifyPool[i].mEvent));
        qti_register_event(&(pDev->mRxNotifyPool[i].mEvent), QTI_RX_EVT_CANCEL);
        qti_register_event(&(pDev->mRxNotifyPool[i].mEvent), QTI_RX_EVT_FILL);
    }
}
static sNotifyList *AllocateRxNotifyItem(sQTIDevUSB *pDev)
{
    int i;
    sNotifyList *item = NULL;
    QC_LOG_DBG(pDev,"");
    for (i = 0; i < QTIDEV_RX_NOTIFY_POOL_SZ; i++)
    {
        if (pDev->mRxNotifyPool[i].mpData == NULL)
        {
           item = &(pDev->mRxNotifyPool[i]);
           item->mIndex = i;
           break;
        }
    }
    return item;
}  // AllocateRxNotifyItem

static sQtiEvent *AddToNotifyList(sQTIDevUSB *pDev,
        void (*pNotifyFunct)(struct sQTIDevUSB *, void *, int), size_t size)
{
    sNotifyList ** ppThisNotifyList;
    QC_LOG_DBG(pDev,"");
    // Go to last URBList entry
    ppThisNotifyList = &pDev->mBulkMemList.mpReadNotifyList;
    while (*ppThisNotifyList != NULL)
    {
        ppThisNotifyList = &(*ppThisNotifyList)->mpNext;
    }

    *ppThisNotifyList = AllocateRxNotifyItem(pDev);
    if (*ppThisNotifyList == NULL)
    {
        QC_LOG_ERR(pDev,"Mem error\n" );
        return NULL;
    }

    (*ppThisNotifyList)->mpNext = NULL;
    (*ppThisNotifyList)->mpNotifyFunct = pNotifyFunct;
    (*ppThisNotifyList)->mpData = &((*ppThisNotifyList)->mEvent);
    (*ppThisNotifyList)->mUsrReqSize = size;
    (*ppThisNotifyList)->mInProcess = 0;

    QC_LOG_DBG(pDev,"added notify[%d]\n", (*ppThisNotifyList)->mIndex);
    return (*ppThisNotifyList)->mpData;
}

static void UpSem(sQTIDevUSB *pDev, void *pData, int evt)
{
    // QC_LOG_DBG(pDev,"qti_set_event - QTI_RX_EVT_FILL");  // TODO
    QC_LOG_DBG(pDev,"");
    qti_set_event((sQtiEvent *)pData, evt);
    return;
}

// FUNCTION RemoveNotification runs within spinlock
void RemoveNotification(sQTIDevUSB *pDev, void *evt)
{
    sNotifyList **ppThisNotifyList;
    sNotifyList *pDelNotifyListEntry;
    QC_LOG_DBG(pDev,"");
    ppThisNotifyList = &pDev->mBulkMemList.mpReadNotifyList;
    pDelNotifyListEntry = NULL;
    /* Find and delete matching entry */
    while (*ppThisNotifyList != NULL)
    {
        if ((*ppThisNotifyList)->mpData == evt)
        {
            pDelNotifyListEntry = *ppThisNotifyList;
            *ppThisNotifyList = (*ppThisNotifyList)->mpNext;
            pDelNotifyListEntry->mpData = NULL;
            pDelNotifyListEntry->mInProcess = 0;
            break;
        }
        ppThisNotifyList = &(*ppThisNotifyList)->mpNext;
    }
}  // RemoveNotification

// FUNCTION NotifyUserRequest runs within spinlock
static int NotifyUserRequest(sQTIDevUSB *pDev, void *evt)
{
    sNotifyList *pNotifyItem;
    unsigned int dataSize;
    QC_LOG_DBG(pDev,"");
    pNotifyItem = pDev->mBulkMemList.mpReadNotifyList;
    if (pNotifyItem == NULL)
    {
        QC_LOG_DBG(pDev,"No client to notify\n");
        return 0;
    }
    dataSize = pDev->mBulkMemList.mReadBuffCircular.mBufflen - ReadBufferFreeSpace(pDev);
#ifdef QCUSB_TEST_ONLY
    if (pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC)
    {
        if (dataSize < pNotifyItem->mUsrReqSize)
        {
             QC_LOG_ERR(pDev,"LPC: insufficient data: %u/%lu\n", dataSize, pNotifyItem->mUsrReqSize);
            return 0;
        }
    }
    else
#endif
    {
        if (dataSize == 0)
        {
            QC_LOG_DBG(pDev,"No data\n");
            return 0;
        }
    }
    if (((evt == NULL) || (pNotifyItem->mpData == evt)) && (pNotifyItem->mInProcess == 0))
    {
        QC_LOG_DBG(pDev,"item[%d] data %u/%lu\n", pNotifyItem->mIndex, dataSize, pNotifyItem->mUsrReqSize);
        pNotifyItem->mInProcess = 1;
        pNotifyItem->mpNotifyFunct(pDev, pNotifyItem->mpData, QTI_RX_EVT_FILL);  // set event
        return 1;
    }
    return 0;
}  // NotifyUserRequest

static int ReadSyncBlk(sQTIDevUSB *pDev,
        void **ppReadData, size_t size, unsigned int flags)
{
    int result;
    size_t dataSize;
    unsigned long iflags;
    sQtiEvent *rxEvt = NULL;
    int retVal = 0;
    QC_LOG_DBG(pDev,"--> \n");
    if (!pDev || !ppReadData || !*ppReadData)
    {
        QC_LOG_ERR(pDev,"Invalid data!\n" );
        return -ENXIO;
    }
    if (flags & O_NONBLOCK)
    {
        return -EAGAIN;
    }
    dataSize = size;

    spin_lock_irqsave(&pDev->mBulkMemList.mReadMemLock, iflags);
    QC_LOG_DBG(pDev,"-->UserBuf 0x%px size %lu\n", *ppReadData, dataSize);
    if ((rxEvt = AddToNotifyList( pDev, UpSem, size)) == NULL)
    {
        QC_LOG_ERR(pDev,"unable to register for notification\n" );
        spin_unlock_irqrestore(&pDev->mBulkMemList.mReadMemLock, iflags);
        return 0; // -EFAULT;
    }
    NotifyUserRequest(pDev, rxEvt);
    spin_unlock_irqrestore(&pDev->mBulkMemList.mReadMemLock, iflags);

    /* Wait for notification */
    QC_LOG_DBG(pDev,"Now, Wait for notification\n");
    result = qti_wait_event(rxEvt, QTIDEV_RX_TIMEOUT); // 2s timeout

    spin_lock_irqsave(&pDev->mBulkMemList.mReadMemLock, iflags);
    switch (result)
    {
        case QTI_RX_EVT_FILL:
        {
           qti_clear_event(rxEvt, result);
           // QC_LOG_DBG(pDev,"QTI_RX_EVT_FILL: 0x%px size %lu -- evt%d\n", *ppReadData, dataSize, result);
           if (CopyFromReadMemList(pDev, ppReadData, &dataSize, &iflags, QTI_RX_MEM) == false)
           {
               dataSize = 0;
           }
           retVal = (int)dataSize;
           break;
        }
        case QTI_RX_EVT_CANCEL:
        {
           qti_clear_event(rxEvt, result);
        //    QC_LOG_EXCEPTION(pDev,"QTI_RX_EVT_CANCEL: 0x%px size %lu -- evt%d\n", *ppReadData, dataSize, result);
           retVal = 0;
           break;
        }
        default:
        {
           dataSize = 0;
           if (result > 0)
           {
              qti_clear_event(rxEvt, result);
           }
           else if (result == 0)
           {
            //    QC_LOG_EXCEPTION(pDev,"qti_wait_event: %d, RX timeout\n", result);
               retVal = 0; // -ETIME;
           }
           else // if (result < 0)
           {
            //    QC_LOG_EXCEPTION(pDev,"qti_wait_event: %d, RX interrupted\n", result);
               retVal = -EINTR;
           }
           break;
        }
    }
    RemoveNotification(pDev, rxEvt);
    NotifyUserRequest(pDev, NULL);
    spin_unlock_irqrestore(&pDev->mBulkMemList.mReadMemLock, iflags);

    if (retVal < 0)
    {
        QC_LOG_EXCEPTION(pDev, "QTI <--UserBuf 0x%px size %d evt %d RxCount %ld\n",
            *ppReadData, retVal, result, pDev->mStats.RxCount);
    } else {
        QC_LOG_DBG(pDev, "QTI:: <--UserBuf 0x%px size %d evt %d RxCount %ld\n",
              *ppReadData, retVal, result, pDev->mStats.RxCount);
    }
    return retVal;
}

static ssize_t UserspaceQTIDevRead(struct file *file, char *buffer, size_t size,
        loff_t *ppos)
{
    sQTIDevUSB *dev=NULL;
    ssize_t result = 0;
    void *pReadData = NULL;

    dev = file->private_data;

    QC_LOG_DBG(dev, "PID = %u, Pname = %s, tgid= %u\n",task_pid_nr(current),current->comm, task_tgid_nr(current));

    if (dev == NULL || !buffer || !size)
    {
        QC_LOG_ERR(dev,"Invalid file data\n");
        return -EBADF;
    }

    pReadData = buffer;

    mutex_lock(&dev->mIoMutex);
    if (dev->disconnected)
    {
        QC_LOG_EXCEPTION(dev, "QTI-ALERT-R0: dev interface is gone\n");
        mutex_unlock(&dev->mIoMutex);
        return -ENODEV;
    }
    mutex_unlock(&dev->mIoMutex);

    result = ReadSyncBlk(dev, &pReadData, size, file->f_flags);

    return result;
}

static void UserspaceQTIDevWrite_bulk_callback(struct urb *urb)
{
    sQTIDevTxBuf *txBuf;
    sQTIDevUSB *dev;
#ifdef QCUSB_TEST_ONLY
    int retval = 0;
#endif
    
    txBuf = urb->context;
    dev = txBuf->mpDev;
    QC_LOG_DBG(dev,"PID = %u, Pname = %s, tgid= %u\n",task_pid_nr(current),current->comm, task_tgid_nr(current));
    QC_LOG_DBG(dev," [%d] status %d, actual_len: %u / %ld 0x%px\n",txBuf->mIndex,
                urb->status, urb->actual_length, dev->mStats.TxCount, urb);
    /* sync/async unlink faults aren't errors */
    if (urb->status) {
        if (!(urb->status == -ENOENT ||
                    urb->status == -ECONNRESET ||
                    urb->status == -ESHUTDOWN))
            QC_LOG_INFO(dev,"%s - nonzero write bulk status received: %d\n",
                    __func__, urb->status);

        spin_lock(&dev->mSpinErrLock);
        dev->mLasterror = urb->status;
        spin_unlock(&dev->mSpinErrLock);
    }
#ifdef QCUSB_TEST_ONLY
    if (!urb->status || (urb->status == -EINPROGRESS))
    {
        if ((dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC) && !dev->mLpcRead)
        {
            dev->mLpcRead = 1;
            retval = SubmitAllReadUrb(dev);
            if (retval != 0)
            {
                QC_LOG_ERR(dev,"Error in reading\n");
            }
        }
    }
#endif
    dev->mStats.TxCount += urb->actual_length;

    urb->transfer_buffer = NULL;
    up((struct semaphore*)txBuf->mpTxSem);
    return;
}

static void InitializeTxBuffers(sQTIDevUSB *pDev)
{
    int i;
    // get a free slot
    for (i = 0; i < QTIDEV_TX_BUF_POOL_SZ; i++)
    {
        pDev->mTxBufferPool[i].mpDev = pDev;
        pDev->mTxBufferPool[i].mIndex = i;
        pDev->mTxBufferPool[i].mpTxBuf = NULL;
        pDev->mTxBufferPool[i].mpTxUrb = NULL;
        init_usb_anchor(&(pDev->mTxBufferPool[i].mTxAnchor));
    }

}  // InitializeTxBuffers

static void DeinitializeTxBuffers(sQTIDevUSB *pDev)
{
    int i, time;
    QC_LOG_DBG(pDev,"");
    // get a free slot
    for (i = 0; i < QTIDEV_TX_BUF_POOL_SZ; i++)
    {
        time = usb_wait_anchor_empty_timeout(&(pDev->mTxBufferPool[i].mTxAnchor), QTIDEV_TX_TIMEOUT);
        if (!time)
        {
            QC_LOG_DBG(pDev,"timeout on URB anchor[%d], kill URBs\n", i);
            usb_kill_anchored_urbs(&(pDev->mTxBufferPool[i].mTxAnchor));
        }
    }
}  // DeinitializeTxBuffers

int PrepareTxUrbBuffer(sQTIDevUSB *pDev, size_t dataLen, void *pSem)
{
    int i;
    QC_LOG_DBG(pDev,"of dataLen = %zu\n", dataLen);
    // get a free slot
    for (i = 0; i < QTIDEV_TX_BUF_POOL_SZ; i++)
    {
        if (pDev->mTxBufferPool[i].mpTxSem == NULL)
        {
            pDev->mTxBufferPool[i].mpTxSem = pSem;
            break;
        }
    }

    if (i >= QTIDEV_TX_BUF_POOL_SZ)
    {
        QC_LOG_GLOBAL("TX buffer out of mem\n");
        return -ENOMEM;
    }
    // prepare TX buffer, re-allocate if necessary
    if (pDev->mTxBufferPool[i].mpTxBuf == NULL)
    {
        pDev->mTxBufferPool[i].mpTxBuf = qti_kmalloc(dataLen, GFP_KERNEL);
        if (pDev->mTxBufferPool[i].mpTxBuf != NULL)
        {
           pDev->mTxBufferPool[i].mTxBufSize = dataLen;
        }
        else
        {
            pDev->mTxBufferPool[i].mTxBufSize = 0;
            pDev->mTxBufferPool[i].mpTxSem = NULL;
            QC_LOG_ERR(pDev,"TX buffer mem failure-0[%d]\n", i);
            return -ENOMEM;
        }
    }
    else if (pDev->mTxBufferPool[i].mTxBufSize < dataLen)
    {
        qti_kfree(pDev->mTxBufferPool[i].mpTxBuf);
        pDev->mTxBufferPool[i].mpTxBuf = qti_kmalloc(dataLen, GFP_KERNEL);
        if (pDev->mTxBufferPool[i].mpTxBuf != NULL)
        {
           pDev->mTxBufferPool[i].mTxBufSize = dataLen;
        }
        else
        {
            pDev->mTxBufferPool[i].mTxBufSize = 0;
            pDev->mTxBufferPool[i].mpTxSem = NULL;
            QC_LOG_ERR(pDev,"TX buffer mem failure-1[%d]\n", i);
            return -ENOMEM;
        }
    }

    // prepare TX URB, re-allocate if necessary
    if (pDev->mTxBufferPool[i].mpTxUrb == NULL)
    {
        pDev->mTxBufferPool[i].mpTxUrb = usb_alloc_urb(0, GFP_KERNEL);
        if (pDev->mTxBufferPool[i].mpTxUrb == NULL)
        {
            QC_LOG_ERR(pDev,"TX urb mem failure[%d]\n", i);
            pDev->mTxBufferPool[i].mpTxSem = NULL;
            return -ENOMEM;
        }
    }

    return i;
}  // PrepareTxUrbBuffer

static ssize_t UserspaceQTIDevWrite(struct file *file, const char *user_buffer,
        size_t dataLen, loff_t *ppos)
{
    sQTIDevUSB *pDev=NULL;
    int retval = 0;
    struct urb *urb = NULL;
    char *buf = NULL;
    struct semaphore writeSem;
    struct usb_anchor *txAnchor;
    int txEmpty;
    int iterator = QTIDEV_RETRY;
    int txIdx = -1;
    u32 bytesSent; 

    pDev = file->private_data;

    /* verify that we actually have some data to write */
    if (!pDev || !dataLen
            || pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_DPL
            || pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_TRACE_IN)
    {
        QC_LOG_ERR(pDev,"Invalid data\n");
        return -ENXIO;
    }

    QC_LOG_DBG(pDev, "PID = %u, Pname = %s, tgid= %u\n",task_pid_nr(current),current->comm, task_tgid_nr(current));

    spin_lock_irq(&pDev->mSpinErrLock);
    if (pDev->mLasterror < 0)
    {
        pDev->mLasterror = 0;
        retval = -EIO;
        QC_LOG_ERR(pDev,"dev error, return\n");
        spin_unlock_irq(&pDev->mSpinErrLock);
        goto error;
    }
    spin_unlock_irq(&pDev->mSpinErrLock);

    sema_init(&writeSem, 0);
    txIdx = PrepareTxUrbBuffer(pDev, dataLen, &writeSem);

    // prepare TX buffer and TX URB, re-allocate if necessary
    if (txIdx < 0)
    {
        goto error;
    }
    urb = pDev->mTxBufferPool[txIdx].mpTxUrb;
    buf = pDev->mTxBufferPool[txIdx].mpTxBuf;
    txAnchor = &(pDev->mTxBufferPool[txIdx].mTxAnchor);

    if (copy_from_user(buf, user_buffer, dataLen))
    {
        QC_LOG_ERR(pDev,"copy_from_user failure\n");
        retval = -EFAULT;
        goto error;
    }

    mutex_lock(&pDev->mIoMutex);
    if (pDev->disconnected)
    {
        QC_LOG_EXCEPTION(pDev,"QTI-ALERT-W0: dev interface is gone\n");
        mutex_unlock(&pDev->mIoMutex);
        retval = -ENODEV;
        goto error;
    }

    usb_fill_bulk_urb(urb, pDev->udev,
            usb_sndbulkpipe(pDev->udev, pDev->mBulkOutEndAddr),
            buf, dataLen, UserspaceQTIDevWrite_bulk_callback, &(pDev->mTxBufferPool[txIdx]));
    urb->transfer_flags |= URB_ZERO_PACKET;
#ifdef QCUSB_TEST_ONLY
    if (pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC)
    {
        urb->transfer_flags &= ~URB_ZERO_PACKET;  // clear ZLP flag
    }
#endif
    usb_anchor_urb(urb, txAnchor);

    /* send the data out the bulk port */
    QC_LOG_DBG(pDev," TX URB[%d] 0x%px Buf 0x%px/0x%px (%lu bytes) flags 0x%x\n",
         txIdx, urb, buf, user_buffer, dataLen, urb->transfer_flags);
    while(iterator--)
    {
        retval = usb_submit_urb(urb, GFP_KERNEL);
        if(retval == -EAGAIN || retval == -EWOULDBLOCK || retval == -ENODEV){
            msleep(QTIDEV_SLEEP_TIMER);
            QC_LOG_DBG(pDev,"After Sleep count: %d",(QTIDEV_RETRY-iterator + 1));
        }
        else
            break;
    }
    if (retval != 0) {
        QC_LOG_ERR(pDev,"- failed submitting write urb, error %d\n", retval);
        usb_unanchor_urb(urb);
        mutex_unlock(&pDev->mIoMutex);
        goto error;
    }
    mutex_unlock(&pDev->mIoMutex);
    
    mutex_lock(&pDev->mIoMutex);
    if (pDev->disconnected)
    {
        QC_LOG_EXCEPTION(pDev, "QTI-ALERT-W1: dev interface is gone, return\n");
        mutex_unlock(&pDev->mIoMutex);
        retval = -ENODEV;
        goto error;
    }
    mutex_unlock(&pDev->mIoMutex);

    // IMPORTANT: assume only one URB on anshor for synchronous IO
    txEmpty = usb_wait_anchor_empty_timeout(txAnchor, QTIDEV_TX_TIMEOUT); /** return 0 on timeout */
    bytesSent = urb->actual_length;
    if (txEmpty == 0)
    {
        // timeout, cancel what's on the anchor
        QC_LOG_WARN(pDev, "QTI TX timeout: planned size %lu TxCount %ld\n", dataLen, pDev->mStats.TxCount);
        usb_unlink_anchored_urbs(txAnchor);
    }

    down(&writeSem); // make it non-interruptible

    pDev->mTxBufferPool[txIdx].mpTxSem = NULL;
    return bytesSent;

error:
    if (txIdx >= 0)
    {
        pDev->mTxBufferPool[txIdx].mpTxSem = NULL;
    }
    return retval;
}

static loff_t UserspaceQTIDevllseek(struct file *file, loff_t offset, int whence)
{
    return file->f_pos;
}

static int UserspaceQTIDevIOCTL(
   struct inode *    pUnusedInode,
   struct file *     pFilp,
   unsigned int      cmd,
   unsigned long     arg )
{
    int retval = 0;
    sQTIDevUSB *pDev=NULL;
    unsigned long userValue = 2;

    pDev = pFilp->private_data;

    /* verify that we actually have some data to write */
    
    if (!pDev || !arg)
    {
        QC_LOG_ERR(pDev,"Invalid data\n");
        return -ENXIO;
    }
    QC_LOG_DBG(pDev, "PID = %u, Pname = %s, tgid= %u\n",task_pid_nr(current),current->comm, task_tgid_nr(current));

    if (copy_from_user(&userValue, (void *)arg, sizeof(unsigned long)))
    {
        QC_LOG_ERR(pDev,"Invalid data\n");
        return -EFAULT;
    }

    switch (cmd)
    {
#ifdef QTIDEV_TIMEOUT
        case IOCTL_QTIDEV_SET_TIMEOUT:
            QC_LOG_DBG(pDev, "Setting timeout for Diag respone : %ld ms\n", userValue);
            break;

        case IOCTL_QTIDEV_KILL_READ:
            QC_LOG_DBG(pDev, "Killing the Read id : <%ld> \n", userValue);
            break;

        case IOCTL_QTIDEV_READ_STATUS:
            QC_LOG_DBG(pDev, "Getting the Read status of id : <%ld> \n", userValue);
            break;

        case IOCTL_QTIDEV_GET_ASYNC_READ:
            QC_LOG_DBG(pDev, "Getting the async read data of id : <%ld> \n", userValue);
            break;
#endif
#ifdef QCUSB_TEST_ONLY
        case IOCTL_QTIDEV_SESSION_TOTAL:
        {
            long long sessionVal;

            if (pDev->mDevInfo.mDevInfInfo.mDevType != QTIDEV_INF_TYPE_LPC)
            {
                QC_LOG_ERR(pDev,"SESSION_TOTAL: Invalid request\n");
                return -EFAULT;
            }
            if (copy_from_user(&sessionVal, (void *)arg, sizeof(long long)))
            {
                QC_LOG_ERR(pDev,"SESSION_TOTAL: Invalid data\n");
                return -EFAULT;
            }
            else
            {
                QC_LOG_DBG(pDev, " %d-%s: SESSION_TOTAL: <0x%llx>\n", pDev->udev->bus->busnum, pDev->udev->devpath, sessionVal);
                // QC_LOG_WARN(pDev,"QTI %d-%s: SESSION_TOTAL: <0x%llx>\n", 
                           // pDev->udev->bus->busnum, pDev->udev->devpath, sessionVal);
                pDev->mSessionTotal  = sessionVal;
                if (pDev->mLpcRead)
                {
                    if (0 != SubmitAllReadUrb(pDev))
                    {
                        QC_LOG_ERR(pDev," SESSION_TOTAL: SubmitAllReadUrb failure\n");
                    }
                }
            }
            break;
        }
#endif
        default:
            retval = -EBADRQC;
            break;
    }

    return retval;
}

static long UnlockedUserspaceQTIDevIOCTL(
   struct file *     pFilp,
   unsigned int      cmd,
   unsigned long     arg )
{
   return UserspaceQTIDevIOCTL( NULL, pFilp, cmd, arg );
}

static unsigned int UserspaceQTIDevPoll(
        struct file *                  pFilp,
        struct poll_table_struct *     pPollTable )
{
    sQTIDevUSB *pDev=NULL;
    unsigned long status = POLLOUT | POLLWRNORM;
    unsigned long flags;

    pDev = pFilp->private_data;

    QC_LOG_DBG(pDev,"-->\n");
    if (!pDev)
    {
        QC_LOG_ERR(pDev,"Invalid data\n");
        return POLLERR;
    }

    spin_lock_irqsave(&pDev->mBulkMemList.mReadMemLock, flags);

    poll_wait(pFilp, &pDev->mBulkMemList.mWaitQueue, pPollTable);


    if(!pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus)
    {
        status |= POLLIN | POLLRDNORM;
    }

    QC_LOG_DBG(pDev,"<--\n");
    spin_unlock_irqrestore(&pDev->mBulkMemList.mReadMemLock, flags);

    return (status | POLLOUT | POLLWRNORM);
}

void *io_sync_complete(struct kiocb *kiocb, void *userData)
{
    struct qtidev_aio_data *io_data;
    io_data = kiocb->private;
    QC_LOG_DBG(io_data->pDev," Inside %s\n", __func__);
    readFromRingbuff(io_data);

    up(&io_data->readSem);

    return NULL;
}

static void aio_submit_read_worker(struct work_struct *work)
{
    int ret = 0;
    unsigned long flags;
    struct qtidev_aio_data *aioDataCtx = container_of(work, struct qtidev_aio_data,
            submit_work);
    
    ret = aioDataCtx->data.iov_offset;
    QC_LOG_DBG(aioDataCtx->pDev, "-->\n");
    spin_lock_irqsave(&aioDataCtx->pDev->mBulkMemList.mReadMemLock, flags);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,17))
    if (aioDataCtx->buf && (aioDataCtx->pDev->mRefCount.refcount.counter > 1))
#else
    if (aioDataCtx->buf && (aioDataCtx->pDev->mRefCount.refcount.refs.counter > 1))
#endif
    {
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,7,19))
        use_mm(aioDataCtx->mm);
    #else
        kthread_use_mm(aioDataCtx->mm);
    #endif
        QC_LOG_DBG(aioDataCtx->pDev,"\n copy_iter offset : %d, len : %lu, mDataLen : %lu\n\n",
                (int)aioDataCtx->data.iov_offset,
                iov_iter_count(&aioDataCtx->data),
                aioDataCtx->mActualLen);
	    spin_unlock_irqrestore(&aioDataCtx->pDev->mBulkMemList.mReadMemLock, flags);
        ret = copy_to_iter(aioDataCtx->buf,
                aioDataCtx->mActualLen, &aioDataCtx->data);
        QC_LOG_DBG(aioDataCtx->pDev,"\n copy_iter offset : %d, len : %lu, mDataLen : %lu, ret = %d\n",
                (int)aioDataCtx->data.iov_offset,
                iov_iter_count(&aioDataCtx->data),
                aioDataCtx->mActualLen, ret);
		spin_lock_irqsave(&aioDataCtx->pDev->mBulkMemList.mReadMemLock, flags);
        ret = aioDataCtx->data.iov_offset;
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,7,19))
        unuse_mm(aioDataCtx->mm);
    #else
        kthread_unuse_mm(aioDataCtx->mm);
    #endif
        qti_kfree(aioDataCtx->buf);
        ret = aioDataCtx->mDataLen;
    }

    #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
        aioDataCtx->kiocb->ki_complete(aioDataCtx->kiocb, ret, 0);
    #else
        aioDataCtx->kiocb->ki_complete(aioDataCtx->kiocb, ret);
    #endif
    
    spin_unlock_irqrestore(&aioDataCtx->pDev->mBulkMemList.mReadMemLock, flags);
    kref_put(&aioDataCtx->mRefCount, ClearAioData);
    kref_put(&aioDataCtx->pDev->mRefCount, QTIDevUSBDelete);

    QC_LOG_DBG(aioDataCtx->pDev, "<--\n");
    return;
}

static void *io_async_complete(struct kiocb *kiocb, void *userData)
{
    struct qtidev_aio_data *io_data;
    int ret = 0;
    sQTIDevUSB *pDev=NULL;
    unsigned long currLen;

    QC_LOG_DBG(pDev,"-->\n");

    io_data = kiocb->private;
    pDev = userData;
    ret = io_data->data.iov_offset;
    currLen = iov_iter_count(&io_data->data);

    if (pDev) {
        unsigned long startIdx;
        unsigned long endIdx;
        unsigned long buffLen;
        unsigned long len;

        currLen = iov_iter_count(&io_data->data);
        if (!io_data->data.iov_offset)
            io_data->mDataLen = iov_iter_count(&io_data->data);

        buffLen = pDev->mBulkMemList.mReadBuffCircular.mBufflen;
        startIdx = pDev->mBulkMemList.mReadBuffCircular.mStartIdx;
        endIdx = pDev->mBulkMemList.mReadBuffCircular.mEndIdx;
        if (pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus) {
            QC_LOG_DBG(pDev,"mBufEmptyStatus true. return userData(<--)\n");
            return userData;
        }

        if (startIdx <= endIdx) {
            len = endIdx - startIdx;
            io_data->mActualLen = min(currLen, len);
        }
	else {
            len = (buffLen - (startIdx - endIdx));
            io_data->mActualLen = min(currLen, len);
        }

#ifndef QCUSB_TEST_ONLY
         currLen = io_data->mActualLen;
#else
        if (pDev->mDevInfo.mDevInfInfo.mDevType != QTIDEV_INF_TYPE_LPC)
            currLen = io_data->mActualLen;
#endif

        if (io_data->mActualLen == currLen) {
            io_data->buf = kzalloc(io_data->mActualLen, GFP_ATOMIC);
            if (!io_data->buf) {
                QC_LOG_ERR(pDev," Failed to allocate memory\n");
                io_data->mActualLen = 0;
            #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
                kiocb->ki_complete(kiocb, 0, -ENOMEM);
            #else
                kiocb->ki_complete(kiocb, -ENOMEM);
            #endif
                return NULL;
            }
            else {
                CopyFromAioReadMemList(pDev, (void **)&io_data->buf, &io_data->mActualLen);
                io_data->mDataLen = io_data->mActualLen;
                currLen = io_data->mActualLen;
            }
        }
    }
    else {
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
        kiocb->ki_complete(kiocb, 0, -ETIMEDOUT);
    #else
        kiocb->ki_complete(kiocb, -ETIMEDOUT);
    #endif
        return NULL;
    }

    if (io_data->mActualLen != currLen)
        return userData;

    kref_get(&io_data->pDev->mRefCount);
    INIT_WORK(&io_data->submit_work, aio_submit_read_worker);
    queue_work(io_data->pDev->mpWorkQ, &io_data->submit_work);
    QC_LOG_DBG(pDev,"<--\n");
    return NULL;
}

static ssize_t ioData_dequeue(struct kiocb *kiocb, void *userData)
{
    struct qtidev_aio_data *io_data;
    sIoReadList *pIoReadTraverse=NULL;
    sIoReadList *pIoReadsafe=NULL;
    struct sQTIDevUSB *pDev=NULL;
    bool found = false;
    unsigned long flags;

    if (kiocb) {
        if (!kiocb->private) {
            QC_LOG_ERR(pDev,"Should never happen\n");
            io_async_complete(kiocb, NULL);
            return 0;
        }
    }
    else {
        QC_LOG_ERR(pDev,"Invalid kiocb\n");
        return -EINVAL;
    }
    io_data = kiocb->private;
    pDev = io_data->pDev;

    QC_LOG_DBG(pDev,"-->\n");

    if (userData && (NULL != io_data->complete(kiocb, userData))) {
        QC_LOG_WARN(pDev," User Data buffer is not full (<--)\n");
        goto end;
    }

    if (pDev->mIoReadBuffListActiveSize<=0) {
	QC_LOG_ERR(pDev," No kiocb found to be popped from read_list. (<--)\n");
	return -EINVAL;
    }

    QC_LOG_DBG(pDev," Dequeue kiocb(%px) to mIoReadBuffList\n", kiocb);
    spin_lock_irqsave(&pDev->mSpinReadBuffLock, flags);
    list_for_each_entry_safe(pIoReadTraverse, pIoReadsafe, &pDev->mIoReadBuffListActive, node) {
        /* Comparing for address */
        if (kiocb == pIoReadTraverse->kiocb) {
	    found = true;
	    pIoReadTraverse->kiocb = NULL;
	    list_move_tail(&pIoReadTraverse->node, &pDev->mIoReadBuffListIdle);
	    pDev->mIoReadBuffListActiveSize--;
	    pDev->mIoReadBuffListIdleSize++;
	    QC_LOG_DBG(pDev," Buffer matched, updated mIoReadBuffListActiveSize to %lu\n", pDev->mIoReadBuffListActiveSize);
	    break;
        }
    }
    spin_unlock_irqrestore(&pDev->mSpinReadBuffLock, flags);

    /*
     * Check for circular Linkedlist entirely being parsed without a match.
     */
    if (!found) {
        QC_LOG_ERR(pDev," Kiocb context did not match any readlist node. This should never happen!\n");
        io_data->complete(kiocb, NULL);
        return -EINVAL;
    }

    if (!userData && (NULL != io_data->complete(kiocb, userData))) {
        QC_LOG_DBG(pDev," User Data buffer is not full\n");
        goto end;
    }
end:

    QC_LOG_DBG(pDev,"<--\n");
    return 0;
}

ssize_t ioData_enqueue(struct kiocb *kiocb)
{
    struct qtidev_aio_data *io_data;
    sIoReadList *pIoReadTraverse;
    unsigned long flags;

    io_data = kiocb->private;
    QC_LOG_INFO(io_data->pDev, "Enqueue kiocb(%px) to mIoReadBuffList\n", kiocb);
    spin_lock_irqsave(&io_data->pDev->mSpinReadBuffLock, flags);
    list_for_each_entry(pIoReadTraverse, &io_data->pDev->mIoReadBuffListIdle, node) {
	    pIoReadTraverse->kiocb = kiocb;
	    list_move_tail(&pIoReadTraverse->node, &io_data->pDev->mIoReadBuffListActive);
	    io_data->pDev->mIoReadBuffListActiveSize++;
	    io_data->pDev->mIoReadBuffListIdleSize--;

	    QC_LOG_DBG(io_data->pDev," Adding to queue [kiocb(%px)]. Updated mIoReadBuffListActiveSize = %lu\n", kiocb, io_data->pDev->mIoReadBuffListActiveSize);
	    spin_unlock_irqrestore(&io_data->pDev->mSpinReadBuffLock, flags);
	    return 0;
    }
    QC_LOG_ERR(io_data->pDev," Unable to enqueue new request.");
    spin_unlock_irqrestore(&io_data->pDev->mSpinReadBuffLock, flags);
    return -ENOMEM;
}

static void aio_cancel_worker(struct work_struct *work)
{
    unsigned long iflags;
    struct qtidev_aio_data *io_data = container_of(work, struct qtidev_aio_data,
            cancellation_work);

    QC_LOG_DBG(io_data->pDev,"--> \n");
    if (io_data->read  == true)
    {
        spin_lock_irqsave(&io_data->pDev->mBulkMemList.mReadMemLock, iflags);
        QC_LOG_INFO(io_data->pDev," io_data->read true and setting dequeue null \n");
        ioData_dequeue(io_data->kiocb, NULL);
        spin_unlock_irqrestore(&io_data->pDev->mBulkMemList.mReadMemLock, iflags);
    } else
    {
        usb_kill_urb(io_data->urb);
    }
    //usb_enable_autosuspend(interface_to_usbdev(io_data->pDev->interface));
    if (io_data->read  == true)
    {
        kref_put(&io_data->mRefCount, ClearAioData);
    }
    else
    {
        kref_put(&io_data->mRefCount, CallbackToRefCount);
    }

    kref_put(&io_data->pDev->mRefCount, QTIDevUSBDelete);

    QC_LOG_DBG(io_data->pDev,"<-- \n");
    return;
}

static int aio_cancel(struct kiocb *iocb)
{
    int value = 0;
    struct qtidev_aio_data *io_data;

    io_data = iocb->private;

    QC_LOG_DBG(io_data->pDev,"--> \n");

    kref_get(&io_data->pDev->mRefCount);
    INIT_WORK(&io_data->cancellation_work, aio_cancel_worker);
    queue_work(io_data->pDev->mpWorkQ, &io_data->cancellation_work);
    value = -EINPROGRESS;
    QC_LOG_DBG(io_data->pDev,"<-- \n");
    return value;
}

static size_t cpyToTargetIter(sQTIDevUSB *pDev, void *to, void *from,
        unsigned long count, unsigned long *iflags, bool isIter)
{
    size_t ret;

#ifdef QCUSB_TEST_ONLY
    QC_LOG_DBG(pDev,"cpyToTargetIter: %lu to user %d Buf<%d %d>\n", count, isIter,
         pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
         pDev->mBulkMemList.mReadBuffCircular.mEndIdx);
#endif

    if (count == 0)
    {
        return 0;
    }

    spin_unlock_irqrestore(&pDev->mBulkMemList.mReadMemLock, *iflags);

    if (isIter)
    {
        ret = !copy_to_iter(from, count, to);
    } else
    {
        ret = copy_to_user(to, from, count);
    }
    spin_lock_irqsave(&pDev->mBulkMemList.mReadMemLock, *iflags);
    return ret;
}

static int CopyFromReadMemListIter(sQTIDevUSB *pDev,
        void **ppReadData,
        size_t *pDataSize,
        unsigned long *iflags,
        bool isIter)
{
    QC_LOG_DBG(pDev,"-->\n");
    return CopyFromReadMemList(pDev, ppReadData, pDataSize, iflags, QTI_RX_ITER);
}

static ssize_t readFromRingbuff(struct qtidev_aio_data *pIoData)
{
    size_t dataSize;
    ssize_t ret = 0;
    sQTIDevUSB *pDev;
    void *data;
#ifdef QCUSB_TEST_ONLY
    unsigned long startIdx;
    unsigned long endIdx;
    unsigned long buffLen;
    unsigned long len;
#endif
    unsigned long iflags;

    pDev = pIoData->pDev;
    dataSize = iov_iter_count(&pIoData->data);
    data = &pIoData->data;
    QC_LOG_DBG(pDev, "-->\n");
    spin_lock_irqsave(&pIoData->pDev->mBulkMemList.mReadMemLock, iflags);
    if (pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus)
    {
	unsigned long startIdx;
	unsigned long endIdx;
	unsigned long buffLen;
	buffLen = pDev->mBulkMemList.mReadBuffCircular.mBufflen;
	startIdx = pDev->mBulkMemList.mReadBuffCircular.mStartIdx;
	endIdx = pDev->mBulkMemList.mReadBuffCircular.mEndIdx;
        QC_LOG_ERR(pDev," (<--) Buffer empty. Returning. empty status: %d strt: %lu end:%lu bufflen:%lu \n",
            pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus, startIdx, endIdx, buffLen);
        goto end;
    }

#ifdef QCUSB_TEST_ONLY
    if (pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC)
    {
        buffLen = pDev->mBulkMemList.mReadBuffCircular.mBufflen;
        startIdx = pDev->mBulkMemList.mReadBuffCircular.mStartIdx;
        endIdx = pDev->mBulkMemList.mReadBuffCircular.mEndIdx;
        QC_LOG_DBG(pDev, "--> LPC\n");
        if (startIdx <= endIdx)
        {
        //  len = endIdx - startIdx + 1;
            len = endIdx - startIdx;
            len = min(dataSize, len);
        } else
        {
        //  len = (buffLen - (startIdx - endIdx + 1));
            len = (buffLen - (startIdx - endIdx));
            len = min(dataSize, len);
        }

        if (len != dataSize)
        {
            goto end;
        }
    }
#endif

    if (CopyFromReadMemListIter(pDev, &data, &dataSize, &iflags, true) == true)
    {
        ret = dataSize;
    }

end:
	QC_LOG_DBG(pDev, "<--\n");
    spin_unlock_irqrestore(&pIoData->pDev->mBulkMemList.mReadMemLock, iflags);
    return ret;
}

static ssize_t processIo(struct kiocb *kiocb)
{
    ssize_t ret = 0;
    struct qtidev_aio_data *io_data;
    unsigned long iflags;

    io_data = kiocb->private;

    if (io_data == NULL)
    {
        QC_LOG_ERR(io_data->pDev,"io_data invalid\n");
        return -EINVAL;  
    }

    sema_init(&io_data->readSem, 0);

    QC_LOG_DBG(io_data->pDev," --> iov count : %d\n", (int)iov_iter_count(&io_data->data));

    spin_lock_irqsave(&io_data->pDev->mBulkMemList.mReadMemLock, iflags);
    if (io_data->pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus==false)
	{
	spin_unlock_irqrestore(&io_data->pDev->mBulkMemList.mReadMemLock, iflags);
	//potential locking issue
	ret = readFromRingbuff(io_data);
	if (ret)
	{
		QC_LOG_INFO(io_data->pDev," (<--) successfully read %d\n", (int)ret);
		return ret;
	}
    }
    QC_LOG_INFO(io_data->pDev,"--> To be added to queue, io_data->aio/read:%d/%d\n", io_data->aio, io_data->read);

    /* In case of sync & read */
    if (!io_data->aio && io_data->read)
    {
        if (!(kiocb->ki_filp->f_flags & O_NONBLOCK))
        {
            io_data->complete = io_sync_complete;
            ret = ioData_enqueue(kiocb);
            if (unlikely(ret < 0))
            {
                QC_LOG_ERR(io_data->pDev,"Failed to add to queue (<--)\n");
                goto end;
            }
            spin_unlock_irqrestore(&io_data->pDev->mBulkMemList.mReadMemLock, iflags);
            ret = down_interruptible(&io_data->readSem);
            spin_lock_irqsave(&io_data->pDev->mBulkMemList.mReadMemLock, iflags);
            if (ret != 0)
            {
                QC_LOG_ERR(io_data->pDev,"Interrupted %d (<--)\n", (int)ret);
                ioData_dequeue(kiocb, NULL);
                goto end;
            }
            ret = io_data->len;
        }
        else
        {
            ret = -EAGAIN;
        }
    } else if (io_data->aio && io_data->read)
    {
        io_data->complete = io_async_complete;
        ret = ioData_enqueue(kiocb);
        if (unlikely(ret < 0))
        {
            QC_LOG_ERR(io_data->pDev,"Failed to add to queue (<--)\n");
            goto end;
        }
        //usb_disable_autosuspend(interface_to_usbdev(io_data->pDev->interface));

        ret = -EIOCBQUEUED;
    }

end:
    QC_LOG_DBG(io_data->pDev,"<--\n");
    spin_unlock_irqrestore(&io_data->pDev->mBulkMemList.mReadMemLock, iflags);
    return ret;
}


static ssize_t UserspaceQTIDevReadIter(struct kiocb *kiocb, struct iov_iter *iov)
{
    ssize_t res = 0;
    struct qtidev_aio_data *aioDataCtx;
    sQTIDevUSB *pDev=NULL;

    if (kiocb == NULL)
    {
        QC_LOG_ERR(pDev,"kiocb invalid\n");
        return -EINVAL;  
    }
	
    aioDataCtx = qti_kmalloc(sizeof(struct qtidev_aio_data), GFP_KERNEL);
    if (unlikely(!aioDataCtx))
    {
        QC_LOG_ERR(pDev,"Failed to alloctate memory\n");
        return -ENOMEM;
    }

    kref_init(&aioDataCtx->mRefCount);

    if(!is_sync_kiocb(kiocb))
    {
        aioDataCtx->aio = true;
    } else
    {
        aioDataCtx->aio = false;
    }

    pDev = kiocb->ki_filp->private_data;
    aioDataCtx->read  = true;
    aioDataCtx->kiocb = kiocb;
    aioDataCtx->pDev  = pDev;

    QC_LOG_DBG(pDev, "PID = %u, Pname = %s, tgid= %u\n",task_pid_nr(current),current->comm, task_tgid_nr(current));
    if(aioDataCtx->aio)
    {
        aioDataCtx->to_free = dup_iter(&aioDataCtx->data, iov, GFP_KERNEL);
        if (aioDataCtx->to_free == NULL)
        {
            QC_LOG_ERR(pDev,"Failed to duplicate iter\n");
            qti_kfree(aioDataCtx);
            return -ENOMEM;
        }
    }
    else
    {
        aioDataCtx->data = *iov;
    }
    aioDataCtx->mm = current->mm;
    kiocb->private = aioDataCtx;

    if (aioDataCtx->aio && (kiocb != NULL)) {
        kiocb_set_cancel_fn(kiocb, aio_cancel);
        QC_LOG_DBG(pDev,"kiocb_set_cancel_fn is get called\n");
    }

    res = processIo(kiocb);
    if (res == -EIOCBQUEUED)
    {
        QC_LOG_DBG(pDev,"IO message has been queued\n");
        return res;
    }

    if (aioDataCtx->aio)
    {
        qti_kfree(aioDataCtx->to_free);
        qti_kfree(aioDataCtx);
    }
    else
    {
        *iov = aioDataCtx->data;
    }

    return res;
}

static void aio_submit_worker(struct work_struct *work)
{
    struct qtidev_aio_data *aioDataCtx = container_of(work, struct qtidev_aio_data,
            submit_work);

	if(aioDataCtx->mActualLen) {
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
	    aioDataCtx->kiocb->ki_complete(aioDataCtx->kiocb, aioDataCtx->mActualLen, 0);
    #else
        aioDataCtx->kiocb->ki_complete(aioDataCtx->kiocb, aioDataCtx->mActualLen);
    #endif
    }
	else {
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
	    aioDataCtx->kiocb->ki_complete(aioDataCtx->kiocb, aioDataCtx->mActualLen, aioDataCtx->mDataLen);
    #else
        aioDataCtx->kiocb->ki_complete(aioDataCtx->kiocb, aioDataCtx->mDataLen);
    #endif
    }

    QC_LOG_INFO(aioDataCtx->pDev,"Done with ki_complete\n");

    kref_put(&aioDataCtx->mRefCount, CallbackToRefCount);
    kref_put(&aioDataCtx->pDev->mRefCount, QTIDevUSBDelete);
    qti_kfree(aioDataCtx);

    return;
}

static void UserspaceQTIDevWrite_bulk_async_callback(struct urb *urb)
{
    sQTIDevUSB *dev;
    int ret = 0;
    struct qtidev_aio_data *aioDataCtx;

    aioDataCtx = urb->context;
    dev = aioDataCtx->pDev;

    QC_LOG_DBG(dev,"%px:%px\n", aioDataCtx, aioDataCtx->kiocb);
    /* sync/async unlink faults aren't errors */
    if (urb->status || (urb->status != -EINPROGRESS)) {
        spin_lock(&dev->mSpinErrLock);
        dev->mLasterror = urb->status;
        spin_unlock(&dev->mSpinErrLock);
    }
    if (!urb->status || (urb->status == -EINPROGRESS))
    {
#ifdef QCUSB_TEST_ONLY
        QC_LOG_DBG(dev,"mDevType= %d, mLpcRead = %x, Status: 0x%X\n", dev->mDevInfo.mDevInfInfo.mDevType, dev->mLpcRead, urb->status);
        if ((dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC) && !dev->mLpcRead)
        {
            dev->mLpcRead = 1;
            QC_LOG_INFO(dev,"LPC mLpcRead now %x\n", dev->mLpcRead);
            ret = SubmitAllReadUrb(dev);
            if (ret != 0)
            {
                QC_LOG_ERR(dev,"Error in reading\n");
            }
        }
#endif

        ret = urb->actual_length;
		QC_LOG_INFO(dev," Actual Length sent is %d \n",urb->actual_length);
		aioDataCtx->mActualLen = urb->actual_length;
    } else
	{
		aioDataCtx->mActualLen = 0;
		aioDataCtx->mDataLen = urb->status;
	}

	QC_LOG_INFO(dev,"io_submit_worker Status: %d\n", urb->status);
    /* free up our allocated buffer */
    qti_kfree(urb->transfer_buffer);
    kref_get(&aioDataCtx->pDev->mRefCount);
    INIT_WORK(&aioDataCtx->submit_work, aio_submit_worker);
    queue_work(aioDataCtx->pDev->mpWorkQ, &aioDataCtx->submit_work);
    //qti_kfree(aioDataCtx);
	usb_free_urb(urb);
    return;
}

ssize_t UserspaceQTIDevWriteIter(struct kiocb *kiocb, struct iov_iter *iov)
{
    ssize_t res = 0;
    struct qtidev_aio_data *aioDataCtx;
    sQTIDevUSB *pDev=NULL;
    struct urb *urb;
    char *buf = NULL;
    int writesize = 0;

    if (kiocb == NULL)
    {
        QC_LOG_ERR(pDev,"kiocb invalid\n");
        return -EINVAL;  
    }

    if(!is_sync_kiocb(kiocb))
    {
        aioDataCtx = qti_kmalloc(sizeof(struct qtidev_aio_data), GFP_KERNEL);
        if (unlikely(!aioDataCtx))
        {
            QC_LOG_ERR(pDev,"Failed to alloctate memory\n");
            return -ENOMEM;
        }
        aioDataCtx->aio = true;
    } else
    {
        QC_LOG_ERR(pDev,"No need to handle\n");
        return -EINVAL;
    }
    kref_init(&aioDataCtx->mRefCount);

    pDev = kiocb->ki_filp->private_data;
    writesize = iov_iter_count(iov);
    aioDataCtx->read  = false;
    aioDataCtx->kiocb = kiocb;
    aioDataCtx->pDev  = pDev;
    aioDataCtx->mm = current->mm;
    kiocb->private = aioDataCtx;
    QC_LOG_DBG(pDev, "PID = %u, Pname = %s, tgid= %u\n",task_pid_nr(current),current->comm, task_tgid_nr(current));
    /* create a urb, and a buffer for it, and copy the data to the urb */
    urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!urb) {
        qti_kfree(aioDataCtx);
        return -ENOMEM;
    }

    aioDataCtx->urb = urb;
    buf = qti_kmalloc(writesize, GFP_KERNEL);
    if (!buf) {
        QC_LOG_ERR(pDev,"Failed to alloctate memory\n");
        qti_kfree(aioDataCtx);
        usb_free_urb(urb);
        return -ENOMEM;
    }

    if (!copy_from_iter(buf, writesize, iov)) {
        QC_LOG_ERR(pDev,"Failed to copy_from_iter\n");
        qti_kfree(aioDataCtx);
        qti_kfree(buf);
        usb_free_urb(urb);
        return -EFAULT;
    }
    QC_LOG_INFO(pDev,"data length is %d \n",writesize);
    /*
     * initialize the urb properly
     */
    usb_fill_bulk_urb(urb, pDev->udev,
            usb_sndbulkpipe(pDev->udev, pDev->mBulkOutEndAddr),
            buf, writesize, UserspaceQTIDevWrite_bulk_async_callback, aioDataCtx);
    urb->transfer_flags |= URB_ZERO_PACKET;
#ifdef QCUSB_TEST_ONLY
    if (pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC)
    {
        QC_LOG_DBG(pDev, "LPC: clear ZLP flag\n");
        urb->transfer_flags &= ~URB_ZERO_PACKET;  // clear ZLP flag
    }
#endif
    usb_anchor_urb(urb, &pDev->submitted);

    if (aioDataCtx->aio && (kiocb != NULL)) {
        kiocb_set_cancel_fn(kiocb, aio_cancel);
        QC_LOG_INFO(pDev,"kiocb_set_cancel_fn is get called\n");
    }

    /* send the data out the bulk port */
    res = usb_submit_urb(urb, GFP_KERNEL);
    if (res) {
		QC_LOG_ERR(pDev,"failed submitting write urb, error %ld\n", res);
		usb_unanchor_urb(urb);
        qti_kfree(buf);
		usb_free_urb(urb);
        qti_kfree(aioDataCtx);
		return res;
	}
    res = -EIOCBQUEUED;
    return res;
}


static const struct file_operations UserSpaceQdssFops = {
    .owner  =   THIS_MODULE,
    .read   =   UserspaceQTIDevRead,
    .write  =   UserspaceQTIDevWrite,
    .read_iter = UserspaceQTIDevReadIter,
    .write_iter = UserspaceQTIDevWriteIter,
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,35 ))
   .ioctl   =   UserspaceQTIDevIOCTL,
#else
   .unlocked_ioctl = UnlockedUserspaceQTIDevIOCTL,
#endif
    .open   =   UserspaceQTIDevOpen,
    .release=   UserspaceQTIDevRelease,
    .flush  =   UserspaceQTIDevFlush,
    .llseek =   UserspaceQTIDevllseek,
    .poll   =   UserspaceQTIDevPoll,
};

static int RegisterQDSSDevice(sQTIDevUSB *pDev)
{
    int result = -ENAVAIL;
    char fqDevName[255];
    dev_t devno;
    struct device *dev_ret;
    QC_LOG_DBG(pDev,"");

    // allocate and fill devno with numbers
    if (pDev->mDevInfo.mClassType == QTIDEV_INF_CLASS_PORTS)
    {
        result = alloc_chrdev_region( &devno, 0, 1, QTIDEV_PORT_CLASS_NAME);
    } else if (pDev->mDevInfo.mClassType == QTIDEV_INF_CLASS_USB)
    {
        result = alloc_chrdev_region( &devno, 0, 1, QTIDEV_USB_CLASS_NAME);
    }
    if (result < 0)
    {
        return result;
    }

    // Create cdev
    cdev_init( &pDev->mCdev, &UserSpaceQdssFops );
    pDev->mCdev.owner = THIS_MODULE;
    pDev->mCdev.ops = &UserSpaceQdssFops;

    result = cdev_add( &pDev->mCdev, devno, true );
    if (result != 0)
    {
        QC_LOG_ERR(pDev, "error adding cdev\n" );
        unregister_chrdev_region(devno, true);
        return result;
    }

    QC_LOG_INFO(pDev,"QDSS: product : %s\n", pDev->udev->product);
    QC_LOG_INFO(pDev,"QDSS: manufacturer : %s\n", pDev->udev->manufacturer);
    QC_LOG_INFO(pDev,"QDSS: serial : %s\n", pDev->udev->serial);

    pDev->mDevNum = devno;
    if (IS_ERR(dev_ret = device_create(pDev->mDevInfo.mpDevClass,
	&pDev->interface->dev,
	devno,
	NULL,
	"%s_%d-%s:%d.%d",
	pDev->mDevInfo.mDevInfInfo.mpKey,
	pDev->udev->bus->busnum,
	pDev->udev->devpath,
	pDev->udev->actconfig->desc.bConfigurationValue,
	pDev->interface->cur_altsetting->desc.bInterfaceNumber)))
                  {
                     printk("QDSS: Device creation failed :%ld ",PTR_ERR(dev_ret));
                     class_destroy(pDev->mDevInfo.mpDevClass);
                     unregister_chrdev_region(devno, 1);
                     return PTR_ERR(dev_ret);
                  }


    sprintf(fqDevName,"%s_%d-%s:%d.%d",pDev->mDevInfo.mDevInfInfo.mpKey,
        pDev->udev->bus->busnum,
        pDev->udev->devpath,
        pDev->udev->actconfig->desc.bConfigurationValue,
        pDev->interface->cur_altsetting->desc.bInterfaceNumber);
    strscpy(pDev->fqDevName, fqDevName, 255);//Filling the fully qualified device name

     /* let the user know what node this device is now attached to */
    QC_LOG_DBG(pDev,"QDSS Bulk In/Out device now attached to '%s'\n",
            pDev->fqDevName);

    return 0;
}

static void DeregisterQDSSDevice(sQTIDevUSB *pDev)
{
    QC_LOG_DBG(pDev,"");
    // Remove device (so no more calls can be made by users)
    if (IS_ERR(pDev->mDevInfo.mpDevClass) == false)
    {
        device_destroy(pDev->mDevInfo.mpDevClass, pDev->mDevNum);
    }

    cdev_del( &pDev->mCdev );

    unregister_chrdev_region( pDev->mDevNum, true );
}

static int ReadBulkUSB( sQTIDevUSB *pDev, struct urb * pBlkURB )
{
    int status;
    int readLength;

    if (pBlkURB == NULL)
        return -1;

    readLength = pBlkURB->transfer_buffer_length;

#ifdef QCUSB_TEST_ONLY
    if (pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC)
    {
        unsigned long iflags;

        spin_lock_irqsave(&pDev->mTestLock, iflags);
        if (pDev->mSessionTotal >= (long long)pDev->mBulkInSize)
        {
            pDev->mSessionTotal -= (long long)pDev->mBulkInSize;
            readLength = pDev->mBulkInSize;
        }
        else
        {
            if (pDev->mSessionTotal > 0LL)
            {
                readLength = (int)pDev->mSessionTotal; // last read
            }
            else // this should not happen
            {
                readLength = 1024;
            }
        }
        spin_unlock_irqrestore(&pDev->mTestLock, iflags);
    }
#endif  // QCUSB_TEST_ONLY

    usb_fill_bulk_urb(pBlkURB,
                      pBlkURB->dev,
                      pBlkURB->pipe,
                      pBlkURB->transfer_buffer,
                      readLength,
                      pBlkURB->complete,
                      pBlkURB->context);

    status = usb_submit_urb( pBlkURB, GFP_ATOMIC );
    if (status != 0)
    {
        QC_LOG_ERR(pDev, "Error sending URB for %dB (%d)\n", readLength, status );
    }
    else
    {
        QC_LOG_DBG(pDev, "read for %d bytes\n", readLength );
    }

    return status;
}  // ReadBulkUSB

static int ResubmitBulkURB( struct urb * pBlkURB )
{
   int status;
   sQTIDevUSB *pDev = NULL;
   sBulkUrbList *pUrbItem;

   // Sanity test
   if ( (pBlkURB == NULL)
   ||   (pBlkURB->dev == NULL) )
   {
      QC_LOG_ERR(pDev,"Error: no dev\n");
      return -EINVAL;
   }

   pUrbItem = pBlkURB->context;
   pDev = pUrbItem->Context;

   QC_LOG_DBG(pDev,"-->\n");

   // Reschedule URB
   status = ReadBulkUSB( pDev, pBlkURB );
   if (status != 0)
   {
      QC_LOG_ERR(pDev,"Error re-submitting read URB %d\n", status );
   }

   QC_LOG_DBG(pDev,"<--\n");
   return status;
}

static bool AddToReadMemList(sQTIDevUSB *pDev,
        void *pDataCopy, unsigned long dataSize)
{
    void *pBulkStartInBuffer;
    unsigned int startIdx;
    unsigned int endIdx;
    unsigned int buffLen;

    pBulkStartInBuffer = pDev->mBulkMemList.mReadBuffCircular.mBulk_in_buffer;
    buffLen = pDev->mBulkMemList.mReadBuffCircular.mBufflen;
    startIdx = pDev->mBulkMemList.mReadBuffCircular.mStartIdx;
    endIdx = pDev->mBulkMemList.mReadBuffCircular.mEndIdx;

#ifdef QDSS_INT_BULK_BUFFER
    if (startIdx <= endIdx)
    {
        if ((startIdx == endIdx) && (pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus == false))
        {
           QC_LOG_ERR(pDev,"buffer full: add to queue (<--)\n");
           return false;
        }

        if (dataSize <= (buffLen - (endIdx - startIdx)))  // enough space for even wrap-around
        {
            int partialLen = 0;

            partialLen = (dataSize <= (buffLen - endIdx)) ? dataSize : buffLen - endIdx;
            memmove(pBulkStartInBuffer+ endIdx, pDataCopy, partialLen);
            if (dataSize != partialLen)  // need to wrap around
            {
                memmove(pBulkStartInBuffer,
                        pDataCopy + partialLen, dataSize - partialLen);  // wrap around filling
                pDev->mBulkMemList.mReadBuffCircular.mEndIdx
                    = dataSize - partialLen;
                QC_LOG_DBG(pDev,"(1) st : %d, end: %d, data : %lu\n",
                        pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
                        pDev->mBulkMemList.mReadBuffCircular.mEndIdx,dataSize);
            }
            else  // mEndIdx still <= buffer end
            {
                pDev->mBulkMemList.mReadBuffCircular.mEndIdx += dataSize;
                QC_LOG_DBG(pDev,"(2) st : %d, end: %d, data : %lu\n",
                        pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
                        pDev->mBulkMemList.mReadBuffCircular.mEndIdx, dataSize);
            }
        }
        else
        {
            QC_LOG_ERR(pDev,"Insufficient buf space, add to queue (<--)\n");
            return false;
        }
    }
    else if (dataSize <= (startIdx - endIdx))  // already wrap-around
    {
        memmove(pBulkStartInBuffer + endIdx, pDataCopy, dataSize);
        pDev->mBulkMemList.mReadBuffCircular.mEndIdx += dataSize;
        QC_LOG_DBG(pDev,"(3) st : %d, end: %d, data : %lu\n",
                pDev->mBulkMemList.mReadBuffCircular.mStartIdx,
                pDev->mBulkMemList.mReadBuffCircular.mEndIdx, dataSize);
    }
    else
    {
        QC_LOG_ERR(pDev,"wrap-around: Insufficient buf space, add to queue (<--)\n");
        return false;
    }
    pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus = false;
#endif

    return true;
}

static void BlkCallback(struct urb *urb)
{
    sQTIDevUSB *pDev=NULL;
    unsigned long iflags;
    sIoReadList *pIoReadTraverse;
    sIoReadList *psafePtr;
    sBulkUrbList *pUrbItem;

    if (!urb || !urb->context)
    {
        QC_LOG_ERR(pDev,"Invalid URB \n");
        return;
    }
    pUrbItem = urb->context;
    pDev = pUrbItem->Context;

    QC_LOG_DBG(pDev,"-->\n");

    if (urb->status && urb->status != -EOVERFLOW)
    {
        if (urb->status != -EOVERFLOW)
        {
            return;
        }
    }
    else if (urb->actual_length)
    {
        pDev->mStats.RxCount += urb->actual_length;

        /* Check whether an application is opened */
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,17))
        if (pDev->mRefCount.refcount.counter < 2)
#else
        if (pDev->mRefCount.refcount.refs.counter < 2)
#endif
        {
            char *urbData;
            urbData = urb->transfer_buffer;
            QC_LOG_DBG(pDev,"--> No application is opened, so no need to copy the urb :%d ref %d\n",
                    urb->actual_length,
                    #if (LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,17)) 
                        pDev->mRefCount.refcount.counter
                    #else 
                        pDev->mRefCount.refcount.refs.counter
                    #endif
                );
        }
        else
        {
            char *urbData;
            unsigned long urbLen = 0;

            spin_lock_irqsave(&pDev->mBulkMemList.mReadMemLock, iflags);

#ifdef QCUSB_TEST_ONLY

	    if (pDev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC) {
		    LpcOutputBytes((unsigned char *)urb->transfer_buffer, pDev->mStats.USBRxCnt,
		    pDev->mBulkMemList.mReadBuffCircular.mEndIdx, "LPC-USB");
	    }
    
#endif
	    pDev->mStats.USBRxCnt += urb->actual_length;
            urbData = urb->transfer_buffer;
            urbLen = urb->actual_length;

            if (urbLen) {
                if (pDev->mBulkUrbBuffListSize > 0 ||
			AddToReadMemList(pDev, urbData, urbLen) == false) {
                    QC_LOG_DBG(pDev,"should be added to queue : %d RxCount 0x%lx BufListSize %lu (<--)\n",
			    urb->actual_length, pDev->mStats.RxCount, pDev->mBulkUrbBuffListSize);
                    AddToQueue(pDev, urb);
                    spin_unlock_irqrestore(&pDev->mBulkMemList.mReadMemLock,
			    iflags);
                    return;
                }
                NotifyUserRequest(pDev, NULL);
                /* Possibly notify poll() that data exists */
                wake_up_interruptible(&pDev->mBulkMemList.mWaitQueue);
            }

	    /*
	     * list_for_each_entry_safe takes care of the deletion in the read_list
	     * being done via ioData_dequeue.
	     */
	    QC_LOG_DBG(pDev," --> Calling ioData_dequeue. Read_list has %lu nodes.\n", pDev->mIoReadBuffListActiveSize);
	    list_for_each_entry_safe(pIoReadTraverse, psafePtr, &pDev->mIoReadBuffListActive, node ) {
		if (pIoReadTraverse->kiocb) {
		    if(ioData_dequeue(pIoReadTraverse->kiocb, pDev) < 0)
			break;
		}
	    }
            QC_LOG_DBG(pDev,"%d-%s: got %d RxCount %ld endIdx %d <--\n", pDev->udev->bus->busnum, pDev->udev->devpath, urb->actual_length, pDev->mStats.RxCount,
                 pDev->mBulkMemList.mReadBuffCircular.mEndIdx);
            spin_unlock_irqrestore(&pDev->mBulkMemList.mReadMemLock, iflags);
        }
    }

    ResubmitBulkURB(urb);
    QC_LOG_DBG(pDev,"<--\n");
    return;
}

static bool PopFromQueueAndSubmit(sQTIDevUSB *pDev)
{
    sBulkUrbList *pBulkUrbMem = NULL;
    int idx = 0;
    int retval;
    unsigned int availableSpace;
    int foundUrb = 0;

    if (pDev->mBulkUrbBuffListSize <= 0) {
        QC_LOG_DBG(pDev,"mBulkUrbBuffList empty. No data found to pop (<--)\n");
        return false;
    }

    list_for_each_entry(pBulkUrbMem, &pDev->mBulkUrbBuffList, node) {
	if (pBulkUrbMem->mBulk_in_buffer != NULL) {
            foundUrb = 1;  // should be the very first item
	    break;
	}
	idx++;
    }

    if (foundUrb == 0) {
        QC_LOG_EXCEPTION(pDev,"No bulk_in_buffer match found for URB list and URB buff list. Bug!\n");
	idx = 0;
	list_for_each_entry(pBulkUrbMem, &pDev->mBulkUrbBuffList, node) {
           QC_LOG_DBG(pDev,"list[%d] vs idx[%d] (<--)\n", pBulkUrbMem->mIndex, idx);
	   idx++;
        }
        return false;
    }
    else if (idx > 0)
    {
        QC_LOG_EXCEPTION(pDev,"QTI-ALERT %d-%s: URB queue anomaly!--%d\n",  pDev->udev->bus->busnum, pDev->udev->devpath, idx);
    }
    idx = pBulkUrbMem->mIndex;

    availableSpace = ReadBufferFreeSpace(pDev);
    QC_LOG_DBG(pDev,"RX buf space: %u buffer empty %d\n", availableSpace, pDev->mBulkMemList.mReadBuffCircular.mBufEmptyStatus);
    if (availableSpace < pDev->mBulkUrbList[idx].mBulk_in_urb->actual_length) {
        QC_LOG_ERR(pDev,"buf[%d] No pop - Insufficient space: %u/%u (<--)\n",
	    idx, availableSpace, pDev->mBulkUrbList[idx].mBulk_in_urb->actual_length);
        return false;
    }

    pBulkUrbMem->mBulk_in_buffer = NULL;
    pDev->mBulkUrbBuffListSize--;
    list_rotate_left(&pDev->mBulkUrbBuffList);	    // To move the idle node to the end of list

    QC_LOG_DBG(pDev,"deQ URB [%d]\n", idx);

    if (AddToReadMemList(pDev,
                pDev->mBulkUrbList[idx].mBulk_in_urb->transfer_buffer,
                pDev->mBulkUrbList[idx].mBulk_in_urb->actual_length) == false)
    {
        QC_LOG_ERR(pDev,"Error allocating pReadMemListEntry "
                "read will be discarded (<--)\n");
        AddToQueue(pDev, pDev->mBulkUrbList[idx].mBulk_in_urb);
        return false;
    }

    NotifyUserRequest(pDev, NULL);
    /* Possibly notify poll() that data exists */
    wake_up_interruptible(&pDev->mBulkMemList.mWaitQueue);

    if (!IS_URB_INITIALIZED(pDev->mBulkUrbList[idx].mUrbStatus) ||
            IS_URB_SUBMITTED(pDev->mBulkUrbList[idx].mUrbStatus))
    {
        /* Either URB is not initialized or already submitted */
        QC_LOG_INFO(pDev,"(<--)\n");
        return true;
    }

    /* Submit Bulk URB buffers */
    retval = ResubmitBulkURB(pDev->mBulkUrbList[idx].mBulk_in_urb);
    if (retval < 0) {
        QC_LOG_ERR(pDev,"Failed to submitting read urb, error %d\n", retval);
        pDev->mBulkUrbList[idx].mUrbStatus = BULK_URB_INITIALIZED;;
    }
    else
    {
        pDev->mBulkUrbList[idx].mUrbStatus |= BULK_URB_SUBMITTED;
    }

    return true;
}

static void AddToQueue(sQTIDevUSB *pDev, struct urb *pUrb)
{
    sBulkUrbList *pBulkUrbMem;
    sBulkUrbList *pBulkUrbTraverse;
    void *pDataCopy;
    sBulkUrbList *pUrbItem;
    int idx = 0;

    pUrbItem = pUrb->context;
    pDataCopy = pUrb->transfer_buffer;

    for (idx = 0; idx < BULK_URB_LIST; idx++) {
        if (pDataCopy == pDev->mBulkUrbList[idx].mBulk_in_urb->transfer_buffer) {
            pBulkUrbMem = &pDev->mBulkUrbList[idx];
            break;
        }
    }

    if (idx == BULK_URB_LIST) {
	QC_LOG_EXCEPTION(pDev,"No match for pDataCopy(%px) in mBulkUrbList. Bug! (<--)\n", pDataCopy);
        return;
    }

    QC_LOG_DBG(pDev,"enQ URB[%d] vs %d <--\n", idx, pUrbItem->mIndex);

    list_for_each_entry(pBulkUrbTraverse, &pDev->mBulkUrbBuffList, node) {
	if (!pBulkUrbTraverse->mBulk_in_buffer) {
	    pBulkUrbMem->mUrbStatus &= (~BULK_URB_SUBMITTED);
	    pBulkUrbTraverse->mUrbStatus = pBulkUrbMem->mUrbStatus;
	    pBulkUrbTraverse->mBulk_in_urb = pBulkUrbMem->mBulk_in_urb;
	    pBulkUrbTraverse->mBulk_in_buffer = pBulkUrbMem->mBulk_in_buffer;
	    pBulkUrbTraverse->mIndex = pBulkUrbMem->mIndex;
	    pBulkUrbTraverse->Context = pBulkUrbMem->Context;
	    pDev->mBulkUrbBuffListSize++;
	    return;
	}
    }

    QC_LOG_EXCEPTION(pDev,"No bulk_if_buffer match found for URB list and URB buff list. Bug! (<--)\n");
    return;
}

static int InitializeURB(sQTIDevUSB *pDev)
{
    int idx = 0;
    int blkLen;
    sBulkUrbList *pBulkUrbList;
    unsigned long flags;

    spin_lock_irqsave(&pDev->mSpinReadBuffLock, flags);
    INIT_LIST_HEAD(&pDev->mIoReadBuffListActive);
    INIT_LIST_HEAD(&pDev->mIoReadBuffListIdle);
    INIT_LIST_HEAD(&pDev->mBulkUrbBuffList);
    pDev->mIoReadBuffListActiveSize = 0;
    pDev->mIoReadBuffListIdleSize = BULK_URB_LIST;
    pDev->mBulkUrbBuffListSize = 0;

   pDev->mpIntURB = usb_alloc_urb( 0, GFP_KERNEL );
   if (pDev->mpIntURB == NULL)
   {
      QC_LOG_ERR(pDev,"Error allocating int urb\n" );
      return -ENOMEM;
   }

   // Create data buffers
   pDev->mpIntBuffer = kzalloc( DEFAULT_READ_URB_LENGTH, GFP_KERNEL );
   if (pDev->mpIntBuffer == NULL)
   {
      QC_LOG_ERR(pDev,"Error allocating int buffer\n" );
      usb_free_urb( pDev->mpIntURB );
      pDev->mpIntURB = NULL;
      return -ENOMEM;
   }   

    while( idx < BULK_URB_LIST) {
        pDev->mBulkUrbList[idx].Context = (void *)pDev;
        pDev->mBulkUrbList[idx].mIndex = idx;
	/* Allocate URB buffers */
	pDev->mBulkUrbList[idx].mBulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pDev->mBulkUrbList[idx].mBulk_in_urb) {
		QC_LOG_ERR(pDev,"Could not allocate.mBulk_in_urb\n");
		return -ENOMEM;
	}

        blkLen = pDev->mBulkInSize;
        /* Create data buffers */
        pDev->mBulkUrbList[idx].mBulk_in_buffer = qti_kmalloc(blkLen, GFP_KERNEL);
        if (!pDev->mBulkUrbList[idx].mBulk_in_buffer) {
            QC_LOG_ERR(pDev,"Could not allocate mBulk_in_buffer\n");
            usb_free_urb(pDev->mBulkUrbList[idx].mBulk_in_urb);
            pDev->mBulkUrbList[idx].mBulk_in_urb = NULL;
            return -ENOMEM;
        }

        pBulkUrbList = qti_kmalloc(sizeof(sBulkUrbList), GFP_KERNEL);
        if (!pBulkUrbList) {
            QC_LOG_ERR(pDev,"Could not allocate mBulkUrbList\n");
            usb_free_urb(pDev->mBulkUrbList[idx].mBulk_in_urb);
            pDev->mBulkUrbList[idx].mBulk_in_urb = NULL;
            qti_kfree(pDev->mBulkUrbList[idx].mBulk_in_buffer);
            pDev->mBulkUrbList[idx].mBulk_in_buffer = NULL;
            return -ENOMEM;
        }

    pDev->mBulkUrbList[idx].mUrbStatus = BULK_URB_INITIALIZED;

	list_add_tail(&pBulkUrbList->node, &pDev->mBulkUrbBuffList);

	pDev->mIoReadList[idx].kiocb = NULL;
	list_add_tail(&pDev->mIoReadList[idx].node, &pDev->mIoReadBuffListIdle);

        idx++;
    }
    spin_unlock_irqrestore(&pDev->mSpinReadBuffLock, flags);
    return 0;
}

static void FinalizeURB(sQTIDevUSB *pDev)
{
    int i;

    QC_LOG_INFO(pDev,"-->\n");
    for (i = 0; i < BULK_URB_LIST; i++)
    {
        if (pDev->mBulkUrbList[i].mBulk_in_urb == NULL)
        {
            QC_LOG_INFO(pDev,"URB[%d] NULL for dev 0x%px\n", i, pDev);
            continue;
        }
        usb_fill_bulk_urb(pDev->mBulkUrbList[i].mBulk_in_urb,
                pDev->udev,
                usb_rcvbulkpipe (pDev->udev, pDev->mBulkInEndAddr),
                pDev->mBulkUrbList[i].mBulk_in_buffer,
                pDev->mBulkInSize,
                BlkCallback,
                (void *)&(pDev->mBulkUrbList[i])); // pDev);

        pDev->mBulkUrbList[i].mUrbStatus = BULK_URB_INITIALIZED;
    }
    QC_LOG_INFO(pDev,"<--\n");
}

static int SubmitAllReadUrb(sQTIDevUSB *pDev)
{
    int retval = 0;
    int idx = 0;
    int submitted = 0;
    struct urb *pUrb;

    if (!pDev) {
        QC_LOG_ERR(pDev,"Invalid device \n");
        return -ENXIO;
    }
    QC_LOG_DBG(pDev,"-->\n");
    while( idx < BULK_URB_LIST)
    {
        if (!IS_URB_INITIALIZED(pDev->mBulkUrbList[idx].mUrbStatus) ||
                IS_URB_SUBMITTED(pDev->mBulkUrbList[idx].mUrbStatus))
        {
            /* Either URB is not initialized or already submitted */
            idx++;
            continue;
        }
        pUrb = pDev->mBulkUrbList[idx].mBulk_in_urb;
        /* Submit Bulk URB buffers */
        retval = ReadBulkUSB(pDev, pUrb);
        if (retval < 0) {
            QC_LOG_ERR(pDev,"Failed to submitting read urb, error %d\n", retval);
            pDev->mBulkUrbList[idx].mUrbStatus = false; /* Reset the status */
            retval = (retval == -ENOMEM) ? retval : -EIO;
            break;
        }
        pDev->mBulkUrbList[idx].mUrbStatus |= BULK_URB_SUBMITTED;
        idx++;
	submitted++;
    }
    QC_LOG_DBG(pDev,"<--submitted %d read URBs\n", submitted);
    return retval;
}

static void DeInitializeURB(sQTIDevUSB *pDev)
{
    int i;
    int idx = 0;
    sBulkUrbList *pBulkUrbListSafe = NULL;
    sBulkUrbList *pBulkUrbListTraverse = NULL;
    sIoReadList *pIoReadTraverse = NULL;
    sIoReadList *pIoReadsafe = NULL;
    unsigned long flags;

    QC_LOG_DBG(pDev,"");
    if (!pDev){
        QC_LOG_ERR(pDev,"Invalid data\n");
        return;
    }

    if (pDev->mpIntURB != NULL)
    {
        // if (pDev->mpIntURB->status &&
        //     pDev->mpIntURB->status != -EOVERFLOW)
        // {
       usb_kill_urb(pDev->mpIntURB);
       usb_free_urb(pDev->mpIntURB);
       pDev->mpIntURB = NULL;
        // }
    }
    else
    {
       QC_LOG_WARN(pDev,"WARNING: NULL INT URB\n");
    }
    /* Release buffers */
    if (pDev->mpIntBuffer != NULL)
    {
       qti_kfree(pDev->mpIntBuffer);
       pDev->mpIntBuffer = NULL;
    }
    else
    {
        QC_LOG_WARN(pDev,"WARNING: NULL INT buffer\n");
    }

    while (idx < BULK_URB_LIST) {
        if (pDev->mBulkUrbList[idx].mBulk_in_urb != NULL)
        {
            if (pDev->mBulkUrbList[idx].mBulk_in_urb->status &&
                pDev->mBulkUrbList[idx].mBulk_in_urb->status != -EOVERFLOW)
            {
                pDev->mBulkUrbList[idx].mUrbStatus &= (~BULK_URB_SUBMITTED);
            }
            else if (IS_URB_SUBMITTED(pDev->mBulkUrbList[idx].mUrbStatus))
            {
                usb_kill_urb(pDev->mBulkUrbList[idx].mBulk_in_urb);
                pDev->mBulkUrbList[idx].mUrbStatus &= (~BULK_URB_SUBMITTED);
            }
        }
        else
        {
            QC_LOG_WARN(pDev,"WARNING: NULL URB[%d]\n", idx);
        }

        if (IS_URB_INITIALIZED(pDev->mBulkUrbList[idx].mUrbStatus)) {
            /* Release buffers */
            if (pDev->mBulkUrbList[idx].mBulk_in_buffer != NULL)
            {
               qti_kfree(pDev->mBulkUrbList[idx].mBulk_in_buffer);
               pDev->mBulkUrbList[idx].mBulk_in_buffer = NULL;
            }
            else
            {
                QC_LOG_WARN(pDev,"WARNING: NULL buffer[%d]\n", idx);
            }

            /* Release URB's */
            if (pDev->mBulkUrbList[idx].mBulk_in_urb != NULL)
            {
               usb_free_urb(pDev->mBulkUrbList[idx].mBulk_in_urb);
               pDev->mBulkUrbList[idx].mBulk_in_urb = NULL;
            }
            else
            {
                QC_LOG_WARN(pDev,"WARNING2: NULL URB[%d]\n", idx);
            }
        }
        pDev->mBulkUrbList[idx].mUrbStatus = false;
        idx++;
    }

    QC_LOG_DBG(pDev,"Deleting mBulkUrbBuffList of %lu active requests and %d allocated nodes\n", pDev->mBulkUrbBuffListSize, BULK_URB_LIST);
    list_for_each_entry_safe(pBulkUrbListTraverse, pBulkUrbListSafe, &pDev->mBulkUrbBuffList, node) {
	list_del(&pBulkUrbListTraverse->node);
    qti_kfree(pBulkUrbListTraverse);
    }

    spin_lock_irqsave(&pDev->mSpinReadBuffLock, flags);
    QC_LOG_DBG(pDev,"Deleting mIoReadBuffList of %lu active requests and %d allocated nodes\n", pDev->mIoReadBuffListActiveSize, BULK_URB_LIST);
    list_for_each_entry_safe(pIoReadTraverse, pIoReadsafe, &pDev->mIoReadBuffListActive, node) {
	list_del(&pIoReadTraverse->node);
	pIoReadTraverse->kiocb = NULL;
	pDev->mIoReadBuffListActiveSize--;
    qti_kfree(pIoReadTraverse);
    }
    pIoReadTraverse = NULL;
    list_for_each_entry_safe(pIoReadTraverse, pIoReadsafe, &pDev->mIoReadBuffListIdle, node) {
	list_del(&pIoReadTraverse->node);
	pIoReadTraverse->kiocb = NULL;
	pDev->mIoReadBuffListIdleSize--;
    //qti_kfree(pIoReadTraverse); // Not sure at this moment.(double-free or invalid-free issue is coming) DeInitializeURB() called by QtiFreeDevices
    } 
    spin_unlock_irqrestore(&pDev->mSpinReadBuffLock, flags);

    mutex_lock(&pDev->mIoMutex);
    /* To clear URB and stop if any ongoing urb exists */
    for (i = 0; i < QTIDEV_TX_BUF_POOL_SZ; i++)
    {
        if (pDev->mTxBufferPool[i].mpTxBuf != NULL)
        {
           qti_kfree(pDev->mTxBufferPool[i].mpTxBuf);
           pDev->mTxBufferPool[i].mpTxBuf = NULL;
        }
        if (pDev->mTxBufferPool[i].mpTxUrb != NULL)
        {
           usb_free_urb(pDev->mTxBufferPool[i].mpTxUrb);
           pDev->mTxBufferPool[i].mpTxUrb = NULL;
        }
    }
    mutex_unlock(&pDev->mIoMutex);

    if (pDev->mBulkMemList.mReadBuffCircular.mBulk_in_buffer) {
        QC_LOG_INFO(pDev,"freeing up vmalloc memory\n");
        vfree(pDev->mBulkMemList.mReadBuffCircular.mBulk_in_buffer);
    }

    return;
}

static void StopRead(sQTIDevUSB *pDev)
{
    int idx = 0;

    QC_LOG_DBG(pDev,"");
    if (!pDev)
    {
        QC_LOG_INFO(pDev,"Invalid data\n");
        return;
    }
    
    if (pDev->mpIntURB != NULL)
    {
       usb_kill_urb(pDev->mpIntURB);
    }
    else
    {
       QC_LOG_WARN(pDev,"WARNING: NULL INT URB\n");
    }

    while (idx < BULK_URB_LIST)
    {
        if (!IS_URB_INITIALIZED(pDev->mBulkUrbList[idx].mUrbStatus) ||
                !IS_URB_SUBMITTED(pDev->mBulkUrbList[idx].mUrbStatus ||
                    pDev->mBulkUrbList[idx].mBulk_in_urb->status))
        {
            /* Either URB is not initialized or not submitted */
            pDev->mBulkUrbList[idx].mUrbStatus &= (~BULK_URB_SUBMITTED);
            idx++;
            continue;
        }
        /* Stop reading */
        if (pDev->mBulkUrbList[idx].mBulk_in_urb != NULL)
        {
            /* Killing URB */
            usb_kill_urb(pDev->mBulkUrbList[idx].mBulk_in_urb);
            pDev->mBulkUrbList[idx].mUrbStatus &= (~BULK_URB_SUBMITTED);
            idx++;
        }
    }

    return;
}

static int UpdateDeviceInfo(fileInfo_t **pFileInfo, char *pFilePath)
{
    int ret;

    QC_LOG_GLOBAL("");
    if (!pFilePath || !pFileInfo)
    {
        QC_LOG_GLOBAL("Invalid data\n");
        return -EINVAL;
    }

    ret = QTIDevInfEntrySize(pFilePath);
    if (ret < 0)
    {
        QC_LOG_GLOBAL("Failed to get the devices info\n");
        return ret;
    }

    *pFileInfo = qti_kmalloc(sizeof(fileInfo_t) + ret * sizeof(devInfo_t), GFP_KERNEL);
    if (!pFileInfo)
    {
        QC_LOG_GLOBAL("error in allocating memory\n");
        return -ENOMEM;
    }

    (*pFileInfo)->mLength = ret;

    if (QTIDevInfParse(pFilePath, *pFileInfo) < 0)
    {
        QC_LOG_GLOBAL("Error in parsing INF file\n");
        qti_kfree(*pFileInfo);
        *pFileInfo = NULL;
        return -ENXIO;
    }
    QC_LOG_GLOBAL("Number of devices %d ; %d\n", (*pFileInfo)->mNumResp, ret);
    return 0;
}

/*<===============sysfs starts============>*/

static ssize_t qdss_show(struct kobject *kobj, 
                struct kobj_attribute *attr, char *buf)
{
        int qdss_sys = 0;
        sQTIDevUSB *pDevOnRecord = NULL;
        printk(KERN_INFO "Sysfs - Read!!!\n");
        printk(KERN_INFO "Inside qdss_show function");
        list_for_each_entry(pDevOnRecord, &DeviceListActive, node)
        {
            printk(KERN_INFO "Write gobi value %px and %px and %s\n",pDevOnRecord->kobj_qdss,kobj,pDevOnRecord->mDevInfo.mDevInfInfo.mpKey);
            if (pDevOnRecord->kobj_qdss==kobj)
            {
                qdss_sys=pDevOnRecord->debug;
                break;
            }
        }
        return sprintf(buf, "%d\n", qdss_sys);
}

static ssize_t qdss_store(struct kobject *kobj, 
                struct kobj_attribute *attr,const char *buf, size_t count)
{
        int qdss_sys = 0;
        sQTIDevUSB *pDevOnRecord = NULL;
        printk(KERN_INFO "Sysfs - Write!!!\n");
        sscanf(buf,"%d",&qdss_sys);
        list_for_each_entry(pDevOnRecord, &DeviceListActive, node)
        {
            printk(KERN_INFO "Write gobi value %px and %px and %s\n",pDevOnRecord->kobj_qdss,kobj,pDevOnRecord->mDevInfo.mDevInfInfo.mpKey);
            if (pDevOnRecord->kobj_qdss==kobj)
            {
                pDevOnRecord->debug=qdss_sys;
                break;
            }
        }
        return count;
}
struct kobj_attribute dev_attr = __ATTR(Debug, S_IRUGO | S_IWUSR, qdss_show, qdss_store);
/*<===============sysfs ends============>*/

static int QTIDevUSBProbe(struct usb_interface *interface,
        const struct usb_device_id *id)
{  
    
    sQTIDevUSB *dev = NULL;
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    struct usb_device   *udev_tmp = NULL;
    int i;
    int retval = -ENOMEM;
    devInfo_t *devInfo;
    struct usb_device   *device;
    struct usb_host_interface *alt;
    char file_name[50];
#ifdef QCUSB_TEST_ONLY
    devInfo_t lpcDevInfo;
#endif
    deviceClass devClass = QTIDEV_INF_CLASS_UNKNOWN;
    char *path;

    path = (gQdssInfFilePath != NULL) ? gQdssInfFilePath : QDSS_INF_PATH;

    QC_LOG_GLOBAL("nEndpoints: %d\n", interface->cur_altsetting->desc.bNumEndpoints);

    retval = QTIDevInfCheckFileStatus(gQdssFileInfo, path);
    if (retval == false)
    {
        QC_LOG_GLOBAL("No update required\n");
    } else if (retval == true)
    {
        QC_LOG_GLOBAL("Update required\n");
        if (gQdssFileInfo)
        {
            qti_kfree(gQdssFileInfo);
            gQdssFileInfo = NULL;
        }
        if (UpdateDeviceInfo(&gQdssFileInfo, path) < 0)
        {
            QC_LOG_ERR(dev,"Error in parsing INF file\n");
            return -ENXIO;
        }
    } else
    {
        QC_LOG_ERR(dev,"Unable to get the status of VID/PID Info\n");
        return -EIO;
    }

#ifdef QDSS_DIAG_MERGE
    path = (gDiagInfFilePath != NULL) ? gDiagInfFilePath : DIAG_INF_PATH;

    retval = QTIDevInfCheckFileStatus(gDiagFileInfo, path);
    if (retval == false)
    {
        QC_LOG_GLOBAL("No update required\n");
    } else if (retval == true)
    {
        QC_LOG_GLOBAL("Update required\n");
        if (gDiagFileInfo)
        {
            qti_kfree(gDiagFileInfo);
            gDiagFileInfo = NULL;
        }
        if (UpdateDeviceInfo(&gDiagFileInfo, path) < 0)
        {
            QC_LOG_ERR(dev,"Error in parsing INF file\n");
            return -ENXIO;
        }
    } else
    {
        QC_LOG_ERR(dev,"Unable to get the status of VID/PID Info\n");
        return -EIO;
    }
#endif

    path = (gModemInfFilePath != NULL) ? gModemInfFilePath : MODEM_INF_PATH;
    retval = QTIDevInfCheckFileStatus(gModemFileInfo, path);

    if (retval == false)
    {
        QC_LOG_GLOBAL("No update required\n");
    } else if (retval == true)
    {
        QC_LOG_GLOBAL("Update required\n");
        if (gModemFileInfo)
        {
            kfree(gModemFileInfo);
            gModemFileInfo = NULL;
        }
        if (UpdateDeviceInfo(&gModemFileInfo, path) < 0)
        {
            QC_LOG_ERR(dev,"Error in parsing INF file\n");
            return -ENXIO;
        }
    } else
    {
        QC_LOG_ERR(dev,"Unable to get the status of VID/PID Info\n");
        return -EIO;
    }


#ifdef QDSS_DIAG_MERGE

    device = interface_to_usbdev(interface);
    alt = interface->cur_altsetting;
#ifdef QCUSB_TEST_ONLY
    if (device->descriptor.idVendor == QTIDEV_QTIOM_VID &&
            device->descriptor.idProduct == QTIDEV_LPC_PID &&
            alt->desc.bInterfaceNumber == QTIDEV_LPC_IFACE_NUM &&
            alt->desc.bAlternateSetting == 0)
    {
        lpcDevInfo.mDevType = QTIDEV_INF_TYPE_LPC;
        //lpcDevInfo.mDevType = QTIDEV_INF_TYPE_BULK;
        strncpy(lpcDevInfo.mpKey, QTIDEV_LPC_DEV_NAME, QTIDEV_INF_MAX_KEY_SIZE);
        lpcDevInfo.mVid_pid_iface[0] = device->descriptor.idVendor;
        lpcDevInfo.mVid_pid_iface[1] = device->descriptor.idProduct;
        lpcDevInfo.mVid_pid_iface[2] = alt->desc.bInterfaceNumber;
        devInfo = &lpcDevInfo;
        devClass = QTIDEV_INF_CLASS_PORTS;
    }
    else
#endif //QCUSB_TEST_ONLY
    if ((devInfo = QTIDevInfGetDevInfo(interface, gQdssFileInfo)))
    {
        devClass = QTIDEV_INF_CLASS_USB;
        QC_LOG_INFO(dev,"USB %s Successfully inserted\n", devInfo->mpKey);
    } else if((devInfo = QTIDevInfGetDevInfo(interface, gDiagFileInfo)))
    {
        devClass = QTIDEV_INF_CLASS_PORTS;
        QC_LOG_INFO(dev,"USB %s Successfully inserted\n", devInfo->mpKey);
    } else if((devInfo = QTIDevInfGetDevInfo(interface, gModemFileInfo)))
    {
        devClass = QTIDEV_INF_CLASS_USB;
        devInfo->mDevType = QTIDEV_INF_TYPE_INT_IN;
        QC_LOG_INFO(dev,"USB %s Successfully inserted\n", devInfo->mpKey);
    } else
    {
        QC_LOG_ERR(dev,"USB corresponds to other Iface, (Supports only: %s)\n", path);
        return -EIO;
    }
#endif //QDSS_DIAG_MERGE

   interface->needs_remote_wakeup = 1;

    udev_tmp = usb_get_dev(interface_to_usbdev(interface));
    /* allocate memory for our device state and initialize it */
    dev = QtiAcquireDevice(udev_tmp, interface, devInfo->mpKey); 
    if (!dev) {
        QC_LOG_ERR(dev,"Out of memory\n");
        retval = -ENOMEM;
        goto error;
    }

    dev->udev = udev_tmp;
    dev->interface = usb_get_intf(interface);
    dev->disconnected = 0;

    /* set up the endpoint information */
    /* use only the first bulk-in and no bulk-out endpoints */
    iface_desc = interface->cur_altsetting;
    dev->mIntfNum = interface->cur_altsetting->desc.bInterfaceNumber;
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        endpoint = &iface_desc->endpoint[i].desc;

        if (!dev->mBulkInEndAddr &&
                usb_endpoint_is_bulk_in(endpoint)) {
            /* we found a bulk in endpoint */
            dev->mBulkInSize = UrbRxSize; // 131072;
            QC_LOG_INFO(dev,"URB Rx Size %zu\n", dev->mBulkInSize);
            dev->mBulkInEndAddr = endpoint->bEndpointAddress;

            QC_LOG_DBG(dev,"In bulk IN endpoint\n");
        }

        if (!dev->mBulkOutEndAddr &&
                usb_endpoint_is_bulk_out(endpoint)) {
            /* we found a bulk out endpoint */
#ifdef QCUSB_TEST_ONLY
            if (devInfo->mDevType == QTIDEV_INF_TYPE_LPC)
            {
                dev->mBulkOutSize = usb_endpoint_maxp(endpoint);
            }
#endif
            dev->mBulkOutEndAddr = endpoint->bEndpointAddress;
        }

        if (usb_endpoint_dir_in( endpoint) == true
      &&  usb_endpoint_xfer_int( endpoint ) == true)
      {
         //dev->mIntInEndp = endpoint.bEndpointAddress;
         dev->mIntInEndp = usb_rcvintpipe( dev->udev, endpoint->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
         retval = usb_clear_halt( dev->udev, dev->mIntInEndp );
         QC_LOG_INFO(dev,"usb_clear_halt INT returned %d\n", retval);
         dev->mIntInEndpMaxPacketSize = usb_endpoint_maxp(endpoint);
      }
    }

    memcpy(&(dev->mDevInfo.mDevInfInfo), devInfo, sizeof(devInfo_t));
    dev->mDevInfo.mClassType = devClass;
    if (devClass == QTIDEV_INF_CLASS_PORTS)
    {
        dev->mDevInfo.mpDevClass = gpDiagClass;
    } else if (devClass == QTIDEV_INF_CLASS_USB)
    {
        dev->mDevInfo.mpDevClass = gpQdssClass;
    }

#ifdef QCUSB_TEST_ONLY
    if ( dev->mBulkInEndAddr
            && dev->mBulkOutEndAddr
            && (dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC))
    {
        QC_LOG_INFO(dev,"found both bulk-in and bulk-out endpoints\n");
    }
    else
#endif
    if ( dev->mBulkInEndAddr
            && dev->mBulkOutEndAddr
            && (dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_BULK))
    {
        QC_LOG_INFO(dev,"found both bulk-in and bulk-out endpoints\n");
        dev->mDevInfo.mDevInfInfo.mDevType = QTIDEV_INF_TYPE_BULK_IN_OUT;
    }
    else if ( dev->mBulkInEndAddr
            && (dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_BULK))
    {
        QC_LOG_INFO(dev,"found bulk-in Trace endpoint\n");
        dev->mDevInfo.mDevInfInfo.mDevType = QTIDEV_INF_TYPE_TRACE_IN;
    }
    else if (dev->mBulkInEndAddr
            && (dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_DPL))
    {
        QC_LOG_INFO(dev,"found bulk-in DPL endpoint\n");
        dev->mDevInfo.mDevInfInfo.mDevType = QTIDEV_INF_TYPE_DPL;
    }
    else if (dev->mIntInEndp
            && (dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_INT_IN))
    {
        QC_LOG_INFO(dev,"found interrupt-in endpoint\n");
        dev->mDevInfo.mDevInfInfo.mDevType = QTIDEV_INF_TYPE_INT_IN;
    }
    else
    {
        QC_LOG_ERR(dev,"unable to get the bulk endpoint info\n");
        retval = -ENXIO;
        QtiReleaseDevice(dev); // qti_kfree(dev);
        dev = NULL;
        goto error;
    }

   /* save our data pointer in this interface device */
    usb_set_intfdata(interface, dev);
    FinalizeURB(dev);
#ifdef QCUSB_TEST_ONLY
    if (dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_LPC)
    {
        QtiEnterSVEFlow(dev);
    }
#endif

    /* we can register the device now, as it is ready */
    retval = RegisterQDSSDevice(dev);
    if (retval) {
        /* something prevented us from registering this driver */
        QC_LOG_ERR(dev,"Not able to get a minor for this device.\n");
        goto error;
    }

    if (dev->mDevInfo.mDevInfInfo.mDevType == QTIDEV_INF_TYPE_DPL)
    {
        /* Start async reading */
        retval = SubmitAllReadUrb(dev);
        if (retval != 0)
        {
            QC_LOG_ERR(dev,"Error in reading\n");
            goto error;
        }
    }
    /*<===============sysfs starts============>*/

    if (dev){
        sprintf(file_name,"%s_%d-%s:%d.%d",dev->mDevInfo.mDevInfInfo.mpKey,
        dev->udev->bus->busnum,
        dev->udev->devpath,
        dev->udev->actconfig->desc.bConfigurationValue,
        dev->interface->cur_altsetting->desc.bInterfaceNumber);
    }
    else {
        sprintf(file_name,"qdss_%d",gQdssCounter);
        gQdssCounter++;
    }
    dev->kobj_qdss = kobject_create_and_add(file_name, NULL);
    if (!dev->kobj_qdss)
      return -ENOMEM;
    retval = sysfs_create_file(dev->kobj_qdss, &dev_attr.attr);
    if (retval && dev->kobj_qdss)
    {
        kobject_put(dev->kobj_qdss);
        dev->kobj_qdss = NULL;
        sysfs_remove_file(dev->kobj_qdss,&dev_attr.attr);
    }
    /*<===============sysfs ends============>*/
    
   /* To enable auto suspend */
   //usb_enable_autosuspend(interface_to_usbdev(interface));

    /* Initialize workqueue for poll() */
    init_waitqueue_head( &dev->mBulkMemList.mWaitQueue );
    InitializeRxNotifyPool(dev);
    QC_LOG_DBG(dev,"size sQTIDevUSB: %zu\n", sizeof(*dev));
    return 0;
    
error:
    if (dev)
    {
        if (dev->mDevNum)
        {
            DeregisterQDSSDevice(dev);
        }
        QtiReleaseDevice(dev); // qti_kfree(dev);
        dev = NULL;
        /* this frees allocated memory */
    }
    usb_set_intfdata(interface, NULL);
    return retval;
}

static void QTIDevUSBDisconnect(struct usb_interface *interface)
{
    sQTIDevUSB *pDev;
    struct inode * pOpenInode;
    struct list_head * pInodeList;
    struct task_struct * pEachTask = NULL;
    struct fdtable * pFDT;
    struct file * pFilp;
    unsigned long flags;
    int count = 0;

    pDev = usb_get_intfdata(interface);

    QC_LOG_INFO(pDev,"device '%s' now disconnected\n", pDev->fqDevName);

	/* prevent more I/O from starting */
	mutex_lock(&pDev->mIoMutex);
	pDev->disconnected = 1;
	mutex_unlock(&pDev->mIoMutex);

    list_for_each( pInodeList, &pDev->mCdev.list ) {
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
                    if (pFilp != NULL &&  (pFilp->f_path).dentry != NULL)
                    {
                        if (file_inode(pFilp) == pOpenInode)
                        {
                            // Close this file handle
                            rcu_assign_pointer( pFDT->fd[count], NULL );
                            QC_LOG_INFO(pDev, "forcing close of open file handle\n" );
                            /* decrement the count on our device */
                            filp_close( pFilp, pEachTask->files );
                        }
                    }
                }
            }
            rcu_read_unlock();
        }
    }

    /* delete the (dev->udev) device and other cleanup */
    DeregisterQDSSDevice(pDev);
    /*<===============sysfs starts============>*/
    if (pDev) {
        if(pDev->kobj_qdss)
        {
            sysfs_remove_file(pDev->kobj_qdss,&dev_attr.attr);
            kobject_put(pDev->kobj_qdss);
            pDev->kobj_qdss = NULL;
        }
    }
    /*<===============sysfs ends============>*/

    spin_lock_irqsave(&pDev->mBulkMemList.mReadMemLock, flags);
    ClearNotifyList(pDev);
    QC_LOG_INFO(pDev, "ClearNotifyList done\n" );
    ClearReadMemList(pDev);
    QC_LOG_INFO(pDev,"ClearReadMemList done\n" );
    spin_unlock_irqrestore(&pDev->mBulkMemList.mReadMemLock, flags);

    QC_LOG_INFO(pDev,"cleaning URBs\n" );
    DeinitializeTxBuffers(pDev);
    usb_kill_anchored_urbs(&pDev->submitted);

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,17))
    if (pDev->mRefCount.refcount.counter > 1)
#else
    if (pDev->mRefCount.refcount.refs.counter > 1)
#endif
        kref_put(&pDev->mRefCount, QTIDevUSBDelete);
    QtiReleaseDevice(pDev);

    QC_LOG_INFO(pDev,"Exit\n" );

    return;
}

static int QTIDevUSBSuspend(struct usb_interface *intf, pm_message_t powerEvent)
{
    sQTIDevUSB *pDev = usb_get_intfdata(intf);

    QC_LOG_DBG(pDev,"");
    if (!pDev)
    {
        QC_LOG_ERR(pDev,"Invalid data\n");
        return 0;
    }

    QTIDevUSBDrawDown(pDev);
    /* Stop async reading  */
    QC_LOG_DBG(pDev,"%s : %d, about to stop urb\n", __func__, __LINE__);
    StopRead(pDev);
    return 0;
}

static int QTIDevUSBResume(struct usb_interface *intf)
{
    sQTIDevUSB *dev = usb_get_intfdata(intf);

    QC_LOG_DBG(dev,"");

    if (!dev)
    {
        QC_LOG_ERR(dev,"Invalid data\n");
        return 0;
    }

    spin_lock_irq(&dev->mSpinErrLock);

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,17))
    if ( !(dev->mDevInfo.mDevInfInfo.mDevType != QTIDEV_INF_TYPE_DPL) || (dev->mRefCount.refcount.counter > 1))
#else
    if ( !(dev->mDevInfo.mDevInfInfo.mDevType != QTIDEV_INF_TYPE_DPL) || (dev->mRefCount.refcount.refs.counter > 1))
#endif
    {
        QC_LOG_DBG(dev,"%s : %d, about to submit urb\n", __func__, __LINE__);
        /* Start async reading */
        if (0 != SubmitAllReadUrb(dev))
        {
            QC_LOG_ERR(dev,"Error in reading\n");
        }
    }
    spin_unlock_irq(&dev->mSpinErrLock);
    return 0;
}

/* table of devices that work with this driver */
static const struct usb_device_id QdssVIDPIDTable[] = {
    /* To check for all devices */
    { .driver_info = 0xffff },
    { }                 /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, QdssVIDPIDTable);

static struct usb_driver qtiDevDriver = {
    .name       = QTIDEV_DRIVER_NAME,
    .probe      = QTIDevUSBProbe,
    .disconnect = QTIDevUSBDisconnect,
    .suspend    = QTIDevUSBSuspend,
    .resume     = QTIDevUSBResume,
    .id_table   = QdssVIDPIDTable,
    .supports_autosuspend = true,
};

/*===========================================================================
METHOD:
QTIDevUSBModInit (Public Method)

DESCRIPTION:
Initialize module
Create device class
Register out usb_driver struct

RETURN VALUE:
int - 0 for success
Negative errno for error
===========================================================================*/
static int QTIDevUSBModInit(void)
{
    int retVal;

    gQdssInfFilePath =
        (gQdssInfFilePath != NULL) ? gQdssInfFilePath : QDSS_INF_PATH;
    gDiagInfFilePath =
        (gDiagInfFilePath != NULL) ? gDiagInfFilePath : DIAG_INF_PATH;
    gModemInfFilePath =
        (gModemInfFilePath != NULL) ? gModemInfFilePath : MODEM_INF_PATH;

    if (UpdateDeviceInfo(&gQdssFileInfo, gQdssInfFilePath) < 0)
    {
        QC_LOG_GLOBAL("Error in parsing INF file\n");
        return -ENXIO;
    }

    if (UpdateDeviceInfo(&gModemFileInfo, gModemInfFilePath) < 0)
    {
        QC_LOG_GLOBAL("Error in parsing INF file\n");
        return -ENXIO;
    }

#ifdef QDSS_DIAG_MERGE
    if (UpdateDeviceInfo(&gDiagFileInfo, gDiagInfFilePath) < 0)
    {
        QC_LOG_GLOBAL("Error in parsing INF file\n");
        qti_kfree(gQdssFileInfo);
        return -ENXIO;
    }
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(6,3,13))
    gpDiagClass = class_create(THIS_MODULE, QTIDEV_PORT_CLASS_NAME);
#else
    gpDiagClass = class_create(QTIDEV_PORT_CLASS_NAME);
#endif
    if (IS_ERR(gpDiagClass) == true)
    {
        QC_LOG_GLOBAL( "error at class_create %ld\n", PTR_ERR(gpDiagClass));
        qti_kfree(gQdssFileInfo);
        gQdssFileInfo = NULL;
        qti_kfree(gDiagFileInfo);
        gDiagFileInfo = NULL;
        return -ENOMEM;
    }

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(6,3,13))
    gpQdssClass = class_create(THIS_MODULE, QTIDEV_USB_CLASS_NAME);
#else
    gpQdssClass = class_create(QTIDEV_USB_CLASS_NAME);
#endif
    if (IS_ERR(gpQdssClass) == true)
    {
        QC_LOG_GLOBAL( "error at class_create %ld\n", PTR_ERR(gpQdssClass));
        qti_kfree(gQdssFileInfo);
        gQdssFileInfo = NULL;
        qti_kfree(gDiagFileInfo);
        gDiagFileInfo = NULL;
        class_destroy(gpDiagClass);
        return -ENOMEM;
    }

    retVal = QtiInitializeDeviceList();
    if (retVal)
    {
        QC_LOG_GLOBAL("QtiInitializeDeviceList failure\n");
    }
    else
    {
        retVal = usb_register( &qtiDevDriver );
        if (retVal)
        {
           QC_LOG_GLOBAL("usb_register failure\n");
           QtiFreeDevices();
        }
    }
    // This will be shown whenever driver is loaded
    QC_LOG_GLOBAL( "Driver Description: %s - Driver Version %s\n", DRIVER_DESC, DRIVER_VERSION );

    return retVal;
}

/*===========================================================================
METHOD:
QTIDevUSBModExit (Public Method)

DESCRIPTION:
Deregister module
Destroy device class

RETURN VALUE:
void
===========================================================================*/
static void __exit QTIDevUSBModExit(void)
{
    QC_LOG_GLOBAL("-->\n");
    usb_deregister( &qtiDevDriver );
    QtiFreeDevices();

    class_destroy(gpQdssClass);
    class_destroy(gpDiagClass);

    if (gQdssFileInfo)
    {
        qti_kfree(gQdssFileInfo);
        gQdssFileInfo = NULL;
    }

#ifdef QDSS_DIAG_MERGE
    if (gDiagFileInfo)
    {
        qti_kfree(gDiagFileInfo);
        gDiagFileInfo = NULL;
    }
#endif

    QC_LOG_GLOBAL("<--\n");
    return;
}

module_init(QTIDevUSBModInit);
module_exit(QTIDevUSBModExit);

MODULE_VERSION( DRIVER_VERSION );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("Dual BSD/GPL");



module_param( debug_g, int, S_IRUGO | S_IWUSR );

module_param( UrbRxSize, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC(UrbRxSize, "URB Rx Size)");

module_param( UrbTxSize, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC(UrbTxSize, "URB Tx Size)");

module_param(gQdssInfFilePath, charp, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC(gQdssInfFilePath, "Inf File location (Need complete path)");

module_param(gDiagInfFilePath, charp, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC(gDiagInfFilePath, "Inf File location (Need complete path)");
