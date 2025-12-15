/*
   Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
   SPDX-License-Identifier: BSD-3-Clause

FILE:
   GobiSerial.h

DESCRIPTION:
   Linux Serial USB driver Implementation for QTI hardwar
*/
#ifndef GOBISER_H
#define GOBISER_H

//---------------------------------------------------------------------------
// Global veriable and defination
//---------------------------------------------------------------------------
#define DRIVER_AUTHOR  "QTI"
#define DRIVER_DESC    "GobiSerial"
#define NUM_BULK_EPS   1
#define MAX_BULK_EPS   6

#define GOBI_PORT_NAME_LEN 128
#define GOBI_INT_BUF_SIZE  256
#define GOBI_ERR_CNT_LIMIT 5
#define GOBI_ID_STR        "GOBI"
#define GOBI_SER_DTR       0x01
#define GOBI_SER_RTS       0x02

// Global pointer to usb_serial_generic_close function
// This function is not exported, which is why we have to use a pointer
// instead of just calling it.
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,26 ))
void (* gpClose)
(
   struct usb_serial_port *,
   struct file *
);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,30 ))
void (* gpClose)
(
   struct tty_struct *,
   struct usb_serial_port *,
   struct file *
);
#else // > 2.6.30
void (* gpClose)(struct usb_serial_port *);
int  (* gpWrite) (struct tty_struct *, struct usb_serial_port *, const unsigned char *, int);
#endif

// DBG macro
#define DBG( format, arg... ) \
   if (debug == 1)\
   { \
      printk( KERN_INFO "GobiSerial::%s " format, __FUNCTION__, ## arg ); \
   } \

#define GOBI_DBG(_context_,_dbg_str_) \
        {\
           DBG _dbg_str_;\
        }

#define GobiDBG(_context_,_format_,_arg_...) \
        {\
           /*printk( KERN_INFO "GobiSerial::%s <%s> " _format_, \
           __FUNCTION__, GobiPort(_context_,NULL), ## _arg_ );*/ \
        }

typedef struct _gobi_device_context
{
   struct usb_serial *MySerial;
   struct usb_serial_port *MyPort;
   int        InterfaceNumber;
   int        bInterruptPresent;
   struct urb *pIntUrb;  // usb_alloc_urb( 0, GFP_KERNEL );
   int        IntPipe;
   int        bDevClosed;
   int        bDevRemoved;
   char       IntBuffer[GOBI_INT_BUF_SIZE];
   int        IntErrCnt;
   int        OpenRefCount;
   spinlock_t AccessLock;
   ulong      DebugMask;
   char       PortName[GOBI_PORT_NAME_LEN];
} gobi_device_context;

/*=========================================================================*/
// Function Prototypes
/*=========================================================================*/

// Attach to correct interfaces
static int GobiProbe
(
   struct usb_serial          *pSerial,
   const struct usb_device_id *pID
);

// Start GPS if GPS port, run usb_serial_generic_open
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,26 ))
int GobiOpen
(
   struct usb_serial_port *pPort,
   struct file            *pFilp
);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,31 ))
int GobiOpen
(
   struct tty_struct      *pTTY,
   struct usb_serial_port *pPort,
   struct file            *pFilp
);
#else // > 2.6.31
int GobiOpen
(
   struct tty_struct      *pTTY,
   struct usb_serial_port *pPort
);
#endif

// Stop GPS if GPS port, run usb_serial_generic_close
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,26 ))
void GobiClose
(
   struct usb_serial_port *,
   struct file *
);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,30 ))
void GobiClose
(
   struct tty_struct *,
   struct usb_serial_port *,
   struct file *
);
#else // > 2.6.30
void GobiClose( struct usb_serial_port * );
int  GobiWrite
(
   struct tty_struct      *tty,
   struct usb_serial_port *port,
   const unsigned char    *buf,
   int                    count
);
int  GobiAttach(struct usb_serial *serial);
void GobiDisconnect(struct usb_serial *serial);
void GobiRelease(struct usb_serial *serial);
void IntCallback( struct urb * pIntUrb );
int  ResubmitIntURB( struct urb * pIntUrb );
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,25 ))
// Read data from USB, push to TTY and user space
static void GobiReadBulkCallback( struct urb * pURB );
#endif

// Set reset_resume flag
int GobiSuspend
(
   struct usb_interface *pIntf,
   pm_message_t         powerEvent
);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))
// Restart URBs killed during usb_serial_suspend
int GobiResume( struct usb_interface * pIntf );
#endif

char *GobiPort(gobi_device_context *context, struct usb_serial_port *pPort);
void GobiSetDtrRts(gobi_device_context *context, __u16 DtrRts);
void PrintHex(void *Context, const unsigned char *pBuffer, int BufferSize, char *Tag);

#endif // GOBISER_H
