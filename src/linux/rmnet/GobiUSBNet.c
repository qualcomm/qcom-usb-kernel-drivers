/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#include "QMIDevice.h"
#include "QMI.h"
#include "qmap.h"
#include <linux/device.h>
#include <linux/if_arp.h>
#include <linux/platform_device.h>

#define DRIVER_DESC "GobiNet"

// Timer value
unsigned long gtimer = 2 * NSEC_PER_MSEC;

// Debug flag
int debug_g = 1; //For global logging
int debug_aggr = 0; //For aggregation logging
// Allow user interrupts
int interruptible = 1;

// Allow down timeouts
int use_down_timeout = 1;

// Number of IP packets which may be queued up for transmit
int txQueueLength = 100;

//mux support
int mux = 0;

//Enable DHCP
int enableDhcp = 0;

#define CONFIG_USB_CODE
#ifdef CONFIG_USB_CODE
#define QTI_RMNET_INF_PATH "/opt/QTI/QUD/rmnet/qtiwwan.inf"
#define GOBI_NUM_DEVICES_DEFAULT 8

fileInfo_t  *gQTIRmnetFileInfo = NULL;
static char *gQTIRmnetInfFilePath = NULL;
#endif

static struct list_head DeviceListIdle;
static struct list_head DeviceListActive;
static spinlock_t DevListLock;
static int DevicesIdle, DevicesActive;

// Class should be created during module init, so needs to be global
static struct class * gpClass;

// Proc enrty should be created during module init, so needs to be global
static struct proc_dir_entry *gpProcEnt;

#ifndef VIRTUAL_USB_CODE
static int gobi_sys;
static int check_perf = 0;
#endif
sGobiUSBNet * gpGobiDev;
#ifdef TX_AGGR
static enum hrtimer_restart cdc_ncm_tx_timer_cb(struct hrtimer *);
static void cdc_ncm_tx_timeout_cancel(struct rm_cdc_ncm_ctx *);
static void cdc_ncm_tx_timeout_start(struct rm_cdc_ncm_ctx *);
#endif

int GobiUSBNetProbe(struct usb_interface *, const struct usb_device_id *);
struct sk_buff *aggr_tx_frame(struct usbnet *, struct sk_buff *);
int GobiUSBNetStartXmit(struct sk_buff *, struct net_device *);
sGobiUSBNet *GobiAcquireDevice(char *, struct usbnet *);
int GobiSuspend(struct usb_interface *, pm_message_t);
sEndpoints *GatherEndpoints(struct usb_interface *);
int GobiInitializeDeviceContext(sGobiUSBNet *);
int findDebugAndLogLevel(unsigned short, int);
void GobiUSBNetTXTimeout(struct net_device *);
int GobiResume(struct usb_interface * pIntf);
int GobiMUXUSBNetOpen(struct net_device *);
int GobiMUXUSBNetStop(struct net_device *);
void GobiResetDeviceContext(sGobiUSBNet *);
void GobiUSBNetURBCallback(struct urb *);
int GobiUSBNetOpen(struct net_device *);
int GobiUSBNetStop(struct net_device *);
void GobiReleaseDevice(sGobiUSBNet *);
int GobiInitializeDeviceList(void);
void GobiFreeDevices(void);

int GobiInitializeDeviceContext(sGobiUSBNet *pGobiDev)
{
    spin_lock_init(&pGobiDev->mAutoPM.mURBListLock);
    spin_lock_init(&pGobiDev->mAutoPM.mActiveURBLock);
    pGobiDev->mpWorkQ = alloc_workqueue("qtiWQ", 0, 4); 
    pGobiDev->mAutoPM.mpURBList = NULL;
    pGobiDev->mQMIDev.mpClientMemList = NULL;
    pGobiDev->mbQMIValid = false;
    pGobiDev->mQMIDev.debug = 1;
    pGobiDev->mQMIDev.logLevel = QC_LOG_LVL_INFO;/*Initializing logLevel*/
    return 0;
}

int GobiInitializeDeviceList(void)
{
    sGobiUSBNet * pGobiDev = NULL;
    int i;
    spin_lock_init(&DevListLock);
    INIT_LIST_HEAD(&DeviceListIdle);
    INIT_LIST_HEAD(&DeviceListActive);
    DevicesIdle = DevicesActive = 0;
    for (i = 0; i < GOBI_NUM_DEVICES_DEFAULT; i++)
    {
        pGobiDev = kzalloc( sizeof( sGobiUSBNet ), GFP_KERNEL );
        if (pGobiDev == NULL)
        {
            break;
        }
        if (GobiInitializeDeviceContext(pGobiDev))
        {
            QC_LOG_ERR(GET_QMIDEV(pGobiDev),"Failed to initialize dev context\n");
            kfree(pGobiDev);
            break;
        }
        list_add_tail(&pGobiDev->node, &DeviceListIdle);
         ++DevicesIdle;
    }
    QC_LOG_GLOBAL("initial device pool size: %d\n", DevicesIdle);
    if (DevicesIdle != 0)
    {
        return 0;
    }
    return 1;  // failure
}

sGobiUSBNet *GobiAcquireDevice(char *mpKey, struct usbnet * pDev)
{
   sGobiUSBNet *pGobiDev = NULL;
   char commonDevName[255];
   int found_devName = 0; 
   unsigned long flags;
   int success = 0;

   spin_lock_irqsave(&DevListLock, flags);
   if (list_empty(&DeviceListIdle) == true)
   {
      spin_unlock_irqrestore(&DevListLock, flags);
      pGobiDev = kzalloc( sizeof( sGobiUSBNet ), GFP_KERNEL );
      if (pGobiDev != NULL)
        {
            if (GobiInitializeDeviceContext(pGobiDev))
            {
                QC_LOG_ERR( GET_QMIDEV(pGobiDev),"Failed to initialize dev context\n");
                kfree(pGobiDev);
                pGobiDev = NULL;
            }
            else
            {
                list_add_tail(&pGobiDev->node, &DeviceListActive);
                ++DevicesActive;
                success = 1;
            }
        }
   }
   else 
   {
      sprintf(commonDevName,"%s:%d-%s", mpKey, pDev->udev->bus->busnum, pDev->udev->devpath);
      commonDevName[254] = '\0';

      list_for_each_entry(pGobiDev, &DeviceListIdle, node)
      {
         char * res = strstr(pGobiDev->mQMIDev.mdeviceName, commonDevName); //pGobiDev->mQMIDev.mdeviceName should contain commonDevName as substring
         if(res != NULL)
         {
            QC_LOG_DBG(GET_QMIDEV(pGobiDev),"Found matching commonDevName\n");
            found_devName = 1;
            list_move_tail(&pGobiDev->node, &DeviceListActive);
            --DevicesIdle;
            ++DevicesActive;
            success = 1;
            break;
         }
      }
      if(found_devName == 0)
      {
         int findNULL = 0;
         list_for_each_entry(pGobiDev, &DeviceListIdle, node)
         {
            int res = strncmp(pGobiDev->mQMIDev.mdeviceName,"",255);
            if(res == 0)
            {
               QC_LOG_DBG(GET_QMIDEV(pGobiDev),"Found NULL entry\n");
               findNULL = 1;
               list_move_tail(&pGobiDev->node, &DeviceListActive);
               --DevicesIdle;
               ++DevicesActive;
               success = 1;
               break;
            }
         }
         if(findNULL == 0)
         {
            pGobiDev = kzalloc( sizeof( sGobiUSBNet ), GFP_KERNEL );
            if (pGobiDev != NULL)
            {
                  if (GobiInitializeDeviceContext(pGobiDev))
                  {
                     QC_LOG_EXCEPTION(GET_QMIDEV(pGobiDev),"Failed to initialize dev context\n");
                     kfree(pGobiDev);
                     pGobiDev = NULL;
                  }
                  else
                  {
                     QC_LOG_DBG(GET_QMIDEV(pGobiDev),"Created a new entry\n");
                     list_add_tail(&pGobiDev->node, &DeviceListActive);
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
        QC_LOG_EXCEPTION(GET_QMIDEV(pGobiDev), "QTI-ALERT: failure to qcquire Device (idle-active %d-%d)\n", DevicesIdle, DevicesActive);
    }
    return pGobiDev;
}

void GobiResetDeviceContext(sGobiUSBNet *pGobiDev)
{
   if (pGobiDev == NULL)
    {
        QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get QMIDevice\n" );
        return;
    }
    QC_LOG_INFO(GET_QMIDEV(pGobiDev), "In \n");
    memset(&(pGobiDev->mDevInfo.mDevInfInfo), 0, sizeof(devInfo_t));
    pGobiDev->mDevInfo.mClassType = QTIDEV_INF_CLASS_UNKNOWN;
    pGobiDev->mDevInfo.mpDevClass = NULL;
    pGobiDev->mDevInfo.mDevInfInfo.mDevType = QTIDEV_INF_TYPE_UNKNOWN;
}

void GobiReleaseDevice(sGobiUSBNet *pGobiDev)
{
   sGobiUSBNet *pDevOnRecord = NULL;
   unsigned long flags;
   int removed = 0;

   if (pGobiDev == NULL)
    {
        QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get QMIDevice\n" );
        return;
    }
    QC_LOG_INFO(GET_QMIDEV(pGobiDev), "In\n");
    spin_lock_irqsave(&DevListLock, flags);
    list_for_each_entry(pDevOnRecord, &DeviceListActive, node)
    {
        if (pDevOnRecord == pGobiDev)
         {
                  list_move_tail(&pDevOnRecord->node, &DeviceListIdle);
                  GobiResetDeviceContext(pGobiDev);
                  ++DevicesIdle;
                  --DevicesActive;
                  removed = 1;
                  break;
         }
    }
    QC_LOG_GLOBAL("Device = 0x%px (idle-active %d-%d)\n", pDevOnRecord, DevicesIdle, DevicesActive);
    if (removed == 0)
    {
        QC_LOG_EXCEPTION(GET_QMIDEV(pGobiDev),"GOBI-ALERT: failure to remove Device = (idle-active %d-%d)\n",DevicesIdle, DevicesActive);
    }
    spin_unlock_irqrestore(&DevListLock, flags);
    return;
}

void GobiFreeDevices(void)
{
    sGobiUSBNet *pDevOnRecord = NULL;
    sGobiUSBNet *pDevSafe = NULL;
    unsigned long flags;
    int removed = 0;
    QC_LOG_GLOBAL("--> DevActive %d DevIdle %d\n", DevicesActive, DevicesIdle);
    spin_lock_irqsave(&DevListLock, flags);
    QC_LOG_GLOBAL("Now, Idle list freeing");
    list_for_each_entry_safe(pDevOnRecord, pDevSafe, &DeviceListIdle, node)
    {
      if (pDevOnRecord != NULL)
      {
         list_del(&pDevOnRecord->node);
         spin_unlock_irqrestore(&DevListLock, flags);
         destroy_workqueue(pDevOnRecord->mpWorkQ);
         kfree(pDevOnRecord);
         --DevicesIdle;
         ++removed;

         spin_lock_irqsave(&DevListLock, flags);
      }
    }
    QC_LOG_GLOBAL("Idle devices removed %d remaining %d (vs %d)\n", removed, DevicesActive, DevicesIdle);
    spin_unlock_irqrestore(&DevListLock, flags);
    spin_lock_irqsave(&DevListLock, flags);
    removed = 0;
    QC_LOG_GLOBAL("Now, Active list freeing");
    list_for_each_entry_safe(pDevOnRecord, pDevSafe, &DeviceListActive, node)
    {
       if (pDevOnRecord != NULL)
       {
          list_del(&pDevOnRecord->node);
          spin_unlock_irqrestore(&DevListLock, flags);
          destroy_workqueue(pDevOnRecord->mpWorkQ);
          kfree(pDevOnRecord);
          --DevicesActive;
          ++removed;
          spin_lock_irqsave(&DevListLock, flags);
       }
    }
    QC_LOG_GLOBAL("Active devices removed %d remaining %d (vs %d)\n", removed, DevicesActive, DevicesIdle);
    spin_unlock_irqrestore(&DevListLock, flags);
    QC_LOG_GLOBAL("<--\n");
    return;
}

/*===========================================================================
METHOD:
   GatherEndpoints (Public Method)

DESCRIPTION:
   Enumerate endpoints

PARAMETERS
   pIntf          [ I ] - Pointer to usb interface

RETURN VALUE:
   sEndpoints structure
              NULL for failure
===========================================================================*/
sEndpoints * GatherEndpoints( struct usb_interface * pIntf )
{
   int numEndpoints;
   int endpointIndex;
   sEndpoints * pOut;
   struct usb_host_endpoint * pEndpoint = NULL;

   pOut = kzalloc( sizeof( sEndpoints ), GFP_ATOMIC );
   if (pOut == NULL)
   {
     QC_LOG_GLOBAL( "unable to allocate memory\n" );
      return NULL;
   }

   pOut->mIntfNum = pIntf->cur_altsetting->desc.bInterfaceNumber;

   // Scan endpoints
   numEndpoints = pIntf->cur_altsetting->desc.bNumEndpoints;
   for (endpointIndex = 0; endpointIndex < numEndpoints; endpointIndex++)
   {
      pEndpoint = pIntf->cur_altsetting->endpoint + endpointIndex;
      if (pEndpoint == NULL)
      {
        QC_LOG_GLOBAL( "invalid endpoint %u\n", endpointIndex );
         kfree( pOut );
         return NULL;
      }

      if (usb_endpoint_dir_in( &pEndpoint->desc ) == true
      &&  usb_endpoint_xfer_int( &pEndpoint->desc ) == true)
      {
         pOut->mIntInEndp = pEndpoint->desc.bEndpointAddress;
         pOut->mIntInEndpMaxPacketSize = usb_endpoint_maxp(&pEndpoint->desc);
      }
      else if (usb_endpoint_dir_in( &pEndpoint->desc ) == true
      &&  usb_endpoint_xfer_int( &pEndpoint->desc ) == false)
      {
         pOut->mBlkInEndp = pEndpoint->desc.bEndpointAddress;
      }
      else if (usb_endpoint_dir_in( &pEndpoint->desc ) == false
      &&  usb_endpoint_xfer_int( &pEndpoint->desc ) == false)
      {
         pOut->mBlkOutEndp = pEndpoint->desc.bEndpointAddress;
      }
   }

   if (pOut->mIntInEndp == 0
   ||  pOut->mBlkInEndp == 0
   ||  pOut->mBlkOutEndp == 0)
   {
     QC_LOG_GLOBAL( "One or more endpoints missing\n" );
      kfree( pOut );
      return NULL;
   }

   QC_LOG_GLOBAL( "intf %u\n", pOut->mIntfNum );
   QC_LOG_GLOBAL( "   int in  0x%02x\n", pOut->mIntInEndp );
   QC_LOG_GLOBAL( "   blk in  0x%02x\n", pOut->mBlkInEndp );
   QC_LOG_GLOBAL( "   blk out 0x%02x\n", pOut->mBlkOutEndp );

   return pOut;
}

/*===========================================================================
METHOD:
   GobiSuspend (Public Method)

DESCRIPTION:
   Stops QMI traffic while device is suspended

PARAMETERS
   pIntf          [ I ] - Pointer to interface
   powerEvent     [ I ] - Power management event

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int GobiSuspend(
   struct usb_interface *     pIntf,
   pm_message_t               powerEvent )
{
   struct usbnet * pDev;
   sGobiUSBNet * pGobiDev = NULL;
   if (pIntf == 0)
   {
      return -ENOMEM;
   }

#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,23 ))
   pDev = usb_get_intfdata( pIntf );
#else
   pDev = (struct usbnet *)pIntf->dev.platform_data;
#endif

   if (pDev == NULL || pDev->net == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "failed to get netdevice\n" );
      return -ENXIO;
   }

   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get QMIDevice\n" );
      return -ENXIO;
   }

   QC_LOG_INFO(GET_QMIDEV(pGobiDev),"In\n");
   // Is this autosuspend or system suspend?
   //    do we allow remote wakeup?
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,33 ))
   if (pDev->udev->auto_pm == 0)
#else
   if ((powerEvent.event & PM_EVENT_AUTO) == 0)
#endif
   {
      QC_LOG_WARN(GET_QMIDEV(pGobiDev), "device suspended to power level %d\n",
           powerEvent.event );
      GobiSetDownReason(pGobiDev, DRIVER_SUSPENDED );
   }
   else
   {
      QC_LOG_WARN(GET_QMIDEV(pGobiDev), "device autosuspend\n" );
   }

   if (powerEvent.event & PM_EVENT_SUSPEND)
   {
      // Stop QMI read callbacks
      KillRead( pGobiDev );
      pDev->udev->reset_resume = 0;

      // Store power state to avoid duplicate resumes
      pIntf->dev.power.power_state.event = powerEvent.event;
   }
   else
   {
      // Other power modes cause QMI connection to be lost
      pDev->udev->reset_resume = 1;
   }

   // Run usbnet's suspend function
   return usbnet_suspend( pIntf, powerEvent );
}

/*===========================================================================
METHOD:
   GobiResume (Public Method)

DESCRIPTION:
   Resume QMI traffic or recreate QMI device

PARAMETERS
   pIntf          [ I ] - Pointer to interface

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int GobiResume( struct usb_interface * pIntf )
{
   struct usbnet * pDev;
   sGobiUSBNet * pGobiDev = NULL;
   int nRet;
   int oldPowerState;

   if (pIntf == 0)
   {
      return -ENOMEM;
   }

#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,23 ))
   pDev = usb_get_intfdata( pIntf );
#else
   pDev = (struct usbnet *)pIntf->dev.platform_data;
#endif

   if (pDev == NULL || pDev->net == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get netdevice\n" );
      return -ENXIO;
   }

   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get QMIDevice\n" );
      return -ENXIO;
   }

   oldPowerState = pIntf->dev.power.power_state.event;
   pIntf->dev.power.power_state.event = PM_EVENT_ON;
   QC_LOG_INFO(GET_QMIDEV(pGobiDev), "resuming from power mode %d\n", oldPowerState );

   if (oldPowerState & PM_EVENT_SUSPEND)
   {
      // It doesn't matter if this is autoresume or system resume
      GobiClearDownReason( pGobiDev, DRIVER_SUSPENDED );

      nRet = usbnet_resume( pIntf );
      if (nRet != 0)
      {
        QC_LOG_ERR(GET_QMIDEV(pGobiDev), "usbnet_resume error %d\n", nRet );
         return nRet;
      }

      // Restart QMI read callbacks
      nRet = StartRead( pGobiDev );
      if (nRet != 0)
      {
        QC_LOG_ERR(GET_QMIDEV(pGobiDev), "StartRead error %d\n", nRet );
         return nRet;
      }

      // Kick Auto PM thread to process any queued URBs
      complete( &pGobiDev->mAutoPM.mThreadDoWork );
   }
   else
   {
      QC_LOG_INFO(GET_QMIDEV(pGobiDev), "nothing to resume\n" );
      return 0;
   }

   return nRet;
}

/*===========================================================================
METHOD:
   GobiNetDriverBind (Public Method)

DESCRIPTION:
   Setup in and out pipes

PARAMETERS
   pDev           [ I ] - Pointer to usbnet device
   pIntf          [ I ] - Pointer to interface

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
static int GobiNetDriverBind(
   struct usbnet *         pDev,
   struct usb_interface *  pIntf )
{
   int numEndpoints;
   int endpointIndex;
   struct usb_host_endpoint * pEndpoint = NULL;
   struct usb_host_endpoint * pIn = NULL;
   struct usb_host_endpoint * pOut = NULL;

   // Verify one altsetting
   if (pIntf->num_altsetting != 1)
   {
     QC_LOG_GLOBAL( "invalid num_altsetting %u\n", pIntf->num_altsetting );
      return -ENODEV;
   }

   // Collect In and Out endpoints
   numEndpoints = pIntf->cur_altsetting->desc.bNumEndpoints;
   for (endpointIndex = 0; endpointIndex < numEndpoints; endpointIndex++)
   {
      pEndpoint = pIntf->cur_altsetting->endpoint + endpointIndex;
      if (pEndpoint == NULL)
      {
        QC_LOG_GLOBAL( "invalid endpoint %u\n", endpointIndex );
         return -ENODEV;
      }

      if (usb_endpoint_dir_in( &pEndpoint->desc ) == true
      &&  usb_endpoint_xfer_int( &pEndpoint->desc ) == false)
      {
         pIn = pEndpoint;
      }
      else if (usb_endpoint_dir_out( &pEndpoint->desc ) == true)
      {
         pOut = pEndpoint;
      }
   }

   if (pIn == NULL || pOut == NULL)
   {
     QC_LOG_GLOBAL( "invalid endpoints\n" );
      return -ENODEV;
   }

   if (usb_set_interface( pDev->udev,
                          pIntf->cur_altsetting->desc.bInterfaceNumber,
                          0 ) != 0)
   {
     QC_LOG_GLOBAL( "unable to set interface\n" );
      return -ENODEV;
   }

   pDev->in = usb_rcvbulkpipe( pDev->udev,
                   pIn->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK );
   pDev->out = usb_sndbulkpipe( pDev->udev,
                   pOut->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK );

   QC_LOG_GLOBAL( "in %x, out %x\n",
        pIn->desc.bEndpointAddress,
        pOut->desc.bEndpointAddress );

   // In later versions of the kernel, usbnet helps with this
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))
   pIntf->dev.platform_data = (void *)pDev;
#endif

   return 0;
}

/*===========================================================================
METHOD:
   GobiMUXNetDriverBind (Public Method)

DESCRIPTION:
   Setup in and out pipes, for the virtual adapters

PARAMETERS
   pDev           [ I ] - Pointer to usbnet device
   pIntf          [ I ] - Pointer to interface

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
static int GobiMUXNetDriverBind(
   struct usbnet *         pDev,
   struct usb_interface *  pIntf )
{
   int numEndpoints;
   int endpointIndex;
   struct usb_host_endpoint * pEndpoint = NULL;
   struct usb_host_endpoint * pIn = NULL;
   struct usb_host_endpoint * pOut = NULL;

   // Verify one altsetting
   if (pIntf->num_altsetting != 1)
   {
     QC_LOG_GLOBAL( "invalid num_altsetting %u\n", pIntf->num_altsetting );
      return -ENODEV;
   }

   // Collect In and Out endpoints
   numEndpoints = pIntf->cur_altsetting->desc.bNumEndpoints;
   for (endpointIndex = 0; endpointIndex < numEndpoints; endpointIndex++)
   {
      pEndpoint = pIntf->cur_altsetting->endpoint + endpointIndex;
      if (pEndpoint == NULL)
      {
        QC_LOG_GLOBAL( "invalid endpoint %u\n", endpointIndex );
         return -ENODEV;
      }

      if (usb_endpoint_dir_in( &pEndpoint->desc ) == true
      &&  usb_endpoint_xfer_int( &pEndpoint->desc ) == false)
      {
         pIn = pEndpoint;
      }
      else if (usb_endpoint_dir_out( &pEndpoint->desc ) == true)
      {
         pOut = pEndpoint;
      }
   }

   if (pIn == NULL || pOut == NULL)
   {
     QC_LOG_GLOBAL( "invalid endpoints\n" );
      return -ENODEV;
   }

   if (usb_set_interface( pDev->udev,
                          pIntf->cur_altsetting->desc.bInterfaceNumber,
                          0 ) != 0)
   {
     QC_LOG_GLOBAL( "unable to set interface\n" );
      return -ENODEV;
   }

//   pDev->in = usb_rcvbulkpipe( pDev->udev,
  //                 pIn->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK );
   pDev->out = usb_sndbulkpipe( pDev->udev,
                   pOut->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK );

   QC_LOG_GLOBAL( "in %x, out %x\n",
        pIn->desc.bEndpointAddress,
        pOut->desc.bEndpointAddress );

   // In later versions of the kernel, usbnet helps with this
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))
   pIntf->dev.platform_data = (void *)pDev;
#endif

   return 0;
}

#ifndef VIRTUAL_USB_CODE
static struct attribute_group dev_attr_grp;
#endif
/*===========================================================================
METHOD:
   GobiNetDriverUnbind (Public Method)

DESCRIPTION:
   Deregisters QMI device (Registration happened in the probe function)

PARAMETERS
   pDev           [ I ] - Pointer to usbnet device
   pIntfUnused    [ I ] - Pointer to interface

RETURN VALUE:
   None
===========================================================================*/
static void GobiNetDriverUnbind(
   struct usbnet *         pDev,
   struct usb_interface *  pIntf)
{
   sGobiUSBNet * pGobiDev = (sGobiUSBNet *)pDev->data[0];
#ifdef VIRTUAL_USB_CODE
   struct usbnet       *dev;
   struct net_device   *net;
   int i;

   QC_LOG_DBG(GET_QMIDEV(pGobiDev), "In \n" );

   for (i=0; i<MAX_MUX_DEVICES; i++)
   {
       dev = pGobiDev->mpNetMUXDev[i];
       net = dev->net;
       unregister_netdev (net);
       cancel_work_sync(&dev->kevent);

       usb_scuttle_anchored_urbs(&dev->deferred);

       // Should already be down, but just in case...
       //netif_carrier_off(net);

       usb_kill_urb(dev->interrupt);
       usb_free_urb(dev->interrupt);
   }
#endif

   // Should already be down, but just in case...
   //netif_carrier_off( pDev->net );
#ifdef TX_AGGR
   atomic_set(&(pGobiDev->tx_aggr_ctx.stop), 1);
   
   hrtimer_cancel(&pGobiDev->tx_aggr_ctx.tx_timer);
   
   tasklet_kill(&pGobiDev->tx_aggr_ctx.bh);
#endif

   DeregisterQMIDevice( pGobiDev );

#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,29 ))
   kfree( pDev->net->netdev_ops );
   pDev->net->netdev_ops = NULL;
#ifdef VIRTUAL_USB_CODE
   for (i=0; i<MAX_MUX_DEVICES; i++)
   {
       kfree( pGobiDev->mpNetMUXDev[i]->net->netdev_ops );
       pGobiDev->mpNetMUXDev[i]->net->netdev_ops = NULL;
       kfree(pGobiDev->mpNetMUXDev[i]->padding_pkt);
       free_netdev(pGobiDev->mpNetMUXDev[i]->net); //Avoid accessing the freed location
   }
#endif
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))
   pIntf->dev.platform_data = NULL;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,19 ))
   if(usb_autopm_get_interface(pIntf))
   {
      QC_LOG_GLOBAL("usb_autopm_get_interface failed\n");
       return;
   }
   pIntf->needs_remote_wakeup = 1;
   usb_autopm_put_interface(pIntf);
#endif
   if(pGobiDev)
   {
#ifndef VIRTUAL_USB_CODE
      if(pGobiDev->kobj_gobi)
      {
         sysfs_remove_group(pGobiDev->kobj_gobi,&dev_attr_grp);
         kobject_put(pGobiDev->kobj_gobi);
         pGobiDev->kobj_gobi = NULL;
      }
#endif
      GobiReleaseDevice(pGobiDev);
      kfree(pGobiDev->mpEndpoints);
      pGobiDev = NULL;
   }
   QC_LOG_GLOBAL("Out!");
   return;
}

/*===========================================================================
METHOD:
   GobiUSBNetURBCallback (Public Method)

DESCRIPTION:
   Write is complete, cleanup and signal that we're ready for next packet

PARAMETERS
   pURB     [ I ] - Pointer to sAutoPM struct

RETURN VALUE:
   None
===========================================================================*/
void GobiUSBNetURBCallback( struct urb * pURB )
{
   unsigned long activeURBflags;
   sAutoPM * pAutoPM = (sAutoPM *)pURB->context;
   if (pAutoPM == NULL)
   {
      // Should never happen
     QC_LOG_GLOBAL( "bad context\n" );
      return;
   }

   if (pURB->status != 0)
   {
      // Note that in case of an error, the behaviour is no different
     QC_LOG_GLOBAL( "urb finished with error %d\n", pURB->status );
   }

   // Remove activeURB (memory to be freed later)
   spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );

   // EAGAIN used to signify callback is done
   pAutoPM->mpActiveURB = ERR_PTR( -EAGAIN );

   spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );

   complete( &pAutoPM->mThreadDoWork );

   usb_free_urb( pURB );
}

/*===========================================================================
METHOD:
   GobiUSBNetTXTimeout (Public Method)

DESCRIPTION:
   Timeout declared by the net driver.  Stop all transfers

PARAMETERS
   pNet     [ I ] - Pointer to net device

RETURN VALUE:
   None
===========================================================================*/
void GobiUSBNetTXTimeout( struct net_device * pNet )
{
   struct sGobiUSBNet * pGobiDev = NULL;
   sAutoPM * pAutoPM;
   sURBList * pURBListEntry, * pURBListEntrySafe = NULL;
   unsigned long activeURBflags, URBListFlags;
   struct usbnet * pDev = netdev_priv( pNet );
   struct urb * pURB;

   if (pDev == NULL || pDev->net == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get usbnet device\n" );
      return;
   }

   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get QMIDevice\n" );
      return;
   }
   pAutoPM = &pGobiDev->mAutoPM;

   QC_LOG_DBG(GET_QMIDEV(pGobiDev), "\n" );

   // Grab a pointer to active URB
   spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );
   pURB = pAutoPM->mpActiveURB;
   spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );

   // Stop active URB
   if (pURB != NULL)
   {
      usb_kill_urb( pURB );
   }

   // Cleanup URB List
   spin_lock_irqsave( &pAutoPM->mURBListLock, URBListFlags );
   list_for_each_entry_safe(pURBListEntry,pURBListEntrySafe,&pAutoPM->mURBList,node){
      atomic_dec( &pAutoPM->mURBListLen );
      usb_free_urb( pURBListEntry->mpURB );
      list_del(&pURBListEntry->node);
   }

   spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );

   complete( &pAutoPM->mThreadDoWork );

   return;
}

/*===========================================================================
METHOD:
   GobiUSBNetAutoPMThread (Public Method)

DESCRIPTION:
   Handle device Auto PM state asynchronously
   Handle network packet transmission asynchronously

PARAMETERS
   pData     [ I ] - Pointer to sAutoPM struct

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
static int GobiUSBNetAutoPMThread( void * pData )
{
   unsigned long activeURBflags, URBListFlags;
   sURBList * pURBListEntry, * pURBListEntrySafe = NULL;
   sAutoPM * pAutoPMListEntry;
   struct list_head *headPos;
   int status;
   struct usb_device * pUdev;
   sAutoPM * pAutoPM = (sAutoPM *)pData;
   struct urb * pURB;

   if (pAutoPM == NULL)
   {
     QC_LOG_GLOBAL( "passed null pointer\n" );
      return -EINVAL;
   }

   pUdev = interface_to_usbdev( pAutoPM->mpIntf );

   QC_LOG_GLOBAL( "traffic thread started\n" );

   while (pAutoPM->mbExit == false)
   {
      // Wait for someone to poke us
      wait_for_completion_interruptible( &pAutoPM->mThreadDoWork );

      // Time to exit?
      if (pAutoPM->mbExit == true)
      {
         // Stop activeURB
         spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );
         pURB = pAutoPM->mpActiveURB;
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );

         if (pURB != NULL)
         {
            usb_kill_urb( pURB );
         }
         // Will be freed in callback function

         // Cleanup URB List
         spin_lock_irqsave( &pAutoPM->mURBListLock, URBListFlags );

         list_for_each_entry_safe(pURBListEntry,pURBListEntrySafe,&pAutoPM->mURBList,node){
            atomic_dec( &pAutoPM->mURBListLen );
            usb_free_urb( pURBListEntry->mpURB );
            list_del(&pURBListEntry->node);
         }
         
         spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );

         break;
      }

      // Is our URB active?
      spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );

      // EAGAIN used to signify callback is done
      if (IS_ERR( pAutoPM->mpActiveURB )
      &&  PTR_ERR( pAutoPM->mpActiveURB ) == -EAGAIN )
      {
         pAutoPM->mpActiveURB = NULL;

         // Restore IRQs so task can sleep
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );

         // URB is done, decrement the Auto PM usage count
         usb_autopm_put_interface( pAutoPM->mpIntf );

         // Lock ActiveURB again
         spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );
      }

      if (pAutoPM->mpActiveURB != NULL)
      {
         // There is already a URB active, go back to sleep
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );
         continue;
      }

      // Is there a URB waiting to be submitted?
      spin_lock_irqsave( &pAutoPM->mURBListLock, URBListFlags );
      if (pAutoPM->mpURBList == NULL)
      {
         // No more URBs to submit, go back to sleep
         spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );
         continue;
      }

      // Pop an element
      atomic_dec( &pAutoPM->mURBListLen );
      spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );

      // Set ActiveURB
      pAutoPMListEntry = list_first_entry (headPos,struct sAutoPM, mURBList);
      pAutoPM->mpActiveURB = pAutoPMListEntry->mpURBList->mpURB;
      spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );

      // Tell autopm core we need device woken up
      status = usb_autopm_get_interface( pAutoPM->mpIntf );
      if (status < 0)
      {
        QC_LOG_GLOBAL( "unable to autoresume interface: %d\n", status );

         // likely caused by device going from autosuspend -> full suspend
         if (status == -EPERM)
         {
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,33 ))
            pUdev->auto_pm = 0;
#endif
            GobiSuspend( pAutoPM->mpIntf, PMSG_SUSPEND );
         }

         // Add pURBListEntry back onto pAutoPM->mpURBList
         spin_lock_irqsave( &pAutoPM->mURBListLock, URBListFlags );
         atomic_inc( &pAutoPM->mURBListLen );
         spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );

         spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );
         pAutoPM->mpActiveURB = NULL;
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );

         // Go back to sleep
         continue;
      }

      // Submit URB
      status = usb_submit_urb( pAutoPM->mpActiveURB, GFP_KERNEL );
      if (status < 0)
      {
         // Could happen for a number of reasons
        QC_LOG_GLOBAL( "Failed to submit URB: %d.  Packet dropped\n", status );
         spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );
         usb_free_urb( pAutoPM->mpActiveURB );
         pAutoPM->mpActiveURB = NULL;
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );
         usb_autopm_put_interface( pAutoPM->mpIntf );

         // Loop again
         complete( &pAutoPM->mThreadDoWork );
      }
   }

  QC_LOG_GLOBAL( "traffic thread exiting\n" );
   pAutoPM->mpThread = NULL;
   return 0;
}

/*===========================================================================
METHOD:
   GobiUSBNetStartXmit (Public Method)

DESCRIPTION:
   Convert sk_buff to usb URB and queue for transmit

PARAMETERS
   pNet     [ I ] - Pointer to net device

RETURN VALUE:
   NETDEV_TX_OK on success
   NETDEV_TX_BUSY on error
===========================================================================*/
int GobiUSBNetStartXmit(
   struct sk_buff *     pSKB,
   struct net_device *  pNet )
{
   unsigned long URBListFlags;
   struct sGobiUSBNet * pGobiDev = NULL;
   sAutoPM * pAutoPM;
   sURBList * pURBListEntry;
   void * pURBData;
   struct usbnet * pDev = netdev_priv( pNet );

   if (pDev == NULL || pDev->net == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "failed to get usbnet device\n" );
      return NETDEV_TX_BUSY;
   }

   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "failed to get QMIDevice\n" );
      return NETDEV_TX_BUSY;
   }
   pAutoPM = &pGobiDev->mAutoPM;

   if (GobiTestDownReason( pGobiDev, DRIVER_SUSPENDED ) == true)
   {
      // Should not happen
      QC_LOG_EXCEPTION(GET_QMIDEV(pGobiDev), "device is suspended\n" );
      dump_stack();
      return NETDEV_TX_BUSY;
   }

   // Add qmap hdr
#ifdef VIRTUAL_USB_CODE
   qmap_mux(pSKB, pGobiDev, pDev->data[1]);
#else
   qmap_mux(pSKB, pGobiDev, 0);
#endif

   // Convert the sk_buff into a URB

   // Check if buffer is full
   if (atomic_read((const atomic_t*) &pAutoPM->mURBListLen ) >= txQueueLength)
   {
      QC_LOG_ERR(GET_QMIDEV(pGobiDev), "not scheduling request, buffer is full\n" );
      return NETDEV_TX_BUSY;
   }

   // Allocate URBListEntry
   pURBListEntry = kzalloc( sizeof( sURBList ), GFP_ATOMIC );
   if (pURBListEntry == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "unable to allocate URBList memory\n" );
      return NETDEV_TX_BUSY;
   }

   // Allocate URB
   pURBListEntry->mpURB = usb_alloc_urb( 0, GFP_ATOMIC );
   if (pURBListEntry->mpURB == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "unable to allocate URB\n" );
      kfree( pURBListEntry );
      return NETDEV_TX_BUSY;
   }

   // Allocate URB transfer_buffer
   pURBData = kzalloc( pSKB->len, GFP_ATOMIC );
   if (pURBData == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "unable to allocate URB data\n" );
      usb_free_urb( pURBListEntry->mpURB );
      kfree( pURBListEntry );
      return NETDEV_TX_BUSY;
   }
   // Fill will SKB's data
   memcpy( pURBData, pSKB->data, pSKB->len );

   usb_fill_bulk_urb( pURBListEntry->mpURB,
                      pGobiDev->mpNetDev->udev,
                      pGobiDev->mpNetDev->out,
                      pURBData,
                      pSKB->len,
                      GobiUSBNetURBCallback,
                      pAutoPM );

   QC_LOG_DBG(GET_QMIDEV(pGobiDev), "Write %d bytes\n", pSKB->len );

   if (pSKB->len < 32)
      PrintHex( pURBData, pSKB->len);
   else
      PrintHex( pURBData, 32);

   // Free the transfer buffer on last reference dropped
   pURBListEntry->mpURB->transfer_flags |= URB_FREE_BUFFER;

   // Aquire lock on URBList
   spin_lock_irqsave( &pAutoPM->mURBListLock, URBListFlags );

   // Add URB to end of list
   list_add_tail(&pURBListEntry->node,&pAutoPM->mURBList);
   atomic_inc( &pAutoPM->mURBListLen );

   spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );

   complete( &pAutoPM->mThreadDoWork );

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0)
   // Start transfer timer
   pNet->trans_start = jiffies;
#else
   netif_trans_update(pNet);
#endif
   // Free SKB
   dev_kfree_skb_any( pSKB );

   return NETDEV_TX_OK;
}

/*===========================================================================
METHOD:
   GobiUSBNetOpen (Public Method)

DESCRIPTION:
   Wrapper to usbnet_open, correctly handling autosuspend
   Start AutoPM thread

PARAMETERS
   pNet     [ I ] - Pointer to net device

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int GobiUSBNetOpen( struct net_device * pNet )
{
   int status = 0;
   struct sGobiUSBNet * pGobiDev = NULL;
   struct usbnet * pDev = netdev_priv( pNet );

   if (pDev == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "failed to get usbnet device\n" );
      return -ENXIO;
   }

   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get QMIDevice\n" );
      return -ENXIO;
   }
   // Start the AutoPM thread
   pGobiDev->mAutoPM.mpIntf = pGobiDev->mpIntf;
   pGobiDev->mAutoPM.mbExit = false;
   pGobiDev->mAutoPM.mpURBList = NULL;
   pGobiDev->mAutoPM.mpActiveURB = NULL;
   spin_lock_init( &pGobiDev->mAutoPM.mURBListLock );
   spin_lock_init( &pGobiDev->mAutoPM.mActiveURBLock );
   atomic_set( &pGobiDev->mAutoPM.mURBListLen, 0 );
   init_completion( &pGobiDev->mAutoPM.mThreadDoWork );

   pGobiDev->mAutoPM.mpThread = kthread_run( GobiUSBNetAutoPMThread,
                                               &pGobiDev->mAutoPM,
                                               "GobiUSBNetAutoPMThread" );
   if (IS_ERR( pGobiDev->mAutoPM.mpThread ))
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "AutoPM thread creation error\n" );
      return PTR_ERR( pGobiDev->mAutoPM.mpThread );
   }

   // Allow traffic
   GobiClearDownReason( pGobiDev, NET_IFACE_STOPPED );

   // Pass to usbnet_open if defined
   if (pGobiDev->mpUSBNetOpen != NULL)
   {
      status = pGobiDev->mpUSBNetOpen( pNet );

      // If usbnet_open was successful enable Auto PM
      if (status == 0)
      {
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,33 ))
         usb_autopm_enable( pGobiDev->mpIntf );
#else
         usb_autopm_put_interface( pGobiDev->mpIntf );
#endif
      }
   }
   else
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "no USBNetOpen defined\n" );
   }

   return status;
}

/*===========================================================================
METHOD:
   GobiUSBNetStop (Public Method)

DESCRIPTION:
   Wrapper to usbnet_stop, correctly handling autosuspend
   Stop AutoPM thread

PARAMETERS
   pNet     [ I ] - Pointer to net device
RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int GobiUSBNetStop( struct net_device * pNet )
{
   struct sGobiUSBNet * pGobiDev = NULL;
   struct usbnet * pDev = netdev_priv( pNet );

   if (pDev == NULL || pDev->net == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "failed to get netdevice\n" );
      return -ENXIO;
   }

   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get QMIDevice\n" );
      return -ENXIO;
   }

   // Stop traffic
   GobiSetDownReason( pGobiDev, NET_IFACE_STOPPED );

   // Tell traffic thread to exit
   pGobiDev->mAutoPM.mbExit = true;
   complete( &pGobiDev->mAutoPM.mThreadDoWork );

   // Wait for it to exit
   while( pGobiDev->mAutoPM.mpThread != NULL )
   {
      msleep( 100 );
   }
   QC_LOG_INFO(GET_QMIDEV(pGobiDev), "thread stopped\n" );

   // Pass to usbnet_stop, if defined
   if (pGobiDev->mpUSBNetStop != NULL)
   {
       return pGobiDev->mpUSBNetStop( pNet );
   }
   else
   {
      return 0;
   }
}

#ifdef VIRTUAL_USB_CODE
/*===========================================================================
METHOD:
   GobiMUXUSBNetOpen (Public Method)

DESCRIPTION:
   Wrapper to usbnet_open, for the virtual adapters to enable Tx

PARAMETERS
   pNet     [ I ] - Pointer to net device

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int GobiMUXUSBNetOpen( struct net_device * pNet )
{
   struct sGobiUSBNet * pGobiDev = NULL;
   struct usbnet * pDev = netdev_priv( pNet );

   if (pDev == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pGobiDev), "failed to get usbnet device\n" );
      return -ENXIO;
   }

   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get QMIDevice\n" );
      return -ENXIO;
   }

   netif_start_queue(pNet);
   return 0;
}

/*===========================================================================
METHOD:
   GobiMUXUSBNetStop (Public Method)

DESCRIPTION:
   Wrapper to usbnet_open, for the virtual adapters to disable Tx

PARAMETERS
   pNet     [ I ] - Pointer to net device
RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int GobiMUXUSBNetStop( struct net_device * pNet )
{
   struct sGobiUSBNet * pGobiDev = NULL;
   struct usbnet * pDev = netdev_priv( pNet );

   if (pDev == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get usbnet device\n" );
      return -ENXIO;
   }

   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get QMIDevice\n" );
      return -ENXIO;
   }

   netif_stop_queue(pNet);
   return 0;
}

#endif

int PreparePacket(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
   char *dataPtr;
   PQTI_ETH_HDR ethHdr;
   unsigned long ethLen;

   dataPtr = (char *)skb->data;
   ethLen = skb->len;
   ethHdr = (PQTI_ETH_HDR)dataPtr;

   QC_LOG_GLOBAL( "IPO: -->_PreparePacket: EtherType 0x%04X\n", ethHdr->EtherType );

   // examine Ethernet header
   switch (ntohs(ethHdr->EtherType))
   {
      case ETH_TYPE_ARP:  // && IPV4
      {
         // locally process ARP under IPV4
         ProcessARP(dev, skb);
         return 0;
      }
      case ETH_TYPE_IPV4:
      {
         QC_LOG_GLOBAL( "IPO: _PreparePacket: IP 0x%04X\n", ntohs(ethHdr->EtherType) );
         skb_pull(skb, ETH_HLEN);
         return 1;
      }

      case ETH_TYPE_IPV6:
      {
         QC_LOG_GLOBAL( "IPO: _PreparePacket: IP 0x%04X\n", ntohs(ethHdr->EtherType) );
         skb_pull(skb, ETH_HLEN);
         return 1;
      }
      default:
      {
         QC_LOG_GLOBAL( "IPO: _PreparePacket: IP 0x%04X\n", ntohs(ethHdr->EtherType) );
         return 0;
      }
   }
}  // MPUSB_PreparePacket

void ProcessARP(struct usbnet *dev, struct sk_buff *skb)
{
   PQTI_ARP_HDR arpHdr;
   char *tempHA[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
   int sz;
   int bRespond = 0;
   //unsigned long Length = skb->len;
   char *EthPkt;

   EthPkt = (char *)skb->data;
   QC_LOG_GLOBAL( "IPO: -->ProcessARP: %dB\n", (int)skb->len );

   arpHdr = (PQTI_ARP_HDR)(EthPkt + sizeof(QTI_ETH_HDR));

   // Ignore non-Ethernet HW type and non-request Operation
   if ((ntohs(arpHdr->HardwareType) != 1) || (ntohs(arpHdr->Operation) != 1))
   {
      QC_LOG_GLOBAL( "IPO: ProcessARP: ignore HW %d OP %d\n", arpHdr->HardwareType, arpHdr->Operation);
      return;
   }

   // Ignore non-IPV4 protocol type
   if (ntohs(arpHdr->ProtocolType) != ETH_TYPE_IPV4)
   {
      QC_LOG_GLOBAL( "IPO: ProcessARP: ignore protocol %d\n", arpHdr->ProtocolType);
      return;
   }

   // Validate HLEN and PLEN
   if (arpHdr->HLEN != ETH_ALEN)
   {
      QC_LOG_GLOBAL( "IPO: ProcessARP: wrong HLEN %d\n", arpHdr->HLEN);
      return;
   }
   if (arpHdr->PLEN != 4)
   {
      QC_LOG_GLOBAL( "IPO: ProcessARP: wrong PLEN %d\n", arpHdr->PLEN);
      return;
   }

   // Ignore gratuitous ARP
   if (arpHdr->SenderIP == arpHdr->TargetIP)
   {
      QC_LOG_GLOBAL( "IPO: ProcessARP: ignore gratuitous ARP (IP 0x%d)\n", (int)arpHdr->TargetIP);
      return;
   }

   // Request for HA
   sz = memcmp(arpHdr->TargetHA, tempHA, ETH_ALEN);

   if ((arpHdr->SenderIP != 0) && (ETH_ALEN== sz))
   {
      QC_LOG_GLOBAL( " IPO: ProcessARP: req for HA\n");
      bRespond = 1;
   }
   else
   {
      QC_LOG_GLOBAL( "IPO: ProcessARP: Ignore\n");
   }

   if (bRespond == 1)
   {
      // respond with canned ARP
      ArpResponse(dev, skb);
   }

   QC_LOG_GLOBAL( "IPO: <--ProcessARP: local rsp %d\n", bRespond);
}  // MPUSB_ProcessARP


void ArpResponse(struct usbnet *dev, struct sk_buff *skb)
{
   PQTI_ARP_HDR        arpHdr;
   struct ethhdr *eth;
   struct sk_buff *skbn;
   sGobiUSBNet *pGobiDev;
   char *p;
   unsigned long Length = skb->len;

   pGobiDev = (sGobiUSBNet *)dev->data[0];
   if (IsDeviceValid(pGobiDev) == false)
    {
        QC_LOG_ERR(GET_QMIDEV(pGobiDev),"failed to get QMIDevice\n" );
        return;
    }
   QC_LOG_DBG(GET_QMIDEV(pGobiDev), "IPO: -->MPUSB_ArpResponse: ETH_Len %dB\n", (int)Length);

   skbn = netdev_alloc_skb(dev->net, Length);
   skb_put(skbn, Length);




   eth = (struct ethhdr *)skb_push(skbn,ETH_HLEN + sizeof(QTI_ARP_HDR));
   memcpy(eth->h_dest, dev->net->dev_addr, ETH_ALEN);
   memset(eth->h_source, 0, ETH_ALEN);
   eth->h_proto = __cpu_to_be16(ETH_P_IP);
   skbn->dev = dev->net;


   // 3. Formulate the response
   // Target: arpHdr->SenderHA & arpHdr->SenderIP
   // Sender: pAdapter->MacAddress2 & pAdapter->IPSettings.IPV4.Address
   p = (char *)eth;

   // ARP Header
   arpHdr = (PQTI_ARP_HDR)(p + sizeof(QTI_ETH_HDR));

   // target/requestor MAC and IP
   memcpy(arpHdr->TargetHA, arpHdr->SenderHA, ETH_ALEN);
   // arpHdr->SenderIP = arpHdr->TargetIP;

   // sender/remote MAC and IP
   memcpy(arpHdr->SenderHA, eth->h_source, ETH_ALEN);
   // arpHdr->TargetIP = pGobiDev->IPv4Addr;

   // Operation: reply
   arpHdr->Operation = ntohs(0x0002);

   usbnet_skb_return(dev, skbn);
   QC_LOG_DBG(GET_QMIDEV(pGobiDev), "IPO: IPO: <--MPUSB_ArpResponse\n");
}  // MPUSB_ArpResponse

#ifdef SEQNO_TESTING 
void CheckSeqNo(int prevseqno, int currseqno, int checktxrx)
{
   int TotalSeqMissing = currseqno - prevseqno;
   if (prevseqno == 0)
      return;
   if (TotalSeqMissing >= 2 && checktxrx == 0) {
      QC_LOG_AGGR("Tx_Aggr: Missing SeqNo: %02X and TotalSeqMissing = %d\n", prevseqno+1, TotalSeqMissing - 1);
   }
   if (TotalSeqMissing >= 2 && checktxrx == 1) {
      QC_LOG_AGGR("RxFixup: Missing SeqNo: %02X and TotalSeqMissing = %d\n", prevseqno+1, TotalSeqMissing - 1);
   }
}
#endif //SEQNO_TESTING


#ifdef TX_AGGR
struct sk_buff *aggr_tx_frame(struct usbnet *dev, struct sk_buff *skb)
{
    sGobiUSBNet *pGobiDev = (sGobiUSBNet *)dev->data[0];
    struct rm_cdc_ncm_ctx *ctx;
    struct sk_buff *skb_out;
    u8 ready2send = 0;
    unsigned int mtu;
    u16 n = 0;
#ifdef SEQNO_TESTING
    static int prevSeqNo = 0, currSeqNo = 0, prevUpdate = 0;
#endif
    ctx = &pGobiDev->tx_aggr_ctx;


    /* if there is a remaining skb, it gets priority */
    if (skb != NULL) 
    {
        swap(skb, ctx->tx_rem_skb);
    } 
    else 
    {
        ready2send = 1;
    }

    mtu = dev->net->mtu;

    /* check if we are resuming an OUT skb */
    skb_out = ctx->tx_curr_skb;

    /* allocate a new OUT skb */
    if (!skb_out) {
        ctx->tx_curr_size = ctx->tx_max;
        skb_out = alloc_skb(ctx->tx_curr_size, GFP_ATOMIC);

        /* No allocation possible so we will abort */
        if (skb_out == NULL) {
            if (skb != NULL) {
                dev_kfree_skb_any(skb);
                dev->net->stats.tx_dropped++;
            }
            goto exit_no_skb;
        }
        // QC_LOG_AGGR("tx_aggr: ctx->tx_curr_size = %d\n",ctx->tx_curr_size);
        /* count total number of frames in this NTB */
        ctx->tx_curr_frame_num = 0;
        
        /* recent payload counter for this skb_out */
        ctx->tx_curr_frame_payload = 0;

    }

    for (n = ctx->tx_curr_frame_num; n < ctx->tx_max_datagrams ; n++) 
    {
       void *tmp;

       /* send any remaining skb first */
       if (skb == NULL) 
       {
           skb = ctx->tx_rem_skb;
           ctx->tx_rem_skb = NULL;
   
           /* check for end of skb */
           if (skb == NULL)
               break;
       }

/* no more time for waiting for more frames if tx current size buffer becomes less than current packet length.*/
       if (ctx->tx_curr_size < skb->len)
       {
         /* no room for skb - store for later */
         if (ctx->tx_rem_skb != NULL) {
            dev_kfree_skb_any(ctx->tx_rem_skb);
            dev->net->stats.tx_dropped++;
         }
         ctx->tx_rem_skb = skb;
         skb = NULL;
         ready2send = 1;
         break;
       }
 
       tmp = skb_put(skb_out, skb->len);
       memcpy(tmp, skb->data, skb->len);
       
       ctx->tx_curr_frame_payload += (skb->len - sizeof(qmap_t) - ((qmap_p)tmp)->cd_rsvd_pad );  /* count real tx payload data */
       ctx->tx_curr_size -= (skb->len);
 
       QC_LOG_AGGR("tx_aggr: n = %d, skb->len = %d, skb_out->len = %d, pad = %d, SeqNo: %02X\n", n, skb->len,skb_out->len, ((qmap_p)tmp)->cd_rsvd_pad, ntohs(*((unsigned short *)(skb->data+8))));
       //PrintHex( skb->data , 32);

#ifdef SEQNO_TESTING 
      if (n == 0 && skb->len > 1400) {
         prevSeqNo = ntohs(*((unsigned short *)(skb->data+8)));
         prevUpdate = 1;
      }
      else if (n >= 1 && skb->len > 1400) {
         if (prevUpdate != 1)
            prevSeqNo = currSeqNo;
         currSeqNo = ntohs(*((unsigned short *)(skb->data+8)));
         CheckSeqNo(prevSeqNo, currSeqNo, 0);
         prevUpdate = 0;
         prevSeqNo = currSeqNo;
      }
      else if (skb->len <= 1400) {
         int tmpSeqNo = ntohs(*((unsigned short *)(skb->data+8)));
         if ((tmpSeqNo - prevSeqNo) == 1) {
            prevSeqNo = tmpSeqNo;
            prevUpdate = 1;
         }
      }
#endif

       dev_kfree_skb_any(skb);
       skb = NULL;
    }

    /* free up any dangling skb */
    if (skb != NULL) {
        dev_kfree_skb_any(skb);
        skb = NULL;
        dev->net->stats.tx_dropped++;
    }

    ctx->tx_curr_frame_num = n;
    if (n == 0) 
    {
      /* wait for more frames */
      /* push variables */
      ctx->tx_curr_skb = skb_out;
      goto exit_no_skb;
   }
   if ((n < ctx->tx_max_datagrams) && (ready2send == 0) && (ctx->timer_interval > 0) /*&& (ctx->tx_curr_size > (sizeof(qmap_t) + 4+ MIN_PACKET_SIZE))*/ )
   {
      /* wait for more frames */
      /* push variables */
      ctx->tx_curr_skb = skb_out;
      /* set the pending count */
      if (n < CDC_NCM_RESTART_TIMER_DATAGRAM_CNT) {
         ctx->tx_timer_pending = CDC_NCM_TIMER_PENDING_CNT;
      }
      goto exit_no_skb;
   }
   else {
      if (n == ctx->tx_max_datagrams)
      ctx->tx_reason_max_datagram++;	/* count reason for transmitting */
      
      //QC_LOG_AGGR("tx_reason_max_datagram = %d\n",ctx->tx_reason_max_datagram);
      /* frame goes out */
      /* variables will be reset at next call */
	}

    /* usbnet will count all the framing overhead by default.
     * Adjust the stats so that the tx_bytes counter show real
     * payload data instead.
     */
    //QC_LOG_AGGR("%s: tx_bytes count= %d\n",__func__,(long)ctx->tx_curr_frame_payload - skb_out->len);
    
    /* keep private stats: */

    pGobiDev->tx_aggr_ctx.tx_curr_skb = NULL;
    pGobiDev->tx_aggr_ctx.tx_curr_frame_num = 0;
    pGobiDev->tx_aggr_ctx.tx_curr_size = 0;
    //pGobiDev->tx_aggr_ctx.tx_reason_max_datagram = 0;

    pGobiDev->tx_aggr_ctx.tx_overhead += skb_out->len - ctx->tx_curr_frame_payload;
    
    /* usbnet will count all the framing overhead by default.
    * Adjust the stats so that the tx_bytes counter show real
	 * payload data instead.
	 */
   usbnet_set_skb_tx_stats(skb_out, n,
            (long)ctx->tx_curr_frame_payload - skb_out->len);

   pGobiDev->tx_aggr_ctx.tx_curr_frame_payload = 0;

   QC_LOG_AGGR( "Tx_AGGR %d bytes,  Protocol %d Exit!\n", skb_out->len, skb_out->protocol );

   //if (skb_out->len < 32)
   //  PrintHex( skb_out->data, skb_out->len);
   //else
   //  PrintHex( skb_out->data, 32);

    //cdc_ncm_tx_timeout_cancel(ctx);
   
   //  if (skb_out->len == 0)
   //  {
   //     QC_LOG_AGGR( "Timer expiry issue...Exit!\n");
   //     return NULL;
   //  }
   //  else
   //  {
       return skb_out;
   //  }

exit_no_skb:
    /* Start timer, if there is a remaining non-empty skb */
    if (ctx->tx_curr_skb != NULL && n > 0)
        cdc_ncm_tx_timeout_start(ctx);
    return NULL;
}

static void cdc_ncm_tx_timeout_start(struct rm_cdc_ncm_ctx *ctx)
{
    /* start timer, if not already started */
    if (!(hrtimer_active(&ctx->tx_timer) || atomic_read((const atomic_t*)&ctx->stop)))
        hrtimer_start(&ctx->tx_timer,
                (ktime_t)ctx->timer_interval,
                HRTIMER_MODE_REL);
}

static void cdc_ncm_tx_timeout_cancel(struct rm_cdc_ncm_ctx *ctx)
{
    /* cancel  timer, if its already started */
    if (hrtimer_active(&ctx->tx_timer))
        hrtimer_try_to_cancel(&ctx->tx_timer);
}

static enum hrtimer_restart cdc_ncm_tx_timer_cb(struct hrtimer *timer)
{
    struct rm_cdc_ncm_ctx *ctx =
        container_of(timer, struct rm_cdc_ncm_ctx, tx_timer);

    if (!atomic_read((const atomic_t*)&ctx->stop))
        tasklet_schedule(&ctx->bh);
    return HRTIMER_NORESTART;
}

static void cdc_ncm_txpath_bh(unsigned long param)
{
    struct usbnet *dev = (struct usbnet *)param;
    sGobiUSBNet *pGobiDev = (sGobiUSBNet *)dev->data[0];
    struct rm_cdc_ncm_ctx *ctx;

    ctx = &pGobiDev->tx_aggr_ctx;

   spin_lock_bh(&ctx->mtx);
   if (ctx->tx_timer_pending != 0) {
      //QC_LOG_AGGR("ctx->tx_timer_pending = %lld, restarted timer\n", ctx->tx_timer_pending);
      ctx->tx_timer_pending--;
      cdc_ncm_tx_timeout_start(ctx);
      spin_unlock_bh(&ctx->mtx);
   } else if (dev->net != NULL) {
      spin_unlock_bh(&ctx->mtx);
      netif_tx_lock_bh(dev->net);
      //QC_LOG_AGGR("usbnet_start_xmit starts\n");
      usbnet_start_xmit(NULL, dev->net);
      netif_tx_unlock_bh(dev->net);
   } else {
      spin_unlock_bh(&ctx->mtx);
   }
}

#endif

static struct sk_buff * GobiNetDriver_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
#ifdef TX_AGGR
   struct sk_buff *skb_out;
#endif
#ifdef SEQNO_TESTING
   static int prevSeqNo = 0, currSeqNo = 0, prevUpdate = 0;
#endif
   sGobiUSBNet *pGobiDev = (sGobiUSBNet *)dev->data[0];

    if (pGobiDev->mbQMIReadyStatus == false)
    {
       QC_LOG_GLOBAL( "Device is not ready\n" );
       return NULL;
    }

if (skb != NULL)
{
   if (enableDhcp == 1)
   {
       skb_pull(skb, ETH_HLEN);
   }
#ifdef VIRTUAL_USB_CODE
   qmap_mux(skb, pGobiDev, dev->data[1]);
#else
   qmap_mux(skb, pGobiDev, 0);
#endif

}
#ifdef TX_AGGR
   /* This driver will accumulate multiple QMAP frames and send out a larger
	 * USB frame when the USB buffer is full or when a single jiffies timeout happens.
	 */
   // struct rm_cdc_ncm_ctx *ctx;
   // ctx = &pGobiDev->tx_aggr_ctx;

   // if (ctx == NULL)
   //    goto error;
   
   spin_lock_bh(&pGobiDev->tx_aggr_ctx.mtx);
   skb_out = aggr_tx_frame(dev, skb);
   spin_unlock_bh(&pGobiDev->tx_aggr_ctx.mtx);
   return skb_out;
#endif

   QC_LOG_AGGR("TxFixup %d bytes, Protocol %d, SeqNo: %02X\n", skb->len, skb->protocol, ntohs(*((unsigned short *)(skb->data+8))));

#ifdef SEQNO_TESTING 
   if (skb->len > 1400 && prevUpdate == 0) {
      prevSeqNo = ntohs(*((unsigned short *)(skb->data+8)));
      prevUpdate = 1;
   }
   else if (skb->len > 1400) {
      if (prevUpdate != 1)
      prevSeqNo = currSeqNo;
      currSeqNo = ntohs(*((unsigned short *)(skb->data+8)));
      CheckSeqNo(prevSeqNo, currSeqNo, 0);
      prevUpdate = 0;
      prevSeqNo = currSeqNo;
   }
   else if (skb->len <= 1400) {
      int tmpSeqNo = ntohs(*((unsigned short *)(skb->data+8)));
      if ((tmpSeqNo - prevSeqNo) == 1) {
         prevSeqNo = tmpSeqNo;
         prevUpdate = 1;
      }
   }
#endif

   // if (skb->len < 32)
   //   PrintHex( skb->data, skb->len);
   // else
   //   PrintHex( skb->data, 32);

   return skb;

//error:
//   if (skb != NULL)
//		dev_kfree_skb_any(skb);
//
//	return NULL;
}

static int GobiNetDriver_rx_fixup(struct usbnet *dev_in, struct sk_buff *skb_in)
{
   struct sk_buff *skbn;
   struct sGobiUSBNet * pGobiDev;
   int offset = 0;
   int packet_len = 0;
   int pad_len = 0;
   qmap_p qhdr;
#ifdef SEQNO_TESTING
   static int prevSeqNo = 0, currSeqNo = 0, prevUpdate = 0;
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 4,5,7 ))
   struct ethhdr *eth;
#endif
   u16 Protocol;
   struct usbnet *dev = dev_in;
   pGobiDev = (sGobiUSBNet *)dev->data[0];

   QC_LOG_AGGR("#### Rx_fixup -> total skb_in->len: %d\n",skb_in->len);

   //if (skb_in->len < 32)
   //  PrintHex( skb_in->data, skb_in->len);
   //else
   //  PrintHex( skb_in->data, 32);

   while( offset < skb_in->len )
   {
     qhdr = (qmap_p) (skb_in->data + offset);
     pad_len = qhdr->cd_rsvd_pad & 0x3f;
     packet_len = ntohs(qhdr->pkt_len);

#ifdef VIRTUAL_USB_CODE
     dev = dev_in;
     if (qhdr->mux_id != dev->data[1])
     {
         if (qhdr->mux_id <= 0x84 && qhdr->mux_id >= 0x81)
         {
            if (qhdr->mux_id == 0x81)
            {
               dev = pGobiDev->mpNetDev;
            }
            else
            {
               dev = pGobiDev->mpNetMUXDev[qhdr->mux_id - 0x81 - 1];
            }
         }
     }
#endif

     if (skb_in->data[offset] & 0x80) {
         /* drop the packet, we do not know what to do */
         QC_LOG_GLOBAL("Dropping command packet\n");
         return 1;
     }

     if ((packet_len - pad_len)> 0)
     {
     
        skbn = netdev_alloc_skb(dev->net, packet_len - pad_len);
        skb_put(skbn, packet_len - pad_len);
        memcpy(skbn->data, skb_in->data + offset + sizeof(qmap_t), packet_len - pad_len);
        Protocol = qmap_ip_ethertype(skbn);
   
   #if (LINUX_VERSION_CODE >= KERNEL_VERSION( 4,5,7 ))
         if (Protocol == __cpu_to_be16(ETH_P_IP))
         {
            skbn->protocol = __cpu_to_be16(ETH_P_IP);
         }
         else
         {
            skbn->protocol = __cpu_to_be16(ETH_P_IPV6);
         }
   #else
         eth = (struct ethhdr *)skb_push(skbn,ETH_HLEN);
         memcpy(eth->h_dest, dev->net->dev_addr, ETH_ALEN);
         memset(eth->h_source, 0, ETH_ALEN);
         if (Protocol == __cpu_to_be16(ETH_P_IP))
         {
            eth->h_proto = __cpu_to_be16(ETH_P_IP);
         }
         else
         {
            eth->h_proto = __cpu_to_be16(ETH_P_IPV6);
         }
   #endif
         skbn->dev = dev->net;
   
      QC_LOG_AGGR("RxFixup %d bytes, SeqNo : %02X\n", skbn->len,  ntohs(*((unsigned short *)(skbn->data+4))));

#ifdef SEQNO_TESTING
      if (offset == 0 && skbn->len > 1400) {
         prevSeqNo = ntohs(*((unsigned short *)(skbn->data+4)));
         prevUpdate = 1;
      }
      else if (offset > 0 && skbn->len > 1400) {
        if (prevUpdate != 1)
           prevSeqNo = currSeqNo;
        currSeqNo = ntohs(*((unsigned short *)(skbn->data+4)));
        CheckSeqNo(prevSeqNo, currSeqNo, 1);
        prevUpdate = 0;
        prevSeqNo = currSeqNo;
      }
      else if ( skbn->len <= 1400 ) {
        int tmpSeqNo = ntohs(*((unsigned short *)(skbn->data+4)));
        if ((tmpSeqNo - prevSeqNo) == 1) {
           prevSeqNo = tmpSeqNo;
           prevUpdate = 1;
        }
      }
#endif

   
      // if (skbn->len < 32)
      //  PrintHex( skbn->data, skbn->len);
      // else
      //  PrintHex( skbn->data, 32);
   
         usbnet_skb_return(dev, skbn);
      }
      offset += (packet_len + sizeof(qmap_t));
   }
   return 1;
}

#ifndef VIRTUAL_USB_CODE
static ssize_t
gobi_show(struct device *dev, struct device_attribute *attr, char *buf)
{
   return snprintf(buf, PAGE_SIZE, "Write gobi value %d\n", gobi_sys);
}

static ssize_t
gobi_perf_show(struct device *dev, struct device_attribute *attr, char *buf)
{
   return snprintf(buf, PAGE_SIZE, "0\n");
}

static ssize_t
gobi_perf_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
   check_perf = 1;
   return count;
}

static ssize_t
gobi_store(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
   int value;
   static int id0 = -1;
   static int ipv6id0 = -1;
   static int connectid0 = -1;
   static int connectipv6id0 = -1;
   u8 profileid;
   profileid = 0x01; /* set the correct profile ID here */

   sscanf(buf, "%d", &value);
   gobi_sys = value;

   if (value == 1)
   {
      // connect
      id0 = WDSConnect(gpGobiDev, &gpGobiDev->mQMIDev, profileid, &ipv6id0, &connectid0, &connectipv6id0);
      if (id0 < 0)
      {
         printk("Error Connecting\n");
      }
      else
      {
         printk("Client ID is %d\n", id0);
      }
   }
   else
   {
      if (id0 != -1)
         WDSDisConnect(id0, gpGobiDev, &gpGobiDev->mQMIDev, ipv6id0, connectid0, connectipv6id0);
      id0 = -1;
   }
   return count;
}

static ssize_t
gobi_show_1(struct device *dev, struct device_attribute *attr, char *buf)
{
   return snprintf(buf, PAGE_SIZE, "Write gobi value %d\n", gobi_sys);
}


static ssize_t
gobi_store_1(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
   int value;
   static int id1 = -1;
   static int ipv6id1 = -1;
   static int connectid1 = -1;
   static int connectipv6id1 = -1;
   u8 profileid;
   profileid = 0x02; /* set the correct profile ID here */

   sscanf(buf, "%d", &value);
   gobi_sys = value;

   if (value == 1)
   {
      // connect
      id1 = WDSConnect(gpGobiDev, &gpGobiDev->mQMIMUXDev[0], profileid, &ipv6id1, &connectid1, &connectipv6id1);
      if (id1 < 0)
      {
         printk("Error Connecting\n");
      }
      else
      {
         printk("Client ID is %d\n", id1);
      }
   }
   else
   {
      if (id1 != -1)
         WDSDisConnect(id1, gpGobiDev, &gpGobiDev->mQMIMUXDev[0], ipv6id1, connectid1, connectipv6id1);
      id1 = -1;
   }
   return count;
}

static ssize_t
gobi_show_2(struct device *dev, struct device_attribute *attr, char *buf)
{
   return snprintf(buf, PAGE_SIZE, "Write gobi value %d\n", gobi_sys);
}


static ssize_t
gobi_store_2(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
   int value;
   static int id2 = -1;
   static int ipv6id2 = -1;
   static int connectid2 = -1;
   static int connectipv6id2 = -1;
   u8 profileid;
   profileid = 0x03; /* set the correct profile ID here */

   sscanf(buf, "%d", &value);
   gobi_sys = value;

   if (value == 1)
   {
      // connect
      id2 = WDSConnect(gpGobiDev, &gpGobiDev->mQMIMUXDev[1], profileid, &ipv6id2, &connectid2, &connectipv6id2);
      if (id2 < 0)
      {
         printk("Error Connecting\n");
      }
      else
      {
         printk("Client ID is %d\n", id2);
      }
   }
   else
   {
      if (id2 != -1)
         WDSDisConnect(id2, gpGobiDev, &gpGobiDev->mQMIMUXDev[1], ipv6id2, connectid2, connectipv6id2);
      id2 = -1;
   }
   return count;
}


static DEVICE_ATTR(gobi0, S_IRUGO | S_IWUSR,
                   gobi_show, gobi_store);

static DEVICE_ATTR(gobi1, S_IRUGO | S_IWUSR,
                   gobi_show_1, gobi_store_1);

static DEVICE_ATTR(gobi2, S_IRUGO | S_IWUSR,
                   gobi_show_2, gobi_store_2);

static DEVICE_ATTR(gobi_perf, S_IRUGO | S_IWUSR,
                   gobi_perf_show, gobi_perf_store);

static struct attribute *dev_attrs[] =
{
   &dev_attr_gobi0.attr,
   &dev_attr_gobi1.attr,
   &dev_attr_gobi2.attr,
   &dev_attr_gobi_perf.attr,
   NULL,
};

static struct attribute_group dev_attr_grp =
{
   .attrs = dev_attrs,
};
#endif

/*=========================================================================*/
// Struct driver_info
/*=========================================================================*/
static const struct driver_info GobiNetInfo =
{
   .description   = "GobiNet Ethernet Device",
   .flags         = FLAG_MULTI_PACKET | FLAG_NO_SETINT,
   .bind          = GobiNetDriverBind,
   .unbind        = GobiNetDriverUnbind,
   .data          = 0,
   .rx_fixup      = GobiNetDriver_rx_fixup,
   .tx_fixup      = GobiNetDriver_tx_fixup,
};

/*=========================================================================*/
// Struct driver_info, Binding info is different for Muxed adapters
/*=========================================================================*/
static const struct driver_info GobiMUXNetInfo =
{
   .description   = "GobiNet Ethernet Device",
   .flags         = FLAG_MULTI_PACKET | FLAG_NO_SETINT,
   .bind          = GobiMUXNetDriverBind,
   .unbind        = GobiNetDriverUnbind,
   .data          = 0,
   .rx_fixup      = GobiNetDriver_rx_fixup,
   .tx_fixup      = GobiNetDriver_tx_fixup,
};

#ifdef CONFIG_USB_CODE
/*=========================================================================*/
// QTI VID/PIDs
/*=========================================================================*/
static const struct usb_device_id GobiConfigVIDPIDTable [] =
{
   // Gobi 3000
   {
      .driver_info = (unsigned long)&GobiNetInfo
   },
   //Terminating entry
   { }
};
MODULE_DEVICE_TABLE( usb, GobiConfigVIDPIDTable );

/*=========================================================================*/
// QTI VID/PIDs Binding info is different for Muxed adapters
/*=========================================================================*/
static const struct usb_device_id GobiConfigMUXVIDPID =
{
      .driver_info = (unsigned long)&GobiMUXNetInfo
};
#endif

/*=========================================================================*/
// QTI VID/PIDs
/*=========================================================================*/
static const struct usb_device_id GobiVIDPIDTable [] =
{
   // Gobi 3000
   {
      USB_DEVICE_INTERFACE_NUMBER( 0x05c6, 0x9091, 2 ),
      .driver_info = (unsigned long)&GobiNetInfo
   },
   {
      USB_DEVICE_INTERFACE_NUMBER( 0x05c6, 0x9025, 4 ),
      .driver_info = (unsigned long)&GobiNetInfo
   },
   {
      USB_DEVICE_INTERFACE_NUMBER( 0x05c6, 0x90A8, 4 ),
      .driver_info = (unsigned long)&GobiNetInfo
   },
   {
      USB_DEVICE_INTERFACE_NUMBER( 0x05c6, 0x9092, 2 ),
      .driver_info = (unsigned long)&GobiNetInfo
   },
   {
      USB_DEVICE_INTERFACE_NUMBER( 0x05c6, 0x9022, 2 ),
      .driver_info = (unsigned long)&GobiNetInfo
   },
   //Terminating entry
   { }
};
MODULE_DEVICE_TABLE( usb, GobiVIDPIDTable );

/*=========================================================================*/
// QTI VID/PIDs Binding info is different for Muxed adapters
/*=========================================================================*/
static const struct usb_device_id GobiMUXVIDPIDTable [] =
{
   // Gobi 3000
   {
      USB_DEVICE_INTERFACE_NUMBER( 0x05c6, 0x9091, 2 ),
      .driver_info = (unsigned long)&GobiMUXNetInfo
   },
   {
      USB_DEVICE_INTERFACE_NUMBER( 0x05c6, 0x9025, 4 ),
      .driver_info = (unsigned long)&GobiNetInfo
   },
   {
      USB_DEVICE_INTERFACE_NUMBER( 0x05c6, 0x90A8, 4 ),
      .driver_info = (unsigned long)&GobiNetInfo
   },
   {
      USB_DEVICE_INTERFACE_NUMBER( 0x05c6, 0x9092, 2 ),
      .driver_info = (unsigned long)&GobiNetInfo
   },
   {
      USB_DEVICE_INTERFACE_NUMBER( 0x05c6, 0x9022, 2 ),
      .driver_info = (unsigned long)&GobiNetInfo
   },
   //Terminating entry
   { }
};

#ifdef CONFIG_USB_CODE
static int UpdateDeviceInfo(fileInfo_t **pFileInfo, char *pFilePath)
{
    int ret;

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

    *pFileInfo = kzalloc(sizeof(fileInfo_t) + ret * sizeof(devInfo_t), GFP_KERNEL);
    if (!pFileInfo)
    {
        QC_LOG_GLOBAL("error in allocating memory\n");
        return -ENOMEM;
    }

    (*pFileInfo)->mLength = ret;

    if (QTIDevInfParse(pFilePath, *pFileInfo) < 0)
    {
        QC_LOG_GLOBAL("Error in parsing INF file\n");
        kfree(*pFileInfo);
        *pFileInfo = NULL;
        return -ENXIO;
    }
    QC_LOG_GLOBAL("Number of devices %d ; %d\n", (*pFileInfo)->mNumResp, ret);
    return 0;
}
#endif

/* fetchVal = 0 for fetching the value for adapter selection, 1 for fetching logLevel*/
int findDebugAndLogLevel(unsigned short sys_val,int fetchVal)
{
   if(fetchVal == 0)
      return sys_val >> 8;
   else
      return (sys_val & 0xFF) * 10;
}

/*<===============sysfs starts============>*/
/*
findPosition(unsigned short n) returns the left most 1's position or returns 255 if n = all 1's.
*/
static int findPosition(unsigned short qmi_sys)
{
   unsigned int position = 0;
    
    if (qmi_sys == 0xFF) // All 1's -> Enable for all the adapters
        return 255;

   while (qmi_sys) {
      if (qmi_sys & 1) { /*If in a particular iteration(0th or later iteration) LSB becomes 1, then "position" value is the left most 1's position*/
         break;
      }

      qmi_sys = qmi_sys >> 1;
      ++position;
   }

   return position;
}

static ssize_t debug_show(struct kobject *kobj, 
                struct kobj_attribute *attr, char *buf)
{
        unsigned short qmi_sys = 0, logLvl = 0;
        int i = 0;
        sGobiUSBNet *pDevOnRecord = NULL;

        QC_LOG_GLOBAL("Sysfs - Read!!!\n");

        list_for_each_entry(pDevOnRecord, &DeviceListActive, node)
        {
         QC_LOG_INFO(GET_QMIDEV(pDevOnRecord),"Read gobi value %px and %px and %s\n",pDevOnRecord->kobj_gobi,kobj,
         pDevOnRecord->mDevInfo.mDevInfInfo.mpKey);

            if (pDevOnRecord->kobj_gobi==kobj)
            {
               logLvl = (pDevOnRecord->mQMIDev.logLevel)/10;
               if (pDevOnRecord->mQMIDev.debug == 0xFF) {
                  qmi_sys=pDevOnRecord->mQMIDev.debug;
               }
               else if (pDevOnRecord->mQMIDev.debug == 1) {
                  qmi_sys = pDevOnRecord->mQMIDev.debug;
                  
                  QC_LOG_GLOBAL("For QMI adapter-0 debug= %d, logLevel=%d\n", 
                   pDevOnRecord->mQMIDev.debug,pDevOnRecord->mQMIDev.logLevel );
               }
               for (i=0;i<MAX_MUX_DEVICES;i++) {
                  if (pDevOnRecord->mQMIMUXDev[i].debug == 1) {
                     logLvl = (pDevOnRecord->mQMIMUXDev[i].logLevel)/10;
                     qmi_sys |= pDevOnRecord->mQMIMUXDev[i].debug << (i+1);

                     QC_LOG_GLOBAL("For QMIMUX adapter-%d debug= %d, logLevel=%d\n", i+1, 
                     pDevOnRecord->mQMIMUXDev[i].debug,pDevOnRecord->mQMIMUXDev[i].logLevel);
                  }
               }
               qmi_sys = (qmi_sys<<8) + logLvl;   
            }
        }
        return sprintf(buf, "%04x\n", qmi_sys);
}

static ssize_t debug_store(struct kobject *kobj, 
                struct kobj_attribute *attr,const char *buf, size_t count)
{
        unsigned short qmi_sys = 0, tmp_sys = 0;
        unsigned short sys_val = 0;
        unsigned short pos = 0, i = 0;
        sGobiUSBNet *pDevOnRecord = NULL;
        unsigned short logLvl = 0;

        QC_LOG_GLOBAL("Sysfs - Write!!!\n");

        sscanf(buf,"%hx",&sys_val);

        tmp_sys = qmi_sys = findDebugAndLogLevel(sys_val,0);
        logLvl = findDebugAndLogLevel(sys_val,1);

        QC_LOG_GLOBAL("Sysfs write sys_val=%hu,qmi_sys=%hu,logLvl=%hu\n",sys_val,qmi_sys,logLvl);

        pos = findPosition(qmi_sys);

        list_for_each_entry(pDevOnRecord, &DeviceListActive, node)
        {
            QC_LOG_INFO(GET_QMIDEV(pDevOnRecord),"Write gobi value %px and %px and %s\n",pDevOnRecord->kobj_gobi,
         kobj,pDevOnRecord->mDevInfo.mDevInfInfo.mpKey);

            if (pDevOnRecord->kobj_gobi==kobj)
            {
                if (qmi_sys == 0xFF) {
                   pDevOnRecord->mQMIDev.debug = 1;
                   pDevOnRecord->mQMIDev.logLevel = logLvl;
                  
                   QC_LOG_GLOBAL("(0xFF) QMI adapter-0 debug= %d, logLevel=%d\n", 
                   pDevOnRecord->mQMIDev.debug,pDevOnRecord->mQMIDev.logLevel );

                   for ( i=0;i<MAX_MUX_DEVICES;i++) {
                     pDevOnRecord->mQMIMUXDev[i].debug = 1;
                     pDevOnRecord->mQMIMUXDev[i].logLevel = logLvl;
                   }

                   QC_LOG_GLOBAL("(0xFF) QMIMUX adapter-%d debug= %d, logLevel=%d\n", i+1, pDevOnRecord->mQMIMUXDev[i].debug,pDevOnRecord->mQMIMUXDev[i].logLevel);
                }
                else if (pos >= 0) {

                  if (pos == 0) {
                     pDevOnRecord->mQMIDev.debug = 1;
                     pDevOnRecord->mQMIDev.logLevel = logLvl;
                  }

                  QC_LOG_GLOBAL("For QMI adapter-0 debug= %d, logLevel=%d\n", 
                  pDevOnRecord->mQMIDev.debug,pDevOnRecord->mQMIDev.logLevel );

                  if (pos >= 1) {
                     for (i=0;i<MAX_MUX_DEVICES;i++) {
                        if ((tmp_sys & (tmp_sys - 1)) != 0) {
                           tmp_sys &= ~(1 << i); 
                           pos = findPosition(tmp_sys);
                        }
                        if (pos-1 < MAX_MUX_DEVICES) {
                           pDevOnRecord->mQMIMUXDev[pos-1].debug = 1;
                           pDevOnRecord->mQMIMUXDev[pos-1].logLevel = logLvl;
                        }

                     QC_LOG_GLOBAL("For QMIMUX adapter-%d debug= %d, logLevel=%d\n", i+1, 
                     pDevOnRecord->mQMIMUXDev[i].debug,pDevOnRecord->mQMIMUXDev[i].logLevel);
                     }
                  }

                }   
               
            }
        }
        return count;
}

static ssize_t gobiQMITimer_show(struct kobject *kobj, 
                struct kobj_attribute *attr, char *buf)
{

   sGobiUSBNet *pDevOnRecord = NULL;
   struct rm_cdc_ncm_ctx *ctx = NULL;

   list_for_each_entry(pDevOnRecord, &DeviceListActive, node)
   {
      QC_LOG_INFO(GET_QMIDEV(pDevOnRecord),"Write gobi value %px and %px and %s\n",pDevOnRecord->kobj_gobi,
         kobj,pDevOnRecord->mDevInfo.mDevInfInfo.mpKey);
      
      if (pDevOnRecord->kobj_gobi==kobj)
      {
         QC_LOG_INFO(GET_QMIDEV(pDevOnRecord),"Sysfs - Read!!!\n");
      #ifdef TX_AGGR
         ctx = &pDevOnRecord->tx_aggr_ctx;
         if (ctx->timer_interval)
            return sprintf(buf, "%llu\n", ctx->timer_interval / (u32)NSEC_PER_MSEC);
      #endif
      }
   }

   return -1;
}

static ssize_t gobiQMITimer_store(struct kobject *kobj, 
               struct kobj_attribute *attr,const char *buf, size_t count)
{
   sGobiUSBNet *pDevOnRecord = NULL;
   struct rm_cdc_ncm_ctx *ctx = NULL;

   QC_LOG_GLOBAL( "Sysfs - Write!!!\n");

   list_for_each_entry(pDevOnRecord, &DeviceListActive, node)
   {
      QC_LOG_INFO(GET_QMIDEV(pDevOnRecord),"Write gobi value %px and %px and %s\n",pDevOnRecord->kobj_gobi,
         kobj,pDevOnRecord->mDevInfo.mDevInfInfo.mpKey);
      
      if (pDevOnRecord->kobj_gobi==kobj)
      {
      #ifdef TX_AGGR
         if (pDevOnRecord->tx_aggr_ctx.timer_interval) {

            ssize_t ret;
            unsigned long val;
            ret = kstrtoul(buf, 0, &val);
            if (ret) {
               QC_LOG_ERR(GET_QMIDEV(pDevOnRecord),"Failed from buff\n");
               return ret;
            }
            if (val && (val < 1UL || val > CDC_NCM_TIMER_INTERVAL_MAX)) {
               QC_LOG_ERR(GET_QMIDEV(pDevOnRecord),"failed. send value more than 1");
               return -EINVAL;
            }
            ctx = &pDevOnRecord->tx_aggr_ctx;
            
            spin_lock_bh(&ctx->mtx);
            ctx->timer_interval = val * NSEC_PER_MSEC;
            if (!ctx->timer_interval)
               ctx->tx_timer_pending = 0;
            spin_unlock_bh(&ctx->mtx);

            QC_LOG_INFO(GET_QMIDEV(pDevOnRecord),"Sysfs - Write!!!\n");
            QC_LOG_INFO(GET_QMIDEV(pDevOnRecord),"updated timer: %lld\n", ctx->timer_interval);
         }
      #endif
      } 
   }

   return count;
}

 struct kobj_attribute debug_attr = __ATTR(Debug, S_IRUGO | S_IWUSR, debug_show, debug_store);
 struct kobj_attribute timer_attr = __ATTR(gobiQMITimer, S_IRUGO | S_IWUSR, gobiQMITimer_show, gobiQMITimer_store);

struct attribute *gobinet_sysfs_attrs[] = {
   &debug_attr.attr,
   &timer_attr.attr,
   NULL,
};

const struct attribute_group gobinet_sysfs_attr_group = {
   //.name       = "GobiNet",
	.attrs = gobinet_sysfs_attrs,
};

int sysfs_entry(sGobiUSBNet * dev, char *device_name)
{
   int retval = -ENOMEM;
   
    dev->kobj_gobi = kobject_create_and_add(device_name, NULL);
    if (!dev->kobj_gobi)
      return -ENOMEM;
    retval = sysfs_create_group(dev->kobj_gobi, &gobinet_sysfs_attr_group);
    if (retval && dev->kobj_gobi)
    {
        sysfs_remove_group(dev->kobj_gobi, &gobinet_sysfs_attr_group);
        kobject_put(dev->kobj_gobi);
        dev->kobj_gobi = NULL;
        QC_LOG_ERR(GET_QMIDEV(dev),"sysfs_create_file failed\n");
        return retval;
    }

   return 0;
}

void sysfs_destroy(sGobiUSBNet * dev)
{
   if (dev) {
        if(dev->kobj_gobi)
        {
            sysfs_remove_group(dev->kobj_gobi, &gobinet_sysfs_attr_group);
            kobject_put(dev->kobj_gobi);
            dev->kobj_gobi = NULL;
        }
    }
}
/*<===============sysfs ends============>*/

/*===========================================================================
METHOD:
   GobiUSBNetProbe (Public Method)

DESCRIPTION:
   Run usbnet_probe
   Setup QMI device

PARAMETERS
   pIntf        [ I ] - Pointer to interface
   pVIDPIDs     [ I ] - Pointer to VID/PID table

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int GobiUSBNetProbe(
   struct usb_interface *        pIntf,
   const struct usb_device_id *  pVIDPIDs )
{
   int status;
   int retval;
   struct usbnet * pDev;
#ifdef VIRTUAL_USB_CODE
   struct usbnet * pMUXDev[MAX_MUX_DEVICES];
#endif
   sGobiUSBNet * pGobiDev = NULL;
   sEndpoints * pEndpoints;
   //unsigned int mtu;
   int pipe;
   int i;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,29 ))
   struct net_device_ops * pNetDevOps;
#ifdef VIRTUAL_USB_CODE
   struct net_device_ops * pNetMUXDevOps;
#endif
#endif

#ifdef CONFIG_USB_CODE
   devInfo_t *devInfo;
   char *path;

   path = (gQTIRmnetInfFilePath != NULL) ? gQTIRmnetInfFilePath : QTI_RMNET_INF_PATH;
   retval = QTIDevInfCheckFileStatus(gQTIRmnetFileInfo, path);

   if (retval == false)
   {
       QC_LOG_GLOBAL("No update required\n");
   } else if (retval == true)
   {
       QC_LOG_GLOBAL("Update required\n");
       if (gQTIRmnetFileInfo)
       {
           kfree(gQTIRmnetFileInfo);
           gQTIRmnetFileInfo = NULL;
       }
       if (UpdateDeviceInfo(&gQTIRmnetFileInfo, path) < 0)
       {
           QC_LOG_ERR(GET_QMIDEV(pGobiDev),"Error in parsing INF file\n");
           return -ENXIO;
       }
   } else
   {
      QC_LOG_ERR(GET_QMIDEV(pGobiDev),"Unable to get the status of VID/PID Info\n");
      return -EIO;
   }

   devInfo = QTIDevInfGetDevInfo(pIntf, gQTIRmnetFileInfo);
   if (!devInfo)
   {
       QC_LOG_ERR(GET_QMIDEV(pGobiDev),"USB corresponds to other Iface, (Supports only: %s)\n", path);
       return -EIO;
   }

#endif

   pEndpoints = GatherEndpoints( pIntf );
   if (pEndpoints == NULL)
   {
      return -ENODEV;
   }

   status = usbnet_probe( pIntf, pVIDPIDs );
   if (status < 0)
   {
      QC_LOG_ERR(GET_QMIDEV(pGobiDev), "usbnet_probe failed %d\n", status );
      return status;
   }

#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,19 ))
   status = usb_autopm_get_interface(pIntf);
   if (status < 0)
   {
      QC_LOG_ERR(GET_QMIDEV(pGobiDev), "usb_autopm_get_interface failed %d\n", status );
       return status;
   }
   pIntf->needs_remote_wakeup = 1;
   usb_autopm_put_interface(pIntf);
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,23 ))
   pDev = usb_get_intfdata( pIntf );
#else
   pDev = (struct usbnet *)pIntf->dev.platform_data;
#endif

   if (pDev == NULL || pDev->net == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "failed to get netdevice\n" );
      usbnet_disconnect( pIntf );
      kfree( pEndpoints );
      return -ENXIO;
   }

#if (LINUX_VERSION_CODE > KERNEL_VERSION( 4,5,7 ))
//   pDev->net->flags |= IFF_UP;
#endif

#ifdef VIRTUAL_USB_CODE
   pGobiDev = GobiAcquireDevice(devInfo->mpKey,pDev);
   if (pGobiDev == NULL)
   {
      QC_LOG_ERR(GET_QMIDEV(pGobiDev), "failed to allocate device buffers" );
      return -ENOMEM;
   }
   memcpy(&(pGobiDev->mDevInfo.mDevInfInfo), devInfo, sizeof(devInfo_t));
   for (i=0; i<MAX_MUX_DEVICES; i++)
   {
#ifdef CONFIG_USB_CODE
      //To create the Muxed adapters, with the different driver info
      QC_LOG_DBG(GET_QMIDEV(pGobiDev), "(%d) creating the Muxed adapters, with the different driver info", i+1 );
      status = usbnet_probe( pIntf, &GobiConfigMUXVIDPID);
#else
      //To create the Muxed adapters, with the different driver info
      status = usbnet_probe( pIntf, GobiMUXVIDPIDTable );
#endif
      if (status < 0)
      {
         QC_LOG_ERR(GET_QMIDEV(pGobiDev), "usbnet_probe failed %d\n", status );
         return status;
      }

#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,19 ))
   status = usb_autopm_get_interface(pIntf);
   if (status < 0)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "usb_autopm_get_interface failed %d\n", status );
       return status;
   }
   pIntf->needs_remote_wakeup = 1;
   usb_autopm_put_interface(pIntf);
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,23 ))
      pMUXDev[i] = usb_get_intfdata( pIntf );
#else
      pMUXDev[i] = (struct usbnet *)pIntf->dev.platform_data;
#endif
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 4,5,7 ))
//      pMUXDev[i]->net->flags |= IFF_UP;
#endif
    }

#endif

   //To make sure 'pIntf' contains the info of Main/Primary adapter
   usb_set_intfdata (pIntf, pDev);

   pDev->data[0] = (unsigned long)pGobiDev;

   pGobiDev->DLAggregationMaxDatagram = DL_AGGREGATION_MAX_DATAGRAMS;
   pGobiDev->DLAggregationMaxSize = DL_AGGREGATION_MAX_SIZE;

#ifdef TX_AGGR

   pGobiDev->tx_aggr_ctx.tx_max_datagrams = UL_AGGREGATION_MAX_DATAGRAMS;
   pGobiDev->tx_aggr_ctx.tx_max = UL_AGGREGATION_MAX_SIZE;
   /* initial coalescing timer interval */
   pGobiDev->tx_aggr_ctx.timer_interval = gtimer;
   pGobiDev->tx_aggr_ctx.tx_timer_pending = 0;

   pGobiDev->tx_aggr_ctx.tx_curr_skb = NULL;
   pGobiDev->tx_aggr_ctx.tx_rem_skb = NULL;
   pGobiDev->tx_aggr_ctx.tx_curr_frame_num = 0;
   pGobiDev->tx_aggr_ctx.tx_curr_size = 0;
   pGobiDev->tx_aggr_ctx.tx_curr_frame_payload = 0;

   pGobiDev->ULAggregationMaxSize = pGobiDev->tx_aggr_ctx.tx_max;
   pGobiDev->ULAggregationMaxDatagram = pGobiDev->tx_aggr_ctx.tx_max_datagrams;

   hrtimer_init(&pGobiDev->tx_aggr_ctx.tx_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
   pGobiDev->tx_aggr_ctx.tx_timer.function = &cdc_ncm_tx_timer_cb;
   //TODO:: you can use tasklet_init and can use tasklet_hi_schedule instead tasklet_schedule
   pGobiDev->tx_aggr_ctx.bh.data = (unsigned long)pDev;
   pGobiDev->tx_aggr_ctx.bh.func = cdc_ncm_txpath_bh;
   atomic_set(&pGobiDev->tx_aggr_ctx.stop, 0);

   spin_lock_init(&pGobiDev->tx_aggr_ctx.mtx);

#endif

   pGobiDev->mpNetDev = pDev;

#ifdef VIRTUAL_USB_CODE
   pDev->data[1] = (unsigned long)0X81;

   for (i=0; i<MAX_MUX_DEVICES; i++)
   {
       pMUXDev[i]->data[0] = (unsigned long)pGobiDev;
       pMUXDev[i]->data[1] = pDev->data[1] + i +1;
       pGobiDev->mpNetMUXDev[i] = pMUXDev[i];
   }
#endif
   pGobiDev->mpEndpoints = pEndpoints;

   // Clearing endpoint halt is a magic handshake that brings
   // the device out of low power (airplane) mode
   // NOTE: FCC verification should be done before this, if required
   pipe = usb_rcvintpipe( pGobiDev->mpNetDev->udev,
                           pGobiDev->mpEndpoints->mIntInEndp );
   usb_clear_halt( pGobiDev->mpNetDev->udev, pipe );


   pipe = usb_rcvbulkpipe( pGobiDev->mpNetDev->udev,
                           pGobiDev->mpEndpoints->mBlkInEndp );
   usb_clear_halt( pGobiDev->mpNetDev->udev, pipe );
   pipe = usb_sndbulkpipe( pGobiDev->mpNetDev->udev,
                           pGobiDev->mpEndpoints->mBlkOutEndp );
   usb_clear_halt( pGobiDev->mpNetDev->udev, pipe );

   pipe = usb_rcvbulkpipe( pGobiDev->mpNetDev->udev,
                           pGobiDev->mpEndpoints->mBlkInEndp );
   usb_clear_halt( pGobiDev->mpNetDev->udev, pipe );
   pipe = usb_sndbulkpipe( pGobiDev->mpNetDev->udev,
                           pGobiDev->mpEndpoints->mBlkOutEndp );
   usb_clear_halt( pGobiDev->mpNetDev->udev, pipe );

   /* Set Control Line State */
   retval = usb_control_msg( pGobiDev->mpNetDev->udev,
      usb_sndctrlpipe(pGobiDev->mpNetDev->udev, 0), 0x22,
         USB_RECIP_INTERFACE | USB_TYPE_CLASS,
         0, pGobiDev->mpEndpoints->mIntfNum, NULL,0,USB_CTRL_SET_TIMEOUT);

   retval = usb_control_msg( pGobiDev->mpNetDev->udev,
      usb_sndctrlpipe(pGobiDev->mpNetDev->udev, 0), 0x22,
      USB_RECIP_INTERFACE | USB_TYPE_CLASS,
      1, pGobiDev->mpEndpoints->mIntfNum, NULL,0,USB_CTRL_SET_TIMEOUT);

   // Overload PM related network functions
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,29 ))
   pGobiDev->mpUSBNetOpen = pDev->net->open;
   pDev->net->open = GobiUSBNetOpen;
   pGobiDev->mpUSBNetStop = pDev->net->stop;
   pDev->net->stop = GobiUSBNetStop;
   pDev->net->hard_start_xmit = GobiUSBNetStartXmit;
   pDev->net->tx_timeout = GobiUSBNetTXTimeout;
#ifdef VIRTUAL_USB_CODE
   for (i=0; i<MAX_MUX_DEVICES; i++)
   {
       pGobiDev->mpNetMUXDev[i] = pMUXDev[i];
       pMUXDev[i]->net->open = GobiMUXUSBNetOpen;
       pMUXDev[i]->net->stop = GobiMUXUSBNetStop;
       pMUXDev[i]->net->hard_start_xmit = GobiUSBNetStartXmit;
       pMUXDev[i]->net->tx_timeout = GobiUSBNetTXTimeout;
   }
#endif
#else

   pNetDevOps = kzalloc( sizeof( struct net_device_ops ), GFP_KERNEL );
   if (pNetDevOps == NULL)
   {
     QC_LOG_ERR(GET_QMIDEV(pGobiDev), "falied to allocate net device ops" );
      usbnet_disconnect( pIntf );
      return -ENOMEM;
   }
   memcpy( pNetDevOps, pDev->net->netdev_ops, sizeof( struct net_device_ops ) );

   pGobiDev->mpUSBNetOpen = pNetDevOps->ndo_open;
   pNetDevOps->ndo_open = GobiUSBNetOpen;
   pGobiDev->mpUSBNetStop = pNetDevOps->ndo_stop;
   pNetDevOps->ndo_stop = GobiUSBNetStop;

   pDev->net->netdev_ops = pNetDevOps;

   /* add our sysfs attrs */
	//pDev->net->sysfs_groups[0] = &gobinet_sysfs_attr_group;

   // set ETHER
   if (enableDhcp == 0)
   {
   pDev->net->header_ops = 0;  /* No header */
   pDev->net->type = ARPHRD_ETHER;
   }
   pDev->net->needed_headroom = sizeof(qmap_t);
   pDev->net->needed_tailroom = SKB_TAIL_ROOM;

   if (enableDhcp == 1)
   {
      pDev->net->flags = IFF_NOARP;
   }

#ifdef VIRTUAL_USB_CODE
   for (i=0; i<MAX_MUX_DEVICES; i++)
   {
       pNetMUXDevOps = kzalloc( sizeof( struct net_device_ops ), GFP_KERNEL );
       if (pNetMUXDevOps == NULL)
       {
           QC_LOG_ERR(GET_QMIDEV(pGobiDev), "falied to allocate net device ops" );
           return -ENOMEM;
       }

       memcpy( pNetMUXDevOps, pMUXDev[i]->net->netdev_ops, sizeof( struct net_device_ops ) );

       pNetMUXDevOps->ndo_open = GobiMUXUSBNetOpen;
       pNetMUXDevOps->ndo_stop = GobiMUXUSBNetStop;

       pMUXDev[i]->net->netdev_ops = pNetMUXDevOps;

       // set ETHER
       if (enableDhcp == 0)
       {
           pMUXDev[i]->net->header_ops = 0;  /* No header */
           pMUXDev[i]->net->type = ARPHRD_ETHER;
       }
       pMUXDev[i]->net->needed_headroom = sizeof(qmap_t);
       pMUXDev[i]->net->needed_tailroom = SKB_TAIL_ROOM;

       if (enableDhcp == 1)
       {
           pMUXDev[i]->net->flags = IFF_NOARP;
       }
   }
#endif

#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,31 ))
   memset( &(pGobiDev->mpNetDev->stats), 0, sizeof( struct net_device_stats ) );
#else
   memset( &(pGobiDev->mpNetDev->net->stats), 0, sizeof( struct net_device_stats ) );
#endif

   pGobiDev->mpIntf = pIntf;
   memset( &(pGobiDev->mMEID), '0', 14 );

   QC_LOG_INFO(GET_QMIDEV(pGobiDev), "Mac Address:\n" );
   PrintHex( &pGobiDev->mpNetDev->net->dev_addr[0], 6 );

   pGobiDev->mbQMIValid = false;

   pGobiDev->mQMIDev.mbCdevIsInitialized = false;
   pGobiDev->mQMIDev.mpDevClass = gpClass;
   
   if(pGobiDev->mQMIDev.mdeviceName[0] == '\0')
   {
      pGobiDev->mQMIDev.debug = 1; //Setting default adapter set value
      pGobiDev->mQMIDev.logLevel = QC_LOG_LVL_INFO;//Setting the default loglevel
   }

   INIT_LIST_HEAD(&pGobiDev->mQMIDev.mClientMemList);
   init_completion( &pGobiDev->mAutoPM.mThreadDoWork );
   INIT_LIST_HEAD(&pGobiDev->mAutoPM.mURBList);
   spin_lock_init( &pGobiDev->mQMIDev.mClientMemLock );


   for (i=0;i<MAX_MUX_DEVICES;i++)
   {
       pGobiDev->mQMIMUXDev[i].mbCdevIsInitialized = false;
       pGobiDev->mQMIMUXDev[i].mpDevClass = gpClass;
       if(pGobiDev->mQMIMUXDev[i].mdeviceName[0] == '\0')
       {
         pGobiDev->mQMIMUXDev[i].debug = 1;
         pGobiDev->mQMIMUXDev[i].logLevel = QC_LOG_LVL_INFO;
       }
       INIT_LIST_HEAD(&pGobiDev->mQMIMUXDev[i].mClientMemList);
       spin_lock_init( &pGobiDev->mQMIMUXDev[i].mClientMemLock );
   }
   // Default to device down
   pGobiDev->mDownReason = 0;
   GobiSetDownReason( pGobiDev, NO_NDIS_CONNECTION );
   GobiSetDownReason( pGobiDev, NET_IFACE_STOPPED );

   // Register QMI
   status = RegisterQMIDevice( pGobiDev );
   if (status != 0)
   {
      // usbnet_disconnect() will call GobiNetDriverUnbind() which will call
      // DeregisterQMIDevice() to clean up any partially created QMI device
      usbnet_disconnect( pIntf );
      return status;
   }

   /* need to sub rx URBs for the aggregation size negotiated */
   pDev->rx_urb_size = RX_URB_SIZE;

#ifdef VIRTUAL_USB_CODE
   for (i=0; i<MAX_MUX_DEVICES; i++)
   {
      /* need to sub rx URBs for the aggregation size negotiated */
      pMUXDev[i]->rx_urb_size = RX_URB_SIZE;
   }
#endif

   QC_LOG_INFO(GET_QMIDEV(pGobiDev),"The rx_urb_size size is 0x%X\n", (int)pDev->rx_urb_size);

   gpGobiDev = pGobiDev;
   // Success
   return 0;
}

static struct usb_driver GobiNet =
{
   .name       = "GobiNet",
#ifdef CONFIG_USB_CODE
   .id_table   = GobiConfigVIDPIDTable,
#else
   .id_table   = GobiVIDPIDTable,
#endif
   .probe      = GobiUSBNetProbe,
   .disconnect = usbnet_disconnect,
   .suspend    = GobiSuspend,
   .resume     = GobiResume,
   .supports_autosuspend = true,
};

#if 0
static const struct of_device_id gobinet_table[] =
{
   { .compatible = "qti,gobi-9x35" },
   { },
};
MODULE_DEVICE_TABLE(of,gobinet_table);
struct platform_driver gobi_platform_driver;
#endif

#ifdef PROC_CREATE
static ssize_t GobiUSBprocRead(struct file *file, char __user *ubuf,size_t count, loff_t *ppos)
{
#define BUFSIZE 32
    char buf[BUFSIZE];
    int len=0;

    QC_LOG_GLOBAL("read handler\n");

    if(*ppos > 0 || count < BUFSIZE)
        return 0;

    len += sprintf(buf, "%d %ld ms\n", debug_g, gtimer/NSEC_PER_USEC);

    if(copy_to_user(ubuf,buf,len))
        return -EFAULT;

    *ppos = len;

    return len;
}

static ssize_t GobiUSBprocWrite(struct file *file, const char __user *ubuf,size_t count, loff_t *ppos)
{
    long x, y, num;
    char buf[BUFSIZE];

    if(*ppos > 0 || count > BUFSIZE)
        return -EFAULT;

    if(copy_from_user(buf, ubuf, count))
        return -EFAULT;

    num = sscanf(buf,"%ld %ld", &x, &y);
    if(num != 2)
        return -EFAULT;

    QC_LOG_GLOBAL("write handler debug : %ld, timer : %ld ms\n", x, y);
    if (y < 2)
    {
        QC_LOG_GLOBAL("Invalid timer value\n");
        return -EFAULT;
    }

    debug_g = !!x;
    gtimer = y * NSEC_PER_USEC;

    *ppos = strlen(buf);

    return *ppos;
}

static struct file_operations procOps =
{
    .owner = THIS_MODULE,
    .read  = GobiUSBprocRead,
    .write = GobiUSBprocWrite,
};

#endif

/*===========================================================================
METHOD:
   GobiUSBNetModInit (Public Method)

DESCRIPTION:
   Initialize module
   Create device class
   Register out usb_driver struct

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
//static int GobiUSBNetModInit(struct platform_device *pdev)
static int GobiUSBNetModInit(void)
{
   int retVal;
   QC_LOG_GLOBAL( "In\n");
#ifdef CONFIG_USB_CODE
   gQTIRmnetInfFilePath = (gQTIRmnetInfFilePath != NULL) ? gQTIRmnetInfFilePath : QTI_RMNET_INF_PATH;

   if (UpdateDeviceInfo(&gQTIRmnetFileInfo, gQTIRmnetInfFilePath) < 0)
   {
       QC_LOG_GLOBAL("Error in parsing INF file\n");
       return -ENXIO;
   }
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(6,3,13))
   gpClass = class_create( THIS_MODULE, "GobiQMI" );
#else
   gpClass = class_create("GobiQMI" );
#endif
   if (IS_ERR( gpClass ) == true)
   {
     QC_LOG_GLOBAL( "error at class_create %ld\n",
           PTR_ERR( gpClass ) );
#ifdef CONFIG_USB_CODE
     if (gQTIRmnetFileInfo)
     {
         kfree(gQTIRmnetFileInfo);
         gQTIRmnetFileInfo = NULL;
     }
#endif
      return -ENOMEM;
   }

#ifdef PROC_CREATE
   gpProcEnt = proc_create("GobiQMI_debug_timer", 0777, NULL, &procOps);
   if (gpProcEnt == NULL)
   {
       QC_LOG_GLOBAL( "error at proc_create\n");
#ifdef CONFIG_USB_CODE
      if (gQTIRmnetFileInfo)
      {
          kfree(gQTIRmnetFileInfo);
          gQTIRmnetFileInfo = NULL;
      }
#endif
   }
#endif

   retVal = GobiInitializeDeviceList();
    if (retVal)
    {
        QC_LOG_GLOBAL("QtiInitializeDeviceList failure\n");
    }
    else
    {
        retVal = usb_register( &GobiNet );
        if (retVal)
        {
           QC_LOG_GLOBAL("usb_register failure\n");
           GobiFreeDevices();
        }
    }

   // This will be shown whenever driver is loaded
   QC_LOG_GLOBAL("%s: %s driver is loaded\n", DRIVER_DESC, DRIVER_VERSION );
   return retVal;
}

/*===========================================================================
METHOD:
   GobiUSBNetModExit (Public Method)

DESCRIPTION:
   Deregister module
   Destroy device class

RETURN VALUE:
   void
===========================================================================*/
// static int GobiUSBNetModExit(struct platform_device *pdev)
//static int GobiUSBNetModExit(void)
static void __exit GobiUSBNetModExit(void)
{
   usb_deregister( &GobiNet );
   GobiFreeDevices();
   class_destroy( gpClass );
   proc_remove(gpProcEnt);
#ifdef CONFIG_USB_CODE
   if (gQTIRmnetFileInfo)
   {
       kfree(gQTIRmnetFileInfo);
       gQTIRmnetFileInfo = NULL;
   }
#endif
   return;
   //return 0;
}

#if 0
struct platform_driver gobi_platform_driver =
{
   .probe = GobiUSBNetModInit,
   .remove = GobiUSBNetModExit,
   .driver = {
                .name = "gobinet_driver",
                .owner = THIS_MODULE,
                .of_match_table = gobinet_table,
             },
};

module_platform_driver(gobi_platform_driver);
#endif

module_init(GobiUSBNetModInit);
module_exit(GobiUSBNetModExit);

MODULE_VERSION( DRIVER_VERSION );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("Dual BSD/GPL");

#ifdef bool
#undef bool
#endif

module_param( debug_g, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( debug_g, "Debuging enabled or not" );

module_param( debug_aggr, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( debug_aggr, "Debugging messages of packet aggregation enabled or not" );

module_param( interruptible, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( interruptible, "Listen for and return on user interrupt" );

module_param( txQueueLength, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( txQueueLength,
                  "Number of IP packets which may be queued up for transmit" );

module_param( mux, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( mux, "MUX support enabled or not" );

module_param( enableDhcp, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( enableDhcp,
                  "Enable DHCP over static IP address settings" );

module_param(gQTIRmnetInfFilePath, charp, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC(gQTIRmnetInfFilePath, "Inf File location (Need complete path)");

#if 0
module_param( RmnetIF, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( RmnetIF, "Rmnet Interface number" );
#endif
