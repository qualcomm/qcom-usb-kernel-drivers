/*
FILE:
   serial.c

DESCRIPTION:
   Linux Serial USB driver Implementation for QTI hardware

   Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
   SPDX-License-Identifier: BSD-3-Clause
*/

#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/version.h>
#include <linux/slab.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 3,2,0 ))
#include <linux/module.h>
#endif
#include "serial.h"
#include "qtiDevInf.h"
#include "../version.h"
// Debug flag
static ulong debug;

#define CONFIG_USB_CODE
#ifdef CONFIG_USB_CODE
#define QTI_MODEM_INF_PATH "/opt/QTI/QUD/serial/qtimdm.inf"

fileInfo_t  *gQTIModemFileInfo = NULL;
static char *gQTIModemInfFilePath = NULL;
#endif

static const struct usb_device_id GobiConfigVIDPIDTable[] =
{
   {
      .driver_info = 0xffff
   },
   //Terminating entry
   { }
};
MODULE_DEVICE_TABLE( usb, GobiConfigVIDPIDTable );

/*=========================================================================*/
// Struct usb_serial_driver
// Driver structure we register with the USB core
/*=========================================================================*/
static struct usb_driver GobiDriver =
{
   .name       = "GobiSerial",
#if ((LINUX_VERSION_CODE < KERNEL_VERSION( 3,5,0 )))
   .probe      = usb_serial_probe,
   .disconnect = usb_serial_disconnect,
#endif
   //.id_table   = GobiVIDPIDTable,
   .id_table   = GobiConfigVIDPIDTable,
   .suspend    = GobiSuspend,
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))
   .resume     = GobiResume,
#else
   .resume     = usb_serial_resume,
#endif
   .supports_autosuspend = true,
};

/*=========================================================================*/
// Struct usb_serial_driver
/*=========================================================================*/
static struct usb_serial_driver gGobiDevice =
{
   .driver =
   {
      .owner     = THIS_MODULE,
      .name      = "GobiSerial driver",
   },
   .description         = "GobiSerial",
   //.id_table            = GobiVIDPIDTable,
   .id_table   = GobiConfigVIDPIDTable,
   .usb_driver          = &GobiDriver,
   .num_ports           = 1,
   .probe               = GobiProbe,
   .open                = GobiOpen,
   .attach              = GobiAttach,
   .disconnect          = GobiDisconnect,
   .release             = GobiRelease,
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,25 ))
   .num_interrupt_in    = NUM_DONT_CARE,
   .num_bulk_in         = 1,
   .num_bulk_out        = 1,
   .read_bulk_callback  = GobiReadBulkCallback,
#endif
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 3,4,0 ))
   static struct usb_serial_driver * const gGobiDevices[] = {
      &gGobiDevice, NULL
   };
#endif

/*===========================================================================
METHOD:
   GobiPort

DESCRIPTION:
   Returns a name assigned to the serial port

PARAMETERS:
   context: [ I ] - private context for the serial device
   pPort:   [ I ] - serial port structure associated with the device

RETURN VALUE:
   NULL-terminated string describing the name assigned to the serial port
===========================================================================*/
char *GobiPort(gobi_device_context *context, struct usb_serial_port *pPort)
{
   char *id = GOBI_ID_STR;

   if (pPort != NULL)
   {
      if (pPort->port.tty != NULL)
      {
         id = pPort->port.tty->name;
      }
   }
   else if (context != NULL)
   {
      if (context->PortName[0] != 0)
      {
         id = context->PortName;
      }
      else if (context->MyPort != NULL)
      {
         if (context->MyPort->port.tty != NULL)
         {
            id = context->MyPort->port.tty->name;
         }
      }
   }

   return id;
}  // GobiPort

void PrintHex
(
   void *Context,
   const unsigned char *pBuffer,
   int  BufferSize,
   char *Tag
)
{
   char pPrintBuf[896];
   int pos, bufSize;
   int status;
   gobi_device_context *context = NULL;

   if (Context != NULL)
   {
      context = (gobi_device_context *)Context;
   }

   memset( pPrintBuf, 0 , 896 );

   if (BufferSize < 896)
   {
      bufSize = BufferSize;
   }
   else
   {
      bufSize = 890;
   }
   GobiDBG(context, "=== %s data %d/%d Bytes ===\n", Tag, bufSize, BufferSize);
   for (pos = 0; pos < bufSize; pos++)
   {
      status = snprintf( (pPrintBuf + (pos * 3)),
                         4,
                         "%02X ",
                         *(u8 *)(pBuffer + pos) );
      if (status != 3)
      {
         GobiDBG(context, "snprintf error %d\n", status );
         return;
      }
   }
   GobiDBG(context, "   : %s\n", pPrintBuf );
   return;
}


/*===========================================================================
METHOD:
   GobiSetDtrRts

DESCRIPTION:
   Set or clear DTR/RTS over the USB control pipe

PARAMETERS:
   context: [ I ] - private context for the serial device
   DtrRts:  [ I ] - DTR/RTS bits

RETURN VALUE:
   none
===========================================================================*/
void GobiSetDtrRts(gobi_device_context *context, __u16 DtrRts)
{
   int dtrResult;

   if (context->bInterruptPresent == 0)
   {
      return;
   }

   GobiDBG(context, "--> 0x%x\n", DtrRts);
   dtrResult = usb_control_msg
               (
                  context->MySerial->dev,
                  usb_sndctrlpipe( context->MySerial->dev, 0 ),
                  0x22,
                  0x21,
                  DtrRts,
                  context->InterfaceNumber,
                  NULL,
                  0,
                  100
               );
   GOBI_DBG(context, ( "<%s> <-- Set DTR/RTS 0x%x\n",  GobiPort(context, NULL), DtrRts));
} // GobiSetDtrRts

//---------------------------------------------------------------------------
// USB serial core overridding Methods
//---------------------------------------------------------------------------

#ifdef CONFIG_USB_CODE
static int UpdateDeviceInfo(fileInfo_t **pFileInfo, char *pFilePath)
{
    int ret;

    if (!pFilePath || !pFileInfo)
    {
        DBG("Invalid data\n");
        return -EINVAL;
    }

    ret = QTIDevInfEntrySize(pFilePath);
    if (ret < 0)
    {
        DBG("Failed to get the devices info\n");
        return ret;
    }

    *pFileInfo =
        kzalloc(sizeof(fileInfo_t) + ret * sizeof(devInfo_t), GFP_KERNEL);
    if (!pFileInfo)
    {
        DBG("error in allocating memory\n");
        return -ENOMEM;
    }

    (*pFileInfo)->mLength = ret;

    if (QTIDevInfParse(pFilePath, *pFileInfo) < 0)
    {
        DBG("Error in parsing INF file\n");
        kfree(*pFileInfo);
        *pFileInfo = NULL;
        return -ENXIO;
    }
    printk("Number of devices %d ; %d\n", (*pFileInfo)->mNumResp, ret);
    return 0;
}
#endif

/*===========================================================================
METHOD:
   GobiProbe (Free Method)

DESCRIPTION:
   Attach to correct interfaces

PARAMETERS:
   pSerial    [ I ] - Serial structure
   pID        [ I ] - VID PID table

RETURN VALUE:
   int - negative error code on failure
         zero on success
===========================================================================*/
static int GobiProbe(
   struct usb_serial *             pSerial,
   const struct usb_device_id *    pID )
{
   // Assume failure
   int nRetval = -ENODEV;
   int nNumInterfaces;
   int nInterfaceNum;
   void *context = usb_get_serial_data(pSerial);
   int interruptOk = 0, pipe = 0, intPipe = 0;

#ifdef CONFIG_USB_CODE
   devInfo_t *devInfo;
   char *path;
   int retval;

   path = (gQTIModemInfFilePath != NULL) ? gQTIModemInfFilePath : QTI_MODEM_INF_PATH;
   retval = QTIDevInfCheckFileStatus(gQTIModemFileInfo, path);

   if (retval == false)
   {
       printk("No update required\n");
   } else if (retval == true)
   {
       printk("Update required\n");
       if (gQTIModemFileInfo)
       {
           kfree(gQTIModemFileInfo);
           gQTIModemFileInfo = NULL;
       }
       if (UpdateDeviceInfo(&gQTIModemFileInfo, path) < 0)
       {
           DBG("Error in parsing INF file\n");
           return -ENXIO;
       }
   } else
   {
       printk("Unable to get the status of VID/PID Info\n");
       return -EIO;
   }

   devInfo = QTIDevInfGetDevInfo(pSerial->interface, gQTIModemFileInfo);
   if (!devInfo)
   {
       printk("USB corresponds to other Iface, (Supports only: %s)\n", path);
       return -EIO;
   }
       printk("Success in fetching USB Iface\n");

#endif
   DBG( "-->GobiProbe\n" );

   // Test parameters
   if ( (pSerial == NULL)
   ||   (pSerial->dev == NULL)
   ||   (pSerial->dev->actconfig == NULL)
   ||   (pSerial->interface == NULL)
   ||   (pSerial->interface->cur_altsetting == NULL)
   ||   (pSerial->type == NULL) )
   {
      DBG( "<--GobiProbe: invalid parameter\n" );
      return -EINVAL;
   }

   nNumInterfaces = pSerial->dev->actconfig->desc.bNumInterfaces;
   nInterfaceNum = pSerial->interface->cur_altsetting->desc.bInterfaceNumber;
   DBG( "Obj / Ctxt = 0x%p / 0x%p\n", pSerial, context );
   DBG( "Num Interfaces = %d\n", nNumInterfaces );
   DBG( "This Interface = %d\n", nInterfaceNum );
   DBG( "Serial private = 0x%p\n", pSerial->private);

   if (nNumInterfaces == 1)
   {
      DBG( "SIngle function device detected\n" );
   }
   else
   {
      DBG( "Composite device detected\n" );
   }

   nRetval = usb_set_interface( pSerial->dev, nInterfaceNum, 0 );
   if (nRetval < 0)
   {
      DBG( "Could not set interface, error %d\n", nRetval );
   }
   // Check for recursion
   if (pSerial->type->close != GobiClose)
   {
      // Store usb_serial_generic_close in gpClose
      gpClose = pSerial->type->close;
      pSerial->type->close = GobiClose;
      DBG( "Installed GobiClose function: 0x%p/0x%p\n", GobiClose, gpClose );
   }

   if (pSerial->type->write != GobiWrite)
   {
      gpWrite = pSerial->type->write;
      pSerial->type->write = GobiWrite;
   }

   if (nRetval == 0)
   {
      // Clearing endpoint halt is a magic handshake that brings
      // the device out of low power (airplane) mode
      // NOTE: FCC verification should be done before this, if required
      struct usb_host_endpoint * pEndpoint;
      int endpointIndex;
      int numEndpoints = pSerial->interface->cur_altsetting
                         ->desc.bNumEndpoints;

      DBG ("numEPs=%d\n", numEndpoints);
      for (endpointIndex = 0; endpointIndex < numEndpoints; endpointIndex++)
      {
         pipe = 0;
         pEndpoint = pSerial->interface->cur_altsetting->endpoint
                   + endpointIndex;

         DBG ("Examining EP 0x%x\n", pEndpoint->desc.bEndpointAddress);
         if (pEndpoint != NULL)
         {
            if (usb_endpoint_dir_out( &pEndpoint->desc ) == true)
            {
               pipe = usb_sndbulkpipe( pSerial->dev,
                                       pEndpoint->desc.bEndpointAddress );
               nRetval = usb_clear_halt( pSerial->dev, pipe );
               DBG ("usb_clear_halt OUT returned %d\n", nRetval);
            }
            else if (usb_endpoint_dir_in( &pEndpoint->desc ) == true)
            {
               if (usb_endpoint_xfer_int( &pEndpoint->desc ) == true)
               {
                  intPipe = usb_rcvintpipe( pSerial->dev,
                                         pEndpoint->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
                  nRetval = usb_clear_halt( pSerial->dev, intPipe );
                  DBG ("usb_clear_halt INT returned %d\n", nRetval);
                  interruptOk = 1;
               }
               else
               {
                  pipe = usb_rcvbulkpipe( pSerial->dev,
                                          pEndpoint->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
                  nRetval = usb_clear_halt( pSerial->dev, pipe );
                  DBG ("usb_clear_halt IN returned %d\n", nRetval);
               }
            }
         }
      }
   }

   if ((nRetval == 0) && (context == NULL))
   {
      gobi_device_context *myContext;

      context = kzalloc( sizeof(gobi_device_context), GFP_KERNEL );
      if (context != NULL)
      {
         DBG ("GobiProbe: Created context 0x%p\n", context);
         myContext = (gobi_device_context *)context;
         myContext->InterfaceNumber = nInterfaceNum;
         myContext->bInterruptPresent = interruptOk;
         myContext->IntPipe = intPipe;
         myContext->pIntUrb = NULL;
         myContext->bDevClosed = myContext->bDevRemoved = 0;
         myContext->MySerial = pSerial;
         myContext->MyPort = NULL;
         myContext->IntErrCnt = 0;
         myContext->OpenRefCount = 0;
         myContext->DebugMask = debug = 0;
         spin_lock_init(&myContext->AccessLock);
         memset(myContext->PortName, 0, GOBI_PORT_NAME_LEN);
         usb_set_serial_data(pSerial, context);
      }
   }
   DBG( "<--GobiProbe\n");
   return nRetval;
}  // GobiProbe

/*===========================================================================
METHOD:
   GobiAttach

DESCRIPTION:
   Attach to serial device

PARAMETERS:
   serial    [ I ] - Serial structure

RETURN VALUE:
   int - negative error code on failure
         zero on success
===========================================================================*/
int GobiAttach(struct usb_serial *serial)
{
   gobi_device_context *context;

   DBG ("-->GobiAttach\n");
   context = (gobi_device_context *)usb_get_serial_data(serial);
   if (context->bInterruptPresent == 0)
   {
      DBG( "<--GobiAttach: no action\n" );
      return 0;
   }

   context->pIntUrb = usb_alloc_urb( 0, GFP_KERNEL );
   if (context->pIntUrb == NULL)
   {
      DBG( "<--GobiAttach: Error allocating int urb\n" );
      return -ENOMEM;
   }

   DBG ("<--GobiAttach\n");
   return 0;
}  // GobiAttach

/*===========================================================================
METHOD:
   GobiDisconnect

DESCRIPTION:
   Disconnect from serial device

PARAMETERS:
   serial    [ I ] - Serial structure

RETURN VALUE:
   none
===========================================================================*/
void GobiDisconnect(struct usb_serial *serial)
{
   gobi_device_context *context = (gobi_device_context *)usb_get_serial_data(serial);

   GOBI_DBG(context, ("<%s> -->\n", GobiPort(context, NULL)));
   if (context != NULL)
   {
      context->bDevRemoved = 1;
      if (context->pIntUrb != NULL)
      {
         usb_kill_urb(context->pIntUrb);
         usb_free_urb(context->pIntUrb);
         context->pIntUrb = NULL;
      }
      else
      {
         GOBI_DBG(context, ( "<%s> Interrupt URB cleared\n",  GobiPort(context, NULL)));
      }
   }
   GOBI_DBG(context, ("<%s> <--\n",  GobiPort(context, NULL)));
}  // GobiDisconnect

/*===========================================================================
METHOD:
   GobiRelease

DESCRIPTION:
   Release allocated resources

PARAMETERS:
   serial    [ I ] - Serial structure

RETURN VALUE:
   none
===========================================================================*/
void GobiRelease(struct usb_serial *serial)
{
   gobi_device_context *context = (gobi_device_context *)usb_get_serial_data(serial);

   GOBI_DBG(context, ("<%s> -->\n",  GobiPort(context, NULL)));
   if (context != NULL)
   {
      context->bDevRemoved = 1;
      if (context->pIntUrb != NULL)
      {
         usb_kill_urb(context->pIntUrb);
         usb_free_urb(context->pIntUrb);
         context->pIntUrb = NULL;
      }
      else
      {
         GOBI_DBG(context, ("<%s> Interrupt URB cleared\n",  GobiPort(context, NULL)));
      }
      kfree(context);
      context = NULL;
      usb_set_serial_data(serial, NULL);
   }
   GOBI_DBG(context, ("<%s> <--\n",  GobiPort(context, NULL)));
}  // GobiRelease

/*===========================================================================
METHOD:
   IntCallback

DESCRIPTION:
   Callback function for interrupt URB

PARAMETERS:
   pIntUrb    [ I ] - URB

RETURN VALUE:
   none
===========================================================================*/
void IntCallback( struct urb * pIntUrb )
{
   gobi_device_context *context = (gobi_device_context *)pIntUrb->context;

   GobiDBG(context, "--> status = %d\n",  pIntUrb->status);
   if (pIntUrb->status != 0)
   {
      // Ignore EOVERFLOW errors
      if (pIntUrb->status != -EOVERFLOW)
      {
         GobiDBG(context, "<-- status = %d\n",  pIntUrb->status);
         context->IntErrCnt++;
         return;
      }
   }
   else
   {
      context->IntErrCnt = 0;
      DBG("IntCallback: %d bytes\n", pIntUrb->actual_length);
      PrintHex(context, pIntUrb->transfer_buffer, pIntUrb->actual_length, "INT");
   }

   if ((context->bDevClosed == 0) && (context->bDevRemoved == 0))
   {
      GobiDBG(context, "re-activate interrupt pipe 0x%p\n",  pIntUrb);
      ResubmitIntURB( pIntUrb );
   }
   GobiDBG(context, "<-- status = %d\n",  pIntUrb->status);
}  // IntCallback

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
   int status;
   int interval;
   gobi_device_context *context = NULL;

   // Sanity test
   if ( (pIntUrb == NULL)
   ||   (pIntUrb->dev == NULL) )
   {
      GOBI_DBG(context, ( "<%s> <-- NULL URB or dev\n",  GobiPort(context, NULL)));
      return -EINVAL;
   }
   context = (gobi_device_context *)pIntUrb->context;
   GOBI_DBG(context, ( "<%s> -->\n",  GobiPort(context, NULL)));
   if ((context->bDevClosed != 0) || (context->bDevRemoved != 0))
   {
      GOBI_DBG(context, ("<%s> <-- No action\n",  GobiPort(context, NULL)));
      return 0;
   }
   if (context->IntErrCnt > GOBI_ERR_CNT_LIMIT)
   {
      GOBI_DBG(context, ("<%s> <-- stop due to errors %d\n",  GobiPort(context, NULL), context->IntErrCnt));
      return 0;
   }

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
      interval
   );
   status = usb_submit_urb( pIntUrb, GFP_ATOMIC );
   GOBI_DBG(context, ("<%s> <-- status %d\n",  GobiPort(context, NULL), status));

   return status;
}  // ResubmitIntURB

/*===========================================================================
METHOD:
   GobiOpen (Free Method)

DESCRIPTION:
   Start GPS if GPS port, run usb_serial_generic_open

PARAMETERS:
   pTTY    [ I ] - TTY structure (only on kernels <= 2.6.26)
   pPort   [ I ] - USB serial port structure
   pFilp   [ I ] - File structure (only on kernels <= 2.6.31)

RETURN VALUE:
   int - zero for success
       - negative errno on error
===========================================================================*/
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,26 ))
int GobiOpen(
   struct usb_serial_port *   pPort,
   struct file *              pFilp )
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,31 ))
int GobiOpen(
   struct tty_struct *        pTTY,
   struct usb_serial_port *   pPort,
   struct file *              pFilp )
#else // > 2.6.31
int GobiOpen(
   struct tty_struct *        pTTY,
   struct usb_serial_port *   pPort )
#endif
{
   gobi_device_context *context = NULL;
   int genericOpenStatus;
   unsigned long flags;
   #ifdef GPS_AUTO_START
   const char startMessage[] = "$GPS_START";
   int bytesWrote;
   int nResult;
   #endif

   GOBI_DBG(NULL, ("<%s> -->\n",  GobiPort(NULL, pPort)));

   // Test parameters
   if ( (pPort == NULL)
   ||   (pPort->serial == NULL)
   ||   (pPort->serial->dev == NULL)
   ||   (pPort->serial->interface == NULL)
   ||   (pPort->serial->interface->cur_altsetting == NULL) )
   {
      GOBI_DBG(NULL, ("<%s> <-- invalid parameter\n", GobiPort(NULL, pPort)));
      return -EINVAL;
   }

   context = (gobi_device_context *)usb_get_serial_data(pPort->serial);
   if (context->MyPort == NULL)
   {
      context->MyPort = pPort;
      if (pPort->port.tty != NULL)
      {
         strncpy(context->PortName, pPort->port.tty->name, (GOBI_PORT_NAME_LEN-1));
      }
   }

   spin_lock_irqsave(&context->AccessLock, flags);
   if (context->OpenRefCount > 0)
   {
      GobiDBG(context, "<--device busy, open denied. RefCnt=%d\n", context->OpenRefCount);
      spin_unlock_irqrestore(&context->AccessLock, flags);
      return -EIO;
   }
   else
   {
      context->OpenRefCount++;
      spin_unlock_irqrestore(&context->AccessLock, flags);
   }
   #ifdef GPS_AUTO_START
   // Is this the GPS port?
   if (pPort->serial->interface->cur_altsetting->desc.bInterfaceNumber == 3)
   {
      // Send startMessage, 1s timeout
      nResult = usb_bulk_msg( pPort->serial->dev,
                              usb_sndbulkpipe( pPort->serial->dev,
                                               pPort->bulk_out_endpointAddress ),
                              (void *)&startMessage[0],
                              sizeof( startMessage ),
                              &bytesWrote,
                              1000 );
      if (nResult != 0)
      {
         GOBI_DBG(NULL, ( "<%s> <-- error %d sending startMessage\n", GobiPort(NULL, pPort), nResult ));
         return nResult;
      }
      if (bytesWrote != sizeof( startMessage ))
      {
         GOBI_DBG(NULL, ("<%s> <-- invalid write size %d, %lu\n", GobiPort(NULL, pPort),
                  bytesWrote, sizeof( startMessage )));
         return -EIO;
      }
   }
   #endif // GPS_AUTO_START

   context->bDevClosed = 0;
   if ((context->pIntUrb != NULL) && (context->bDevRemoved == 0))
   {
      if (context->bInterruptPresent != 0)
      {
         int interval = 9;

         GOBI_DBG(NULL, ("<%s> start interrupt EP\n", GobiPort(NULL, pPort)));
         context->IntErrCnt = 0;
         usb_fill_int_urb
         (
            context->pIntUrb,
            context->MySerial->dev,
            context->IntPipe,
            context->IntBuffer,
            GOBI_INT_BUF_SIZE,
            IntCallback,
            context,
            interval
         );

         usb_submit_urb( context->pIntUrb, GFP_KERNEL );

         // set DTR/RTS
         GobiSetDtrRts(context, (GOBI_SER_DTR | GOBI_SER_RTS));
      }
   }

   // Pass to usb_serial_generic_open
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,26 ))
   genericOpenStatus = usb_serial_generic_open( pPort, pFilp );
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,31 ))
   genericOpenStatus = usb_serial_generic_open( pTTY, pPort, pFilp );
#else // > 2.6.31
   genericOpenStatus = usb_serial_generic_open( pTTY, pPort );
#endif

   if (genericOpenStatus != 0)
   {
      spin_lock_irqsave(&context->AccessLock, flags);
      context->OpenRefCount--;
      spin_unlock_irqrestore(&context->AccessLock, flags);
   }

   GobiDBG(context, "<-- ST %d RefCnt %d\n", genericOpenStatus, context->OpenRefCount);
   return genericOpenStatus;
}  // GobiOpen

/*===========================================================================
METHOD:
   GobiClose (Free Method)

DESCRIPTION:
   Stop GPS if GPS port, run usb_serial_generic_close

PARAMETERS:
   pTTY    [ I ] - TTY structure (only if kernel > 2.6.26 and <= 2.6.30)
   pPort   [ I ] - USB serial port structure
   pFilp   [ I ] - File structure (only on kernel <= 2.6.30)
===========================================================================*/
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,26 ))
void GobiClose(
   struct usb_serial_port *   pPort,
   struct file *              pFilp )
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,30 ))
void GobiClose(
   struct tty_struct *        pTTY,
   struct usb_serial_port *   pPort,
   struct file *              pFilp )
#else // > 2.6.30
void GobiClose( struct usb_serial_port * pPort )
#endif
{
   #ifdef GPS_AUTO_START
   const char stopMessage[] = "$GPS_STOP";
   int nResult;
   int bytesWrote;
   #endif
   gobi_device_context *context;
   unsigned long flags;

   GOBI_DBG(NULL, ("<%s> -->\n", GobiPort(NULL, pPort)));

   // Test parameters
   if ( (pPort == NULL)
   ||   (pPort->serial == NULL)
   ||   (pPort->serial->dev == NULL)
   ||   (pPort->serial->interface == NULL)
   ||   (pPort->serial->interface->cur_altsetting == NULL) )
   {
      GOBI_DBG(NULL, ("<%s> <-- invalid parameter\n", GobiPort(NULL, pPort)));
      return;
   }

   context = (gobi_device_context *)usb_get_serial_data(pPort->serial);
   context->bDevClosed = 1;
   if (context->pIntUrb != NULL)
   {
      GOBI_DBG(NULL, ("<%s> cancel interrupt URB 0x%p\n", GobiPort(NULL, pPort), context->pIntUrb));
      usb_kill_urb(context->pIntUrb);
      // clear DTR/RTS
      GobiSetDtrRts(context, 0);
   }

   spin_lock_irqsave(&context->AccessLock, flags);
   context->OpenRefCount--;
   spin_unlock_irqrestore(&context->AccessLock, flags);

   #ifdef GPS_AUTO_START
   // Is this the GPS port?
   if (pPort->serial->interface->cur_altsetting->desc.bInterfaceNumber == 3)
   {
      // Send stopMessage, 1s timeout
      nResult = usb_bulk_msg( pPort->serial->dev,
                              usb_sndbulkpipe( pPort->serial->dev,
                                               pPort->bulk_out_endpointAddress ),
                              (void *)&stopMessage[0],
                              sizeof( stopMessage ),
                              &bytesWrote,
                              1000 );
      if (nResult != 0)
      {
         GOBI_DBG(NULL, ("<%s> error %d sending stopMessage\n", GobiPort(NULL, pPort), nResult));
      }
      if (bytesWrote != sizeof( stopMessage ))
      {
         GOBI_DBG(NULL, ("<%s> invalid write size %d, %lu\n", GobiPort(NULL, pPort),
                   bytesWrote, sizeof( stopMessage )));
      }
   }
   #endif // GPS_AUTO_START

   // Pass to usb_serial_generic_close
   if (gpClose == NULL)
   {
      GOBI_DBG(NULL, ("<%s> <-- NULL gpClose\n", GobiPort(NULL, pPort)));
      return;
   }

#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,26 ))
   gpClose( pPort, pFilp );
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,30 ))
   gpClose( pTTY, pPort, pFilp );
#else // > 2.6.30
   gpClose( pPort );
#endif
   GobiDBG(context, "<-- with gpClose RefCnt %d\n", context->OpenRefCount);
}  // GobiClose

/*===========================================================================
METHOD:
   GobiWrite

DESCRIPTION:
   Write data over the USB BULK pipe

PARAMETERS:
   tty:    [ I ] - TTY structure associated with the serial device
   pPort:  [ I ] - the serial port structure
   buf:    [ I ] - buffer containing the USB bulk OUT data
   count:  [ I ] - number of bytes of the USB bulk OUT data

RETURN VALUE:
===========================================================================*/
int GobiWrite
(
   struct tty_struct      *tty,
   struct usb_serial_port *pPort,
   const unsigned char    *buf,
   int                    count
)
{
   void *context = usb_get_serial_data(pPort->serial);

   /***
   if (context != NULL)
   {
      GOBI_DBG(NULL, ("<%s> serial ctxt 0x%p / 0x%p\n", GobiPort(NULL, pPort), pPort->serial, context));
   }
   else
   {
      GOBI_DBG(NULL, ("<%s> serial ctxt 0x%p / NULL\n", GobiPort(NULL, pPort), pPort->serial));
   }
   ***/
   PrintHex(context, buf, count, "SEND");
   return gpWrite(tty, pPort, buf, count);
}  // GobiWrite

#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,25 ))

/*===========================================================================
METHOD:
   GobiReadBulkCallback (Free Method)

DESCRIPTION:
   Read data from USB, push to TTY and user space

PARAMETERS:
   pURB  [ I ] - USB Request Block (urb) that called us

RETURN VALUE:
===========================================================================*/
static void GobiReadBulkCallback( struct urb * pURB )
{
   struct usb_serial_port * pPort = pURB->context;
   struct tty_struct * pTTY = pPort->tty;
   int nResult;
   int nRoom = 0;
   unsigned int pipeEP;

   DBG( "port %d\n", pPort->number );

   if (pURB->status != 0)
   {
      DBG( "nonzero read bulk status received: %d\n", pURB->status );

      return;
   }

   usb_serial_debug_data( debug,
                          &pPort->dev,
                          __FUNCTION__,
                          pURB->actual_length,
                          pURB->transfer_buffer );

   // We do no port throttling

   // Push data to tty layer and user space read function
   if (pTTY != 0 && pURB->actual_length)
   {
      nRoom = tty_buffer_request_room( pTTY, pURB->actual_length );
      DBG( "room size %d %d\n", nRoom, 512 );
      if (nRoom != 0)
      {
         tty_insert_flip_string( pTTY, pURB->transfer_buffer, nRoom );
         tty_flip_buffer_push( pTTY );
      }
   }

   pipeEP = usb_rcvbulkpipe( pPort->serial->dev,
                             pPort->bulk_in_endpointAddress );

   // For continuous reading
   usb_fill_bulk_urb( pPort->read_urb,
                      pPort->serial->dev,
                      pipeEP,
                      pPort->read_urb->transfer_buffer,
                      pPort->read_urb->transfer_buffer_length,
                      GobiReadBulkCallback,
                      pPort );

   nResult = usb_submit_urb( pPort->read_urb, GFP_ATOMIC );
   if (nResult != 0)
   {
      DBG( "failed resubmitting read urb, error %d\n", nResult );
   }
}

#endif

/*===========================================================================
METHOD:
   GobiSuspend (Public Method)

DESCRIPTION:
   Set reset_resume flag

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
   struct usb_serial * pDev;

   if (pIntf == 0)
   {
      return -ENOMEM;
   }

   pDev = usb_get_intfdata( pIntf );
   if (pDev == NULL)
   {
      return -ENXIO;
   }

   // Unless this is PM_EVENT_SUSPEND, make sure device gets rescanned
   if ((powerEvent.event & PM_EVENT_SUSPEND) == 0)
   {
      pDev->dev->reset_resume = 1;
   }

   // Run usb_serial's suspend function
   return usb_serial_suspend( pIntf, powerEvent );
}  // GobiSuspend

#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))

/*===========================================================================
METHOD:
   GobiResume (Free Method)

DESCRIPTION:
   Restart URBs killed during usb_serial_suspend

   Fixes 2 bugs in 2.6.23 kernel
      1. pSerial->type->resume was NULL and unchecked, caused crash.
      2. set_to_generic_if_null was not run for resume.

PARAMETERS:
   pIntf  [ I ] - Pointer to interface

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int GobiResume( struct usb_interface * pIntf )
{
   struct usb_serial * pSerial = usb_get_intfdata( pIntf );
   struct usb_serial_port * pPort;
   int portIndex, errors, nResult;

   if (pSerial == NULL)
   {
      DBG( "no pSerial\n" );
      return -ENOMEM;
   }
   if (pSerial->type == NULL)
   {
      DBG( "no pSerial->type\n" );
      return ENOMEM;
   }
   if (pSerial->type->resume == NULL)
   {
      // Expected behaviour in 2.6.23, in later kernels this was handled
      // by the usb-serial driver and usb_serial_generic_resume
      errors = 0;
      for (portIndex = 0; portIndex < pSerial->num_ports; portIndex++)
      {
         pPort = pSerial->port[portIndex];
         if (pPort->open_count > 0 && pPort->read_urb != NULL)
         {
            nResult = usb_submit_urb( pPort->read_urb, GFP_NOIO );
            if (nResult < 0)
            {
               // Return first error we see
               DBG( "error %d\n", nResult );
               return nResult;
            }
         }
      }

      // Success
      return 0;
   }

   // Execution would only reach this point if user has
   // patched version of usb-serial driver.
   return usb_serial_resume( pIntf );
}  // GobiResume

#endif

/*===========================================================================
METHOD:
   GobiInit (Free Method)

DESCRIPTION:
   Register the driver and device

PARAMETERS:

RETURN VALUE:
   int - negative error code on failure
         zero on success
===========================================================================*/
static int __init GobiInit( void )
{
   int nRetval = 0;
   gpClose = NULL;

   gGobiDevice.num_ports = NUM_BULK_EPS;

#ifdef CONFIG_USB_CODE
   gQTIModemInfFilePath = (gQTIModemInfFilePath != NULL) ? gQTIModemInfFilePath : QTI_MODEM_INF_PATH;

   if (UpdateDeviceInfo(&gQTIModemFileInfo, gQTIModemInfFilePath) < 0)
   {
       DBG("Error in parsing INF file\n");
       return -ENXIO;
   }
#endif

   // Registering driver to USB serial core layer
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 3,4,0 ))
      nRetval = usb_serial_register( &gGobiDevice );
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION( 3,5,0 ))
      //nRetval = usb_serial_register_drivers( gGobiDevices, "Gobi", GobiVIDPIDTable);
      nRetval = usb_serial_register_drivers( gGobiDevices, "Gobi", GobiConfigVIDPIDTable);
#else
      nRetval = usb_serial_register_drivers( &GobiDriver, gGobiDevices);
#endif

   if (nRetval != 0)
   {
#ifdef CONFIG_USB_CODE
     if (gQTIModemFileInfo)
     {
         kfree(gQTIModemFileInfo);
         gQTIModemFileInfo = NULL;
     }
#endif
      return nRetval;
   }

   // Registering driver to USB core layer
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 3,4,0 ))
   nRetval = usb_register( &GobiDriver );
   if (nRetval != 0)
   {
      usb_serial_deregister( &gGobiDevice );
#ifdef CONFIG_USB_CODE
     if (gQTIModemFileInfo)
     {
         kfree(gQTIModemFileInfo);
         gQTIModemFileInfo = NULL;
     }
#endif
      return nRetval;
   }
#endif

   // This will be shown whenever driver is loaded
   printk( KERN_INFO "%s: %s\n", DRIVER_DESC, DRIVER_VERSION );

   return nRetval;
}  // GobiInit

/*===========================================================================
METHOD:
   GobiExit (Free Method)

DESCRIPTION:
   Deregister the driver and device

PARAMETERS:

RETURN VALUE:
===========================================================================*/
static void __exit GobiExit( void )
{
   gpClose = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 3,4,0 ))
   usb_deregister( &GobiDriver );
   usb_serial_deregister( &gGobiDevice );
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION( 3,5,0 ))
   usb_serial_deregister_drivers( gGobiDevices );
#else
   usb_serial_deregister_drivers( &GobiDriver, gGobiDevices );
#endif
}  // GobiExit

// Calling kernel module to init our driver
module_init( GobiInit );
module_exit( GobiExit );

MODULE_VERSION( DRIVER_VERSION );
MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("Dual BSD/GPL");

module_param( debug, ulong, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( debug, "Debug enabled or not" );

module_param(gQTIModemInfFilePath, charp, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC(gQTIModemInfFilePath, "Inf File location (Need complete path)");

