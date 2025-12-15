/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B I N T . C

GENERAL DESCRIPTION
    This file contains implementations for reading data from USB device's
    interrupt endpoint.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "USBINT.h"
#include "USBUTL.h"
#include "USBRD.h"
#include "USBPWR.h"
#include "USBPNP.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBINT.tmh"

#endif   // EVENT_TRACING

extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);
extern USHORT ucThCnt;

NTSTATUS USBINT_InitInterruptPipe(IN PDEVICE_OBJECT pDevObj)
{
   NTSTATUS ntStatus;
   USHORT usLength, i;
   PDEVICE_EXTENSION pDevExt;
   PIRP pIrp;
   OBJECT_ATTRIBUTES objAttr;

   pDevExt = pDevObj->DeviceExtension;

   // Make sure the int thread is created at PASSIVE_LEVEL
   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> INT: wrong IRQL\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((pDevExt->pInterruptThread != NULL) || (pDevExt->hInterruptThreadHandle != NULL))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> INT up/resumed\n", pDevExt->PortName)
      );
      USBINT_ResumeInterruptService(pDevExt, 0);
      return STATUS_SUCCESS;
   }
   pDevExt->pInterruptBuffer = NULL;

   // is there an interrupt pipe?
   if (pDevExt->InterruptPipe != (UCHAR)-1)
   {
      // allocate buffer for interrupt data
      usLength = pDevExt->Interface[pDevExt->usCommClassInterface]
                 ->Pipes[pDevExt->InterruptPipe].MaximumPacketSize;
      pDevExt->pInterruptBuffer = ExAllocatePool
                                  (
                                     NonPagedPool, 
                                     usLength
                                  );
      if(!pDevExt->pInterruptBuffer)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> INT: NO_MEM BUF\n", pDevExt->PortName)
         );
         return STATUS_NO_MEMORY;
      }

      RtlZeroMemory(pDevExt->pInterruptBuffer, usLength);
   }

   // kick off interrupt service thread

   KeClearEvent(&pDevExt->IntThreadStartedEvent);
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   ucThCnt++;
   ntStatus = PsCreateSystemThread
              (
                 OUT &pDevExt->hInterruptThreadHandle,
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)ReadInterruptPipe,
                 IN (PVOID) pDevExt
              );         
   if ((!NT_SUCCESS(ntStatus)) || (pDevExt->hInterruptThreadHandle == NULL))
   {
      pDevExt->hInterruptThreadHandle = NULL;
      pDevExt->pInterruptThread = NULL;
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> INT th failure\n", pDevExt->PortName)
      );
      return ntStatus;
   }
   ntStatus = KeWaitForSingleObject
              (
                 &pDevExt->IntThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );

   ntStatus = ObReferenceObjectByHandle
              (
                 pDevExt->hInterruptThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&pDevExt->pInterruptThread,
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> INT: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
      );
      pDevExt->pInterruptThread = NULL;
   }
   else
   {
      if (STATUS_SUCCESS != (ntStatus = ZwClose(pDevExt->hInterruptThreadHandle)))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> INT ZwClose failed - 0x%x\n", pDevExt->PortName, ntStatus)
         );
      }
      else
      {
         ucThCnt--;
         pDevExt->hInterruptThreadHandle = NULL;
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> INT handle=0x%p thOb=0x%p\n", pDevExt->PortName,
       pDevExt->hInterruptThreadHandle, pDevExt->pInterruptThread)
   );
   
   return ntStatus;

}

VOID ReadInterruptPipe(IN PVOID pContext)
{
   USHORT siz;
   PURB pUrb = NULL;
   USBD_STATUS urb_status;
   PIRP pIrp = NULL;
   PIO_STACK_LOCATION nextstack;
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pContext;
   NTSTATUS ntStatus, ntStatus2;
   PUSB_NOTIFICATION_STATUS pNotificationStatus;
   KIRQL IrqLevel;
   BOOLEAN bKeepRunning = TRUE;
   BOOLEAN bCancelled = FALSE, bStopped = FALSE, bThreadStarted = FALSE;
   PKWAIT_BLOCK pwbArray = NULL;
   LARGE_INTEGER checkRegInterval, currentTime, lastTime = {0};
   UNICODE_STRING ucValueName;
   ULONG debugMask, oldMask, driverResident;
   OBJECT_ATTRIBUTES oa;
   HANDLE hRegKey = NULL;
   KIRQL OldRdIrqLevel;
   KEVENT dummyEvent;
   short errCnt, errCnt0;
   NTSTATUS regIdleStatus = STATUS_SUCCESS;
   BOOLEAN  bSignalStopState = FALSE;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> INT2: wrong IRQL\n", pDevExt->PortName)
      );
      if (pDevExt->pInterruptBuffer != NULL)
      {
         ExFreePool(pDevExt->pInterruptBuffer);
         pDevExt->pInterruptBuffer = NULL;
      }
      _closeHandle(pDevExt->hInterruptThreadHandle, "I-1");
      return; // PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
   }

   if (pDevExt -> InterruptPipe != (UCHAR)-1)
   {
      siz = sizeof( struct _URB_BULK_OR_INTERRUPT_TRANSFER );
      pUrb = ExAllocatePool(NonPagedPool, siz);
      if (!pUrb) 
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> INT: NO_MEM Urb\n", pDevExt->PortName)
         );
         // fail and exit thread
         if (pDevExt->pInterruptBuffer != NULL)
         {
            ExFreePool(pDevExt->pInterruptBuffer);
            pDevExt->pInterruptBuffer = NULL;
         }
         _closeHandle(pDevExt->hInterruptThreadHandle, "I-2");
         return; // PsTerminateSystemThread(STATUS_NO_MEMORY);
      }

      // allocate an irp for the iocontrol call to the lower driver
      pIrp = IoAllocateIrp((CCHAR)(pDevExt->StackDeviceObject->StackSize+1), FALSE );
      pDevExt->InterruptIrp = pIrp;

      if(!pIrp)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> INT: NO_MEM Irp\n", pDevExt->PortName)
         );
         ExFreePool(pUrb);
         if (pDevExt->pInterruptBuffer != NULL)
         {
            ExFreePool(pDevExt->pInterruptBuffer);
            pDevExt->pInterruptBuffer = NULL;
         }
         _closeHandle(pDevExt->hInterruptThreadHandle, "I-3");
         return; // PsTerminateSystemThread(STATUS_NO_MEMORY);
      }
   }

   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (INT_PIPE_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 "ReadInterruptPipe.pwbArray"
              );
   if (!pwbArray)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> INT: NO_MEM 3\n", pDevExt->PortName)
      );
      if (pUrb != NULL)
      {
         ExFreePool(pUrb);
      }
      if (pIrp != NULL)
      {
         IoReuseIrp(pIrp, STATUS_SUCCESS);
         IoFreeIrp(pIrp);
      }
      if (pDevExt->pInterruptBuffer != NULL)
      {
         ExFreePool(pDevExt->pInterruptBuffer);
         pDevExt->pInterruptBuffer = NULL;
      }
      _closeHandle(pDevExt->hInterruptThreadHandle, "I-4");
      return; // PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   if (pDevExt->InterruptPipe != (UCHAR)-1)
   {
      KeSetPriorityThread(KeGetCurrentThread(), QCUSB_INT_PRIORITY);
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> INT: prio %d\n", pDevExt->PortName,
          KeQueryPriorityThread(KeGetCurrentThread()) )
      );
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_FORCE,
      ("<%s> I: ON-0x%x\n", pDevExt->PortName, pDevExt->InterruptPipe)
   );
   pDevExt->bIntActive  = FALSE;

   pDevExt->pInterruptPipeEvents[INT_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);

   KeClearEvent(&dummyEvent);
   KeClearEvent(&pDevExt->eInterruptCompletion);
   KeClearEvent(&pDevExt->CancelInterruptPipeEvent);
   KeClearEvent(&pDevExt->InterruptStopServiceEvent);
   KeClearEvent(&pDevExt->InterruptResumeServiceEvent);

   errCnt = errCnt0 = 0;
   InitializeObjectAttributes(&oa, &gServicePath, 0, NULL, NULL);
   while(bKeepRunning)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_VERBOSE,
         ("<%s> I: pipe 0x%x Stp 0x%x Iact %d Rml[0]=%u\n", pDevExt->PortName,
           pDevExt->InterruptPipe, bStopped, pDevExt->bIntActive, pDevExt->Sts.lRmlCount[0])
      );
      if (pDevExt->bIntActive == TRUE)
      {
         goto wait_for_completion;
      }

      if ((pDevExt -> InterruptPipe != (UCHAR)-1) && (bStopped == FALSE) &&
          (pDevExt->PowerSuspended == FALSE))
      {
         // setup the urb
         UsbBuildInterruptOrBulkTransferRequest
         (
            pUrb, 
            siz,
            pDevExt->Interface[pDevExt->usCommClassInterface]
               ->Pipes[pDevExt->InterruptPipe].PipeHandle,
            (PVOID) pDevExt -> pInterruptBuffer,
            NULL,
            pDevExt ->Interface[pDevExt->usCommClassInterface]
                    ->Pipes[pDevExt->InterruptPipe].MaximumPacketSize,
            USBD_SHORT_TRANSFER_OK,
            NULL
         );

         // and init the irp stack for the lower level driver
         IoReuseIrp(pIrp, STATUS_SUCCESS);

         nextstack = IoGetNextIrpStackLocation( pIrp );
         nextstack->Parameters.Others.Argument1 = pUrb;
         nextstack->Parameters.DeviceIoControl.IoControlCode =
            IOCTL_INTERNAL_USB_SUBMIT_URB;
         nextstack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
         IoSetCompletionRoutine
         (
            pIrp, 
            (PIO_COMPLETION_ROUTINE) InterruptPipeCompletion, 
            (PVOID)pDevExt, 
            TRUE,
            TRUE,
            TRUE
         );
         // IoSetCancelRoutine(pIrp, NULL); // DV?

         QCUSB_DbgPrint
         (
            (QCUSB_DBG_MASK_READ | QCUSB_DBG_MASK_ENC),
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> Ith: IoCallDriver IRP 0x%p", pDevExt->PortName, pIrp)
         );
         pDevExt->bIntActive = TRUE;
         ntStatus = IoCallDriver(pDevExt->StackDeviceObject, pIrp );
         if(!NT_SUCCESS(ntStatus))
         {
            QCUSB_DbgPrint
            (
               (QCUSB_DBG_MASK_READ | QCUSB_DBG_MASK_ENC),
               QCUSB_DBG_LEVEL_CRITICAL,
               ("<%s> Ith: IoCallDriver rtn 0x%x - %d", pDevExt->PortName, ntStatus, errCnt0)
            );
            ++errCnt0;
            if (errCnt0 >= 3)
            {
               errCnt0 = 3;
               bStopped = TRUE;
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_CRITICAL,
                  ("<%s> Ith: IoCallDriver failed, stopped", pDevExt->PortName)
               );
            }
         }
         else
         {
            QCUSB_DbgPrint
            (
               (QCUSB_DBG_MASK_READ | QCUSB_DBG_MASK_ENC),
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: IoCallDriver rtn 0x%x - Iact %d", pDevExt->PortName, ntStatus, pDevExt->bIntActive)
            );
            errCnt0 = 0;
         }
      }  // if (pDevExt -> InterruptPipe != (UCHAR)-1)

     // No matter what IoCallDriver returns, we always wait on the kernel event
     // we created earlier. Our completion routine will gain control when the IRP
     // completes to signal this event. -- Walter Oney's WDM book page 228
wait_for_completion:

      // QCUSB_DbgPrint2
      // (
      //    QCUSB_DBG_MASK_READ,
      //    QCUSB_DBG_LEVEL_DETAIL,
      //    ("<%s> Ith: Wait...\n", pDevExt->PortName)
      // );

      if (bThreadStarted == FALSE)
      {
         bThreadStarted = TRUE;
         KeSetEvent(&pDevExt->IntThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      }

      checkRegInterval.QuadPart = -(50 * 1000 * 1000);   // 5.0 sec
      ntStatus = KeWaitForMultipleObjects
                 (
                    INT_PIPE_EVENT_COUNT,
                    (VOID **)&pDevExt->pInterruptPipeEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,             // non-alertable
                    &checkRegInterval,
                    pwbArray
                 );
      KeQuerySystemTime(&currentTime);
      if (ntStatus != STATUS_TIMEOUT)
      {
         if ((currentTime.QuadPart - lastTime.QuadPart) >= 50000000L)
         {
            lastTime.QuadPart = currentTime.QuadPart;
            KeSetEvent(&dummyEvent, IO_NO_INCREMENT, FALSE);
         }
      }

      switch (ntStatus)
      {
         case INT_COMPLETION_EVENT_INDEX:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: INT_COMPLETION 0x%p (ST 0x%x)\n", pDevExt->PortName, pIrp, pIrp->IoStatus.Status)
            );

            KeClearEvent(&pDevExt->eInterruptCompletion);
            pDevExt->bIntActive = FALSE;  // set in completion

            if (bSignalStopState == TRUE)
            {
               bSignalStopState = FALSE;
               KeSetEvent
               (
                  &pDevExt->InterruptStopServiceRspEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
            }
            if ((pUrb->UrbHeader.Status == USBD_STATUS_DEVICE_GONE) ||
                (pUrb->UrbHeader.Status == USBD_STATUS_XACT_ERROR)  ||
                (pIrp->IoStatus.Status  == STATUS_NO_SUCH_DEVICE))
            {
               pDevExt->IntPipeStatus = STATUS_NO_SUCH_DEVICE;
            }
            else
            {
               pDevExt->IntPipeStatus = pIrp->IoStatus.Status;
            }

            if (bCancelled == TRUE)
            {
               bKeepRunning = FALSE;
               break;  // just exit
            }
            if (pDevExt->InterruptPipe == (UCHAR)-1)
            {
               break;
            }
            else if (bStopped == TRUE)
            {
               // pDevExt->InterruptPipe = (UCHAR)(-1);
               break;
            }

            // check status of completed urb
            if (USBD_ERROR(pUrb->UrbHeader.Status) ||
                (!NT_SUCCESS(pIrp->IoStatus.Status)))
            {
               ++errCnt;
               QCUSB_DbgPrint
               (
                  (QCUSB_DBG_MASK_READ | QCUSB_DBG_MASK_ENC),
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> INT: URB error 0x%x/0x%x (%d)\n", pDevExt->PortName,
                   pUrb->UrbHeader.Status, pIrp->IoStatus.Status, errCnt)
               );

               if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
               {
                  // if (pDevExt->PowerSuspended == TRUE)
                  {
                     QCUSB_DbgPrint
                     (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> Ith: ResetPipe\n", pDevExt->PortName)
                     );
                     QCUSB_ResetInt(pDevExt->MyDeviceObject, QCUSB_RESET_HOST_PIPE);
                  }
               }

               if ((errCnt >= 3) || (pIrp->IoStatus.Status == STATUS_DEVICE_NOT_CONNECTED))
               {
                  errCnt = 3;
                  bStopped = TRUE;
               }
               USBUTL_Wait(pDevExt, -(40 * 1000));  // 4ms
               break; // bail on error
            }
            else
            {
               errCnt = 0;
            }

            USBUTL_PrintBytes
            (
               pDevExt->pInterruptBuffer,
               128,
               pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
               "INTData",
               pDevExt,
               QCUSB_DBG_MASK_RDATA,
               QCUSB_DBG_LEVEL_VERBOSE
            );

            if (pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength < 
                (sizeof(USB_NOTIFICATION_STATUS)-sizeof(USHORT)))
            {
               QCUSB_DbgPrint
               (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> Ith: bad data\n", pDevExt->PortName)
               );
               break;
            }

            pNotificationStatus = (PUSB_NOTIFICATION_STATUS)pDevExt->pInterruptBuffer ;

            switch (pNotificationStatus->bNotification)
            {
               case CDC_NOTIFICATION_SERIAL_STATE:
                  USBINT_HandleSerialStateNotification(pDevExt);
                  break;
               case CDC_NOTIFICATION_RESPONSE_AVAILABLE:
                  USBINT_HandleResponseAvailableNotification(pDevExt);
                  break;
               case CDC_NOTIFICATION_NETWORK_CONNECTION:
                  USBINT_HandleNetworkConnectionNotification(pDevExt);
                  break;
               case CDC_NOTIFICATION_CONNECTION_SPD_CHG:
                  USBINT_HandleConnectionSpeedChangeNotification(pDevExt);
                  break;
               default:
                  break;
            }

            break;
         } // end of INT_COMPLETION_EVENT_INDEX
         
         case INT_STOP_SERVICE_EVENT:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: STOP Iact %d\n", pDevExt->PortName, pDevExt->bIntActive)
            );

            KeClearEvent(&pDevExt->InterruptStopServiceEvent);
            bStopped = TRUE;

            if (pDevExt->bIntActive == TRUE)
            {
               // we need to cancel the outstanding read
               IoCancelIrp(pIrp);
               bSignalStopState = TRUE;
               goto wait_for_completion;
            }
            else
            {
               KeSetEvent
               (
                  &pDevExt->InterruptStopServiceRspEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
            }
            break;
         }
         case INT_RESUME_SERVICE_EVENT:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: RESUME\n", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->InterruptResumeServiceEvent);
            bStopped = FALSE;
            pDevExt->IntPipeStatus = STATUS_SUCCESS;
            QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 5);
            break;
         }

         case INT_EMPTY_RD_QUEUE_EVENT_INDEX:
         {
            QCUSB_DbgPrint2
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: EMPTY_RD_QUEUE\n", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->InterruptEmptyRdQueueEvent);
            QcEmptyCompletionQueue
            (
               pDevExt,
               &pDevExt->RdCompletionQueue,
               &pDevExt->ReadSpinLock,
               QCUSB_IRP_TYPE_RIRP
            );
            goto wait_for_completion;;
         }
         case INT_EMPTY_WT_QUEUE_EVENT_INDEX:
         {
            QCUSB_DbgPrint2
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: EMPTY_WT_QUEUE\n", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->InterruptEmptyWtQueueEvent);
            QcEmptyCompletionQueue
            (
               pDevExt,
               &pDevExt->WtCompletionQueue,
               &pDevExt->WriteSpinLock,
               QCUSB_IRP_TYPE_WIRP
            );
            goto wait_for_completion;;
         }
         case INT_EMPTY_CTL_QUEUE_EVENT_INDEX:
         {
            QCUSB_DbgPrint2
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: EMPTY_CTL_QUEUE\n", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->InterruptEmptyCtlQueueEvent);
            QcEmptyCompletionQueue
            (
               pDevExt,
               &pDevExt->CtlCompletionQueue,
               &pDevExt->ControlSpinLock,
               QCUSB_IRP_TYPE_CIRP
            );
            goto wait_for_completion;;
         }
         case INT_EMPTY_SGL_QUEUE_EVENT_INDEX:
         {
            QCUSB_DbgPrint2
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: EMPTY_SGL_QUEUE\n", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->InterruptEmptySglQueueEvent);
            QcEmptyCompletionQueue
            (
               pDevExt,
               &pDevExt->SglCompletionQueue,
               &pDevExt->SingleIrpSpinLock,
               QCUSB_IRP_TYPE_CIRP
            );
            goto wait_for_completion;;
         }

         case CANCEL_EVENT_INDEX:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: CANCEL Iact %d\n", pDevExt->PortName, pDevExt->bIntActive)
            );

            KeClearEvent(&pDevExt->CancelInterruptPipeEvent);
            bCancelled = TRUE;

            if (pDevExt->bIntActive == TRUE)
            {
               // we need to cancel the outstanding read
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> Ith: IoCancelIrp\n", pDevExt->PortName)
               );
               IoCancelIrp(pIrp);
               goto wait_for_completion;
            }
            bKeepRunning = FALSE;
            break;
         }

         case INT_REG_IDLE_NOTIF_EVENT_INDEX:
         {
            // Register Idle Notification Request
            if (regIdleStatus == STATUS_DELETE_PENDING)
            {
               KeClearEvent(&pDevExt->InterruptRegIdleEvent);
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_PIRP,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> Ith: REG IDLE: DELETE_PENDING\n", pDevExt->PortName)
               );
            }
            else if (pDevExt->IntPipeStatus == STATUS_NO_SUCH_DEVICE)
            {
               KeClearEvent(&pDevExt->InterruptRegIdleEvent);
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_PIRP,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> Ith: REG IDLE: NO_SUCH_DEVICE\n", pDevExt->PortName)
               );
            }
            else
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_PIRP,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> Ith: REG IDLE\n", pDevExt->PortName)
               );
               regIdleStatus = QCPWR_RegisterIdleNotification(pDevExt);
            }

            break;
         }

         case INT_IDLE_EVENT:
         {
            KeClearEvent(&(pDevExt->InterruptIdleEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: IDLE\n", pDevExt->PortName)
            );
            pDevExt->IdleCallbackInProgress = 1;
            break;
         }

         case INT_IDLENESS_COMPLETED_EVENT:
         {
            KeClearEvent(&(pDevExt->InterruptIdlenessCompletedEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: IDLE_COMPLETED\n", pDevExt->PortName)
            );
            if (pDevExt->IdleCallbackInProgress != 0)
            {
               // delay power up until idle callback completed
               pDevExt->bIdleIrpCompleted = TRUE;
            }
            else
            {
               QCPWR_GetOutOfIdleState(pDevExt);
            }
            break;
         }

         case INT_IDLE_CBK_COMPLETED_EVENT:
         {
            KeClearEvent(&(pDevExt->IdleCbkCompleteEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Ith: IDLE_CBK_COMPLETED\n", pDevExt->PortName)
            );

            if (pDevExt->bIdleIrpCompleted == TRUE)
            {
               pDevExt->bIdleIrpCompleted = FALSE;
               QCPWR_GetOutOfIdleState(pDevExt);
            }
            pDevExt->IdleCallbackInProgress = 0;

            break;
         }

         case INT_DUMMY_EVENT_INDEX:
         case STATUS_TIMEOUT:
         default:
         {
            // unconditionally clears the dummyEvent
            KeClearEvent(&dummyEvent);
            lastTime.QuadPart = currentTime.QuadPart;
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_STATE,
               QCUSB_DBG_LEVEL_CRITICAL,
               ("<%s> STA: svc %u Rstp %u/%u Ron %u/%u Won %u Srm %u RM %u ST 0x%x Susp %u\n",
                 pDevExt->PortName,
                 pDevExt->bInService,
                 pDevExt->bL1Stopped,     pDevExt->bL2Stopped,
                 pDevExt->bL1ReadActive,  pDevExt->bL2ReadActive,
                 pDevExt->bWriteActive,   pDevExt->bDeviceSurpriseRemoved,
                 pDevExt->bDeviceRemoved, pDevExt->bmDevState,
                 pDevExt->PowerSuspended
               )
            );
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_STATE,
               QCUSB_DBG_LEVEL_CRITICAL,
               ("<%s> STA: [%s] RML %ld %ld %ld %ld RW %ld %ld PWR %u/%u SS %us/%u OSR %d\n",
                 pDevExt->PortName, gVendorConfig.DriverVersion,
                 pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
                 pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
                 pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedWrites,
                 pDevExt->DevicePower, pDevExt->SystemPower,
                 pDevExt->SelectiveSuspendIdleTime, pDevExt->InServiceSelectiveSuspension,
                 pDevExt->Sts.lOversizedReads)
            );

            if (bStopped == TRUE)
            {
               // We stopped accessing registry since the PDO is probably
               // obsolete by now
               goto wait_for_completion;
            }
            #ifndef NDIS_WDM

            ntStatus2 = IoOpenDeviceRegistryKey
                        (
                           pDevExt->PhysicalDeviceObject,
                           PLUGPLAY_REGKEY_DRIVER,
                           KEY_ALL_ACCESS,
                           &hRegKey
                        );
            if (!NT_SUCCESS(ntStatus2))
            {
               _closeRegKey(hRegKey, "I-0");
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> INT: reg ERR\n", pDevExt->PortName)
               );
               break; // ignore failure
            }

            RtlInitUnicodeString(&ucValueName, VEN_DBG_MASK);
            ntStatus2 = getRegDwValueEntryData
                        (
                           hRegKey,
                           ucValueName.Buffer,
                           &debugMask
                        );
            _closeRegKey(hRegKey, "I-1");

            if (ntStatus2 == STATUS_SUCCESS)
            {
               oldMask = gVendorConfig.DebugMask;

               #ifdef DEBUG_MSGS
               gVendorConfig.DebugMask = debugMask = 0xFFFFFFFF;
               #else
               gVendorConfig.DebugMask = debugMask;
               #endif

               gVendorConfig.DebugLevel = (UCHAR)(debugMask & 0x0F);
               pDevExt->DebugMask = debugMask;
               pDevExt->DebugLevel = (UCHAR)(debugMask & 0x0F);

               if (debugMask != oldMask)
               {
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_READ,
                     QCUSB_DBG_LEVEL_ERROR,
                     ("<%s> INT: DebugMask 0x%x\n", pDevExt->PortName, debugMask)
                  );
               }
            }

            // Get driver resident state
            ntStatus2 = ZwOpenKey
                        (
                           &hRegKey,
                           KEY_READ,
                           &oa
                        );
            if (!NT_SUCCESS(ntStatus2))
            {
               _closeRegKey(hRegKey, "I-0a");
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> INT: reg srv ERR\n", pDevExt->PortName)
               );
               break; // ignore failure
            }

            RtlInitUnicodeString(&ucValueName, VEN_DRV_RESIDENT);
            ntStatus2 = getRegDwValueEntryData
                        (
                           hRegKey,
                           ucValueName.Buffer,
                           &driverResident
                        );

            if (ntStatus2 == STATUS_SUCCESS)
            {
               if (driverResident != gVendorConfig.DriverResident)
               {
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_READ,
                     QCUSB_DBG_LEVEL_ERROR,
                     ("<%s> INT: DriverResident 0x%x\n", pDevExt->PortName, driverResident)
                  );
                  gVendorConfig.DriverResident = driverResident;
               }
            }
            _closeRegKey(hRegKey, "I-1a");

            #endif  // NDIS_WDM

            // if ntStatus is unexpected after a cancel event, we still
            // need to go back to wait for the final completion
            if ((ntStatus != STATUS_TIMEOUT) && (ntStatus != INT_DUMMY_EVENT_INDEX))
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> INT: unexpected evt-0x%x\n", pDevExt->PortName, ntStatus)
               );
               goto wait_for_completion;
            }

            break;
         }
      } // end switch

      if (ntStatus == INT_REG_IDLE_NOTIF_EVENT_INDEX)
      {
         USBUTL_Wait(pDevExt, -(10 * 1000));  // 1ms
      }
   } // end while keep running

   if (pUrb != NULL)
   {
      ExFreePool( pUrb );
      pUrb = NULL;
   }
   if (pIrp != NULL)
   {
      IoReuseIrp(pIrp, STATUS_SUCCESS);
      IoFreeIrp(pIrp); // free irp 
   }
   if(pwbArray != NULL)
   {
      _ExFreePool(pwbArray);
   }

   if(pDevExt->pInterruptBuffer != NULL)
   {
      ExFreePool(pDevExt->pInterruptBuffer);
      pDevExt->pInterruptBuffer = NULL;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_FORCE,
      ("<%s> Ith: OUT (0x%x)-0x%x\n", pDevExt->PortName, ntStatus, pDevExt->InterruptPipe)
   );

   KeSetEvent(&pDevExt->InterruptPipeClosedEvent, IO_NO_INCREMENT, FALSE );

   return; // PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}

NTSTATUS InterruptPipeCompletion
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP           pIrp,
   IN PVOID          pContext
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pContext;
   KIRQL OldRdIrqLevel;

   KeSetEvent( &pDevExt->eInterruptCompletion, IO_NO_INCREMENT, FALSE );

   QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 2); // INT completion

   return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS USBINT_CancelInterruptThread(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER delayValue;
   PVOID intThread;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> INT Cxl - %d\n", pDevExt->PortName, cookie)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> INT Cxl: wrong IRQL - %d\n", pDevExt->PortName, cookie)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (InterlockedIncrement(&pDevExt->InterruptThreadCancelStarted) > 1)
   {
      while ((pDevExt->hInterruptThreadHandle != NULL) || (pDevExt->pInterruptThread != NULL))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> Ith cxl in pro\n", pDevExt->PortName)
         );
         USBUTL_Wait(pDevExt, -(3 * 1000 * 1000));
      }
      InterlockedDecrement(&pDevExt->InterruptThreadCancelStarted);      
      return STATUS_SUCCESS;
   }

   if ((pDevExt->hInterruptThreadHandle != NULL) || (pDevExt->pInterruptThread != NULL))
   {
      KeClearEvent(&pDevExt->InterruptPipeClosedEvent);
      KeSetEvent
      (
         &pDevExt->CancelInterruptPipeEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pDevExt->pInterruptThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pDevExt->pInterruptThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pDevExt->pInterruptThread);
         KeClearEvent(&pDevExt->InterruptPipeClosedEvent);
         _closeHandle(pDevExt->hInterruptThreadHandle, "I-5");
         pDevExt->pInterruptThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pDevExt->InterruptPipeClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pDevExt->InterruptPipeClosedEvent);
         _closeHandle(pDevExt->hInterruptThreadHandle, "I-6");
      }
   }
   InterlockedDecrement(&pDevExt->InterruptThreadCancelStarted);      

   return ntStatus;
} // USBINT_CancelInterruptThread

NTSTATUS USBINT_StopInterruptService
(
   PDEVICE_EXTENSION pDevExt,
   BOOLEAN           bWait,
   BOOLEAN           CancelWaitWake,
   UCHAR             cookie
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> INT stop - %d\n", pDevExt->PortName, cookie)
   );

   KeClearEvent(&pDevExt->InterruptStopServiceEvent);
   KeClearEvent(&pDevExt->InterruptStopServiceRspEvent);
   KeSetEvent
   (
      &pDevExt->InterruptStopServiceEvent,
      IO_NO_INCREMENT,
      FALSE
   );

   if (bWait == TRUE)
   {
      KeWaitForSingleObject
      (
         &pDevExt->InterruptStopServiceRspEvent,
         Executive,
         KernelMode,
         FALSE,
         NULL
      );
      KeClearEvent(&pDevExt->InterruptStopServiceRspEvent);
   }

   #ifdef QCUSB_PWR
   // for STOP/SURPRISE_REMOVAL/REMOVE_DEVICE
   // CancelWaitWake is FALSE when called for power management
   QCPWR_CancelIdleTimer(pDevExt, 0, CancelWaitWake, 0);
   if (CancelWaitWake == TRUE)
   {
      QCPWR_CancelWaitWakeIrp(pDevExt, 3);   // for STOP/SURPRISE_REMOVAL/REMOVE_DEVICE
   }
   #endif // QCUSB_PWR

   return ntStatus;
} // USBINT_StopInterruptService

NTSTATUS USBINT_ResumeInterruptService(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> INT resume - %d\n", pDevExt->PortName, cookie)
   );

   if (inDevState(DEVICE_STATE_SURPRISE_REMOVED) ||
       inDevState(DEVICE_STATE_DEVICE_REMOVED0))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> INT resume - dev removed, no act\n", pDevExt->PortName)
      );
      return ntStatus;
   }

   KeClearEvent(&pDevExt->InterruptResumeServiceEvent);
   KeSetEvent
   (
      &pDevExt->InterruptResumeServiceEvent,
      IO_NO_INCREMENT,
      FALSE
   );

   return ntStatus;
} // USBINT_ResumeInterruptService

VOID USBINT_HandleSerialStateNotification(PDEVICE_EXTENSION pDevExt)
{
   PUSB_NOTIFICATION_STATUS pUartStatus;
   USHORT usStatusBits;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> USBINT_ProcessSerialState\n", pDevExt->PortName)
   );

   usStatusBits = 0;
   pUartStatus = (PUSB_NOTIFICATION_STATUS)pDevExt->pInterruptBuffer ;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> INTSerSt: Req 0x%x Noti 0x%x Val 0x%x Idx 0x%x Len %d usVal 0x%x\n",
        pDevExt->PortName, pUartStatus->bmRequestType, pUartStatus->bNotification,
        pUartStatus->wValue, pUartStatus->wIndex, pUartStatus->wLength,
        pUartStatus->usValue)
   );

   if (pUartStatus->usValue & USB_CDC_INT_RX_CARRIER)
   {
      usStatusBits |= SERIAL_EV_RLSD; // carrier-detection
   }
   if (pUartStatus->usValue & USB_CDC_INT_TX_CARRIER)
   {
      usStatusBits |= SERIAL_EV_DSR;  // data-set-ready
   }
   if (pUartStatus->usValue & USB_CDC_INT_BREAK)
   {
      usStatusBits |= SERIAL_EV_BREAK;  // break
   }
   if (pUartStatus->usValue & USB_CDC_INT_RING)
   {
      usStatusBits |= SERIAL_EV_RING;  // ring-detection
   }
   if (pUartStatus->usValue & USB_CDC_INT_FRAME_ERROR)
   {
      usStatusBits |= SERIAL_EV_ERR;  // line-error
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> Interrupt: USB frame err\n", pDevExt->PortName)
      );
   }
   if (pUartStatus->usValue & USB_CDC_INT_PARITY_ERROR)
   {
      usStatusBits |= SERIAL_EV_ERR;  // line-error
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> Interrupt: USB parity err\n", pDevExt->PortName)
      );
   }
}  // USBINT_HandleSerialStateNotification

VOID USBINT_HandleNetworkConnectionNotification(PDEVICE_EXTENSION pDevExt)
{
   PUSB_NOTIFICATION_STATUS pNetCon;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> INTNetworkConnection\n", pDevExt->PortName)
   );

   pNetCon = (PUSB_NOTIFICATION_STATUS)pDevExt->pInterruptBuffer ;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> INTNetCon: Req 0x%x Noti 0x%x Val 0x%x Idx 0x%x\n",
        pDevExt->PortName, pNetCon->bmRequestType, pNetCon->bNotification,
        pNetCon->wValue, pNetCon->wIndex)
   );

   if (pNetCon->wValue == 0)
   {
      // Disconnected
      USBIF_NotifyLinkDown(pDevExt->MyDeviceObject);
      USBUTL_DriverRegistrySetDword(pDevExt, VEN_DEV_SSR, 1);  // SSR happens
   }
   else
   {
       USBUTL_DriverRegistrySetDword(pDevExt, VEN_DEV_SSR, 0);  // no SSR
      // Connected
   }
}  // USBINT_HandleNetworkConnectionNotification

VOID USBINT_HandleResponseAvailableNotification(PDEVICE_EXTENSION pDevExt)
{
   PLONG pRspAvailableCnt;
   PKEVENT pReadCtlEvent;
   PUSB_NOTIFICATION_STATUS pNotification;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pNotification = (PUSB_NOTIFICATION_STATUS)pDevExt->pInterruptBuffer;

   // Need to notify dispatch thread to issue GetEncapsulatedResponse
   if (pNotification->wIndex < MAX_INTERFACE)
   {
      QcAcquireSpinLock(&gGenSpinLock, &levelOrHandle);

      pReadCtlEvent = pDevExt->pReadControlEvent;
      pRspAvailableCnt = pDevExt->pRspAvailableCount;

      #ifdef QCUSB_SHARE_INTERRUPT
      pRspAvailableCnt = USBSHR_GetRspAvailableCount(pDevExt->MyDeviceObject, pNotification->wIndex);
      pReadCtlEvent = USBSHR_GetReadControlEvent(pDevExt->MyDeviceObject, pNotification->wIndex);
      if ((pRspAvailableCnt == NULL) || (pReadCtlEvent == NULL))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_ENC,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> INTRespAvail-f: failed to get cnt/evt(%d)\n",
              pDevExt->PortName, pNotification->wIndex)
         );
         QcReleaseSpinLock(&gGenSpinLock, levelOrHandle);
         return;
      }
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_ENC,
         QCUSB_DBG_LEVEL_INFO,
         ("<%s> INTRespAvail-shr: evt add (0x%p, 0x%p) IF %d\n",
           pDevExt->PortName, pReadCtlEvent, pDevExt->pReadControlEvent,
           pNotification->wIndex)
      );

      #endif // QCUSB_SHARE_INTERRUPT

      InterlockedIncrement(pRspAvailableCnt);
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_ENC,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> INTRespAvail-s: Req 0x%x Noti 0x%x Idx(IF) 0x%x Cnt=%d\n",
           pDevExt->PortName, pNotification->bmRequestType,
           pNotification->bNotification, pNotification->wIndex,
           *pRspAvailableCnt)
      );

      QcReleaseSpinLock(&gGenSpinLock, levelOrHandle);

      KeSetEvent
      (
         pReadCtlEvent,
         IO_NO_INCREMENT,
         FALSE
      );
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_ENC,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> INTRespAvail: IF out of range %d\n", pDevExt->PortName, pNotification->wIndex)
      );
   }

}  // USBINT_HandleResponseAvailableNotification

VOID USBINT_HandleConnectionSpeedChangeNotification(PDEVICE_EXTENSION pDevExt)
{
   PUSB_NOTIFICATION_STATUS pNotification;
   PUSB_NOTIFICATION_CONNECTION_SPEED pConSpd;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> INTConSpeedChange\n", pDevExt->PortName)
   );

   pNotification = (PUSB_NOTIFICATION_STATUS)pDevExt->pInterruptBuffer;
   pConSpd = (PUSB_NOTIFICATION_CONNECTION_SPEED)&pNotification->usValue;

   QCUSB_DbgPrint
   (
      (QCUSB_DBG_MASK_READ | QCUSB_DBG_MASK_WRITE | QCUSB_DBG_MASK_CONTROL),
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> INTConSpdChange: Req 0x%x Noti 0x%x Idx(IF) 0x%x Len %d US %u DS %u\n",
        pDevExt->PortName, pNotification->bmRequestType,
        pNotification->bNotification, pNotification->wIndex, pNotification->wLength,
        pConSpd->ulUSBitRate, pConSpd->ulDSBitRate)
   );

   if (pConSpd->ulUSBitRate == 0)
   {
      // read flow off
      USBRD_SetStopState(pDevExt, TRUE, 0);
   }
   else
   {
      // read flow on
      USBRD_SetStopState(pDevExt, FALSE, 1);
   }

   if (pConSpd->ulDSBitRate == 0)
   {
      NTSTATUS ntStatus;
      LARGE_INTEGER delayValue;

      // write flow off
      KeClearEvent(&pDevExt->WriteFlowOffAckEvent);
      KeSetEvent(&pDevExt->WriteFlowOffEvent, IO_NO_INCREMENT, FALSE);
      delayValue.QuadPart = -(100 * 1000 * 1000); // 10.0 sec
      ntStatus = KeWaitForSingleObject
                 (
                    &pDevExt->WriteFlowOffAckEvent,
                    Executive,
                    KernelMode,
                    FALSE,
                    &delayValue
                 );
      if (ntStatus == STATUS_TIMEOUT)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> INT: timeout -- W flow off\n", pDevExt->PortName)
         );
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_WRITE,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> INT: W flow off\n", pDevExt->PortName)
         );
      }
   }
   else
   {
      // write flow on
      KeSetEvent(&pDevExt->WriteFlowOnEvent, IO_NO_INCREMENT, FALSE);
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_WRITE,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> INT: W flow on\n", pDevExt->PortName)
      );
   }

}  // USBINT_HandleConnectionSpeedChangeNotification

