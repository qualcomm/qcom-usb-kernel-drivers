/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B W T . C

GENERAL DESCRIPTION
  This is the file which contains TX functions for QDSS driver.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "QDBMAIN.h"
#include "QDBWT.h"

#ifdef EVENT_TRACING
#include "QDBWT.tmh"
#endif

// USB write not supported in phase 1
VOID QDBWT_IoWrite
(
   WDFQUEUE   Queue,
   WDFREQUEST Request,
   size_t     Length
)
{
   PDEVICE_CONTEXT pDevContext;
   PFILE_CONTEXT   fileContext = NULL;

   pDevContext = QdbDeviceGetContext(WdfIoQueueGetDevice(Queue));

   QDB_DbgPrint
   (
      QDB_DBG_MASK_WRITE,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> -->QDBWT_IoWrite: 0x%p\n", pDevContext->PortName, Request)
   );

   fileContext = QdbFileGetContext(WdfRequestGetFileObject(Request));

   switch (fileContext->Type)
   {
      case QDB_FILE_TYPE_TRACE:
      case QDB_FILE_TYPE_DPL:
      {
         QDB_DbgPrint
         (
             QDB_DBG_MASK_WRITE,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBWT_IoWrite: no USB pipe for TRACE/DPL write\n", pDevContext->PortName)
         );
         WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
         return;
      }

      case QDB_FILE_TYPE_DEBUG:
      {
         if (fileContext->DebugOUT == NULL)
         {
            QDB_DbgPrint
            (
               QDB_DBG_MASK_WRITE,
               QDB_DBG_LEVEL_ERROR,
               ("<%s> QDBWT_IoWrite: write pipes not available\n", pDevContext->PortName)
            );
            WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
            return;
         }
         break;
      }
      default:
      {
         QDB_DbgPrint
         (
            QDB_DBG_MASK_WRITE,
            QDB_DBG_LEVEL_ERROR,
            ("<%s> QDBWT_IoWrite: unknown file type\n", pDevContext->PortName)
         );
         WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
         return;
      }
   }

   if (Length == 0)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_WRITE,
         QDB_DBG_LEVEL_DETAIL,
         ("<%s> QDBWT_IoWrite: req length 0\n", pDevContext->PortName)
      );
      WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
      return;
   }

   QDBWT_WriteUSB(Queue, Request, (ULONG)Length);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_WRITE,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--QDBWT_IoWrite: failed 0x%p\n", pDevContext->PortName, Request)
   );

   return;
}  // QDBWT_IoWrite

VOID QDBWT_WriteUSB
(
   IN WDFQUEUE         Queue,
   IN WDFREQUEST       Request,
   IN ULONG            Length
)
{
   NTSTATUS         ntStatus;
   PDEVICE_CONTEXT  pDevContext;
   PFILE_CONTEXT    fileContext = NULL;
   PREQUEST_CONTEXT txContext = NULL;
   WDFMEMORY        hTxBufferObj;
   WDFUSBPIPE       pipeOUT;
   WDFIOTARGET      ioTarget;
   BOOLEAN          bResult;

   pDevContext = QdbDeviceGetContext(WdfIoQueueGetDevice(Queue));

   QDB_DbgPrint
   (
      QDB_DBG_MASK_WRITE,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> -->QDBWT_WriteUSB: 0x%p (%dB)\n", pDevContext->PortName, Request, Length)
   );

   if (Length > QDB_USB_TRANSFER_SIZE_MAX)
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_WRITE,
         QDB_DBG_LEVEL_ERROR,
         ("<%s> QDBWT_WriteUSB: discard oversized TX (%uB)\n", pDevContext->PortName, Length)
      );
      WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
      return;
   }

   txContext = QdbRequestGetContext(Request);
   txContext->Index = InterlockedIncrement(&(pDevContext->TxCount));
   fileContext = QdbFileGetContext(WdfRequestGetFileObject(Request));
   pipeOUT = fileContext->DebugOUT;

   ntStatus = WdfRequestRetrieveInputMemory(Request, &hTxBufferObj);
   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_WRITE,
         QDB_DBG_LEVEL_ERROR,
         ("<%s> QDBWT_WriteUSB: failure retrieving TX buffer (0x%x)\n", pDevContext->PortName, ntStatus)
      );
      WdfRequestCompleteWithInformation(Request, ntStatus, 0);
      return;
   }

   // format URB and set URB flag to USBD_TRANSFER_DIRECTION_OUT
   ntStatus = WdfUsbTargetPipeFormatRequestForWrite(pipeOUT, Request, hTxBufferObj, NULL);
   if (!NT_SUCCESS(ntStatus))
   {
      QDB_DbgPrint
      (
         QDB_DBG_MASK_WRITE,
         QDB_DBG_LEVEL_ERROR,
         ("<%s> QDBWT_WriteUSB: failed to format URB (0x%x)\n", pDevContext->PortName, ntStatus)
      );
      WdfRequestCompleteWithInformation(Request, ntStatus, 0);
      return;
   }

   ioTarget = WdfUsbTargetPipeGetIoTarget(pipeOUT);
   txContext->IoBlock = hTxBufferObj;

   // set completion routine and send...
   WdfRequestSetCompletionRoutine(Request, QDBWT_WriteUSBCompletion, pDevContext);

   bResult = WdfRequestSend(Request, ioTarget, WDF_NO_SEND_OPTIONS);
   if (bResult == FALSE)
   {
      ntStatus = WdfRequestGetStatus(Request);
      QDB_DbgPrint
      (
         QDB_DBG_MASK_WRITE,
         QDB_DBG_LEVEL_ERROR,
         ("<%s> QDBWT_WriteUSB: WdfRequestSend failed (0x%x)\n", pDevContext->PortName, ntStatus)
      );
   }

   QDB_DbgPrint
   (
      QDB_DBG_MASK_WRITE,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--QDBWT_WriteUSB: 0x%p (%dB)\n", pDevContext->PortName, Request, Length)
   );
}  // QDBWT_WriteUSB

VOID QDBWT_WriteUSBCompletion
(
   WDFREQUEST                  Request,
   WDFIOTARGET                 Target,
   PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
   WDFCONTEXT                  Context
)
{
   PWDF_USB_REQUEST_COMPLETION_PARAMS usbInfo;
   NTSTATUS         ntStatus;
   PDEVICE_CONTEXT  pDevContext;
   PREQUEST_CONTEXT txContext;
   WDFUSBPIPE       pipeOUT;
   ULONG            writeLength = 0;

   pDevContext = (PDEVICE_CONTEXT)Context;
   txContext = QdbRequestGetContext(Request);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_READ,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> -->QDBWT_WriteUSBCompletion: [%u] 0x%p)\n", pDevContext->PortName,
        txContext->Index, Request)
   );

   pipeOUT  = (WDFUSBPIPE)Target;
   usbInfo  = CompletionParams->Parameters.Usb.Completion;
   ntStatus = CompletionParams->IoStatus.Status;
   if (!NT_SUCCESS(ntStatus))
   {
       // TODO: need to reset pipe at PASSIVE_LEVEL?
       // QueuePassiveLevelCallback(WdfIoTargetGetDevice(Target), pipeOUT);
      QDB_DbgPrint
      (
         QDB_DBG_MASK_WRITE,
         QDB_DBG_LEVEL_TRACE,
         ("<%s> QDBWT_WriteUSBCompletion: error completion 0x%p(0x%x)\n", pDevContext->PortName,
           Request, ntStatus)
      );
   }
   else
   {
      writeLength = (ULONG)usbInfo->Parameters.PipeWrite.Length;
      WdfRequestSetInformation(Request, writeLength);
   }

   WdfRequestComplete(Request, ntStatus);

   QDB_DbgPrint
   (
      QDB_DBG_MASK_WRITE,
      QDB_DBG_LEVEL_TRACE,
      ("<%s> <--QDBWT_WriteUSBCompletion: 0x%p (%dB/%dB)\n", pDevContext->PortName,
        Request, writeLength, pDevContext->MaxXfrSize)
   );

   return;

}  // QDBWT_WriteUSBCompletion

