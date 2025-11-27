/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B W T . C

GENERAL DESCRIPTION
  This file contains implementations for writing data to USB device.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdio.h>
#include "USBWT.h"
#include "USBUTL.h"
#include "USBPWR.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBWT.tmh"

#endif   // EVENT_TRACING

extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);

extern USHORT ucThCnt;

NTSTATUS USBWT_Write(IN PDEVICE_OBJECT DeviceObject, IN PIRP pIrp)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation (pIrp);
   KIRQL irql = KeGetCurrentIrql();
   ULONG ulWriteLength;

   if (DeviceObject == NULL)
   {
      QCUSB_DbgPrintG
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> WIRP: 0x%p (No Port for 0x%p)\n", gDeviceName, pIrp, DeviceObject)
      );
      USBUTL_CompleteRequest(pIrp, STATUS_DELETE_PENDING, 0);
      return STATUS_DELETE_PENDING;
   }
   pDevExt = DeviceObject->DeviceExtension;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s 0x%p> WIRP 0x%p => t 0x%p\n", pDevExt->PortName, DeviceObject, pIrp,
       KeGetCurrentThread())
   );

   ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, pIrp);
   if (!NT_SUCCESS(ntStatus))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> WIRP 0x%p(Crm 0x%x)\n", pDevExt->PortName, pIrp, ntStatus)
      );
      USBUTL_CompleteRequest(pIrp, ntStatus, 0);
      return ntStatus;
   }

   InterlockedIncrement(&(pDevExt->Sts.lRmlCount[2]));

   if (pIrp->MdlAddress)
   {
      ulWriteLength = USBUTL_GetMdlTotalCount(pIrp->MdlAddress);
   }
   else
   {
      ulWriteLength = irpStack->Parameters.Write.Length;
   }

   #ifdef QCNDIS_ERROR_TEST
   {
      PQCQOS_HDR qosHdr;

      if (ulWriteLength > sizeof(QCQOS_HDR))
      {
         qosHdr = (PQCQOS_HDR)(pIrp->AssociatedIrp.SystemBuffer);
         if (qosHdr->FlowId != 0)
         {
            // fail this packet
            pIrp->IoStatus.Information = 0;
            pIrp->IoStatus.Status = ntStatus = STATUS_UNSUCCESSFUL;

            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WIRP,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> WIRP (CeQos 0x%x/%ldB) 0x%p No DEV\n", pDevExt->PortName,
                 ntStatus, pIrp->IoStatus.Information, pIrp)
            );
            QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
            QCIoCompleteRequest(pIrp, IO_NO_INCREMENT);
            goto Exit;
         }
      }
   }
   #endif // QCNDIS_ERROR_TEST

   if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(pIrp);

      if ((gVendorConfig.DriverResident == 0) && (pDevExt->bInService == FALSE))
      {
         pIrp->IoStatus.Information = 0;
         pIrp->IoStatus.Status = ntStatus = STATUS_DELETE_PENDING;
      }
      else
      {
         pIrp->IoStatus.Information = ulWriteLength;
         pIrp->IoStatus.Status = ntStatus = STATUS_SUCCESS;
      }
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> WIRP (Cdp 0x%x/%ldB) 0x%p No DEV\n", pDevExt->PortName,
           ntStatus, pIrp->IoStatus.Information, pIrp)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
      goto Exit;
   }

   if (pDevExt->Sts.lRmlCount[0] <= 0 && pDevExt->bInService == TRUE) // device not opened
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> WIRP 0x%p(RML[0] %d) \n", pDevExt->PortName, pIrp, pDevExt->Sts.lRmlCount[0])
      );
   }

   if ((pDevExt->bInService == FALSE) || (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED)))
   {  //the device has not been opened (CreateFile)
      pIrp->IoStatus.Information = 0;
      pIrp->IoStatus.Status = ntStatus = STATUS_DELETE_PENDING;
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> WIRP 0x%p(Cns 0x%x)\n", pDevExt->PortName, pIrp, ntStatus)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
      goto Exit;
   } 

   if (ulWriteLength == 0)
   {
      pIrp->IoStatus.Information = 0;
      pIrp->IoStatus.Status = ntStatus = STATUS_SUCCESS;
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> WIRP (C 0x%x/0B) 0x%p immidiate\n", pDevExt->PortName,
           ntStatus, pIrp)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
      goto Exit;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> ENQ WIRP-0 0x%p =>%ldB\n", pDevExt->PortName, pIrp, ulWriteLength)
   );

   ntStatus = USBWT_Enqueue(pDevExt, pIrp, 1);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> ENQ WIRP-1 0x%p =>%ldB (0x%x)\n", pDevExt->PortName,
        pIrp, ulWriteLength, ntStatus)
   );

   if (ntStatus != STATUS_PENDING)
   {
      if ((ntStatus == STATUS_CANCELLED) || (ntStatus == STATUS_DELETE_PENDING))
      {
         pIrp->IoStatus.Information = 0;
      }
      pIrp->IoStatus.Status = ntStatus;

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> WIRP 0x%p(C 0x%x)\n", pDevExt->PortName, pIrp, ntStatus)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
   }

Exit:

   return ntStatus;
}  // USBWT_Write

VOID CancelWriteRoutine(PDEVICE_OBJECT DeviceObject, PIRP pIrp)
{
   KIRQL irql = KeGetCurrentIrql();
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   BOOLEAN bIrpInQueue;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   #ifdef QCUSB_MULTI_WRITES
   if (pDevExt->UseMultiWrites == TRUE)
   {
      USBMWT_CancelWriteRoutine(DeviceObject, pIrp);
      return;
   }
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WIRP,
      QCUSB_DBG_LEVEL_ERROR,
      ("<%s> CancelWriteRoutine-0 IRP 0x%p\n", pDevExt->PortName, pIrp)
   );
   IoReleaseCancelSpinLock(pIrp->CancelIrql);

   QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

   bIrpInQueue = USBUTL_IsIrpInQueue
                 (
                    pDevExt,
                    &pDevExt->WriteDataQueue,
                    pIrp,
                    QCUSB_IRP_TYPE_WIRP,
                    2
                 );

   if (bIrpInQueue == TRUE)
   {
      RemoveEntryList(&pIrp->Tail.Overlay.ListEntry);
      pIrp->IoStatus.Status = STATUS_CANCELLED;
      pIrp->IoStatus.Information = 0;
      InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);
   }
   else if ((pDevExt->pWriteCurrent == pIrp) && (pDevExt->bWriteActive == TRUE))
   {
      // restore the cancel routine for future cancellation within the thread
      IoSetCancelRoutine(pIrp, CancelWriteRoutine);
      USBUTL_AddIrpRecord(pDevExt->WriteCancelRecords, pIrp, NULL, NULL);
      // Set event to cancel the current read IRP
      KeSetEvent(&pDevExt->WriteCancelCurrentEvent, IO_NO_INCREMENT, FALSE);
   }

   // If the IRP is not in IO queue or not active, then it should be in
   // the completion queue by now, and we are fine.

   QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
}  // CancelWriteRoutine

NTSTATUS USBWT_WriteIrpCompletion
(
   PDEVICE_EXTENSION pDevExt,
   ULONG             ulRequestedBytes,
   ULONG             ulActiveBytes,
   NTSTATUS          ntStatus,
   UCHAR             cookie
)
{
   PIRP pIrp;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pIrp = pDevExt->pWriteCurrent;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WRITE,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> WIC<%d>: IRP 0x%p (0x%x)\n", pDevExt->PortName, cookie, pIrp, ntStatus)
   );


   if (NT_SUCCESS(ntStatus) || (ntStatus == STATUS_CANCELLED))
   {
      pDevExt->WtErrorCount = 0;
   }
   else
   {
      pDevExt->WtErrorCount++;

      // after some magic number of times of failure,
      // we mark the device as 'removed'
      if ((pDevExt->WtErrorCount > pDevExt->NumOfRetriesOnError) && (pDevExt->ContinueOnDataError == FALSE))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> WIC: failure %d, dev removed\n", pDevExt->PortName, pDevExt->WtErrorCount)
         );
         clearDevState(DEVICE_STATE_PRESENT);
         pDevExt->bWriteStopped  = TRUE;
         pDevExt->WtErrorCount = pDevExt->NumOfRetriesOnError;
      }
   }

   if (pIrp != NULL) // if it wasn't canceled out of the block
   {
      if (!IoSetCancelRoutine(pIrp, NULL))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> WIRPC: WIRP 0x%p already cxled\n", pDevExt->PortName, pIrp)
         );
         goto ExitWriteCompletion;
      }

      pIrp->IoStatus.Information = ulRequestedBytes - ulActiveBytes;
      pIrp->IoStatus.Status = ntStatus;

      InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);

   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> WIC NULL IRP-%d\n", pDevExt->PortName, cookie)
      );
   }

ExitWriteCompletion:

   return ntStatus;
}  // USBWT_WriteIrpCompletion

NTSTATUS USBWT_CancelWriteThread(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   NTSTATUS ntStatus;
   LARGE_INTEGER delayValue;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WRITE,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> USBWT_CancelWriteThread %d\n", pDevExt->PortName, cookie)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> USBWT_CancelWriteThread: wrong IRQL - %d\n", pDevExt->PortName, cookie)
      );

      // best effort
      KeSetEvent(&pDevExt->CancelWriteEvent,IO_NO_INCREMENT,FALSE);
      return STATUS_UNSUCCESSFUL;
   }

   if (InterlockedIncrement(&pDevExt->WriteThreadCancelStarted) > 1)
   {
      while ((pDevExt->hWriteThreadHandle != NULL) || (pDevExt->pWriteThread != NULL))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> Wth cxl in pro\n", pDevExt->PortName)
         );
         USBUTL_Wait(pDevExt, -(3 * 1000 * 1000));  // 0.3 sec
      }
      InterlockedDecrement(&pDevExt->WriteThreadCancelStarted);      
      return STATUS_SUCCESS;
   }

   if ((pDevExt->hWriteThreadHandle != NULL) || (pDevExt->pWriteThread != NULL))
   {
      KeClearEvent(&pDevExt->WriteThreadClosedEvent);
      KeSetEvent(&pDevExt->CancelWriteEvent,IO_NO_INCREMENT,FALSE);
   
      if (pDevExt->pWriteThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pDevExt->pWriteThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pDevExt->pWriteThread);
         KeClearEvent(&pDevExt->WriteThreadClosedEvent);
         _closeHandle(pDevExt->hWriteThreadHandle, "W-0");
         pDevExt->pWriteThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pDevExt->WriteThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pDevExt->WriteThreadClosedEvent);
         _closeHandle(pDevExt->hWriteThreadHandle, "W-0");
      }

   }

   InterlockedDecrement(&pDevExt->WriteThreadCancelStarted);      
   
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WRITE,
      QCUSB_DBG_LEVEL_INFO,
      ("<%s> Wth: OUT Rml[0]=%u\n", pDevExt->PortName, pDevExt->Sts.lRmlCount[0])
   );

   return STATUS_SUCCESS;
}  // USBWT_CancelWriteThread


NTSTATUS USBWT_Enqueue(PDEVICE_EXTENSION pDevExt, PIRP pIrp, UCHAR cookie)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   OBJECT_ATTRIBUTES objAttr; 
   BOOLEAN bQueueWasEmpty = FALSE;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> W_Enq STATUS_DELETE_PENDING (irp=0x%p)\n", pDevExt->PortName, pIrp)
      );
      return STATUS_DELETE_PENDING;
   }

   // Make sure the write thread is created with IRQL==PASSIVE_LEVEL
   if (((pDevExt->pWriteThread == NULL) &&
        (pDevExt->hWriteThreadHandle == NULL)) &&
        (irql > PASSIVE_LEVEL))
   {
      NTSTATUS ntS;

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> _W IRQL high\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   // has the write thead started>?
   if ((pDevExt->hWriteThreadHandle == NULL) && (pDevExt->pWriteThread == NULL) &&
       inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      NTSTATUS ntStatus;

      QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
      if (InterlockedIncrement(&pDevExt->WriteThreadInCreation) > 1)
      {
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
         goto W_Enqueue;
      }
      else
      {
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
      }
 
      KeClearEvent(&pDevExt->WriteThreadStartedEvent);
      InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
      ucThCnt++;

      #ifdef QCUSB_MULTI_WRITES
      if (pDevExt->UseMultiWrites == TRUE)
      {
         ntStatus = PsCreateSystemThread
                    (
                       OUT &pDevExt->hWriteThreadHandle,
                       IN THREAD_ALL_ACCESS,
                       IN &objAttr,     // POBJECT_ATTRIBUTES  ObjectAttributes
                       IN NULL,         // HANDLE  ProcessHandle
                       OUT NULL,        // PCLIENT_ID  ClientId
                       IN (PKSTART_ROUTINE)USBMWT_WriteThread,
                       IN (PVOID) pDevExt
                    );
      }
      else
      #endif
      {
         ntStatus = PsCreateSystemThread
                    (
                       OUT &pDevExt->hWriteThreadHandle,
                       IN THREAD_ALL_ACCESS,
                       IN &objAttr,     // POBJECT_ATTRIBUTES  ObjectAttributes
                       IN NULL,         // HANDLE  ProcessHandle
                       OUT NULL,        // PCLIENT_ID  ClientId
                       IN (PKSTART_ROUTINE)USBWT_WriteThread,
                       IN (PVOID) pDevExt
                    );
      }
      if (ntStatus != STATUS_SUCCESS)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> _Write thread failure 0x%x\n", pDevExt->PortName, ntStatus)
         );
         pDevExt->pWriteThread = NULL;
         pDevExt->hWriteThreadHandle = NULL;
         InterlockedDecrement(&pDevExt->WriteThreadInCreation);
         return ntStatus;
      }

      ntStatus = KeWaitForSingleObject
                 (
                    &pDevExt->WriteThreadStartedEvent,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL
                 );

      ntStatus = ObReferenceObjectByHandle
                 (
                    pDevExt->hWriteThreadHandle,
                    THREAD_ALL_ACCESS,
                    NULL,
                    KernelMode,
                    (PVOID*)&pDevExt->pWriteThread,
                    NULL
                 );
      if (!NT_SUCCESS(ntStatus))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> W: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
         );
         pDevExt->pWriteThread = NULL;
      }
      else
      {
         if (!NT_SUCCESS(ntStatus = ZwClose(pDevExt->hWriteThreadHandle)))
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WRITE,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> W: ZwClose failed - 0x%x\n", pDevExt->PortName, ntStatus)
            );
         }
         else
         {
            ucThCnt--;
            pDevExt->hWriteThreadHandle = NULL;
         }
      }

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> W handle=0x%p thOb=0x%p\n", pDevExt->PortName,
          pDevExt->hWriteThreadHandle, pDevExt->pWriteThread)
      );
      InterlockedDecrement(&pDevExt->WriteThreadInCreation);
   }
  
   W_Enqueue:

   if (pIrp == NULL)
   {
      return ntStatus;
   }

   ntStatus = STATUS_PENDING;

   QcAcquireSpinLockWithLevel(&pDevExt->WriteSpinLock, &levelOrHandle, irql);

   // if the IRP was cancelled between then
   // and now, then we need special processing here. Note: the cancel routine
   // will not complete the IRP in this case because it will not find the
   // IRP anywhwere.
   if (pIrp->Cancel)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> WIRP 0x%p pre-cxl\n", pDevExt->PortName, pIrp)
      );
      IoSetCancelRoutine(pIrp, NULL);
      ntStatus = STATUS_CANCELLED;
      QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);
      return ntStatus;
   }

   IoSetCancelRoutine(pIrp, CancelWriteRoutine);
   _IoMarkIrpPending(pIrp);

   pIrp->IoStatus.Information = 0;

   if (IsListEmpty(&pDevExt->WriteDataQueue))
   {
      bQueueWasEmpty = TRUE;
   }
   InsertTailList(&pDevExt->WriteDataQueue, &pIrp->Tail.Overlay.ListEntry);

   QCPWR_CheckToWakeup(pDevExt, NULL, QCUSB_BUSY_WT, 0);

   if (bQueueWasEmpty == TRUE)
   {
      KeSetEvent
      (
         pDevExt->pWriteEvents[KICK_WRITE_EVENT_INDEX],
         IO_NO_INCREMENT,
         FALSE
      );
   }

   QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);

   return ntStatus; // accepted the write, should be STATUS_PENDING
}  // USBWT_Enqueue

NTSTATUS WriteCompletionRoutine
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pContext;

   if (pIrp->IoStatus.Status == STATUS_PENDING)
   {
      KeSetEvent
      (
         pDevExt->pWriteEvents[KICK_WRITE_EVENT_INDEX],
         IO_NO_INCREMENT,
         FALSE
      );
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> ERR-WCR-0: 0x%p StkLo=%d/%d (0x%x)", pDevExt->PortName, pIrp,
           pIrp->CurrentLocation, pIrp->StackCount, pIrp->IoStatus.Status)
      );
      return STATUS_MORE_PROCESSING_REQUIRED;
   }

   KeSetEvent
   (
      pDevExt->pWriteEvents[WRITE_COMPLETION_EVENT_INDEX],
      IO_NO_INCREMENT,
      FALSE
   );

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // WriteCompletionRoutine

void USBWT_WriteThread(PVOID pContext)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pContext;
   PIO_STACK_LOCATION pNextStack;
   PIRP         pIrp;
   PURB         pUrb;
   ULONG        ulTransferBytes;
   BOOLEAN      bCancelled = FALSE, bPurged = FALSE;
   NTSTATUS     ntStatus;
   PKWAIT_BLOCK pwbArray = NULL;
   BOOLEAN      bSendZeroLength = FALSE;
   ULONG        i;
   PVOID        pActiveBuffer = NULL;
   ULONG        ulActiveBytes = 0, ulReqBytes = 0;
   KEVENT       dummyEvent;
   BOOLEAN bFirstTime = TRUE;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   // allocate an urb for write operations
   pUrb = ExAllocatePool
          (
             NonPagedPool,
             sizeof( struct _URB_BULK_OR_INTERRUPT_TRANSFER )
          );
   if (pUrb == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> Wth: NULL URB - STATUS_NO_MEMORY\n", pDevExt->PortName)
      );
      _closeHandle(pDevExt->hWriteThreadHandle, "W-0");
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }
   
   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (WRITE_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 "WriteThread.pwbArray"
              );
   if (!pwbArray)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> Wth: STATUS_NO_MEMORY 1\n", pDevExt->PortName)
      );
      _closeHandle(pDevExt->hWriteThreadHandle, "W-1");
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   // allocate irp to use for write operations
   pIrp = IoAllocateIrp((CCHAR)(pDevExt->StackDeviceObject->StackSize+1), FALSE );

   if (!pIrp )
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> Wth: NULL IRP - STATUS_NO_MEMORY\n", pDevExt->PortName)
      );
      _closeHandle(pDevExt->hWriteThreadHandle, "W-2");
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   #ifdef ENABLE_LOGGING
   // Create logs
   if (pDevExt->EnableLogging == TRUE)
   {
      QCUSB_CreateLogs(pDevExt, QCUSB_CREATE_TX_LOG);
   }
   #endif // ENABLE_LOGGING

   // Set WRITE thread priority
   KeSetPriorityThread(KeGetCurrentThread(), QCUSB_WT_PRIORITY);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WRITE,
      QCUSB_DBG_LEVEL_INFO,
      ("<%s> W pri=%d\n", pDevExt->PortName, KeQueryPriorityThread(KeGetCurrentThread()))
   );

   pDevExt->bWriteActive  = FALSE;
   pDevExt->bWriteStopped = FALSE;
   pDevExt->pWriteCurrent = NULL;  // PIRP

   pDevExt->pWriteEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
   KeClearEvent(&dummyEvent);
   KeClearEvent(&pDevExt->WritePurgeEvent);
   
   while (TRUE)
   {
     if ((pDevExt->bWriteStopped == TRUE)  ||
         (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED)) ||
         (bPurged == TRUE))
     {
         USBUTL_PurgeQueue
         (
            pDevExt,
            &pDevExt->WriteDataQueue,
            &pDevExt->WriteSpinLock,
            QCUSB_IRP_TYPE_WIRP,
            0
         );
         bPurged = FALSE;
         USBUTL_CheckPurgeStatus(pDevExt, USB_PURGE_WT);

         if (bCancelled == TRUE)
         {
            break;
         }
         goto wait_for_completion;
      }

      // De-Queue
      QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

      if ((!IsListEmpty(&pDevExt->WriteDataQueue)) &&
          (pDevExt->bWriteActive == FALSE))
      {
         PLIST_ENTRY headOfList;
         PIO_STACK_LOCATION irpStack;

         headOfList = RemoveHeadList(&pDevExt->WriteDataQueue);
         pDevExt->pWriteCurrent = CONTAINING_RECORD
                                             (
                                                headOfList,
                                                IRP,
                                                Tail.Overlay.ListEntry
                                             );
         irpStack = IoGetCurrentIrpStackLocation(pDevExt->pWriteCurrent);
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

         // pActiveBuffer not NULL during ->pWriteCurrent transfer
         // See if we need to get new buffer pointer and new byte count
         if (pActiveBuffer == NULL)
         {
            if ((pDevExt->pWriteCurrent)->MdlAddress)
            {
               pActiveBuffer = MmGetSystemAddressForMdlSafe
                               (
                                  (pDevExt->pWriteCurrent)->MdlAddress,
                                  HighPagePriority
                               );
               ulReqBytes = USBUTL_GetMdlTotalCount((pDevExt->pWriteCurrent)->MdlAddress);
            }
            else
            {
               pActiveBuffer = (pDevExt->pWriteCurrent)->AssociatedIrp.SystemBuffer;
               ulReqBytes = irpStack->Parameters.Write.Length;
            }
            ulActiveBytes = ulReqBytes;
         }  // if (pActiveBuffer == NULL)

         ulTransferBytes = ulActiveBytes;

         RtlZeroMemory
         (
            pUrb,
            sizeof( struct _URB_BULK_OR_INTERRUPT_TRANSFER )
         ); // clear out the urb we reuse

         if ((pDevExt->pWriteCurrent)->MdlAddress == NULL)
         {
            UsbBuildInterruptOrBulkTransferRequest
            (
               pUrb,
               sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
               pDevExt->Interface[pDevExt->DataInterface]
                   ->Pipes[pDevExt->BulkPipeOutput].PipeHandle,
               pActiveBuffer,
               NULL,
               ulTransferBytes,
               USBD_TRANSFER_DIRECTION_OUT,
               NULL
            );
         }
         else
         {
            UsbBuildInterruptOrBulkTransferRequest
            (
               pUrb,
               sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
               pDevExt->Interface[pDevExt->DataInterface]
                   ->Pipes[pDevExt->BulkPipeOutput].PipeHandle,
               NULL,
               (pDevExt->pWriteCurrent)->MdlAddress,
               ulTransferBytes,
               USBD_TRANSFER_DIRECTION_OUT,
               NULL
            );
         }

         IoReuseIrp(pIrp, STATUS_SUCCESS);
         pNextStack = IoGetNextIrpStackLocation(pIrp);
         pNextStack->Parameters.DeviceIoControl.IoControlCode =
            IOCTL_INTERNAL_USB_SUBMIT_URB;
         pNextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
         pNextStack->Parameters.Others.Argument1 = pUrb;

         IoSetCompletionRoutine
         (
            pIrp,
            WriteCompletionRoutine,
            pDevExt,
            TRUE,
            TRUE,
            TRUE
         );

         #ifdef ENABLE_LOGGING
         if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hTxLogFile != NULL))
         {
            if ((pDevExt->pWriteCurrent)->MdlAddress == NULL)
            {
               QCUSB_LogData
               (
                  pDevExt,
                  pDevExt->hTxLogFile,
                  pActiveBuffer, 
                  ulTransferBytes,
                  QCUSB_LOG_TYPE_WRITE
               );
            }
            else
            {
               USBUTL_LogMdlData
               (
                  pDevExt,
                  pDevExt->hTxLogFile,
                  (pDevExt->pWriteCurrent)->MdlAddress, 
                  QCUSB_LOG_TYPE_WRITE
               );
            }
         }
         #endif // ENABLE_LOGGING

         if ((pDevExt->pWriteCurrent)->MdlAddress == NULL)
         {
            USBUTL_PrintBytes
            (
               pActiveBuffer, 128, ulTransferBytes, "SendData", pDevExt,
               QCUSB_DBG_MASK_WDATA, QCUSB_DBG_LEVEL_VERBOSE
            );
         }
         else
         {
            USBUTL_MdlPrintBytes
            (
               (pDevExt->pWriteCurrent)->MdlAddress, 128, "MdlSendData",
               QCUSB_DBG_MASK_WDATA, QCUSB_DBG_LEVEL_VERBOSE
            );
         }

         pDevExt->bWriteActive = TRUE;
         ntStatus = IoCallDriver(pDevExt->StackDeviceObject,pIrp);
         if(ntStatus != STATUS_PENDING)
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WRITE,
               QCUSB_DBG_LEVEL_CRITICAL,
               ("<%s> Wth: IoCallDriver rtn 0x%x", pDevExt->PortName, ntStatus)
            );
         }
      }
      else
      {
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
      }

      // if ((pDevExt->bWriteActive == FALSE) && (bCancelled == TRUE))
      // {
      //    break;
      // }

     // No matter what IoCallDriver returns, we always wait on the kernel event
     // we created earlier. Our completion routine will gain control when the IRP
     // completes to signal this event. -- Walter Oney's WDM book page 228
wait_for_completion:

      if (bFirstTime == TRUE)
      {
         bFirstTime = FALSE;
         KeSetEvent(&pDevExt->WriteThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      }

      // if nothing in the queue, we just wait for a KICK event
      ntStatus = KeWaitForMultipleObjects
                 (
                    WRITE_EVENT_COUNT,
                    (VOID **) &pDevExt->pWriteEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE, // non-alertable // TRUE,
                    NULL,
                    pwbArray
                 );

      switch(ntStatus)
      {
         case QC_DUMMY_EVENT_INDEX:
         {
            KeClearEvent(&dummyEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WRITE,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> W: DUMMY_EVENT\n", pDevExt->PortName)
            );
            goto wait_for_completion;
         }

         case WRITE_COMPLETION_EVENT_INDEX:
         {
            // reset write completion event
            KeClearEvent(&pDevExt->WriteCompletionEvent);
            pDevExt->bWriteActive = FALSE;

            // check completion status

            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

            ntStatus = pIrp->IoStatus.Status;

            // log status
            #ifdef ENABLE_LOGGING
            if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hTxLogFile != NULL))
            {
               if (ntStatus != STATUS_SUCCESS)
               {
                  QCUSB_LogData
                  (
                     pDevExt,
                     pDevExt->hTxLogFile,
                     (PVOID)&ntStatus,
                     sizeof(NTSTATUS),
                     QCUSB_LOG_TYPE_RESPONSE_WT
                  );
               }
            }
            #endif // ENABLE_LOGGING

            if (ntStatus == STATUS_SUCCESS)
            {
               // subtract the bytes transfered from the requested bytes
               ulTransferBytes =
                  pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
               ulActiveBytes -= ulTransferBytes;

               // have we written the full request yet?
               if (ulActiveBytes > 0)
               {
                  if ((pDevExt->pWriteCurrent)->MdlAddress == NULL)
                  {
                     // update buffer pointer
                     (PUCHAR)pActiveBuffer +=
                        pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
                     // reque the io
                     InsertHeadList
                     (
                        &pDevExt->WriteDataQueue,
                        &(pDevExt->pWriteCurrent->Tail.Overlay.ListEntry)
                     );

                     pDevExt->pWriteCurrent = NULL;
                     QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                     continue; // go around again
                  }
                  else
                  {
                     QCUSB_DbgPrint
                     (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> W: MDL write not fullfilled (%u/%uB)\n", pDevExt->PortName,
                          ulActiveBytes, ulReqBytes)
                     );
                  }
               }
               else if (ulReqBytes % pDevExt->wMaxPktSize == 0)
               {
                  // zero-length packet is enqueued
                  if (bSendZeroLength == FALSE)
                  {
                     QCUSB_DbgPrint
                     (
                        QCUSB_DBG_MASK_WRITE,
                        QCUSB_DBG_LEVEL_DETAIL,
                        ("<%s> W: add 0-len\n", pDevExt->PortName)
                     );
                     bSendZeroLength = TRUE;
                     InsertHeadList
                     (
                        &pDevExt->WriteDataQueue,
                        &(pDevExt->pWriteCurrent->Tail.Overlay.ListEntry)
                     );

                     pDevExt->pWriteCurrent = NULL;
                     QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
   
                     continue; // go around again
                  }
               }
            } // if STATUS_SUCCESS
            else // error???
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_WRITE,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> Wth: TX failure 0x%x xferred %d", pDevExt->PortName, ntStatus,
                    pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength)
               );
               // re-send IRP???
            }

            if ((ntStatus == STATUS_CANCELLED) || (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED)))
            {
               ntStatus = STATUS_CANCELLED;
            }

            USBWT_WriteIrpCompletion(pDevExt, ulReqBytes, ulActiveBytes, ntStatus, 7);
            bSendZeroLength = FALSE;
            pActiveBuffer   = NULL;
            ulActiveBytes   = ulReqBytes = 0;
            pDevExt->pWriteCurrent = NULL;

            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

            break;
         }

         case WRITE_CANCEL_CURRENT_EVENT_INDEX:
         {
            PQCUSB_IRP_RECORDS irpToCancelRec;
            KeClearEvent(&pDevExt->WriteCancelCurrentEvent);

            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
            if (pDevExt->pWriteCurrent == NULL)
            {
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
               break;
            }

            irpToCancelRec = USBUTL_RemoveIrpRecord
                             (
                                pDevExt->WriteCancelRecords,
                                pDevExt->pWriteCurrent
                             );
            if (irpToCancelRec != NULL)
            {
               ExFreePool(irpToCancelRec);
               IoCancelIrp(pIrp);
            }
            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

            break;
         }
         
         case KICK_WRITE_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->KickWriteEvent);

            if (pDevExt->bWriteActive == TRUE)
            {
               goto wait_for_completion;
            }
            else if (pDevExt->pWriteCurrent != NULL)
            {
               if (ulActiveBytes > 0)
               {
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_WRITE,
                     QCUSB_DBG_LEVEL_ERROR,
                     ("<%s> ERR-Wth: CurrIrp - 0x%p(%luB)", pDevExt->PortName,
                       pDevExt->pWriteCurrent,  ulActiveBytes)
                  );
                  goto wait_for_completion;
               }
            }
            // else we just go around the loop again, picking up the next
            // write entry
            continue;
         }

         case CANCEL_EVENT_INDEX:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WRITE,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> W CANCEL_EVENT_INDEX-0", pDevExt->PortName)
            );

            // clear cancel event so we don't reactivate
            KeClearEvent(&pDevExt->CancelWriteEvent);

            #ifdef ENABLE_LOGGING
            QCUSB_LogData
            (
               pDevExt,
               pDevExt->hTxLogFile,
               (PVOID)NULL,
               0,
               QCUSB_LOG_TYPE_CANCEL_THREAD
            );
            #endif // ENABLE_LOGGING

            // signal the loop that a cancel has occurred
            pDevExt->bWriteStopped = bCancelled = TRUE;
            if (pDevExt->bWriteActive == TRUE)
            {
               // cancel outstanding irp
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_WRITE,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> Wth: CANCEL - IRP\n", pDevExt->PortName)
               );
               IoCancelIrp(pIrp);

               // wait for writes to complete, don't cancel
               // if a write is active, continue to wait for the completion
               // we pick up the canceled status at the top of the service
               // loop
               goto wait_for_completion;
            }
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WRITE,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> W CANCEL_EVENT_INDEX-1", pDevExt->PortName)
            );
            break; // goto exit_WriteThread; // nothing active exit
         }

         case WRITE_PURGE_EVENT_INDEX:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WRITE,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> W PURGE_EVENT-0", pDevExt->PortName)
            );

            // clear purge event so we don't reactivate
            KeClearEvent(&pDevExt->WritePurgeEvent);

            // signal the loop that a purge has occurred
            bPurged = TRUE;
            if (pDevExt->bWriteActive == TRUE)
            {
               // cancel outstanding irp
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_WRITE,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> Wth: PURGE - IRP\n", pDevExt->PortName)
               );
               IoCancelIrp(pIrp);

               // wait for writes to complete, don't cancel
               // if a write is active, continue to wait for the completion
               // we pick up the canceled status at the top of the service
               // loop
               goto wait_for_completion;
            }
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WRITE,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> W PURGE_EVENT-1", pDevExt->PortName)
            );
            break;
         }

         case WRITE_STOP_EVENT_INDEX:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WRITE,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> W STOP_EVENT", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->WriteStopEvent);
            pDevExt->bWriteStopped = TRUE;
            break;
         }

         case WRITE_RESUME_EVENT_INDEX:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WRITE,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> W RESUME_EVENT rm %d", pDevExt->PortName, pDevExt->bDeviceRemoved)
            );
            KeClearEvent(&pDevExt->WriteResumeEvent);
            pDevExt->bWriteStopped = pDevExt->bDeviceRemoved;
            break;
         }

         default:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_WRITE,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> Wth: default sig 0x%x", pDevExt->PortName, ntStatus)
            );
            // log status
            #ifdef ENABLE_LOGGING
            QCUSB_LogData
            (
               pDevExt,
               pDevExt->hTxLogFile,
               (PVOID)&ntStatus,
               sizeof(NTSTATUS),
               QCUSB_LOG_TYPE_RESPONSE_WT
            );
            #endif // ENABLE_LOGGING

            // Ignore for now
            break;
         } // default
      }  // switch

      // go round again
   }  // end while forever 

exit_WriteThread:

   if (pDevExt->hTxLogFile != NULL)
   {
      #ifdef ENABLE_LOGGING
      QCUSB_LogData
      (
         pDevExt,
         pDevExt->hTxLogFile,
         (PVOID)NULL,
         0,
         QCUSB_LOG_TYPE_THREAD_END
      );
      #endif // ENABLE_LOGGING
      ZwClose(pDevExt->hTxLogFile);
      pDevExt->hTxLogFile = NULL;
   }

   if(pUrb)
   {
      ExFreePool(pUrb);
   }
   if(pIrp)
   {
      IoReuseIrp(pIrp, STATUS_SUCCESS);
      IoFreeIrp(pIrp);
   }
   if(pwbArray)
   {
      _ExFreePool(pwbArray);
   }

   KeSetEvent
   (
      &pDevExt->WriteThreadClosedEvent,
      IO_NO_INCREMENT,
      FALSE
   ); // signal write thread closed

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WRITE,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> Wth: OUT\n", pDevExt->PortName)
   );

   PsTerminateSystemThread(STATUS_SUCCESS); // end this thread
}  // USBWT_WriteThread

VOID USBWT_SetStopState(PDEVICE_EXTENSION pDevExt, BOOLEAN StopState)
{
   if ((pDevExt->hWriteThreadHandle == NULL) && (pDevExt->pWriteThread == NULL))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> W: Wth not alive, no act %u\n", pDevExt->PortName, StopState)
      );
      return;
   }

   if (inDevState(DEVICE_STATE_SURPRISE_REMOVED) ||
       inDevState(DEVICE_STATE_DEVICE_REMOVED0))
   {
      if (StopState == FALSE)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> W start - dev removed, no act\n", pDevExt->PortName)
         );
         return;
      }
   }

   if (StopState == TRUE)
   {
      KeSetEvent(&pDevExt->WriteStopEvent, IO_NO_INCREMENT, FALSE);
   }
   else
   {
      KeSetEvent(&pDevExt->WriteResumeEvent, IO_NO_INCREMENT, FALSE);
   }
}  // USBWT_SetStopState
 
NTSTATUS USBWT_Suspend(PDEVICE_EXTENSION pDevExt)
{
   LARGE_INTEGER delayValue;
   NTSTATUS nts = STATUS_UNSUCCESSFUL;

   if ((pDevExt->hWriteThreadHandle != NULL) || (pDevExt->pWriteThread != NULL))
   {
      delayValue.QuadPart = -(50 * 1000 * 1000); // 5 seconds

      KeClearEvent(&pDevExt->WriteStopAckEvent);
      KeSetEvent(&pDevExt->WriteStopEvent, IO_NO_INCREMENT, FALSE);

      nts = KeWaitForSingleObject
            (
               &pDevExt->WriteStopAckEvent,
               Executive,
               KernelMode,
               FALSE,
               &delayValue
            );
      if (nts == STATUS_TIMEOUT)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> WTSuspend: WTO\n", pDevExt->PortName)
         );
         KeClearEvent(&pDevExt->WriteStopEvent);
      }

      KeClearEvent(&pDevExt->WriteStopAckEvent);
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WRITE,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> WTSuspend: 0x%x\n", pDevExt->PortName, nts)
   );

   return nts;
}  // USBWT_Suspend

NTSTATUS USBWT_Resume(PDEVICE_EXTENSION pDevExt)
{
   if ((pDevExt->hWriteThreadHandle != NULL) || (pDevExt->pWriteThread != NULL))
   {
      KeSetEvent(&pDevExt->WriteResumeEvent, IO_NO_INCREMENT, FALSE);
   }

   return STATUS_SUCCESS;

}  // USBWT_Resume

NTSTATUS USBWT_IrpCompletionSetEvent
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP           Irp,
   IN PVOID          Context
)
{
   PKEVENT pEvent = Context;

   KeSetEvent(pEvent, IO_NO_INCREMENT, FALSE);

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // USBWT_IrpCompletionSetEvent

BOOLEAN USBWT_OutPipeOk(PDEVICE_EXTENSION pDevExt)
{
   PIO_STACK_LOCATION pNextStack;
   PIRP               pIrp;
   PURB               pUrb;
   PCHAR              dummyBuf;
   PKEVENT            pEvent;
   LARGE_INTEGER      delayValue;
   NTSTATUS           ntStatus;
   PQCMWT_BUFFER      pWtBuf;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WRITE,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> --> USBWT_OutPipeOk 0x%x\n", pDevExt->PortName, pDevExt->BulkPipeOutput)
   );
   if (pDevExt->BulkPipeOutput == (UCHAR)-1)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _OutPipeOk: no OUT pipe!\n", pDevExt->PortName)
      );
      return FALSE;
   }
   dummyBuf = &(pDevExt->DebugLevel);
   pWtBuf = pDevExt->pMwtBuffer[0];
   if (pWtBuf == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _OutPipeOk: no MEM!\n", pDevExt->PortName)
      );
      return FALSE;
   }
   pIrp = pWtBuf->Irp;
   pUrb = &(pWtBuf->Urb);
   pEvent = &(pWtBuf->WtCompletionEvt);
   RtlZeroMemory
   (
      pUrb,
      sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER)
   );
   UsbBuildInterruptOrBulkTransferRequest
   (
      pUrb,
      sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
      pDevExt->Interface[pDevExt->DataInterface]
          ->Pipes[pDevExt->BulkPipeOutput].PipeHandle,
      dummyBuf,
      NULL,
      0,
      USBD_TRANSFER_DIRECTION_OUT,
      NULL
   );

   IoReuseIrp(pIrp, STATUS_SUCCESS);
   pNextStack = IoGetNextIrpStackLocation(pIrp);
   pNextStack->Parameters.DeviceIoControl.IoControlCode =
      IOCTL_INTERNAL_USB_SUBMIT_URB;
   pNextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
   pNextStack->Parameters.Others.Argument1 = pUrb;

   IoSetCompletionRoutine
   (
      pIrp,
      USBWT_IrpCompletionSetEvent,
      pEvent,
      TRUE,
      TRUE,
      TRUE
   );
   ntStatus = IoCallDriver(pDevExt->StackDeviceObject, pIrp);
   delayValue.QuadPart = -(10 * 1000 * 1000);   // 1.0 sec
   if (ntStatus == STATUS_PENDING)
   {
      ntStatus = KeWaitForSingleObject
                 (
                    pEvent, Executive, KernelMode,
                    FALSE, &delayValue
                 );
      if (ntStatus == STATUS_TIMEOUT)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _OutPipeOk: 1st timeout\n", pDevExt->PortName)
         );
         IoCancelIrp(pIrp);
         KeWaitForSingleObject
         (
            pEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL
         );
      }
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _OutPipeOk: IoCallDriver failed 0x%x\n", pDevExt->PortName, ntStatus)
      );
      KeWaitForSingleObject
      (
         pEvent, Executive, KernelMode,
         FALSE, &delayValue
      );
   }

   KeClearEvent(pEvent);

   pDevExt->OutputPipeStatus = pIrp->IoStatus.Status;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WRITE,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <-- USBWT_OutPipeOk 0x%x/0x%x\n", pDevExt->PortName,
        pIrp->IoStatus.Status, STATUS_DEVICE_NOT_CONNECTED)
   );
   return (pIrp->IoStatus.Status != STATUS_DEVICE_NOT_CONNECTED);
}  // USBWT_OutPipeOk
