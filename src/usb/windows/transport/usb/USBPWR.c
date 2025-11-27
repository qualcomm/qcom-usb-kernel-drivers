/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q C P W R . C

GENERAL DESCRIPTION
  This file implementations of power management functions.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#include <stdarg.h>
#include <stdio.h>
#include <initguid.h>
#include "USBPWR.h"
#include "USBINT.h"
#include "USBRD.h"
#include "USBWT.h"
#include "USBUTL.h"
#include "USBIOC.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "USBPWR.tmh"

#endif   // EVENT_TRACING

/*******
WMIGUIDREGINFO QCSerialPortPowerWmiGuidList[] =
{
    // "Allow the computer to turn off this device to save power."
    // If this feature is on, the selective suspension is supported:
    //    1. If QCDriverSelectiveSuspendIdleTime does not exist or
    //       its value is less than 3s, we use 5s as the default value.
    //    2. If QCDriverSelectiveSuspendIdleTime has a valid value,
    //       use it as the idle timeout time.
    // If this feature is off, the selective suspension is not supported.
    {
        &GUID_POWER_DEVICE_ENABLE,
        1,
        0
    },

    // "Allow this device to bring the computer out of standby."
    // In practice, it's meaningless to support this with USB because the
    // power state of the USB hub would be too low to wake up the system.
    {
        &GUID_POWER_DEVICE_WAKE_ENABLE,
        1,
        0
    
};
******/

VOID QCPWR_PowrerManagement
(
   PDEVICE_EXTENSION  pDevExt,
   PIRP               Irp,
   PIO_STACK_LOCATION IrpStack
)
{
   NTSTATUS nts = STATUS_SUCCESS;
   BOOLEAN  bUnsupported = FALSE;
   
   // This function returns SUCCESS or PENDING and use completion
   // routine to start the next power IRP and complete the current IRP.
   // On failure, the function returns failure status and the PnP
   // dispatch function completes the IRP.

   switch (IrpStack->MinorFunction)
   {
      case IRP_MN_QUERY_POWER:
      {
         switch (IrpStack->Parameters.Power.Type)
         {
            case SystemPowerState:
            {
               SYSTEM_POWER_STATE pwrState = IrpStack->Parameters.Power.State.SystemState;

               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_POWER,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> IRP_MN_QUERY_POWER: SystemPowerState %d\n", pDevExt->PortName, pwrState)
               );
               nts = QCPWR_QuerySystemPowerState(pDevExt, pwrState);

               if (NT_SUCCESS(nts))
               {
                  // completion routine starts the next IRP
                  IoCopyCurrentIrpStackLocationToNext(Irp);
                  IoSetCompletionRoutine
                  (
                     Irp,
                     (PIO_COMPLETION_ROUTINE)QCPWR_SystemPowerRequestCompletion,
                     pDevExt, TRUE, TRUE, TRUE
                  );
               }
               else
               {
                  PoStartNextPowerIrp(Irp);
               }
               break;
            }
            case DevicePowerState:
            {
               DEVICE_POWER_STATE pwrState = IrpStack->Parameters.Power.State.DeviceState;

               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_POWER,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> IRP_MN_QUERY_POWER: DevicePowerState D%d\n", pDevExt->PortName, pwrState-1)
               );

               nts = QCPWR_QueryDevicePowerState(pDevExt, pwrState);

               if (NT_SUCCESS(nts))
               {
                  // Completetion routine starts the next power IRP
                  IoCopyCurrentIrpStackLocationToNext(Irp);
                  IoSetCompletionRoutine
                  (
                     Irp,
                     (PIO_COMPLETION_ROUTINE)QCPWR_DevicePowerDownIrpCompletion,
                     pDevExt, TRUE, TRUE, TRUE
                  );
               }
               else
               {
                  PoStartNextPowerIrp(Irp);
               }

               break;
            }

            default:
            {
               PoStartNextPowerIrp(Irp);

               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_POWER,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> IRP_MN_QUERY_POWER: unknown pwr type %u\n",
                    pDevExt->PortName, IrpStack->Parameters.Power.Type)
               );

               IoSkipCurrentIrpStackLocation(Irp);
               bUnsupported = TRUE;

               break;
            }
         }

         break;
      }  // IRP_MN_QUERY_POWER

      case IRP_MN_SET_POWER:
      {

         switch (IrpStack->Parameters.Power.Type)
         {
            case SystemPowerState:
            {
               SYSTEM_POWER_STATE pwrState = IrpStack->Parameters.Power.State.SystemState;

               // We must agree with system power request

               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_POWER,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> IRP_MN_SET_POWER: SystemPowerState %d\n", pDevExt->PortName, pwrState)
               );
               QCPWR_SetSystemPowerState(pDevExt, pwrState);  // do nothing for now

               // The completion routine requests device power IRP,
               // it also starts the next IRP, this IRP cannot fail
               IoCopyCurrentIrpStackLocationToNext(Irp);
               IoSetCompletionRoutine
               (
                  Irp,
                  (PIO_COMPLETION_ROUTINE)QCPWR_SystemPowerRequestCompletion,
                  pDevExt, TRUE, TRUE, TRUE
               );

               break;
            }

            case DevicePowerState:
            {
               DEVICE_POWER_STATE pwrState = IrpStack->Parameters.Power.State.DeviceState;

               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_POWER,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> IRP_MN_SET_POWER: D%d\n", pDevExt->PortName, pwrState-1)
               );

               QCPWR_SetDevicePowerState(pDevExt, Irp, pwrState);

               break;
            }

            default:
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_POWER,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> IRP_MN_SET_POWER: unknown pwr type %u\n",
                    pDevExt->PortName, IrpStack->Parameters.Power.Type)
               );

               // We forward IRP only in this case, which is unlikely
               PoStartNextPowerIrp(Irp);
               IoSkipCurrentIrpStackLocation(Irp);
               bUnsupported = TRUE;

               break;
            }

         }

         break;
      }  // IRP_MN_SET_POWER

      case IRP_MN_WAIT_WAKE:
      {
         QCPWR_ProcessWaitWake(pDevExt, Irp);
         break;
      }

      default:
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> PowerManagement: Unkown MN 0x%x\n", pDevExt->PortName, IrpStack->MinorFunction)
         );
         PoStartNextPowerIrp(Irp);
         IoSkipCurrentIrpStackLocation(Irp);
         bUnsupported = TRUE;
         break;
      }

   }  // switch

   if (!NT_SUCCESS(nts))  // failure can only happen with QUERY POWER
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> PIRP: (Ce 0x%x) 0x%p\n", pDevExt->PortName, nts, Irp)
      );
      USBUTL_CompleteRequest(Irp, nts, 0);
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> PIRP: (C FWD) 0x%p\n", pDevExt->PortName, Irp)
      );
      nts = PoCallDriver(pDevExt->StackDeviceObject,Irp);
      if (!NT_SUCCESS(nts))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> PoCallDriver failure: IRP 0x%p(0x%x)\n", pDevExt->PortName, Irp, nts)
         );
      }

      if (bUnsupported == TRUE)
      {
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
      }
   }

} // QCPWR_PowrerManagement

BOOLEAN QCPWR_Prelude
(
   PIRP               Irp,
   PDEVICE_EXTENSION  pDevExt,
   PNTSTATUS          pNtStatus
)
{
   BOOLEAN ret = FALSE;

   if ((pDevExt->bDeviceSurpriseRemoved == TRUE) ||
       (pDevExt->bDeviceRemoved == TRUE))
   {
      PoStartNextPowerIrp(Irp);
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> PIRP: (Cdp) 0x%p\n", pDevExt->PortName, Irp)
      );
      USBUTL_CompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
      *pNtStatus = STATUS_DELETE_PENDING;
      ret = TRUE;
   }
   else if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      PoStartNextPowerIrp(Irp);
      IoSkipCurrentIrpStackLocation(Irp);
      *pNtStatus = PoCallDriver(pDevExt->StackDeviceObject,Irp);

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> PIRP (FWDi 0x%x) 0x%p\n", pDevExt->PortName, *pNtStatus, Irp)
      );
      ret = TRUE;
   }

   return ret;

}  // QCPWR_Prelude

// ========================================================
//          System Query Power
// ========================================================
NTSTATUS QCPWR_QuerySystemPowerState
(
   PDEVICE_EXTENSION  pDevExt,
   SYSTEM_POWER_STATE PowerState
)
{
   NTSTATUS nts = STATUS_SUCCESS;

   if ((pDevExt->PowerManagementEnabled == FALSE) &&
       (pDevExt->bInService == TRUE))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> QuerySystemPowerState: dev in service, reject.\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }
   /* Block runtime D1/D2/D3 while in S0 and busy/in-service */
   if ((PowerState > PowerDeviceD0) &&
       (pDevExt->SystemPower <= PowerSystemWorking) &&
       ((pDevExt->bInService == TRUE) ||
           (pDevExt->IoBusyMask != 0) ||
           (pDevExt->InServiceSelectiveSuspension == FALSE)))
   {
       QCUSB_DbgPrint(
           QCUSB_DBG_MASK_PIRP, QCUSB_DBG_LEVEL_ERROR,
           ("<%s> QueryDevicePowerState: block D%d (busy/in-service S0)\n", pDevExt->PortName, PowerState - 1)
       );
       return STATUS_INVALID_DEVICE_STATE;
   }
   if (pDevExt->bRemoteWakeupEnabled == TRUE)
   {
      if (PowerState > pDevExt->DeviceCapabilities.SystemWake)
      {
         // New power state is beyond wake-able power, cancel the Wait-Wake IRP
         QCPWR_CancelWaitWakeIrp(pDevExt, 0);
      }
      else
      {
         // Issue Wait-Wake IRP
         QCPWR_RegisterWaitWakeIrp(pDevExt, 5);
      }
   }

   switch (PowerState)
   {
      case PowerSystemWorking:
      case PowerSystemSleeping1:
      case PowerSystemSleeping2:
      case PowerSystemSleeping3:
      case PowerSystemHibernate:
      case PowerSystemShutdown:
      {
         if ((pDevExt->bRemoteWakeupEnabled == TRUE) &&
             (PowerState > pDevExt->SystemPower))
         {
            // QCPWR_RegisterWaitWakeIrp(pDevExt, 0);
         }

         break;
      }

      default:
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> QuerySystemPowerState: invalid state req1 - %u\n", pDevExt->PortName, PowerState)
         );
         nts = STATUS_INVALID_DEVICE_STATE;
      }
   }

   return nts;

}  // QCPWR_QuerySystemPowerState

NTSTATUS QCPWR_SystemPowerRequestCompletion
(
  PDEVICE_OBJECT    DeviceObject,
  PIRP              Irp,
  PDEVICE_EXTENSION pDevExt
)
{
   NTSTATUS           ntStatus = Irp->IoStatus.Status;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> SystemPowerRequestCompletion: IRP 0x%p(0x%x)\n", pDevExt->PortName, Irp, ntStatus)
   );

   if (!NT_SUCCESS(ntStatus))
   {
      // We are done with this IRP
      PoStartNextPowerIrp(Irp);

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> PIRP (Ce 0x%x) 0x%p\n", pDevExt->PortName, ntStatus, Irp)
      );

      // No further action, let the system continue to complete the IRP
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
      return STATUS_SUCCESS;
   }

   if (irpStack->MinorFunction == IRP_MN_SET_POWER)
   {
      SYSTEM_POWER_STATE pwrState = irpStack->Parameters.Power.State.SystemState;

      if (pwrState <= PowerSystemWorking)
      {
         pDevExt->PrepareToPowerDown = FALSE;

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> SysPwrReqCompletion: 0x%p [PrepareToPowerDown 0]\n",
              pDevExt->PortName, Irp)
         );
      }
   }

   // queue device irp and return STATUS_MORE_PROCESSING_REQUIRED

   QCPWR_RequestDevicePowerIrp(pDevExt, Irp);

   return STATUS_MORE_PROCESSING_REQUIRED;

}  // QCPWR_SystemPowerRequestCompletion

VOID QCPWR_RequestDevicePowerIrp
(
  PDEVICE_EXTENSION pDevExt,
  PIRP              Irp
)
{
   NTSTATUS                  ntStatus;
   POWER_STATE               powState;
   PIO_STACK_LOCATION        irpStack = IoGetCurrentIrpStackLocation(Irp);
   SYSTEM_POWER_STATE        systemState;
   DEVICE_POWER_STATE        devState;
   PPOWER_COMPLETION_CONTEXT powerContext;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> ReqDevicePowerIrp: SysPwrIRP 0x%p\n", pDevExt->PortName, Irp)
   );
   systemState = irpStack->Parameters.Power.State.SystemState;
   devState = pDevExt->DeviceCapabilities.DeviceState[systemState];

   // Check the power state consistency! -- bug with W2K
   if ((systemState == PowerSystemWorking) && (devState != PowerDeviceD0))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> ReqDevicePowerIrp: unexpected pwr map (sys[%u]<->dev[%u])\n",
           pDevExt->PortName, systemState, devState)
      );
      devState = PowerDeviceD0;
   }

   powState.DeviceState = devState;

   InterlockedIncrement(&(pDevExt->PwrCtextIdx));
   if (pDevExt->PwrCtextIdx >= QCUSB_SYS_PWR_IRP_MAX)
   {
      pDevExt->PwrCtextIdx = 0;
   }
   powerContext = &(pDevExt->PwrCompContext[pDevExt->PwrCtextIdx]);
   powerContext->DeviceExtension = pDevExt;
   powerContext->Irp = Irp;

   // The following code block is the optimization required by
   // Windows Vista (codename Longhorn). The QCUSB_LONGHORN
   // must be defined in the SOURCES file to build the image
   // for Windows Vista. Though it also works well on Windows XP
   // as far as the test goes, it may be a better idea to make
   // the code Vista-specific until enough tests are done for
   // older Windows versions.
   if (pDevExt->WdmVersion >= WinVistaOrHigher)
   {
      if ((irpStack->MinorFunction == IRP_MN_SET_POWER) &&
          (systemState == PowerSystemWorking))
      {
         powerContext->Irp = NULL;
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> LONGHORN fast resume: to complete SysIrp 0x%p\n",
              pDevExt->PortName, Irp)
         );
      }
   }

   ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, powerContext);
   if (NT_SUCCESS(ntStatus))
   {
      // update statistics
      QcInterlockedIncrement(0, powerContext, 2);
   }
   else
   {
      PoStartNextPowerIrp(Irp);

      // The driver must not fail the system power IRP
      Irp->IoStatus.Status = STATUS_SUCCESS;
      Irp->IoStatus.Information = 0;

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> PIRP (Csys 0) 0x%p dev req2 0x%x\n", pDevExt->PortName, Irp, ntStatus)
      );
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
      return;
   }

   ntStatus = PoRequestPowerIrp
              (
                 pDevExt->PhysicalDeviceObject,
                 irpStack->MinorFunction,
                 powState,
                 (PREQUEST_POWER_COMPLETE)QCPWR_TopDevicePowerIrpCompletionRoutine,
                 powerContext,
                 NULL
              );

   // if failure or system IRP is a set-IRP in Longhorn, complete it right away
   if ((!NT_SUCCESS(ntStatus)) || (powerContext->Irp == NULL))
   {
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, powerContext, 0);
      PoStartNextPowerIrp(Irp);

      // The driver must not fail the system power IRP
      Irp->IoStatus.Status = STATUS_SUCCESS;
      Irp->IoStatus.Information = 0;

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> PIRP (Csys 0) 0x%p dev req 0x%x\n", pDevExt->PortName, Irp, ntStatus)
      );
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
   }

   return;

}  // QCPWR_RequestDevicePowerIrp

// top routine
VOID QCPWR_TopDevicePowerIrpCompletionRoutine
(
   PDEVICE_OBJECT   DeviceObject,
   UCHAR            MinorFunction,
   POWER_STATE      PowerState,
   PVOID            Context,
   PIO_STATUS_BLOCK IoStatus
)
{
   PIRP                      systemPwrIrp;
   PDEVICE_EXTENSION         pDevExt;
   PPOWER_COMPLETION_CONTEXT powerContext = (PPOWER_COMPLETION_CONTEXT)Context;

   systemPwrIrp = powerContext->Irp;
   pDevExt      = powerContext->DeviceExtension;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> TopDevicePowerIrpCompletion: sIRP 0x%p DevObj 0x%p\n", pDevExt->PortName, systemPwrIrp, DeviceObject)
   );

   if (systemPwrIrp == NULL)
   {
      return;
   }

   // copy the device Irp status into  system Irp
   systemPwrIrp->IoStatus.Status = IoStatus->Status;

   // complete the system Irp
   PoStartNextPowerIrp(systemPwrIrp);
   systemPwrIrp->IoStatus.Information = 0;

   if (NT_SUCCESS(IoStatus->Status))
   {
      PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(systemPwrIrp);
      if (irpStack->MinorFunction == IRP_MN_QUERY_POWER)
      {
         SYSTEM_POWER_STATE pwrState = irpStack->Parameters.Power.State.SystemState;

         if (pwrState >= PowerSystemSleeping3)
         {
            pDevExt->PrepareToPowerDown = TRUE;
         }
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> PIRP (Csys 0x%x) 0x%p [PrepareToPowerDown %u]\n",
        pDevExt->PortName, IoStatus->Status, systemPwrIrp,
        pDevExt->PrepareToPowerDown)
   );
   IoCompleteRequest(systemPwrIrp, IO_NO_INCREMENT);
   QcIoReleaseRemoveLock(pDevExt->pRemoveLock, systemPwrIrp, 0);

   // Release remove lock one more time
   QcIoReleaseRemoveLock(pDevExt->pRemoveLock, powerContext, 0);

   return;
}  // QCPWR_TopDevicePowerIrpCompletionRoutine

// ========================================================
//          Device Query Power
// ========================================================
NTSTATUS QCPWR_QueryDevicePowerState
(
   PDEVICE_EXTENSION  pDevExt,
   DEVICE_POWER_STATE PowerState
)
{
   NTSTATUS nts = STATUS_SUCCESS;

   if ((pDevExt->PowerManagementEnabled == FALSE) &&
       (pDevExt->bInService == TRUE))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> QueryDevicePowerState: dev in service, reject.\n", pDevExt->PortName)
      );
      return STATUS_INVALID_DEVICE_STATE;
   }

   if (pDevExt->bRemoteWakeupEnabled == TRUE)
   {
      if (PowerState > pDevExt->DeviceCapabilities.DeviceWake)
      {
         // New power state is too low to issue RESUME, cancel the Wait-Wake IRP
         QCPWR_CancelWaitWakeIrp(pDevExt, 1);
      }

      // Do not issue wait-wake without knowledge of current system power state
   }

   switch (PowerState)
   {
      case PowerDeviceD0:
      case PowerDeviceD1:
      case PowerDeviceD2:
      case PowerDeviceD3:
      {
         if (PowerState > pDevExt->DevicePower)
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_PIRP,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> QueryDevicePowerState: D%u, no action\n", pDevExt->PortName, PowerState-1)
            );
         }
         break;
      }

      default:
      {
         nts = STATUS_NOT_SUPPORTED;
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> QueryDevicePowerState: invalid state req1 - D%u\n", pDevExt->PortName, PowerState-1)
         );
      }
   }

   return nts;

}  // QCPWR_QueryDevicePowerState

NTSTATUS QCPWR_DevicePowerDownIrpCompletion
(
  PDEVICE_OBJECT    DeviceObject,
  PIRP              Irp,
  PDEVICE_EXTENSION pDevExt
)
{
   NTSTATUS           ntStatus = Irp->IoStatus.Status;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> DevicePowerDownIrpCompletion: IRP 0x%p\n", pDevExt->PortName, Irp)
   );

   if (NT_SUCCESS(ntStatus) && (irpStack->MinorFunction == IRP_MN_SET_POWER))
   {
       pDevExt->DevicePower = irpStack->Parameters.Power.State.DeviceState;
/***
       PoSetPowerState
       (
          pDevExt->MiniportFDO, // DeviceObject,
          DevicePowerState,
          irpStack->Parameters.Power.State
       );
***/
   }

   PoStartNextPowerIrp(Irp);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> PIRP (Cdev 0x%x) 0x%p\n", pDevExt->PortName, Irp->IoStatus.Status, Irp)
   );

   // Let the system continue to complete the IRP by
   // calling QCPWR_TopDevicePowerIrpCompletionRoutine which in turn
   // completes the system power IRP.
   QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);

   return STATUS_SUCCESS;

}  // QCPWR_DevicePowerDownIrpCompletion

// ========================================================
//          Set System Power
// ========================================================
NTSTATUS QCPWR_SetSystemPowerState
(
   PDEVICE_EXTENSION  pDevExt,
   SYSTEM_POWER_STATE PowerState
)
{
   NTSTATUS nts = STATUS_SUCCESS;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->SetSystemPowerState: %u\n", pDevExt->PortName, PowerState)
   );

   pDevExt->SystemPower = PowerState;

   if (pDevExt->bRemoteWakeupEnabled == TRUE)
   {
      if (PowerState > pDevExt->DeviceCapabilities.SystemWake)
      {
         // New power state is beyond wake-able power, cancel the Wait-Wake IRP
         // QCPWR_CancelWaitWakeIrp(pDevExt, 2);
         QCPWR_CancelPendingIrps(pDevExt, PowerState);
      }
      else
      {
         // Issue Wait-Wake IRP
         QCPWR_RegisterWaitWakeIrp(pDevExt, 4);
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--SetSystemPowerState: %u\n", pDevExt->PortName, PowerState)
   );

   return nts;
}  // QCPWR_SetSystemPowerState

VOID QCPWR_CancelPendingIrps
(
   PDEVICE_EXTENSION  pDevExt,
   SYSTEM_POWER_STATE PowerState
)
{
   NTSTATUS nts = STATUS_SUCCESS;
   NTSTATUS nts2;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE     levelOrHandle;
   #else
   KIRQL                  levelOrHandle;
   #endif
   LARGE_INTEGER delayValue;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->CancelPendingIrps: %u\n", pDevExt->PortName, PowerState)
   );

   QCPWR_CancelWaitWakeIrp(pDevExt, 2);

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);

   if (KeReadStateEvent(&pDevExt->InterruptRegIdleEvent) != 0)
   {
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

      // event signaled, idle IRP issuance is at work
      // wait for the ack event and then cancel the idle IRP

      delayValue.QuadPart = -(300 * 1000 * 1000); // 20 seconds
      nts2 = KeWaitForSingleObject
             (
                &pDevExt->RegIdleAckEvent,
                Executive, KernelMode, FALSE, &delayValue
             );
      KeClearEvent(&pDevExt->RegIdleAckEvent);

      if (nts2 == STATUS_TIMEOUT)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> WTO-a\n", pDevExt->PortName)
         );
      }
      else
      {
         // cancel the idle IRP
         QCPWR_CancelIdleTimer(pDevExt, 0, TRUE, 3);
      }
   }
   else
   {
      // Cancel the idle IRP
      KeClearEvent(&pDevExt->RegIdleAckEvent);
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      QCPWR_CancelIdleTimer(pDevExt, 0, TRUE, 4);
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--CancelPendingIrps: %u\n", pDevExt->PortName, PowerState)
   );
}  // QCPWR_CancelPendingIrps

// ========================================================
//          Set Device Power
// ========================================================
NTSTATUS QCPWR_SetDevicePowerState
(
   PDEVICE_EXTENSION  pDevExt,
   PIRP               Irp,
   DEVICE_POWER_STATE PowerState
)
{
   NTSTATUS nts = STATUS_SUCCESS;
   // Block runtime D1/D2/D3 while in S0 and busy/in-service
   if ((pDevExt->SystemPower <= PowerSystemWorking) && (PowerState > PowerDeviceD0))
   {
       if ((pDevExt->bInService == TRUE) ||
           (pDevExt->IoBusyMask != 0) ||
           (pDevExt->InServiceSelectiveSuspension == FALSE))
       {
           QCUSB_DbgPrint
           (
               QCUSB_DBG_MASK_PIRP,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> SetDevicePowerState: block runtime D%d while busy/in-service (S0)\n",
                   pDevExt->PortName, PowerState - 1)
           );

           // Complete IRP as success without changing HW power; keeps device in D0
           PoStartNextPowerIrp(Irp);
           Irp->IoStatus.Status = STATUS_INVALID_DEVICE_STATE;
           Irp->IoStatus.Information = 0;

           // Release remove lock acquired in power management path
           QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
           return STATUS_INVALID_DEVICE_STATE;
       }
   }
   // For our device, we can consider it as either D0 or D3
   if (PowerState == PowerDeviceD0)
   {
      if (PowerState < pDevExt->DevicePower)
      {
         // Set power-up completion routine and forward the IRP
         IoCopyCurrentIrpStackLocationToNext(Irp);
         IoSetCompletionRoutine
         (
            Irp,
            (PIO_COMPLETION_ROUTINE)QCPWR_DevicePowerUpIrpCompletion,
            pDevExt,
            TRUE, TRUE, TRUE
         );
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> SetDevicePowerState: already in D0, no act(0x%p)\n", pDevExt->PortName, Irp)
         );
         PoStartNextPowerIrp(Irp);
         IoSkipCurrentIrpStackLocation(Irp);
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
      }
   }
   else
   {
      // Decrease Power
      QCPWR_DecreaseDevicePower(pDevExt, Irp, PowerState);
   }

   return nts;
}  // QCPWR_SetDevicePowerState

NTSTATUS QCPWR_DevicePowerUpIrpCompletion
(
   PDEVICE_OBJECT    DeviceObject,
   PIRP              Irp,
   PDEVICE_EXTENSION pDevExt
)
{
   NTSTATUS ntStatus = Irp->IoStatus.Status;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> DevicePowerUpIrpCompletion: 0x%p\n", pDevExt->PortName, Irp)
   );

   if (Irp->PendingReturned)
   {
      IoMarkIrpPending(Irp);
   }

   if (!NT_SUCCESS(ntStatus))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> DevicePowerUpIrpCompletion failure 0x%x\n", pDevExt->PortName, ntStatus)
      );
      PoStartNextPowerIrp(Irp);

      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);

      // Continue completion
      return STATUS_SUCCESS;
   }

   ntStatus = QCPWR_ScheduleDevicePowerUp(pDevExt, Irp);
   if (!NT_SUCCESS(ntStatus))
   {
      PoStartNextPowerIrp(Irp);

      // Complete the IRP
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> PIRP (Cdev-s0 0x%x) 0x%p\n", pDevExt->PortName, Irp->IoStatus.Status, Irp)
      );

      // IoCompleteRequest(Irp, IO_NO_INCREMENT);
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);

      return STATUS_SUCCESS;
   }

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // QCPWR_DevicePowerUpIrpCompletion

NTSTATUS QCPWR_DecreaseDevicePower
(
   PDEVICE_EXTENSION  pDevExt,
   PIRP               Irp,
   DEVICE_POWER_STATE NewDevicePower
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> DecreaseDevicePower: IRP 0x%p\n", pDevExt->PortName, Irp)
   );

   if((pDevExt->DevicePower == PowerDeviceD0) && (NewDevicePower > pDevExt->DevicePower))
   {
      // D0 -> Dn, stop Device
      pDevExt->PowerSuspended = TRUE;

      if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
      {
         USBIF_SyncPowerDownDevice(pDevExt, NewDevicePower);
      }
   }
   else
   {
      // We have following cases:
      //    1. D0 -> D0
      //    2. D3 -> D3
      // So, no action
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> DecreaseDevicePower: not in D0, no act(0x%p)\n", pDevExt->PortName, Irp)
      );
   }

   pDevExt->DevicePower = NewDevicePower;

   // Set Completion Routine so PoStartNext... gets called.
   IoCopyCurrentIrpStackLocationToNext(Irp);
   IoSetCompletionRoutine
   (
      Irp,
      (PIO_COMPLETION_ROUTINE)QCPWR_DevicePowerDownIrpCompletion,
      pDevExt,
      TRUE, TRUE, TRUE
   );

   return ntStatus;

}  // QCPWR_DecreaseDevicePower

NTSTATUS QCPWR_ScheduleDevicePowerUp
(
   PDEVICE_EXTENSION pDevExt,
   PIRP              Irp
)
{
   NTSTATUS               ntStatus;
   PIO_WORKITEM           item;
   PWORKER_ITEM_CONTEXT   context;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->ScheduleDevicePowerUp: 0x%p\n", pDevExt->PortName, Irp)
   );

   context = ExAllocatePool(NonPagedPool, sizeof(WORKER_ITEM_CONTEXT));

   if (context != NULL)
   {
      item = IoAllocateWorkItem(pDevExt->MiniportFDO);

      if (item != NULL)
      {
         context->Irp             = Irp;
         context->DeviceExtension = pDevExt;
         context->WorkItem        = item;

         IoMarkIrpPending(Irp);

         IoQueueWorkItem
         (
            item,
            QCPWR_PowerUpDevice,
            DelayedWorkQueue,
            context
         );

         ntStatus = STATUS_PENDING;
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> ScheduleDevicePowerUp: Out of mem 1\n", pDevExt->PortName)
         );
         ExFreePool(context);
         ntStatus = STATUS_INSUFFICIENT_RESOURCES;
      }
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> ScheduleDevicePowerUp: Out of mem 2\n", pDevExt->PortName)
      );
      ntStatus = STATUS_INSUFFICIENT_RESOURCES;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> <--ScheduleDevicePowerUp: 0x%p ST 0x%x\n", pDevExt->PortName, Irp, ntStatus)
   );
   return ntStatus;
}  // QCPWR_ScheduleDevicePowerUp

VOID QCPWR_PowerUpDevice
(
   PDEVICE_OBJECT DeviceObject,
   PVOID          Context
)
{
   NTSTATUS               ntStatus;
   PWORKER_ITEM_CONTEXT   context  = (PWORKER_ITEM_CONTEXT)Context;
   PDEVICE_EXTENSION      pDevExt  = context->DeviceExtension;
   PIRP                   irp      = context->Irp;
   PIO_STACK_LOCATION     irpStack = IoGetCurrentIrpStackLocation(irp);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> PowerUpDevice: 0x%p\n", pDevExt->PortName, irp)
   );

   // Resume I/O
   pDevExt->DevicePower = PowerDeviceD0;  // device has only D0 and D3
   pDevExt->PowerSuspended = pDevExt->SelectiveSuspended = FALSE;
/****
   PoSetPowerState
   (
      pDevExt->MiniportFDO, // DeviceObject,
      DevicePowerState,
      irpStack->Parameters.Power.State
   );
****/

   QCPWR_ResumeDataThreads(pDevExt, TRUE);

   // Free memory
   IoFreeWorkItem(context->WorkItem);
   ExFreePool((PVOID)context);

   // Now it's time to start the next power IRP
   PoStartNextPowerIrp(irp);

   // Complete the IRP
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> PIRP (Cdev-s 0x%x) 0x%p\n", pDevExt->PortName, irp->IoStatus.Status, irp)
   );

   IoCompleteRequest(irp, IO_NO_INCREMENT);

   QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 6); // end of _PowerUpDevice
   QcIoReleaseRemoveLock(pDevExt->pRemoveLock, irp, 0);

}  // QCPWR_PowerUpDevice

// ========================================================
//          Selective Suspend
// ========================================================
VOID QCPWR_CancelIdleTimer
(
   PDEVICE_EXTENSION pDevExt,
   UCHAR             BusyMask,
   BOOLEAN           CancelIdleNotificationIrp,
   UCHAR             Cookie
)
{
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE     levelOrHandle;
   #else
   KIRQL                  levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->CancelIdleTimer(%u): BusyMask 0x%x\n", pDevExt->PortName, Cookie, BusyMask)
   );

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);

   pDevExt->IoBusyMask |= BusyMask;
   if (KeCancelTimer(&pDevExt->IdleTimer) == TRUE)  // successfully dequeued timer obj
   {
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, (PVOID)&pDevExt->IdleTimer, 3);

      // Re-initialize to non-signal state
      KeInitializeTimer(&pDevExt->IdleTimer);
      pDevExt->IdleTimerLaunched = FALSE;
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> CancelIdleTimer(deQ) - RML %d\n", pDevExt->PortName, pDevExt->Sts.lRmlCount[3])
      );
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
   }
   else if (CancelIdleNotificationIrp == TRUE)
   {
      // It's possible there's a pending idle IRP in this case.
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      QCPWR_CancelIdleNotificationIrp(pDevExt, 1);
   }
   else
   {
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> <--CancelIdleTimer(%u): BusyMask 0x%x\n", pDevExt->PortName, Cookie, BusyMask)
   );
}  // QCPWR_CancelIdleTimer

VOID QCPWR_SetIdleTimer
(
   PDEVICE_EXTENSION pDevExt,
   UCHAR             BusyMask,
   BOOLEAN           NoReset,
   UCHAR             Cookie
)
{
   LARGE_INTEGER          dueTime;
   NTSTATUS               ntStatus = STATUS_SUCCESS;
   BOOLEAN                bExpired = TRUE;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE     levelOrHandle;
   #else
   KIRQL                  levelOrHandle;
   #endif
   KIRQL                  irql = KeGetCurrentIrql();

   QcAcquireSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, &levelOrHandle, irql);

   pDevExt->IoBusyMask &= ~(BusyMask);  // clear mask bit

   if ((pDevExt->WdmVersion < WinXpOrHigher)                   ||
       ((pDevExt->SelectiveSuspendInMili == TRUE) && (pDevExt->SelectiveSuspendIdleTime < QCUSB_SS_MILI_IDLE_MIN)) ||
       (pDevExt->SelectiveSuspendIdleTime < QCUSB_SS_IDLE_MIN) ||
       (pDevExt->PowerManagementEnabled == FALSE)) //  ||
       // (pDevExt->ExWdmMonitorStarted == FALSE))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> <--SetIdleTimer(%u): BM 0x%x SS disabled\n", pDevExt->PortName, Cookie, BusyMask)
      );
      // No selective suspend for Win2K and lower versions
      QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
      return;
   }

   if ((pDevExt->bInService == TRUE) && (pDevExt->InServiceSelectiveSuspension == FALSE))
   {
      QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
      return;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->SetIdleTimer(%u): BusyMask 0x%x IRQL %u Idle %us Rml[0]=%u\n", pDevExt->PortName,
        Cookie, BusyMask, irql, pDevExt->SelectiveSuspendIdleTime, pDevExt->Sts.lRmlCount[0])
   );

   if (pDevExt->PowerSuspended == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> <--SetIdleTimer(%u): Suspended, no act\n", pDevExt->PortName, Cookie)
      );
      QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
      return;
   }

   if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> <--SetIdleTimer(%u): dev not in working state\n", pDevExt->PortName, Cookie)
      );
      QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
      return;
   }

   if (pDevExt->IdleNotificationIrp != NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> <--SetIdleTimer(%u): Idle req pending, no act 0x%p\n", pDevExt->PortName, Cookie,
           pDevExt->IdleNotificationIrp)
      );
      QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
      return;
   }

   // USBPWR_GetSystemPowerState(pDevExt, QCUSB_PREPARE_TO_STANDBY);
   if (pDevExt->PrepareToPowerDown == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> <--SetIdleTimer(%u): powering down, no act\n", pDevExt->PortName, Cookie)
      );
      QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
      return;
   }

   if (pDevExt->IoBusyMask == 0)
   {
      BOOLEAN rmLockAdded = FALSE;

      bExpired = KeReadStateTimer(&pDevExt->IdleTimer);

      if ((pDevExt->IdleTimerLaunched == FALSE) || // first-time launch
          (bExpired == TRUE))                      // signaled/expired
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> SetIdleTimer: re-start %d\n", pDevExt->PortName, pDevExt->Sts.lRmlCount[3])
         );
         pDevExt->IdleTimerLaunched = TRUE;
         ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, (PVOID)&pDevExt->IdleTimer);
         QcInterlockedIncrement(3, &pDevExt->IdleTimer, 3);
         rmLockAdded = TRUE;
      }

      if ((bExpired == FALSE) && (NoReset == TRUE))
      {
         // no action
         ntStatus = STATUS_TIMER_NOT_CANCELED;
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> SetIdleTimer: no reset\n", pDevExt->PortName)
         );
         if (rmLockAdded == TRUE)
         {
            pDevExt->IdleTimerLaunched = FALSE;
            QcIoReleaseRemoveLock(pDevExt->pRemoveLock, (PVOID)&pDevExt->IdleTimer, 3);
         }
      }
      else if (NT_SUCCESS(ntStatus))
      {
          if (pDevExt->SelectiveSuspendInMili == TRUE)
          {
             dueTime.QuadPart = (LONGLONG)(-10000) * pDevExt->SelectiveSuspendIdleTime;
          }
          else
          {
             dueTime.QuadPart = (LONGLONG)(-10000) * 1000 * pDevExt->SelectiveSuspendIdleTime;
          }
          
         KeSetTimer(&pDevExt->IdleTimer, dueTime, &pDevExt->IdleDpc);
      }

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> <--SetIdleTimer: ST 0x%x\n", pDevExt->PortName, ntStatus)
      );
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> <--SetIdleTimer: err - IoBusyMask 0x%x (not 0) (RML %d)\n", 
           pDevExt->PortName, pDevExt->IoBusyMask, pDevExt->Sts.lRmlCount[3])
      );
   }

   QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);

}  // QCPWR_SetIdleTimer

// Idle timer DPC routine
VOID QCPWR_IdleDpcRoutine
(
   PKDPC Dpc,
   PVOID DeferredContext,
   PVOID SystemArgument1,
   PVOID SystemArgument2
)
{
   PDEVICE_EXTENSION      pDevExt;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE     levelOrHandle;
   #else
   KIRQL                  levelOrHandle;
   #endif

   pDevExt = (PDEVICE_EXTENSION)DeferredContext;
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->IdleDpcRoutine: context 0x%p\n", pDevExt->PortName, DeferredContext)
   );

   KeSetEvent
   (
      &pDevExt->InterruptRegIdleEvent,
      IO_NO_INCREMENT,
      FALSE
   );
   QcIoReleaseRemoveLock(pDevExt->pRemoveLock, (PVOID)&pDevExt->IdleTimer, 3);
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--IdleDpcRoutine: context 0x%p\n", pDevExt->PortName, DeferredContext)
   );
}  // QCPWR_IdleDpcRoutine

// This routine is called by the interrupt thread
// Rule: system must be at S0 (or above SystemWake)
NTSTATUS QCPWR_RegisterIdleNotification(PDEVICE_EXTENSION pDevExt)
{
   PIRP                    irp = NULL;
   PUSB_IDLE_CALLBACK_INFO idleCallbackInfo = NULL;
   NTSTATUS                ntStatus;
   KIRQL                   oldIrql;
   PIO_STACK_LOCATION      nextStack;
   BOOLEAN                 bToRegister = FALSE;

   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE      levelOrHandle;
   #else
   KIRQL                   levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->RegisterIdleNotification\n", pDevExt->PortName)
   );
   if ((pDevExt->bInService == TRUE) || (pDevExt->InServiceSelectiveSuspension == FALSE) || (pDevExt->IoBusyMask != 0))
   {
       QCUSB_DbgPrint
       (
           QCUSB_DBG_MASK_PIRP,
           QCUSB_DBG_LEVEL_DETAIL,
           ("<%s> RegisterIdleNotification: in-service/busy, abort\n", pDevExt->PortName)
       );
       return STATUS_POWER_STATE_INVALID;
   }
   // USBPWR_GetSystemPowerState(pDevExt, QCUSB_SYS_POWER_CURRENT);

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);

   if (KeReadStateTimer(&pDevExt->IdleTimer) == TRUE)  // not been reset
   {
      // Move the following to DPC
      // QcIoReleaseRemoveLock(pDevExt->pRemoveLock, (PVOID)&pDevExt->IdleTimer, 3);

      if (pDevExt->SystemPower <= pDevExt->DeviceCapabilities.SystemWake)
      {
         // Signal the INT thread to submit an idle request tlo the bus
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> RegIdle: to request idle IRP\n", pDevExt->PortName)
         );
         bToRegister = TRUE;
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> RegIdle: SystemPower low, not to send idle req\n", pDevExt->PortName)
         );
      }
   }

   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   if (bToRegister == FALSE)
   {
      ntStatus = STATUS_POWER_STATE_INVALID;
      goto RegisterIdleNotification_Exit;
   }

   if (pDevExt->PowerManagementEnabled == FALSE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> RegisterIdleNotification: selective suspension disabled\n", pDevExt->PortName)
      );
      ntStatus = STATUS_POWER_STATE_INVALID;
      goto RegisterIdleNotification_Exit;
   }

   if (pDevExt->PrepareToPowerDown == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> <--RegisterIdleNotification: powering down, no act\n", pDevExt->PortName)
      );
      ntStatus = STATUS_POWER_STATE_INVALID;
      goto RegisterIdleNotification_Exit;
   }

   if(PowerDeviceD0 != pDevExt->DevicePower)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> RegisterIdleNotification: wrong dev power state: D%u\n", pDevExt->PortName,
           pDevExt->DevicePower-1)
      );
      ntStatus = STATUS_POWER_STATE_INVALID;
      goto RegisterIdleNotification_Exit;
   }

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   if ((pDevExt->IdleNotificationIrp != NULL) || (pDevExt->IoBusyMask != 0))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> RegisterIdleNotification: dev busy, abort (0x%p, 0x%x)\n", pDevExt->PortName,
          pDevExt->IdleNotificationIrp, pDevExt->IoBusyMask)
      );
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      ntStatus = STATUS_DEVICE_BUSY;
      goto RegisterIdleNotification_Exit;
   }
   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   idleCallbackInfo = ExAllocatePool
                      (
                         NonPagedPool,
                         sizeof(struct _USB_IDLE_CALLBACK_INFO)
                      );

   if (idleCallbackInfo != NULL)
   {
      idleCallbackInfo->IdleCallback = QCPWR_IdleNotificationCallback;
      idleCallbackInfo->IdleContext = (PVOID)pDevExt;

      irp = IoAllocateIrp((CCHAR)(pDevExt->MyDeviceObject->StackSize+1), FALSE);

      if (irp == NULL)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> RegisterIdleNotification: no mem-0\n", pDevExt->PortName)
         );

         ExFreePool(idleCallbackInfo);
         ntStatus = STATUS_INSUFFICIENT_RESOURCES;
         goto RegisterIdleNotification_Exit;
      }

      nextStack = IoGetNextIrpStackLocation(irp);
      nextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
      nextStack->Parameters.DeviceIoControl.IoControlCode =
                 IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION;
      nextStack->Parameters.DeviceIoControl.Type3InputBuffer =
                 idleCallbackInfo;
      nextStack->Parameters.DeviceIoControl.InputBufferLength =
                 sizeof(struct _USB_IDLE_CALLBACK_INFO);

      IoSetCompletionRoutine
      (
         irp,
         QCPWR_IdleNotificationIrpCompletion,
         idleCallbackInfo,
         TRUE, TRUE, TRUE
      );

      if ((pDevExt->IoBusyMask != 0) ||
          (PowerDeviceD0 != pDevExt->DevicePower))
      {
         ExFreePool(idleCallbackInfo);
         pDevExt->IdleNotificationIrp = NULL; // not necessary
         IoFreeIrp(irp);
         ntStatus = STATUS_UNSUCCESSFUL;
      }
      else
      {
         QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
         if (KeCancelTimer(&pDevExt->IdleTimer) == TRUE)
         {
            QcIoReleaseRemoveLock(pDevExt->pRemoveLock, (PVOID)&pDevExt->IdleTimer, 3);
            // Re-initialize to non-signal state
            KeInitializeTimer(&pDevExt->IdleTimer);
            pDevExt->IdleTimerLaunched = FALSE;
         }
         pDevExt->IdleNotificationIrp = irp;
         QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

         ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, irp);
         if (NT_SUCCESS(ntStatus))
         {
            // update statistics
            QcInterlockedIncrement(0, irp, 12);
         }
         else
         {
            ExFreePool(idleCallbackInfo);
            IoFreeIrp(irp);
            ntStatus = STATUS_UNSUCCESSFUL;
            pDevExt->IdleNotificationIrp = NULL;
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_PIRP,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> RegisterIdleNotification: rm lock failrue\n", pDevExt->PortName)
            );
            goto RegisterIdleNotification_Exit;
         }

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> RegisterIdleNotification: Submit an idle req at D%u 0x%p\n",
              pDevExt->PortName, pDevExt->DevicePower-1, irp)
         );

         ntStatus = IoCallDriver(pDevExt->StackDeviceObject, irp);
         if (NT_SUCCESS(ntStatus))
         {
            KeSetEvent
            (
               &pDevExt->RegIdleAckEvent,
               IO_NO_INCREMENT,
               FALSE
            );
         }
      }
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> RegisterIdleNotification: no mem\n", pDevExt->PortName)
      );
      ntStatus = STATUS_INSUFFICIENT_RESOURCES;
   }

RegisterIdleNotification_Exit:

   KeClearEvent(&pDevExt->InterruptRegIdleEvent);

   // Set ack event

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> <--RegisterIdleNotification: ST 0x%x RML %d\n", pDevExt->PortName,
        ntStatus, pDevExt->Sts.lRmlCount[3])
   );

   return ntStatus;
}  // QCPWR_RegisterIdleNotification

VOID QCPWR_IdleNotificationCallback(PDEVICE_EXTENSION pDevExt)
{
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->IdleNotificationCallback\n", pDevExt->PortName)
   );

   if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> IdleNotificationCallback: err-dev not ready\n", pDevExt->PortName)
      );

      return;
   }

   KeSetEvent(&pDevExt->InterruptIdleEvent, 0, FALSE);

   QCPWR_IdleNotificationCallbackEpisode(pDevExt);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> <--IdleNotificationCallback\n", pDevExt->PortName)
   );
   KeSetEvent(&pDevExt->IdleCbkCompleteEvent, IO_NO_INCREMENT, FALSE);
}  // QCPWR_IdleNotificationCallback

VOID QCPWR_IdleNotificationCallbackEpisode(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS                ntStatus;
   POWER_STATE             powerState;
   KEVENT                  irpCompletionEvent;
   PIRP_COMPLETION_CONTEXT irpContext;
   int                     i = 0;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->IdleNotificationCallbackEpisode\n", pDevExt->PortName)
   );

   if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> IdleNotificationCallbackEpisode: err-dev not ready\n", pDevExt->PortName)
      );

      return;
   }

   if (pDevExt->PrepareToPowerDown == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> <--IdleNotificationCallbackEpisode: powering down, no act\n", pDevExt->PortName)
      );
      return;
   }

   if (pDevExt->bRemoteWakeupEnabled == TRUE)
   {
      QCPWR_RegisterWaitWakeIrp(pDevExt, 2);
   }

   // power down the device

   irpContext = (PIRP_COMPLETION_CONTEXT)ExAllocatePool
                                         (
                                            NonPagedPool,
                                            sizeof(IRP_COMPLETION_CONTEXT)
                                         );

   if (irpContext == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> IdleNotificationCallbackEpisode: Out of mem 1\n", pDevExt->PortName)
      );
      ntStatus = STATUS_INSUFFICIENT_RESOURCES;
   }
   else
   {
      powerState.DeviceState = pDevExt->PowerDownLevel;
      KeInitializeEvent(&irpCompletionEvent, NotificationEvent, FALSE);
      irpContext->DeviceExtension = pDevExt;
      irpContext->Event = &irpCompletionEvent;

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> IdleNotificationCallbackEpisode: Sending PWR req for D%u\n",
           pDevExt->PortName, powerState.DeviceState-1)
      );

      ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, irpContext);
      if (NT_SUCCESS(ntStatus))
      {
         // update statistics
         QcInterlockedIncrement(0, irpContext, 4);
      }
      else
      {
         ExFreePool(irpContext);
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> IdleNotificationCallbackEpisode: rm lock failure\n", pDevExt->PortName)
         );
         return;
      }

      // add power irp record
      QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
      if (!IsListEmpty(&pDevExt->OriginatedPwrReqPool))
      {
         PLIST_ENTRY                headOfList;
         PSELF_ORIGINATED_POWER_REQ pwrRec;

         headOfList = RemoveHeadList(&pDevExt->OriginatedPwrReqPool);
         pwrRec = CONTAINING_RECORD(headOfList, SELF_ORIGINATED_POWER_REQ, List);
         pwrRec->PwrState.DeviceState = powerState.DeviceState;
         InsertTailList(&pDevExt->OriginatedPwrReqQueue, &(pwrRec->List));
      }
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

      ntStatus = PoRequestPowerIrp
                 (
                    pDevExt->PhysicalDeviceObject,
                    IRP_MN_SET_POWER,
                    powerState,
                    (PREQUEST_POWER_COMPLETE)QCPWR_TopDeviceSetPowerIrpCompletionRoutine,
                    irpContext,
                    NULL
                 );

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> IdleNotificationCallback: PWR req for D%u sent, status 0x%x\n",
           pDevExt->PortName, powerState.DeviceState-1, ntStatus)
      );

      if (!NT_SUCCESS(ntStatus))
      {
         // remove the added "self" record
         QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
         if (!IsListEmpty(&pDevExt->OriginatedPwrReqQueue))
         {
            PLIST_ENTRY                tailOfList;
            PSELF_ORIGINATED_POWER_REQ pwrRec;

            tailOfList = RemoveTailList(&pDevExt->OriginatedPwrReqQueue);
            pwrRec = CONTAINING_RECORD(tailOfList, SELF_ORIGINATED_POWER_REQ, List);
            InsertHeadList(&pDevExt->OriginatedPwrReqPool, &(pwrRec->List));
         }
         QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      }

      if (STATUS_PENDING == ntStatus)
      {
         NTSTATUS nts;
         LARGE_INTEGER delayValue;

         // Wait for power IRP completion
         while (TRUE)
         { 
            delayValue.QuadPart = -(10 * 1000 * 1000); // 1 second
            nts = KeWaitForSingleObject
                  (
                     &irpCompletionEvent,
                     Executive,
                     KernelMode,
                     FALSE,
                     &delayValue
                  );

            if (nts == STATUS_TIMEOUT)
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_PIRP,
                  QCUSB_DBG_LEVEL_ERROR,
                  ("<%s> WTO-2 (%d)\n", pDevExt->PortName, ++i)
               );
            }
            else
            {
               QCUSB_DbgPrint
               (
                  QCUSB_DBG_MASK_PIRP,
                  QCUSB_DBG_LEVEL_DETAIL,
                  ("<%s> Got IRP completion event (%d)\n", pDevExt->PortName, ++i)
               );
               break;  // out of the loop
            }
         }

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> IdleNotificationCallbackEpisode: powered down\n", pDevExt->PortName)
         );
         pDevExt->SelectiveSuspended = TRUE;
      }
   }

   if (!NT_SUCCESS(ntStatus))
   {
      if (irpContext)
      {
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, irpContext, 0);
         ExFreePool(irpContext);
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> <--IdleNotificationCallbackEpisode 0x%x\n", pDevExt->PortName, ntStatus)
   );
}  // QCPWR_IdleNotificationCallbackEpisode

VOID QCPWR_TopDeviceSetPowerIrpCompletionRoutine
(
   PDEVICE_OBJECT   DeviceObject,
   UCHAR            MinorFunction,
   POWER_STATE      PowerState,
   PVOID            Context,
   PIO_STATUS_BLOCK IoStatus
)
{
   PIRP_COMPLETION_CONTEXT irpContext;
   PDEVICE_EXTENSION       pDevExt;

   if (Context != NULL)
   {
      irpContext = (PIRP_COMPLETION_CONTEXT)Context;
      pDevExt    = irpContext->DeviceExtension;

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> TopDevSetPowerIrpComp: ST 0x%x\n", pDevExt->PortName, IoStatus->Status)
      );

      KeSetEvent(irpContext->Event, 0, FALSE);

      // Release remove lock
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, irpContext, 0);
      ExFreePool(irpContext);
   }

   return;
}  // QCPWR_TopDeviceSetPowerIrpCompletionRoutine

NTSTATUS QCPWR_IdleNotificationIrpCompletion
(
   PDEVICE_OBJECT          DeviceObject,
   PIRP                    Irp,
   PUSB_IDLE_CALLBACK_INFO IdleCallbackInfo
)
{
   PDEVICE_EXTENSION                pDevExt  = IdleCallbackInfo->IdleContext;
   NTSTATUS                         ntStatus = Irp->IoStatus.Status;
   PQCUSB_IDLE_IRP_COMPLETION_PARAM param;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE     levelOrHandle;
   #else
   KIRQL                  levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->IdleNotificationIrpCompletion: IRP 0x%p/0x%p st 0x%x\n", pDevExt->PortName,
        Irp, pDevExt->IdleNotificationIrp,  ntStatus)
   );

   // Allocate stack parameters and push stack
   param = ExAllocatePoolWithTag
           (
              NonPagedPool,
              sizeof(QCUSB_IDLE_IRP_COMPLETION_PARAM),
              QC_MEM_TAG
           );
   if (param == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> IdleNotificationIrpCompletion: NO_MEM, best effort\n", pDevExt->PortName)
      );
      QCPWR_IdleNotificationIrpCompletionEpisode
      (
         DeviceObject, Irp, IdleCallbackInfo
      );
   }
   else
   {
      // push the parameter
      param->DeviceObject     = DeviceObject;
      param->Irp              = Irp;
      param->IdleCallbackInfo = IdleCallbackInfo;

      QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
      InsertTailList(&pDevExt->IdleIrpCompletionStack, &param->List);
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      KeSetEvent(&pDevExt->InterruptIdlenessCompletedEvent, 0, FALSE);
   }
   
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> <--IdleNotificationIrpCompletion [PrepareToPowerDown %d]\n", pDevExt->PortName,
        pDevExt->PrepareToPowerDown)
   );

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // QCPWR_IdleNotificationIrpCompletion

VOID QCPWR_GetOutOfIdleState(PDEVICE_EXTENSION pDevExt)
{
   PQCUSB_IDLE_IRP_COMPLETION_PARAM param = NULL;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE     levelOrHandle;
   #else
   KIRQL                  levelOrHandle;
   #endif

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   if (!IsListEmpty(&pDevExt->IdleIrpCompletionStack))
   {
      PLIST_ENTRY headOfList;

      headOfList = RemoveHeadList(&pDevExt->IdleIrpCompletionStack);
      param = CONTAINING_RECORD(headOfList, QCUSB_IDLE_IRP_COMPLETION_PARAM, List);
   }
   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   if (param != NULL)
   {
      QCPWR_IdleNotificationIrpCompletionEpisode
      (
         param->DeviceObject,
         param->Irp,
         param->IdleCallbackInfo
      );
      ExFreePool(param);
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _GetOutOfIdleState: no param\n", pDevExt->PortName)
      );
   }

}  // QCPWR_GetOutOfIdleState

// Calling this completion routine means the device is no longer idle.
NTSTATUS QCPWR_IdleNotificationIrpCompletionEpisode
(
   PDEVICE_OBJECT          DeviceObject,
   PIRP                    Irp,
   PUSB_IDLE_CALLBACK_INFO IdleCallbackInfo
)
{
   NTSTATUS                ntStatus = Irp->IoStatus.Status;
   PIRP                    idleIrp  = NULL;
   PDEVICE_EXTENSION       pDevExt  = IdleCallbackInfo->IdleContext;
   POWER_STATE             powerState;
   KIRQL                   oldIrql;
   LARGE_INTEGER           dueTime;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE     levelOrHandle;
   #else
   KIRQL                  levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->IdleNotificationIrpCompletionEpisode: IRP 0x%p/0x%p st 0x%x\n", pDevExt->PortName,
        Irp, pDevExt->IdleNotificationIrp,  ntStatus)
   );

   // USBPWR_GetSystemPowerState(pDevExt, QCUSB_PREPARE_TO_STANDBY);

   // Error check is based on DDK - "Selective Suspension of USB Devices"
   if ((pDevExt->PrepareToPowerDown == FALSE) &&
       // ((!NT_SUCCESS(ntStatus) && (ntStatus != STATUS_POWER_STATE_INVALID)) ||
       ((ntStatus != STATUS_POWER_STATE_INVALID) ||

       // Note:

       // We use the following condition to wake up a device that
       // does not have WW enabled even when this completion routine receives
       // STATUS_SUCCESS. This helps to avoid two issues: if one of the
       // logical devices is waken up by an app, the other logical devices
       // are left in suspend state forever, making those logical devices hang.
       // -- because other logical device will receive STATUS_SUCCESS from
       // this routine and will not wake up without the following condition.
       // The second issue is if an app wakes up one of the logical devices,
       // the physical device will never get suspended (because all idle IRP's
       // are completed) until after all logical devices are opened and
       // closed, triggering all logical devices to register idle IRP's.

       // However, this implementation has some overhead, especially during
       // hibernation -- if the system is entering hibernation when the
       // device is selectively suspended, the device needs waking up first,
       // then entering suspend state again.

       (pDevExt->WaitWakeEnabled == FALSE)))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> IdleNotificationIrpCompletionEpisode: err 0x%x, requesting D0\n", pDevExt->PortName, ntStatus)
      );

      // #ifdef QCUSB_REQ_D0_ON_IDLE_ERR

      // if device is in D0, do nothing
      if (pDevExt->DevicePower > PowerDeviceD0)
      {
         // Raise power back to D0
         powerState.DeviceState = PowerDeviceD0;

         ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, pDevExt);
         if (NT_SUCCESS(ntStatus))
         {
            // update statistics
            QcInterlockedIncrement(0, pDevExt, 5);
         }
         else
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_PIRP,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> IdleNotificationIrpCompletionEpisode: rm lock failure 0x%x\n", pDevExt->PortName, ntStatus)
            );
            goto ExitPoint;
         }

         // add power irp record
         QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
         if (!IsListEmpty(&pDevExt->OriginatedPwrReqPool))
         {
            PLIST_ENTRY                headOfList;
            PSELF_ORIGINATED_POWER_REQ pwrRec;
   
            headOfList = RemoveHeadList(&pDevExt->OriginatedPwrReqPool);
            pwrRec = CONTAINING_RECORD(headOfList, SELF_ORIGINATED_POWER_REQ, List);
            pwrRec->PwrState.DeviceState = powerState.DeviceState;
            InsertTailList(&pDevExt->OriginatedPwrReqQueue, &(pwrRec->List));
         }
         QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

         ntStatus = PoRequestPowerIrp
                    (
                       pDevExt->PhysicalDeviceObject,
                       IRP_MN_SET_POWER,
                       powerState,
                       (PREQUEST_POWER_COMPLETE)QCPWR_TopIdlePowerUpCompletionRoutine,
                       pDevExt,
                       NULL
                    );

         if (!NT_SUCCESS(ntStatus))
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_PIRP,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> IdleNotificationIrpCompletionEpisode: err req D0 0x%x\n", pDevExt->PortName, ntStatus)
            );
            QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pDevExt, 0);

            // remove the added "self" record
            QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
            if (!IsListEmpty(&pDevExt->OriginatedPwrReqQueue))
            {
               PLIST_ENTRY                tailOfList;
               PSELF_ORIGINATED_POWER_REQ pwrRec;

               tailOfList = RemoveTailList(&pDevExt->OriginatedPwrReqQueue);
               pwrRec = CONTAINING_RECORD(tailOfList, SELF_ORIGINATED_POWER_REQ, List);
               InsertHeadList(&pDevExt->OriginatedPwrReqPool, &(pwrRec->List));
            }
            QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
         }
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> IdleNotificationIrpCompletionEpisode: dev in D0, no act\n", pDevExt->PortName)
         );
      }
      // #endif // QCUSB_REQ_D0_ON_IDLE_ERR
   }

ExitPoint:

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   if (pDevExt->IdleNotificationIrp == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> IdleNotificationIrpCompletionEpisode: IRP was cancelled\n", pDevExt->PortName)
      );
   }
   else
   {
      pDevExt->IdleNotificationIrp = NULL;
   }
   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   ExFreePool(IdleCallbackInfo);

   QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 0); // end of IdleNotificationComplete

   if (pDevExt->PrepareToPowerDown == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> IdleNotificationIrpCompletionEpisode: powering down\n", pDevExt->PortName)
      );
      USBIF_PowerDownDevice(pDevExt, PowerDeviceD3);
   }

   QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> <--IdleNotificationIrpCompletionEpisode [PrepareToPowerDown %d] Rml[0]=%u\n", pDevExt->PortName,
        pDevExt->PrepareToPowerDown, pDevExt->Sts.lRmlCount[0])
   );

   if (Irp != NULL)
   {
      IoFreeIrp(Irp);
   }

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // QCPWR_IdleNotificationIrpCompletionEpisode

VOID QCPWR_TopIdlePowerUpCompletionRoutine
(
   PDEVICE_OBJECT   DeviceObject,
   UCHAR            MinorFunction,
   POWER_STATE      PowerState,
   PVOID            Context,
   PIO_STATUS_BLOCK IoStatus
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)Context;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> TopIdlePowerUpCompletionRoutine: 0x%x\n", pDevExt->PortName, IoStatus->Status)
   );

   QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pDevExt, 0);

   return;
}  // QCPWR_TopIdlePowerUpCompletionRoutine

// This function is NOT called within SingleIrpSpinLock
// Calling this routine making the IRP be completed with error code which
// in turn triggers the driver to power up the device.
VOID QCPWR_CancelIdleNotificationIrp(PDEVICE_EXTENSION pDevExt, UCHAR Cookie)
{
   PIRP                   irp = NULL;
   KIRQL                  oldIrql;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE     levelOrHandle;
   #else
   KIRQL                  levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->CancelIdleNotificationIrp(%u) 0x%p Rml[0]=%u\n", pDevExt->PortName, Cookie,
        pDevExt->IdleNotificationIrp, pDevExt->Sts.lRmlCount[0])
   );

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   irp = pDevExt->IdleNotificationIrp;
   if (irp == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> CancelIdleNotificationIrp: not pending, no act\n", pDevExt->PortName)
      );
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      return;
   }
   pDevExt->IdleNotificationIrp = NULL;
   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   IoCancelIrp(irp);

   // IRP will be released in the completion function.

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--CancelIdleNotificationIrp(%u) Rml[0]=%u\n", pDevExt->PortName, Cookie,
        pDevExt->Sts.lRmlCount[0])
   );

   return;
}  // QCPWR_CancelIdleNotificationIrp

// ========================================================
//                     Wait-Wake
// ========================================================
VOID QCPWR_ProcessWaitWake(PDEVICE_EXTENSION pDevExt, PIRP Irp)
{
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> ProcessWaitWake: IRP 0x%p\n", pDevExt->PortName, Irp)
   );

   IoMarkIrpPending(Irp);
   IoCopyCurrentIrpStackLocationToNext(Irp);
   IoSetCompletionRoutine
   (
      Irp,
      (PIO_COMPLETION_ROUTINE)QCPWR_WaitWakeCompletion,
      pDevExt,
      TRUE, TRUE, TRUE
   );
   PoStartNextPowerIrp(Irp);
}  // QCPWR_ProcessWaitWake

NTSTATUS QCPWR_WaitWakeCompletion
(
   PDEVICE_OBJECT    DeviceObject,
   PIRP              Irp,
   PDEVICE_EXTENSION pDevExt
)
{
   NTSTATUS               ntStatus;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE     levelOrHandle;
   #else
   KIRQL                  levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->WaitWakeCompletion: 0x%p\n", pDevExt->PortName, Irp)
   );

   if (Irp->PendingReturned)
   {
      IoMarkIrpPending(Irp);
   }

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);

   if (pDevExt->WaitWakeIrp != NULL)
   {
      pDevExt->WaitWakeIrp = NULL;

      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

      PoStartNextPowerIrp(Irp);
      ntStatus = STATUS_SUCCESS;
   }
   else
   {
      // The IRP has been cancelled by CancelWaitWake
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      PoStartNextPowerIrp(Irp);
      ntStatus = STATUS_CANCELLED;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> PIRP (Cww 0x%x/0x%x) 0x%p\n", pDevExt->PortName, Irp->IoStatus.Status, ntStatus, Irp)
   );

   QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--WaitWakeCompletion: 0x%p(0x%x)\n", pDevExt->PortName, Irp, ntStatus)
   );

   return ntStatus;

}  // QCPWR_WaitWakeCompletion

VOID QCPWR_RegisterWaitWakeIrp(PDEVICE_EXTENSION  pDevExt, UCHAR Cookie)
{
   POWER_STATE pwrState;
   NTSTATUS    ntStatus;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->RegisterWaitWake: %u\n", pDevExt->PortName, Cookie)
   );

   if (pDevExt->WaitWakeEnabled == FALSE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> <--RegisterWaitWake(%u): feature disabled\n", pDevExt->PortName, Cookie)
      );

      return;
   }

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);

   if (pDevExt->WaitWakeIrp != NULL)
   {
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> <--RegisterWaitWake: already pending(%u)\n", pDevExt->PortName, Cookie)
      );

      return;
   }

   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   // lowest state from which this Irp will wake the system
   pwrState.SystemState = pDevExt->DeviceCapabilities.SystemWake;

   ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, pDevExt);
   if (NT_SUCCESS(ntStatus))
   {
      // update statistics
      QcInterlockedIncrement(0, pDevExt, 6);
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> <--RegisterWaitWake: rm lock failure(0x%x)\n", pDevExt->PortName, ntStatus)
      );
      return;
   }

   ntStatus = PoRequestPowerIrp
              (
                 pDevExt->PhysicalDeviceObject,
                 IRP_MN_WAIT_WAKE,
                 pwrState,
                 (PREQUEST_POWER_COMPLETE)QCPWR_TopWaitWakeCompletionRoutine,
                 pDevExt,
                 &pDevExt->WaitWakeIrp
              );

   if (!NT_SUCCESS(ntStatus))
   {
      QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
      pDevExt->WaitWakeIrp = NULL;
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pDevExt, 0);
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> <--RegisterWaitWake: requested IRP (0x%p)\n", pDevExt->PortName, pDevExt->WaitWakeIrp)
   );

   return;

}  // QCPWR_RegisterWaitWakeIrp

VOID QCPWR_TopWaitWakeCompletionRoutine
(
   PDEVICE_OBJECT   DeviceObject,
   UCHAR            MinorFunction,
   POWER_STATE      PowerState,
   PVOID            Context,
   PIO_STATUS_BLOCK IoStatus
)
{
   NTSTATUS          ntStatus;
   POWER_STATE       powerState;
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)Context;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> TopWaitWakeCompletion: IRP 0x%p MN 0x%x\n", pDevExt->PortName, pDevExt->WaitWakeIrp, MinorFunction)
   );

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   pDevExt->WaitWakeIrp = NULL;  // IRP is released by the system
   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   if (!NT_SUCCESS(IoStatus->Status))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> TopWaitWakeCompletion failure: IRP 0x%p(0x%x)\n", pDevExt->PortName,
          pDevExt->WaitWakeIrp, IoStatus->Status)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pDevExt, 0);
      return;
   }

   // wake up the device

   if (pDevExt->DevicePower == PowerDeviceD0)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> TopWaitWakeCompletion: device up running\n", pDevExt->PortName)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pDevExt, 0);
      return;
   }

   powerState.DeviceState = PowerDeviceD0;

   ntStatus = PoRequestPowerIrp
              (
                 pDevExt->PhysicalDeviceObject,
                 IRP_MN_SET_POWER,
                 powerState,
                 (PREQUEST_POWER_COMPLETE)QCPWR_TopDeviceWakeUpCompletionRoutine,
                 pDevExt,
                 NULL
              );

   if (!NT_SUCCESS(ntStatus))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> TopWaitWakeCompletion: pwr IRP req failure 0x%x\n", pDevExt->PortName, ntStatus)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pDevExt, 0);
   }

   if (pDevExt->bRemoteWakeupEnabled)
   {
       // QCPWR_RegisterWaitWakeIrp(pDevExt, 1);
   }

   return;

}  // QCPWR_TopWaitWakeCompletionRoutine

VOID QCPWR_TopDeviceWakeUpCompletionRoutine
(
   PDEVICE_OBJECT   DeviceObject,
   UCHAR            MinorFunction,
   POWER_STATE      PowerState,
   PVOID            Context,
   PIO_STATUS_BLOCK IoStatus
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)Context;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> TopDeviceWakeUpCompletionRoutine: MN 0x%x st 0x%x\n", pDevExt->PortName,
        MinorFunction, IoStatus->Status)
   );

   // Release the lock acquired before the WAIT_WAKE request
   QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pDevExt, 0);

   return;

}  // QCPWR_TopDeviceWakeUpCompletionRoutine

VOID QCPWR_CancelWaitWakeIrp(PDEVICE_EXTENSION pDevExt, UCHAR Cookie)
{
   PIRP irp;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> CancelWaitWakeIrp(%u) \n", pDevExt->PortName, Cookie)
   );

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   if (pDevExt->WaitWakeIrp != NULL)
   {
      irp = pDevExt->WaitWakeIrp;
      pDevExt->WaitWakeIrp = NULL;
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      IoCancelIrp(irp);
   }
   else
   {
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
   }
}  // QCPWR_CancelWaitWakeIrp

// ========================================================
//          Version Support for Power Management
// ========================================================
VOID QCPWR_GetWdmVersion(PDEVICE_EXTENSION pDevExt)
{
   if (IoIsWdmVersionAvailable(WDM_MJ_VERSION_VISTA, WDM_MN_VERSION_VISTA))
   {
      pDevExt->WdmVersion = WinVistaOrHigher;
   }
   else if (IoIsWdmVersionAvailable(WDM_MJ_VERSION, WDM_MN_VERSION_XP))
   {
      pDevExt->WdmVersion = WinXpOrHigher;
   }
   else if (IoIsWdmVersionAvailable(WDM_MJ_VERSION, WDM_MN_VERSION_2K))
   {
      pDevExt->WdmVersion = Win2kOrHigher;
   }
   else
   {
      pDevExt->WdmVersion = WinMeOrOlder;
   }
   QCUSB_DbgPrint
   (
      (QCUSB_DBG_MASK_CONTROL | QCUSB_DBG_MASK_POWER),
      QCUSB_DBG_LEVEL_CRITICAL,
      ("<%s> WdmVersionIdx: %d\n", pDevExt->PortName, pDevExt->WdmVersion)
   );
}  // QCPWR_GetWdmVersion

// ========================================================
//          Data Threads Suspend/Resume
// ========================================================

// This function checks if an IRP triggers any communication with device,
// if so the device needs to be in D0 or return to D0. The purpose of
// this function is to tell the caller whether to hold on the IRP until
// the device is set to D0.
BOOLEAN QCPWR_CheckToWakeup
(
   PDEVICE_EXTENSION pDevExt,
   PIRP              Irp,
   UCHAR             BusyMask,
   UCHAR             Cookie
)
{
   PIO_STACK_LOCATION irpStack;
   BOOLEAN            holdRequest = FALSE;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->CheckToWakeup(%u) IRQL(%u): IRP 0x%p Susp %u\n", pDevExt->PortName,
        Cookie, KeGetCurrentIrql(), Irp, pDevExt->PowerSuspended)
   );

   if (Irp == NULL)
   {
      holdRequest = TRUE;
   }
   else
   {
      // Inspect Irp to decide if we do have an interrupt pipe
      irpStack = IoGetCurrentIrpStackLocation(Irp);

      if ((irpStack->MajorFunction == IRP_MJ_CREATE) ||
          (irpStack->MajorFunction == IRP_MJ_CLEANUP))
      {
         // This IRP triggers the USB clear feature to bulk EP's
         holdRequest = TRUE;
      }
      else if (irpStack->MajorFunction == IRP_MJ_WRITE)
      {
         holdRequest = TRUE;
      } 
      // else if ((pDevExt->InterruptPipeIdx != (UCHAR)-1) ||
      else if (pDevExt->InterruptPipe    != (UCHAR)-1)
      {
         // The following will trigger communication with device
         // if there's interrupt pipe
         switch (irpStack->MajorFunction)
         {
            case IRP_MJ_CLOSE:
            {
               holdRequest = TRUE;
               break;
            }
            case IRP_MJ_DEVICE_CONTROL:
            {
               switch (irpStack->Parameters.DeviceIoControl.IoControlCode)
               {
                  case IOCTL_SERIAL_GET_BAUD_RATE:
                  case IOCTL_SERIAL_SET_BAUD_RATE:
                  case IOCTL_SERIAL_GET_LINE_CONTROL:
                  case IOCTL_SERIAL_SET_LINE_CONTROL:
                  case IOCTL_SERIAL_SET_BREAK_ON:
                  case IOCTL_SERIAL_SET_BREAK_OFF:
                  case IOCTL_SERIAL_SET_DTR:
                  case IOCTL_SERIAL_CLR_DTR:
                  case IOCTL_SERIAL_SET_RTS:
                  case IOCTL_SERIAL_CLR_RTS:
                  case IOCTL_SERIAL_SET_HANDFLOW:
                  case IOCTL_QCDEV_SEND_CONTROL:
                  case IOCTL_QCDEV_READ_CONTROL:
                  {
                     holdRequest = TRUE;
                  }
               }
               break;
            }
         }  // switch (irpStack->MajorFunction)
      }
   }

   if (holdRequest == TRUE)
   {
      if (pDevExt->PowerSuspended == FALSE)
      {
         // // Device is in D0
         QCPWR_CancelIdleTimer(pDevExt, BusyMask, TRUE, 1);
         holdRequest = FALSE;
      }
      else
      {
         KeSetEvent
         (
            &pDevExt->DispatchPreWakeupEvent,
            IO_NO_INCREMENT,
            FALSE
         );
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--CheckToWakeup(%u): IRP 0x%p Susp %u hold %u\n", pDevExt->PortName,
        Cookie, Irp, pDevExt->PowerSuspended, holdRequest)
   );

   return holdRequest;

}  // QCPWR_CheckToWakeup

VOID QCPWR_ResumeDataThreads(PDEVICE_EXTENSION pDevExt, BOOLEAN Blocking)
{
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> -->ResumeDataThreads\n", pDevExt->PortName)
   );

   USBIF_SetPowerState(pDevExt->MyDeviceObject, NdisDeviceStateD0);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> <--ResumeDataThreads\n", pDevExt->PortName)
   );

}  // QCPWR_ResumeDataThreads

// ========================================================
//      Power management WMI guids for device control
// ========================================================
VOID QCPWR_RegisterWmiPowerGuids(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS ntStatus;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->RegisterWmiPowerGuids\n", pDevExt->PortName)
   );

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   if (pDevExt->PMWmiRegistered == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_PIRP,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> RegisterWmiPowerGuids: dup req\n", pDevExt->PortName)
      );
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      return;
   }
   pDevExt->PMWmiRegistered = TRUE;
   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   /****
   pDevExt->WmiLibInfo.GuidCount = sizeof(QCSerialPortPowerWmiGuidList) / sizeof(WMIGUIDREGINFO);
   pDevExt->WmiLibInfo.GuidList  = QCSerialPortPowerWmiGuidList;
   pDevExt->WmiLibInfo.QueryWmiRegInfo    = QCPWR_PMQueryWmiRegInfo;
   pDevExt->WmiLibInfo.QueryWmiDataBlock  = QCPWR_PMQueryWmiDataBlock;
   pDevExt->WmiLibInfo.SetWmiDataBlock    = QCPWR_PMSetWmiDataBlock;
   pDevExt->WmiLibInfo.SetWmiDataItem     = QCPWR_PMSetWmiDataItem;
   pDevExt->WmiLibInfo.ExecuteWmiMethod   = NULL;
   pDevExt->WmiLibInfo.WmiFunctionControl = NULL;
   ****/

   ntStatus = IoWMIRegistrationControl
              (
                 pDevExt->MyDeviceObject,
                 WMIREG_ACTION_REGISTER
              );

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--RegisterWmiPowerGuids ST 0x%x\n", pDevExt->PortName, ntStatus)
   );
}  // QCPWR_RegisterWmiPowerGuids

VOID QCPWR_DeregisterWmiPowerGuids(PDEVICE_EXTENSION pDevExt)
{
   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->DeregisterWmiPowerGuids\n", pDevExt->PortName)
   );

   IoWMIRegistrationControl
   (
      pDevExt->MyDeviceObject,
      WMIREG_ACTION_DEREGISTER
   );

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--DeregisterWmiPowerGuids\n", pDevExt->PortName)
   );
}  // QCPWR_DeregisterWmiPowerGuids

NTSTATUS QCPWR_ProcessSystemControlIrps
(
   IN  PDEVICE_EXTENSION pDevExt,
   IN  PIRP              Irp
)
{
   SYSCTL_IRP_DISPOSITION irpDisposition;
   NTSTATUS               ntStatus;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->ProcessSystemControlIrps IRP 0x%p\n", pDevExt->PortName, Irp)
   );

   ntStatus = WmiSystemControl
              (
                 &pDevExt->WmiLibInfo,
                 pDevExt->MyDeviceObject,
                 Irp,
                 &irpDisposition
              );

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> ProcessSystemControlIrps: WmiSystemControl IRP 0x%p ST 0x%x (0x%x)\n",
             pDevExt->PortName, Irp, ntStatus, irpDisposition)
   );

   switch (irpDisposition)
   {
      case IrpProcessed:
      {
         ntStatus = STATUS_SUCCESS;
         break;
      }
      case IrpNotCompleted:
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> CIRP (C 0x%x) 0x%p\n", pDevExt->PortName, Irp->IoStatus.Status, Irp)
         );
         IoCompleteRequest(Irp, IO_NO_INCREMENT);
         ntStatus = STATUS_SUCCESS;
         break;
      }
      case IrpForward:
      case IrpNotWmi:
      {
         ntStatus = STATUS_NOT_SUPPORTED;
         break;
      }
      default:
      {
         ntStatus = STATUS_NOT_SUPPORTED;
         break;
      }
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--ProcessSystemControlIrps IRP 0x%p (0x%x)\n", pDevExt->PortName, Irp, ntStatus)
   );
   return ntStatus;
}  // QCPWR_ProcessSystemControlIrps

NTSTATUS QCPWR_PMQueryWmiRegInfo
(
   IN  PDEVICE_OBJECT  DeviceObject,
   OUT PULONG          RegFlags,
   OUT PUNICODE_STRING InstanceName,
   OUT PUNICODE_STRING *RegistryPath,
   OUT PUNICODE_STRING MofResourceName,
   OUT PDEVICE_OBJECT  *Pdo
)
{
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->PMQueryWmiRegInfo\n", pDevExt->PortName)
   );

   *RegFlags     = WMIREG_FLAG_INSTANCE_PDO;
   *RegistryPath = &gServicePath;
   *Pdo          = pDevExt->PhysicalDeviceObject;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--PMQueryWmiRegInfo\n", pDevExt->PortName)
   );

   return STATUS_SUCCESS;
}  // QCPWR_PMQueryWmiRegInfo

NTSTATUS QCPWR_PMQueryWmiDataBlock
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP           Irp,
   IN ULONG          GuidIndex,
   IN ULONG          InstanceIndex,
   IN ULONG          InstanceCount,
   IN OUT PULONG     InstanceLengthArray,
   IN ULONG          BufferAvail,
   OUT PUCHAR        Buffer
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
   NTSTATUS          ntStatus = STATUS_SUCCESS;

   QC_MP_Paged_Code();

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->PMQueryWmiDataBlock: IRP 0x%p GIDX %u InsIdx %u InsCnt %u\n",
        pDevExt->PortName, Irp, GuidIndex, InstanceIndex, InstanceCount)
   );

   switch (GuidIndex)
   {
      case QCPWR_WMI_POWER_DEVICE_ENABLE:
      {
         if (BufferAvail < sizeof(BOOLEAN))
         {
            ntStatus = STATUS_BUFFER_TOO_SMALL;
            break;
         }

         *(PBOOLEAN)Buffer = pDevExt->PowerManagementEnabled;
         *InstanceLengthArray = sizeof(BOOLEAN);
         break;
      }

      case QCPWR_WMI_POWER_DEVICE_WAKE_ENABLE:
      {
         // if ((0 != InstanceIndex) || (1 != InstanceCount))
         // {
         //    ntStatus = STATUS_INVALID_DEVICE_REQUEST;
         //    break;
         // }

         if (BufferAvail < sizeof(BOOLEAN))
         {
            QCUSB_DbgPrint
            (
               QCUSB_DBG_MASK_PIRP,
               QCUSB_DBG_LEVEL_ERROR,
               ("<%s> PMQueryWmiDataBlock: IRP 0x%p buf too small %uB\n",
                 pDevExt->PortName, Irp, BufferAvail)
            );
            ntStatus = STATUS_BUFFER_TOO_SMALL;
            break;
         }

         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_PIRP,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> PMQueryWmiDataBlock: IRP 0x%p WaitWakeEnabled %u\n",
              pDevExt->PortName, Irp, pDevExt->WaitWakeEnabled)
         );

         *(PBOOLEAN)Buffer = pDevExt->WaitWakeEnabled;
         *InstanceLengthArray = sizeof(BOOLEAN);
         break;
      }

      default:
          ntStatus = STATUS_WMI_GUID_NOT_FOUND;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> CIRP (Cw0 0x%x/0x%x) 0x%p\n", pDevExt->PortName, Irp->IoStatus.Status, ntStatus, Irp)
   );
   ntStatus = WmiCompleteRequest
              (
                 DeviceObject,
                 Irp,
                 ntStatus,
                 sizeof(BOOLEAN),
                 IO_NO_INCREMENT
              );

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--PMQueryWmiDataBlock: IRP 0x%p(ST 0x%x) GIDX %u InsIdx %u InsCnt %u\n",
        pDevExt->PortName, Irp, ntStatus, GuidIndex, InstanceIndex, InstanceCount)
   );

   return ntStatus;
}  // QCPWR_PMQueryWmiDataBlock

NTSTATUS QCPWR_PMSetWmiDataBlock
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP           Irp,
   IN ULONG          GuidIndex,
   IN ULONG          InstanceIndex,
   IN ULONG          BufferSize,
   IN PUCHAR         Buffer
)
{
   PDEVICE_EXTENSION  pDevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
   NTSTATUS           ntStatus = STATUS_SUCCESS;

   QC_MP_Paged_Code();

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->PMSetWmiDataBlock: IRP 0x%p GIDX %u InsIdx %u\n",
        pDevExt->PortName, Irp, GuidIndex, InstanceIndex)
   );

   switch (GuidIndex)
   {
      case QCPWR_WMI_POWER_DEVICE_ENABLE:
      {
         if (BufferSize < sizeof(BOOLEAN))
         {
            ntStatus = STATUS_BUFFER_TOO_SMALL;
            break;
         }
         QCPWR_SetPMState(pDevExt, *(PBOOLEAN)Buffer);
         break;
      }

      case QCPWR_WMI_POWER_DEVICE_WAKE_ENABLE:
      {
         if (BufferSize < sizeof(BOOLEAN))
         {
            ntStatus = STATUS_BUFFER_TOO_SMALL;
            break;
         }
         else if (0 != InstanceIndex)
         {
           ntStatus = STATUS_INVALID_DEVICE_REQUEST;
           break;
         }

         QCPWR_SetWaitWakeState(pDevExt, *(PBOOLEAN)Buffer);
         break;
      }

      default:
         ntStatus = STATUS_WMI_GUID_NOT_FOUND;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> CIRP (Cw1 0x%x/0x%x) 0x%p\n", pDevExt->PortName, Irp->IoStatus.Status, ntStatus, Irp)
   );
   ntStatus = WmiCompleteRequest
              (
                 DeviceObject,
                 Irp,
                 ntStatus,
                 sizeof(BOOLEAN),
                 IO_NO_INCREMENT
              );

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--PMSetWmiDataBlock: IRP 0x%p (ST 0x%x) GIDX %u InsIdx %u\n",
        pDevExt->PortName, Irp, ntStatus, GuidIndex, InstanceIndex)
   );

   return ntStatus;
}  // QCPWR_PMSetWmiDataBlock

NTSTATUS QCPWR_PMSetWmiDataItem
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP           Irp,
   IN ULONG          GuidIndex,
   IN ULONG          InstanceIndex,
   IN ULONG          DataItemId,
   IN ULONG          BufferSize,
   IN PUCHAR         Buffer
)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS          ntStatus = STATUS_SUCCESS;

   QC_MP_Paged_Code();

   pDevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->PMSetWmiDataItem: IRP 0x%p GIDX %u InsIdx %u ItmId %u\n",
        pDevExt->PortName, Irp, GuidIndex, InstanceIndex, DataItemId)
   );

   switch (GuidIndex)
   {
      case QCPWR_WMI_POWER_DEVICE_ENABLE:
      {
         QCPWR_SetPMState(pDevExt, *(PBOOLEAN)Buffer);
         break;
      }

      case QCPWR_WMI_POWER_DEVICE_WAKE_ENABLE:
      {
         if (BufferSize < sizeof(BOOLEAN))
         {
            ntStatus = STATUS_BUFFER_TOO_SMALL;
            break;
         }
         else if ((1 != DataItemId) || (0 != InstanceIndex))
         {
            ntStatus = STATUS_INVALID_DEVICE_REQUEST;
            break;
         }

         QCPWR_SetWaitWakeState(pDevExt, *(PBOOLEAN)Buffer);
         break;
      }

      default:
         ntStatus = STATUS_WMI_GUID_NOT_FOUND;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> CIRP (Cw2 0x%x/0x%x) 0x%p\n", pDevExt->PortName, Irp->IoStatus.Status, ntStatus, Irp)
   );
   ntStatus = WmiCompleteRequest
              (
                 DeviceObject,
                 Irp,
                 ntStatus,
                 0,
                 IO_NO_INCREMENT
              );

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--PMSetWmiDataItem: IRP 0x%p (ST 0x%x) GIDX %u InsIdx %u ItmId %u\n",
        pDevExt->PortName, Irp, ntStatus, GuidIndex, InstanceIndex, DataItemId)
   );
   return ntStatus;
}  // QCPWR_PMSetWmiDataItem

NTSTATUS QCPWR_SetPMState
(
   PDEVICE_EXTENSION pDevExt,
   BOOLEAN           IsEnabled
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->SetPMState: %u\n", pDevExt->PortName, IsEnabled)
   );

   if ((IsEnabled == TRUE) && (pDevExt->PowerManagementEnabled == FALSE))
   {
      pDevExt->PowerManagementEnabled = TRUE;

      // Set idle timer
      QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 9);

       #ifndef QCUSB_PWR
      // Update registry
      ntStatus = QCUTILS_PMSetRegEntry
                 (
                    pDevExt,
                    QCPWR_WMI_POWER_DEVICE_ENABLE,
                    IsEnabled
                 );
      #endif // QCUSB_PWR
   }
   else if ((IsEnabled == FALSE) && (pDevExt->PowerManagementEnabled == TRUE))
   {
      pDevExt->PowerManagementEnabled = FALSE;

      // cancel idle timer/idle IRP
      QCPWR_CancelIdleTimer(pDevExt, 0, TRUE, 5);

       #ifndef QCUSB_PWR
      // Update registry
      ntStatus = QCUTILS_PMSetRegEntry
                 (
                    pDevExt,
                    QCPWR_WMI_POWER_DEVICE_ENABLE,
                    IsEnabled
                 );
      #endif // QCUSB_PWR
   }


   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--SetPMState: %u (ST 0x%x)\n", pDevExt->PortName, IsEnabled, ntStatus)
   );

   return ntStatus;
}  // QCPWR_SetPMState

NTSTATUS QCPWR_SetWaitWakeState
(
   PDEVICE_EXTENSION pDevExt,
   BOOLEAN           IsEnabled
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->SetWaitWakeState: %u\n", pDevExt->PortName, IsEnabled)
   );

   // Handling possible pending WW IRP
   if ((IsEnabled == TRUE) && (pDevExt->WaitWakeEnabled == FALSE))
   {
      pDevExt->WaitWakeEnabled = TRUE;
      QCPWR_RegisterWaitWakeIrp(pDevExt, 6);

      #ifndef QCUSB_PWR
      // Update registry
      ntStatus = QCUTILS_PMSetRegEntry
                 (
                    pDevExt,
                    QCPWR_WMI_POWER_DEVICE_WAKE_ENABLE,
                    IsEnabled
                 );
      #endif // QCUSB_PWR
   }
   else if ((IsEnabled == FALSE) && (pDevExt->WaitWakeEnabled == TRUE))
   {
      pDevExt->WaitWakeEnabled = FALSE;
      QCPWR_CancelWaitWakeIrp(pDevExt, 4);

      #ifndef QCUSB_PWR
      // Update registry
      ntStatus = QCUTILS_PMSetRegEntry
                 (
                    pDevExt,
                    QCPWR_WMI_POWER_DEVICE_WAKE_ENABLE,
                    IsEnabled
                 );
      #endif // QCUSB_PWR
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_PIRP,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--SetWaitWakeState: %u (ST 0x%x)\n", pDevExt->PortName, IsEnabled, ntStatus)
   );

   return ntStatus;
}  // QCPWR_SetWaitWakeState

// ========================================================
//      Power States in Device Capabilities
// ========================================================
VOID QCPWR_VerifyDeviceCapabilities(PDEVICE_EXTENSION pDevExt)
{
   BOOLEAN useD3 = FALSE;

   if (pDevExt->DeviceCapabilities.DeviceState[PowerSystemWorking] != PowerDeviceD0)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> VerifyDeviceCapabilities: S0 maps to D%u, use D0\n",
           pDevExt->PortName,
           pDevExt->DeviceCapabilities.DeviceState[PowerSystemWorking]-1
         )
      );
      pDevExt->DeviceCapabilities.DeviceState[PowerSystemWorking] = PowerDeviceD0;
   }

   if (pDevExt->DeviceCapabilities.DeviceState[PowerSystemSleeping1] <= PowerDeviceD0)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> VerifyDeviceCapabilities: S1 maps to D%u, use D3\n",
           pDevExt->PortName,
           pDevExt->DeviceCapabilities.DeviceState[PowerSystemSleeping1]-1
         )
      );
      useD3 = TRUE;
   }

   if (pDevExt->DeviceCapabilities.DeviceState[PowerSystemSleeping2] <= PowerDeviceD0)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> VerifyDeviceCapabilities: S2 maps to D%u, use D3\n",
           pDevExt->PortName,
           pDevExt->DeviceCapabilities.DeviceState[PowerSystemSleeping2]-1
         )
      );
      useD3 = TRUE;
   }

   if (pDevExt->DeviceCapabilities.DeviceState[PowerSystemSleeping3] <= PowerDeviceD0)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_CONTROL,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> VerifyDeviceCapabilities: S3 maps to D%u, use D3\n",
           pDevExt->PortName,
           pDevExt->DeviceCapabilities.DeviceState[PowerSystemSleeping3]-1
         )
      );
      useD3 = TRUE;
   }

   if (useD3 == TRUE)
   {
      pDevExt->DeviceCapabilities.DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
      pDevExt->DeviceCapabilities.DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
      pDevExt->DeviceCapabilities.DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
      pDevExt->DeviceCapabilities.DeviceState[PowerSystemHibernate] = PowerDeviceD3;
      pDevExt->DeviceCapabilities.DeviceState[PowerSystemShutdown]  = PowerDeviceD3;
   }
}  // QCPWR_VerifyDeviceCapabilities

// ========================================================
//      Enqueue Power IRP
// ========================================================
VOID QCPWR_Enqueue(PDEVICE_EXTENSION pDevExt, PIRP Irp)
{
   PIO_STACK_LOCATION irpStack;
   PLIST_ENTRY        headOfList, peekEntry;
   PIRP               pIrp;
   BOOLEAN            enqueued = FALSE;
   LIST_ENTRY         tmpQueue;
   int                cnt = 0;

   if (IsListEmpty(&pDevExt->DispatchQueue))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> DSP: PWR IRP 0x%p to front-A\n", pDevExt->PortName, Irp)
      );
      InsertHeadList(&pDevExt->DispatchQueue, &Irp->Tail.Overlay.ListEntry);
      return;
   }

   InitializeListHead(&tmpQueue);
   while (!IsListEmpty(&pDevExt->DispatchQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->DispatchQueue);
      pIrp = CONTAINING_RECORD(headOfList, IRP, Tail.Overlay.ListEntry);
      irpStack   = IoGetCurrentIrpStackLocation(pIrp);
      if (irpStack->MajorFunction != IRP_MJ_POWER)
      {
         // put back
         InsertHeadList(&pDevExt->DispatchQueue, &pIrp->Tail.Overlay.ListEntry);

         // place IRP block before non-power IRP block
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> DSP: PWR IRP 0x%p before 0x%x CNT %d\n", pDevExt->PortName,
              pIrp, irpStack->MajorFunction, cnt)
         );
         InsertHeadList(&pDevExt->DispatchQueue, &Irp->Tail.Overlay.ListEntry);
         enqueued = TRUE;
         break;
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> DSP: existing PWR IRP 0x%p[%x,%x]-%d\n", pDevExt->PortName,
              pIrp, irpStack->MajorFunction, irpStack->MinorFunction, cnt)
         );
      }
      InsertTailList(&tmpQueue, &pIrp->Tail.Overlay.ListEntry);
      ++cnt;
   }

   if (enqueued == FALSE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> DSP: PWR IRP 0x%p to front-B CNT %d\n",
           pDevExt->PortName, Irp, cnt)
      );
      InsertHeadList(&pDevExt->DispatchQueue, &Irp->Tail.Overlay.ListEntry);
   }

   while (!IsListEmpty(&tmpQueue))
   {
      headOfList = RemoveTailList(&tmpQueue);
      pIrp =  CONTAINING_RECORD
              (
                 headOfList,
                 IRP,
                 Tail.Overlay.ListEntry
              );
      InsertHeadList(&pDevExt->DispatchQueue, &pIrp->Tail.Overlay.ListEntry);
   }
}  // QCPWR_Enqueue

// ========================================================
//              System Power Information
// ========================================================
// {C88E99DE-6905-47d6-AE7E-A110AA903B98}
DEFINE_GUID(GUID_DEVINTERFACE_QCUSB_WDM,
0xc88e99de, 0x6905, 0x47d6, 0xae, 0x7e, 0xa1, 0x10, 0xaa, 0x90, 0x3b, 0x98);
#define QCWDM_TAG 'MDWQ'

VOID USBPWR_StartExWdmDeviceMonitor(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS ntStatus;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->_StartDeviceMonitor: 0x%x\n", pDevExt->PortName, pDevExt->ExWdmMonitorStarted)
   );

   if (pDevExt->ExWdmMonitorStarted == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _StartDeviceMonitor error: dup\n", pDevExt->PortName)
      );
      return;
   }

   ntStatus = IoRegisterPlugPlayNotification
              (
                 EventCategoryDeviceInterfaceChange,
                 PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                 (PVOID)&GUID_DEVINTERFACE_QCUSB_WDM,
                 pDevExt->MyDriverObject,
                 (PDRIVER_NOTIFICATION_CALLBACK_ROUTINE)USBPWR_DeviceInterfaceChange,
                 (PVOID)pDevExt,
                 &pDevExt->ExWdmIfNotificationEntry
              );

   if (!NT_SUCCESS(ntStatus))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _StartDeviceMonitor error: 0x%x 0x%p\n", pDevExt->PortName,
           ntStatus, pDevExt->ExWdmIfNotificationEntry)
      );
   }
   else
   {
      pDevExt->ExWdmMonitorStarted = TRUE;
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--_StartDeviceMonitor: 0x%x 0x%p\n", pDevExt->PortName,
        pDevExt->ExWdmMonitorStarted, pDevExt->ExWdmIfNotificationEntry)
   );
}  // USBPWR_StartExWdmDeviceMonitor

VOID USBPWR_StopExWdmDeviceMonitor(PDEVICE_EXTENSION pDevExt)
{
   if (pDevExt->ExWdmMonitorStarted == FALSE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _StopDeviceMonitor error: dup\n", pDevExt->PortName)
      );
      return;
   }
   pDevExt->ExWdmMonitorStarted = FALSE;

   USBPWR_CloseExWdmDevice(pDevExt);

   if (pDevExt->ExWdmIfNotificationEntry != NULL)
   {
      // De-ref our own driver object so that we can be removed
      IoUnregisterPlugPlayNotification(pDevExt->ExWdmIfNotificationEntry);
      pDevExt->ExWdmIfNotificationEntry = NULL;
   }
} // USBPWR_StopExWdmDeviceMonitor 

NTSTATUS USBPWR_DeviceInterfaceChange
(
   PVOID NotificationStructure,
   PVOID Context
)
{
   PDEVICE_INTERFACE_CHANGE_NOTIFICATION notification =
      (PDEVICE_INTERFACE_CHANGE_NOTIFICATION)NotificationStructure;
   PDEVICE_EXTENSION pDevExt = Context;
   NTSTATUS          ntStatus = STATUS_SUCCESS;
   PUNICODE_STRING   symbolicLinkName;
   ULONG             len;

   symbolicLinkName = notification->SymbolicLinkName;

   if (!IsEqualGUID((LPGUID)&(notification->InterfaceClassGuid),
                    (LPGUID)&GUID_DEVINTERFACE_QCUSB_WDM))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _DeviceInterfaceChange: wrong IF\n", pDevExt->PortName)
      );
      return STATUS_SUCCESS;
   }

   if (IsEqualGUID((LPGUID)&(notification->Event),
                   (LPGUID)&GUID_DEVICE_INTERFACE_ARRIVAL))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> _DeviceInterfaceChange: ARRIVAL\n", pDevExt->PortName)
      );

      if (pDevExt->ExWdmDeviceSymbName.Buffer != NULL)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_CRITICAL,
            ("<%s> _DeviceInterfaceChange: err - DUP ARRIVAL\n", pDevExt->PortName)
         );
         return STATUS_SUCCESS;
      }

      /********************
      pDevExt->ExWdmDeviceSymbName.MaximumLength = symbolicLinkName->Length + sizeof(UNICODE_NULL);
      pDevExt->ExWdmDeviceSymbName.Length = symbolicLinkName->Length;
      pDevExt->ExWdmDeviceSymbName.Buffer = ExAllocatePoolWithTag
                                             (
                                                NonPagedPool,
                                                pDevExt->ExWdmDeviceSymbName.MaximumLength,
                                                QCWDM_TAG
                                             );
      if (pDevExt->ExWdmDeviceSymbName.Buffer == NULL)
      {
         pDevExt->ExWdmDeviceSymbName.MaximumLength = 0;
         pDevExt->ExWdmDeviceSymbName.Length = 0;
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _DeviceInterfaceChange: NO_MEM\n", pDevExt->PortName)
         );
         KeSetEvent(&pDevExt->DispatchXwdmCloseEvent, IO_NO_INCREMENT, FALSE);
         return STATUS_SUCCESS;
      }
      RtlCopyUnicodeString(&pDevExt->ExWdmDeviceSymbName, symbolicLinkName);

      KeSetEvent(&pDevExt->DispatchXwdmOpenEvent, IO_NO_INCREMENT, FALSE);
      **********************/
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _DeviceInterfaceChange: REMOVAL\n", pDevExt->PortName)
      );
      pDevExt->XwdmStatus = STATUS_DEVICE_NOT_CONNECTED;
      KeSetEvent(&pDevExt->DispatchXwdmCloseEvent, IO_NO_INCREMENT, FALSE);
   }
   return STATUS_SUCCESS;

}  // USBPWR_DeviceInterfaceChange

NTSTATUS USBPWR_DeviceChange
(
    PTARGET_DEVICE_REMOVAL_NOTIFICATION NotificationStruct,
    PVOID                               Context
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)Context;

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->_DeviceChange\n", pDevExt->PortName)
   );

   if ( (IsEqualGUID((LPGUID)&(NotificationStruct->Event),
                     (LPGUID)&GUID_TARGET_DEVICE_QUERY_REMOVE)))
   {
      PTARGET_DEVICE_REMOVAL_NOTIFICATION removalNotification;

      removalNotification =
                  (PTARGET_DEVICE_REMOVAL_NOTIFICATION)NotificationStruct;

      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> _DeviceChange: EX_WDM_QUERY_REMOVE\n", pDevExt->PortName)
      );

      if (pDevExt->ExFileObject != removalNotification->FileObject)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _DeviceChange: err - wrong file obj 0x%p/0x%p\n",
              pDevExt->PortName, removalNotification->FileObject, pDevExt->ExFileObject)
         );
         return STATUS_SUCCESS;
      }
      KeSetEvent(&pDevExt->DispatchXwdmQueryRmEvent, IO_NO_INCREMENT, FALSE);
   }
   else if (IsEqualGUID((LPGUID)&(NotificationStruct->Event),
                        (LPGUID)&GUID_TARGET_DEVICE_REMOVE_COMPLETE))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> _DeviceChange: EX_WDM_REMOVE_COMPLETE\n", pDevExt->PortName)
      );

      KeSetEvent(&pDevExt->DispatchXwdmCloseEvent, IO_NO_INCREMENT, FALSE);
      pDevExt->XwdmStatus = STATUS_DEVICE_NOT_CONNECTED;
   }
   else if (IsEqualGUID((LPGUID)&(NotificationStruct->Event),
                        (LPGUID)&GUID_TARGET_DEVICE_REMOVE_CANCELLED))
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_DETAIL,
         ("<%s> _DeviceChange: EX_WDM_REMOVE_CANCELLED\n", pDevExt->PortName)
      );

      // Should be null because we cleared it in query-remove
      if (pDevExt->ExFileObject != NULL)
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _DeviceChange: err - FileObj not NUL 0x%p\n",
              pDevExt->PortName, pDevExt->ExFileObject)
         );
         return STATUS_SUCCESS;
      }

      // reopen the device and obtain new ExFileObject
      KeSetEvent(&pDevExt->DispatchXwdmReopenEvent, IO_NO_INCREMENT, FALSE);
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _DeviceChange: UNKNOWN_NOTIFICATION\n", pDevExt->PortName)
      );
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--_DeviceChange\n", pDevExt->PortName)
   );
   return STATUS_SUCCESS;
}  // USBPWR_DeviceChange

NTSTATUS USBPWR_OpenExWdmDevice(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS status;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->_OpenExWdmDevice\n", pDevExt->PortName)
   );

   QcAcquireSpinLock(&pDevExt->ExWdmSpinLock, &levelOrHandle);

   if (pDevExt->ExFileObject != NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _OpenExWdmDevice: DUP 0x%p\n", pDevExt->PortName,
           pDevExt->ExFileObject)
      );
      QcReleaseSpinLock(&pDevExt->ExWdmSpinLock, levelOrHandle);
      return STATUS_SUCCESS;
   }

   if (pDevExt->ExWdmDeviceSymbName.Length == 0)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _OpenExWdmDevice: err - no dev name\n", pDevExt->PortName)
      );
      QcReleaseSpinLock(&pDevExt->ExWdmSpinLock, levelOrHandle);
      return STATUS_SUCCESS;
   }

   if (pDevExt->ExWdmInService == TRUE)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _OpenExWdmDevice: err - dev busy\n", pDevExt->PortName)
      );
      QcReleaseSpinLock(&pDevExt->ExWdmSpinLock, levelOrHandle);
      return STATUS_DEVICE_BUSY;
   }

   pDevExt->ExWdmInService = TRUE;
   QcReleaseSpinLock(&pDevExt->ExWdmSpinLock, levelOrHandle);

   status = IoGetDeviceObjectPointer
            (
               &pDevExt->ExWdmDeviceSymbName,
               STANDARD_RIGHTS_ALL,
               &pDevExt->ExFileObject,
               &pDevExt->ExWdmDeviceObject
            );

   if (NT_SUCCESS(status))
   {
      status = IoRegisterPlugPlayNotification
               (
                  EventCategoryTargetDeviceChange,
                  0,
                  (PVOID)pDevExt->ExFileObject,
                  pDevExt->MyDriverObject,
                  (PDRIVER_NOTIFICATION_CALLBACK_ROUTINE)USBPWR_DeviceChange,
                  (PVOID)pDevExt,
                  &pDevExt->ExWdmDevNotificationEntry
               );
      if (!NT_SUCCESS(status))
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _OpenExWdmDevice: fail to reg for dev change 0x%p\n",
              pDevExt->PortName, pDevExt->ExFileObject)
         );
      }
      else
      {
         QCUSB_DbgPrint
         (
             QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> _OpenExWdmDevice: dev 0x%p fileObj 0x%p hdl 0x%p\n",
               pDevExt->PortName, pDevExt->ExWdmDeviceObject, pDevExt->ExFileObject,
               pDevExt->ExWdmDevNotificationEntry)
         );
/*****
         status = USBPWR_RegisterXwdmNotification(pDevExt);
         if (!NT_SUCCESS(status))
         {
            KeSetEvent(&pDevExt->DispatchXwdmCloseEvent, IO_NO_INCREMENT, FALSE);
         }
*****/
      }
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _OpenExWdmDevice: fail to get dev ptr 0x%x\n",
           pDevExt->PortName, status)
      );
   }

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--_OpenExWdmDevice 0x%x\n", pDevExt->PortName, status)
   );
   return status;
}  // USBPWR_OpenExWdmDevice

VOID USBPWR_CloseExWdmDevice(PDEVICE_EXTENSION pDevExt)
{
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> -->_CloseExWdmDevice 0x%p, 0x%p, 0x%p\n", pDevExt->PortName,
        pDevExt->ExWdmDeviceObject, pDevExt->ExFileObject,
        pDevExt->ExWdmDevNotificationEntry)
   );
  
   QcAcquireSpinLock(&pDevExt->ExWdmSpinLock, &levelOrHandle);
   pDevExt->ExWdmInService = FALSE;
   QcReleaseSpinLock(&pDevExt->ExWdmSpinLock, levelOrHandle);

   USBPWR_CancelXwdmNotification(pDevExt);

   QcAcquireSpinLock(&pDevExt->ExWdmSpinLock, &levelOrHandle);
   // De-ref the ExWdm device object
   if (pDevExt->ExFileObject != NULL)
   {
      ObDereferenceObject(pDevExt->ExFileObject);
      pDevExt->ExFileObject = NULL;
   }
   pDevExt->ExWdmDeviceObject = NULL;
   QcReleaseSpinLock(&pDevExt->ExWdmSpinLock, levelOrHandle);

   // De-ref our driver object
   if (pDevExt->ExWdmDevNotificationEntry != NULL)
   {
      IoUnregisterPlugPlayNotification(pDevExt->ExWdmDevNotificationEntry);
      pDevExt->ExWdmDevNotificationEntry = NULL;
   }

   // free memory
   QcAcquireSpinLock(&pDevExt->ExWdmSpinLock, &levelOrHandle);
   if (pDevExt->ExWdmDeviceSymbName.Buffer != NULL)
   {
      ExFreePool(pDevExt->ExWdmDeviceSymbName.Buffer);
      pDevExt->ExWdmDeviceSymbName.MaximumLength = 0;
      pDevExt->ExWdmDeviceSymbName.Length        = 0;
      pDevExt->ExWdmDeviceSymbName.Buffer        = NULL;
   }
   QcReleaseSpinLock(&pDevExt->ExWdmSpinLock, levelOrHandle);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_TRACE,
      ("<%s> <--_CloseExWdmDevice 0x%p\n", pDevExt->PortName, pDevExt->ExWdmDeviceObject)
   );
}  // USBPWR_CloseExWdmDevice

VOID USBPWR_QuerySystemPowerState(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS ntStatus;

   KeClearEvent(&(pDevExt->DispatchXwdmQuerySysPwrAckEvent));

   // 1. set event to DSP
   KeSetEvent(&pDevExt->DispatchXwdmQuerySysPwrEvent, IO_NO_INCREMENT, FALSE);

   // 2. wait event from DSP
   ntStatus = KeWaitForSingleObject
              (
                 &(pDevExt->DispatchXwdmQuerySysPwrAckEvent),
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );
   // 3. sysPower should be stored in pDevExt->SystemPower

}  // USBPWR_QuerySystemPowerState

NTSTATUS USBPWR_GetSysPowerIrpCompletion
(
   PDEVICE_OBJECT dummy,
   PIRP           Irp,
   PVOID          DeviceExtension
)
{
   PQCUSB_POWER_REQ pSysPwr;
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)DeviceExtension;

   if (Irp->IoStatus.Status == STATUS_SUCCESS)
   {
      if (Irp->IoStatus.Information >= sizeof(QCUSB_POWER_REQ))
      {
         pSysPwr = (PQCUSB_POWER_REQ)(Irp->AssociatedIrp.SystemBuffer);
         switch (pSysPwr->Type)
         {
            case QCUSB_SYS_POWER_CURRENT:
            {
               pDevExt->SystemPower = pSysPwr->State;
               break;
            }
            case QCUSB_PREPARE_TO_STANDBY:
            {
               pDevExt->PrepareToPowerDown = (pSysPwr->State == 1);
               break;
            }
            default:
            {
               break;
            }
         }
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_DETAIL,
            ("<%s> _GetSystemPowerIrpComp: [%d] %d\n", pDevExt->PortName,
                pSysPwr->Type, pSysPwr->State)
         );
      }
      else
      {
         QCUSB_DbgPrint
         (
            QCUSB_DBG_MASK_POWER,
            QCUSB_DBG_LEVEL_ERROR,
            ("<%s> _GetSystemPowerIrpComp: buf too small\n", pDevExt->PortName)
         );
      }
   }
   else
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _GetSystemPowerIrpComp: failure 0x%x\n", pDevExt->PortName,
           Irp->IoStatus.Status)
      );
   }

   IoFreeIrp(Irp);
   return STATUS_MORE_PROCESSING_REQUIRED;
}  // USBPWR_GetSysPowerIrpCompletion

SYSTEM_POWER_STATE USBPWR_GetSystemPowerState
(
   PDEVICE_EXTENSION    pDevExt,
   QCUSB_POWER_REQ_TYPE Type
)
{
   NTSTATUS ntStatus;
   PIRP     pIrp;
   PIO_STACK_LOCATION nextstack;
   ULONG              ioBuffer = 0;
   PQCUSB_POWER_REQ   pwrReq;
   SYSTEM_POWER_STATE sysPwr = PowerSystemWorking;
   PDEVICE_OBJECT currStackTopDO;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pwrReq = (PQCUSB_POWER_REQ)&ioBuffer;

   QcAcquireSpinLock(&pDevExt->ExWdmSpinLock, &levelOrHandle);
   if (pDevExt->ExWdmDeviceObject == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _GetSystemPowerState: ExWdm not open\n", pDevExt->PortName)
      );
      QcReleaseSpinLock(&pDevExt->ExWdmSpinLock, levelOrHandle);
      return sysPwr;
   }
   currStackTopDO = IoGetAttachedDeviceReference(pDevExt->ExWdmDeviceObject);
   if (currStackTopDO == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _GetSystemPowerState: ExWdm ref failure\n", pDevExt->PortName)
      );
      QcReleaseSpinLock(&pDevExt->ExWdmSpinLock, levelOrHandle);
      return sysPwr;
   }
   QcReleaseSpinLock(&pDevExt->ExWdmSpinLock, levelOrHandle);

   pIrp = IoAllocateIrp(2, FALSE);
   if (pIrp == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _GetSystemPowerState: NO_MEM Irp\n", pDevExt->PortName)
      );
      ObDereferenceObject(currStackTopDO);
      return sysPwr;
   }

   pwrReq->Type = Type;
   pIrp->AssociatedIrp.SystemBuffer = &ioBuffer;
   nextstack = IoGetNextIrpStackLocation(pIrp);
   nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
   nextstack->Parameters.DeviceIoControl.IoControlCode = IOCTL_QCUSB_SYSTEM_POWER;
   // nextstack->Parameters.Read.Length = sizeof(QCUSB_POWER_REQ);
   nextstack->Parameters.DeviceIoControl.InputBufferLength = sizeof(QCUSB_POWER_REQ);
   nextstack->Parameters.DeviceIoControl.OutputBufferLength = sizeof(QCUSB_POWER_REQ);

   IoSetCompletionRoutine
   (
      pIrp,
      (PIO_COMPLETION_ROUTINE)USBPWR_GetSysPowerIrpCompletion,
      (PVOID)pDevExt,
      TRUE, TRUE, TRUE
   );

   ntStatus = IoCallDriver(pDevExt->ExWdmDeviceObject, pIrp);

   // IRP should be returned and freed after IoCallDriver returns

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> _GetSystemPowerState: sysPwr %d\n",
        pDevExt->PortName, pDevExt->SystemPower)
   );

   ObDereferenceObject(currStackTopDO);

   if (Type == QCUSB_SYS_POWER_CURRENT)
   {
      return pDevExt->SystemPower;
   }

   return pDevExt->PrepareToPowerDown;
}  // USBPWR_GetSystemPowerState

NTSTATUS USBPWR_ExWdmDevNotifyCompletion
(
   PDEVICE_OBJECT dummy,
   PIRP           Irp,
   PVOID          Context
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)Context;

   KeSetEvent(&pDevExt->DispatchXwdmNotifyEvent, IO_NO_INCREMENT, FALSE);

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // USBPWR_ExWdmDevNotifyCompletion

NTSTATUS USBPWR_RegisterXwdmNotification(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS ntStatus;
   PIRP     pIrp;
   PIO_STACK_LOCATION nextstack;
   static ULONG valueHolder = 0;

   pIrp = IoAllocateIrp(2, FALSE);
   if (pIrp == NULL)
   {
      QCUSB_DbgPrint
      (
         QCUSB_DBG_MASK_POWER,
         QCUSB_DBG_LEVEL_ERROR,
         ("<%s> _RegisterNotification: NO_MEM Irp\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   pIrp->AssociatedIrp.SystemBuffer = (PVOID)&valueHolder;
   nextstack = IoGetNextIrpStackLocation(pIrp);
   nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
   nextstack->Parameters.DeviceIoControl.IoControlCode = IOCTL_QCUSB_QCDEV_NOTIFY;
   nextstack->Parameters.Read.Length = sizeof(ULONG);

   IoSetCompletionRoutine
   (
      pIrp,
      (PIO_COMPLETION_ROUTINE)USBPWR_ExWdmDevNotifyCompletion,
      (PVOID)pDevExt,
      TRUE, TRUE, TRUE
   );

   pDevExt->XwdmNotificationIrp = pIrp;

   ntStatus = IoCallDriver(pDevExt->ExWdmDeviceObject, pIrp);

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> _RegisterNotification: ST 0x%x\n", pDevExt->PortName, ntStatus)
   );

   return ntStatus;
}  // USBPWR_RegisterXwdmNotification

VOID USBPWR_CancelXwdmNotification(PDEVICE_EXTENSION pDevExt)
{
   PIRP irp;
   #ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCUSB_DbgPrint
   (
      QCUSB_DBG_MASK_POWER,
      QCUSB_DBG_LEVEL_DETAIL,
      ("<%s> _CancelXwdmNotification: 0x%p\n",
        pDevExt->PortName, pDevExt->XwdmNotificationIrp)
   );

   if (pDevExt->XwdmNotificationIrp != NULL)
   {
      IoCancelIrp(pDevExt->XwdmNotificationIrp);
   }
}  // USBPWR_CancelXwdmNotification

NTSTATUS USBPWR_HandleExWdmQueryRemove(PDEVICE_EXTENSION pDevExt)
{
   if (pDevExt->ExFileObject != NULL)
   {
      ObDereferenceObject(pDevExt->ExFileObject);
      pDevExt->ExFileObject = NULL;
   }

   return STATUS_SUCCESS;
}  // USBPWR_HandleExWdmQueryRemove

NTSTATUS USBPWR_HandleExWdmReopen(PDEVICE_EXTENSION pDevExt)
{
   // the ExFileObject is gone, so unregister
   if (pDevExt->ExWdmDevNotificationEntry != NULL)
   {
      IoUnregisterPlugPlayNotification(pDevExt->ExWdmDevNotificationEntry);
      pDevExt->ExWdmDevNotificationEntry = NULL;
   }
   return USBPWR_OpenExWdmDevice(pDevExt);
}  // USBPWR_HandleExWdmReopen
