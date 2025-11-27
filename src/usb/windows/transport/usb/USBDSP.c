/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B D S P . C

GENERAL DESCRIPTION
  This file contains dispatch functions.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include "USBMAIN.h"
#include "USBDSP.h"
#include "USBIOC.h"
#include "USBWT.h"
#include "USBRD.h"
#include "USBINT.h"
#include "USBUTL.h"
#include "USBPNP.h"
#include "USBIF.h"
#include "USBENC.h"
#include "USBPWR.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBDSP.tmh"

#endif   // EVENT_TRACING

extern USHORT ucThCnt;

NTSTATUS USBDSP_DirectDispatch(IN PDEVICE_OBJECT CalledDO, IN PIRP Irp)
{
   PIO_STACK_LOCATION     irpStack;
   NTSTATUS               ntStatus;
   PDEVICE_EXTENSION      pDevExt;
   PDEVICE_OBJECT         DeviceObject;
   KIRQL irql = KeGetCurrentIrql();
   BOOLEAN                bRemoved;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrintG2
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("DDSP: ------------- Dispatch 0x%p ---------\n", Irp)
   );

   irpStack = IoGetCurrentIrpStackLocation(Irp);

   DeviceObject = CalledDO;
   if (DeviceObject == NULL)
   {
      QCUSB_DbgPrintG
      (
         QCUSB_DBG_MASK_CIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CIRP: No PTDO - 0x%p\n", gDeviceName, Irp)
      );
      USBMAIN_DispatchDebugOutput(Irp, irpStack, DeviceObject, irql);
      USBUTL_CompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
      return STATUS_DELETE_PENDING;
   }
   else
   {
      pDevExt = DeviceObject->DeviceExtension;

      if (gVendorConfig.DebugMask > 0)
      {
         USBMAIN_DispatchDebugOutput(Irp, irpStack, DeviceObject, irql);
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> dCIRP: 0x%p => t 0x%p\n", pDevExt->PortName, Irp, KeGetCurrentThread())
   );

   if (pDevExt->StackDeviceObject == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CIRP: No PDO - 0x%p\n", pDevExt->PortName, Irp)
      );
      USBUTL_CompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
      return STATUS_DELETE_PENDING;
   }

   // Acquire remove lock except when getting IRP_MJ_CLOSE
   if (irpStack->MajorFunction != IRP_MJ_CLOSE)
   {
      ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, Irp);
      if (!NT_SUCCESS(ntStatus))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> CIRP: (Ce 0x%x) 0x%p RMLerr-D\n", pDevExt->PortName, ntStatus, Irp)
         );
         USBUTL_CompleteRequest(Irp, ntStatus, 0);
         return ntStatus;
      }
      InterlockedIncrement(&(pDevExt->Sts.lRmlCount[0]));
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> dDSP-RML-0 <%ld, %ld, %ld, %ld> RW <%ld, %ld>\n",
        pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
        pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
        pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedWrites)
   );

   return USBDSP_Dispatch(DeviceObject, CalledDO, Irp, &bRemoved);

}  // USBDSP_DirectDispatch

NTSTATUS USBDSP_QueuedDispatch(IN PDEVICE_OBJECT CalledDO, IN PIRP Irp)
{
   ULONG                  ioControlCode;
   PIO_STACK_LOCATION     irpStack;
   NTSTATUS               ntStatus;
   PDEVICE_EXTENSION      pDevExt;
   PDEVICE_OBJECT         DeviceObject;
   KIRQL irql = KeGetCurrentIrql();

   QCUSB_DbgPrintG2
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("QDSP: ------------- Dispatch 0x%p ---------\n", Irp)
   );

   irpStack = IoGetCurrentIrpStackLocation(Irp);

   DeviceObject = CalledDO;
   if (DeviceObject == NULL)
   {
      QCUSB_DbgPrintG
      (
         (QCUSB_DBG_MASK_CIRP | QCUSB_DBG_MASK_CONTROL),
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> CIRP: No PTDO - 0x%p\n", gDeviceName, Irp)
      );
      USBMAIN_DispatchDebugOutput(Irp, irpStack, DeviceObject, irql);
      USBUTL_CompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
      return STATUS_DELETE_PENDING;
   }
   else
   {
      pDevExt = DeviceObject->DeviceExtension;

      if (gVendorConfig.DebugMask > 0)
      {
         USBMAIN_DispatchDebugOutput(Irp, irpStack, DeviceObject, irql);
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> qCIRP: 0x%p => t 0x%p\n", pDevExt->PortName, Irp, KeGetCurrentThread())
   );

   // Acquire remove lock except when getting IRP_MJ_CLOSE
   if (irpStack->MajorFunction != IRP_MJ_CLOSE)  // condition always TRUE
   {
      ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, Irp);
      if (!NT_SUCCESS(ntStatus))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> CIRP: (Ce 0x%x) 0x%p RMLerr-Q\n", pDevExt->PortName, ntStatus, Irp)
         );
         USBUTL_CompleteRequest(Irp, ntStatus, 0);
         return ntStatus;
      }
      InterlockedIncrement(&(pDevExt->Sts.lRmlCount[0]));
   }

   ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;

   if (irpStack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
   {
      if ((!inDevState(DEVICE_STATE_PRESENT_AND_STARTED)) &&  // device detached
          (pDevExt->bInService == FALSE)                  &&  // port not in use
          (gVendorConfig.DriverResident == 0))                // driver not resident
      {
         // QCUSB_DbgPrint
         // (
         //    QCUSB_DBG_MASK_CIRP,
         //    QCUSB_DBG_LEVEL_ERROR,
         //    ("<%s> CIRP: (Ce 0x%x) 0x%p no DEV\n", pDevExt->PortName, STATUS_DELETE_PENDING, Irp)
         // );
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
         USBUTL_CompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
         return STATUS_DELETE_PENDING;
      }

      if (ioControlCode == IOCTL_QCDEV_READ_CONTROL)
      {
         return USBENC_Enqueue(DeviceObject, Irp, irql);
      }
   }

   if (irpStack->MajorFunction == IRP_MJ_PNP)
   {
      switch (irpStack->MinorFunction)
      {
         case IRP_MN_SURPRISE_REMOVAL:
         // case IRP_MN_REMOVE_DEVICE:
         {
            clearDevState(DEVICE_STATE_PRESENT_AND_STARTED);
            setDevState(DEVICE_STATE_SURPRISE_REMOVED);
            USBINT_StopInterruptService(pDevExt, FALSE, FALSE, 4);
            pDevExt->bEncStopped = TRUE;
            USBMAIN_ResetRspAvailableCount(pDevExt, pDevExt->DataInterface, pDevExt->PortName, 5);
            break;
         }
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> qDSP-RML-0 <%ld, %ld, %ld, %ld> RW <%ld, %ld>\n",
        pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1], pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
        pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedWrites)
   );

   return USBDSP_Enqueue(DeviceObject, Irp, irql);

}  // USBDSP_QueuedDispatch

NTSTATUS USBDSP_Enqueue(PDEVICE_OBJECT DeviceObject, PIRP Irp, KIRQL Irql)
{
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   NTSTATUS ntStatus = STATUS_SUCCESS; 
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   ntStatus = STATUS_PENDING;

   QcAcquireSpinLockWithLevel(&pDevExt->ControlSpinLock, &levelOrHandle, Irql);
   if (Irp->Cancel)
   {
      PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> USBDSP_Enqueue: CANCELLED\n", pDevExt->PortName)
      );
      ntStatus = Irp->IoStatus.Status = STATUS_CANCELLED;
      QcReleaseSpinLockWithLevel(&pDevExt->ControlSpinLock, levelOrHandle, Irql);
      goto disp_enq_exit;
   }


   _IoMarkIrpPending(Irp);
   IoSetCancelRoutine(Irp, DispatchCancelQueued);

   if (irpStack->MajorFunction == IRP_MJ_POWER)
   {
      // Give power IRP priority to avoid being blocked by other IRP's
      QCPWR_Enqueue(pDevExt, Irp);
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_READ,
         QCUSB_DBG_LEVEL_TRACE,
         ("<%s> USBDSP_Enqueue; non-PWR IRP 0x%p to tail\n", pDevExt->PortName, Irp)
      );
      InsertTailList(&pDevExt->DispatchQueue, &Irp->Tail.Overlay.ListEntry);
   }

   QcReleaseSpinLockWithLevel(&pDevExt->ControlSpinLock, levelOrHandle, Irql);

   KeSetEvent
   (
      pDevExt->pDispatchEvents[DSP_START_DISPATCH_EVENT_INDEX],
      IO_NO_INCREMENT,
      FALSE
   );

disp_enq_exit:

   if (ntStatus != STATUS_PENDING)
   {
      IoSetCancelRoutine(Irp, NULL);
      Irp->IoStatus.Status = ntStatus;
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> CIRP (C 0x%x) 0x%p\n", pDevExt->PortName, ntStatus, Irp)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
   }

   return ntStatus;
}  // USBDSP_Enqueue

NTSTATUS USBDSP_InitDispatchThread(IN PDEVICE_OBJECT pDevObj)
{
   NTSTATUS ntStatus;
   USHORT usLength, i;
   PDEVICE_EXTENSION pDevExt;
   PIRP pIrp;
   OBJECT_ATTRIBUTES objAttr;

   pDevExt = pDevObj->DeviceExtension;

   pDevExt->pDispatchEvents[DSP_READ_CONTROL_EVENT_INDEX] =
      pDevExt->pReadControlEvent;

   // Make sure the int thread is created at PASSIVE_LEVEL
   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> DSP: wrong IRQL\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((pDevExt->pDispatchThread != NULL) || (pDevExt->hDispatchThreadHandle != NULL))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> DSP up - 1\n", pDevExt->PortName)
      );
      return STATUS_SUCCESS;
   }

   KeClearEvent(&pDevExt->DspThreadStartedEvent);
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   ucThCnt++;
   ntStatus = PsCreateSystemThread
              (
                 OUT &pDevExt->hDispatchThreadHandle,
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)DispatchThread,
                 IN (PVOID) pDevExt
              );         
   if ((!NT_SUCCESS(ntStatus)) || (pDevExt->hDispatchThreadHandle == NULL))
   {
      pDevExt->hDispatchThreadHandle = NULL;
      pDevExt->pDispatchThread = NULL;
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> DSP th failure\n", pDevExt->PortName)
      );
      return ntStatus;
   }

   ntStatus = KeWaitForSingleObject
              (
                 &pDevExt->DspThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );

   ntStatus = ObReferenceObjectByHandle
              (
                 pDevExt->hDispatchThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&pDevExt->pDispatchThread,
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> DSP: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
      );
      pDevExt->pDispatchThread = NULL;
   }
   else
   {
      if (STATUS_SUCCESS != (ntStatus = ZwClose(pDevExt->hDispatchThreadHandle)))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> DSP ZwClose failed - 0x%x\n", pDevExt->PortName, ntStatus)
         );
      }
      else
      {
         ucThCnt--;
         pDevExt->hDispatchThreadHandle = NULL;
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> DSP handle=0x%p thOb=0x%p\n", pDevExt->PortName,
       pDevExt->hDispatchThreadHandle, pDevExt->pDispatchThread)
   );
   
   return ntStatus;

}  // USBDSP_InitDispatchThread

VOID DispatchThread(PVOID pContext)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pContext;
   PIO_STACK_LOCATION pNextStack;
   BOOLEAN bCancelled = FALSE, bKeepLoop = TRUE;
   NTSTATUS  ntStatus;
   PKWAIT_BLOCK pwbArray;
   PIRP pDispatchIrp = NULL;
   BOOLEAN bFirstTime = TRUE;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KEVENT dummyEvent;
   PLIST_ENTRY headOfList;
   LARGE_INTEGER checkRegInterval;
   USHORT errCnt = 0;
   BOOLEAN bDeviceRemoved = FALSE;
   BOOLEAN bOnHold        = FALSE;
   LONG    retries = 0;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> Dth: wrong IRQL\n", pDevExt->PortName)
      );
      #ifdef DEBUG_MSGS
      KdBreakPoint();
      #endif
   }

   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (DSP_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 "DispatchThread.pwbArray"
              );
   if (!pwbArray)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> Dth: STATUS_NO_MEMORY 1\n", pDevExt->PortName)
      );
      _closeHandle(pDevExt->hDispatchThreadHandle, "D-1");
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   KeSetPriorityThread(KeGetCurrentThread(), QCUSB_DSP_PRIORITY);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_INFO,
      ("<%s> D pri=%d\n", pDevExt->PortName, KeQueryPriorityThread(KeGetCurrentThread()))
   );

   pDevExt->bEncStopped = FALSE;
   pDevExt->pDispatchEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
   KeClearEvent(&dummyEvent);
   
   while (bKeepLoop == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_VERBOSE,
         ("<%s> D: BEGIN XwdmIrp 0x%p Cxl %d Rml[0]=%u\n", pDevExt->PortName,
           pDevExt->XwdmNotificationIrp, bCancelled, pDevExt->Sts.lRmlCount[0])
      );
 
      if (bCancelled == FALSE)
      {
         if (pDevExt->PurgeIrp != NULL)
         {
            goto dispatch_wait;
         }

         if ((*(pDevExt->pRspAvailableCount) > 0) && (pDevExt->bEncStopped == FALSE))
         {
            ntStatus = DSP_READ_CONTROL_EVENT_INDEX;
            goto dispatch_case;
         }
      }
      else
      {
         // Purge the SEND_CONTROL items in the DispatchQueue
         USBENC_PurgeQueue(pDevExt, &pDevExt->DispatchQueue, TRUE, 4);
         QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
         if ((IsListEmpty(&pDevExt->DispatchQueue)) &&
             (pDevExt->XwdmNotificationIrp == NULL) &&
             (pDevExt->ExWdmMonitorStarted == FALSE))
         {
            // nothing left, exit main loop
            QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
            bKeepLoop = FALSE;
            continue;
         }
         QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
         goto dispatch_wait;
      }

      if ((bOnHold == TRUE) && (pDevExt->DevicePower > PowerDeviceD0))
      {
         goto dispatch_wait;
      }

      // De-queue
      QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);

      if ((!IsListEmpty(&pDevExt->DispatchQueue)) && (pDispatchIrp == NULL))
      {
         PLIST_ENTRY   peekEntry;

         headOfList = &pDevExt->DispatchQueue;
         peekEntry  = headOfList->Flink;
         pDispatchIrp = CONTAINING_RECORD
                        (
                           peekEntry,
                           IRP,
                           Tail.Overlay.ListEntry
                        );
         if (QCDSP_ToProcessIrp(pDevExt, pDispatchIrp) == FALSE)
         {
            // Place it at the tail to give other IRPs priority
            headOfList = RemoveHeadList(&pDevExt->DispatchQueue);
            InsertTailList(&pDevExt->DispatchQueue, &pDispatchIrp->Tail.Overlay.ListEntry);
            pDispatchIrp = NULL;
            QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
            goto dispatch_wait;
         }

         bOnHold = QCPWR_CheckToWakeup(pDevExt, pDispatchIrp, QCUSB_BUSY_CTRL, 2);
         if (bOnHold == TRUE)
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> Dth: wait for wakeup\n", pDevExt->PortName)
            );
            bOnHold = FALSE;
            pDispatchIrp = NULL;
            QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
            goto dispatch_wait;
         }

         // De-queue
         RemoveEntryList(peekEntry);
         pDevExt->pCurrentDispatch = pDispatchIrp;

         QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

         if (pDispatchIrp != NULL)
         {
            if (IoSetCancelRoutine(pDispatchIrp, NULL) == NULL)
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_CRITICAL,
                  ("<%s> Dth: Cxled IRP 0x%p\n", pDevExt->PortName, pDispatchIrp)
               );
               pDispatchIrp = pDevExt->pCurrentDispatch = NULL;
               QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_CTRL, TRUE, 11);
               continue;
            }
            else if (bCancelled == TRUE)
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CIRP,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> CIRP: (Cx 0x%p DSP RmlCount[0]=%u)\n", pDevExt->PortName, pDispatchIrp,
                    pDevExt->Sts.lRmlCount[0])
               );
               pDispatchIrp->IoStatus.Status = STATUS_CANCELLED;
               QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pDispatchIrp, 0);
               IoCompleteRequest(pDispatchIrp, IO_NO_INCREMENT);
               pDispatchIrp = pDevExt->pCurrentDispatch = NULL;
               QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_CTRL, TRUE, 12);
               continue;
            }

            ntStatus = USBDSP_Dispatch
                       (
                          pDevExt->MyDeviceObject,
                          NULL,
                          pDispatchIrp,
                          &bDeviceRemoved
                       );
            pDispatchIrp = pDevExt->pCurrentDispatch = NULL;

            if (bDeviceRemoved == FALSE)
            {
               QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_CTRL, FALSE, 16);
            }

            // we don't try to complete the IRP, the dispatch routine will.
         }
         else
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> Dth: ERR - NULL IRP\n", pDevExt->PortName)
            );
         }
         continue;
      } 
      else
      {
         QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
         if ((bCancelled == TRUE) && (pDevExt->XwdmNotificationIrp == NULL) &&
             (pDevExt->ExWdmMonitorStarted == FALSE))
         {
            bKeepLoop = FALSE;
            continue;
         }
      }  //if ((!IsListEmpty(&pDevExt->DispatchQueue)) && (pDispatchIrp == NULL))

dispatch_wait:

      if (bFirstTime == TRUE)
      {
         bFirstTime = FALSE;
         KeSetEvent(&pDevExt->DspThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      }

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_VERBOSE,
         ("<%s> D: WAIT...Rml[0]=%u\n", pDevExt->PortName, pDevExt->Sts.lRmlCount[0])
      );

      checkRegInterval.QuadPart = -(10 * 1000 * 1000); // 1.0 sec
      ntStatus = KeWaitForMultipleObjects
                 (
                    DSP_EVENT_COUNT,
                    (VOID **)&pDevExt->pDispatchEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,
                    &checkRegInterval,
                    pwbArray
                 );

dispatch_case:

      if ((bCancelled == TRUE) && (pDevExt->XwdmNotificationIrp == NULL) &&
          (pDevExt->ExWdmMonitorStarted == FALSE))
      {
         continue;
      }

      switch (ntStatus)
      {
         case QC_DUMMY_EVENT_INDEX:
         {
            KeClearEvent(&dummyEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: DUMMY_EVENT\n", pDevExt->PortName)
            );
            goto dispatch_wait;
         }

         case DSP_START_DISPATCH_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->DispatchStartEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_VERBOSE,
               ("<%s> D: START_DISPATCH_EVENT\n", pDevExt->PortName)
            );
            continue;
         }

         case DSP_CANCEL_DISPATCH_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->DispatchCancelEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: CANCEL_DISPATCH_EVENT\n", pDevExt->PortName)
            );
            bCancelled = TRUE;
            USBENC_PurgeQueue(pDevExt, &pDevExt->EncapsulatedDataQueue, FALSE, 1);
            continue;
         }

         case DSP_READ_CONTROL_EVENT_INDEX:
         {
            KeClearEvent(pDevExt->pDispatchEvents[DSP_READ_CONTROL_EVENT_INDEX]);

            if (pDevExt->PrepareToPowerDown == TRUE)
            {
               // bail out, RspAvailableCount will be reset by DispatchStandbyEvent
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_ENC,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> D: READ_CONTROL_EVENT(%d, %d, rm%d) powering down \n", pDevExt->PortName,
                    pDevExt->DataInterface,
                    *(pDevExt->pRspAvailableCount), pDevExt->bDeviceRemoved)
               );
               USBUTL_Wait(pDevExt, -(100 * 1000L)); // relax for 10ms
               if (++retries >= 10)
               {
                  retries = 0;
                  USBSHR_ResetRspAvailableCount(pDevExt->MyDeviceObject, pDevExt->DataInterface);
               }
               break;
            }

            while ((*(pDevExt->pRspAvailableCount) > 0) &&
                   (pDevExt->PurgeIrp == NULL)          &&
                   inDevState(DEVICE_STATE_PRESENT)     &&
                   (pDevExt->bEncStopped == FALSE))
            {
               NTSTATUS nts;
               USHORT interface;

               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_ENC,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> D: READ_CONTROL_EVENT(%d, %d, rm%d) -- 0\n", pDevExt->PortName,
                    pDevExt->DataInterface,
                    *(pDevExt->pRspAvailableCount), pDevExt->bDeviceRemoved)
               );

               if (pDevExt->IsEcmModel == TRUE)
               {
                  interface = pDevExt->usCommClassInterface;
               }
               else
               {
                  interface = pDevExt->DataInterface;
               }

               // dequeue IRP from IOCTL_QCDEV_READ_CONTROL
               nts = USBENC_CDC_GetEncapsulatedResponse
                     (
                        pDevExt->MyDeviceObject, 
                        interface
                     );

               if (NT_SUCCESS(nts) || (nts == STATUS_CANCELLED))
               {
                  errCnt = 0;
               }
               else
               {
                  ++errCnt;
                  if (errCnt > pDevExt->NumOfRetriesOnError)
                  {
                     NTSTATUS nts;

                     // try to reset
                     nts = QCUSB_ResetInt(pDevExt->MyDeviceObject, QCUSB_RESET_PIPE_AND_ENDPOINT);
                     if ((NT_SUCCESS(nts)) && inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
                     {
                        QCUSB_DbgPrint
                        (
                           (QCUSB_DBG_MASK_READ | QCUSB_DBG_MASK_ENC),
                           QCUSB_DBG_LEVEL_ERROR,
                           ("<%s> RD_CTL reset on error, retrying...%u\n\n", pDevExt->PortName, errCnt)
                        );
                        errCnt = 0;
                        InterlockedExchange(pDevExt->pRspAvailableCount, 0);
                     }
                     else  // reset failure
                     {
                        clearDevState(DEVICE_STATE_PRESENT);
                        errCnt = pDevExt->NumOfRetriesOnError;
                        QCUSB_DbgPrint
                        (
                           (QCUSB_DBG_MASK_READ | QCUSB_DBG_MASK_ENC),
                           QCUSB_DBG_LEVEL_ERROR,
                           ("<%s> RD_CTL err %d - dev removed\n\n", pDevExt->PortName, errCnt)
                        );
                     }
                     USBUTL_Wait(pDevExt, -(50 * 1000L)); // 5ms
                  }
               }

               if (nts != STATUS_SUCCESS)
               {
                  // we received nothing, so do not decrease the RspAvailableCount
                  QCUSB_DbgPrint
                  (
                     (QCUSB_DBG_MASK_READ | QCUSB_DBG_MASK_ENC),
                     QCUSB_DBG_LEVEL_ERROR,
                     ("<%s> RD_CTL err 0x%x\n", pDevExt->PortName, nts)
                  );
                  USBUTL_Wait(pDevExt, -(100 * 1000L)); // 10ms
                  continue;
               }

               QcAcquireSpinLock(&gGenSpinLock, &levelOrHandle);

               InterlockedDecrement(pDevExt->pRspAvailableCount);
               if (*(pDevExt->pRspAvailableCount) < 0)
               {
                  QCUSB_DbgPrint
                  (
                     (QCUSB_DBG_MASK_READ | QCUSB_DBG_MASK_ENC),
                     QCUSB_DBG_LEVEL_ERROR,
                     ("<%s> D: RspAvailCnt err(%d, %d) -- 0\n", pDevExt->PortName,
                       pDevExt->DataInterface,
                       *(pDevExt->pRspAvailableCount))
                  );
                  InterlockedExchange(pDevExt->pRspAvailableCount, 0);
               }

               QcReleaseSpinLock(&gGenSpinLock, levelOrHandle);

            }  // while

            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: READ_CONTROL_EVENT (IF%d) -- end CNT %d\n", pDevExt->PortName,
                 pDevExt->DataInterface, *(pDevExt->pRspAvailableCount))
            );

            if (*(pDevExt->pRspAvailableCount) > 0)
            {
               // is it possible we didn't even enter the while loop?
               USBUTL_Wait(pDevExt, -(100 * 1000L)); // relax for 10ms
            }

            continue;
         }

         case DSP_PURGE_EVENT_INDEX:
         {
            // this event is issued by the L1 thread
            KeClearEvent(&pDevExt->DispatchPurgeEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: PURGE_EVENT\n", pDevExt->PortName)
            );

            // Purge the SEND_CONTROL items in the DispatchQueue
            USBENC_PurgeQueue(pDevExt, &pDevExt->DispatchQueue, TRUE, 2);

            KeSetEvent
            (
               &pDevExt->DispatchPurgeCompleteEvent,
               IO_NO_INCREMENT,
               FALSE
            );
            break;
         }
         case DSP_STANDBY_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->DispatchStandbyEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: STANDBY_EVENT\n", pDevExt->PortName)
            );

            if (pDevExt->bLowPowerMode == TRUE)
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> D: STANDBY: already in low power mode\n", pDevExt->PortName)
               );
               break;
            }

            // D0 -> Dn, stop Device
            pDevExt->PowerSuspended = TRUE;
            pDevExt->bLowPowerMode = TRUE;

            // If device powers down, drop DTR
            // USBPWR_GetSystemPowerState(pDevExt, QCUSB_SYS_POWER_CURRENT);
            if ((pDevExt->PowerState >= PowerDeviceD3) &&
                ((pDevExt->SystemPower > PowerSystemWorking) ||
                 (pDevExt->PrepareToPowerDown == TRUE)))
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> STANDBY: drop DTR\n", pDevExt->PortName)
               );
               USBCTL_ClrDtrRts(pDevExt->MyDeviceObject);
            }
            else
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> STANDBY: still in S0, keep DTR\n", pDevExt->PortName)
               );
            }
            pDevExt->bL2Polling = FALSE;
            USBRD_SetStopState(pDevExt, TRUE, 2);
            USBWT_SetStopState(pDevExt, TRUE);
            USBINT_StopInterruptService(pDevExt, FALSE, FALSE, 3);
            USBMAIN_ResetRspAvailableCount
            (
               pDevExt,
               pDevExt->DataInterface,
               pDevExt->PortName,
               4
            );
            break;
         }
         case DSP_WAKEUP_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->DispatchWakeupEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: WAKEUP_EVENT\n", pDevExt->PortName)
            );
            pDevExt->bLowPowerMode = FALSE;
            // Before bringing up data pipes, try to enable TLP
            ntStatus = USBCTL_EnableDataBundling
                       (
                          pDevExt->MyDeviceObject,
                          pDevExt->DataInterface,
                          QC_LINK_DL
                       );
            if (!NT_SUCCESS(ntStatus))
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> DSP-rst: DL bundling err 0x%x\n", pDevExt->PortName, ntStatus)
               );
            }
            pDevExt->PowerSuspended = pDevExt->SelectiveSuspended = FALSE;
            pDevExt->bL2Polling = TRUE;
            USBRD_SetStopState(pDevExt, FALSE, 3);
            USBWT_SetStopState(pDevExt, FALSE);
            USBINT_ResumeInterruptService(pDevExt, 1);
            USBCTL_SetDtr(pDevExt->MyDeviceObject);
            break;
         }
         case DSP_WAKEUP_RESET_EVENT_INDEX:
         {
            NTSTATUS ntStatus;

            KeClearEvent(&pDevExt->DispatchWakeupResetEvent);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: WAKEUP_RESET_EVENT\n", pDevExt->PortName)
            );

            pDevExt->bLowPowerMode = FALSE;
            ntStatus = QCUSB_ResetInt(pDevExt->MyDeviceObject, QCUSB_RESET_PIPE_AND_ENDPOINT);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: WAKEUP_RESET-ResetInt 0x%x\n", pDevExt->PortName, ntStatus)
            );
            if (ntStatus == STATUS_SUCCESS)
            {
               QCUSB_ResetInput(pDevExt->MyDeviceObject, QCUSB_RESET_PIPE_AND_ENDPOINT);
               QCUSB_ResetOutput(pDevExt->MyDeviceObject, QCUSB_RESET_PIPE_AND_ENDPOINT);
            }
            // Before bringing up data pipes, try to enable TLP
            ntStatus = USBCTL_EnableDataBundling
                       (
                          pDevExt->MyDeviceObject,
                          pDevExt->DataInterface,
                          QC_LINK_DL
                       );
            if (!NT_SUCCESS(ntStatus))
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> DSP-rst: DL bundling err 0x%x\n", pDevExt->PortName, ntStatus)
               );
            }
            pDevExt->PowerSuspended = pDevExt->SelectiveSuspended = FALSE;
            pDevExt->bL2Polling = TRUE;
            USBRD_SetStopState(pDevExt, FALSE, 4);
            USBWT_SetStopState(pDevExt, FALSE);
            USBINT_ResumeInterruptService(pDevExt, 1);
            USBCTL_SetDtr(pDevExt->MyDeviceObject);
            break;
         }

         case DSP_PRE_WAKEUP_EVENT_INDEX:
         {
            KeClearEvent(&(pDevExt->DispatchPreWakeupEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: -->PRE_WAKEUP\n", pDevExt->PortName)
            );

            if (pDevExt->PowerSuspended == TRUE)
            {
               // Wakeup the device
               QCPWR_CancelIdleNotificationIrp(pDevExt, 2);
            }
            USBUTL_Wait(pDevExt, -(100 * 1000L)); // relax for 10ms
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: <--PRE_WAKEUP\n", pDevExt->PortName)
            );
            break;
         }

         case DSP_XWDM_OPEN_EVENT_INDEX:
         {
            if (!inDevState(DEVICE_STATE_PRESENT))
            {
               break;
            }

            KeClearEvent(&(pDevExt->DispatchXwdmOpenEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: -->OPEN_WDM_DEV\n", pDevExt->PortName)
            );
            USBPWR_OpenExWdmDevice(pDevExt);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: <--OPEN_WDM_DEV\n", pDevExt->PortName)
            );
            break;
         }

         case DSP_XWDM_CLOSE_EVENT_INDEX:
         {
            KeClearEvent(&(pDevExt->DispatchXwdmCloseEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: -->CLOSE_WDM_DEV\n", pDevExt->PortName)
            );
            USBPWR_CloseExWdmDevice(pDevExt);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: <--CLOSE_WDM_DEV\n", pDevExt->PortName)
            );
            break;
         }

         case DSP_XWDM_NOTIFY_EVENT_INDEX:
         {
            KeClearEvent(&(pDevExt->DispatchXwdmNotifyEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: -->XWDM_NOTIFY 0x%p\n", pDevExt->PortName, pDevExt->XwdmNotificationIrp)
            );
            if (pDevExt->XwdmNotificationIrp != NULL)
            {
               IoFreeIrp(pDevExt->XwdmNotificationIrp);
               pDevExt->XwdmNotificationIrp = NULL;
            }
            USBPWR_CloseExWdmDevice(pDevExt);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: <--XWDM_NOTIFY 0x%p\n", pDevExt->PortName, pDevExt->XwdmNotificationIrp)
            );
            break;
         } 

         case DSP_XWDM_QUERY_RM_EVENT_INDEX:
         {
            if (!inDevState(DEVICE_STATE_PRESENT))
            {
               break;
            }

            KeClearEvent(&(pDevExt->DispatchXwdmQueryRmEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: -->XWDM_Q_RM\n", pDevExt->PortName)
            );
            USBPWR_HandleExWdmQueryRemove(pDevExt);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: <--XWDM_Q_RM\n", pDevExt->PortName)
            );
            break;
         }

         case DSP_XWDM_RM_CANCELLED_EVENT_INDEX:
         {
            if (!inDevState(DEVICE_STATE_PRESENT))
            {
               break;
            }

            KeClearEvent(&(pDevExt->DispatchXwdmReopenEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: -->XWDM_RM_CANCELLED\n", pDevExt->PortName)
            );
            USBPWR_HandleExWdmReopen(pDevExt);
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: <--XWDM_RM_CANCELLED\n", pDevExt->PortName)
            );
            break;
         }

         case DSP_XWDM_QUERY_S_PWR_EVENT_INDEX:
         {
            if (!inDevState(DEVICE_STATE_PRESENT))
            {
               break;
            }

            KeClearEvent(&(pDevExt->DispatchXwdmQuerySysPwrEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: -->QUERY_SYS_PWR\n", pDevExt->PortName)
            );
            // USBPWR_GetSystemPowerState(pDevExt, QCUSB_SYS_POWER_CURRENT);
            KeSetEvent
            (
               &pDevExt->DispatchXwdmQuerySysPwrAckEvent,
               IO_NO_INCREMENT,
               FALSE
            );
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: <--QUERY_SYS_PWR\n", pDevExt->PortName)
            );
            break;
         }

         case DSP_START_POLLING_EVENT_INDEX:
         {
            if (!inDevState(DEVICE_STATE_PRESENT))
            {
               break;
            }

            KeClearEvent(&(pDevExt->DispatchStartPollingEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: -->START_POLLING\n", pDevExt->PortName)
            );
            pDevExt->bL2Polling = TRUE;
            USBRD_SetStopState(pDevExt, FALSE, 5);
            break;
         }
         case DSP_STOP_POLLING_EVENT_INDEX:
         {
            KeClearEvent(&(pDevExt->DispatchStopPollingEvent));
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_DETAIL,
               ("<%s> D: -->STOP_POLLING\n", pDevExt->PortName)
            );
            pDevExt->bL2Polling = FALSE;
            USBRD_SetStopState(pDevExt, TRUE, 6);
            break;
         }

         default:
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_VERBOSE,
               ("<%s> D: default sig or TO 0x%x (RmlCount[0]=%d)\n",
                 pDevExt->PortName, ntStatus, pDevExt->Sts.lRmlCount[0])
            );
            continue;
         }
      }
   } // while (TRUE)

   if(pwbArray)
   {
      _ExFreePool(pwbArray);
   }

   KeSetEvent
   (
      &pDevExt->DispatchThreadClosedEvent,
      IO_NO_INCREMENT,
      FALSE
   );
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_CRITICAL,
      ("<%s> Dth: OUT RML (%ld,%ld,%ld,%ld) RW <%ld, %ld>\n",
        pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
        pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
        pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedWrites)
   );

   PsTerminateSystemThread(STATUS_SUCCESS); // end this thread
}  // DispatchThread

NTSTATUS USBDSP_CancelDispatchThread(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER delayValue;
   PVOID dispatchThread;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> DSP Cxl - %d(0)\n", pDevExt->PortName, cookie)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_CRITICAL,
         ("<%s> DSP Cxl: wrong IRQL - %d\n", pDevExt->PortName, cookie)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (InterlockedIncrement(&pDevExt->DispatchThreadCancelStarted) > 1)
   {
      while ((pDevExt->hDispatchThreadHandle != NULL) || (pDevExt->pDispatchThread != NULL))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> Dth cxl in pro\n", pDevExt->PortName)
         );
         USBUTL_Wait(pDevExt, -(3 * 1000 * 1000));
      }
      InterlockedDecrement(&pDevExt->DispatchThreadCancelStarted);      
      return STATUS_SUCCESS;
   }

   if ((pDevExt->hDispatchThreadHandle != NULL) || (pDevExt->pDispatchThread != NULL))
   {
      KeClearEvent(&pDevExt->DispatchThreadClosedEvent);
      KeSetEvent
      (
         &pDevExt->DispatchCancelEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pDevExt->pDispatchThread != NULL)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> Dth: wait for closure -1\n", pDevExt->PortName)
         );
         ntStatus = KeWaitForSingleObject
                    (
                       pDevExt->pDispatchThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pDevExt->pDispatchThread);
         KeClearEvent(&pDevExt->DispatchThreadClosedEvent);
         _closeHandle(pDevExt->hDispatchThreadHandle, "D-5");
         pDevExt->pDispatchThread = NULL;
      }
      else  // best effort
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> Dth: wait for closure -2\n", pDevExt->PortName)
         );
         ntStatus = KeWaitForSingleObject
                    (
                       &pDevExt->DispatchThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pDevExt->DispatchThreadClosedEvent);
         _closeHandle(pDevExt->hDispatchThreadHandle, "D-6");
      }

   }
   InterlockedDecrement(&pDevExt->DispatchThreadCancelStarted);      

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> Dth: cxl - %d(1)\n", pDevExt->PortName, cookie)
   );

   return ntStatus;
} // USBDSP_CancelDispatchThread

VOID DispatchCancelQueued(PDEVICE_OBJECT CalledDO, PIRP Irp)
{
   KIRQL irql = KeGetCurrentIrql();
   PDEVICE_OBJECT DeviceObject = CalledDO;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
   BOOLEAN bIrpInQueue;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> DSP CQed: 0x%p\n", pDevExt->PortName, Irp)
   );
   IoReleaseCancelSpinLock(Irp->CancelIrql);

   // De-queue
   QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
   bIrpInQueue = USBUTL_IsIrpInQueue
                 (
                    pDevExt,
                    &pDevExt->DispatchQueue,
                    Irp,
                    QCUSB_IRP_TYPE_CIRP,
                    3
                 );

   if (bIrpInQueue == TRUE)
   {
      RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
      Irp->IoStatus.Status = STATUS_CANCELLED;
      Irp->IoStatus.Information = 0;
      InsertTailList(&pDevExt->CtlCompletionQueue, &Irp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyCtlQueueEvent, IO_NO_INCREMENT, FALSE);
   }
   else
   {
      // The IRP is in processing, since transfer over control EP has
      // time constraint, we just re-install the cancel routine here.
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> DSP_Cxl: no action to active 0x%p\n", pDevExt->PortName, Irp)
      );
      IoSetCancelRoutine(Irp, DispatchCancelQueued);
   }

   QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
}  // DispatchCancelQueued


/*****************************************************************************
 *****************************************************************************
 *****************************************************************************
 *****************************************************************************/

NTSTATUS USBDSP_GetMUXInterfaceCompletion(
                           PDEVICE_OBJECT pDO,
                           PIRP           pIrp,
                           PVOID          pContext
                           )
{
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pContext;
    QCUSB_DbgPrint
    (
       QCUSB_DBG_MASK_CONTROL,
       QCUSB_DBG_LEVEL_TRACE,
       ("<%s> ---> USBDSP_GetMUXInterfaceCompletion (IRP 0x%p St 0x%x)\n", pDevExt->PortName, pIrp, pIrp->IoStatus.Status)
    );

   KeSetEvent(pIrp->UserEvent, 0, FALSE);
   return STATUS_MORE_PROCESSING_REQUIRED;
}

/*****************************************************************************
 *****************************************************************************
 *****************************************************************************
 *****************************************************************************/

void USBDSP_GetMUXInterface(PDEVICE_EXTENSION  pDevExt, UCHAR InterfaceNumber)
{
    PIRP pIrp = NULL;
    PIO_STACK_LOCATION nextstack;
    NTSTATUS Status;
    KEVENT Event;
    
    //
    // Set up the event we'll use.
    //
    
    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    QCUSB_DbgPrint
    (
       QCUSB_DBG_MASK_CONTROL,
       QCUSB_DBG_LEVEL_TRACE,
       ("<%s> ---> USBDSP_GetMUXInterface and Interface Number 0x%x \n", pDevExt->PortName, InterfaceNumber)
    );

    pIrp = IoAllocateIrp((CCHAR)(pDevExt->StackDeviceObject->StackSize+1), FALSE );

    if( pIrp == NULL )
    {
        QCUSB_DbgPrint
        (
           QCUSB_DBG_MASK_CONTROL,
           QCUSB_DBG_LEVEL_ERROR,
           ("<%s> USBDSP_GetMUXInterface failed to allocate an IRP\n", pDevExt->PortName)
        );
        return;
    }

    // set the event
    pIrp->UserEvent = &Event;
    
    pIrp->AssociatedIrp.SystemBuffer = &(pDevExt->MuxInterface);
    nextstack = IoGetNextIrpStackLocation(pIrp);
    nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    nextstack->Parameters.DeviceIoControl.IoControlCode = IOCTL_QCDEV_GET_MUX_INTERFACE;
    nextstack->Parameters.Read.Length = sizeof(MUX_INTERFACE_INFO);
    nextstack->Parameters.DeviceIoControl.InputBufferLength = 0x01;
    nextstack->Parameters.DeviceIoControl.OutputBufferLength = sizeof(MUX_INTERFACE_INFO);
    (*(UCHAR *)pIrp->AssociatedIrp.SystemBuffer) = InterfaceNumber;

    IoSetCompletionRoutine(
                          pIrp, 
                          (PIO_COMPLETION_ROUTINE)USBDSP_GetMUXInterfaceCompletion,
                          (PVOID)pDevExt, 
                          TRUE,TRUE,TRUE
                          );

    // Status = QCIoCallDriver(pAdapter->USBDo, pIrp);
    Status = IoCallDriver(pDevExt->StackDeviceObject, pIrp);

    if ( Status == STATUS_PENDING)
    {
       KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, 0);
       Status = pIrp->IoStatus.Status;
    }

    QCUSB_DbgPrint
    (
       QCUSB_DBG_MASK_CONTROL,
       QCUSB_DBG_LEVEL_TRACE,
       ("<%s> <--- USBDSP_GetMUXInterface IRP 0x%p (Status: %d)\n", 
         pDevExt->PortName, pIrp, Status)
    );

    QCUSB_DbgPrint
    (
       QCUSB_DBG_MASK_CONTROL,
       QCUSB_DBG_LEVEL_TRACE,
       ("<%s> <--- USBDSP_GetMUXInterface MUX_INTERFACE_INFO %d %d %d 0x%p \n", 
         pDevExt->PortName, pDevExt->MuxInterface.InterfaceNumber,
         pDevExt->MuxInterface.PhysicalInterfaceNumber, pDevExt->MuxInterface.MuxEnabled, pDevExt->MuxInterface.FilterDeviceObj)
    );
    
    if (Status == STATUS_SUCCESS)
    {
       
    }

    IoFreeIrp(pIrp);
    return;
}

NTSTATUS USBDSP_Dispatch
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PDEVICE_OBJECT CalledDO,
   IN PIRP Irp,
   IN OUT BOOLEAN *Removed
)
{
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   PIO_STACK_LOCATION irpStack, nextStack;
   PDEVICE_EXTENSION pDevExt;
   PVOID ioBuffer;
   ULONG inputBufferLength, outputBufferLength, ioControlCode;
   NTSTATUS ntStatus = STATUS_SUCCESS, ntCloseStatus = STATUS_SUCCESS, myNts;
   PDEVICE_OBJECT pUsbDevObject;
   USHORT usLength;
   BOOLEAN bRemoveRequest = FALSE;
   KEVENT ePNPEvent;
   char myPortName[16];
   KIRQL irqLevel = KeGetCurrentIrql();

   pDevExt = pDevExt = DeviceObject->DeviceExtension;
   strcpy(myPortName, pDevExt->PortName);

   QCUSB_DbgPrint2
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> DSP: >>>>> IRQL %d IRP 0x%p <<<<<\n", myPortName, irqLevel, Irp)
   );

   irpStack = IoGetCurrentIrpStackLocation (Irp);
   nextStack = IoGetNextIrpStackLocation (Irp);
   pUsbDevObject = pDevExt->StackDeviceObject;

   // get the parameters from an IOCTL call
   ioBuffer           = Irp->AssociatedIrp.SystemBuffer;
   inputBufferLength  = irpStack->Parameters.DeviceIoControl.InputBufferLength;
   outputBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
   ioControlCode      = irpStack->Parameters.DeviceIoControl.IoControlCode;

   if (irpStack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
   {
      if ((gVendorConfig.DriverResident == 0) && (pDevExt->bInService == FALSE))
      {
         if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> DSP: dev disconnected-1\n", myPortName)
            );
            Irp->IoStatus.Status = STATUS_DELETE_PENDING;
            Irp->IoStatus.Information = 0;
            goto USBDSP_Dispatch_Done0;
         }
      }
   }
   else if (irpStack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL)
   {
      if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> DSP: dev disconnected-2\n", myPortName)
         );
         Irp->IoStatus.Status = STATUS_DELETE_PENDING;
         Irp->IoStatus.Information = 0;
         goto USBDSP_Dispatch_Done0;
      }
   }

   switch (irpStack->MajorFunction)
   {
      case IRP_MJ_CREATE:
      {
         Irp->IoStatus.Status = STATUS_SUCCESS;
         Irp->IoStatus.Information = 0;

         QcAcquireDspPass(&pDevExt->DSPSyncEvent);

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> OP-RML <%ld, %ld, %ld, %ld> RW <%ld, %ld> - 0x%x\n",
              pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
              pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
              pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedWrites,
              DeviceObject->Flags)
         );
         Irp->IoStatus.Status = ntStatus = USBIF_Open(DeviceObject);
         QcReleaseDspPass(&pDevExt->DSPSyncEvent);
 
         break;
      }
      case IRP_MJ_CLOSE:
      {
         QcAcquireDspPass(&pDevExt->DSPSyncEvent);
         _AcquireMutex(&pDevExt->muPnPMutex);

         ntStatus = Irp->IoStatus.Status = STATUS_SUCCESS;
         Irp->IoStatus.Information = 0;

         ntCloseStatus = USBIF_Close(DeviceObject);
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> CL-RML <%ld, %ld, %ld, %ld> RW <%ld, %ld>\n\n",
              pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
              pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
              pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedWrites)
         );

         _ReleaseMutex( &pDevExt->muPnPMutex);
         QcReleaseDspPass(&pDevExt->DSPSyncEvent);
         break;
      }
      case IRP_MJ_CLEANUP:
      {
         // QcAcquireDspPass(&pDevExt->DSPSyncEvent);  // 0802

         Irp->IoStatus.Status = STATUS_SUCCESS;
         Irp->IoStatus.Information = 0;

         if (DeviceObject == CalledDO)
         {
            Irp->IoStatus.Status = USBDSP_CleanUp(DeviceObject, Irp, TRUE);
         }
         else
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_CONTROL,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> DSP: late _CLEANUP, no action\n", myPortName)
            );
         }
         // QcReleaseDspPass(&pDevExt->DSPSyncEvent);  // 0802

         break; // IRP_MJ_CLEANUP
      }
      case IRP_MJ_DEVICE_CONTROL:
      {
         Irp->IoStatus.Status = STATUS_SUCCESS;
         Irp->IoStatus.Information = 0;

         switch (ioControlCode)
         {
            case IOCTL_QCDEV_WAIT_NOTIFY:
            {
               ntStatus = USBIOC_CacheNotificationIrp
                          (
                             DeviceObject,
                             ioBuffer,
                             Irp
                          );
               if (ntStatus == STATUS_PENDING)
               {
                  goto USBDSP_Dispatch_Done; // don't touch the "pending" IRP!
               }
               Irp->IoStatus.Status = ntStatus;
               break;
            }

            case IOCTL_QCDEV_DRIVER_ID:
            {
               Irp->IoStatus.Status = USBIOC_GetDriverGUIDString
                                      (
                                         DeviceObject,
                                         ioBuffer,
                                         Irp
                                      );
               break;
            }

            case IOCTL_QCDEV_GET_SERVICE_KEY:
            {
               Irp->IoStatus.Status = USBIOC_GetServiceKey
                                      (
                                         DeviceObject,
                                         ioBuffer,
                                         Irp
                                      );
               break;
            }
 
            case IOCTL_QCDEV_SEND_CONTROL:
            {
               USHORT interface;

               if (pDevExt->IsEcmModel == TRUE)
               {
                  interface = pDevExt->usCommClassInterface;
               }
               else
               {
                  interface = pDevExt->DataInterface;
               }
               
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_INFO,
                  ("<%s> SendEncap: IRP 0x%p IF %d\n", myPortName,
                    Irp, interface)
               );

               Irp->IoStatus.Status = USBENC_CDC_SendEncapsulatedCommand
                                      (
                                         DeviceObject,
                                         interface,
                                         Irp
                                      );
               break;
            }

            case IOCTL_QCDEV_READ_CONTROL:
            {
               // it's impossible to get here since we already queued
               // this IRP to the EncapsulatedDataQueue
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> QCDEV_READ_CONTROL: logic error!\n", myPortName)
               );
               ntStatus = STATUS_UNSUCCESSFUL;
               Irp->IoStatus.Status = ntStatus;
               break;
            }

            case IOCTL_QCDEV_CREATE_CTL_FILE:
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_INFO,
                  ("<%s> IOCTL_QCDEV_CREATE_CTL_FILE: IRP 0x%p \n", myPortName,
                    Irp)
               );

               Irp->IoStatus.Status = QCUSB_CallUSBDIrp
                                      (
                                         DeviceObject,
                                         Irp
                                      );
               ntStatus = Irp->IoStatus.Status;
               break;
            }

            case IOCTL_QCDEV_CLOSE_CTL_FILE:
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_INFO,
                  ("<%s> IOCTL_QCDEV_CLOSE_CTL_FILE: IRP 0x%p \n", myPortName,
                    Irp)
               );

               Irp->IoStatus.Status = QCUSB_CallUSBDIrp
                                      (
                                         DeviceObject,
                                         Irp
                                      );
               ntStatus = Irp->IoStatus.Status;
               break;
            }

            case IOCTL_QCDEV_REQUEST_DEVICEID:
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_INFO,
                  ("<%s> IOCTL_QCDEV_REQUEST_DEVICEID: IRP 0x%p \n", myPortName,
                    Irp)
               );

               Irp->IoStatus.Status = QCUSB_CallUSBDIrp
                                      (
                                         DeviceObject,
                                         Irp
                                      );
               ntStatus = Irp->IoStatus.Status;
               break;
            }

            case IOCTL_QCDEV_GET_HDR_LEN:
            {
               Irp->IoStatus.Status = USBIOC_GetDataHeaderLength
                                      (
                                         DeviceObject,
                                         ioBuffer,
                                         Irp
                                      );
               break;
            }

            #ifdef QC_IP_MODE
            case IOCTL_QCDEV_LOOPBACK_DATA_PKT:
            {
               Irp->IoStatus.Status = USBRD_LoopbackPacket
                                      (
                                         DeviceObject,
                                         ioBuffer,
                                         outputBufferLength
                                      );
               break;
            }
            #endif // QC_IP_MODE

            default:
               Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
         } // switch ioControlCode

         ASSERT(Irp->IoStatus.Status != STATUS_PENDING);
         break;
      }  // IRP_MJ_DEVICE_CONTROL

      case IRP_MJ_INTERNAL_DEVICE_CONTROL:
      {
        // THESE ARE ADDRESSED TO THE SERIAL PORT IT THINKS WE ARE
         switch(ioControlCode)
         {
            case IOCTL_SERIAL_INTERNAL_DO_WAIT_WAKE:
               Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
               break;

            case IOCTL_SERIAL_INTERNAL_CANCEL_WAIT_WAKE:
               Irp->IoStatus.Status = STATUS_SUCCESS;
               break;

            case IOCTL_SERIAL_INTERNAL_BASIC_SETTINGS:
               Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
               break;

            case IOCTL_SERIAL_INTERNAL_RESTORE_SETTINGS:
               Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
               break;

            default:
               Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
               break;
         } // switch(IoControlCode)

         break;
      }  // INTERNAL_DEVICE_CONTROL

      case IRP_MJ_POWER:
      {
         QCPWR_PowrerManagement(pDevExt, Irp, irpStack);

         goto USBDSP_Dispatch_Done;
      
         break;
      }  // IRP_MJ_POWER

      case IRP_MJ_PNP:
      {
         switch (irpStack->MinorFunction) 
         {
            case IRP_MN_QUERY_CAPABILITIES:  // PASSIVE_LEVEL
            {
               PDEVICE_CAPABILITIES pdc = irpStack->Parameters.DeviceCapabilities.Capabilities;

               QcAcquireDspPass(&pDevExt->DSPSyncEvent);

               if (pdc->Version < 1)
               {
                  // just pass this down the stack
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_CIRP,
                     QCUSB_DBG_LEVEL_DETAIL,
                     ("<%s> CIRP: (C FWD) 0x%p\n", myPortName, Irp)
                  );
                  IoSkipCurrentIrpStackLocation(Irp);
                  ntStatus = IoCallDriver(pUsbDevObject, Irp);
                  QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);

                  QcReleaseDspPass(&pDevExt->DSPSyncEvent);
                  goto USBDSP_Dispatch_Done;
               }
               else
               {
                  KEVENT event;
                  // we forward and wait the IRP
                  KeInitializeEvent(&event, SynchronizationEvent, FALSE);
                  IoCopyCurrentIrpStackLocationToNext(Irp); 
                  IoSetCompletionRoutine
                  (
                     Irp,
                     USBUTL_IrpCompletionSetEvent,
                     &event,
                     TRUE,
                     TRUE,
                     TRUE
                  );
                  // IoSetCancelRoutine(Irp, NULL);  // DV?
                  IoCallDriver(pUsbDevObject, Irp);
                  KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
                  ntStatus = Irp->IoStatus.Status;
                  if (NT_SUCCESS(ntStatus))
                  {
                     irpStack = IoGetCurrentIrpStackLocation(Irp);
                     pdc = irpStack->Parameters.DeviceCapabilities.Capabilities;
                     pdc->SurpriseRemovalOK = TRUE;
                     pdc->Removable         = TRUE;
                     RtlCopyMemory
                     ( 
                        &(pDevExt->DeviceCapabilities), 
                        irpStack->Parameters.DeviceCapabilities.Capabilities, 
                        sizeof(DEVICE_CAPABILITIES)
                     );
                  }
               }
               QcReleaseDspPass(&pDevExt->DSPSyncEvent);
               goto USBDSP_Dispatch_Done0;
            }  // IRP_MN_QUERY_CAPABILITIES

            case IRP_MN_START_DEVICE:  // PASSIVE_LEVEL
               QcAcquireDspPass(&pDevExt->DSPSyncEvent);

               _AcquireMutex(&pDevExt->muPnPMutex);
               IoCopyCurrentIrpStackLocationToNext(Irp);

               // after the copy set the completion routine
               
               KeInitializeEvent(&ePNPEvent, SynchronizationEvent, FALSE);

               IoSetCompletionRoutine
               (
                  Irp,
                  USBUTL_IrpCompletionSetEvent,
                  &ePNPEvent,
                  TRUE,
                  TRUE,
                  TRUE
               );

               ntStatus = IoCallDriver
                          (
                             pDevExt->StackDeviceObject,
                             Irp
                          );
               if (!NT_SUCCESS(ntStatus))
               {
                  QCUSB_DbgPrint
                  (
                     QCUSB_DBG_MASK_CONTROL,
                     QCUSB_DBG_LEVEL_DETAIL,
                     ("<%s> IRP_MN_START_DEVICE lowerERR= 0x%x\n", pDevExt->PortName, ntStatus)
                  );
                  QcReleaseDspPass(&pDevExt->DSPSyncEvent);
                  goto USBDSP_Dispatch_Done0;
               }

               ntStatus = KeWaitForSingleObject
                          (
                             &ePNPEvent,
                             Executive,
                             KernelMode,
                             FALSE,
                             NULL
                          );

               QCUSB_DbgPrint
               (
                   QCUSB_DBG_MASK_CONTROL,
                   QCUSB_DBG_LEVEL_DETAIL,
                   ("<%s> IRP_MN_START_DEVICE calling USBPNP_StartDevice!\n", pDevExt->PortName)
               );
               ntStatus = USBPNP_StartDevice(DeviceObject);
 
               ASSERT(NT_SUCCESS(ntStatus));
               _ReleaseMutex( &pDevExt->muPnPMutex);

               Irp->IoStatus.Status = ntStatus;
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> PNP IRP_MN_START_DEVICE = 0x%x\n", pDevExt->PortName, ntStatus)
               );

               QcReleaseDspPass(&pDevExt->DSPSyncEvent);

               if (NT_SUCCESS( ntStatus ))
               {
                  pDevExt->bDeviceRemoved = FALSE;
                  pDevExt->bDeviceSurpriseRemoved = FALSE;
                  setDevState(DEVICE_STATE_PRESENT_AND_STARTED);

                  if (pDevExt->bRemoteWakeupEnabled)
                  {
                      QCPWR_RegisterWaitWakeIrp(pDevExt, 3);
                  }
                  QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 1); // start device
                  #ifdef QCUSB_PWR
                  // USBPWR_StartExWdmDeviceMonitor(pDevExt);
                  #endif
               }
               // our dispatch routine completes the IRP
               goto USBDSP_Dispatch_Done0;
               break;
                            
            case IRP_MN_QUERY_STOP_DEVICE:  // PASSIVE_LEVEL
               QcAcquireDspPass(&pDevExt->DSPSyncEvent);

               USBINT_StopInterruptService(pDevExt, TRUE, FALSE, 0);
               USBMAIN_CancalIoThreads(DeviceObject);
               ntStatus = USBPNP_StopDevice( DeviceObject );
               if (NT_SUCCESS(ntStatus))
               {
                  // USBIF_Close(DeviceObject);
                  clearDevState(DEVICE_STATE_DEVICE_STARTED);
                  setDevState(DEVICE_STATE_DEVICE_STOPPED);
               }
               Irp->IoStatus.Status = ntStatus;
               Irp->IoStatus.Information = 0;

               QcReleaseDspPass(&pDevExt->DSPSyncEvent);
               break;
               
            case IRP_MN_STOP_DEVICE:  // PASSIVE_LEVEL
               QcAcquireDspPass(&pDevExt->DSPSyncEvent);

               if (inDevState(DEVICE_STATE_DEVICE_STOPPED))
               {
                  Irp->IoStatus.Status = STATUS_SUCCESS;
                  Irp->IoStatus.Information = 0;
                  QcReleaseDspPass(&pDevExt->DSPSyncEvent);
                  break;
               }

               USBINT_StopInterruptService(pDevExt, TRUE, FALSE, 1);
               USBMAIN_CancalIoThreads(DeviceObject);
               ntStatus = USBPNP_StopDevice( DeviceObject );
               if (NT_SUCCESS(ntStatus))
               {
                  // USBIF_Close(DeviceObject);
                  clearDevState(DEVICE_STATE_DEVICE_STARTED);
                  setDevState(DEVICE_STATE_DEVICE_STOPPED);
               }
               Irp->IoStatus.Status = ntStatus;
               Irp->IoStatus.Information = 0;

               QcReleaseDspPass(&pDevExt->DSPSyncEvent);
               break;
                 
            case IRP_MN_QUERY_REMOVE_DEVICE:  // PASSIVE_LEVEL
               QcAcquireDspPass(&pDevExt->DSPSyncEvent);
               // USBIF_Close(DeviceObject);

               QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
               clearDevState(DEVICE_STATE_PRESENT_AND_STARTED);
               setDevState(DEVICE_STATE_SURPRISE_REMOVED);
               pDevExt->bDeviceSurpriseRemoved = TRUE;
               ntStatus = STATUS_SUCCESS;

               QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
               QcReleaseDspPass(&pDevExt->DSPSyncEvent);

               USBUTL_DriverRegistrySetDword(pDevExt, VEN_DEV_TIME, 0);
               USBUTL_DriverRegistrySetDword(pDevExt, VEN_DEV_PROTOC, 0);

               #ifdef NDIS_WDM
               Irp->IoStatus.Status = ntStatus;
               Irp->IoStatus.Information = 0;
               goto USBDSP_Dispatch_Done0;  // not to send the IRP down
               #endif // NDIS_WDM

               break;

            case IRP_MN_SURPRISE_REMOVAL:  // PASSIVE_LEVEL
            {
               QcAcquireDspPass(&pDevExt->DSPSyncEvent);
               QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
               clearDevState(DEVICE_STATE_PRESENT_AND_STARTED);
               setDevState(DEVICE_STATE_SURPRISE_REMOVED);
               QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

               USBUTL_DriverRegistrySetDword(pDevExt, VEN_DEV_TIME, 0);
               USBUTL_DriverRegistrySetDword(pDevExt, VEN_DEV_PROTOC, 0);
               USBENC_PurgeQueue(pDevExt, &pDevExt->EncapsulatedDataQueue, FALSE, 5);
               USBPWR_StopExWdmDeviceMonitor(pDevExt);
               // USBIF_Close(DeviceObject);
               USBINT_StopInterruptService(pDevExt, TRUE, TRUE, 2);

               _AcquireMutex( &pDevExt->muPnPMutex);
               pDevExt->bDeviceSurpriseRemoved = TRUE;
               USBMAIN_CancalIoThreads(DeviceObject);
               USBMAIN_ResetRspAvailableCount(pDevExt, pDevExt->DataInterface, pDevExt->PortName, 1);
               _ReleaseMutex( &pDevExt->muPnPMutex);

               Irp->IoStatus.Status = STATUS_SUCCESS;

               QcReleaseDspPass(&pDevExt->DSPSyncEvent);

               #ifdef NDIS_WDM
               goto USBDSP_Dispatch_Done0;  // not to send the IRP down
               #endif // NDIS_WDM

               // just send on down to device
               break;
            }
            case IRP_MN_REMOVE_DEVICE:  // PASSIVE_LEVEL
               QcAcquireDspPass(&pDevExt->DSPSyncEvent);

               _AcquireMutex( &pDevExt->muPnPMutex);
               USBPWR_StopExWdmDeviceMonitor(pDevExt);
               USBDSP_CleanUp(DeviceObject, Irp, FALSE);  // once again
               // USBIF_Close(DeviceObject);
               clearDevState(DEVICE_STATE_PRESENT_AND_STARTED);
               setDevState(DEVICE_STATE_DEVICE_REMOVED0);
               pDevExt->bDeviceRemoved = TRUE;
               pDevExt->bDeviceSurpriseRemoved = TRUE;

               QcAcquireEntryPass(&gSyncEntryEvent, myPortName);

               #ifndef NDIS_WDM
               USBPNP_HandleRemoveDevice(DeviceObject, CalledDO, Irp);
               #endif // NDIS_WDM

               bRemoveRequest = TRUE;


               USBMAIN_ResetRspAvailableCount(pDevExt, pDevExt->DataInterface, pDevExt->PortName, 2);
               USBINT_CancelInterruptThread(pDevExt, 9);
               USBMAIN_ResetRspAvailableCount(pDevExt, pDevExt->DataInterface, pDevExt->PortName, 3);
               // QCUSB_CleanupDeviceExtensionBuffers(DeviceObject);

               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_CRITICAL,
                  ("<%s> Bf IoReleaseRemoveLockAndWait (%ld,%ld,%ld,%ld) RW <%ld, %ld>\n",
                    pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
                    pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
                    pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedWrites)
               );
               // #ifndef NDIS_WDM
               IoReleaseRemoveLockAndWait(pDevExt->pRemoveLock, Irp);
               InterlockedDecrement(&(pDevExt->Sts.lRmlCount[0]));
               // #else
               // QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
               // #endif  // NDIS_WDM
               

               QcReleaseEntryPass(&gSyncEntryEvent, myPortName, "OUT");
               _ReleaseMutex( &pDevExt->muPnPMutex);
               QcReleaseDspPass(&pDevExt->DSPSyncEvent);

               ntStatus = STATUS_SUCCESS;

               Irp->IoStatus.Status = ntStatus;

               QCUSB_DbgPrintG2
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_FORCE,
                  ("<%s> REMOVE_END: ThCnt=%d\n", myPortName, ucThCnt)
               );

               // our dispatch routine completes the IRP
               goto USBDSP_Dispatch_Done0;

            case IRP_MN_QUERY_ID:  // PASSIVE_LEVEL
               QCUSB_DbgPrint2
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> IRP_MN_QUERY_ID: 0x%x\n", myPortName,
                    irpStack->Parameters.QueryId.IdType)
               );
               break; // case IRP_MN_QUERY_ID

            case IRP_MN_QUERY_PNP_DEVICE_STATE:
               Irp->IoStatus.Information = PNP_DEVICE_RESOURCE_REQUIREMENTS_CHANGED; 
               Irp->IoStatus.Status = STATUS_SUCCESS;
               break; // case IRP_MN_QUERY_PNP_DEVICE_STATE

            case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
               break; // case IRP_MN_QUERY_RESOURCE_REQUIREMENTS

            case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
               break; // case IRP_MN_FILTER_RESOURCE_REQUIREMENTS

            case IRP_MN_QUERY_DEVICE_RELATIONS:
               break; // case IRP_MN_QUERY_DEVICE_RELATIONS

            case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
               // MSDN says "This IRP is reserved for system use"
               // and WDM.H doesn't define the IRP type
               // (although ntddk.h does).
               break;

            default:
            {
               QCUSB_DbgPrint2
               (
                  QCUSB_DBG_MASK_CONTROL,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> PNP IRP MN 0x%x not handled\n", myPortName, irpStack->MinorFunction)
               );
            }
            
         } //switch pnp minorfunction

         // All PNP messages get passed to PhysicalDeviceObject.

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> CIRP: (C FWD) 0x%p\n", myPortName, Irp)
         );
         IoSkipCurrentIrpStackLocation(Irp);
         // IoSetCancelRoutine(Irp, NULL);  // DV ?
         ntStatus = IoCallDriver( pUsbDevObject, Irp );

         /*
         * Bypass the IoCompleteRequest since the USBD stack took care of it.
         */
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
         goto USBDSP_Dispatch_Done;
         break;         // pnp power
         
      } // IRP_MJ_PNP

      case IRP_MJ_SYSTEM_CONTROL:
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> CIRP: (C FWD) 0x%p\n", myPortName, Irp)
         );
         IoSkipCurrentIrpStackLocation(Irp);
         ntStatus = IoCallDriver( pUsbDevObject, Irp );
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
         goto USBDSP_Dispatch_Done; // the USBD stack took care of it
         break;         // power           
      } // IRP_MJ_SYSTEM_CONTROL i.e. WMI
      default:
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> CIRP: (C FWD) 0x%p\n", myPortName, Irp)
         );
         IoSkipCurrentIrpStackLocation(Irp);
         ntStatus = IoCallDriver( pUsbDevObject, Irp );

         // Bypass the IoCompleteRequest since the USBD stack took care of it
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
         goto USBDSP_Dispatch_Done;
         break;  // power           
      }
   }// switch majorfunction

USBDSP_Dispatch_Done0:

   ntStatus = Irp->IoStatus.Status;

   if (ntStatus == STATUS_PENDING)
   {
      _IoMarkIrpPending(Irp);
      if (bRemoveRequest == TRUE)
      {
         QCUSB_DbgPrintG
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> Dispatch: (P 0x%p)-0x%x\n", myPortName, Irp, ntStatus)
         );
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> Dispatch: (P 0x%p)-0x%x\n", myPortName, Irp, ntStatus)
         );
      }
   }
   else
   {
      if (bRemoveRequest == TRUE)
      {
         QCUSB_DbgPrintG
         (
            QCUSB_DBG_MASK_CIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> CIRP: (C 0x%x) 0x%p (removal)\n", myPortName, ntStatus, Irp)
         );
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> CIRP: (C 0x%x) 0x%p RmlCount[0]=%u\n", myPortName, ntStatus, Irp,
              pDevExt->Sts.lRmlCount[0])
         );
      }

      ASSERT(ntStatus == STATUS_SUCCESS       ||
             ntStatus == STATUS_NOT_SUPPORTED ||
             ntStatus == STATUS_DEVICE_NOT_CONNECTED ||
             ntStatus == STATUS_CANCELLED ||
             ntStatus == STATUS_DELETE_PENDING
            );

      if ((irpStack->MajorFunction == IRP_MJ_PNP &&
           irpStack->MinorFunction == IRP_MN_REMOVE_DEVICE) ||
           (irpStack->MajorFunction == IRP_MJ_CREATE))
      {
         IoCompleteRequest(Irp, IO_NO_INCREMENT);
      }
      else
      {
         if (irpStack->MajorFunction == IRP_MJ_CLOSE &&
             ntCloseStatus == STATUS_UNSUCCESSFUL)
         {
             IoCompleteRequest(Irp, IO_NO_INCREMENT);
         }
         else
         {
            QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
         }
      }
   }

USBDSP_Dispatch_Done:

   if (bRemoveRequest == TRUE)
   {
      QCUSB_DbgPrintG
      (
         QCUSB_DBG_MASK_CIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> DSP: Exit\n", myPortName)
      );
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> DSP: END-RML <%ld,%ld,%ld,%ld> RW <%d, %d>\n\n", myPortName,
           pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
           pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
           pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedWrites
         )
      );
   }

   *Removed = bRemoveRequest;

   return ntStatus;
}  // USBDSP_Dispatch

NTSTATUS USBDSP_CleanUp
(
   IN PDEVICE_OBJECT DeviceObject,
   PIRP              pIrp,
   BOOLEAN           bIsCleanupIrp
)
{
   PDEVICE_EXTENSION pDevExt;
   KIRQL cancelIrql;
   NTSTATUS ntStatus;
   KIRQL IrqLevel;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pDevExt = DeviceObject->DeviceExtension;

   if (bIsCleanupIrp == TRUE)
   {
      QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);

      if (pDevExt->PurgeIrp != NULL)
      {
         QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
         return STATUS_CANCELLED;
      }
      else
      {
         pDevExt->PurgeIrp = pIrp;
         QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
      }
   }

   ntStatus = QCUSB_AbortPipe
              (
                 DeviceObject,
                 pDevExt->BulkPipeInput,
                 pDevExt->DataInterface
              );
   ntStatus = QCUSB_AbortPipe
              (
                 DeviceObject,
                 pDevExt->BulkPipeOutput,
                 pDevExt->DataInterface
              );

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_INFO,
      ("<%s> _CleanUp: - Purge L1\n", pDevExt->PortName)
   );
   // ntStatus = USBRD_CancelReadThread(pDevExt, 1);
   KeSetEvent(&pDevExt->L1ReadPurgeEvent,IO_NO_INCREMENT,FALSE);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_INFO,
      ("<%s> _CleanUp: - Purge W\n", pDevExt->PortName)
   );

   // ntStatus = USBWT_CancelWriteThread(pDevExt, 0);
   KeSetEvent(&pDevExt->WritePurgeEvent,IO_NO_INCREMENT,FALSE);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_INFO,
      ("<%s> _CleanUp: - CancelSglIrp\n", pDevExt->PortName)
   );
   USBMAIN_CancalNotificationIrp(DeviceObject);

   // USBUTL_CleanupReadWriteQueues(pDevExt);

   pIrp->IoStatus.Information = 0;

   return STATUS_PENDING;
}  // USBDSP_CleanUp

BOOLEAN QCDSP_ToProcessIrp
(
   PDEVICE_EXTENSION pDevExt,
   PIRP              Irp
)
{
   PIO_STACK_LOCATION irpStack;

   irpStack = IoGetCurrentIrpStackLocation(Irp);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_CONTROL,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> _ToProcessIrp: 0x%p[0x%x, 0x%x] RmlCount[0]=%d\n", pDevExt->PortName,
        Irp, irpStack->MajorFunction, irpStack->MinorFunction, pDevExt->Sts.lRmlCount[0])
   );

   if ((irpStack->MajorFunction == IRP_MJ_PNP) &&
       (irpStack->MinorFunction == IRP_MN_REMOVE_DEVICE))
   {
      if (pDevExt->Sts.lRmlCount[0] > 1)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_CONTROL,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> _ToProcessIrp: outstanding IRPs %d\n", pDevExt->PortName,
              pDevExt->Sts.lRmlCount[0])
         );

         if (pDevExt->PowerSuspended == FALSE)
         {
            // Device is in D0
            QCPWR_CancelIdleTimer(pDevExt, QCUSB_BUSY_CTRL, TRUE, 6);
         }
         else
         {
            KeSetEvent
            (
               &(pDevExt->DispatchPreWakeupEvent),
               IO_NO_INCREMENT,
               FALSE
            );
         }
         return FALSE;
      }
   }

   return TRUE;
}  // QCDSP_ToProcessIrp
