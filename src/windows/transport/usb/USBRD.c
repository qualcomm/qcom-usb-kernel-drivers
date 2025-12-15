/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                            U S B R D . C

GENERAL DESCRIPTION
  This file contains implementation for reading data over USB.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include "USBRD.h"
#include "USBUTL.h"
#include "USBENC.h"
#include "USBPWR.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBRD.tmh"

#endif   // EVENT_TRACING

extern USHORT ucThCnt;

#undef QCOM_TRACE_IN

// The following protypes are implemented in ntoskrnl.lib
extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);

NTSTATUS USBRD_Read(IN PDEVICE_OBJECT DeviceObject, IN PIRP pIrp)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   BOOLEAN status;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   if (DeviceObject == NULL)
   {
      QCUSB_DbgPrintG
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> RIRP: 0x%p (No Port for 0x%p)\n", gDeviceName, pIrp, DeviceObject)
      );
      USBUTL_CompleteRequest(pIrp, STATUS_DELETE_PENDING, 0);
      return STATUS_DELETE_PENDING;
   }

   pDevExt = DeviceObject->DeviceExtension;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_RIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s 0X%p> RIRP 0x%p => t 0x%p\n", pDevExt->PortName, DeviceObject, pIrp,
        KeGetCurrentThread())
   );

   ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, pIrp);
   if (!NT_SUCCESS(ntStatus))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_RIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> RIRP (Crm 0x%x/%ldB) 0x%p\n", pDevExt->PortName, ntStatus,
          pIrp->IoStatus.Information, pIrp)
      );
      USBUTL_CompleteRequest(pIrp, ntStatus, 0);
      return ntStatus;
   }
   InterlockedIncrement(&(pDevExt->Sts.lRmlCount[1]));

   pIrp->IoStatus.Information = 0;

   if ((gVendorConfig.DriverResident == 0) && (pDevExt->bInService == FALSE))
   {
      if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
      {
         ntStatus = STATUS_DELETE_PENDING;
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_RIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> RIRP (Cdp 0x%x/%ldB) 0x%p No DEV\n", pDevExt->PortName,
             ntStatus, pIrp->IoStatus.Information, pIrp)
         );
         goto Exit;
      }
      if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
      {
         ntStatus = STATUS_DELETE_PENDING;
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_RIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> RIRP (Cdp 0x%x/%ldB) 0x%p No DEV-2\n", pDevExt->PortName,
              ntStatus, pIrp->IoStatus.Information, pIrp)
         );
         goto Exit;
      } 
   }  // if driver not resident

/*****
   if (pDevExt->Sts.lRmlCount[0] <= 0 && pDevExt->bInService == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> _Read: AC:RML_COUNTS=<%ld,%ld,%ld,%ld>\n",
           pDevExt->PortName,pDevExt->Sts.lRmlCount[0],pDevExt->Sts.lRmlCount[1],pDevExt->Sts.lRmlCount[2],pDevExt->Sts.lRmlCount[3]
         )
      );
      #ifdef DEBUG_MSGS
      KdBreakPoint();
      #endif

      ntStatus = STATUS_DELETE_PENDING;
      goto Exit;
   }
*****/

   if (pDevExt->bL1InCancellation == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> RD in cancellation\n", pDevExt->PortName)
      );
      ntStatus = STATUS_CANCELLED;
      goto Exit;
   }

   // Enqueue the Irp
   ntStatus = USBRD_Enqueue(pDevExt, pIrp, 1);

Exit:
   if (ntStatus != STATUS_PENDING)
   {
      pIrp->IoStatus.Status = ntStatus;
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_RIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> RIRP (Ce %ldB) 0x%p\n", pDevExt->PortName,
          pIrp->IoStatus.Information, pIrp)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 1);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
   }

   return ntStatus;
}  // USBRD_Read

NTSTATUS USBRD_CancelReadThread(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   NTSTATUS ntStatus;
   LARGE_INTEGER delayValue;
   PVOID readThread;
   
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_WRITE,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> USBRD_CancelReadThread %d\n", pDevExt->PortName, cookie)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> USBRD_CancelReadThread: wrong IRQL - %d\n", pDevExt->PortName, cookie)
      );

      // best effort
      KeSetEvent(&pDevExt->L1CancelReadEvent,IO_NO_INCREMENT,FALSE);
      return STATUS_UNSUCCESSFUL;
   }

   if (InterlockedIncrement(&pDevExt->ReadThreadCancelStarted) > 1)
   {
      while ((pDevExt->hL1ReadThreadHandle != NULL) || (pDevExt->pL1ReadThread != NULL))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> Rth cxl in pro\n", pDevExt->PortName)
         );
         USBUTL_Wait(pDevExt, -(3 * 1000 * 1000L));  // 0.3 second
      }
      InterlockedDecrement(&pDevExt->ReadThreadCancelStarted);      
      return STATUS_SUCCESS;
   }

   if ((pDevExt->hL1ReadThreadHandle != NULL) || (pDevExt->pL1ReadThread != NULL))
   {
      KeClearEvent(&pDevExt->L1ReadThreadClosedEvent);
      KeSetEvent(&pDevExt->L1CancelReadEvent,IO_NO_INCREMENT,FALSE);
   
      if (pDevExt->pL1ReadThread != NULL)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> USBRD_CancelReadThread: Wait for L1 (%d)\n", pDevExt->PortName, cookie)
         );
         ntStatus = KeWaitForSingleObject
                    (
                       pDevExt->pL1ReadThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pDevExt->pL1ReadThread);
         KeClearEvent(&pDevExt->L1ReadThreadClosedEvent);
         _closeHandle(pDevExt->hL1ReadThreadHandle, "R-0");
         pDevExt->pL1ReadThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pDevExt->L1ReadThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pDevExt->L1ReadThreadClosedEvent);
         _closeHandle(pDevExt->hL1ReadThreadHandle, "R-0");
      }
   }

   InterlockedDecrement(&pDevExt->ReadThreadCancelStarted);      

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> USBRD_CancelReadThread: L1 OUT (%d)\n", pDevExt->PortName, cookie)
   );

   return STATUS_SUCCESS;
} // USBRD_CancelReadThread

NTSTATUS USBRD_Enqueue(PDEVICE_EXTENSION pDevExt, PIRP pIrp, UCHAR cookie)
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

   // has the read thread started?

   if ((pDevExt->hL1ReadThreadHandle == NULL) && (pDevExt->pL1ReadThread == NULL) &&
       inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      // Make sure the read thread is created when IRQL==PASSIVE_LEVEL
      if (irql > PASSIVE_LEVEL)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> _R: wrong IRQL\n", pDevExt->PortName)
         );
         return STATUS_UNSUCCESSFUL;
      }
      KeClearEvent(&pDevExt->ReadThreadStartedEvent);

      QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
      if (InterlockedIncrement(&pDevExt->ReadThreadInCreation) > 1)
      {
         QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
         goto L1_Enqueue;
      }
      else
      {
         QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
      }

      InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
      ucThCnt++;

      if (pDevExt->UseMultiReads == TRUE)
      {
         ntStatus = PsCreateSystemThread
                    (
                       OUT &pDevExt->hL1ReadThreadHandle,
                       IN THREAD_ALL_ACCESS,
                       IN &objAttr, // POBJECT_ATTRIBUTES
                       IN NULL,     // HANDLE  ProcessHandle
                       OUT NULL,    // PCLIENT_ID  ClientId
                       IN (PKSTART_ROUTINE)USBMRD_L1MultiReadThread,
                       IN (PVOID) pDevExt
                    );
      }
      else
      {
         ntStatus = PsCreateSystemThread
                    (
                       OUT &pDevExt->hL1ReadThreadHandle,
                       IN THREAD_ALL_ACCESS,
                       IN &objAttr, // POBJECT_ATTRIBUTES
                       IN NULL,     // HANDLE  ProcessHandle
                       OUT NULL,    // PCLIENT_ID  ClientId
                       IN (PKSTART_ROUTINE)USBRD_ReadThread,
                       IN (PVOID) pDevExt
                    );
      }
      if ((ntStatus != STATUS_SUCCESS) || (pDevExt->hL1ReadThreadHandle == NULL))
      {
         pDevExt->pL1ReadThread = NULL;
         pDevExt->hL1ReadThreadHandle = NULL;
         InterlockedDecrement(&pDevExt->ReadThreadInCreation);
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> _Read: th failure 0x%x\n", pDevExt->PortName, ntStatus)
         );
         return ntStatus;
      }
      
      // wait for read thread to start up before que'ing and kicking
      ntStatus = KeWaitForSingleObject
                 (
                    &pDevExt->ReadThreadStartedEvent, 
                    Executive, 
                    KernelMode, 
                    FALSE, 
                    NULL
                 );
      ntStatus = ObReferenceObjectByHandle
                 (
                    pDevExt->hL1ReadThreadHandle,
                    THREAD_ALL_ACCESS,
                    NULL,
                    KernelMode,
                    (PVOID*)&pDevExt->pL1ReadThread,
                    NULL
                 );
      if (!NT_SUCCESS(ntStatus))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> RD: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
         );
         pDevExt->pL1ReadThread = NULL;
      }
      else
      {
         if (!NT_SUCCESS(ntStatus = ZwClose(pDevExt->hL1ReadThreadHandle)))
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
            pDevExt->hL1ReadThreadHandle = NULL;
         }
      }

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> L1 handle=0x%p thOb=0x%p\n", pDevExt->PortName,
           pDevExt->hL1ReadThreadHandle, pDevExt->pInterruptThread)
      );
      InterlockedDecrement(&pDevExt->ReadThreadInCreation);
   }

   L1_Enqueue:

   if (pIrp == NULL)
   {
      return ntStatus;
   }

   ntStatus = STATUS_PENDING;

   // setup to que and/or kick read thread
   QcAcquireSpinLockWithLevel(&pDevExt->ReadSpinLock, &levelOrHandle, irql);

   // if the IRP was cancelled between enquening
   // and now, then we need special processing here. Note: the cancel routine
   // will not complete the IRP in this case because it will not find the
   // IRP anywhwere.
   if (pIrp->Cancel)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_RIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> RIRP 0x%p pre-cxl\n", pDevExt->PortName, pIrp)
      );
      IoSetCancelRoutine(pIrp, NULL);
      ntStatus = STATUS_CANCELLED;
      QcReleaseSpinLockWithLevel(&pDevExt->ReadSpinLock, levelOrHandle, irql);
      return ntStatus;
   }
   IoSetCancelRoutine(pIrp, CancelReadRoutine);
   _IoMarkIrpPending(pIrp);

   if (IsListEmpty(&pDevExt->ReadDataQueue))
   {
      bQueueWasEmpty = TRUE;
   }
   InterlockedIncrement(&pDevExt->NumberOfQueuedReadIRPs);
   InsertTailList(&pDevExt->ReadDataQueue, &pIrp->Tail.Overlay.ListEntry);
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_RIRP,
      QCUSB_DBG_LEVEL_ERROR,
      ("<%s> RIRP EnQ 0x%p cnt %d\n", pDevExt->PortName, pIrp, pDevExt->NumberOfQueuedReadIRPs)
   );

   if (bQueueWasEmpty == TRUE)
   {
      QCPWR_CancelIdleTimer(pDevExt, QCUSB_BUSY_WT, TRUE, 22);  // queue gained work: set busy and cancel idle IRP
      KeSetEvent
      (
         pDevExt->pL1ReadEvents[L1_KICK_READ_EVENT_INDEX],
         IO_NO_INCREMENT,
         FALSE
      );
   }

   QcReleaseSpinLockWithLevel(&pDevExt->ReadSpinLock, levelOrHandle, irql);

   return ntStatus;
}  // USBRD_Enqueue

NTSTATUS ReadCompletionRoutine
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pContext;

   #ifdef QCOM_TRACE_IN
   DbgPrint("L2 OFF\n");
   #endif

   KeSetEvent
   (
      pDevExt->pL2ReadEvents[L2_READ_COMPLETION_EVENT_INDEX],
      IO_NO_INCREMENT,
      FALSE
   );

   return STATUS_MORE_PROCESSING_REQUIRED;
}

void USBRD_ReadThread(PVOID pContext)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pContext;
   BOOLEAN bCancelled = FALSE;
   NTSTATUS  ntStatus;
   BOOLEAN bFirstTime = TRUE;
   BOOLEAN bNotDone = TRUE;
   PKWAIT_BLOCK pwbArray;
   OBJECT_ATTRIBUTES objAttr;
   KEVENT dummyEvent;
   int i, oldFillIdx;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pDevExt->bL1Stopped = FALSE;

   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (L1_READ_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 "USBRD_ReadThread.pwbArray"
              );
   if (!pwbArray)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> L1: STATUS_NO_MEMORY 01\n", pDevExt->PortName)
      );
      _closeHandle(pDevExt->hL1ReadThreadHandle, "L1R-0");
      KeSetEvent(&pDevExt->ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   // Set L1 thread priority
   //KeSetPriorityThread(KeGetCurrentThread(), QCUSB_L1_PRIORITY);
   if (gVendorConfig.ThreadPriority > 10)
   {
      KeSetPriorityThread(KeGetCurrentThread(), gVendorConfig.ThreadPriority - 1);
   }

   // Initialize L2 buffer
   pDevExt->L2IrpIdx = pDevExt->L2FillIdx = 0;
   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
      pDevExt->pL2ReadBuffer[i].Length  = 0;
      pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
   }

   pDevExt->bL1ReadActive = FALSE;

   pDevExt->pL1ReadEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
   KeClearEvent(&dummyEvent);
   KeClearEvent(&pDevExt->L1ReadPurgeEvent);

   while (bNotDone)
   {
      QCUSB_DbgPrint2
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> L1 checking rd Q\n", pDevExt->PortName)
      );


      // check any status' which would ask us to drain que
      if (pDevExt->bL1Stopped == TRUE)
      {
         if (bCancelled == TRUE) {
            bNotDone = FALSE;
            continue;
         }
         goto wait_for_completion2;

      }  // if (pDevExt->bL1Stopped == TRUE)

wait_for_completion:

      if (bFirstTime == TRUE)
      {
         bFirstTime = FALSE;

         // To ensure when L2 runs, the L1 has already been waiting for data
         if ((pDevExt->hL2ReadThreadHandle == NULL) && (pDevExt->pL2ReadThread == NULL))
         {
            // Start the L2 read thread
            KeClearEvent(&pDevExt->L2ReadThreadStartedEvent);
            InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
            ucThCnt++;
            ntStatus = PsCreateSystemThread
                       (
                          OUT &pDevExt->hL2ReadThreadHandle,
                          IN THREAD_ALL_ACCESS,
                          IN &objAttr, // POBJECT_ATTRIBUTES
                          IN NULL,     // HANDLE  ProcessHandle
                          OUT NULL,    // PCLIENT_ID  ClientId
                          IN (PKSTART_ROUTINE)QCUSB_ReadThread,
                          IN (PVOID) pDevExt
                       );
            if ((!NT_SUCCESS(ntStatus)) || (pDevExt->hL2ReadThreadHandle == NULL))
            {
               pDevExt->hL2ReadThreadHandle = NULL;
               pDevExt->pL2ReadThread = NULL;
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_CRITICAL,
                  ("<%s> L1: L2 th failure\n", pDevExt->PortName)
               );
               KeSetEvent(&pDevExt->ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
               PsTerminateSystemThread(STATUS_NO_MEMORY);
            }
            ntStatus = KeWaitForSingleObject
                       (
                          &pDevExt->L2ReadThreadStartedEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL
                       );

            ntStatus = ObReferenceObjectByHandle
            (
               pDevExt->hL2ReadThreadHandle,
               THREAD_ALL_ACCESS,
               NULL,
               KernelMode,
               (PVOID*)&pDevExt->pL2ReadThread,
               NULL
            );
            if (!NT_SUCCESS(ntStatus))
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_CRITICAL,
                  ("<%s> L2: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
               );
               pDevExt->pL2ReadThread = NULL;
            }
            else
            {
               if (!NT_SUCCESS(ntStatus = ZwClose(pDevExt->hL2ReadThreadHandle)))
               {
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_READ,
                     QCUSB_DBG_LEVEL_DETAIL,
                     ("<%s> L2 ZwClose failed - 0x%x\n", pDevExt->PortName, ntStatus)
                  );
               }
               else
               {
                  ucThCnt--;
                  pDevExt->hL2ReadThreadHandle = NULL;
               }
            }

            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> L2 handle=0x%p thOb=0x%p\n", pDevExt->PortName,
                pDevExt->hL2ReadThreadHandle, pDevExt->pL2ReadThread)
            );

         }  // end of creating L2 thread

         // inform read function that we've started
         KeSetEvent(&pDevExt->ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      }  // if (bFirstTime == TRUE)

wait_for_completion2:

      if ((pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bFilled == TRUE) &&
          (pDevExt->bL1Stopped == FALSE))
      {
         QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
         if (IsListEmpty(&pDevExt->ReadDataQueue))
         {
            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
            ntStatus = KeWaitForMultipleObjects
                       (
                          L1_READ_EVENT_COUNT,
                          (VOID **)&pDevExt->pL1ReadEvents,
                          WaitAny,
                          Executive,
                          KernelMode,
                          FALSE, // non-alertable // TRUE,
                          NULL,
                          pwbArray
                       );
         }
         else
         {
            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_INFO,
               ("<%s> L1 direct completion[%u](0x%x)\n", pDevExt->PortName,
                pDevExt->L2FillIdx, pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status)
            );
            ntStatus = L1_READ_COMPLETION_EVENT_INDEX;
         }
      }
      else
      {
         ntStatus = KeWaitForMultipleObjects
                    (
                       L1_READ_EVENT_COUNT,
                       (VOID **)&pDevExt->pL1ReadEvents,
                       WaitAny,
                       Executive,
                       KernelMode,
                       FALSE, // non-alertable // TRUE,
                       NULL,
                       pwbArray
                    );
      }

      switch(ntStatus)
      {
         case QC_DUMMY_EVENT_INDEX:
         {
            KeClearEvent(&dummyEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> L1: DUMMY_EVENT\n", pDevExt->PortName)
            );
            goto wait_for_completion;
         }
         case L1_READ_COMPLETION_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L1ReadCompletionEvent);

            if (pDevExt->bL1Stopped == FALSE)
            {
               USBRD_FillReadRequest(pDevExt);
            }

            break;
         }

         case L1_KICK_READ_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L1KickReadEvent);

            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> L1 R kicked, active=%d\n", pDevExt->PortName, pDevExt->bL1ReadActive)
            );

            if (pDevExt->bL1Stopped == FALSE)
            {
               // kick L2 thread
               KeSetEvent
               (
                  pDevExt->pL2ReadEvents[L2_KICK_READ_EVENT_INDEX],
                  IO_NO_INCREMENT,
                  FALSE
               ); // kick the read thread

               USBRD_FillReadRequest(pDevExt);
            }
            break;
         }

         case L1_CANCEL_EVENT_INDEX:
         {
            pDevExt->bL1InCancellation = TRUE;
            pDevExt->bL1Stopped = TRUE;

            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> L1_CANCEL_EVENT_INDEX\n", pDevExt->PortName)
            );
            
            // reset read cancel event so we dont reactive
            KeClearEvent(&pDevExt->L1CancelReadEvent); // never set

            // Alert L2 thread
            if ((pDevExt->hL2ReadThreadHandle != NULL) || (pDevExt->pL2ReadThread != NULL))
            {
               KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
               KeSetEvent
               (
                  &pDevExt->CancelReadEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );

               if (pDevExt->pL2ReadThread != NULL)
               {
                  KeWaitForSingleObject
                  (
                     pDevExt->pL2ReadThread,
                     Executive,
                     KernelMode,
                     FALSE,
                     NULL
                  );
                  ObDereferenceObject(pDevExt->pL2ReadThread);
                  KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
                  _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-7");
                  pDevExt->pL2ReadThread = NULL;
               }
               else  // best effort
               {
                  KeWaitForSingleObject
                  (
                     &pDevExt->L2ReadThreadClosedEvent,
                     Executive,
                     KernelMode,
                     FALSE,
                     NULL
                  );
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_READ,
                     QCUSB_DBG_LEVEL_DETAIL,
                     ("<%s> L1R cxl: L2 OUT\n", pDevExt->PortName)
                  );
                  KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
                  _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-8");
               }
            }

            bCancelled = TRUE; // singnal the loop that a cancel has occurred
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> L1_CANCEL_EVENT_INDEX END\n", pDevExt->PortName)
            );
            break;
         }

         case L1_READ_PURGE_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L1ReadPurgeEvent);

            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> L1_READ_PURGE_EVENT\n", pDevExt->PortName)
            );

            USBUTL_PurgeQueue
            (
               pDevExt,
               &pDevExt->ReadDataQueue,
               &pDevExt->ReadSpinLock,
               QCUSB_IRP_TYPE_RIRP,
               1
            );

            // Purge READ_CONTROL items
            USBENC_PurgeQueue(pDevExt, &pDevExt->EncapsulatedDataQueue, FALSE, 0);

            // Purge SEND_CONTROL items
            KeClearEvent(&pDevExt->DispatchPurgeCompleteEvent);
            KeSetEvent(&pDevExt->DispatchPurgeEvent, IO_NO_INCREMENT, FALSE);
            KeWaitForSingleObject
            (
               &pDevExt->DispatchPurgeCompleteEvent,
               Executive,
               KernelMode,
               FALSE,
               NULL
            );

            USBUTL_CheckPurgeStatus(pDevExt, (USB_PURGE_RD | USB_PURGE_EC));

            break;
         }

         case L1_STOP_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L1StopEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> L1_STOP_EVENT\n", pDevExt->PortName)
            );
            pDevExt->bL1Stopped = TRUE;
            break;
         }

         case L1_RESUME_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L1ResumeEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> L1_RESUME_EVENT rm %d\n", pDevExt->PortName, pDevExt->bDeviceRemoved)
            );
            pDevExt->bL1Stopped = pDevExt->bDeviceRemoved;
            break;
         }

         default:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> L1 unsupported st 0x%x\n", pDevExt->PortName, ntStatus)
            );

            // Ignore it for now
            break;
         }  // default

      }  // switch (ntStatus)

   }  // while (bNoptDone)

   // exiting thread, release allocated resources
   if(pwbArray)
   {
      _ExFreePool(pwbArray);
      pwbArray = NULL;
   }

   if ((pDevExt->hL2ReadThreadHandle != NULL) || (pDevExt->pL2ReadThread != NULL))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> L1 thread exiting, cancelling L2\n", pDevExt->PortName)
      );

      KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
      KeSetEvent
      (
         &pDevExt->CancelReadEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pDevExt->pL2ReadThread != NULL)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> L1 thread exiting, cancelling L2 - 2 \n", pDevExt->PortName)
         );
         KeWaitForSingleObject
         (
            pDevExt->pL2ReadThread,
            Executive,
            KernelMode,
            FALSE,
            NULL
         );
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> L1 thread exiting, cancelling L2 - 3 \n", pDevExt->PortName)
         );
         ObDereferenceObject(pDevExt->pL2ReadThread);
         KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
         _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-5");
         pDevExt->pL2ReadThread = NULL;
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> L1 thread exiting, cancelling L2 - 4 \n", pDevExt->PortName)
         );
      }
      else  // best effort
      {
         KeWaitForSingleObject
         (
            &pDevExt->L2ReadThreadClosedEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL       // &delayValue
         );
         KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
         _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-6");
      }
   }

   USBUTL_PurgeQueue
   (
      pDevExt,
      &pDevExt->ReadDataQueue,
      &pDevExt->ReadSpinLock,
      QCUSB_IRP_TYPE_RIRP,
      0
   );

   // Reset L2 Buffers
   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
      pDevExt->pL2ReadBuffer[i].Length  = 0;
      pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
   }

   KeSetEvent(&pDevExt->L1ReadThreadClosedEvent,IO_NO_INCREMENT,FALSE);
   pDevExt->bL1InCancellation = FALSE;

   PsTerminateSystemThread(STATUS_SUCCESS); // terminate this thread
}  // USBRD_ReadThread

void QCUSB_ReadThread(PDEVICE_EXTENSION pDevExt)
{
   PIRP pIrp;
   PIO_STACK_LOCATION pNextStack;
   PURB     pUrb = NULL;
   UCHAR    ucPipeIndex;
   BOOLEAN  bCancelled = FALSE;
   NTSTATUS ntStatus;
   BOOLEAN  bFirstTime = TRUE;
   PKWAIT_BLOCK pwbArray;
   KIRQL    OldRdIrqLevel;
   ULONG    ulReadSize;
   KEVENT   dummyEvent;
   int      i, devBusyCnt = 0, devErrCnt = 0;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> L2 ERR: wrong IRQL\n", pDevExt->PortName)
      );
      #ifdef DEBUG_MSGS
      KdBreakPoint();
      #endif
   }

   // allocate an urb for read operations
   pUrb = ExAllocatePool
          (
             NonPagedPool,
             sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER)
          );
   if (pUrb == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> L2 - STATUS_NO_MEMORY 0\n", pDevExt->PortName)
      );
      _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-0");
      KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (L2_READ_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 "Level2ReadThread.pwbArray"
              );
   if (!pwbArray)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> L2 - STATUS_NO_MEMORY 1\n", pDevExt->PortName)
      );
      ExFreePool(pUrb);
      _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-2");
      KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   // allocate IRP to use for read operations
   pIrp = IoAllocateIrp((CCHAR)(pDevExt->StackDeviceObject->StackSize+1), FALSE);
   if (!pIrp )
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> L2 - STATUS_NO_MEMORY 2\n", pDevExt->PortName)
      );
      ExFreePool(pUrb);
      _ExFreePool(pwbArray);
      _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-3");
      KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   #ifdef ENABLE_LOGGING
   // Create logs
   if (pDevExt->EnableLogging == TRUE)
   {
      QCUSB_CreateLogs(pDevExt, QCUSB_CREATE_RX_LOG);
   }
   #endif  // ENABLE_LOGGING

   // Set L2 thread priority
   if (gVendorConfig.ThreadPriority > 10)
   {
      KeSetPriorityThread(KeGetCurrentThread(), gVendorConfig.ThreadPriority);
   }
   
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_INFO,
      ("<%s> L2: pri=%d, L2Buf=%d\n",
        pDevExt->PortName,
        KeQueryPriorityThread(KeGetCurrentThread()),
        pDevExt->NumberOfL2Buffers
      )
   );

   pDevExt->bL2ReadActive = FALSE;

   pDevExt->pL2ReadEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent,NotificationEvent,FALSE);
   KeClearEvent(&dummyEvent);

   while (bCancelled == FALSE)
   {
      if (pDevExt->pL2ReadBuffer[pDevExt->L2IrpIdx].bFilled == FALSE)
      {
         if ((pDevExt->bL2ReadActive == FALSE) && (pDevExt->bL2Stopped == FALSE))
         {
            IoReuseIrp(pIrp, STATUS_SUCCESS);

            pNextStack = IoGetNextIrpStackLocation( pIrp );
            pNextStack->Parameters.Others.Argument1 = pUrb;
            pNextStack->Parameters.DeviceIoControl.IoControlCode =
               IOCTL_INTERNAL_USB_SUBMIT_URB;
            pNextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;

            IoSetCompletionRoutine
            (
               pIrp,
               ReadCompletionRoutine,
               pDevExt,
               TRUE,
               TRUE,
               TRUE
            );

            ulReadSize = pDevExt->MaxPipeXferSize;

            UsbBuildInterruptOrBulkTransferRequest
            (
               pUrb,
               sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
               pDevExt->Interface[pDevExt->DataInterface]->Pipes[pDevExt->BulkPipeInput].PipeHandle,
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpIdx].Buffer,
               NULL,
               ulReadSize,
               USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN,
               NULL
            )
            QCPWR_CancelIdleTimer(pDevExt, QCUSB_BUSY_WT, TRUE, 20);  // RX in progress, set busy and cancel idle IRP
            pDevExt->bL2ReadActive = TRUE;
            ntStatus = IoCallDriver(pDevExt->StackDeviceObject,pIrp);
            if (ntStatus != STATUS_PENDING)
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_CRITICAL,
                  ("<%s> Rth: IoCallDriver rtn 0x%x", pDevExt->PortName, ntStatus)
               );
               if ((ntStatus == STATUS_DEVICE_NOT_READY) || (ntStatus == STATUS_DEVICE_DATA_ERROR))
               {
                  devErrCnt++;
                  if (devErrCnt > 3)
                  {
                     pDevExt->bL2Stopped = TRUE;
                     QCUSB_DbgPrint
                     (
                        QCUSB_DBG_MASK_READ,
                        QCUSB_DBG_LEVEL_ERROR,
                        ("<%s> L2 err, stopped\n", pDevExt->PortName)
                     );
                  }
               }
               else
               {
                  devErrCnt = 0;
               }
            }
            else
            {
               devErrCnt = 0;
            }
            #ifdef QCOM_TRACE_IN
            DbgPrint("L2 ON %ld/%lu\n", ulReadSize, CountReadQueue(pDevExt));
            #endif
         } // if L2 is inactive and not stopped
         else
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> L2R: ERR-ACTorSTOP\n", pDevExt->PortName)
            );
         }
      } // end if -- if L2 buffer is available for filling
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> L2 buffers exhausted, wait...\n", pDevExt->PortName)
         );
      }

      // No matter what IoCallDriver returns, we always wait on the kernel event
      // we created earlier. Our completion routine will gain control when the IRP
      // completes to signal this event. -- Walter Oney's WDM book page 228
wait_for_completion:

      if(bFirstTime == TRUE)
      {
         bFirstTime = FALSE;

         // inform read function that we've started
         KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      }

      if (KeGetCurrentIrql() > PASSIVE_LEVEL)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> L2 ERR: wrong IRQL -2\n", pDevExt->PortName)
         );
         #ifdef DEBUG_MSGS
         KdBreakPoint();
         #endif
      }

      ntStatus = KeWaitForMultipleObjects
                 (
                    L2_READ_EVENT_COUNT,
                    (VOID **)&pDevExt->pL2ReadEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,    // non-alertable
                    NULL,     // no timeou for bulk read
                    pwbArray
                 );

      switch (ntStatus)
      {
         case QC_DUMMY_EVENT_INDEX:
         {
            KeClearEvent(&dummyEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> L2: DUMMY_EVENT\n", pDevExt->PortName)
            );
            goto wait_for_completion;
         }
         case STATUS_ALERTED:
         {
            // this case should not happen
            goto wait_for_completion;
            break;
         }

         case L2_READ_COMPLETION_EVENT_INDEX:
         {
            // clear read completion event
            KeClearEvent(&pDevExt->L2ReadCompletionEvent);
            pDevExt->bL2ReadActive = FALSE;
            if (IsListEmpty(&pDevExt->ReadDataQueue)) {
                QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, FALSE, 21);
            }
            ntStatus = pIrp->IoStatus.Status;

            #ifdef ENABLE_LOGGING
            if (ntStatus == STATUS_SUCCESS)
            {
               #ifdef QCOM_TRACE_IN
               DbgPrint("   L2R %ld\n", pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength);
               #endif
               // log to file
               if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hRxLogFile != NULL))
               {
                  QCUSB_LogData
                  (
                     pDevExt,
                     pDevExt->hRxLogFile,
                     (PVOID)(pUrb->UrbBulkOrInterruptTransfer.TransferBuffer),
                     pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                     QCUSB_LOG_TYPE_READ
                  );
               }
            }
            else
            {
               if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hRxLogFile != NULL))
               {
                  // log to file
                  QCUSB_LogData
                  (
                     pDevExt,
                     pDevExt->hRxLogFile,
                     (PVOID)&ntStatus,
                     sizeof(NTSTATUS),
                     QCUSB_LOG_TYPE_RESPONSE_RD
                  );
               }

               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_CRITICAL,
                  ("<%s> L2 RX failed - 0x%x\n", pDevExt->PortName, pIrp->IoStatus.Status)
               );
            }
            #endif  // ENABLE_LOGGING

            if ((pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength > 0)
                || (ntStatus != STATUS_SUCCESS))
            {
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpIdx].Status = ntStatus;
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpIdx].Length =
                  pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpIdx].bFilled = TRUE;
               if (++(pDevExt->L2IrpIdx) == pDevExt->NumberOfL2Buffers)
               {
                  pDevExt->L2IrpIdx = 0;
               }

               if (!NT_SUCCESS(ntStatus))
               {
                  // STATUS_DEVICE_NOT_READY      0xC00000A3
                  // STATUS_DEVICE_DATA_ERROR     0xC000009C
                  // STATUS_DEVICE_NOT_CONNECTED  0xC000009D
                  // STATUS_UNSUCCESSFUL          0xC0000001
                  if (ntStatus != STATUS_CANCELLED)
                  {
                     if (++devBusyCnt >= (pDevExt->NumOfRetriesOnError+2))
                     {
                        pDevExt->bL2Stopped = TRUE;
                        QCUSB_DbgPrint
                        (
                           QCUSB_DBG_MASK_READ,
                           QCUSB_DBG_LEVEL_CRITICAL,
                           ("<%s> L2 cont failure, stopped...\n", pDevExt->PortName)
                        );
                        devBusyCnt = 0;
                        // USBUTL_Wait(pDevExt, -(10 * 1000 * 1000L));  // wait for 1 second
                        KeSetEvent
                        (
                           &pDevExt->L1ReadCompletionEvent,
                           IO_NO_INCREMENT,
                           FALSE
                        );
                        goto wait_for_completion;
                     }
                  }
               }

               // Notify L1 thread
               KeSetEvent
               (
                  &pDevExt->L1ReadCompletionEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );

               continue;
            }

            if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
            {
               // Clear all received bytes
               pDevExt->L2IrpIdx = pDevExt->L2FillIdx = 0;
               for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
               {
                  pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
                  pDevExt->pL2ReadBuffer[i].Length  = 0;
                  pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
               }
               pDevExt->bL2Stopped = TRUE;
            }

            break;
         }

         case L2_KICK_READ_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L2KickReadEvent);

            // Use error level as output priority
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> L2 R kicked, act=%d\n", pDevExt->PortName, pDevExt->bL2ReadActive)
            );

            if (pDevExt->bL2ReadActive == TRUE)
            {
               goto wait_for_completion;
            }
            // else we just go around the loop again, picking up the next
            // read entry
            continue;
         }

         case CANCEL_EVENT_INDEX:
         {
            // reset read cancel event so we dont reactive
            KeClearEvent(&pDevExt->CancelReadEvent); // never set

            // Reset all L2 buffers on cancel event
            pDevExt->L2IrpIdx = pDevExt->L2FillIdx = 0;
            for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
            {
               pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
               pDevExt->pL2ReadBuffer[i].Length  = 0;
               pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
            }

            #ifdef ENABLE_LOGGING
            QCUSB_LogData
            (
               pDevExt,
               pDevExt->hRxLogFile,
               (PVOID)NULL,
               0,
               QCUSB_LOG_TYPE_CANCEL_THREAD
            );
            #endif // ENABLE_LOGGING

            bCancelled = pDevExt->bL2Stopped = TRUE;
            if (pDevExt->bL2ReadActive == TRUE)
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_INFO,
                  ("<%s> L2 CANCEL - IRP\n", pDevExt->PortName)
               );
               IoCancelIrp(pIrp); // we don't care the result
               goto wait_for_completion;
            }
            break;
         }

         case L2_STOP_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L2StopEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> L2_STOP_EVENT\n", pDevExt->PortName)
            );
            pDevExt->bL2Stopped = TRUE;
            break;
         }

         case L2_RESUME_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L2ResumeEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> L2_RESUME_EVENT rm %d\n", pDevExt->PortName, pDevExt->bDeviceRemoved)
            );
            pDevExt->bL2Stopped = pDevExt->bDeviceRemoved;
            break;
         }

         default:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> L2 unsupported st 0x%x\n", pDevExt->PortName, ntStatus)
            );

            // Ignore for now
            break;

         }  // default

      }  // switch
   }  // end while forever 

   // exiting thread, release allocated resources
   if(pwbArray)
   {
      _ExFreePool(pwbArray);
      pwbArray = NULL;
   }
   if(pUrb) 
   {
      ExFreePool(pUrb);
      pUrb = NULL;
   }
   if(pIrp)
   {
      IoReuseIrp(pIrp, STATUS_SUCCESS);
      IoFreeIrp(pIrp);
   }

   if (pDevExt->hRxLogFile != NULL)
   {
      #ifdef ENABLE_LOGGING
      QCUSB_LogData
      (
         pDevExt,
         pDevExt->hRxLogFile,
         (PVOID)NULL,
         0,
         QCUSB_LOG_TYPE_THREAD_END
      );
      #endif // ENABLE_LOGGING
      ZwClose(pDevExt->hRxLogFile);
      pDevExt->hRxLogFile = NULL;
   }

   KeSetEvent(&pDevExt->L2ReadThreadClosedEvent,IO_NO_INCREMENT,FALSE);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> L2: OUT 0x%x\n", pDevExt->PortName, ntStatus)
   );

   PsTerminateSystemThread(STATUS_SUCCESS); // terminate this thread
}  // QCUSB_ReadThread

VOID CancelReadRoutine(PDEVICE_OBJECT DeviceObject, PIRP pIrp)
{
   KIRQL irql = KeGetCurrentIrql();
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   BOOLEAN bIrpInQueue;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_ERROR,
      ("<%s> RD_Cxl: RIRP: 0x%p\n", pDevExt->PortName, pIrp)
   );
   IoReleaseCancelSpinLock(pIrp->CancelIrql);   //set by the IO mgr.

   QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
   bIrpInQueue = USBUTL_IsIrpInQueue
                 (
                    pDevExt,
                    &pDevExt->ReadDataQueue,
                    pIrp,
                    QCUSB_IRP_TYPE_RIRP,
                    1
                 );

   if (bIrpInQueue == TRUE)
   {
      RemoveEntryList(&pIrp->Tail.Overlay.ListEntry);
      pIrp->IoStatus.Status = STATUS_CANCELLED;
      pIrp->IoStatus.Information = 0;
      InsertTailList(&pDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
   }
   else
   {
      // The read IRP is in processing, we just re-install the cancel routine here.
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> RD_Cxl: no action to active 0x%p\n", pDevExt->PortName, pIrp)
      );
      IoSetCancelRoutine(pIrp, CancelReadRoutine);
   }

   QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

}  // CancelReadRoutine

// called by read thread within ReadQueMutex because it manipulates pCallingIrp
NTSTATUS USBRD_ReadIrpCompletion(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   PIRP pIrp = NULL;
   PLIST_ENTRY headOfList;
   PUCHAR ioBuffer = NULL;
   ULONG  ulReqLength;
   PIO_STACK_LOCATION irpStack;
   PUCHAR DataBuffer;
   ULONG DataLength;
#if defined(QCMP_MBIM_DL_SUPPORT)
   PQCNTB_16BIT_HEADER pNTBHrd;
   PQCNDP_16BIT_HEADER pNDPHrd;
   PQCDATAGRAM_STRUCT pDatagramPtr;
#endif

   NTSTATUS ntStatus = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status;
   ULONG ulLength = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Length;

   if (NT_SUCCESS(ntStatus) || (ntStatus == STATUS_CANCELLED))
   {
      pDevExt->RdErrorCount = 0;
   }
   else
   //if ((ntStatus == STATUS_DEVICE_DATA_ERROR) ||
   //    (ntStatus == STATUS_DEVICE_NOT_READY)  ||
   //    (ntStatus == STATUS_UNSUCCESSFUL))
   {
      pDevExt->RdErrorCount++;
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> RIC<%d>: DEVICE_ERROR %d\n", pDevExt->PortName, cookie, pDevExt->RdErrorCount)
      );

      // after some magic number of times of failure,
      // we mark the device as 'removed'
      if ((pDevExt->RdErrorCount > pDevExt->NumOfRetriesOnError) && (pDevExt->ContinueOnDataError == FALSE))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> RIC<%d>: err %d - dev removed\n\n", pDevExt->PortName, cookie, pDevExt->RdErrorCount)
         );
         clearDevState(DEVICE_STATE_PRESENT); 
         pDevExt->RdErrorCount = pDevExt->NumOfRetriesOnError;
      }
   }

   //Decode NTBdata here
#if defined(QCMP_MBIM_DL_SUPPORT)
  QCUSB_DbgPrint
  (
     QCUSB_DBG_MASK_READ,
     QCUSB_DBG_LEVEL_DETAIL,
     ("<%s> RIC : MBIMDL:%d  \n", pDevExt->PortName, pDevExt->MBIMDLEnabled)
  );

  if (pDevExt->MBIMDLEnabled == TRUE)
  {
   // need to check the MBIM sanity but for now just skip the MBIM NCM header
   pNTBHrd = (PQCNTB_16BIT_HEADER)pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer;

   // Print the MBIM packet
   USBUTL_PrintBytes
   (
      pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer,
      128,
      pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Length,
      "READDATA->MBIM",
      pDevExt,
      QCUSB_DBG_MASK_RDATA,
      QCUSB_DBG_LEVEL_VERBOSE
   );

   
   if (pNTBHrd->wBlockLength != ulLength)
   {
      pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status = STATUS_UNSUCCESSFUL;
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> RIC : Wrong MBIM packet BlockLength : %d and PacketLen : %d\n", pDevExt->PortName, pNTBHrd->wBlockLength, ulLength)
      );
      return STATUS_UNSUCCESSFUL;
   }
   pNDPHrd = (PQCNDP_16BIT_HEADER)((PCHAR)pNTBHrd + pNTBHrd->wNdpIndex);
   
   // Currently implement only one NDP
   // Also assume that readIRPs are present all the time.
   // Action item to take care of  no ReadIRPs in ReadDataQueue
   pDatagramPtr = (PQCDATAGRAM_STRUCT)((PCHAR)pNDPHrd + sizeof(QCNDP_16BIT_HEADER));
   while ( pDatagramPtr->Datagram != 0)
   {
      DataBuffer = (PCHAR)pNTBHrd + pDatagramPtr->DatagramPtr;
      DataLength = pDatagramPtr->DatagramLen;

      
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> RIC : MBIMDL: Current Datagram Ptr : %x and Datagram len : %x\n", pDevExt->PortName, pDatagramPtr->DatagramPtr, pDatagramPtr->DatagramLen)
      );
      
      pDatagramPtr = (PQCDATAGRAM_STRUCT)((PCHAR)pDatagramPtr + sizeof(QCDATAGRAM_STRUCT));

      // IP decodning
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_RIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> RIC<%d> IP Identifier is 6th Byte=0x%x and 7th Byte=0x%x\n",
         pDevExt->PortName, cookie, DataBuffer[4], DataBuffer[5]
         )
      );
      
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> RIC : MBIMDL: Next Datagram Ptr : %x and Datagram len : %x\n", pDevExt->PortName, pDatagramPtr->DatagramPtr, pDatagramPtr->DatagramLen)
      );
          
      // De-queue
      if (!IsListEmpty(&pDevExt->ReadDataQueue))
      {
         headOfList = RemoveHeadList(&pDevExt->ReadDataQueue);
         pIrp =  CONTAINING_RECORD
                 (
                    headOfList,
                    IRP,
                    Tail.Overlay.ListEntry
                 );
      }

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_RIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> RIC<%d> RIRP=0x%p (%ldB) st 0x%x\n",
         pDevExt->PortName, cookie, pIrp, DataLength, ntStatus
         )
      );

      if (pIrp != NULL)
      {
         irpStack = IoGetCurrentIrpStackLocation(pIrp);
         if (pIrp->MdlAddress)
         {
            ulReqLength = MmGetMdlByteCount(pIrp->MdlAddress);
            ioBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, HighPagePriority);
            if (ioBuffer == NULL)
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_CRITICAL,
                  ("<%s> ERROR: Mdl translation 0x%p\n", pDevExt->PortName, pIrp->MdlAddress)
               );
            }
         }
         else
         {
            ulReqLength = irpStack->Parameters.Read.Length;
            ioBuffer = (PUCHAR)pIrp->AssociatedIrp.SystemBuffer;
         }

         if (ntStatus == STATUS_SUCCESS)
         {
            #ifdef QC_IP_MODE
            if (pDevExt->IPOnlyMode == TRUE)
            {
               ulReqLength -= sizeof(QCUSB_ETH_HDR);
            }
            #endif // QC_IP_MODE

             if (DataLength > ulReqLength)
             {
                ntStatus = STATUS_BUFFER_TOO_SMALL;
             }
             if (ioBuffer == NULL)
             {
                ntStatus = STATUS_NO_MEMORY;
             }

             if (ntStatus == STATUS_SUCCESS)
             {
                PUCHAR pPktStart; 
                pPktStart = ioBuffer;

                pIrp->IoStatus.Information = DataLength;

                #ifdef QC_IP_MODE
                if (pDevExt->IPOnlyMode == TRUE)
                {
                   RtlCopyMemory(ioBuffer, &(pDevExt->EthHdr), sizeof(QCUSB_ETH_HDR));
                   ioBuffer += sizeof(QCUSB_ETH_HDR);
                   pIrp->IoStatus.Information += sizeof(QCUSB_ETH_HDR);
                }
                #endif // QC_IP_MODE

                RtlCopyMemory   // use copy for now...
                (
                   ioBuffer,
                   DataBuffer,
                   DataLength
                );
                #ifdef QC_IP_MODE
                if (pDevExt->IPOnlyMode == TRUE)
                {
                   USBUTL_SetEtherType(pPktStart);
                }
                #endif // QC_IP_MODE
                QCUSB_DbgPrint2
                (
                   QCUSB_DBG_MASK_RIRP,
                   QCUSB_DBG_LEVEL_DETAIL,
                   ("<%s> RIC<%d> - Filled RIRP 0x%p\n", pDevExt->PortName, cookie, pIrp)
                );
             }
          }
          if (pIrp->Cancel)
          {
             QCUSB_DbgPrint
             (
                QCUSB_DBG_MASK_RIRP,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> RIC<%d> RIRP 0x%p C: in callellation!!\n", pDevExt->PortName, cookie, pIrp)
             );
          }

          if (IoSetCancelRoutine(pIrp, NULL) == NULL)
          {
             // the IRP is in Cancellation
             QCUSB_DbgPrint
             (
                QCUSB_DBG_MASK_RIRP,
                QCUSB_DBG_LEVEL_ERROR,
                ("<%s> RIC<%d> RIRP 0x%p C: 0\n", pDevExt->PortName, cookie, pIrp)
             );
             return ntStatus;
          }

          pIrp->IoStatus.Status = ntStatus;

          if (ntStatus == STATUS_CANCELLED)
          {
             pIrp->IoStatus.Information = 0;
          }

          QCUSB_DbgPrint2
          (
             QCUSB_DBG_MASK_RIRP,
             QCUSB_DBG_LEVEL_DETAIL,
             ("<%s> RIC<%d> - QtoC RIRP (0x%x) 0x%p\n", pDevExt->PortName,
               cookie, ntStatus, pIrp)
          );
          InsertTailList(&pDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
          KeSetEvent(&pDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
       }
       else
       {
          ntStatus = STATUS_UNSUCCESSFUL;
       }
   }
  }
  else   
  {
#endif  
   // De-queue
   if (!IsListEmpty(&pDevExt->ReadDataQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->ReadDataQueue);
      pIrp =  CONTAINING_RECORD
              (
                 headOfList,
                 IRP,
                 Tail.Overlay.ListEntry
              );
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_RIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> RIC<%d> RIRP=0x%p (%ldB) st 0x%x\n",
        pDevExt->PortName, cookie, pIrp, ulLength, ntStatus
      )
   );

   if (pIrp != NULL)
   {
      irpStack = IoGetCurrentIrpStackLocation(pIrp);
      if (pIrp->MdlAddress)
      {
         ulReqLength = MmGetMdlByteCount(pIrp->MdlAddress);
         ioBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, HighPagePriority);
         if (ioBuffer == NULL)
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_CRITICAL,
               ("<%s> ERROR: Mdl translation 0x%p\n", pDevExt->PortName, pIrp->MdlAddress)
            );
         }
      }
      else
      {
         ulReqLength = irpStack->Parameters.Read.Length;
         ioBuffer = (PUCHAR)pIrp->AssociatedIrp.SystemBuffer;
      }

      if (ntStatus == STATUS_SUCCESS)
      {
         #ifdef QC_IP_MODE
         if (pDevExt->IPOnlyMode == TRUE)
         {
            ulReqLength -= sizeof(QCUSB_ETH_HDR);
         }
         #endif // QC_IP_MODE

         if (ulLength > ulReqLength)
         {
            ntStatus = STATUS_BUFFER_TOO_SMALL;
         }
         if (ioBuffer == NULL)
         {
            ntStatus = STATUS_NO_MEMORY;
         }

         if (ntStatus == STATUS_SUCCESS)
         {
            PUCHAR pPktStart = ioBuffer;

            pIrp->IoStatus.Information = ulLength;

            #ifdef QC_IP_MODE
            if (pDevExt->IPOnlyMode == TRUE)
            {
               RtlCopyMemory(ioBuffer, &(pDevExt->EthHdr), sizeof(QCUSB_ETH_HDR));
               ioBuffer += sizeof(QCUSB_ETH_HDR);
               pIrp->IoStatus.Information += sizeof(QCUSB_ETH_HDR);
            }
            #endif // QC_IP_MODE

            RtlCopyMemory   // use copy for now...
            (
               ioBuffer,
               pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer,
               ulLength
            );
            #ifdef QC_IP_MODE
            if (pDevExt->IPOnlyMode == TRUE)
            {
               USBUTL_SetEtherType(pPktStart);
            }
            #endif // QC_IP_MODE
            QCUSB_DbgPrint2
            (
               QCUSB_DBG_MASK_RIRP,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> RIC<%d> - Filled RIRP 0x%p\n", pDevExt->PortName, cookie, pIrp)
            );
         }
      }
      if (pIrp->Cancel)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_RIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> RIC<%d> RIRP 0x%p C: in callellation!!\n", pDevExt->PortName, cookie, pIrp)
         );
      }

      if (IoSetCancelRoutine(pIrp, NULL) == NULL)
      {
         // the IRP is in Cancellation
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_RIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> RIC<%d> RIRP 0x%p C: 0\n", pDevExt->PortName, cookie, pIrp)
         );
         return ntStatus;
      }

      pIrp->IoStatus.Status = ntStatus;

      if (ntStatus == STATUS_CANCELLED)
      {
         pIrp->IoStatus.Information = 0;
      }

      QCUSB_DbgPrint2
      (
         QCUSB_DBG_MASK_RIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> RIC<%d> - QtoC RIRP (0x%x) 0x%p\n", pDevExt->PortName,
           cookie, ntStatus, pIrp)
      );
      InsertTailList(&pDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
   }
   else
   {
      ntStatus = STATUS_UNSUCCESSFUL;
   }
#if defined(QCMP_MBIM_DL_SUPPORT)   
  }
#endif
   return ntStatus;
}  // USBRD_ReadIrpCompletion

ULONG CountL1ReadQueue(PDEVICE_EXTENSION pDevExt)
{
   return CountReadQueue(pDevExt);
}

ULONG CountReadQueue(PDEVICE_EXTENSION pDevExt)
{
   ULONG i;
   LONG lCount = 0;

   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      if (pDevExt->pL2ReadBuffer[i].bFilled == TRUE)
      {
         lCount += pDevExt->pL2ReadBuffer[i].Length;
      }
   }

   return (ULONG)lCount;
}

NTSTATUS USBRD_InitializeL2Buffers(PDEVICE_EXTENSION pDevExt)
{
   int i;
  
   if ((pDevExt->bEnableDataBundling[QC_LINK_DL] == FALSE)
#if defined(QCMP_MBIM_DL_SUPPORT)
       && (pDevExt->MBIMDLEnabled == FALSE)
#endif
#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
          && (pDevExt->QMAPEnabledV2 == FALSE)
          && (pDevExt->QMAPEnabledV3 == FALSE)
#endif       
#endif
#if defined(QCMP_QMAP_V1_SUPPORT)
          && (pDevExt->QMAPEnabledV4 == FALSE)
          && (pDevExt->QMAPEnabledV1 == FALSE)
#endif       
      )
   {
      //if (pDevExt->NumberOfL2Buffers < QCUSB_MAX_MRD_BUF_COUNT)
      //{
      //   pDevExt->NumberOfL2Buffers = QCUSB_MAX_MRD_BUF_COUNT;
      //}
   }
   else
   {
      if (pDevExt->MaxPipeXferSize < QCUSB_MRECEIVE_MAX_BUFFER_SIZE)
      {
         pDevExt->MaxPipeXferSize = QCUSB_MRECEIVE_MAX_BUFFER_SIZE;
      }
   }

/* Currently a HACK for now */
#if defined(QCMP_MBIM_DL_SUPPORT)
   if (pDevExt->MaxPipeXferSize < QCUSB_MRECEIVE_MAX_BUFFER_SIZE)
   {
      pDevExt->MaxPipeXferSize = QCUSB_MRECEIVE_MAX_BUFFER_SIZE;
   }
#endif

#ifdef QCUSB_MUX_PROTOCOL
#if defined(QCMP_QMAP_V2_SUPPORT)
if (pDevExt->MaxPipeXferSize < QCUSB_MRECEIVE_MAX_BUFFER_SIZE)
{
   pDevExt->MaxPipeXferSize = QCUSB_MRECEIVE_MAX_BUFFER_SIZE;
}

#endif
#endif

#if defined(QCMP_QMAP_V1_SUPPORT)
   if (pDevExt->MaxPipeXferSize < QCUSB_MRECEIVE_MAX_BUFFER_SIZE)
   {
      pDevExt->MaxPipeXferSize = QCUSB_MRECEIVE_MAX_BUFFER_SIZE;
   }
#endif

   // customize for USB 3.0 super speed
   if (pDevExt->wMaxPktSize >= QC_SS_BLK_PKT_SZ)
   {
      if (pDevExt->EnableData5G == 0)
      {
         if (pDevExt->NumberOfL2Buffers < QCUSB_NUM_OF_LEVEL2_BUF_FOR_SS)
         {
            pDevExt->NumberOfL2Buffers = QCUSB_NUM_OF_LEVEL2_BUF_FOR_SS;
         }
      }
      else
      {
         if (pDevExt->NumberOfL2Buffers < QCUSB_NUM_OF_LEVEL2_BUF_FOR_SS_5G)
         {
            pDevExt->NumberOfL2Buffers = QCUSB_NUM_OF_LEVEL2_BUF_FOR_SS_5G;
         }
      }
   }


   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_FORCE,
      ("<%s> InitL2: size 0x%xB(%d)\n", pDevExt->PortName,
        pDevExt->MaxPipeXferSize, pDevExt->NumberOfL2Buffers)
   );

   pDevExt->pL2ReadBuffer = _ExAllocatePool
                            (
                               NonPagedPool,
                               sizeof(USBMRD_L2BUFFER) * pDevExt->NumberOfL2Buffers,
                               "pL2ReadBuffer0"
                            );
   if (pDevExt->pL2ReadBuffer == NULL)
   {
      return STATUS_NO_MEMORY;
   }

   RtlZeroMemory
   (
      pDevExt->pL2ReadBuffer,
      sizeof(USBMRD_L2BUFFER) * pDevExt->NumberOfL2Buffers
   );

   InitializeListHead(&pDevExt->L2CompletionQueue);
   InitializeListHead(&pDevExt->L2PendingQueue);
   KeInitializeSpinLock(&pDevExt->L2Lock);

   // Initialize L2 buffer
   pDevExt->L2IrpIdx = pDevExt->L2FillIdx = 0;

   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
      pDevExt->pL2ReadBuffer[i].Length  = 0;
      pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
      pDevExt->pL2ReadBuffer[i].DataPtr = NULL;

      if (pDevExt->pL2ReadBuffer[i].Buffer == NULL)
      {
         pDevExt->pL2ReadBuffer[i].Buffer = _ExAllocatePool
                                            (
                                               NonPagedPool,
                                               pDevExt->MaxPipeXferSize,
                                               "pL2ReadBuffer"
                                            );
         if (pDevExt->pL2ReadBuffer[i].Buffer == NULL)
         {
            if (i == 0)
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_CRITICAL,
                  ("<%s> InitL2: ERR - NO_MEMORY\n", pDevExt->PortName)
               );
               return STATUS_NO_MEMORY;
            }
            else
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_READ,
                  QCUSB_DBG_LEVEL_CRITICAL,
                  ("<%s> InitL2: WARNING - degraded to %d\n", pDevExt->PortName, i)
               );
               pDevExt->NumberOfL2Buffers = i;
               return STATUS_SUCCESS;
            }
         }
         else
         {
            pDevExt->pL2ReadBuffer[i].Irp = IoAllocateIrp((CCHAR)(pDevExt->MyDeviceObject->StackSize), FALSE);
            if (pDevExt->pL2ReadBuffer[i].Irp == NULL)
            {
               ExFreePool(pDevExt->pL2ReadBuffer[i].Buffer);
               pDevExt->pL2ReadBuffer[i].Buffer = NULL;
               pDevExt->NumberOfL2Buffers = i;

               if (i == 0)
               {
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_READ,
                     QCUSB_DBG_LEVEL_CRITICAL,
                     ("<%s> InitL2: ERR2 - NO_MEMORY\n", pDevExt->PortName)
                  );
                  return STATUS_NO_MEMORY;
               }
               else
               {
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_READ,
                     QCUSB_DBG_LEVEL_CRITICAL,
                     ("<%s> InitL2: WAR2 - degraded to %d\n", pDevExt->PortName, i)
                  );

                  return STATUS_SUCCESS;
               }
            }
            else
            {
               pDevExt->pL2ReadBuffer[i].Index = i;
               pDevExt->pL2ReadBuffer[i].State = L2BUF_STATE_READY;
               // KeInitializeEvent
               // (
               //   &(pDevExt->pL2ReadBuffer[i].CompletionEvt),
               //   NotificationEvent, FALSE
               // );
            }
         }

         pDevExt->pL2ReadBuffer[i].DataPtr = pDevExt->pL2ReadBuffer[i].Buffer;
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> InitL2: num buf %d\n", pDevExt->PortName, pDevExt->NumberOfL2Buffers)
   );
   return STATUS_SUCCESS;
}  // USBRD_InitializeL2Buffers

VOID USBRD_FillReadRequest(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS ntStatus = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status;
   int oldFillIdx;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   if (pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bFilled != TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> L1: no buf for filling\n", pDevExt->PortName)
      );
      return;
   }

   if (ntStatus == STATUS_SUCCESS)
   {
      USBUTL_PrintBytes
      (
         pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer,
         128,
         pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Length,
         "ReadData",
         pDevExt,
         QCUSB_DBG_MASK_RDATA,
         QCUSB_DBG_LEVEL_VERBOSE
      );
   }

   // check completion status
   QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);

   ntStatus = USBRD_ReadIrpCompletion(pDevExt, 4);

   if ((NT_SUCCESS(ntStatus)) ||  // buffer consumed
       (!NT_SUCCESS(pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status)))  // err return
       
   {
      // reset the buffer record
      pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status  = STATUS_PENDING;
      pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Length  = 0;
      pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bFilled = FALSE;
      oldFillIdx = pDevExt->L2FillIdx;
      if (++(pDevExt->L2FillIdx) == pDevExt->NumberOfL2Buffers)
      {
         pDevExt->L2FillIdx = 0;
      }

      if (oldFillIdx == pDevExt->L2IrpIdx)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> L1: L2 buf available, kick L2\n", pDevExt->PortName)
         );
         // kick L2 thread
         KeSetEvent
         (
            pDevExt->pL2ReadEvents[L2_KICK_READ_EVENT_INDEX],
            IO_NO_INCREMENT,
            FALSE
         );
      }
   }

   if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      pDevExt->bL1Stopped = TRUE;
   }
   QCUSB_DbgPrint2
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> RCOMP to OUT lock -2\n", pDevExt->PortName)
   );
   QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

   return;

}  // USBRD_FillReadRequest

VOID USBRD_SetStopState(PDEVICE_EXTENSION pDevExt, BOOLEAN StopState, UCHAR Cookie)
{
   LARGE_INTEGER checkRegInterval;
   NTSTATUS nts;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> R-SetStopState: %u (%d)\n", pDevExt->PortName, StopState, Cookie)
   );

   if ((pDevExt->hL2ReadThreadHandle == NULL) &&
       (pDevExt->pL2ReadThread == NULL))
   {
      // if L2 is not alive, do nothing
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> R: SetStop: L2 not alive, no act %u\n", pDevExt->PortName, StopState)
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
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> R start - dev removed, no act\n", pDevExt->PortName)
         );
         return;
      }
   }

   if (StopState == TRUE)
   {
      if (pDevExt->UseMultiReads == TRUE)
      {
         KeClearEvent(&pDevExt->L1StopAckEvent);
         KeSetEvent(&pDevExt->L1StopEvent, IO_NO_INCREMENT, FALSE);

         while (TRUE)
         {
            checkRegInterval.QuadPart = -(1 * 1000 * 1000); // .1 sec
            nts = KeWaitForSingleObject
                  (
                     &pDevExt->L1StopAckEvent,
                     Executive,
                     KernelMode,
                     FALSE,
                     &checkRegInterval
                  );

            // CR136843
            if (nts != STATUS_SUCCESS)
            {
               if (USBRD_IsL1Terminated(pDevExt, 0) == TRUE)
               {
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_READ,
                     QCUSB_DBG_LEVEL_ERROR,
                     ("<%s> R SetStop: give up 0x%x\n", pDevExt->PortName, nts)
                  );
                  break;
               }
            }
            else
            {
               KeClearEvent(&pDevExt->L1StopAckEvent);
               break;
            }
         }

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> SetStop: Calling reset L2 Buffers\n", pDevExt->PortName)
         );
         /* Reset L2 Buffers */
         USBMRD_ResetL2Buffers(pDevExt);
         USBMRD_RecycleFrameBuffer(pDevExt);

      }
      else
      { 
         KeSetEvent(&pDevExt->L2StopEvent, IO_NO_INCREMENT, FALSE);
         KeSetEvent(&pDevExt->L1StopEvent, IO_NO_INCREMENT, FALSE);
      }
   }
   else
   {
      if (pDevExt->UseMultiReads == TRUE)
      {
         KeClearEvent(&pDevExt->L1ResumeAckEvent);
         KeSetEvent(&pDevExt->L1ResumeEvent, IO_NO_INCREMENT, FALSE);

         while (TRUE)
         {
            checkRegInterval.QuadPart = -(1 * 1000 * 1000); // .1 sec
            nts = KeWaitForSingleObject
                  (
                     &pDevExt->L1ResumeAckEvent,
                     Executive,
                     KernelMode,
                     FALSE,
                     &checkRegInterval
                  );
            if (nts != STATUS_SUCCESS)
            {
               if (USBRD_IsL1Terminated(pDevExt, 1) == TRUE)
               {
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_READ,
                     QCUSB_DBG_LEVEL_ERROR,
                     ("<%s> R SetStop(resume): give up 0x%x\n", pDevExt->PortName, nts)
                  );
                  break;
               }
            }
            else
            {
               KeClearEvent(&pDevExt->L1ResumeAckEvent);
               break;
            }
         }
      }
      else
      {
         KeSetEvent(&pDevExt->L2ResumeEvent, IO_NO_INCREMENT, FALSE);
         KeSetEvent(&pDevExt->L1ResumeEvent, IO_NO_INCREMENT, FALSE);
      }
   }
}  // USBRD_SetStopState

BOOLEAN USBRD_IsL1Terminated(PDEVICE_EXTENSION pDevExt, UCHAR Cookie)
{
   // check if L1 is terminated
   if ((pDevExt->hL1ReadThreadHandle == NULL) &&
       (pDevExt->pL1ReadThread == NULL))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> R: L1 terminated, no act-%d\n", pDevExt->PortName, Cookie)
      );
      return TRUE;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_ERROR,
      ("<%s> R: SetStop: L1 still alive, wait...%d\n", pDevExt->PortName, Cookie)
   );
   return FALSE;
}  // USBRD_IsL1Terminated

NTSTATUS USBRD_L2Suspend(PDEVICE_EXTENSION pDevExt)
{
   LARGE_INTEGER delayValue;
   NTSTATUS nts = STATUS_UNSUCCESSFUL;

   if ((pDevExt->hL2ReadThreadHandle != NULL) ||
       (pDevExt->pL2ReadThread != NULL))
   {
      delayValue.QuadPart = -(50 * 1000 * 1000); // 5 seconds

      KeClearEvent(&pDevExt->L2StopAckEvent);
      KeSetEvent(&pDevExt->L2StopEvent, IO_NO_INCREMENT, FALSE);

      nts = KeWaitForSingleObject
            (
               &pDevExt->L2StopAckEvent,
               Executive,
               KernelMode,
               FALSE,
               &delayValue
            );
      if (nts == STATUS_TIMEOUT)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_READ,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> L2Suspend: err - WTO\n", pDevExt->PortName)
         );
         KeClearEvent(&pDevExt->L2StopEvent);
      }
      KeClearEvent(&pDevExt->L2StopAckEvent);
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_READ,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> L2Suspend: 0x%x\n", pDevExt->PortName, nts)
   );

   return nts;
}  // USBRD_L2Suspend

NTSTATUS USBRD_L2Resume(PDEVICE_EXTENSION pDevExt)
{
   if ((pDevExt->hL2ReadThreadHandle != NULL) ||
       (pDevExt->pL2ReadThread != NULL))
   {
      KeSetEvent(&pDevExt->L2ResumeEvent, IO_NO_INCREMENT, FALSE);
   }

   return STATUS_SUCCESS;

}  // USBRD_L2Resume

#ifdef QC_IP_MODE
NTSTATUS USBRD_LoopbackPacket
(
   PDEVICE_OBJECT DeviceObject,
   PVOID          DataPacket,
   ULONG          Length
)
{
   PDEVICE_EXTENSION  pDevExt;
   PIRP               pIrp = NULL;
   NTSTATUS           ntStatus = STATUS_SUCCESS;
   PLIST_ENTRY        headOfList;
   PUCHAR             ioBuffer = NULL;
   ULONG              ulReqLength;
   PIO_STACK_LOCATION irpStack;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL              levelOrHandle;
   #endif

   pDevExt = DeviceObject->DeviceExtension;

   QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);

   if (!IsListEmpty(&pDevExt->ReadDataQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->ReadDataQueue);
      pIrp = CONTAINING_RECORD(headOfList, IRP, Tail.Overlay.ListEntry);
DbgPrint("<qnetxxx> IPO RDQ: removed from loopback, IRP 0x%p\n", pIrp);
   }

   if (pIrp != NULL)
   {
      irpStack = IoGetCurrentIrpStackLocation(pIrp);
      if (pIrp->MdlAddress)
      {
         ulReqLength = MmGetMdlByteCount(pIrp->MdlAddress);
         ioBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, HighPagePriority);
         if (ioBuffer == NULL)
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_READ,
               QCUSB_DBG_LEVEL_CRITICAL,
               ("<%s> IPO: LPBK: err Mdl translation 0x%p\n", pDevExt->PortName, pIrp->MdlAddress)
            );
         }
      }
      else
      {
         ulReqLength = irpStack->Parameters.Read.Length;
         ioBuffer = (PUCHAR)pIrp->AssociatedIrp.SystemBuffer;
      }

      if (Length > ulReqLength)
      {
         ntStatus = STATUS_BUFFER_TOO_SMALL;
      }
      if (ioBuffer == NULL)
      {
         ntStatus = STATUS_NO_MEMORY;
      }

      if (ntStatus == STATUS_SUCCESS)
      {
         RtlCopyMemory(ioBuffer, DataPacket, Length);
         pIrp->IoStatus.Information = Length;
      }
      else
      {
         pIrp->IoStatus.Information = 0;

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_RIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> IPO: LPBK: ST 0x%x\n", pDevExt->PortName, ntStatus)
         );
      }

      if (IoSetCancelRoutine(pIrp, NULL) == NULL)
      {
         // the IRP is in Cancellation
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_RIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> IPO: LPBK: RIC2 RIRP 0x%p: Cxl\n", pDevExt->PortName, pIrp)
         );
         QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
         return ntStatus;
      }

      pIrp->IoStatus.Status = ntStatus;

      InsertTailList(&pDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
   }

   QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

   return ntStatus;
}  // USBRD_LoopbackPacket
#endif // QC_IP_MODE
