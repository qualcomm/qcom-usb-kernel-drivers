/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B C T L. C

GENERAL DESCRIPTION
    This file contains USB-related implementations.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include <stdio.h>
#include "USBMAIN.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBCTL.tmh"

#endif  // EVENT_TRACING

NTSTATUS QCUSB_PassThrough
( 
   PDEVICE_OBJECT pDevObj, 
   PVOID ioBuffer, 
   ULONG outputBufferLength, 
   ULONG *pRetSize
)
{
   NTSTATUS nts;
   UCHAR bRequest;
   USHORT wRecipient, wType;
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   PURB pUrb = NULL;
   PDEVICE_EXTENSION pDevExt;
   ULONG size, DescSize, Rsize;
   USHORT UrbFunction;
   UCHAR UrbDescriptorType, *pData;
   PUSB_DEVICE_DESCRIPTOR deviceDesc = NULL;
   PUSB_ENDPOINT_DESCRIPTOR pEndPtDesc;
   PUSB_INTERFACE_DESCRIPTOR pIfDesc;
   USHORT FlagsWord;
   UCHAR bmRequestType;
 
   pDevExt = pDevObj -> DeviceExtension;

   if (!inDevState(DEVICE_STATE_PRESENT))
   {
      return STATUS_DELETE_PENDING;
   }

   *pRetSize = 0;
   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)ioBuffer;
   FlagsWord = USBD_SHORT_TRANSFER_OK;
   bmRequestType = pRequest -> bmRequestType;

   if ( bmRequestType & BM_REQUEST_DIRECTION_BIT )
   {
      FlagsWord |= USBD_TRANSFER_DIRECTION_IN;
   }

   wType = (bmRequestType >> bmRequestTypeBits)&3;
   wRecipient = bmRequestType&bmRequestRecipientMask;
   /*
   * We now have all the information we need to select and build the correct URBs.
   */
   pData = (PUCHAR)&pRequest -> Data[0];
   bRequest = pRequest -> bRequest;

   switch ( wType )
   {
      case USB_REQUEST_TYPE_STANDARD:  
      {

         switch (bRequest) 
         {
            case USB_REQUEST_GET_DESCRIPTOR: //bRequest
            {
               switch( wRecipient ) 
               {
                  case RECIPIENT_DEVICE:
                  {
                     UrbFunction = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
                     UrbDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
                     DescSize = sizeof(USB_DEVICE_DESCRIPTOR);
                     break;
                  }
                  case RECIPIENT_ENDPOINT:
                  {             
                      UrbFunction = URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT;
                      UrbDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
                      DescSize = sizeof(USB_ENDPOINT_DESCRIPTOR);
                      break;
                  }
                  case RECIPIENT_INTERFACE:
                  {
                      UrbFunction = URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE;
                      UrbDescriptorType = USB_INTERFACE_DESCRIPTOR_TYPE;
                      DescSize = sizeof(USB_INTERFACE_DESCRIPTOR);
                      break;
                  }
                  default:
                  {
                      nts = STATUS_INVALID_PARAMETER;
                      goto func_return;
                  }
               }

               size = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);

               pUrb = ExAllocatePool(
                   NonPagedPool, 
                   size);

               if ( !pUrb ) 
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               if (DescSize > outputBufferLength)
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               deviceDesc = ExAllocatePool(
                   NonPagedPool, 
                   DescSize);

               if ( !deviceDesc )
               {
                  nts = STATUS_NO_MEMORY;
                  goto func_return;
               }

               pUrb -> UrbHeader.Function =  UrbFunction;
               pUrb -> UrbHeader.Length = (USHORT) sizeof (struct _URB_CONTROL_DESCRIPTOR_REQUEST);
               pUrb -> UrbControlDescriptorRequest.TransferBufferLength = size;
               pUrb -> UrbControlDescriptorRequest.TransferBufferMDL = NULL;
               pUrb -> UrbControlDescriptorRequest.TransferBuffer = deviceDesc;
               pUrb -> UrbControlDescriptorRequest.DescriptorType = UrbDescriptorType;
               pUrb -> UrbControlDescriptorRequest.Index = 0;
               pUrb -> UrbControlDescriptorRequest.LanguageId = 0;
               pUrb -> UrbControlDescriptorRequest.UrbLink = NULL;

               nts = QCUSB_CallUSBD( pDevObj, pUrb );

               if( !NT_SUCCESS( nts ) )
               {
                   goto func_return;
               }
                // copy the device descriptor to the output buffer

               RtlCopyMemory( ioBuffer, deviceDesc, deviceDesc->bLength );
               *pRetSize = deviceDesc -> bLength;
       
            
               break;
            } //USB_REQUEST_GET_DESCRIPTOR: //bRequest       
            case USB_REQUEST_SET_DESCRIPTOR: // bRequest
            {
                switch( wRecipient ) 
                {
                  case RECIPIENT_DEVICE:
                  {
                      UrbFunction = URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE;
                      UrbDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
                      DescSize = sizeof(USB_DEVICE_DESCRIPTOR);
                      break;
                  }
                  case RECIPIENT_INTERFACE:
                  {
                      UrbFunction = URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE;
                      UrbDescriptorType = USB_INTERFACE_DESCRIPTOR_TYPE;
                      DescSize = sizeof(USB_INTERFACE_DESCRIPTOR);
                      break;
                  }
                  case RECIPIENT_ENDPOINT:
                  {
                      UrbFunction = URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT;
                      UrbDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
                      DescSize = sizeof(USB_ENDPOINT_DESCRIPTOR);
                      break;
                  }
                  default:   
                  {
                      nts = STATUS_INVALID_PARAMETER;
                      goto func_return;
                  }
               }

               size = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);

               pUrb = ExAllocatePool(
                   NonPagedPool, 
                   size);

               if (!pUrb) 
               {
                   // _fail;
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               if (DescSize > outputBufferLength)
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }


               deviceDesc = ExAllocatePool(
                   NonPagedPool, 
                   DescSize);

               if ( !deviceDesc )
               {
                   // _fail;
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               RtlCopyMemory( deviceDesc, pData, size );

               pUrb -> UrbHeader.Function =  URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE;
               pUrb -> UrbHeader.Length = (USHORT) sizeof (struct _URB_CONTROL_DESCRIPTOR_REQUEST);
               pUrb -> UrbControlDescriptorRequest.TransferBufferLength = size;
               pUrb -> UrbControlDescriptorRequest.TransferBufferMDL = NULL;
               pUrb -> UrbControlDescriptorRequest.TransferBuffer = deviceDesc;
               pUrb -> UrbControlDescriptorRequest.DescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
               pUrb -> UrbControlDescriptorRequest.Index = 0;
               pUrb -> UrbControlDescriptorRequest.LanguageId = 0;
               pUrb -> UrbControlDescriptorRequest.UrbLink = NULL;
               nts = QCUSB_CallUSBD( pDevObj, pUrb );
               break;
            } //USB_REQUEST_SET_DESCRIPTOR //bRequest
          
            case USB_REQUEST_SET_FEATURE: //bRequest
            {
                /*
                  USB defined Feature selectors
                  USB_FEATURE_ENDPOINT_STALL
                  USB_FEATURE_REMOTE_WAKEUP
                  USB_FEATURE_POWER_D0
                  USB_FEATURE_POWER_D1
                  USB_FEATURE_POWER_D2
                  USB_FEATURE_POWER_D3
                */

                switch( wRecipient ) 
                {
                  case RECIPIENT_DEVICE:
                  {
                      UrbFunction = URB_FUNCTION_SET_FEATURE_TO_DEVICE;
                      break;
                  }
                  case RECIPIENT_INTERFACE:
                  {
                      UrbFunction = URB_FUNCTION_SET_FEATURE_TO_INTERFACE;
                      break;
                  }
                  case RECIPIENT_ENDPOINT:
                  {
                      UrbFunction = URB_FUNCTION_SET_FEATURE_TO_ENDPOINT;
                      break;
                  }
                  default:
                  {
                      nts = STATUS_INVALID_PARAMETER;
                      goto func_return;
                  }
                }
               
                size = sizeof( struct _URB_CONTROL_FEATURE_REQUEST );

                pUrb = ExAllocatePool( 
                   NonPagedPool, 
                   size );

                if (!pUrb)
                {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
                }

                UsbBuildFeatureRequest(
                   pUrb,
                   UrbFunction,
                   pRequest -> wValue,   //feature selector, as passed in by caller
                   pRequest -> wIndex,
                   (struct _URB *)NULL);

                nts = QCUSB_CallUSBD( pDevObj, pUrb );     

                break;
            }
            case USB_REQUEST_CLEAR_FEATURE: //bRequest
            {

                switch( wRecipient ) 
                {
                  case RECIPIENT_DEVICE:
                  {
                      UrbFunction = URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE;
                      break;
                  }
                  case RECIPIENT_INTERFACE:
                  {
                      UrbFunction = URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE;
                      break;
                  }
                  case RECIPIENT_ENDPOINT:
                  {
                      UrbFunction = URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT;
                      break;
                  }
                  default:
                  {
                      nts = STATUS_INVALID_PARAMETER;
                      goto func_return;
                  }
                }

                size = sizeof( struct _URB_CONTROL_FEATURE_REQUEST );

                pUrb = ExAllocatePool( 
                   NonPagedPool, 
                   size );
             
                if (!pUrb)
                {
                   // _fail;
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
                }

                UsbBuildFeatureRequest(
                      pUrb,
                      UrbFunction,
                      pRequest -> wValue,      //feature selector, as passed in by caller
                      pRequest -> wIndex,
                      (struct _URB *)NULL);
             
                nts = QCUSB_CallUSBD( pDevObj, pUrb );

                break;
            }

            // Configuration request
            case USB_REQUEST_GET_CONFIGURATION: //bRequest
            {
               switch( wRecipient ) 
               {
                  case RECIPIENT_DEVICE:
                  {
                     UrbFunction = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
                     UrbDescriptorType = USB_CONFIGURATION_DESCRIPTOR_TYPE;
                     DescSize = sizeof(USB_CONFIGURATION_DESCRIPTOR);
                     break;
                  }
                  default:
                  {
                      nts = STATUS_INVALID_PARAMETER;
                      goto func_return;
                  }
               }

               size = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);

               pUrb = ExAllocatePool(
                   NonPagedPool, 
                   size);

               if ( !pUrb ) 
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               if (DescSize > outputBufferLength)
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               deviceDesc = ExAllocatePool(
                   NonPagedPool, 
                   DescSize);

               if ( !deviceDesc )
               {
                  nts = STATUS_NO_MEMORY;
                  goto func_return;
               }

               pUrb -> UrbHeader.Function =  UrbFunction;
               pUrb -> UrbHeader.Length = (USHORT) sizeof (struct _URB_CONTROL_DESCRIPTOR_REQUEST);
               pUrb -> UrbControlDescriptorRequest.TransferBufferLength = size;
               pUrb -> UrbControlDescriptorRequest.TransferBufferMDL = NULL;
               pUrb -> UrbControlDescriptorRequest.TransferBuffer = deviceDesc;
               pUrb -> UrbControlDescriptorRequest.DescriptorType = UrbDescriptorType;
               pUrb -> UrbControlDescriptorRequest.Index = 0;
               pUrb -> UrbControlDescriptorRequest.LanguageId = 0;
               pUrb -> UrbControlDescriptorRequest.UrbLink = NULL;

               nts = QCUSB_CallUSBD( pDevObj, pUrb );

               if( !NT_SUCCESS( nts ) )
               {
                   goto func_return;
               }
                // copy the device descriptor to the output buffer

               RtlCopyMemory( ioBuffer, deviceDesc, deviceDesc -> bLength );
               *pRetSize = deviceDesc -> bLength;

               break;
            } // USB_REQUEST_GET_CONFIGURATION: //bRequest       

            default: //bRequest
            {
                nts = STATUS_INVALID_PARAMETER;
                goto func_return;
            } 
         } //switch (bRequest) 

         break;
      } //wType = USB_REQUEST_TYPE_STANDARD

      case USB_REQUEST_TYPE_VENDOR: //wType
      {
         
         /* URB Function Code
      * Flags Word   Indicates transfer direction and whether short transfer is OK.
      * Request   This is the request code that is interpreted by the device (e.g., GET_PORT_STATUS).
      * Value   This field is defined by the vendor.
      * Index   This field is defined by the vendor.
      * BufSize   Length of data to be sent (if any).
      * Data[]   Data
      */
         switch( wRecipient ) 
         {
            case RECIPIENT_DEVICE:
            {
               UrbFunction = URB_FUNCTION_VENDOR_DEVICE;
               break;
            }
            case RECIPIENT_INTERFACE:
            {
               UrbFunction = URB_FUNCTION_VENDOR_INTERFACE;
               break;
            }
            case RECIPIENT_ENDPOINT:
            {
               UrbFunction = URB_FUNCTION_VENDOR_ENDPOINT;
               break;
            }
            default:
            {
               nts = STATUS_INVALID_PARAMETER;
               goto func_return;
            }
         }

         size = sizeof( struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST );

         pUrb = ExAllocatePool( 
               NonPagedPool, 
               size );
            
         if ( !pUrb )
         {
            // _fail;
            nts =  STATUS_NO_MEMORY;
            goto func_return;
         }

         RtlZeroMemory( pUrb, size );

         UsbBuildVendorRequest (
               pUrb,
               UrbFunction,
               (USHORT)size,         //size of this URB
               FlagsWord,
               0,            //reserved bits
               pRequest -> bRequest,      //request
               pRequest -> wValue,      //value
               pRequest -> wIndex,      //index (zero interface?)
               pData,            //transfer buffer adr
               NULL,            //mdl adr
               pRequest -> wLength,      //size of transfer buffer
               NULL            //URB link adr
              );

         nts = QCUSB_CallUSBD( pDevObj, pUrb );

         if (NT_SUCCESS( nts ))
         {
            *pRetSize = pUrb -> UrbControlVendorClassRequest.TransferBufferLength;
         } else
         {
            *pRetSize = 0;
         }
 
         break;
      }
      
      case USB_REQUEST_TYPE_CLASS: //wType
      {
         switch( wRecipient ) 
         {
            case RECIPIENT_DEVICE:
            {
               UrbFunction = URB_FUNCTION_CLASS_DEVICE;
               break;
            }
            case RECIPIENT_INTERFACE:
            {
               UrbFunction = URB_FUNCTION_CLASS_INTERFACE;
               break;
            }
            case RECIPIENT_ENDPOINT:
            {
               UrbFunction = URB_FUNCTION_CLASS_ENDPOINT;
               break;
            }
            default:        
            {
               nts = STATUS_INVALID_PARAMETER;
               goto func_return;
            }
         }

         size = sizeof( struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST );

         pUrb = ExAllocatePool( NonPagedPool, size );
            
         if ( !pUrb )
         {
            // _fail;
            nts =  STATUS_NO_MEMORY;
            goto func_return;
         }

         RtlZeroMemory( pUrb, size );      

         UsbBuildVendorRequest (
               pUrb,
               UrbFunction,
               (USHORT)size,               //size of this URB
               FlagsWord,
               0,                          //reserved bits
               pRequest->bRequest,         //request
               (USHORT)pRequest->wValue,   //value
               (USHORT)pRequest->wIndex,   //index (zero interface?)
               pData,                      //transfer buffer adr
               NULL,                       //mdl adr
               pRequest->wLength,          //size of transfer buffer
               (struct _URB *)NULL         //URB link adr
              );

         nts = QCUSB_CallUSBD( pDevObj, pUrb );

         if (NT_SUCCESS(nts))
         {
            *pRetSize = pUrb->UrbControlVendorClassRequest.TransferBufferLength;
         } else
         {
            *pRetSize = pUrb->UrbControlVendorClassRequest.TransferBufferLength;
            // *pRetSize = 0;
         }

         break;
      }
      default:
      {
         nts = STATUS_INVALID_PARAMETER;
         goto func_return;
      }
   }

func_return:

   _freeBuf(pUrb);
   _freeBuf(deviceDesc);

   return nts;
}  // QCUSB_PassThrough

/*********************************************************************
 *
 * function:   QCUSB_ResetInput
 *
 * purpose:    Reset the current input pipe
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    NT status
 *
 */
/******************************************************************************

Description:
   This function is invoked by a calling app via IO_CONTROL. A URB pipe request
   URB is built and sent synchronously to the device.
      
******************************************************************************/      
NTSTATUS QCUSB_ResetInput(IN PDEVICE_OBJECT pDevObj, IN QCUSB_RESET_SCOPE Scope)
{
    NTSTATUS status;
    static USHORT mdmCnt = 0;
    USHORT errCnt;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;
    PURB urb;
    ULONG size = sizeof( struct _URB_PIPE_REQUEST );

   
   if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
       (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
   {
      return STATUS_SUCCESS;
   }
   if (!inDevState(DEVICE_STATE_PRESENT))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> USB: dev disconnected-ResetIn\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

    urb = ExAllocatePool( NonPagedPool, size );
    if (urb == NULL)
    {
       return STATUS_NO_MEMORY;
    }
    urb->UrbHeader.Length = (USHORT) size;
    #if defined(QCUSB_VERSION_WXP_FRE) || defined(QCUSB_VERSION_WXP_CHK)
    if (Scope == QCUSB_RESET_HOST_PIPE)
    {
       urb->UrbHeader.Function = URB_FUNCTION_SYNC_RESET_PIPE;
    }
    else if (Scope == QCUSB_RESET_ENDPOINT)
    {
       urb->UrbHeader.Function = URB_FUNCTION_SYNC_CLEAR_STALL;
    }
    else
    #endif
    {
       urb->UrbHeader.Function = URB_FUNCTION_RESET_PIPE;
    }
    urb->UrbPipeRequest.PipeHandle = pDevExt->Interface[pDevExt->DataInterface] 
           ->Pipes[pDevExt->BulkPipeInput].PipeHandle;

    status = QCUSB_CallUSBD( pDevObj, urb ); 
    QCUSB_DbgPrint
    (
       QCUSB_DBG_MASK_CONTROL,
       QCUSB_DBG_LEVEL_FORCE,
       ("<%s> ResetIN (_CallUSBD) - 0x%x\n", pDevExt->PortName, status)
    );
    if (!NT_SUCCESS(status))
    {
       errCnt = ++mdmCnt;
       QCUSB_DbgPrint
       (
          QCUSB_DBG_MASK_READ,
          QCUSB_DBG_LEVEL_ERROR,
          ("<%s> ResetIN(%d) - 0x%x\n", pDevExt->PortName, errCnt, status)
       );
    }
    ExFreePool( urb );
    return status;
}

NTSTATUS QCUSB_ResetInt(IN PDEVICE_OBJECT pDevObj, IN QCUSB_RESET_SCOPE Scope)
{
   ULONG size;
   PURB pUrb;
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS nts = STATUS_SUCCESS;
 
   pDevExt = pDevObj->DeviceExtension;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> USB_ResetInt\n", pDevExt->PortName)
   );

   if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
       (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
   {
      return STATUS_SUCCESS;
   }

   if (pDevExt->InterruptPipe == (UCHAR)-1)
   {
      // if there's no interrupt pipe presented
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _ResetInt: no int pipe\n", pDevExt->PortName)
      );
      return nts;
   }

   if (!inDevState(DEVICE_STATE_PRESENT))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> USB: dev disconnected-ResetInt\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   size = sizeof( struct _URB_PIPE_REQUEST );
   if ((pUrb = ExAllocatePool( NonPagedPool, size )) == NULL)
   {
      nts = STATUS_NO_MEMORY;
   }
   else
   {
      pUrb -> UrbPipeRequest.PipeHandle = pDevExt -> Interface[pDevExt->usCommClassInterface]
         -> Pipes[pDevExt -> InterruptPipe].PipeHandle;
      pUrb -> UrbPipeRequest.Hdr.Length = (USHORT)size;
      #if defined(QCUSB_VERSION_WXP_FRE) || defined(QCUSB_VERSION_WXP_CHK)
      if (Scope == QCUSB_RESET_HOST_PIPE)
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_SYNC_RESET_PIPE;
      }
      else if (Scope == QCUSB_RESET_ENDPOINT)
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_SYNC_CLEAR_STALL;
      }
      else
      #endif
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_RESET_PIPE;
      }
      nts = QCUSB_CallUSBD( pDevObj, pUrb ); 
      ExFreePool( pUrb );
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> ResetInt: 0x%x\n", pDevExt->PortName, nts)
      );
   }

   return nts;
}


/*********************************************************************
 *
 * function:   QCUSB_AbortInterrupt
 *
 * purpose:    abort operations on the current interrupt pipe
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    NT status
 *
 */
NTSTATUS QCUSB_AbortInterrupt( IN PDEVICE_OBJECT pDevObj)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   KIRQL IrqLevel;

   pDevExt = pDevObj->DeviceExtension;
   if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
       (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
   {
      return STATUS_SUCCESS;
   }

   if (pDevExt->InterruptPipe == (UCHAR)-1)
   {
      return ntStatus;
   }
   
   ntStatus = QCUSB_AbortPipe
              (
                 pDevObj,
                 pDevExt->InterruptPipe,
                 pDevExt->usCommClassInterface
              );
   return ntStatus; 
}
/*********************************************************************
 *
 * function:   QCUSB_AbortOutput
 *
 * purpose:    abort operations on the current output pipe
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    NT status
 *
 */
NTSTATUS QCUSB_AbortOutput( IN PDEVICE_OBJECT pDevObj)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   KIRQL IrqLevel;

   pDevExt = pDevObj -> DeviceExtension;

   if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
       (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
   {
      return STATUS_SUCCESS;
   }

   ntStatus = QCUSB_AbortPipe
              (
                 pDevObj,
                 pDevExt->BulkPipeOutput,
                 pDevExt->DataInterface
              );
   return ntStatus; 
}
/*********************************************************************
 *
 * function:   QCUSB_AbortInput
 *
 * purpose:    abort operations on the current input pipe
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    NT status
 *
 */
NTSTATUS QCUSB_AbortInput( IN PDEVICE_OBJECT pDevObj )
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   KIRQL IrqLevel;

   pDevExt = pDevObj -> DeviceExtension;

   if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
       (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
   {
      return STATUS_SUCCESS;
   }

   ntStatus = QCUSB_AbortPipe
              (
                 pDevObj,
                 pDevExt->BulkPipeInput,
                 pDevExt->DataInterface
              );
   return ntStatus; 
}

NTSTATUS QCUSB_AbortPipe
(
   IN PDEVICE_OBJECT pDevObj,
   UCHAR             PipeNum,
   UCHAR             InterfaceNum
)
{
   PDEVICE_EXTENSION pDevExt;
   ULONG size;
   PURB pUrb;
   NTSTATUS nts = STATUS_SUCCESS;
   KIRQL IrqLevel;
   UCHAR EndpointAddress;
 
   pDevExt = pDevObj->DeviceExtension;

   if (!inDevState(DEVICE_STATE_PRESENT))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> USB: dev disconnected-AbortPp\n", pDevExt->PortName)
      );
      nts = STATUS_UNSUCCESSFUL;
      return nts;
   }

   //TODO: have a small function to check this
   if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
       (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
   {
      return STATUS_SUCCESS;
   }

   size = sizeof( struct _URB_PIPE_REQUEST );
   if ((pUrb = ExAllocatePool( NonPagedPool, size )) == NULL)
   {
      nts = STATUS_NO_MEMORY;
   }
   else
   {
      pUrb->UrbPipeRequest.PipeHandle = pDevExt->Interface[InterfaceNum]
         ->Pipes[PipeNum].PipeHandle;
      pUrb -> UrbPipeRequest.Hdr.Length = (USHORT)size;
      pUrb -> UrbHeader.Length = (USHORT)size;
      pUrb -> UrbHeader.Function = URB_FUNCTION_ABORT_PIPE; 
      nts = QCUSB_CallUSBD( pDevObj, pUrb ); 
      ExFreePool( pUrb );
   }
   return nts;
}

/*********************************************************************
 *
 * function:   QCUSB_ResetOutput
 *
 * purpose:    abort operations on the current output pipe
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    NT status
 *
 */
/******************************************************************************

Description:
   This function is invoked by a calling app via IO_CONTROL. A URB pipe request
   URB is built and sent synchronously to the device.
      
******************************************************************************/      

NTSTATUS QCUSB_ResetOutput(IN PDEVICE_OBJECT pDevObj, IN QCUSB_RESET_SCOPE Scope)
{
   ULONG size;
   PURB pUrb;
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS nts;
 
   pDevExt = pDevObj -> DeviceExtension;

   if (!inDevState(DEVICE_STATE_PRESENT))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> USB: dev disconnected-ResetOut\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
       (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
   {
      return STATUS_SUCCESS;
   }

   size = sizeof( struct _URB_PIPE_REQUEST );

   if ((pUrb = ExAllocatePool( NonPagedPool, size )) == NULL)
   {
      nts = STATUS_NO_MEMORY;
   }
   else
    {
      pUrb->UrbPipeRequest.PipeHandle = pDevExt->Interface[pDevExt->DataInterface]
          ->Pipes[pDevExt->BulkPipeOutput].PipeHandle;
      pUrb -> UrbPipeRequest.Hdr.Length = (USHORT)size;
      #if defined(QCUSB_VERSION_WXP_FRE) || defined(QCUSB_VERSION_WXP_CHK)
      if (Scope == QCUSB_RESET_HOST_PIPE)
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_SYNC_RESET_PIPE;
      }
      else if (Scope == QCUSB_RESET_ENDPOINT)
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_SYNC_CLEAR_STALL;
      }
      else
      #endif
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_RESET_PIPE;
      }
      nts = QCUSB_CallUSBD( pDevObj, pUrb ); 
      ExFreePool( pUrb );
   }
   return nts;
}

/*****************************************************************************
 *
 * function:   QCUSB_GetStatus
 *
 * purpose:    get status from device
 *
 * arguments:  pDevObj   = adr(device object)
 *             ioBuffer = adr(data return buffer) 
 *
 * returns:    NT status
 *
 */
/******************************************************************************
Description:
   This function is invoked by a calling app via IO_CONTROL. A Get status
   URB is built and sent synchronously to the device, and the results are
   returned to the buffer adr passed in by the (ring 3) caller.
      
******************************************************************************/      

NTSTATUS QCUSB_GetStatus( IN PDEVICE_OBJECT pDevObj, PUCHAR ioBuff )

{
   ULONG size, *pUl;
   PURB pUrb;
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS nts;
 
   pUl = (PULONG)ioBuff;
   *pUl = 0L;      //clear the buffer
   pDevExt = pDevObj -> DeviceExtension;

   if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
       (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
   {
      return STATUS_SUCCESS;
   }
   
   size = sizeof( struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST );
   if ((pUrb = ExAllocatePool( NonPagedPool, size )) == NULL)
      nts = STATUS_NO_MEMORY;
   else
    {
      UsbBuildVendorRequest (
               pUrb,
               URB_FUNCTION_CLASS_INTERFACE,
               (USHORT)size,      //size of this URB
               USBD_TRANSFER_DIRECTION_IN|USBD_SHORT_TRANSFER_OK,
               0,            //reserved bits
               GET_PORT_STATUS,      //request
               0,            //value
               0,            //index (zero interface?)
               ioBuff,         //transfer buffer adr
               NULL,         //mdl adr
               sizeof( ULONG ),      //size of transfer buffer
               NULL         //URB link adr
               )
      nts = QCUSB_CallUSBD( pDevObj, pUrb ); 
      ExFreePool( pUrb );
   }

   return nts;
}

NTSTATUS QCUSB_CallUSBD_Completion
(
   PDEVICE_OBJECT dummy,
   PIRP pIrp,
   PVOID pEvent
)
{
   KeSetEvent((PKEVENT)pEvent, IO_NO_INCREMENT, FALSE);
   return STATUS_MORE_PROCESSING_REQUIRED;
}

/*****************************************************************************
 *
 * function:   QCUSB_CallUSBD
 *
 * purpose:    Passes a URB to the USBD class driver
 *
 * arguments:  DeviceObject = adr(device object)
 *             Urb       = adr(USB Request Block)
 *
 * returns:    NT status
 *
 */
NTSTATUS QCUSB_CallUSBD
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PURB Urb
)
{
   NTSTATUS ntStatus, ntStatus2;
   PDEVICE_EXTENSION pDevExt;
   PIRP irp;
   KEVENT event;
   IO_STATUS_BLOCK ioStatus;
   PIO_STACK_LOCATION nextStack;
   LARGE_INTEGER delayValue;
   KIRQL IrqLevel;

   // the device should respond within 50ms over the control pipe
   // so it's good enough to use 1.5 second here

   pDevExt = DeviceObject->DeviceExtension;

   IrqLevel = KeGetCurrentIrql();

   if (IrqLevel > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CallUSBD: _CANCELLED(IRQL %d)\n", pDevExt->PortName, IrqLevel)
      );
      Urb->UrbHeader.Status = USBD_STATUS_CANCELED;
      return STATUS_CANCELLED;
   }

   if (!inDevState(DEVICE_STATE_PRESENT))
   {
      return STATUS_DELETE_PENDING;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> CallUSBD: PWR S(%d/%d), D(%d/%d)\n", pDevExt->PortName,
        pDevExt->SystemPower, PowerSystemSleeping3,
        pDevExt->DevicePower, PowerDeviceD2)
   );

   if ((pDevExt->bDeviceSurpriseRemoved == TRUE) ||
       (pDevExt->bDeviceRemoved == TRUE))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CallUSBD: STATUS_DELETE_PENDING\n", pDevExt->PortName)
      );
      return STATUS_DELETE_PENDING;
   }

   if ((pDevExt->SystemPower >= PowerSystemSleeping3) ||
       (pDevExt->DevicePower >  PowerDeviceD2))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CallUSBD: pwr too low, _POWER_STATE_INVALID\n", pDevExt->PortName)
      );
      return STATUS_POWER_STATE_INVALID;
   }

   // issue a synchronous request to read the UTB 
   KeInitializeEvent(&event, NotificationEvent, FALSE);

   irp = IoBuildDeviceIoControlRequest
         (
            IOCTL_INTERNAL_USB_SUBMIT_URB,
            pDevExt -> StackDeviceObject,
            NULL,
            0,
            NULL,
            0,
            TRUE, /* INTERNAL */
            &event,
            &ioStatus
         );

   if (!irp)
   {
      return STATUS_INSUFFICIENT_RESOURCES;
   }

   // Call the class driver to perform the operation.  If the returned status
   // is PENDING, wait for the request to complete.

   nextStack = IoGetNextIrpStackLocation( irp );
   nextStack->Parameters.Others.Argument1 = Urb;

   if (pDevExt->NoTimeoutOnCtlReq == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> CallUSBD: 0-sending IRP 0x%p\n", pDevExt->PortName, irp)
      );
      ntStatus = IoCallDriver( pDevExt -> StackDeviceObject, irp );

      if (ntStatus == STATUS_PENDING)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &event,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
      }

      // USBD maps the error code for us
      // ntStatus = URB_STATUS(Urb);
      ioStatus.Status = ntStatus;
      goto ExitCallUSBD;
   }

   // set completion routine to regain control of the irp
   IoSetCompletionRoutine
   (
      irp, QCUSB_CallUSBD_Completion, (PVOID)&event,
      TRUE, TRUE, TRUE
   );

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> CallUSBD: 1-sending IRP 0x%p\n", pDevExt->PortName, irp)
   );
   ntStatus = IoCallDriver( pDevExt -> StackDeviceObject, irp );
   if (ntStatus == STATUS_PENDING) 
   {
      delayValue.QuadPart = -(15 * 1000 * 1000);   // 1.0 sec  
      ntStatus = KeWaitForSingleObject
                 (
                    &event,
                    Executive,
                    KernelMode,
                    FALSE,
                    &delayValue
                 );


      if (ntStatus == STATUS_TIMEOUT) 
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> timeout on control req - 1 Urb 0x%p IRP 0x%p\n", pDevExt->PortName, Urb, irp)
         );
         IoCancelIrp(irp);  // cancel the irp
         KeWaitForSingleObject
         (
            &event,
            Executive,
            KernelMode,
            FALSE,
            NULL
         );

         Urb->UrbHeader.Status = USBD_STATUS_DEV_NOT_RESPONDING;
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> CallUSBD - returned IRP 0x%p (ST 0x%x) URB 0x%p(ST 0x%x)\n",
              pDevExt->PortName, irp, irp->IoStatus.Status, Urb, Urb->UrbHeader.Status)
         );
      }
   }
   else  // success or failure
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CallUSBD - IoCallDriver/0x%p 0x%x IRP 0x%x URB 0x%p(0x%x)\n",
           pDevExt->PortName, pDevExt->StackDeviceObject, ntStatus,
           irp->IoStatus.Status, Urb, Urb->UrbHeader.Status)
      );
      delayValue.QuadPart = -(30 * 1000 * 1000);   // 3.0 sec  
      ntStatus2 = KeWaitForSingleObject
                 (
                    &event,
                    Executive,
                    KernelMode,
                    FALSE,
                    &delayValue
                 );
      if (ntStatus2 == STATUS_TIMEOUT)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> CallUSBD ERR: 2nd wait timeout, URB 0x%p\n", pDevExt->PortName, Urb)
         );
         #ifdef DEBUG_MSGS
         KdBreakPoint();
         #endif // DEBUG_MSGS
      }
   }

   if ((Urb->UrbHeader.Status == USBD_STATUS_DEVICE_GONE) ||
       (Urb->UrbHeader.Status == USBD_STATUS_XACT_ERROR)  ||
       (irp->IoStatus.Status  == STATUS_NO_SUCH_DEVICE))
   {
      pDevExt->ControlPipeStatus = STATUS_NO_SUCH_DEVICE;
   }
   else
   {
      pDevExt->ControlPipeStatus = ntStatus;
   }

   IoCompleteRequest(irp, IO_NO_INCREMENT);

ExitCallUSBD:

   if (ntStatus == STATUS_TIMEOUT)
   {
      ntStatus = STATUS_UNSUCCESSFUL;
   }

   return ntStatus;

}  // QCUSB_CallUSBD


/*****************************************************************************
 *
 * function:   QCUSB_CallUSBDIrp
 *
 * purpose:    Passes a IRP to the USBD class driver/Filter Driver
 *
 * arguments:  DeviceObject = adr(device object)
 *             Irp       = adr(USB Request Block)
 *
 * returns:    NT status
 *
 */
NTSTATUS QCUSB_CallUSBDIrp
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP Irp
)
{
   NTSTATUS ntStatus, ntStatus2;
   PDEVICE_EXTENSION pDevExt;
   PIRP irp;
   KEVENT event;
   IO_STATUS_BLOCK ioStatus;
   PIO_STACK_LOCATION nextStack;
   LARGE_INTEGER delayValue;
   KIRQL IrqLevel;

   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
   PVOID ioBuffer = Irp->AssociatedIrp.SystemBuffer;
   ULONG Length = irpStack->Parameters.DeviceIoControl.InputBufferLength;


   pDevExt = DeviceObject->DeviceExtension;

   IrqLevel = KeGetCurrentIrql();

   if (IrqLevel > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CallUSBD: _CANCELLED(IRQL %d)\n", pDevExt->PortName, IrqLevel)
      );
      return STATUS_CANCELLED;
   }

#if 0
   if (!inDevState(DEVICE_STATE_PRESENT))
   {
      return STATUS_DELETE_PENDING;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> CallUSBD: PWR S(%d/%d), D(%d/%d)\n", pDevExt->PortName,
        pDevExt->SystemPower, PowerSystemSleeping3,
        pDevExt->DevicePower, PowerDeviceD2)
   );

   if ((pDevExt->bDeviceSurpriseRemoved == TRUE) ||
       (pDevExt->bDeviceRemoved == TRUE))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CallUSBD: STATUS_DELETE_PENDING\n", pDevExt->PortName)
      );
      return STATUS_DELETE_PENDING;
   }

   if ((pDevExt->SystemPower >= PowerSystemSleeping3) ||
       (pDevExt->DevicePower >  PowerDeviceD2))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CallUSBD: pwr too low, _POWER_STATE_INVALID\n", pDevExt->PortName)
      );
      return STATUS_POWER_STATE_INVALID;
   }
#endif

   // issue a synchronous request to read the UTB 
   KeInitializeEvent(&event, NotificationEvent, FALSE);

   irp = IoBuildDeviceIoControlRequest
         (
            irpStack->Parameters.DeviceIoControl.IoControlCode,
            pDevExt -> StackDeviceObject,
            Irp->AssociatedIrp.SystemBuffer,
            irpStack->Parameters.DeviceIoControl.InputBufferLength,
            Irp->AssociatedIrp.SystemBuffer,
            irpStack->Parameters.DeviceIoControl.InputBufferLength,
            //TRUE, /* INTERNAL */
            FALSE, /* EXTERNAL */
            &event,
            &ioStatus
         );

   if (!irp)
   {
      return STATUS_INSUFFICIENT_RESOURCES;
   }

   ntStatus = IoCallDriver( pDevExt -> StackDeviceObject, irp );
   
   if (ntStatus == STATUS_PENDING)
   {
      delayValue.QuadPart = -(50 * 1000 * 1000);   // 5.0 sec  
      ntStatus = KeWaitForSingleObject
                 (
                    &event,
                    Executive,
                    KernelMode,
                    FALSE,
                    &delayValue
                 );
      if (ntStatus == STATUS_TIMEOUT) 
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> timeout on filter command req\n", pDevExt->PortName)
         );
         IoCancelIrp(irp);  // cancel the irp
         KeWaitForSingleObject
         (
            &event,
            Executive,
            KernelMode,
            FALSE,
            NULL
         );
         ntStatus = STATUS_CANCELLED;
      }    
      else
      {
         ntStatus = ioStatus.Status;
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> QCUSB_CallUSBDIrp return status 0x%x length %d response 0x%p\n", pDevExt->PortName, ntStatus, ioStatus.Information, 
       Irp->AssociatedIrp.SystemBuffer)
   );

   Irp->IoStatus.Information = ioStatus.Information;
   return ntStatus;
   
}  // QCUSB_CallUSBDIrp



NTSTATUS QCUSB_SetRemoteWakeup(IN PDEVICE_OBJECT pDevObj)
{
    NTSTATUS status;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;
    PURB urb;
    ULONG size = sizeof( struct _URB_CONTROL_FEATURE_REQUEST );

    if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
        (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
    {
       return STATUS_SUCCESS;
    }

    urb = ExAllocatePool( NonPagedPool, size );
    if (urb == NULL)
    {
       return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(urb, size);
    urb->UrbHeader.Length = (USHORT) size;
    urb->UrbHeader.Function = URB_FUNCTION_SET_FEATURE_TO_DEVICE;
    urb->UrbControlFeatureRequest.UrbLink = NULL;
    urb->UrbControlFeatureRequest.FeatureSelector = 1; // 1: remote wakeup; 0: endpoint halt; 2: test mode
    urb->UrbControlFeatureRequest.Index = 0;

    status = QCUSB_CallUSBD( pDevObj, urb );
    if (!NT_SUCCESS(status))
    {
       QCUSB_DbgPrint
       (
          QCUSB_DBG_MASK_CONTROL,
          QCUSB_DBG_LEVEL_ERROR,
          ("<%s> SetRemoteWakeup - Err 0x%x\n", pDevExt->PortName, status)
       );
       status = STATUS_UNSUCCESSFUL;
    }
    ExFreePool( urb );

    return status;
}  // QCUSB_SetRemoteWakeup

NTSTATUS QCUSB_ClearRemoteWakeup(IN PDEVICE_OBJECT pDevObj)
{
    NTSTATUS status;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;
    PURB urb;
    ULONG size = sizeof( struct _URB_CONTROL_FEATURE_REQUEST );

    if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
        (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
    {
       return STATUS_SUCCESS;
    }

    QCUSB_DbgPrint
    (
       QCUSB_DBG_MASK_CONTROL,
       QCUSB_DBG_LEVEL_DETAIL,
       ("<%s> -->_ClearRemoteWakeup: FuncIF %d\n", pDevExt->PortName, pDevExt->usCommClassInterface)
    );

    urb = ExAllocatePool( NonPagedPool, size );
    if (urb == NULL)
    {
       QCUSB_DbgPrint
       (
          QCUSB_DBG_MASK_CONTROL,
          QCUSB_DBG_LEVEL_ERROR,
          ("<%s> <--_ClearRemoteWakeup: NO_MEM\n", pDevExt->PortName)
       );
       return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(urb, size);
    urb->UrbHeader.Length = (USHORT) size;
    // if (pDevExt->wMaxPktSize >= QC_SS_BLK_PKT_SZ)  // SS device
    // {
    //    UCHAR ifNum = (UCHAR)pDevExt->usCommClassInterface;
    //    urb->UrbHeader.Function = URB_FUNCTION_SET_FEATURE_TO_INTERFACE;
    //    urb->UrbControlFeatureRequest.UrbLink = NULL;
    //    urb->UrbControlFeatureRequest.FeatureSelector = 0;  // function suspend  -- wValue
    //    urb->UrbControlFeatureRequest.Index = (0x0000 | (ifNum << 8));  // TODO - verify if ifNum needs shift
    // }
    // else
    {
       urb->UrbHeader.Function = URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE;
       urb->UrbControlFeatureRequest.UrbLink = NULL;
       urb->UrbControlFeatureRequest.FeatureSelector = 1; // 1: remote wakeup; 0: endpoint halt; 2: test mode
       urb->UrbControlFeatureRequest.Index = 0;
    }

    status = QCUSB_CallUSBD( pDevObj, urb );
    if (!NT_SUCCESS(status))
    {
       QCUSB_DbgPrint
       (
          QCUSB_DBG_MASK_CONTROL,
          QCUSB_DBG_LEVEL_ERROR,
          ("<%s> ClearRemoteWakeup - Err 0x%x\n", pDevExt->PortName, status)
       );
    }
    ExFreePool( urb );

    QCUSB_DbgPrint
    (
       QCUSB_DBG_MASK_CONTROL,
       QCUSB_DBG_LEVEL_DETAIL,
       ("<%s> <--_ClearRemoteWakeup: FuncIF %d\n", pDevExt->PortName, pDevExt->usCommClassInterface)
    );

    return status;
}  // QCUSB_ClearRemoteWakeup

VOID QCUSB_DebugUrb(PIRP Irp)
{
   PIO_STACK_LOCATION nextStack = IoGetNextIrpStackLocation(Irp);
   PURB pUrb = nextStack->Parameters.Others.Argument1;
   char outBuffer[512];

   if (pUrb == NULL)
   {
      QCUSB_DbgPrintG
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> QCUSB: Error -- NULL URB\n", gDeviceName)
      );
      return;
   }

   RtlZeroMemory(outBuffer, 512);
   sprintf(outBuffer, "   --- URB Contents ---\n");
   sprintf(outBuffer+strlen(outBuffer), "Header Length: %d\n", pUrb->UrbHeader.Length);
   sprintf(outBuffer+strlen(outBuffer), "Header Function: 0x%x\n", pUrb->UrbHeader.Function);
   sprintf(outBuffer+strlen(outBuffer), "Reserved: 0x%p\n", pUrb->UrbControlVendorClassRequest.Reserved);
   sprintf(outBuffer+strlen(outBuffer), "Flags: 0x%x\n", pUrb->UrbControlVendorClassRequest.TransferFlags);
   sprintf(outBuffer+strlen(outBuffer), "BufLen: %d\n", pUrb->UrbControlVendorClassRequest.TransferBufferLength);
   sprintf(outBuffer+strlen(outBuffer), "Buf: 0x%p\n", pUrb->UrbControlVendorClassRequest.TransferBuffer);
   sprintf(outBuffer+strlen(outBuffer), "MDL: 0x%p\n", pUrb->UrbControlVendorClassRequest.TransferBufferMDL);
   sprintf(outBuffer+strlen(outBuffer), "UrbLink: 0x%p\n", pUrb->UrbControlVendorClassRequest.UrbLink);
   sprintf(outBuffer+strlen(outBuffer), "ReqReserved: 0x%x\n", pUrb->UrbControlVendorClassRequest.RequestTypeReservedBits);
   sprintf(outBuffer+strlen(outBuffer), "Request: 0x%x\n", pUrb->UrbControlVendorClassRequest.Request);
   sprintf(outBuffer+strlen(outBuffer), "Value: 0x%x\n", pUrb->UrbControlVendorClassRequest.Value);
   sprintf(outBuffer+strlen(outBuffer), "Index: 0x%x\n", pUrb->UrbControlVendorClassRequest.Index);
   sprintf(outBuffer+strlen(outBuffer), "Reserved1: 0x%x\n", pUrb->UrbControlVendorClassRequest.Reserved1);

   QCUSB_DbgPrintG
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_ERROR,
      ("<%s> %s\n", gDeviceName, outBuffer)
   );

}  // QCUSB_DebugUrb

NTSTATUS USBCTL_ClrDtrRts(PDEVICE_OBJECT DeviceObject)
{
   USB_DEFAULT_PIPE_REQUEST Request;
   PUSB_DEFAULT_PIPE_REQUEST pRequest = &Request;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   NTSTATUS nts;

   if (pDevExt->IsEcmModel == TRUE)
   {
      // Need to disable interface
      return STATUS_SUCCESS;
   }

   if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
       (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
   {
      return STATUS_SUCCESS;
   }

   Request.bmRequestType = CLASS_HOST_TO_INTERFACE;
   Request.bRequest = CDC_SET_CONTROL_LINE_STATE;
   Request.wValue = 0;
   Request.wIndex = pDevExt->DataInterface; // usCommClassInterface;
   Request.wLength = 0;

   nts = QCUSB_PassThrough(DeviceObject, pRequest, 0, &ulRetSize);
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> ClrDtrRts: returned 0x%x\n", pDevExt->PortName, nts)
   );

   return nts;
}  // USBCTL_ClrDtrRts

NTSTATUS USBCTL_SetDtr(PDEVICE_OBJECT DeviceObject)
{
   USB_DEFAULT_PIPE_REQUEST Request;
   PUSB_DEFAULT_PIPE_REQUEST pRequest = &Request;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;

   if (pDevExt->IsEcmModel == TRUE)
   {
      // Need to enable interface
      return STATUS_SUCCESS;
   }

   if ((pDevExt->MuxInterface.MuxEnabled == 1) &&
       (pDevExt->MuxInterface.InterfaceNumber != pDevExt->MuxInterface.PhysicalInterfaceNumber))
   {
      return STATUS_SUCCESS;
   }
   Request.bmRequestType = CLASS_HOST_TO_INTERFACE;
   Request.bRequest = CDC_SET_CONTROL_LINE_STATE;
   Request.wValue = CDC_CONTROL_LINE_DTR;
   Request.wIndex = pDevExt->DataInterface; // usCommClassInterface;
   Request.wLength = 0;

   return QCUSB_PassThrough(DeviceObject, pRequest, 0, &ulRetSize);
} // USBCTL_SetDtr

NTSTATUS USBCTL_RequestDeviceID(PDEVICE_OBJECT pDevObj, USHORT Interface)
{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt = pDevObj->DeviceExtension;
   UCHAR pBuf[sizeof(USB_DEFAULT_PIPE_REQUEST)+32];
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   ULONG ulRetSize = 0;
   BOOLEAN bDeviceResourceSharing = FALSE;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_INFO,
      ("<%s> RequestDeviceID: IF 0x%02x\n", pDevExt->PortName, Interface)
   );

   pDevExt->bEnableResourceSharing = FALSE;

   #ifdef QCUSB_SHARE_INTERRUPT

   if ((pDevExt->bVendorFeature == TRUE)           && // vs-guid
       (pDevExt->DeviceControlCapabilities & USB_CTRL_CAPABILITY_VENDOR_CMD) &&
       (pDevExt->DeviceControlCapabilities & USB_CTRL_CAPABILITY_ENCAPSULATED_CMD) &&
       (pDevExt->DeviceDataCapabilities    & USB_DATA_CAPABILITY_RSRC_SHARING))
   {
      bDeviceResourceSharing = TRUE;
   }

   if (bDeviceResourceSharing == FALSE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_INFO,
         ("<%s> RequestDeviceID: feature not available in device.\n", pDevExt->PortName)
      );
      return STATUS_SUCCESS;
   }

   RtlZeroMemory(pBuf, sizeof(USB_DEFAULT_PIPE_REQUEST)+32);

   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;

   // bmRequest:
   //    STANDARD_HOST_TO_DEVICE
   //    STANDARD_DEVICE_TO_HOST
   //    VENDOR_HOST_TO_DEVICE
   //    CLASS_HOST_TO_INTERFACE
   // bRequest:
   //    CDC_SEND_ENCAPSULATED_CMD
   //    USB_REQUEST_GET_CONFIGURATION
   //    USB_REQUEST_SET_CONFIGURATION
   // wValue:
   //    QCUSB_REQUEST_DEVICE_ID
   // wIndex:
   // wLength:
   // Data:

   pRequest->bmRequestType = VENDOR_DEVICE_TO_HOST;
   pRequest->bRequest      = QCUSB_REQUEST_DEVICE_ID;
   pRequest->wIndex        = Interface;
   pRequest->wValue        = 0;
   pRequest->wLength       = 16;

   nts = QCUSB_PassThrough ( pDevObj, pBuf, 32, &ulRetSize );

   // The returned data should be in pRequest->Data[0] , [1], [2], ...

   if (nts == STATUS_SUCCESS)
   {
      if ((ulRetSize > 0) && (ulRetSize <= 16))
      {
         PDeviceReadControlEvent rdCtlElement = NULL;

#ifdef QCUSB_SHARE_INTERRUPT_OLD
         // Copy the device ID into device extension
         RtlCopyMemory(pDevExt->DeviceId, pRequest->Data, ulRetSize);
#endif
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_INFO,
            ("<%s> RequestDeviceID: got %uB id\n", pDevExt->PortName, ulRetSize)
         );

         rdCtlElement = USBSHR_AddReadControlElement(pDevExt->DeviceId);
         if (rdCtlElement == NULL)
         {
            nts = STATUS_UNSUCCESSFUL;
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_INFO,
               ("<%s> RequestDeviceID: failed to get id element\n", pDevExt->PortName)
            );
         }
         else
         {
            USHORT interface = pDevExt->DataInterface;
            pDevExt->pReadControlEvent  = &(rdCtlElement->ReadControlEvent[interface]);
            pDevExt->pRspAvailableCount = &(rdCtlElement->RspAvailableCount[interface]);
            pDevExt->bEnableResourceSharing = TRUE;
            pDevExt->pDispatchEvents[DSP_READ_CONTROL_EVENT_INDEX] = pDevExt->pReadControlEvent;
         }
      }
      else
      {
         nts = STATUS_UNSUCCESSFUL;

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_INFO,
            ("<%s> RequestDeviceID: failed to get id (%uB)\n", pDevExt->PortName, ulRetSize)
         );
      }
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_INFO,
         ("<%s> RequestDeviceID: failed to get id (0x%x)\n", pDevExt->PortName, nts)
      );
   }

   #endif // QCUSB_SHARE_INTERRUPT

   return nts;
}  // USBCTL_RequestDeviceID

NTSTATUS USBCTL_EnableDataBundling
(
   PDEVICE_OBJECT pDevObj,
   USHORT         Interface,
   QC_LINK        Link
)
{
   NTSTATUS          nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt = pDevObj->DeviceExtension;
   UCHAR             pBuf[sizeof(USB_DEFAULT_PIPE_REQUEST)+32];
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   ULONG             ulRetSize = 0;
   BOOLEAN           bBundlingCapable[QC_LINK_MAX] = { FALSE, FALSE };

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_INFO,
      ("<%s> EnableDataBundling: IF 0x%02x Link 0x%x\n", pDevExt->PortName,
        Interface, Link)
   );

   if ((Link > QC_LINK_MAX) || (Link < 0))
   {
      return STATUS_UNSUCCESSFUL;
   }

   pDevExt->bEnableDataBundling[Link] = FALSE;
   if (Link == QC_LINK_DL)
   {
      bBundlingCapable[Link] = ((pDevExt->DeviceDataCapabilities & USB_DATA_CAPABILITY_DL_PKTS_BUNDLING) != 0);
   }
   else
   {
      bBundlingCapable[Link] = ((pDevExt->DeviceDataCapabilities & USB_DATA_CAPABILITY_UL_PKTS_BUNDLING) != 0);
   }

   if ((pDevExt->bVendorFeature == TRUE)           && // vs-guid
       (pDevExt->DeviceControlCapabilities & USB_CTRL_CAPABILITY_VENDOR_CMD) &&
       (bBundlingCapable[Link] == TRUE))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_INFO,
         ("<%s> EnableDataBundling: feature supported - 0x%x/0x%x\n", pDevExt->PortName,
           pDevExt->DeviceControlCapabilities, pDevExt->DeviceDataCapabilities)
      );
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_INFO,
         ("<%s> EnableDataBundling: feature not available in device - 0x%x/0x%x\n", pDevExt->PortName,
           pDevExt->DeviceControlCapabilities, pDevExt->DeviceDataCapabilities)
      );
      return STATUS_SUCCESS;
   }

   RtlZeroMemory(pBuf, sizeof(USB_DEFAULT_PIPE_REQUEST)+32);

   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;

   // bmRequest:
   //    STANDARD_HOST_TO_DEVICE
   //    STANDARD_DEVICE_TO_HOST
   //    VENDOR_HOST_TO_DEVICE
   //    CLASS_HOST_TO_INTERFACE
   // bRequest:
   //    CDC_SEND_ENCAPSULATED_CMD
   //    USB_REQUEST_GET_CONFIGURATION
   //    USB_REQUEST_SET_CONFIGURATION
   // wValue:
   //    QCUSB_ENABLE_DL_BUNDLING
   //    QCUSB_ENABLE_UL_BUNDLING
   // wIndex:
   // wLength:
   // Data:

   pRequest->bmRequestType = VENDOR_DEVICE_TO_HOST;
   if (Link == QC_LINK_DL)
   {
      pRequest->bRequest = QCUSB_ENABLE_DL_BUNDLING;
   }
   else
   {
      pRequest->bRequest = QCUSB_ENABLE_UL_BUNDLING;
   }
   pRequest->wIndex        = Interface;
   pRequest->wValue        = (UCHAR)HOST_DATA_BUNDLING_VERSION;
   pRequest->wLength       = 1; // expect 1-byte supported version from device

   nts = QCUSB_PassThrough(pDevObj, pBuf, 32, &ulRetSize);

   // The returned data should be in pRequest->Data[0] , [1], [2], ...

   if (nts == STATUS_SUCCESS)
   {
      if (ulRetSize == 1)
      {
         UCHAR deviceSupportedVersion = pRequest->Data[0];

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_INFO,
            ("<%s> EnableDataBundling: got ver %d\n", pDevExt->PortName,
              deviceSupportedVersion)
         );

         if (deviceSupportedVersion > HOST_DATA_BUNDLING_VERSION)
         {
            nts = STATUS_UNSUCCESSFUL;
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_INFO,
               ("<%s> EnableDataBundling: dev ver not supported!\n", pDevExt->PortName)
            );
         }
         else
         {
            pDevExt->bEnableDataBundling[Link] = TRUE;
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_INFO,
               ("<%s> EnableDataBundling: link %d OK\n", pDevExt->PortName, Link)
            );
         }
      }
      else
      {
         nts = STATUS_UNSUCCESSFUL;

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_INFO,
            ("<%s> EnableDataBundling: failed to get ver\n", pDevExt->PortName)
         );
      }
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_INFO,
         ("<%s> EnableDataBundling: failed to get ver 0x%x\n", pDevExt->PortName, nts)
      );
   }

   return nts;
}  // USBCTL_EnableDataBundling

NTSTATUS QCUSB_CDC_SetInterfaceIdle
(
   PDEVICE_OBJECT DeviceObject,
   USHORT         Interface,
   BOOLEAN        IdleState,
   UCHAR          Cookie
)
{
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   NTSTATUS nts = STATUS_SUCCESS;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt;
   ULONG Length = sizeof(USHORT);
   PVOID pBuf;
   ULONG bufSize;
   PUSHORT pCommStatus;

   pDevExt = DeviceObject->DeviceExtension;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->CDC_SetInterfaceIdle<%d>: IF %u Idle %d\n",
        pDevExt->PortName, Cookie, Interface, IdleState)
   );

   return nts;
   
   if (pDevExt->IsEcmModel == TRUE)
   {
      URB    urb;
      UCHAR  alternateSetting = 0;
      USHORT size;

      if ((!inDevState(DEVICE_STATE_PRESENT)) ||
          (pDevExt->ConfigurationHandle == NULL))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> <--CDC_SetInterfaceIdle: err-wrong dev state, 0x%p\n",
              pDevExt->PortName, pDevExt->ConfigurationHandle)
         );
         return STATUS_UNSUCCESSFUL;
      }

      if (IdleState == TRUE)
      {
         size = GET_SELECT_INTERFACE_REQUEST_SIZE(0);
         alternateSetting = 0;
      }
      else
      {
         size = GET_SELECT_INTERFACE_REQUEST_SIZE(2);
         alternateSetting = 1;
      }

      UsbBuildSelectInterfaceRequest
      (
         &urb,
         size,
         pDevExt->ConfigurationHandle,
         pDevExt->DataInterface,
         alternateSetting
      );

//    nts = QCUSB_CallUSBD(DeviceObject, &urb);
      if (!NT_SUCCESS(nts))
      {
         // QCUSB_DbgPrint
         // (
         //    QCUSB_DBG_MASK_CONTROL,
         //    QCUSB_DBG_LEVEL_ERROR,
         //    ("<%s> CDC_SetInterfaceIdle: err-CallUSBD 0x%x, IF %u alt %u Flags 0x%x\n",
         //      pDevExt->PortName, nts, pDevExt->DataInterface, alternateSetting, urb.UrbHeader.UsbdFlags)
         // );
         // KdPrint(("<%s> original flags: 0x%x\n", pDevExt->PortName, pDevExt->UsbConfigUrb->UrbHeader.UsbdFlags));
         // KdPrint(("<%s> NumPipes %u\n", pDevExt->PortName,
         //          urb.UrbSelectInterface.Interface.NumberOfPipes));
      }

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_TRACE,
         ("<%s> <--CDC_SetInterfaceIdle: ECM - IF %u alt %u (0x%x)\n",
           pDevExt->PortName, pDevExt->DataInterface, alternateSetting, nts)
      );
      return nts;
   }

   if (pDevExt->SetCommFeatureSupported == FALSE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_TRACE,
         ("<%s> <--CDC_SetInterfaceIdle: no dev support\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   bufSize = sizeof(USB_DEFAULT_PIPE_REQUEST)+Length;
   pBuf = ExAllocatePool(NonPagedPool, bufSize);
   if (pBuf == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> <--CDC_SetInterfaceIdle: NO_MEM\n", pDevExt->PortName)
      );
      return STATUS_NO_MEMORY;
   }
   RtlZeroMemory(pBuf, bufSize);

   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;
   pRequest->bmRequestType = CLASS_HOST_TO_INTERFACE;
   pRequest->bRequest = CDC_SET_COMM_FEATURE;
   pRequest->wIndex = Interface;
   pRequest->wValue = CDC_ABSTRACT_STATE;
   pRequest->wLength = Length;
   pCommStatus = (PUSHORT)pRequest->Data;
   *pCommStatus = (IdleState == TRUE);

   nts = QCUSB_PassThrough
         (
            DeviceObject, pBuf, bufSize, &ulRetSize
         );

   ExFreePool(pBuf);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> <--CDC_SetInterfaceIdle<%d>: IF %u Idle %d ST 0x%x\n",
        pDevExt->PortName, Cookie, Interface, IdleState, nts)
   );

   return nts;
}  // QCUSB_CDC_SetInterfaceIdle
