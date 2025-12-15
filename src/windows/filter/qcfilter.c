/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q C F I L T E R . C

GENERAL DESCRIPTION

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#include <stdio.h>
#include <stdlib.h>
////////////////////////////////////////////////////////////
// WDK headers defined here
//
#include "Ntddk.h"
#include "Usbdi.h"
#include "Usbdlib.h"
#include "Usbioctl.h"
#include "Wdm.h"
#include <Ntstrsafe.h>
#include <string.h>

#include "qcfilter.h"
#include "qcfilterioc.h"

#ifdef EVENT_TRACING
#define WPP_GLOBALLOGGER
#include "qcfilter.tmh"      //  this is the file that will be auto generated
#endif

KEVENT ControlLock;
ULONG InstanceCount = (ULONG)0;

// To store the device name
char gDeviceName[255];
ULONG gDeviceIndex;
ULONG gMuxDeviceIndex = (ULONG)NUM_START_INTERFACEID;

LIST_ENTRY Control_DeviceList;
KSPIN_LOCK Control_DeviceLock;

/****************************************************************************
 *
 * function: DriverEntry
 *
 * purpose:  Installable driver initialization entry point, called directly
 *           by the I/O system.
 *
 * arguments:DriverObject = pointer to the driver object
 *           RegistryPath = pointer to a unicode string representing the path
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
DriverEntry ( PDRIVER_OBJECT  driverObject, PUNICODE_STRING registryPath )
{
   NTSTATUS            status = STATUS_SUCCESS;
   ULONG               ulIndex;
   PDRIVER_DISPATCH  * dispatch;
   ULONG i;
   ANSI_STRING asDevName;
   char *p;

#ifdef EVENT_TRACING
   //
   // include this to support WPP.
   //

   // This macro is required to initialize software tracing. 
   WPP_INIT_TRACING(driverObject,registryPath);
#endif   

   DbgPrint("Entered the Driver Entry\n");

   //call this to make sure NonPagedPoolNx is passed to ExAllocatePool in Win10 and above
   ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

   // Store the device name
   status = RtlUnicodeStringToAnsiString
              (
                 &asDevName,
                 registryPath,
                 TRUE
              );

   if ( !NT_SUCCESS( status ) )
   {
      DbgPrint("qcfilter: Failure at DriverEntry.\n");
#ifdef EVENT_TRACING
      WPP_CLEANUP(driverObject);
#endif
      return STATUS_UNSUCCESSFUL;
   }

   p = asDevName.Buffer + asDevName.Length - 1;
   while ( p > asDevName.Buffer )
   {
      if (*p == '\\')
      {
         p++;
         break;
      }
      p--;
   }

   RtlZeroMemory(gDeviceName, 255);
   if ((strlen(p) == 0) || (strlen(p) >= 255)) // invalid name
   {
      strcpy(gDeviceName, (char *)"filter_unknown");
   }
   strcpy(gDeviceName, p);

   // Free the allocated string
   RtlFreeAnsiString(&asDevName);

   //
   // Create dispatch points
   //
   for (ulIndex = 0, dispatch = driverObject->MajorFunction;
      ulIndex <= IRP_MJ_MAXIMUM_FUNCTION;
      ulIndex++, dispatch++) 
   {

     *dispatch = QCFilterPass;
   }

   driverObject->MajorFunction[IRP_MJ_PNP]            = QCFilterDispatchPnp;
   driverObject->MajorFunction[IRP_MJ_POWER]          = QCFilterDispatchPower;
   driverObject->DriverExtension->AddDevice           = QCFilterAddDevice;
   driverObject->DriverUnload                         = QCFilterUnload;

   driverObject->MajorFunction[IRP_MJ_CREATE]         = 
   driverObject->MajorFunction[IRP_MJ_CLOSE]          = 
   driverObject->MajorFunction[IRP_MJ_CLEANUP]        = 
   driverObject->MajorFunction[IRP_MJ_READ]           = 
   driverObject->MajorFunction[IRP_MJ_WRITE]          = 
   driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = 
   driverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = 
                                                     QCFilterDispatchIo;
   //
   // ControlLock is to synchronize multiple threads creating & deleting 
   // control deviceobjects. 
   //
   KeInitializeEvent(&ControlLock, SynchronizationEvent, TRUE);

   KeInitializeSpinLock(&Control_DeviceLock);
   InitializeListHead(&Control_DeviceList);

   return status;
}

/****************************************************************************
 *
 * function: QCFilterAddDevice
 *
 * purpose:  Installable device entry point, called directly
 *           by the I/O system.
 *
 * arguments:DriverObject = pointer to the driver object
 *           PhysicalDeviceObject = pointer to a PDO created by the bus driver
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
QCFilterAddDevice( 
   PDRIVER_OBJECT QCDriverObject, 
   PDEVICE_OBJECT QCPhysicalDeviceObject 
)
{
   PDEVICE_OBJECT          QCdeviceObject = NULL;
   NTSTATUS                NTstatus = STATUS_SUCCESS;
   PDEVICE_EXTENSION       pDevExt, QCdevExt;

   QCdeviceObject = IoGetAttachedDeviceReference(QCPhysicalDeviceObject);
   ObDereferenceObject(QCdeviceObject);

   NTstatus = IoCreateDevice
              (
                 QCDriverObject, 
                 sizeof(DEVICE_EXTENSION), 
                 NULL, 
                 QCdeviceObject->DeviceType,
                 FILE_DEVICE_SECURE_OPEN, 
                 FALSE, 
                 &QCdeviceObject
              );
   if (NTstatus != STATUS_SUCCESS) 
   {
      /* Return if IoCreateDevice fails */
      return NTstatus;
   }

   // Get the debugmask here
   NTstatus = QCFLT_VendorRegistryProcess(QCDriverObject, QCPhysicalDeviceObject, QCdeviceObject);
   if (NTstatus != STATUS_SUCCESS) 
   {
      DbgPrint("<%s> AddDevice: reg access failure.\n", gDeviceName);
      IoDeleteDevice(QCdeviceObject);
      return STATUS_UNSUCCESSFUL;
   }
   
   pDevExt = QCdevExt = (PDEVICE_EXTENSION) QCdeviceObject->DeviceExtension;

   QCFLT_DbgPrint
   (
    DBG_LEVEL_DETAIL,
    ("AddDevice PDO (0x%p) FDO (0x%p)\n", QCPhysicalDeviceObject, QCdeviceObject)
   );

   QCdevExt->Type = DEVICE_TYPE_QCFIDO;
   QCdevExt->NextLowerDriver = IoAttachDeviceToDeviceStack( QCdeviceObject, QCPhysicalDeviceObject);
   if (QCdevExt->NextLowerDriver == NULL) 
   {
      IoDeleteDevice(QCdeviceObject);
      return STATUS_UNSUCCESSFUL;
   }

   QCdeviceObject->Flags |= QCdevExt->NextLowerDriver->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO |
                          DO_POWER_PAGABLE );
   QCdeviceObject->DeviceType = QCdevExt->NextLowerDriver->DeviceType;
   QCdeviceObject->Characteristics = QCdevExt->NextLowerDriver->Characteristics;
   QCdevExt->Self = QCdeviceObject;
   QCdevExt->QCPhysicalDeviceObject = QCPhysicalDeviceObject;

   QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterAddDevice : Initialize RemoveLock\n"));
   IoInitializeRemoveLock( &QCdevExt->RemoveLock , POOL_TAG, 1, 100); 
   NTstatus = QCFilter_InitializeDeviceExt( QCdeviceObject );
   if (NTstatus != STATUS_SUCCESS)
   {
      IoDeleteDevice(QCdeviceObject);
      return STATUS_UNSUCCESSFUL;
   }
   NTstatus = QCFilter_StartFilterThread( QCdeviceObject );
   if (NTstatus != STATUS_SUCCESS)
   {
      IoDeleteDevice(QCdeviceObject);
      return STATUS_UNSUCCESSFUL;
   }

   QC_INIT_PNP_STATE(QCdevExt);

   pDevExt->DebugLevel = DBG_LEVEL_VERBOSE;
   QCFLT_DbgPrint
   (
    DBG_LEVEL_DETAIL,
    ("<%s> AddDevice: %p to %p->%p \n", QCdevExt->PortName, QCdeviceObject, QCdevExt->NextLowerDriver, QCPhysicalDeviceObject)
   );

   QCdeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

   QCFilterCreateFriendlyName(QCdevExt, QCPhysicalDeviceObject);

   return STATUS_SUCCESS;
}

/****************************************************************************
 *
 * function: QCFilterPass
 *
 * purpose:  The default dispatch routine, passes the IRPs to the
 *           lower driver.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *           Irp = pointer to the dispatch IO request packet
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
QCFilterPass ( PDEVICE_OBJECT DeviceObject, PIRP Irp )

{
   PDEVICE_EXTENSION           deviceExtension;
   NTSTATUS    status;

   deviceExtension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;
   if (deviceExtension->Type == DEVICE_TYPE_QCFIDO) 
   {

      // DbgPrint("QCFilterPass : Acquire RemoveLock\n");
      status = IoAcquireRemoveLock( &deviceExtension->RemoveLock, Irp );
      if (!NT_SUCCESS (status)) 
      {
         Irp->IoStatus.Status = status;
         IoCompleteRequest (Irp, IO_NO_INCREMENT);
         return status;
      }
      
      IoSkipCurrentIrpStackLocation (Irp);
      status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
      // DbgPrint("QCFilterPass : Release RemoveLock\n");
      IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
      return status;
   }
   else
   {
      status = STATUS_NOT_SUPPORTED;
      Irp->IoStatus.Status = status;
      IoCompleteRequest (Irp, IO_NO_INCREMENT);
      return status;
   }
}

/****************************************************************************
 *
 * function: QCFilterDispatchPnp
 *
 * purpose:  The plug and play dispatch routine, called directly
 *           by the PnP manager.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *           Irp = pointer to the PnP IO request packet
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
QCFilterDispatchPnp( PDEVICE_OBJECT DeviceObject, PIRP Irp )
{
   PDEVICE_EXTENSION deviceExtension, pDevExt;
   PIO_STACK_LOCATION irpStack;
   NTSTATUS status;
   KEVENT event;

   pDevExt = deviceExtension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;
   if (deviceExtension->Type == DEVICE_TYPE_QCFIDO) 
   {
      irpStack = IoGetCurrentIrpStackLocation(Irp);
      QCFLT_DbgPrint
      (
         DBG_LEVEL_DETAIL,
         ("<%s> FilterDO PNP MINOR 0x%x IRP:0x%p \n", deviceExtension->PortName, irpStack->MinorFunction, Irp)
      );

      // DbgPrint("QCFilterDispatchPnp : Acquire RemoveLock\n");
      status = IoAcquireRemoveLock(&deviceExtension->RemoveLock, Irp);
      if (!NT_SUCCESS (status)) 
      {
         Irp->IoStatus.Status = status;
         IoCompleteRequest (Irp, IO_NO_INCREMENT);
         return status;
      }

      switch (irpStack->MinorFunction) 
      {
         case IRP_MN_START_DEVICE:
         {
            DbgPrint("IRP_MN_START_DEVICE : called\n");
            KeInitializeEvent(&event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp,
                                  (PIO_COMPLETION_ROUTINE) QCFilterStartCompletionRoutine,
                                  &event,
                                  TRUE,
                                  TRUE,
                                  TRUE);

            DbgPrint("IRP_MN_START_DEVICE : IoCallDriver called\n");
            status = IoCallDriver(deviceExtension->NextLowerDriver, Irp);
            KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);          
            DbgPrint("IRP_MN_START_DEVICE : IoCallDriver returned\n");

            if (NT_SUCCESS (status))
            {
               QC_SET_NEW_PNP_STATE(deviceExtension, QC_STARTED);
               if( deviceExtension->NextLowerDriver->Characteristics & FILE_REMOVABLE_MEDIA ) 
               {
                  DeviceObject->Characteristics |= FILE_REMOVABLE_MEDIA;
               }
               Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            IoCompleteRequest (Irp, IO_NO_INCREMENT);
            // DbgPrint("QCFilterDispatchPnp : Release RemoveLock\n");
            IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
            return status;
         }   
         case IRP_MN_REMOVE_DEVICE:
         {
            //
            // Wait for all outstanding requests to complete
            //
            QCFLT_DbgPrint
            (
               DBG_LEVEL_DETAIL,
               ("<%s> Waiting for outstanding requests\n", deviceExtension->PortName)
            );

            QCFilter_CancelFilterThread(DeviceObject);

            QCFLT_DbgPrint
            (
               DBG_LEVEL_DETAIL,
               ("<%s> Before calling QCFilterDeleteControlObject\n", deviceExtension->PortName)
            );
			

            QCFilterDeleteControlObject(DeviceObject, NULL);

            QCFLT_DbgPrint
            (
               DBG_LEVEL_DETAIL,
               ("<%s> After calling QCFilterDeleteControlObject\n", deviceExtension->PortName)
            );

            // DbgPrint("QCFilterDispatchPnp : Release RemoveLock and wait\n");

            IoReleaseRemoveLockAndWait(&deviceExtension->RemoveLock, Irp);

            IoSkipCurrentIrpStackLocation(Irp);

            status = IoCallDriver(deviceExtension->NextLowerDriver, Irp);

            QC_SET_NEW_PNP_STATE(deviceExtension, QC_DELETED);

            IoDetachDevice(deviceExtension->NextLowerDriver);
            IoDeleteDevice(DeviceObject);
            return status;
         }
         
         case IRP_MN_QUERY_STOP_DEVICE:
         {
            QC_SET_NEW_PNP_STATE(deviceExtension, QC_STOP_PENDING);
            status = STATUS_SUCCESS;
            break;
         }
         
         case IRP_MN_CANCEL_STOP_DEVICE:
         {
            if (QC_STOP_PENDING == deviceExtension->DevicePnPState)
            {
               QC_SET_PREV_PNP_STATE(deviceExtension);
            }
            status = STATUS_SUCCESS; // We must not fail this IRP.
            break;
         }
         case IRP_MN_STOP_DEVICE:
         {
            QC_SET_NEW_PNP_STATE(deviceExtension, QC_STOPPED);
            status = STATUS_SUCCESS;
            break;
         }
         case IRP_MN_QUERY_REMOVE_DEVICE:
         {
            QC_SET_NEW_PNP_STATE(deviceExtension, QC_REMOVE_PENDING);
            status = STATUS_SUCCESS;
            break;
         }
         case IRP_MN_SURPRISE_REMOVAL:
         {
            QC_SET_NEW_PNP_STATE(deviceExtension, QC_SURPRISE_REMOVE_PENDING);
            status = STATUS_SUCCESS;
            break;
         }
         case IRP_MN_CANCEL_REMOVE_DEVICE:
         {
            if (QC_REMOVE_PENDING == deviceExtension->DevicePnPState)
            {
               QC_SET_PREV_PNP_STATE(deviceExtension);
            }

            status = STATUS_SUCCESS; // We must not fail this IRP.
            break;
         }
         case IRP_MN_DEVICE_USAGE_NOTIFICATION:
         {
            if ((DeviceObject->AttachedDevice == NULL) ||
               (DeviceObject->AttachedDevice->Flags & DO_POWER_PAGABLE)) 
            {
               DeviceObject->Flags |= DO_POWER_PAGABLE;
            }

            IoCopyCurrentIrpStackLocationToNext(Irp);

            IoSetCompletionRoutine(
               Irp,
               QCFilterDeviceUsageNotificationCompletionRoutine,
               NULL,
               TRUE,
               TRUE,
               TRUE
               );

            return IoCallDriver(deviceExtension->NextLowerDriver, Irp);
         }
         default:
         {
            status = Irp->IoStatus.Status;
            break;
         }
      }
      //
      // Pass the IRP down and forget it.
      //
      Irp->IoStatus.Status = status;
      IoSkipCurrentIrpStackLocation (Irp);
      status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
      // DbgPrint("QCFilterDispatchPnp : Release RemoveLock\n");
      IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
      return status;
   }
   else
   {
      status = STATUS_NOT_SUPPORTED;
      Irp->IoStatus.Status = status;
      IoCompleteRequest (Irp, IO_NO_INCREMENT);
      return status;
   }    
}

/****************************************************************************
 *
 * function: QCFilterStartCompletionRoutine
 *
 * purpose:  A completion routine for use when calling the lower device objects to
 *           which our filter deviceobject is attached.
 *
 * arguments: DeviceObject = pointer to the deviceObject.
 *            Irp = pointer to the dispatch IO request packet
 *            Context = NULL
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
QCFilterStartCompletionRoutine( PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context )
{
   PKEVENT             event = (PKEVENT)Context;
   UNREFERENCED_PARAMETER (DeviceObject);

   DbgPrint("QCFilterStartCompletionRoutine : Called\n");

   if(Irp->PendingReturned)
   {
      IoMarkIrpPending(Irp);
   }

      KeSetEvent (event, IO_NO_INCREMENT, FALSE);

   //
   // The dispatch routine will have to call IoCompleteRequest
   //
   return STATUS_MORE_PROCESSING_REQUIRED;
}


/****************************************************************************
 *
 * function: QCFilterDeviceUsageNotificationCompletionRoutine
 *
 * purpose:  A completion routine for use when calling the lower device objects to
 *           which our filter deviceobject is attached.
 *
 * arguments: DeviceObject = pointer to the deviceObject.
 *            Irp = pointer to the dispatch IO request packet
 *            Context = NULL
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
QCFilterDeviceUsageNotificationCompletionRoutine( PDEVICE_OBJECT   DeviceObject, PIRP Irp, PVOID Context )
{
   PDEVICE_EXTENSION       deviceExtension;

   UNREFERENCED_PARAMETER(Context);

   deviceExtension = (PDEVICE_EXTENSION) DeviceObject->DeviceExtension;

   if (Irp->PendingReturned) 
   {
      IoMarkIrpPending(Irp);
   }

   if (!(deviceExtension->NextLowerDriver->Flags & DO_POWER_PAGABLE)) 
   {
      DeviceObject->Flags &= ~DO_POWER_PAGABLE;
   }
   // DbgPrint("QCFilterDeviceUsageNotificationCompletionRoutine : Release RemoveLock\n");
   IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
   return STATUS_CONTINUE_COMPLETION;
}

/****************************************************************************
 *
 * function: QCFilterDispatchPower
 *
 * purpose:  The Power IRPs dispatch routine, called directly
 *           by the power manager.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *           Irp = pointer to the Power IO request packet
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
QCFilterDispatchPower( PDEVICE_OBJECT QCDeviceObject, 
                           PIRP Irp )
{
   NTSTATUS    NTstatus;
   PDEVICE_EXTENSION   QCdeviceExtension;

   QCdeviceExtension = (PDEVICE_EXTENSION) QCDeviceObject->DeviceExtension;
   if (QCdeviceExtension->Type != DEVICE_TYPE_QCFIDO) 
   {
       NTstatus = STATUS_NOT_SUPPORTED;
       Irp->IoStatus.Status = NTstatus;
       IoCompleteRequest (Irp, IO_NO_INCREMENT);
       return NTstatus;
   }
   else
   {
      // DbgPrint("QCFilterDispatchPower : Acquire RemoveLock\n");
      NTstatus = IoAcquireRemoveLock (&QCdeviceExtension->RemoveLock, Irp);
      if (NTstatus != STATUS_SUCCESS) 
      { 
         Irp->IoStatus.Status = NTstatus;
         PoStartNextPowerIrp(Irp);
         IoCompleteRequest (Irp, IO_NO_INCREMENT);
         return NTstatus;
      }
      PoStartNextPowerIrp(Irp);
      IoSkipCurrentIrpStackLocation(Irp);
      NTstatus = PoCallDriver(QCdeviceExtension->NextLowerDriver, Irp);
      // DbgPrint("QCFilterDispatchPower : Release RemoveLock\n");
      IoReleaseRemoveLock(&QCdeviceExtension->RemoveLock, Irp); 
      return NTstatus;
   }    
}

/*********************************************************************
 *
 * function:   QCFilterUnload
 *
 * purpose:    Free all the allocated resources, and unload driver
 *
 * arguments:  DriverObject = adr(driver object)
 *
 * returns:    none
 *
 *********************************************************************/
VOID
QCFilterUnload( PDRIVER_OBJECT DriverObject )

{
   if (DriverObject->DeviceObject != NULL) 
   {
   }

   // Clean up the control device object queue
   //QCFLT_FreeControlDeviceList();

   //
   // We should not be unloaded until all the devices we control
   // have been removed from our queue.
   //
   DbgPrint("Unload\n");

#ifdef EVENT_TRACING
   WPP_CLEANUP(DriverObject);
#endif
   
   return;
}

/****************************************************************************
 *
 * function: QCFilterCreateControlObject
 *
 * purpose:  Creates the control device object.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *           pFilterDeviceInfo = pointer to the control device object information
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
QCFilterCreateControlObject( PDEVICE_OBJECT    DeviceObject, PFILTER_DEVICE_INFO pFilterDeviceInfo )
{
   UNICODE_STRING ntDeviceName;
   UNICODE_STRING symbolicLinkName;
   PCONTROL_DEVICE_EXTENSION deviceExtension;
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS status = STATUS_UNSUCCESSFUL;

   pDevExt = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
   //
   // IoCreateDeviceSecure & IoCreateSymbolicLink must be called at
   // PASSIVE_LEVEL.
   // Hence we use an event and not a fast mutex.
   //
   KeEnterCriticalRegion();
   KeWaitForSingleObject(&ControlLock, Executive, KernelMode, FALSE, NULL);

   QCFLT_DbgPrint
   (
	  DBG_LEVEL_TRACE,
	  ("<%s> D: QCFilterCreateControlObject : After entering the critical Section\n", pDevExt->PortName)
   );

   //
   // Initialize a security descriptor string. 
   //
   //RtlInitUnicodeString( &sddlString, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");

   status = IoCreateDeviceSecure(DeviceObject->DriverObject,
                          sizeof(CONTROL_DEVICE_EXTENSION),
                          pFilterDeviceInfo->pDeviceName,
                          FILE_DEVICE_UNKNOWN,
                          FILE_DEVICE_SECURE_OPEN,
                          FALSE, 
                          &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX,
                          (LPCGUID)&QMI_DEVICE_INTERFACE_CLASS,
                          &(pFilterDeviceInfo->pControlDeviceObject));
   if (NT_SUCCESS( status )) 
   {
      pFilterDeviceInfo->pControlDeviceObject->Flags |= DO_BUFFERED_IO;
      status = IoCreateSymbolicLink( pFilterDeviceInfo->pDeviceLinkName, pFilterDeviceInfo->pDeviceName );
      if ( !NT_SUCCESS( status )) 
      {
         IoDeleteDevice(pFilterDeviceInfo->pControlDeviceObject);
         QCFLT_DbgPrint
         (
            DBG_LEVEL_DETAIL,
            ("IoCreateSymbolicLink failed %x\n", status)
         );
         goto End;
      }
      pFilterDeviceInfo->pFilterDeviceObject = DeviceObject;
      deviceExtension = pFilterDeviceInfo->pControlDeviceObject->DeviceExtension;
      deviceExtension->Type = DEVICE_TYPE_QCCDO;
      deviceExtension->Deleted = FALSE;
      deviceExtension->InService = FALSE;
      deviceExtension->DeviceOpenCount = 0;
      deviceExtension->Self = pFilterDeviceInfo->pControlDeviceObject;
      deviceExtension->DriverObject = DeviceObject->DriverObject;
      deviceExtension->FidoDeviceObject = DeviceObject;
      strcpy(deviceExtension->PortName, pDevExt->PortName);
      deviceExtension->DebugMask = pDevExt->DebugMask;
      deviceExtension->DebugLevel = pDevExt->DebugLevel; 
      pDevExt->ControlDeviceObject = pFilterDeviceInfo->pControlDeviceObject;
      pDevExt->pAdapterContext = pFilterDeviceInfo->pAdapter;

      QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterCreateControlObject : Initialize RemoveLock\n"));                          
      IoInitializeRemoveLock(&deviceExtension->RmLock, 0, 0, 0);
      IoAcquireRemoveLock(&deviceExtension->RmLock, NULL);
         
      pFilterDeviceInfo->pControlDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

      //Add the filterdeviceinfo to the global list
      QCFLT_AddControlDevice(pFilterDeviceInfo);
	  QCFLT_DbgPrint
	  (
		 DBG_LEVEL_TRACE,
		 ("<%s> D: QCFilterCreateControlObject : Added Control Device \n", pDevExt->PortName)
	  );
	  
   }
   else 
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_DETAIL,
         ("IoCreateDevice failed %x\n", status)
      );
   }
   
End:
    
   KeSetEvent(&ControlLock, IO_NO_INCREMENT, FALSE);
   KeLeaveCriticalRegion();
   QCFLT_DbgPrint
   (
	  DBG_LEVEL_TRACE,
	  ("<%s> D: QCFilterCreateControlObject : Leaving the critical Section\n", pDevExt->PortName)
   );
   
   return status;
}

/****************************************************************************
 *
 * function: QCFilterDeleteControlObject
 *
 * purpose:  Deletes the control device object.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *           pFilterDeviceInfo = pointer to the control device object information
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
QCFilterDeleteControlObject( PDEVICE_OBJECT    DeviceObject, PFILTER_DEVICE_INFO pFilterDeviceInfo)
{
   UNICODE_STRING symbolicLinkName;
   PDEVICE_OBJECT pControlDeviceObject;
   NTSTATUS status = STATUS_SUCCESS;

   if (pFilterDeviceInfo != NULL && pFilterDeviceInfo->pControlDeviceObject != NULL)
   {
      pControlDeviceObject = pFilterDeviceInfo->pControlDeviceObject;
   }
   else
   {
      pControlDeviceObject = QCFLT_FindControlDeviceFido(DeviceObject);
   }

   KeEnterCriticalRegion();
   KeWaitForSingleObject(&ControlLock, Executive, KernelMode, FALSE, NULL);

   //QCFLT_DbgPrint
   //(
   //	  DBG_LEVEL_TRACE,
   //	  ("D: QCFilterDeleteControlObject : After entering the critical Section\n")
   //  );

   //
   // If this is the last instance of the device then delete the controlobject
   // and symbolic link to enable the pnp manager to unload the driver.
   //

   if (pControlDeviceObject != NULL)
   {
      QCFLT_DeleteControlDevice(pControlDeviceObject);
	  //QCFLT_DbgPrint
	  //(
		// DBG_LEVEL_TRACE,
		 //("D: QCFilterDeleteControlObject : Deleted the Control Device\n")
	  //);
	  
   }

   KeSetEvent(&ControlLock, IO_NO_INCREMENT, FALSE);
   KeLeaveCriticalRegion();
   //QCFLT_DbgPrint
   //(
	 // DBG_LEVEL_TRACE,
	  //("D: QCFilterDeleteControlObject : leaving the critical Section\n")
   //);
   
   return status;
}

/****************************************************************************
 *
 * function: DispatchCancelQueued
 *
 * purpose:  Cancels the dispath IRP.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *           Irp = pointer to dispatch Irp
 *
 * returns:  void
 *
 ****************************************************************************/
VOID DispatchCancelQueued(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
   KIRQL irql = KeGetCurrentIrql();
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
   BOOLEAN bIrpInQueue;
   KIRQL levelOrHandle;

   QCFLT_DbgPrint
   (
      DBG_LEVEL_DETAIL,
      ("<%s> DSP CQed: 0x%p\n", pDevExt->PortName, Irp)
   );
   IoReleaseCancelSpinLock(Irp->CancelIrql);

   // De-queue
   QcAcquireSpinLock(&pDevExt->FilterSpinLock, &levelOrHandle);
   bIrpInQueue = QCFLT_IsIrpInQueue
                 (
                    pDevExt,
                    &pDevExt->DispatchQueue,
                    Irp
                 );

   if (bIrpInQueue == TRUE)
   {
      RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
      Irp->IoStatus.Status = STATUS_CANCELLED;
      Irp->IoStatus.Information = 0;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
   }
   else
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> DSP_Cxl: no action to active 0x%p\n", pDevExt->PortName, Irp)
      );
      IoSetCancelRoutine(Irp, DispatchCancelQueued);
   }

   QcReleaseSpinLock(&pDevExt->FilterSpinLock, levelOrHandle);
}  // DispatchCancelQueued

NTSTATUS QCFLT_CallUSBD_Completion
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
NTSTATUS QCFLT_CallUSBD
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

   IrqLevel = KeGetCurrentIrql();

   if (IrqLevel > PASSIVE_LEVEL)
   {
      Urb->UrbHeader.Status = USBD_STATUS_CANCELED;
      return STATUS_CANCELLED;
   }

   pDevExt = DeviceObject->DeviceExtension;

   // issue a synchronous request to read the UTB 
   KeInitializeEvent(&event, NotificationEvent, FALSE);

   irp = IoBuildDeviceIoControlRequest
         (
            IOCTL_INTERNAL_USB_SUBMIT_URB,
            pDevExt->NextLowerDriver,
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

   // set completion routine to regain control of the irp
   IoSetCompletionRoutine
   (
      irp, QCFLT_CallUSBD_Completion, (PVOID)&event,
      TRUE, TRUE, TRUE
   );

   ntStatus = IoCallDriver( pDevExt->NextLowerDriver, irp );

   if (ntStatus == STATUS_PENDING) 
   {
      delayValue.QuadPart = -(20 * 1000 * 1000);   // 2.0 sec  
      ntStatus = KeWaitForSingleObject
                 (
                    &event,
                    Executive,
                    KernelMode,
                    FALSE,
                    &delayValue // HGUO
                 );


      if (ntStatus == STATUS_TIMEOUT) 
      {
         QCFLT_DbgPrint
         (
            DBG_LEVEL_ERROR,
            ("<%s> QCFLT_CallUSD timeout on control req - 1\n", pDevExt->PortName)
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

         Urb->UrbHeader.Status = USBD_STATUS_REQUEST_FAILED;  // HGUO
      }
   }
   else // success or failrue
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> QCFLT_CallUSD - IoCallDriver/0x%p 0x%x IRP 0x%x\n",
           pDevExt->PortName, pDevExt->NextLowerDriver, ntStatus,
           irp->IoStatus.Status)
      );
      delayValue.QuadPart = -(10 * 1000 * 1000);   // 1.0 sec
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
         QCFLT_DbgPrint
         (
            DBG_LEVEL_CRITICAL,
            ("<%s> QCFLT_CallUSD ERR: 2nd wait timeout\n", pDevExt->PortName)
         );
      }
   }

   IoCompleteRequest(irp, IO_NO_INCREMENT);

ExitCallUSBD:

   if (ntStatus == STATUS_TIMEOUT)
   {
      ntStatus = STATUS_UNSUCCESSFUL;
   }

   return ntStatus;

}  // QCUSB_CallUSBD

NTSTATUS QCFLT_GetStringDescriptor
(
   PDEVICE_OBJECT DeviceObject,
   UCHAR          Index,
   USHORT         LanguageId,
   BOOLEAN        MatchPrefix
)
{
   PDEVICE_EXTENSION pDevExt;
   PUSB_STRING_DESCRIPTOR pSerNum;
   URB urb;
   NTSTATUS ntStatus;
   PCHAR pSerLoc = NULL;
   UNICODE_STRING ucValueName;
   HANDLE         hRegKey;
   INT            strLen;
   BOOLEAN        bSetEntry = FALSE;

   pDevExt = DeviceObject->DeviceExtension;

   QCFLT_DbgPrint
   (
      DBG_LEVEL_DETAIL,
      ("<%s> -->QCFLT_GetStringDescriptor : GetStringDescriptor DO 0x%p idx %d\n", pDevExt->PortName, DeviceObject, Index)
   );

   if (Index == 0)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_DETAIL,
         ("<%s> <--QCFLT_GetStringDescriptor: GetStringDescriptor: index is NULL\n", pDevExt->PortName)
      );
      goto UpdateRegistry;
   }

   pSerNum = (PUSB_STRING_DESCRIPTOR)(pDevExt->DevSerialNumber);
   RtlZeroMemory(pDevExt->DevSerialNumber, 256);

   UsbBuildGetDescriptorRequest
   (
      &urb,
      (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
      USB_STRING_DESCRIPTOR_TYPE,
      Index,
      LanguageId,
      pSerNum,
      NULL,
      sizeof(USB_STRING_DESCRIPTOR_TYPE),
      NULL
   );

   ntStatus = QCFLT_CallUSBD(DeviceObject, &urb);
   if (!NT_SUCCESS(ntStatus))
   {
      goto UpdateRegistry;
   }

   UsbBuildGetDescriptorRequest
   (
      &urb,
      (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
      USB_STRING_DESCRIPTOR_TYPE,
      Index,
      LanguageId,
      pSerNum,
      NULL,
      pSerNum->bLength,
      NULL
   );

   ntStatus = QCFLT_CallUSBD(DeviceObject, &urb);
   if (!NT_SUCCESS(ntStatus))
   {
      RtlZeroMemory(pDevExt->DevSerialNumber, 256);
   }
   else
   {
      QCFLT_PrintBytes
      (
         (PVOID)(pDevExt->DevSerialNumber),
         pSerNum->bLength,
         256,
         "SerialNumber",
         pDevExt,
         DBG_LEVEL_DETAIL
      );
   }

   if (!NT_SUCCESS(ntStatus))
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_DETAIL,
         ("<%s> QCFLT_GetStringDescriptor : DO 0x%p failure NTS 0x%x\n", pDevExt->PortName,
           DeviceObject, ntStatus)
      );
      goto UpdateRegistry;
   }
   else
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_DETAIL,
         ("<%s> QCFLT_GetStringDescriptor : DO 0x%p NTS 0x%x (%dB)\n", pDevExt->PortName,
           DeviceObject, ntStatus, pSerNum->bLength)
      );
   }

   if (pSerNum->bLength > 2)
   {
      strLen = (INT)pSerNum->bLength -2;  // exclude 2 bytes in header
   }
   else
   {
      strLen = 0;
   }

   pSerLoc = (PCHAR)pSerNum->bString;
   bSetEntry = TRUE;

   // search for "_SN:"
   if ((MatchPrefix == TRUE) && (strLen > 0))
   {
      INT idx, adjusted = 0;
      PCHAR p = pSerLoc;
      BOOLEAN bMatchFound = FALSE;

      for (idx = 0; idx < strLen; idx++)
      {
         if ((*p     == '_') && (*(p+1) == 0) &&
             (*(p+2) == 'S') && (*(p+3) == 0) &&
             (*(p+4) == 'N') && (*(p+5) == 0) &&
             (*(p+6) == ':') && (*(p+7) == 0))
         {
            pSerLoc = p + 8;
            adjusted += 8;
            bMatchFound = TRUE;
            break;
         }
         p++;
         adjusted++;
      }

      // Adjust length
      if (bMatchFound == TRUE)
      {
         INT tmpLen = strLen;

         tmpLen -= adjusted;
         p = pSerLoc;
         while (tmpLen > 0)
         {
            if (((*p == ' ') && (*(p+1) == 0)) ||  // space
                ((*p == '_') && (*(p+1) == 0)))    // or _ for another field
            {
               break;
         }
            else
            {
               p += 2;       // advance 1 unicode byte
               tmpLen -= 2;  // remaining string length
            }
         }
         strLen = (USHORT)(p - pSerLoc); // 18;
      }
      else
      {
         QCFLT_DbgPrint
         (
            DBG_LEVEL_TRACE,
            ("<%s> <--QCFLT_GetDeviceSerialNumber: no SN found\n", pDevExt->PortName)
         );
         ntStatus = STATUS_UNSUCCESSFUL;
         bSetEntry = FALSE;
      }
   }

   QCFLT_DbgPrint
   (
      DBG_LEVEL_TRACE,
      ("<%s> QCFLT_GetDeviceSerialNumber: strLen %d\n", pDevExt->PortName, strLen)
   );

UpdateRegistry:

   // update registry
   ntStatus = IoOpenDeviceRegistryKey
              (
                 pDevExt->QCPhysicalDeviceObject,
                 PLUGPLAY_REGKEY_DRIVER,
                 KEY_ALL_ACCESS,
                 &hRegKey
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> QCFLT_GetDeviceSerialNumber: reg access failure 0x%x\n", pDevExt->PortName, ntStatus)
      );
      return ntStatus;
   }
   if (MatchPrefix == FALSE)
   {
      RtlInitUnicodeString(&ucValueName, VEN_DEV_SERNUM);
   }
   else
   {
      RtlInitUnicodeString(&ucValueName, VEN_DEV_MSM_SERNUM);
   }
   if ((bSetEntry == TRUE) && (strLen > 0))
   {
      ZwSetValueKey
      (
         hRegKey,
         &ucValueName,
         0,
         REG_SZ,
         (PVOID)pSerLoc,
         strLen
      );
   }
   else
   {
      ZwDeleteValueKey(hRegKey, &ucValueName);
   }
   ZwClose(hRegKey);

   return ntStatus;

} // QCFLT_GetStringDescriptor

NTSTATUS GetConfigurationCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pEvent)
{

   KeSetEvent((PKEVENT)pEvent, IO_NO_INCREMENT, FALSE);
   return STATUS_MORE_PROCESSING_REQUIRED;
}

/****************************************************************************
 *
 * function: QCFilterDispatchIo
 *
 * purpose:  This routine is the dispatch routine for non passthru irps.
 *                We will check the input device object to see if the request
 *                is meant for the control device object. If it is, we will
 *                handle and complete the IRP, if not, we will pass it down to 
 *                the lower drive.
 *
 * arguments:DeviceObject = pointer to the deviceObject.
 *           Irp = pointer to dispatch Irp
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
QCFilterDispatchIo( PDEVICE_OBJECT DeviceObject, PIRP Irp )
{
   PDEVICE_EXTENSION   deviceExtension, pDevExt;
   PCONTROL_DEVICE_EXTENSION pCtlExt;
   PIO_STACK_LOCATION          irpStack;
   NTSTATUS                    status;
   KEVENT                      event;
   ULONG                       inlen, outlen;
   PVOID                       buffer;
   KIRQL                       levelOrHandle;
   KIRQL irql = KeGetCurrentIrql();

   pDevExt = deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
   irpStack = IoGetCurrentIrpStackLocation(Irp);
   Irp->IoStatus.Information = 0;

   //
   // Please note that this is a common dispatch point for controlobject and
   // filter deviceobject attached to the pnp stack. 
   //
   if (deviceExtension->Type == DEVICE_TYPE_QCFIDO) 
   {
      // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterDispatchIo : Acquire RemoveLock\n"));
      status = IoAcquireRemoveLock (&deviceExtension->RemoveLock, Irp);
      if (!NT_SUCCESS (status)) 
      {
         Irp->IoStatus.Status = status;
         IoCompleteRequest (Irp, IO_NO_INCREMENT);
         return status;
      }
      switch( irpStack->MajorFunction )
      {
         case IRP_MJ_DEVICE_CONTROL: 
         {
            // QCFLT_DbgPrint
            // (   
            //    DBG_LEVEL_TRACE,
            //    ("MPFILTER: IRP_MJ_DEVICE_CONTROL to 0x%p\n", DeviceObject)
            // );
          
            buffer = Irp->AssociatedIrp.SystemBuffer;  
            inlen = irpStack->Parameters.DeviceIoControl.InputBufferLength;
            outlen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
       
            // QCFLT_DbgPrint
            // (   
            //    DBG_LEVEL_TRACE,
            //    ("MPFILTER inlen : %d outlen : %d \n", inlen, outlen)
            // );         

            if(!buffer)
            {
               Irp->IoStatus.Information = 0;
               QCFLT_DbgPrint
               (   
                  DBG_LEVEL_ERROR,
                  ("IRP_MJ_DEVICE_CONTROL: STATUS_INVALID_PARAMETER\n")
               );
               status = STATUS_INVALID_PARAMETER;
               break;
            }
            
            status = STATUS_SUCCESS;
            
            switch( irpStack->Parameters.DeviceIoControl.IoControlCode )
            {
               case IOCTL_QCDEV_CREATE_CTL_FILE:
               {
                  QCFLT_DbgPrint
                  (   
                     DBG_LEVEL_TRACE,
                     ("MPFILTER: IRP_MJ_DEVICE_CONTROL sub command IOCTL_QCDEV_CREATE_CTL_FILE\n")
                  );
                   
                  QcAcquireSpinLockWithLevel(&deviceExtension->FilterSpinLock, &levelOrHandle, irql);
                  if (Irp->Cancel)
                  {
                     PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
                     QCFLT_DbgPrint
                     (
                        DBG_LEVEL_DETAIL,
                        ("<%s> USBFLT_Enqueue: CANCELLED\n", deviceExtension->PortName)
                     );
                     status = Irp->IoStatus.Status = STATUS_CANCELLED;
                     QcReleaseSpinLockWithLevel(&deviceExtension->FilterSpinLock, levelOrHandle, irql);
                     break;
                  }

                  _IoMarkIrpPending(Irp);
                  IoSetCancelRoutine(Irp, DispatchCancelQueued);
                      
                  status = STATUS_PENDING;
                  InsertTailList( &deviceExtension->DispatchQueue, &Irp->Tail.Overlay.ListEntry);
                  QcReleaseSpinLockWithLevel(&deviceExtension->FilterSpinLock, levelOrHandle, irql);

                  QCFLT_DbgPrint
                  (   
                     DBG_LEVEL_TRACE,
                     ("MPFILTER: IRP_MJ_DEVICE_CONTROL sub command queued up the control queue\n")
                  );

                  KeSetEvent
                  (
                     deviceExtension->FilterThread.pFilterEvents[QCFLT_FILTER_CREATE_CONTROL],
                     IO_NO_INCREMENT,
                     FALSE
                  );
                  break;
               }   
               case IOCTL_QCDEV_CLOSE_CTL_FILE:
               {
                  QCFLT_DbgPrint
                  (   
                     DBG_LEVEL_TRACE,
                     ("MPFILTER: IRP_MJ_DEVICE_CONTROL sub command IOCTL_QCDEV_CLOSE_CTL_FILE\n")
                  );
                   
                  QcAcquireSpinLockWithLevel(&deviceExtension->FilterSpinLock, &levelOrHandle, irql);
                  if (Irp->Cancel)
                  {
                     PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
                     QCFLT_DbgPrint
                     (
                        DBG_LEVEL_DETAIL,
                        ("<%s> USBFLT_Enqueue: CANCELLED\n", deviceExtension->PortName)
                     );
                     status = Irp->IoStatus.Status = STATUS_CANCELLED;
                     QcReleaseSpinLockWithLevel(&deviceExtension->FilterSpinLock, levelOrHandle, irql);
                     break;
                  }

                  _IoMarkIrpPending(Irp);
                  IoSetCancelRoutine(Irp, DispatchCancelQueued);
                      
                  status = STATUS_PENDING;
                  InsertTailList( &deviceExtension->DispatchQueue, &Irp->Tail.Overlay.ListEntry);
                  QcReleaseSpinLockWithLevel(&deviceExtension->FilterSpinLock, levelOrHandle, irql);

                  QCFLT_DbgPrint
                  (   
                     DBG_LEVEL_TRACE,
                     ("MPFILTER: IRP_MJ_DEVICE_CONTROL sub command queued up the control queue\n")
                  );

                  KeSetEvent
                  (
                     deviceExtension->FilterThread.pFilterEvents[QCFLT_FILTER_CLOSE_CONTROL],
                     IO_NO_INCREMENT,
                     FALSE
                   );
                  break;
               }
#ifdef QCUSB_SHARE_INTERRUPT  
               case IOCTL_QCDEV_REQUEST_DEVICEID:
               {
                  QCFLT_DbgPrint
                  (   
                     DBG_LEVEL_TRACE,
                     ("MPFILTER: IRP_MJ_DEVICE_CONTROL sub command IOCTL_QCDEV_REQUEST_DEVICEID\n")
                  );

                  if (outlen < sizeof(int))
                  {
                     QCFLT_DbgPrint
                     (
                        DBG_LEVEL_TRACE,
                        ("MPFILTER: IOCTL_QCDEV_REQUEST_DEVICEID: buf len too small\n")
                     );
                     status = Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
                     Irp->IoStatus.Information = 0;
                     break;
                  }
                   
                  QcAcquireSpinLockWithLevel(&deviceExtension->FilterSpinLock, &levelOrHandle, irql);
                  if (Irp->Cancel)
                  {
                     PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
                     QCFLT_DbgPrint
                     (
                        DBG_LEVEL_DETAIL,
                        ("<%s> USBFLT_Enqueue: CANCELLED\n", deviceExtension->PortName)
                     );
                     status = Irp->IoStatus.Status = STATUS_CANCELLED;
                     QcReleaseSpinLockWithLevel(&deviceExtension->FilterSpinLock, levelOrHandle, irql);
                     break;
                  }

                  status = STATUS_SUCCESS;
                  *(PULONG *)buffer = (PULONG)DeviceObject;
                  Irp->IoStatus.Information = sizeof(PDEVICE_OBJECT);
                  QcReleaseSpinLockWithLevel(&deviceExtension->FilterSpinLock, levelOrHandle, irql);
                  break;
               }   
#endif
               case IOCTL_QCDEV_GET_MUX_INTERFACE:
               {
                  PMUX_INTERFACE_INFO pMuxInfo;
                  UCHAR MuxInterfaceNumber = 0;
                  QCFLT_DbgPrint
                  (   
                     DBG_LEVEL_TRACE,
                     ("MPFILTER: IRP_MJ_DEVICE_CONTROL sub command IOCTL_QCDEV_GET_MUX_INTERFACE\n")
                  );
                   
                  QcAcquireSpinLockWithLevel(&deviceExtension->FilterSpinLock, &levelOrHandle, irql);
                  if (Irp->Cancel)
                  {
                     PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
                     QCFLT_DbgPrint
                     (
                        DBG_LEVEL_DETAIL,
                        ("<%s> USBFLT_Enqueue: CANCELLED\n", deviceExtension->PortName)
                     );
                     status = Irp->IoStatus.Status = STATUS_CANCELLED;
                     QcReleaseSpinLockWithLevel(&deviceExtension->FilterSpinLock, levelOrHandle, irql);
                     break;
                  }

                  status = STATUS_SUCCESS;
                  pMuxInfo = (PMUX_INTERFACE_INFO)buffer;
                  MuxInterfaceNumber = *(UCHAR *)buffer;
				  if (pDevExt->MuxSupport == 0x01)
				  {
					  ULONG i,j;
					  status = STATUS_UNSUCCESSFUL;
					  for (i=0;i<NUM_MUX_INTERFACES_MUX; i++)
					  {
						  if (pDevExt->InterfaceMuxList[i].PhysicalRmnetInterface.RmnetInterface == MuxInterfaceNumber)
						  {
							  pMuxInfo->FilterDeviceObj = pDevExt;
							  pMuxInfo->InterfaceNumber = MuxInterfaceNumber;
							  pMuxInfo->MuxEnabled = 0x01;
							  pMuxInfo->PhysicalInterfaceNumber = MuxInterfaceNumber;
							  status = STATUS_SUCCESS;
							  Irp->IoStatus.Information = sizeof(MUX_INTERFACE_INFO);
							  goto Success;
						  }
						  for(j=0;j<NUM_MUX_INTERFACES_MUX;j++)
						  {
							  if (pDevExt->InterfaceMuxList[i].MuxInterfaces[j].RmnetInterface == MuxInterfaceNumber)
							  {
								  pMuxInfo->FilterDeviceObj = pDevExt;
								  pMuxInfo->InterfaceNumber = MuxInterfaceNumber;
								  pMuxInfo->MuxEnabled = 0x01;
								  pMuxInfo->PhysicalInterfaceNumber = pDevExt->InterfaceMuxList[i].PhysicalRmnetInterface.RmnetInterface;
								  status = STATUS_SUCCESS;
								  Irp->IoStatus.Information = sizeof(MUX_INTERFACE_INFO);
								  goto Success;
							  }
						  }
					  }
Success:              
					  QcReleaseSpinLockWithLevel(&deviceExtension->FilterSpinLock, levelOrHandle, irql);
					  break;
				  }
				  else
				  {
                     pMuxInfo->FilterDeviceObj = pDevExt;
                     pMuxInfo->InterfaceNumber = MuxInterfaceNumber;
                     pMuxInfo->MuxEnabled = 0x00;
                     pMuxInfo->PhysicalInterfaceNumber = MuxInterfaceNumber;
                     status = STATUS_SUCCESS;
                     Irp->IoStatus.Information = sizeof(MUX_INTERFACE_INFO);
                     QcReleaseSpinLockWithLevel(&deviceExtension->FilterSpinLock, levelOrHandle, irql);
                     break;
				  }
               }   

               case IOCTL_QCDEV_REPORT_DEV_NAME:
               {
                  QCFLT_DbgPrint
                  (   
                     DBG_LEVEL_TRACE,
                     ("MPFILTER: IOCTL_QCDEV_REPORT_DEV_NAME: inlen %d\n", inlen)
                  );
                  if (inlen >= 1024)
                  {
                     QCFLT_DbgPrint
                     (   
                        DBG_LEVEL_TRACE,
                        ("MPFILTER: IOCTL_QCDEV_REPORT_DEV_NAME: failure\n")
                     );
                     status = STATUS_UNSUCCESSFUL;
                     Irp->IoStatus.Information = 0;
                  }
                  else
                  {
                     RtlCopyMemory(pDevExt->PeerDevName, buffer, inlen);
                     pDevExt->PeerDevNameLength = inlen;
                     status = STATUS_SUCCESS;
                     Irp->IoStatus.Information = inlen;
                  }
                  break;
               }

               case IOCTL_QCDEV_GET_PEER_DEV_NAME:
               {
                  QCFLT_DbgPrint
                  (   
                     DBG_LEVEL_TRACE,
                     ("MPFILTER: IOCTL_QCDEV_GET_PEER_DEV_NAME: outlen %d/%d\n", outlen,
                       pDevExt->PeerDevNameLength)
                  );
                  if ((outlen < pDevExt->PeerDevNameLength) || (pDevExt->PeerDevNameLength == 0))
                  {
                     QCFLT_DbgPrint
                     (   
                        DBG_LEVEL_TRACE,
                        ("MPFILTER: IOCTL_QCDEV_GET_PEER_DEV_NAME: failure\n")
                     );
                     status = STATUS_UNSUCCESSFUL;
                     Irp->IoStatus.Information = 0;
                  }
                  else
                  {
                     RtlCopyMemory(buffer, pDevExt->PeerDevName, pDevExt->PeerDevNameLength);
                     status = STATUS_SUCCESS;
                     Irp->IoStatus.Information = pDevExt->PeerDevNameLength;
                  }
                  break;
               }

               case IOCTL_QCDEV_GET_PARENT_DEV_NAME:
               {
                  UNICODE_STRING parentDevNameU;
                  ULONG          nameLen;

                  RtlInitUnicodeString(&parentDevNameU, pDevExt->FriendlyNameHolder);
                  QCFLT_DbgPrint
                  (
                     DBG_LEVEL_TRACE,
                     ("MPFILTER: IOCTL_QCDEV_GET_PARENT_DEV_NAME: outlen %d/%d\n", outlen,
                       parentDevNameU.Length)
                  );

                  if ((outlen < parentDevNameU.Length) || (parentDevNameU.Length == 0))
                  {
                     QCFLT_DbgPrint
                     (
                        DBG_LEVEL_TRACE,
                        ("MPFILTER: IOCTL_QCDEV_GET_PARENT_DEV_NAME: failure\n")
                     );
                     status = STATUS_UNSUCCESSFUL;
                     Irp->IoStatus.Information = 0;
                  }
                  else
                  {
                     RtlCopyMemory(buffer, parentDevNameU.Buffer, parentDevNameU.Length);
                     status = STATUS_SUCCESS;
                     Irp->IoStatus.Information = parentDevNameU.Length;
                  }
                  break;
               }

               default:
               {
                  IoSkipCurrentIrpStackLocation (Irp);
                  status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
                  // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterDispatchIo : Release RemoveLock\n"));
                  IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
                  return status;
               }
            } 
            break;
         }
         case IRP_MJ_INTERNAL_DEVICE_CONTROL:
         {
            // QCFLT_DbgPrint
            // (   
            //    DBG_LEVEL_TRACE,
            //    ("MPFILTER: IRP_MJ_INTERNAL_DEVICE_CONTROL to 0x%p\n", DeviceObject)
            // );
          
            buffer = Irp->AssociatedIrp.SystemBuffer;  
            inlen = irpStack->Parameters.DeviceIoControl.InputBufferLength;
            outlen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
       
            // QCFLT_DbgPrint
            // (   
            //    DBG_LEVEL_TRACE,
            //    ("MPFILTER inlen : %d outlen : %d \n", inlen, outlen)
            // );         

            status = STATUS_SUCCESS;
            
            switch( irpStack->Parameters.DeviceIoControl.IoControlCode )
            {
               case IOCTL_INTERNAL_USB_SUBMIT_URB:
               {
                  PURB            pUrb = NULL;

                  // URB is collected BEFORE forward to bus driver or next lower object
                  pUrb = (PURB) irpStack->Parameters.Others.Argument1;

                  // QCFLT_DbgPrint
                  // (   
                  //    DBG_LEVEL_TRACE,
                  //    ("MPFILTER: IRP_MJ_INTERNAL_DEVICE_CONTROL sub command IOCTL_INTERNAL_USB_SUBMIT_URB(0x%x) with URB 0x%p\n", irpStack->Parameters.DeviceIoControl.IoControlCode, pUrb)
                  // );

                  if (pUrb != NULL)
                  {
                  switch (pUrb->UrbHeader.Function)
                  {
                     case URB_FUNCTION_GET_CONFIGURATION:
                     {

                        KEVENT event;
                        KeInitializeEvent(&event, NotificationEvent, FALSE);
                           
                        QCFLT_DbgPrint
                        (   
                          DBG_LEVEL_TRACE,
                          ("MPFILTER: IOCTL_INTERNAL_USB_SUBMIT_URB sub command URB_FUNCTION_GET_CONFIGURATION\n")
                         );

                         // Forward this request to bus driver or next lower object
                         // with completion routine
                         IoCopyCurrentIrpStackLocationToNext(Irp);

                         IoSetCompletionRoutine( Irp, 
                                                (PIO_COMPLETION_ROUTINE) GetConfigurationCompletion,
                                                &event, TRUE, TRUE, TRUE);

                         status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
                         return status;
                    }
                    case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
                    {
                       int i;
                       QCFLT_DbgPrint
                       (   
                         DBG_LEVEL_TRACE,
                         ("MPFILTER: URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE descriptor type 0x%x stacklocations %d\n", pUrb->UrbControlDescriptorRequest.DescriptorType, Irp->StackCount)
                       );
                       if (pUrb->UrbControlDescriptorRequest.DescriptorType == 0x01)
                       {
                          KEVENT event;
                          KeInitializeEvent(&event, NotificationEvent, FALSE);

                          // Forward this request to bus driver or next lower object
                          // with completion routine
                          IoCopyCurrentIrpStackLocationToNext(Irp);

                          IoSetCompletionRoutine( Irp, 
                                            (PIO_COMPLETION_ROUTINE) GetConfigurationCompletion,
                                            &event, TRUE, TRUE, TRUE);

                          status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
                          
                          KeWaitForSingleObject
                                 (
                                   &event,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL
                                 );
                          status = Irp->IoStatus.Status;
                           if (NT_SUCCESS (status))
                           {
                              PUSB_DEVICE_DESCRIPTOR pDeviceDiscriptor;
                              pDeviceDiscriptor = (PUSB_DEVICE_DESCRIPTOR)pUrb->UrbControlDescriptorRequest.TransferBuffer;
						      RtlCopyMemory(&pDevExt->DeviceDescriptor, pDeviceDiscriptor, sizeof(USB_DEVICE_DESCRIPTOR));
						   }
                          Irp->IoStatus.Status = status;
                          IoCompleteRequest (Irp, IO_NO_INCREMENT);
                          // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterDispatchIo : Release RemoveLock\n"));
                          IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
                          return status;
					   }
                       else if (pUrb->UrbControlDescriptorRequest.DescriptorType == 0x02)
                       {
                          KEVENT event;
                          KeInitializeEvent(&event, NotificationEvent, FALSE);

                          // Forward this request to bus driver or next lower object
                          // with completion routine
                          IoCopyCurrentIrpStackLocationToNext(Irp);

                          IoSetCompletionRoutine( Irp, 
                                            (PIO_COMPLETION_ROUTINE) GetConfigurationCompletion,
                                            &event, TRUE, TRUE, TRUE);

                          status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
                          
                          KeWaitForSingleObject
                                 (
                                   &event,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL
                                 );
                          status = Irp->IoStatus.Status;
                           if (NT_SUCCESS (status))
                           {
                              PUSB_CONFIGURATION_DESCRIPTOR pConfigDiscriptor;
                              pConfigDiscriptor = (PUSB_CONFIGURATION_DESCRIPTOR)pUrb->UrbControlDescriptorRequest.TransferBuffer;
                             if (pUrb->UrbControlDescriptorRequest.TransferBufferLength == sizeof (USB_CONFIGURATION_DESCRIPTOR))
                             {
							   if (USBPNP_ValidateConfigDescriptor( pDevExt, pConfigDiscriptor) == FALSE)
							   {
								   QCFLT_DbgPrint
								   (   
									 DBG_LEVEL_TRACE,
									 ("MPFILTER: wrong config discriptor\n")
								   );
								   status = STATUS_UNSUCCESSFUL;
							   }
							   else
							   {
									QCFLT_PrintBytes
									(
										pConfigDiscriptor,
										pUrb->UrbControlDescriptorRequest.TransferBufferLength,
										pUrb->UrbControlDescriptorRequest.TransferBufferLength,
										"BEFORE",
										pDevExt,
										DBG_LEVEL_VERBOSE
									);
								   if ((pDevExt->MuxSupport == 1) && (pConfigDiscriptor->bNumInterfaces >= (pDevExt->StartRmnetIface + pDevExt->NumRmnetIface - 1)))
						           {
                                      pConfigDiscriptor->wTotalLength = pConfigDiscriptor->wTotalLength + (sizeof(USB_INTERFACE_DESCRIPTOR)*(pDevExt->NumRmnetIfaceVCount));
                                      pConfigDiscriptor->bNumInterfaces += pDevExt->NumRmnetIfaceVCount;
								   }
									QCFLT_PrintBytes
									(
										pConfigDiscriptor,
										pUrb->UrbControlDescriptorRequest.TransferBufferLength,
										pUrb->UrbControlDescriptorRequest.TransferBufferLength,
										"AFTER",
										pDevExt,
										DBG_LEVEL_VERBOSE
									);
							   }
                             }
                             else
                             {
                                 PUSB_CONFIGURATION_DESCRIPTOR pConfigDiscriptor = (PUSB_CONFIGURATION_DESCRIPTOR)pUrb->UrbControlDescriptorRequest.TransferBuffer;
						         if ((pDevExt->MuxSupport == 1) && (pConfigDiscriptor->bNumInterfaces >= (pDevExt->StartRmnetIface + pDevExt->NumRmnetIface - 1)))
						         {
							        if (USBPNP_ValidateConfigDescriptor( pDevExt, pConfigDiscriptor) == FALSE)
								    {
								       QCFLT_DbgPrint
									   (   
									      DBG_LEVEL_TRACE,
									      ("MPFILTER: wrong config discriptor\n")
									   );
									   status = STATUS_UNSUCCESSFUL;
								    }
								    else
								    {
										QCFLT_PrintBytes
										(
											pConfigDiscriptor,
											pUrb->UrbControlDescriptorRequest.TransferBufferLength,
											pUrb->UrbControlDescriptorRequest.TransferBufferLength,
											"BEFORE",
											pDevExt,
											DBG_LEVEL_VERBOSE
										);
									    USBPNP_SelectAndAddInterfaces(pDevExt, pUrb);
										QCFLT_PrintBytes
										(
											pConfigDiscriptor,
											pUrb->UrbControlDescriptorRequest.TransferBufferLength,
											pUrb->UrbControlDescriptorRequest.TransferBufferLength,
											"AFTER",
											pDevExt,
											DBG_LEVEL_VERBOSE
										);
								    }
								}
                            }
                          }
                          Irp->IoStatus.Status = status;
                          IoCompleteRequest (Irp, IO_NO_INCREMENT);
                          // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterDispatchIo : Release RemoveLock\n"));

                          // Get the serial numer here and update it
                          status = QCFLT_GetStringDescriptor(DeviceObject, pDevExt->DeviceDescriptor.iProduct, 0x0409, TRUE);
                          if ((!NT_SUCCESS(status)) && (pDevExt->DeviceDescriptor.iProduct != 2))
                          {
                             QCFLT_DbgPrint
                             (
                                DBG_LEVEL_ERROR,
                                ("<%s> QCFilterDispatchIo: Failed to get SerialNumber with iProduct: 0x%x, try default\n",
                                  pDevExt->PortName, pDevExt->DeviceDescriptor.iProduct)
                             );
                             // workaround: try default iProduct value 0x02
                             status = QCFLT_GetStringDescriptor(DeviceObject, 0x02, 0x0409, TRUE);
                          }
                          QCFLT_DbgPrint
                          (
                             DBG_LEVEL_ERROR,
                             ("<%s> QCFilterDispatchIo: Tried to get SerialNumber with iProduct: ST(0x%x)\n",
                               pDevExt->PortName, status)
                          );
                          
                          status = QCFLT_GetStringDescriptor(DeviceObject, pDevExt->DeviceDescriptor.iSerialNumber, 0x0409, FALSE);
                          QCFLT_DbgPrint
                          (
                             DBG_LEVEL_ERROR,
                             ("<%s> QCFilterDispatchIo: Tried to get SerialNumber with : 0x%x ST(0x%x)\n",
                               pDevExt->PortName, pDevExt->DeviceDescriptor.iSerialNumber, status)
                          );
                          status = STATUS_SUCCESS; // make possible failure none-critical 

                          
                          IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
                          return status;
                       }
                       else
                       {
                          IoSkipCurrentIrpStackLocation (Irp);
                          status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
                          // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterDispatchIo : Release RemoveLock\n"));                          
                          IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
                          return status;
                       }
                    }
               default:
               {
                       // QCFLT_DbgPrint
                       // (   
                       //   DBG_LEVEL_TRACE,
                       //   ("MPFILTER: IOCTL_INTERNAL_USB_SUBMIT_URB sub command 0x%x\n", pUrb->UrbHeader.Function)
                       // );

                  IoSkipCurrentIrpStackLocation (Irp);
                  status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
                  // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterDispatchIo : Release RemoveLock\n"));
                  IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
                  return status;
               }
            } 
         }
              else
              {
                  QCFLT_DbgPrint
                  (   
                     DBG_LEVEL_TRACE,
                     ("MPFILTER: URB IS NULL\n")
                  );


                IoSkipCurrentIrpStackLocation (Irp);
                status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
                // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterDispatchIo : Release RemoveLock\n"));
                IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
                return status;
              }
         break;
            }   
         default:
         {
               QCFLT_DbgPrint
               (   
                  DBG_LEVEL_TRACE,
                  ("MPFILTER: OTHER IRP_MJ_INTERNAL_DEVICE_IOCONTROL (0x%x)\n", irpStack->Parameters.DeviceIoControl.IoControlCode)
               );


                  IoSkipCurrentIrpStackLocation (Irp);
                  status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
                  // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterDispatchIo : Release RemoveLock\n"));
                  IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
                  return status;
               }
            } 
            break;
         }
         
         default:
         {
         QCFLT_DbgPrint
         (   
            DBG_LEVEL_TRACE,
            ("MPFILTER: OTHER IOCONTROL\n")
         );
            IoSkipCurrentIrpStackLocation (Irp);
            status = IoCallDriver (deviceExtension->NextLowerDriver, Irp);
            // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterDispatchIo : Release RemoveLock\n"));
            IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
            return status;
         }
         
      }
      if (status != STATUS_PENDING)
      {
         QCFLT_DbgPrint
         (   
            DBG_LEVEL_TRACE,
            ("CIRP C 0x%p(0x%x)\n", Irp, status)
         );
         Irp->IoStatus.Status = status;
         IoSetCancelRoutine(Irp, NULL);
         // QCFLT_DbgPrint( DBG_LEVEL_DETAIL,("QCFilterDispatchIo : Release RemoveLock\n"));
         IoReleaseRemoveLock(&deviceExtension->RemoveLock, Irp); 
         IoCompleteRequest(Irp, IO_NO_INCREMENT);
      }
      return status;
   }
 
   ASSERT(deviceExtension->Type == DEVICE_TYPE_QCCDO);

   pCtlExt = (PCONTROL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
  
   if (!pCtlExt->Deleted) 
   { 
      PFILTER_DEVICE_LIST pIocFilterDev = NULL;
      
      pIocFilterDev = QCFLT_FindControlDevice(DeviceObject);
      if (pIocFilterDev == NULL)
      {
         Irp->IoStatus.Information = 0;
         Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
         IoCompleteRequest(Irp, IO_NO_INCREMENT);
         return STATUS_UNSUCCESSFUL;
      }

      status = STATUS_UNSUCCESSFUL;
      irpStack = IoGetCurrentIrpStackLocation (Irp);

      switch (irpStack->MajorFunction) 
      {
         case IRP_MJ_CREATE:
         {
            if (pIocFilterDev->DispatchTable[IRP_MJ_CREATE] != NULL)
            {
               status = (pIocFilterDev->DispatchTable[IRP_MJ_CREATE])(DeviceObject, Irp);
               if (status == STATUS_SUCCESS)
               {
                  InterlockedIncrement(&(pCtlExt->DeviceOpenCount));
               }
               return status;
            }
            break;
         }
         case IRP_MJ_CLOSE:
         {
            InterlockedDecrement(&(pCtlExt->DeviceOpenCount));
            if (pIocFilterDev->DispatchTable[IRP_MJ_CLOSE] != NULL)
            {
               status = (pIocFilterDev->DispatchTable[IRP_MJ_CLOSE])(DeviceObject, Irp);
               return status;
            }
            else
            {
               status = STATUS_SUCCESS;
            }
            break;
         }
         case IRP_MJ_CLEANUP:
         {
            KIRQL levelOrHandle;
               // DbgPrint( "QCFilterDispatchIo IRP_MJ_CLEANUP: Acquire RemoveLock\n" );
            status = IoAcquireRemoveLock(&pCtlExt->RmLock, NULL);
               if (NT_SUCCESS( status ))
               {
            if (pIocFilterDev->DispatchTable[IRP_MJ_CLEANUP] != NULL)
            {
                     status = (pIocFilterDev->DispatchTable[IRP_MJ_CLEANUP])(DeviceObject, Irp);
                     // DbgPrint("QCFilterDispatchIo IRP_MJ_CLEANUP: Release RemoveLock\n");
                  IoReleaseRemoveLock(&pCtlExt->RmLock, NULL);
                     return status;
                  }
                  // DbgPrint( "QCFilterDispatchIo IRP_MJ_CLEANUP: Release RemoveLock\n" );
               IoReleaseRemoveLock(&pCtlExt->RmLock, NULL);
               break;
            }
         }
         case  IRP_MJ_DEVICE_CONTROL:
         {
            KIRQL levelOrHandle;
               // DbgPrint( "QCFilterDispatchIo IRP_MJ_DEVICE_CONTROL: Acquire RemoveLock\n" );
            status = IoAcquireRemoveLock(&pCtlExt->RmLock, NULL);
               if (NT_SUCCESS( status ))
               {
            if (pIocFilterDev->DispatchTable[IRP_MJ_DEVICE_CONTROL] != NULL)
            {
                     status = (pIocFilterDev->DispatchTable[IRP_MJ_DEVICE_CONTROL])(DeviceObject, Irp);
                     // DbgPrint( "QCFilterDispatchIo IRP_MJ_DEVICE_CONTROL: Release RemoveLock\n" );
                  IoReleaseRemoveLock(&pCtlExt->RmLock, NULL);
                     return status;
                  }
                  // DbgPrint( "QCFilterDispatchIo IRP_MJ_DEVICE_CONTROL: Release RemoveLock\n" );
               IoReleaseRemoveLock(&pCtlExt->RmLock, NULL);
            }
            break;
         }
         case  IRP_MJ_READ:
         {
            KIRQL levelOrHandle;
               // DbgPrint( "QCFilterDispatchIo IRP_MJ_READ: Acquire RemoveLock\n" );
            status = IoAcquireRemoveLock(&pCtlExt->RmLock, NULL);
               if (NT_SUCCESS( status ))
               {
  	          if (pIocFilterDev->DispatchTable[IRP_MJ_READ] != NULL)
                  {
                     status = (pIocFilterDev->DispatchTable[IRP_MJ_READ])(DeviceObject, Irp);
                     // DbgPrint( "QCFilterDispatchIo IRP_MJ_READ: Release RemoveLock\n" );
                  IoReleaseRemoveLock(&pCtlExt->RmLock, NULL);
                     return status;
                  }
                  // DbgPrint( "QCFilterDispatchIo IRP_MJ_READ: Release RemoveLock\n" );
               IoReleaseRemoveLock(&pCtlExt->RmLock, NULL);
            }
            break;
         }
         case  IRP_MJ_WRITE:
         {
            KIRQL levelOrHandle;
               // DbgPrint( "QCFilterDispatchIo IRP_MJ_WRITE: Acquire RemoveLock\n" );
            status = IoAcquireRemoveLock(&pCtlExt->RmLock, NULL);
               if (NT_SUCCESS( status ))
               {
            if (pIocFilterDev->DispatchTable[IRP_MJ_WRITE] != NULL)
            {
                     status = (pIocFilterDev->DispatchTable[IRP_MJ_WRITE])(DeviceObject, Irp);
                     // DbgPrint( "QCFilterDispatchIo IRP_MJ_WRITE: Release RemoveLock\n" );
                  IoReleaseRemoveLock(&pCtlExt->RmLock, NULL);
                     return status;
                  }
                  // DbgPrint( "QCFilterDispatchIo IRP_MJ_WRITE: Release RemoveLock\n");
               IoReleaseRemoveLock(&pCtlExt->RmLock, NULL);
            }
            break;
         }
         default:
            break;
      }
   } 
   else 
   {
      status = STATUS_DEVICE_REMOVED;
   }

   Irp->IoStatus.Status = status;
   IoCompleteRequest (Irp, IO_NO_INCREMENT);
   return status;
}

/*********************************************************************
 *
 * function:   QCFLT_Wait
 *
 * purpose:    Used for sleep in passive level
 *
 * arguments:  pDevExt = Device Extension,
 *                    WaitTime = time to waht in milliseonds
 *
 * returns:    void
 *
 *********************************************************************/
VOID QCFLT_Wait(PDEVICE_EXTENSION pDevExt, LONGLONG WaitTime)
{
   LARGE_INTEGER delayValue;

   delayValue.QuadPart = WaitTime;
   KeClearEvent(&pDevExt->ForTimeoutEvent);
   KeWaitForSingleObject
   (
      &pDevExt->ForTimeoutEvent,
      Executive,
      KernelMode,
      FALSE,
      &delayValue
   );
   KeClearEvent(&pDevExt->ForTimeoutEvent);

   return;
}

/*********************************************************************
 *
 * function:   QCFilter_InitializeDeviceExt
 *
 * purpose:    Initializes the filter device object device extension
 *
 * arguments:  deviceObject = filter device object
 *
 * returns:    NT Status 
 *
 *********************************************************************/
NTSTATUS 
QCFilter_InitializeDeviceExt( PDEVICE_OBJECT deviceObject )
{
   PDEVICE_EXTENSION pDevExt = deviceObject->DeviceExtension;
   NTSTATUS          ntStatus = STATUS_SUCCESS;

   if (pDevExt->Type != DEVICE_TYPE_QCFIDO)
   {
      return STATUS_UNSUCCESSFUL;
   }
   pDevExt->ControlDeviceObject = NULL;
   pDevExt->pAdapterContext = NULL;
   KeInitializeSpinLock(&pDevExt->FilterSpinLock);

   InitializeListHead(&pDevExt->DispatchQueue);

   KeInitializeEvent
   (
      &pDevExt->ForTimeoutEvent,
      SynchronizationEvent,
      TRUE
   );

   /* Filter thread initialization */
   pDevExt->FilterThread.pFilterThread = NULL;
   pDevExt->FilterThread.hFilterThreadHandle = NULL;
   pDevExt->FilterThread.bFilterThreadInCreation = FALSE;
   pDevExt->FilterThread.FilterThreadInCancellation = 0;
   pDevExt->FilterThread.bFilterActive = FALSE;
   pDevExt->FilterThread.bFilterStopped = FALSE;
   pDevExt->FilterThread.pFilterCurrent = NULL;

   KeInitializeEvent
   (
      &pDevExt->FilterThread.FilterThreadStartedEvent,
      SynchronizationEvent,
      FALSE
   );

   KeInitializeEvent
   (
      &pDevExt->FilterThread.FilterCreateControlDeviceEvent,
      SynchronizationEvent,
      FALSE
   );

   KeInitializeEvent
   (
      &pDevExt->FilterThread.FilterCloseControlDeviceEvent,
      SynchronizationEvent,
      FALSE
   );

   KeInitializeEvent
   (
      &pDevExt->FilterThread.FilterStopEvent,
      SynchronizationEvent,
      FALSE
   );

   KeInitializeEvent
   (
      &pDevExt->FilterThread.FilterThreadExitEvent,
      SynchronizationEvent,
      FALSE
   );

   pDevExt->FilterThread.pFilterEvents[QCFLT_FILTER_CREATE_CONTROL] = &pDevExt->FilterThread.FilterCreateControlDeviceEvent;
   pDevExt->FilterThread.pFilterEvents[QCFLT_FILTER_CLOSE_CONTROL] = &pDevExt->FilterThread.FilterCloseControlDeviceEvent;
   pDevExt->FilterThread.pFilterEvents[QCFLT_STOP_FILTER] = &pDevExt->FilterThread.FilterStopEvent;

   RtlZeroMemory(pDevExt->PeerDevName, 4096);
   pDevExt->PeerDevNameLength = 0;

   return ntStatus;
}

/*********************************************************************
 *
 * function:   QCFLT_IsIrpInQueue
 *
 * purpose:    Initializes the filter device object device extension
 *
 * arguments:  pDevExt = Filter device object device extension
 *                   Queue = Pointer to Queue                      
 *                   Irp = Irp
 *
 * returns:    Boolean
 *
 **********************************************************************/
BOOLEAN 
QCFLT_IsIrpInQueue(PDEVICE_EXTENSION pDevExt, PLIST_ENTRY Queue,PIRP Irp)
{
   PLIST_ENTRY headOfList, peekEntry;
   PIRP pIrp;
   BOOLEAN bInQueue = FALSE;
   PCHAR pcIrpName;
   KIRQL levelOrHandle;


   QCFLT_DbgPrint
   (
      DBG_LEVEL_DETAIL,
      ("<%s> IsIrpInQueue...\n", pDevExt->PortName)
   );

   if (!IsListEmpty(Queue))
   {
      headOfList = Queue;
      peekEntry = headOfList->Flink;

      while (peekEntry != headOfList)
      {
         pIrp =  CONTAINING_RECORD
                 (
                    peekEntry,
                    IRP,
                    Tail.Overlay.ListEntry
                 );

         if (pIrp == Irp)
         {
            bInQueue = TRUE;
            break;
         }

         peekEntry = peekEntry->Flink;
      }
   }

   return bInQueue;

}  

/****************************************************************************
 *
 * function: QCFLT_AddControlDevice
 *
 * purpose:  Add the control device to the global list.
 *
 * arguments: pFilterDeviceInfo = pointer to the control device object information
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS
QCFLT_AddControlDevice
(
   PFILTER_DEVICE_INFO pFilterDeviceInfo
)
{
   PFILTER_DEVICE_LIST pIocDev;
   PCONTROL_DEVICE_EXTENSION pDevExt = pFilterDeviceInfo->pControlDeviceObject->DeviceExtension;
   KIRQL levelOrHandle;

   QcAcquireSpinLock(&Control_DeviceLock, &levelOrHandle);

   pIocDev = ExAllocatePool(NonPagedPool, sizeof(FILTER_DEVICE_LIST));
   if (pIocDev == NULL)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("MPIOC: AddDev failure.\n")
      );
      QcReleaseSpinLock(&Control_DeviceLock, levelOrHandle);
      return STATUS_UNSUCCESSFUL;
   }
   RtlZeroMemory(pIocDev, sizeof(FILTER_DEVICE_LIST));
   pIocDev->pAdapter = pFilterDeviceInfo->pAdapter;

   pIocDev->DeviceName.Buffer = (PWSTR) _ExAllocatePool( NonPagedPool, pFilterDeviceInfo->pDeviceName->Length, "NAME" );
   if (pIocDev->DeviceName.Buffer == NULL)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("\n<%s> QCFLT_AddControlDevice: DeviceName failure", pDevExt->PortName)
      );
   }
   else
   {
      pIocDev->DeviceName.MaximumLength = pFilterDeviceInfo->pDeviceName->Length;
      RtlCopyUnicodeString(&pIocDev->DeviceName, pFilterDeviceInfo->pDeviceName);
   }

   pIocDev->DeviceLinkName.Buffer = (PWSTR) _ExAllocatePool( NonPagedPool, pFilterDeviceInfo->pDeviceLinkName->Length, "NAME" );
   if (pIocDev->DeviceLinkName.Buffer == NULL)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("\n<%s> QCFLT_AddControlDevice: DeviceLinkName failure", pDevExt->PortName)
      );
   }
   else
   {
      pIocDev->DeviceLinkName.MaximumLength = pFilterDeviceInfo->pDeviceLinkName->Length;
      RtlCopyUnicodeString(&pIocDev->DeviceLinkName, pFilterDeviceInfo->pDeviceLinkName);
   }

   RtlCopyMemory(&pIocDev->DispatchTable, pFilterDeviceInfo->MajorFunctions, sizeof(pIocDev->DispatchTable));
   pIocDev->pControlDeviceObject = pFilterDeviceInfo->pControlDeviceObject;
   pIocDev->pFilterDeviceObject= pFilterDeviceInfo->pFilterDeviceObject;

   InsertTailList(&Control_DeviceList, &pIocDev->List);

   QcReleaseSpinLock(&Control_DeviceLock, levelOrHandle);
   return STATUS_SUCCESS;
}  // MPIOC_AddDevice

/****************************************************************************
 *
 * function: QCFLT_DeleteControlDevice
 *
 * purpose:  Delete the control device from the global list.
 *
 * arguments: pControlDeviceObject = pointer to the control device object
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS 
QCFLT_DeleteControlDevice
(
   PDEVICE_OBJECT pControlDeviceObject
)
{
   PFILTER_DEVICE_LIST pIocDev;
   PCONTROL_DEVICE_EXTENSION pDevExt;
   PLIST_ENTRY     headOfList, peekEntry, nextEntry;
   KIRQL levelOrHandle;

   QcAcquireSpinLock(&Control_DeviceLock, &levelOrHandle);

   if (!IsListEmpty(&Control_DeviceList))
   {
      headOfList = &Control_DeviceList;
      peekEntry = headOfList->Flink;
      nextEntry = peekEntry->Flink;

      while (peekEntry != headOfList)
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      FILTER_DEVICE_LIST,
                      List
                   );
         
         if (pIocDev->pControlDeviceObject == pControlDeviceObject)
         {
            RemoveEntryList(peekEntry);
            QcReleaseSpinLock(&Control_DeviceLock, levelOrHandle);
            pDevExt = pControlDeviceObject->DeviceExtension;
            RtlZeroMemory(&pIocDev->DispatchTable, sizeof(pIocDev->DispatchTable)); 
            if ((pIocDev->pControlDeviceObject != NULL) && (pDevExt->DeviceOpenCount == 0))
            {
               IoReleaseRemoveLockAndWait(&pDevExt->RmLock, NULL);            
               IoDeleteSymbolicLink(&(pIocDev->DeviceLinkName));
               RtlFreeUnicodeString(&(pIocDev->DeviceLinkName));
               RtlFreeUnicodeString(&(pIocDev->DeviceName));
               IoDeleteDevice(pIocDev->pControlDeviceObject);
               pIocDev->pControlDeviceObject = NULL;
               ExFreePool(pIocDev);
               return STATUS_SUCCESS;
            }
            else
            {
               QcAcquireSpinLock(&Control_DeviceLock, &levelOrHandle);
               InsertTailList(&Control_DeviceList, &pIocDev->List);               
               break;
            }

         }
         peekEntry = nextEntry;
         nextEntry = nextEntry->Flink;
      }  // while
   } // if

   QcReleaseSpinLock(&Control_DeviceLock, levelOrHandle);

   return STATUS_SUCCESS;
}  

/****************************************************************************
 *
 * function: QCFLT_FindControlDevice
 *
 * purpose:  Finds the control device from the global list.
 *
 * arguments: pControlDeviceObject = pointer to the control device object
 *
 * returns: Filter device object item
 *
 ****************************************************************************/
PFILTER_DEVICE_LIST 
QCFLT_FindControlDevice
(
   PDEVICE_OBJECT pControlDeviceObject
)
{
   PFILTER_DEVICE_LIST pIocDev = NULL, foundIocDev = NULL;
   PCONTROL_DEVICE_EXTENSION pDevExt = pControlDeviceObject->DeviceExtension;
   PLIST_ENTRY     headOfList, peekEntry;
   KIRQL levelOrHandle;

   QCFLT_DbgPrint
   (
      DBG_LEVEL_DETAIL,
      ("<%s> -->QCFLT_FindControlDevice 0x%p\n", pDevExt->PortName, pControlDeviceObject)
   );

   
   QcAcquireSpinLock(&Control_DeviceLock, &levelOrHandle);

   if (!IsListEmpty(&Control_DeviceList))
   {
      headOfList = &Control_DeviceList;
      peekEntry = headOfList->Flink;

      while ((peekEntry != headOfList) && (foundIocDev == NULL))
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      FILTER_DEVICE_LIST,
                      List
                   );
         
         if (pIocDev->pControlDeviceObject == pControlDeviceObject)
         {
            foundIocDev = pIocDev;
         }
         peekEntry = peekEntry->Flink;
      }  // while
   } // if

   QcReleaseSpinLock(&Control_DeviceLock, levelOrHandle);

   return foundIocDev;
}  // MPIOC_DeleteDevice

/****************************************************************************
 *
 * function: QCFLT_FindControlDevice
 *
 * purpose:  Finds the control device from the global list.
 *
 * arguments: pFido = pointer to the filter device object
 *
 * returns: control device object
 *
 ****************************************************************************/
PDEVICE_OBJECT
QCFLT_FindControlDeviceFido
(
   PDEVICE_OBJECT pFido
)
{
   PFILTER_DEVICE_LIST pIocDev = NULL, foundIocDev = NULL;
   PDEVICE_EXTENSION pDevExt = pFido->DeviceExtension;
   PLIST_ENTRY     headOfList, peekEntry;
   KIRQL levelOrHandle;

   QCFLT_DbgPrint
   (
      DBG_LEVEL_DETAIL,
      ("<%s> -->QCFLT_FindControlDevice 0x%p\n", pDevExt->PortName, pFido)
   );

   
   QcAcquireSpinLock(&Control_DeviceLock, &levelOrHandle);

   if (!IsListEmpty(&Control_DeviceList))
   {
      headOfList = &Control_DeviceList;
      peekEntry = headOfList->Flink;

      while ((peekEntry != headOfList) && (foundIocDev == NULL))
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      FILTER_DEVICE_LIST,
                      List
                   );
         
         if (pIocDev->pFilterDeviceObject == pFido)
         {
            foundIocDev = pIocDev;
         }
         peekEntry = peekEntry->Flink;
      }  // while
   } // if

   QcReleaseSpinLock(&Control_DeviceLock, levelOrHandle);

   if (foundIocDev != NULL)
   {
      return foundIocDev->pControlDeviceObject;
   }
   else
   {
      return NULL;
   }
}  

/****************************************************************************
 *
 * function: QCFLT_FindControlDevice
 *
 * purpose:  Finds the control device from the global list.
 *
 * arguments: DeviceObject = pointer to the filter device object
 *                   pFilterDeviceInfo= Pointer to filter device info
 *
 * returns: Filter device object item
 *
 ****************************************************************************/
PFILTER_DEVICE_LIST 
QCFLT_FindFilterDevice
(
   PDEVICE_OBJECT    DeviceObject,
   PFILTER_DEVICE_INFO pFilterDeviceInfo
)
{
   PFILTER_DEVICE_LIST pIocDev = NULL, foundIocDev = NULL;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   PLIST_ENTRY     headOfList, peekEntry;
   KIRQL levelOrHandle;

   QCFLT_DbgPrint
   (
      DBG_LEVEL_DETAIL,
      ("<%s> -->QCFLT_FindFilterDevice 0x%p\n", pDevExt->PortName, pFilterDeviceInfo)
   );

   
   QcAcquireSpinLock(&Control_DeviceLock, &levelOrHandle);

   if (!IsListEmpty(&Control_DeviceList))
   {
      headOfList = &Control_DeviceList;
      peekEntry = headOfList->Flink;

      while ((peekEntry != headOfList) && (foundIocDev == NULL))
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      FILTER_DEVICE_LIST,
                      List
                   );

         QcReleaseSpinLock(&Control_DeviceLock, levelOrHandle);
         
         if ((RtlCompareUnicodeString(&(pIocDev->DeviceName), pFilterDeviceInfo->pDeviceName, TRUE) == 0) &&
             (RtlCompareUnicodeString(&(pIocDev->DeviceLinkName), pFilterDeviceInfo->pDeviceLinkName, TRUE) == 0))
         {
            foundIocDev = pIocDev;
         }

         QcAcquireSpinLock(&Control_DeviceLock, &levelOrHandle);         
         
         if (foundIocDev != NULL)
         {
            RtlCopyMemory(&foundIocDev->DispatchTable, pFilterDeviceInfo->MajorFunctions, sizeof(foundIocDev->DispatchTable));
            pFilterDeviceInfo->pControlDeviceObject = foundIocDev->pControlDeviceObject;
            pFilterDeviceInfo->pFilterDeviceObject = foundIocDev->pFilterDeviceObject;
            foundIocDev->pAdapter = pFilterDeviceInfo->pAdapter;
         }
         peekEntry = peekEntry->Flink;
      }  // while
   } // if

   QcReleaseSpinLock(&Control_DeviceLock, levelOrHandle);

   return foundIocDev;
}  // MPIOC_DeleteDevice

/****************************************************************************
 *
 * function: QCFLT_FreeControlDeviceList
 *
 * purpose:  Delete the full control device from the global list.
 *
 * arguments: none
 *
 * returns:  void
 *
 ****************************************************************************/
VOID 
QCFLT_FreeControlDeviceList()
{
   PFILTER_DEVICE_LIST pIocDev;
   PLIST_ENTRY     headOfList, peekEntry, nextEntry;
   KIRQL levelOrHandle;

   QcAcquireSpinLock(&Control_DeviceLock, &levelOrHandle);

   if (!IsListEmpty(&Control_DeviceList))
   {
      headOfList = &Control_DeviceList;
      peekEntry = headOfList->Flink;
      nextEntry = peekEntry->Flink;

      while (peekEntry != headOfList)
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      FILTER_DEVICE_LIST,
                      List
                   );
         
         RemoveEntryList(peekEntry);
         QcReleaseSpinLock(&Control_DeviceLock, levelOrHandle);

         if (pIocDev->pControlDeviceObject != NULL)
         {
            RtlFreeUnicodeString(&(pIocDev->DeviceLinkName));
            RtlFreeUnicodeString(&(pIocDev->DeviceName));
         }
         ExFreePool(pIocDev);

         QcAcquireSpinLock(&Control_DeviceLock, &levelOrHandle);
         
         peekEntry = nextEntry;
         nextEntry = nextEntry->Flink;
      }  // while
   } // if

   QcReleaseSpinLock(&Control_DeviceLock, levelOrHandle);
}

/****************************************************************************
 *
 * function: QCFLT_VendorRegistryProcess
 *
 * purpose:  Get the parameters from registry
 *
 * arguments:pDriverObject = pointer to the driver object
 *           PhysicalDeviceObject = pointer to a PDO created by the bus driver
 *           Fido = pointer to a PDO created by the bus driver
 *
 * returns:  NT status
 *
 ****************************************************************************/
NTSTATUS QCFLT_VendorRegistryProcess
(
   IN PDRIVER_OBJECT pDriverObject,
   IN PDEVICE_OBJECT PhysicalDeviceObject,
   IN PDEVICE_OBJECT Fido
)
{
   NTSTATUS       ntStatus  = STATUS_SUCCESS;
   HANDLE         hRegKey = NULL;
   UNICODE_STRING ucValueName;
   PDEVICE_EXTENSION pDevExt = Fido->DeviceExtension;

   ntStatus = IoOpenDeviceRegistryKey
              (
                 PhysicalDeviceObject,
                 PLUGPLAY_REGKEY_DRIVER,
                 KEY_ALL_ACCESS,
                 &hRegKey
              );
   if (!NT_SUCCESS(ntStatus))
   {
      return ntStatus;
   }

   // Get Debug Level
   pDevExt->DebugMask = 0;
   RtlInitUnicodeString(&ucValueName, VEN_DBG_MASK);
   ntStatus = getRegDwValueEntryData
              (
                 hRegKey,
                 ucValueName.Buffer,
                 &(pDevExt->DebugMask)
              );

   if (ntStatus != STATUS_SUCCESS)
   {
      pDevExt->DebugMask = 0;
   }

   // Enable logging by default
   pDevExt->DebugMask = 1;

    QCFLT_DbgPrint
    (   
        DBG_LEVEL_TRACE,
        ("MPFILTER: QCFLT_VendorRegistryProcess DebugMask 0x%x \n", pDevExt->DebugMask)
    );

   // Get Mux Support
   pDevExt->MuxSupport = 0;
   RtlInitUnicodeString(&ucValueName, VEN_MUX_ENABLED);
   ntStatus = getRegDwValueEntryData
              (
                 hRegKey,
                 ucValueName.Buffer,
                 &(pDevExt->MuxSupport)
              );

   if (ntStatus != STATUS_SUCCESS)
   {
      pDevExt->MuxSupport = 0;
   }

    QCFLT_DbgPrint
    (   
        DBG_LEVEL_TRACE,
        ("MPFILTER: QCFLT_VendorRegistryProcess MuxSupport 0x%x \n", pDevExt->MuxSupport)
    );

   // Get Iface Start Id
   pDevExt->StartRmnetIface = 0;
   RtlInitUnicodeString(&ucValueName, VEN_MUX_STARTRMNETIF);
   ntStatus = getRegDwValueEntryData
              (
                 hRegKey,
                 ucValueName.Buffer,
                 &(pDevExt->StartRmnetIface)
              );

   if (ntStatus != STATUS_SUCCESS)
   {
      pDevExt->StartRmnetIface = 0;
   }

    QCFLT_DbgPrint
    (   
        DBG_LEVEL_TRACE,
        ("MPFILTER: QCFLT_VendorRegistryProcess StartRmnetIface 0x%x \n", pDevExt->StartRmnetIface)
    );

   // Get Num Iface Id
   pDevExt->NumRmnetIface = 0;
   RtlInitUnicodeString(&ucValueName, VEN_MUX_NUMRMNETIF);
   ntStatus = getRegDwValueEntryData
              (
                 hRegKey,
                 ucValueName.Buffer,
                 &(pDevExt->NumRmnetIface)
              );

   if (ntStatus != STATUS_SUCCESS)
   {
      pDevExt->NumRmnetIface = 0;
   }

    QCFLT_DbgPrint
    (   
        DBG_LEVEL_TRACE,
        ("MPFILTER: QCFLT_VendorRegistryProcess NumRmnetIface 0x%x \n", pDevExt->NumRmnetIface)
    );

   // Get Num Mux Iface Ids
   pDevExt->NumMuxRmnetIface = 0;
   RtlInitUnicodeString(&ucValueName, VEN_MUX_NUM_MUX_RMNETIF);
   ntStatus = getRegDwValueEntryData
              (
                 hRegKey,
                 ucValueName.Buffer,
                 &(pDevExt->NumMuxRmnetIface)
              );

   if (ntStatus != STATUS_SUCCESS)
   {
      pDevExt->NumMuxRmnetIface = 0;
   }

    QCFLT_DbgPrint
    (   
        DBG_LEVEL_TRACE,
        ("MPFILTER: QCFLT_VendorRegistryProcess NumMuxRmnetIface 0x%x \n", pDevExt->NumMuxRmnetIface)
    );

   if (pDevExt->NumRmnetIface > 0) // NUmber of primary interfaces
   {
      pDevExt->NumRmnetIfaceCount = ((pDevExt->NumRmnetIface*pDevExt->NumMuxRmnetIface) + pDevExt->NumRmnetIface);
      pDevExt->NumRmnetIfaceVCount = pDevExt->NumRmnetIface*pDevExt->NumMuxRmnetIface;
   } 

    QCFLT_DbgPrint
    (   
        DBG_LEVEL_TRACE,
        ("MPFILTER: QCFLT_VendorRegistryProcess NumRmnetIfaceCount 0x%x and NumRmnetIfaceVCount 0x%x\n", pDevExt->NumRmnetIfaceCount, pDevExt->NumRmnetIfaceVCount)
    );

   _closeHandle( hRegKey, "PNP-2" );

   // Store the correct device name
   sprintf(pDevExt->PortName, "%s%d", gDeviceName, gDeviceIndex);
   InterlockedIncrement(&gDeviceIndex);
      
   return STATUS_SUCCESS;
} // QCFLT_VendorRegistryProcess

/******************************************************************************************
* Function Name: getRegDwValueEntryData
* Arguments:
* 
*  IN HANDLE OpenRegKey, 
*  IN PWSTR ValueEntryName,
*  OUT PULONG pValueEntryData
*
*******************************************************************************************/
NTSTATUS getRegDwValueEntryData
(
   IN HANDLE OpenRegKey, 
   IN PWSTR ValueEntryName,
   OUT PULONG pValueEntryData
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   PKEY_VALUE_FULL_INFORMATION pKeyValueInfo = NULL;

   ntStatus = GetValueEntry( OpenRegKey, ValueEntryName, &pKeyValueInfo );
   
   if (!NT_SUCCESS( ntStatus ))
   {
      goto getRegDwValueEntryData_Return;
   }
               
   *pValueEntryData = GetDwordField( pKeyValueInfo );

getRegDwValueEntryData_Return:

    if (pKeyValueInfo)
    {
       _ExFreePool( pKeyValueInfo );
       pKeyValueInfo = NULL;
    }

   return ntStatus;   
}

/******************************************************************************************
* Function Name: getRegSzValueEntryData
* Arguments:
* 
*  IN HANDLE OpenRegKey, 
*  IN PWSTR ValueEntryName,
*  OUT PULONG pValueEntryData
*
*******************************************************************************************/
NTSTATUS getRegSzValueEntryData
(
   IN HANDLE OpenRegKey, 
   IN PWSTR ValueEntryName,
   OUT PCHAR pValueEntryData
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   PKEY_VALUE_FULL_INFORMATION pKeyValueInfo = NULL;

   ntStatus = GetValueEntry( OpenRegKey, ValueEntryName, &pKeyValueInfo );
   
   if (!NT_SUCCESS( ntStatus ))
   {
      goto getRegSzValueEntryData_Return;
   }
               
   GetSzField( pKeyValueInfo, pValueEntryData);

getRegSzValueEntryData_Return:

    if (pKeyValueInfo)
    {
       _ExFreePool( pKeyValueInfo );
       pKeyValueInfo = NULL;
    }

   return ntStatus;   
}

/************************************************************************
Routine Description:
   Return a KEY_VALUE information from an open registry node
      
Arguments:
   HANDLE hKey -- node key
   PWSTR FieldName -- name of value entry
   PKEY_VALUE_FULL_INFORMATION  *pKeyValInfo --  Key value information
   
Returns:
   NT Status
      
************************************************************************/    
NTSTATUS GetValueEntry( HANDLE hKey, PWSTR FieldName, PKEY_VALUE_FULL_INFORMATION  *pKeyValInfo )
{
   PKEY_VALUE_FULL_INFORMATION  pKeyValueInfo = NULL;
   UNICODE_STRING Keyname;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   ULONG ulReturnLength = 0;
   ULONG ulBuffLength = 0; 

   _zeroString( Keyname );

   RtlInitUnicodeString(&Keyname,   FieldName);

   ulBuffLength = sizeof( KEY_VALUE_FULL_INFORMATION );

   while (1)
   {
      pKeyValueInfo = (PKEY_VALUE_FULL_INFORMATION) _ExAllocatePool( 
         NonPagedPool, 
         ulBuffLength,
         "GetValueEntry - 2");     
 
      if ( pKeyValueInfo == NULL )
      {
         *pKeyValInfo = NULL;
         ntStatus = STATUS_NO_MEMORY;
         goto GetValueEntry_Return; 
      }

      ntStatus = ZwQueryValueKey( 
         hKey,
         &Keyname, 
         KeyValueFullInformation, 
         pKeyValueInfo,
         ulBuffLength,
         &ulReturnLength );

      if ( NT_SUCCESS(ntStatus) )
      {
         *pKeyValInfo = pKeyValueInfo;
         break;
      }
      else
      {
         if ( ntStatus == STATUS_BUFFER_OVERFLOW )   
         {
            _freeBuf( pKeyValueInfo );
            ulBuffLength = ulReturnLength;
            continue;
         }
         else
         {
            _freeBuf( pKeyValueInfo );
            *pKeyValInfo = NULL;
            break;
         }
   
      }
   }// while

GetValueEntry_Return:

  return ntStatus;
   
}

/************************************************************************
Routine Description:
   Return a DWORD value from an open registry node
      
Arguments:
   pKvi -- node key information
      
Returns:
   ULONG value of the value entry
      
************************************************************************/      
ULONG GetDwordField( PKEY_VALUE_FULL_INFORMATION pKvi )
{
   ULONG dwVal, *pVal;
  
   pVal = (PULONG)((PCHAR)pKvi + pKvi->DataOffset);
   dwVal = *pVal;
   return dwVal;
}

/************************************************************************
Routine Description:
   Return a Sz value from an open registry node
      
Arguments:
   pKvi -- node key information
      
Returns:
   Sz value of the value entry
      
************************************************************************/      
VOID GetSzField( PKEY_VALUE_FULL_INFORMATION pKvi, PCHAR ValueEntryDataSz  )
{
   UNICODE_STRING unicodeStr;
   ANSI_STRING    ansiStr;
   PWSTR strPtr = (PWSTR)((PCHAR)pKvi + pKvi->DataOffset);
   RtlInitUnicodeString( &unicodeStr, strPtr );
   RtlUnicodeStringToAnsiString(&ansiStr, &unicodeStr, TRUE);
   strcpy(ValueEntryDataSz, ansiStr.Buffer);
   RtlFreeAnsiString( &ansiStr );
}

/************************************************************************
Routine Description:
   Return void
      
************************************************************************/      
VOID QCFLT_PrintBytes
(
   PVOID Buf,
   ULONG len,
   ULONG PktLen,
   char *info,
   PDEVICE_EXTENSION x,
   ULONG DbgLevel
)
{
   ULONG nWritten;
   char  *buf, *p, *cBuf, *cp;
   char *buffer;
   ULONG count = 0, lastCnt = 0, spaceNeeded;
   ULONG i, j, s;
   ULONG nts;
   PCHAR dbgOutputBuffer;
   ULONG myTextSize = 1280;
   PDEVICE_EXTENSION pDevExt = x;

   #define SPLIT_CHAR '|'

   if (x != NULL)
   {
#ifdef EVENT_TRACING
      if (!((x->DebugMask !=0) && 
            (QCWPP_USER_LEVEL(WPP_DRV_MASK_CONTROL) >= DbgLevel))) 
   
      {
         return;
      }
#else
      if (((x->DebugMask & DbgMask) == 0) || (x->DebugLevel < DbgLevel))
      {
         return;
      }
#endif
   }

   // re-calculate text buffer size
   if (myTextSize < (len * 5 +360))
   {
      myTextSize = len * 5 +360;
   }

   buffer = (char *)Buf;

   dbgOutputBuffer = ExAllocatePool(NonPagedPool, myTextSize);
   if (dbgOutputBuffer == NULL)
{
      return;
   }

   RtlZeroMemory(dbgOutputBuffer, myTextSize);
   cBuf = dbgOutputBuffer;
   buf  = dbgOutputBuffer + 128;
   p    = buf;
   cp   = cBuf;

   if (PktLen < len)
   {
      len = PktLen;
   }

   sprintf(p, "\r\n\t   --- <%s> DATA %u/%u BYTES ---\r\n", info, len, PktLen);
   p += strlen(p);

   for (i = 1; i <= len; i++)
   {
      if (i % 16 == 1)
      {
         sprintf(p, "  %04u:  ", i-1);
         p += 9;
      }

      sprintf(p, "%02X ", (UCHAR)buffer[i-1]);
      if (isprint(buffer[i-1]) && (!isspace(buffer[i-1])))
      {
         *cp = buffer[i-1]; // sprintf(cp, "%c", buffer[i-1]);
      }
      else
      {
         *cp = '.'; // sprintf(cp, ".");
      }

      p += 3;
      cp += 1;

      if ((i % 16) == 8)
      {
         *p = *(p+1) = ' '; // sprintf(p, "  ");
         p += 2;
      }

      if (i % 16 == 0)
      {
         if (i % 64 == 0)
         {
            sprintf(p, " %c  %s\r\n\r\n", SPLIT_CHAR, cBuf);
         }
         else
         {
            sprintf(p, " %c  %s\r\n", SPLIT_CHAR, cBuf);
         }

       QCFLT_DbgPrint
       (
       DBG_LEVEL_VERBOSE,
       ("%s",buf)
       );

         RtlZeroMemory(dbgOutputBuffer, myTextSize);
         p = buf;
         cp = cBuf;
      }
   }

   lastCnt = i % 16;

   if (lastCnt == 0)
   {
      lastCnt = 16;
   }

   if (lastCnt != 1)
   {
      // 10 + 3*8 + 2 + 3*8 = 60 (full line bytes)
      spaceNeeded = (16 - lastCnt + 1) * 3;
      if (lastCnt <= 8)
      {
         spaceNeeded += 2;
      }
      for (s = 0; s < spaceNeeded; s++)
   {
         *p++ = ' '; // sprintf(p++, " ");
      }
      sprintf(p, " %c  %s\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n",
              SPLIT_CHAR, cBuf, info, len, PktLen);
     QCFLT_DbgPrint
     (
     DBG_LEVEL_VERBOSE,
     ("%s",buf)
     );
   }
   else
   {
      sprintf(buf, "\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n", info, len, PktLen);
     QCFLT_DbgPrint
     (
     DBG_LEVEL_VERBOSE,
      ("%s",buf)
     );
}

   ExFreePool(dbgOutputBuffer);

}  //USBUTL_PrintBytes


/************************************************************************
Routine Description:
   Return a DWORD value from an open registry node
      
Arguments:
   pKvi -- node key information
      
Returns:
   ULONG value of the value entry
      
************************************************************************/      
BOOLEAN USBPNP_ValidateConfigDescriptor
(
   PDEVICE_EXTENSION pDevExt,
   PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc
)
{
   if ((ConfigDesc->bLength ==0) ||
       (ConfigDesc->bLength > 9) ||
       (ConfigDesc->wTotalLength == 0) ||
       (ConfigDesc->wTotalLength < 9))
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> _ValidateConfigDescriptor: bad length: %uB, %uB\n",
           pDevExt->PortName, ConfigDesc->bLength, ConfigDesc->wTotalLength
         )
      );
      return FALSE;
   }

   if (ConfigDesc->bDescriptorType != 0x02)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> _ValidateConfigDescriptor: bad bDescriptorType 0x%x\n",
           pDevExt->PortName, ConfigDesc->bDescriptorType)
      );
      return FALSE;
   }

   return TRUE;

}  // USBPNP_ValidateConfigDescriptor

BOOLEAN USBPNP_ValidateDeviceDescriptor
(
   PDEVICE_EXTENSION      pDevExt,
   PUSB_DEVICE_DESCRIPTOR DevDesc
)
{
   if (DevDesc->bLength == 0)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> _ValidateDeviceDescriptor: 0 bLength\n", pDevExt->PortName)
      );
      return FALSE;
   }

   if (DevDesc->bDescriptorType != 0x01)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> _ValidateDeviceDescriptor: bad bDescriptorType 0x%x\n",
           pDevExt->PortName, DevDesc->bDescriptorType)
      );
      return FALSE;
   }
   return TRUE;
}  // USBPNP_ValidateDeviceDescriptor


#define MAX_INTERFACE 64

NTSTATUS USBPNP_SelectAndAddInterfaces
(
   IN PDEVICE_EXTENSION pDevExt,
   IN PURB pUrb
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   PUSB_INTERFACE_DESCRIPTOR pInterfaceDesc;
   ULONG lTotalInterfaces;
   USHORT tempLen;
   int i, j;

   PUSB_CONFIGURATION_DESCRIPTOR pConfigDesc = (PUSB_CONFIGURATION_DESCRIPTOR)pUrb->UrbControlDescriptorRequest.TransferBuffer;

	tempLen = pConfigDesc->wTotalLength;

	pConfigDesc->wTotalLength = pConfigDesc->wTotalLength + (sizeof(USB_INTERFACE_DESCRIPTOR)*(pDevExt->NumRmnetIfaceVCount));
	pUrb->UrbControlDescriptorRequest.TransferBufferLength = pConfigDesc->wTotalLength;
	pConfigDesc->bNumInterfaces += pDevExt->NumRmnetIfaceVCount;

	pInterfaceDesc = (PUSB_INTERFACE_DESCRIPTOR)((PCHAR)pConfigDesc + tempLen);

	for ( i=0; i < pDevExt->NumRmnetIface ; i++)
	{
	   pDevExt->InterfaceMuxList[i].PhysicalRmnetInterface.RmnetInterface = pDevExt->StartRmnetIface + i;
	   pDevExt->InterfaceMuxList[i].PhysicalRmnetInterface.FilterDevInfo = pDevExt;
       for(j=0; j< pDevExt->NumMuxRmnetIface; j++)
{
	      pInterfaceDesc->bLength = sizeof(USB_INTERFACE_DESCRIPTOR);
	      pInterfaceDesc->bDescriptorType = 0x04;
	      pInterfaceDesc->bInterfaceNumber = NUM_START_INTERFACEID + (i*pDevExt->NumMuxRmnetIface) + j; //TODO: check the boundary
		  // InterlockedIncrement(&gMuxDeviceIndex);
	      pInterfaceDesc->bAlternateSetting = 0x00;
	      pInterfaceDesc->bNumEndpoints = 0x00;
	      pInterfaceDesc->iInterface = 0x00;
	      pInterfaceDesc->bInterfaceClass = 0xff;
	      pInterfaceDesc->bInterfaceSubClass = 0xff;
	      pInterfaceDesc->bInterfaceProtocol = 0xff;
		  
		  pDevExt->InterfaceMuxList[i].MuxInterfaces[j].RmnetInterface = pInterfaceDesc->bInterfaceNumber;
  
		  pInterfaceDesc = (PUSB_INTERFACE_DESCRIPTOR)((PCHAR)pInterfaceDesc + sizeof(USB_INTERFACE_DESCRIPTOR));
	   }
}

   return ntStatus;
}  // USBPNP_SelectInterfaces

NTSTATUS QCFilterCreateFriendlyName
(
   PDEVICE_EXTENSION pDevExt,
   PDEVICE_OBJECT QCPhysicalDeviceObject
)
{
   NTSTATUS        nts = STATUS_UNSUCCESSFUL;
   ULONG           bufLen = MAX_NAME_LEN, resultLen = 0;
   CHAR            driverKey[512];
   UNICODE_STRING  friendlyNameU;
   PCHAR           pSwInstance = NULL;
   BOOLEAN         bMatched = FALSE;

#define LEFT_P L" ("
#define RIGHT_P L")"

   RtlZeroMemory(driverKey, 512);
   RtlZeroMemory(pDevExt->FriendlyNameHolder, bufLen*sizeof(WCHAR));

   nts = IoGetDeviceProperty
         (
            QCPhysicalDeviceObject,
            DevicePropertyFriendlyName,
            bufLen,
            (PVOID)pDevExt->FriendlyNameHolder,
            &resultLen
         );
   if (nts == STATUS_SUCCESS)
   {
      return nts;
   }
   else
   {
      // FriendlyName not found, create one
      bufLen = 512;
      nts = IoGetDeviceProperty
            (
               QCPhysicalDeviceObject,
               DevicePropertyDriverKeyName,
               bufLen,
               (PVOID)driverKey,
               &resultLen
            );

      if (nts == STATUS_SUCCESS)
      {
         PCHAR pStart, pEnd;

         QCFLT_DbgPrint
         (
            DBG_LEVEL_DETAIL,
            ("<%s> QCFilterCreateFriendlyName(SwKey): <%ws>\n", pDevExt->PortName, (PWCHAR)driverKey)
         );
         pStart = (PCHAR)driverKey;
         pEnd = pStart + resultLen;

         // look for '\', little-endian byte order: 0x5C 0x00
         while (pEnd > pStart)
         {
            if (*pEnd != 0x5C)  // look for '\'
            {
               pEnd--;
            }
            else
            {
               bMatched = TRUE;
               break;
            }
         }
         if (bMatched == TRUE)
         {
            pSwInstance = (pEnd + 2);
         }
      }

      bufLen = MAX_NAME_LEN;
      nts = IoGetDeviceProperty
            (
               QCPhysicalDeviceObject,
               DevicePropertyDeviceDescription,
               bufLen,
               (PVOID)pDevExt->FriendlyNameHolder,
               &resultLen
            );

      if (nts == STATUS_SUCCESS)
      {
         RtlStringCbCatW(pDevExt->FriendlyNameHolder, MAX_NAME_LEN, LEFT_P);
         RtlStringCbCatW(pDevExt->FriendlyNameHolder, MAX_NAME_LEN, (PCWSTR)pSwInstance);
         RtlStringCbCatW(pDevExt->FriendlyNameHolder, MAX_NAME_LEN, RIGHT_P);
         RtlInitUnicodeString(&friendlyNameU, pDevExt->FriendlyNameHolder);

         // Create FriendlyName in registry
         nts = QCFilterSetFriendlyName
               (
                  pDevExt,
                  QCPhysicalDeviceObject,
                  &friendlyNameU,
                  (PWCHAR)driverKey
               );
      }
   }

   return nts;
}  // QCFilterCreateFriendlyName

NTSTATUS QCFilterSetFriendlyName
(
   PDEVICE_EXTENSION pDevExt,
   PDEVICE_OBJECT    QCPhysicalDeviceObject,
   PUNICODE_STRING   FriendlyName,
   PWCHAR            TargetDriverKey
)
{
#define QC_NAME_LEN 1024
#define DEVICE_HW_KEY_ROOT L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\"
   NTSTATUS        nts;
   ULONG           bufLen, actualLen;
   CHAR            hwId[QC_NAME_LEN], swKey[QC_NAME_LEN], devKey[QC_NAME_LEN];
   PWCHAR          pId;
   DWORD           idLen, restLen, subKeyIdx, subKeyLen;
   int             selected = 0;
   LONG            regResult;
   HANDLE          hwKeyHandle;
   UNICODE_STRING  ucHwKeyPath;
   OBJECT_ATTRIBUTES oa;

   RtlZeroMemory(hwId,  QC_NAME_LEN);
   RtlZeroMemory(swKey, QC_NAME_LEN);
   RtlZeroMemory(devKey, QC_NAME_LEN);

   // get HW IDs
   bufLen = QC_NAME_LEN;
   nts = IoGetDeviceProperty
         (
            QCPhysicalDeviceObject,
            DevicePropertyHardwareID,
            bufLen,
            (PVOID)hwId,
            &actualLen
         );
   if (!NT_SUCCESS(nts))
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> QDBPNP_SetFriendlyName: IoGetDeviceProperty (HWID) failure\n", pDevExt->PortName)
      );
      return nts;
   }
   else
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_DETAIL,
         ("<%s> QDBPNP_SetFriendlyName: IoGetDeviceProperty (HWID) len: %u\n", pDevExt->PortName, actualLen)
      );
   }
   pId = (PWCHAR)hwId;
   restLen = actualLen;

   while ((idLen = wcsnlen(pId, restLen)) > 0)
   {
      if (idLen >= restLen) break;
      selected = 1;
      if (wcsstr(pId, L"REV_") != NULL)
      { selected = 0;
      }
      else
      {
         break;
      }
      pId += (idLen + 1); // including NULL
      restLen -= (idLen + 1);
   }
   if (selected == 0)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_DETAIL,
         ("<%s> QDBPNP_SetFriendlyName: failed to find devID\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   // HW ID: pId, idLen
   // construct full HW key
   nts = RtlStringCbCopyW((PWCHAR)devKey, QC_NAME_LEN, DEVICE_HW_KEY_ROOT);
   if (nts != STATUS_SUCCESS)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> QDBPNP_SetFriendlyName: HW key root failure\n", pDevExt->PortName)
      );
      return nts;
   }
   nts = RtlStringCbCatW((PWCHAR)devKey, QC_NAME_LEN, pId);
   if (nts != STATUS_SUCCESS)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> QDBPNP_SetFriendlyName: RtlStringCbCat failed\n", pDevExt->PortName)
      );
      return nts;
   }
   QCFLT_DbgPrint
   (
      DBG_LEVEL_DETAIL,
      ("<%s> QDBPNP_SetFriendlyName: devKey <%ws>\n", pDevExt->PortName, (PWCHAR)devKey)
   );

   // open HW key
   RtlInitUnicodeString(&ucHwKeyPath, (PWCHAR)devKey);
   InitializeObjectAttributes
   (
      &oa,
      &ucHwKeyPath,
      (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
      NULL, NULL
   );
   nts = ZwOpenKey
         (
            &hwKeyHandle,
            GENERIC_ALL,
            &oa
         );
   if (nts != STATUS_SUCCESS)
   {
      QCFLT_DbgPrint
      (
         DBG_LEVEL_ERROR,
         ("<%s> QDBPNP_SetFriendlyName: ZwOpenKey failed 0x%x\n", pDevExt->PortName, nts)
      );
      return nts;
   }
   // enum subkeys
   subKeyIdx = 0;
   while (nts == STATUS_SUCCESS)
   {
      CHAR           subKeyInfo[QC_NAME_LEN];
      UNICODE_STRING ucSubKeyPath;
      PKEY_BASIC_INFORMATION keyName;
      OBJECT_ATTRIBUTES      oa;

      RtlZeroMemory(subKeyInfo, QC_NAME_LEN);
      nts = ZwEnumerateKey
            (
               hwKeyHandle,
               subKeyIdx,
               KeyBasicInformation,
               (PVOID)subKeyInfo,
               QC_NAME_LEN,
               &subKeyLen
            );
      // open each subkey for R/W and find a match with SW key
      if (nts == STATUS_SUCCESS)
      {
         HANDLE subKeyHandle;
         CHAR   driverInfo[QC_NAME_LEN];
         DWORD  infoLen;
         UNICODE_STRING ucRegEntryName;

         keyName = (PKEY_BASIC_INFORMATION)subKeyInfo;
         QCFLT_DbgPrint
         (
            DBG_LEVEL_DETAIL,
            ("<%s> QDBPNP_SetFriendlyName: subKey[%d]: <%ws>\n", pDevExt->PortName, subKeyIdx, keyName->Name)
         );

         RtlInitUnicodeString(&ucSubKeyPath, keyName->Name);
         InitializeObjectAttributes
         (
            &oa,
            &ucSubKeyPath,
            (OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE),
            hwKeyHandle,
            NULL
         );
         nts = ZwOpenKey
               (
                  &subKeyHandle,
                  GENERIC_ALL,
                  &oa
               );
         if (nts != STATUS_SUCCESS)
         {
            QCFLT_DbgPrint
            (
               DBG_LEVEL_ERROR,
               ("<%s> QDBPNP_SetFriendlyName: ZwOpenKey failed (sub): 0x%x\n", pDevExt->PortName, nts)
            );
            break;
         }

         // retrieve driver key and match
         RtlZeroMemory(driverInfo, QC_NAME_LEN);
         RtlInitUnicodeString(&ucRegEntryName, L"Driver");
         nts = ZwQueryValueKey
               (
                  subKeyHandle,
                  &ucRegEntryName,
                  KeyValueFullInformation,
                  (PKEY_VALUE_FULL_INFORMATION)&driverInfo,
                  QC_NAME_LEN,
                  &infoLen
               );
         if (nts == STATUS_SUCCESS)
         {
            UNICODE_STRING swKey1, swKey2;
            PKEY_VALUE_FULL_INFORMATION pDriverKey;
            PCHAR pDrvKeyVal;

            pDriverKey = (PKEY_VALUE_FULL_INFORMATION)&driverInfo;
            pDrvKeyVal = (PCHAR)&driverInfo;
            pDrvKeyVal += pDriverKey->DataOffset;
            QCFLT_DbgPrint
            (
               DBG_LEVEL_DETAIL,
               ("<%s> QDBPNP_SetFriendlyName: retrived %uB swKey <%ws>vs<%ws>\n", pDevExt->PortName,
                 infoLen, (PWCHAR)pDrvKeyVal, TargetDriverKey)
            );
            RtlInitUnicodeString(&swKey1, TargetDriverKey);
            RtlInitUnicodeString(&swKey2, (PWCHAR)pDrvKeyVal);
            if (TRUE == RtlEqualUnicodeString(&swKey1, &swKey2, TRUE))
            {
               UNICODE_STRING ucSetEntryName;

               // set FriendlyName
               RtlInitUnicodeString(&ucSetEntryName, L"FriendlyName");
               nts = ZwSetValueKey
                     (
                        subKeyHandle,
                        &ucSetEntryName,
                        0,
                        REG_SZ,
                        FriendlyName->Buffer,
                        FriendlyName->Length+2
                     );
               if (!NT_SUCCESS(nts))
               {
                  QCFLT_DbgPrint
                  (
                     DBG_LEVEL_ERROR,
                     ("<%s> QDBPNP_SetFriendlyName: failed to set FriendlyName: 0x%x\n", pDevExt->PortName, nts)
                  );
               }
            }
         }
         else
         {
            QCFLT_DbgPrint
            (
               DBG_LEVEL_ERROR,
               ("<%s> QDBPNP_SetFriendlyName: ZwQueryValueKey (swKey) failure 0x%x\n", pDevExt->PortName, nts)
            );
         }
         ZwClose(subKeyHandle);
      }
      else
      {
         QCFLT_DbgPrint
         (
            DBG_LEVEL_ERROR,
            ("<%s> QDBPNP_SetFriendlyName: ZwEnumerateKey failed/exhausted 0x%x\n", pDevExt->PortName, nts)
         );
         if (nts == STATUS_NO_MORE_ENTRIES)
         {
            nts = STATUS_SUCCESS;
         }
         break;
      }
      subKeyIdx++;
   }  // while
   ZwClose(hwKeyHandle);

   return nts;
}  // QCFilterSetFriendlyName 
