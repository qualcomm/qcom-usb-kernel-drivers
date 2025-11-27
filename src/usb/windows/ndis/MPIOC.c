/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P I O C . C

GENERAL DESCRIPTION
    This modules contains functions to register/deregsiter a control-
    deviceobject for ioctl purposes and dispatch routine for handling
    ioctl requests from usermode.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#if defined(IOCTL_INTERFACE)

#include <stdio.h>
#include "MPMain.h"
#include "MPIOC.h"
#include "USBUTL.h"
#include "MPQCTL.h"
#include "MPUSB.h"
#include "MPQMUX.h"

// EVENT_TRACING
#ifdef EVENT_TRACING

#include "MPWPP.h"               // Driver specific WPP Macros, Globals, etc 
#include "MPIOC.tmh"

#endif   // EVENT_TRACING

#define MP_MAX_CRTL_RETRIES 6

// Global variables
extern NDIS_HANDLE MyNdisHandle;
LIST_ENTRY MP_DeviceList;
PNDIS_SPIN_LOCK MP_CtlLock = NULL;

// we borrow the definition from WDK here
extern NTKERNELAPI NTSTATUS ZwSetSecurityObject
(
   IN HANDLE                Handle,
   IN SECURITY_INFORMATION  SecurityInformation,
   IN PSECURITY_DESCRIPTOR  SecurityDescriptor
); 

NDIS_STATUS MPIOC_RegisterDevice
(
   PMP_ADAPTER Adapter,
   NDIS_HANDLE WrapperConfigurationContext
)
{
    NDIS_STATUS         Status = NDIS_STATUS_SUCCESS;

#if 0
    Status = MPIOC_RegisterAllDevices
             (
                WrapperConfigurationContext,
                Adapter,
                Adapter->NumClients  // MP_NUM_DEV
             );
#endif

    Status = MPIOC_RegisterFilterDevice
             (
                WrapperConfigurationContext,
                Adapter
             );

    if (Status != NDIS_STATUS_SUCCESS)
    {
       MPIOC_DeleteFilterDevice(Adapter, NULL, NULL); // delete all
    }

    return(Status);
}  // MPIOC_RegisterDevice

NTSTATUS MPIOC_IRPDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION  irpStack;
    NTSTATUS            status = STATUS_SUCCESS, Status;
    ULONG               inlen, outlen;
    PVOID               buffer;
    PMPIOC_DEV_INFO     pIocDev;
    PMP_ADAPTER         pAdapter;  // for DBG output
    UCHAR               mjFunc;
    KIRQL               irql = KeGetCurrentIrql();
    PFILE_OBJECT       fileObj;
    ULONG              QMIType = 0;
    ANSI_STRING        ansiString;

    irpStack = IoGetCurrentIrpStackLocation(Irp);
    
    fileObj = irpStack->FileObject;
    
    if (fileObj->FileName.Length != 0)
    {
       RtlUnicodeStringToAnsiString(&ansiString, &(fileObj->FileName), TRUE);
       if (RtlCharToInteger( (PCSZ)&(ansiString.Buffer[1]), 10, &QMIType) != STATUS_SUCCESS)
       {
          QCNET_DbgPrintG(("CIRP Cu 0x%p - invalid FileObject 0x%p\n", Irp, DeviceObject));
          RtlFreeAnsiString(&ansiString);
          Irp->IoStatus.Information = 0;
          Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
          IoCompleteRequest(Irp, IO_NO_INCREMENT);
          return STATUS_UNSUCCESSFUL;
       }
       RtlFreeAnsiString(&ansiString);
    }
    
    pIocDev = MPIOC_FindIoDevice(NULL, DeviceObject, NULL, NULL, Irp, QMIType);
    if (pIocDev == NULL)
    {
       QCNET_DbgPrintG(("CIRP Cu 0x%p - invalid DO 0x%p\n", Irp, DeviceObject));
       Irp->IoStatus.Information = 0;
       Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
       IoCompleteRequest(Irp, IO_NO_INCREMENT);
       return STATUS_UNSUCCESSFUL;
    }

    pAdapter = pIocDev->Adapter;

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
       ("<%s> CIRP => 0x%p\n", pAdapter->PortName, Irp)
    );

    irpStack = IoGetCurrentIrpStackLocation(Irp);
    Irp->IoStatus.Information = 0;
    mjFunc = irpStack->MajorFunction;

    if (MP_STATE_STOP(pIocDev) && (MPIOC_LocalProcessing(Irp) == FALSE))
    {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
          ("<%s> CIRP Cu 0x%p -MP stopped, rejected\n", pAdapter->PortName, Irp)
       );
       Irp->IoStatus.Information = 0;
       Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
       IoCompleteRequest(Irp, IO_NO_INCREMENT);
       return STATUS_UNSUCCESSFUL;
    }

    InterlockedIncrement(&(pIocDev->IrpCount));

    switch( irpStack->MajorFunction )
    {
       case IRP_MJ_CREATE:
       {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
             ("<%s> MPIOC: (ps 0x%p) IRP_MJ_CREATE to 0x%p (IOC 0x%p, Type %d) CSL %d/%d/%d\n", pAdapter->PortName,
             PsGetCurrentProcessId(), DeviceObject, pIocDev, pIocDev->Type, Irp->CurrentLocation, Irp->StackCount, DeviceObject->StackSize)
          );

#if 0 
          if (pIocDev->SecurityReset == 0)
          {
             status = STATUS_SUCCESS;
             InterlockedIncrement(&(pIocDev->DeviceOpenCount));
             break;
          }
#endif
          NdisAcquireSpinLock(MP_CtlLock);

          InterlockedIncrement(&(pIocDev->DeviceOpenCount));
#if 0          
          KeClearEvent(&pIocDev->ClientClosedEvent);
#endif
          NdisReleaseSpinLock(MP_CtlLock);

          if (((pAdapter->Flags & fMP_STATE_INITIALIZED) == 0) ||
              (pAdapter->Flags & fMP_ANY_FLAGS))
          {
             status = STATUS_UNSUCCESSFUL;
             QCNET_DbgPrint
             (
                MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                ("<%s> _CREATE failure: MP not initialized.\n", pAdapter->PortName)
             );
             break;
          }

          #ifdef QCMP_SUPPORT_CTRL_QMIC

          if ((pIocDev->Type == MP_DEV_TYPE_CLIENT) || (pIocDev->Type == MP_DEV_TYPE_EXTERNAL_CTL))

          #else

          if (pIocDev->Type == MP_DEV_TYPE_CLIENT)

          #endif  // QCMP_SUPPORT_CTRL_QMIC

          {
             if (pIocDev->Acquired == TRUE)
             {
                NdisAcquireSpinLock(MP_CtlLock);

                if (pIocDev->DeviceOpenCount == 1)
                {
                   pIocDev->IdleTime = 0;

                   if (pIocDev->ClientId != 0)
                   {
                      PFILE_OBJECT       fileObj;
                      fileObj = irpStack->FileObject;
                      fileObj->FsContext = (PVOID)pIocDev->ClientId;
                      pIocDev->FsContext = (PVOID)pIocDev->ClientId;
                   }

                   NdisReleaseSpinLock(MP_CtlLock);

                   // start the write thread
                   status = MPIOC_StartWriteThread(pIocDev);

                   // Do the bind
                   if ( ((pIocDev->QMIType == QMUX_TYPE_WDS)||
                         (pIocDev->QMIType == QMUX_TYPE_QOS)||
                         (pIocDev->QMIType == QMUX_TYPE_DFS)) &&
                         (pIocDev->ClientId != 0) )
                   {
                       USHORT ReqType = QMIWDS_BIND_MUX_DATA_PORT_REQ;
                       if (pIocDev->QMIType == QMUX_TYPE_QOS) 
                       {
                          ReqType = QMI_QOS_BIND_DATA_PORT_REQ;
                       }
                       if (pIocDev->QMIType == QMUX_TYPE_DFS) 
                       {
                          ReqType = QMIDFS_BIND_CLIENT_REQ;
                       }
                       MPQMUX_SetQMUXBindMuxDataPort( pAdapter, pIocDev, pIocDev->QMIType, ReqType);
                   }
                }
                else
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                      ("<%s> _CREATE failure: dev in use, cnt %u\n",
                        pAdapter->PortName, pIocDev->DeviceOpenCount)
                   );
                   NdisReleaseSpinLock(MP_CtlLock);
                   status = STATUS_DEVICE_BUSY;
                }
             }
             else
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                   ("<%s> MPIOC: IRP_MJ_CREATE to 0x%p, not acquired\n", pAdapter->PortName, DeviceObject)
                );
                status = STATUS_UNSUCCESSFUL;
             }
          }
          else
          {
                // control device
          }
          break;
       }

       case IRP_MJ_CLEANUP:
       {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
             ("<%s> MPIOC: IRP_MJ_CLEANUP to 0x%p IOC 0x%p (type %d)\n", pAdapter->PortName, DeviceObject, pIocDev, pIocDev->Type)
          );

#if 0
          if (pIocDev->SecurityReset == 0)
          {
             status = STATUS_SUCCESS;
             break;
          }
#endif

          if (pIocDev->Type == MP_DEV_TYPE_CONTROL)  // MTU service is provided by CONTROL device
          {
             if (pAdapter->MtuClient == irpStack->FileObject)
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                   ("<%s> MPIOC: IRP_MJ_CLEANUP: cancel V4/6 notify for MTU on IOC_CTL\n", pAdapter->PortName)
                );
                MPIOC_CancelNotificationIrp(pIocDev, IOCTL_QCDEV_IPV4_MTU_NOTIFY);
                MPIOC_CancelNotificationIrp(pIocDev, IOCTL_QCDEV_IPV6_MTU_NOTIFY);
                MPIOC_CancelNotificationIrp(pIocDev, IOCTL_QCDEV_WAIT_NOTIFY);
             }
          }
          else
          {
             QCNET_DbgPrint
             (
                MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                ("<%s> MPIOC: IRP_MJ_CLEANUP: cancel rm notify for IOC 0x%p fileObj 0x%p\n", pAdapter->PortName, pIocDev, irpStack->FileObject)
             );
             MPIOC_CancelNotificationIrp(pIocDev, IOCTL_QCDEV_WAIT_NOTIFY);
          }
          MPIOC_EmptyIrpCompletionQueue(pIocDev);

          #ifdef QCMP_SUPPORT_CTRL_QMIC

          if ((pIocDev->Type == MP_DEV_TYPE_CLIENT) || (pIocDev->Type == MP_DEV_TYPE_EXTERNAL_CTL))

          #else

          if (pIocDev->Type == MP_DEV_TYPE_CLIENT)

          #endif  // QCMP_SUPPORT_CTRL_QMIC
          {
             QCNET_DbgPrint
             (
                MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                ("<%s> MPIOC: IRP_MJ_CLEANUP cleanup Q's\n", pAdapter->PortName)
             );
             // cleanup I/O queues (ReadIrpQueue/WriteIrpQueue/ReadDataQueue)
             MPIOC_CleanupQueues(pIocDev, 0);
          }
/****************
          else if (pIocDev->Type == MP_DEV_TYPE_CONTROL)
          {
             NdisAcquireSpinLock(MP_CtlLock);
             if (pIocDev->GetServiceFileIrp != NULL)
             { 
                PIRP pIrp = pIocDev->GetServiceFileIrp;

                QCNET_DbgPrintG(("GetServiceFileIrp NULL for 0x%p - 3\n", pIocDev));
                pIocDev->GetServiceFileIrp = NULL;
                pIrp->IoStatus.Status = STATUS_CANCELLED;
                pIrp->IoStatus.Information = 0;
                NdisReleaseSpinLock(MP_CtlLock);
                IoCompleteRequest(pIrp, IO_NO_INCREMENT);
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                   ("<%s> CIRP Cc 0x%p(0x%x)\n", pAdapter->PortName, pIrp, STATUS_CANCELLED)
                );
             }
             else
             {
                NdisReleaseSpinLock(MP_CtlLock);
             }
          }
******************/
          break;
       }

       case IRP_MJ_CLOSE:
       {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
             ("<%s> MPIOC: (ps 0x%p) IRP_MJ_CLOSE to 0x%p/0x%p (CID %u/%u)\n", pAdapter->PortName,
               PsGetCurrentProcessId(), pIocDev, DeviceObject, pIocDev->Type, pIocDev->ClientId)
          );

#if 0
          if (pIocDev->SecurityReset == 0)
          {
             status = STATUS_SUCCESS;
             if (pIocDev->DeviceOpenCount > 0)
             {
                InterlockedDecrement(&(pIocDev->DeviceOpenCount));
             }
             break;
          }
#endif
          #ifdef QCMP_SUPPORT_CTRL_QMIC

          if ((pIocDev->Type == MP_DEV_TYPE_CLIENT) || (pIocDev->Type == MP_DEV_TYPE_EXTERNAL_CTL))

          #else

          if (pIocDev->Type == MP_DEV_TYPE_CLIENT)

          #endif  // QCMP_SUPPORT_CTRL_QMIC
          {

             #ifdef QCMP_SUPPORT_CTRL_QMIC
             if ((pAdapter->ExternalQCTLEnabled == TRUE) && (pIocDev->QMIType == 0))
             {
                if (pIocDev->ClientId != 0)
                {
                   pAdapter->CtrlDevAcquired = FALSE;
                   pIocDev->Type = MP_DEV_TYPE_CLIENT;
                }
             }
             else
             #endif // QCMP_SUPPORT_CTRL_QMIC
             {
                // Release client ID for TYPE_CLIENT
                MPQCTL_ReleaseClientId
                (
                   pIocDev->Adapter,
                   pIocDev->QMIType,
                   pIocDev  // Context
                );
             }

             pIocDev->Acquired = FALSE;
             pIocDev->ClientId = 0; // force to zero

             QCNET_DbgPrint
             (
                MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                ("<%s> MPIOC: _CLOSE to 0x%p/0x%p Acquired=FALSE\n",
                  pAdapter->PortName, DeviceObject, pIocDev)
             );

             // terminate the write worker thread
             MPIOC_CancelWriteThread(pIocDev);

             // Cleanup queues
             MPIOC_CleanupQueues(pIocDev, 1);
          }

          if (pIocDev->Type == MP_DEV_TYPE_CONTROL)  // MTU service is provided by CONTROL device
          {
             if (pAdapter->MtuClient == irpStack->FileObject)
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                   ("<%s> MPIOC: IRP_MJ_CLOSE: cancel V4/6 notify for MTU on IOC_CTL\n", pAdapter->PortName)
                );
                MPIOC_CancelNotificationIrp(pIocDev, IOCTL_QCDEV_IPV4_MTU_NOTIFY);
                MPIOC_CancelNotificationIrp(pIocDev, IOCTL_QCDEV_IPV6_MTU_NOTIFY);
                MPIOC_CancelNotificationIrp(pIocDev, IOCTL_QCDEV_WAIT_NOTIFY);
             }
          }
          else
          {
             QCNET_DbgPrint
             (
                MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                ("<%s> MPIOC: IRP_MJ_CLOSE: cancel rm notify for IOC 0x%p fileObj 0x%p\n", pAdapter->PortName, pIocDev, irpStack->FileObject)
             );
             MPIOC_CancelNotificationIrp(pIocDev, IOCTL_QCDEV_WAIT_NOTIFY);
          }
          MPIOC_EmptyIrpCompletionQueue(pIocDev);

          if (pIocDev->DeviceOpenCount > 0)
          {
             InterlockedDecrement(&(pIocDev->DeviceOpenCount));
          }

          InterlockedDecrement(&(pIocDev->IrpCount));

          if ((pIocDev->Type == MP_DEV_TYPE_CLIENT) && (pIocDev->DeviceOpenCount == 0) && (!MP_STATE_STOP(pIocDev)))
          {
             MPIOC_DeleteFilterDevice(pAdapter, pIocDev,NULL);
          }
          
          
          break;        
       }

       case IRP_MJ_DEVICE_CONTROL: 
       {
          QCNET_DbgPrint
          (
             MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
             ("<%s> MPIOC: IRP_MJ_DEVICE_CONTROL to 0x%p\n", pAdapter->PortName, DeviceObject)
          );

#if 0
          if (pIocDev->SecurityReset == 0)
          {
             status = STATUS_UNSUCCESSFUL;
             QCNET_DbgPrint
             (
                MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                ("<%s> MPIOC: failure - security not reset\n", pAdapter->PortName)
             );
             break;
          }
#endif          

          buffer = Irp->AssociatedIrp.SystemBuffer;  
          inlen = irpStack->Parameters.DeviceIoControl.InputBufferLength;
          outlen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

          switch( irpStack->Parameters.DeviceIoControl.IoControlCode )
          {
             case IOCTL_QCDEV_GET_SERVICE_FILE:
             {
                CHAR ClientFileName[IOCDEV_CLIENT_FILE_NAME_LEN];
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                   ("<%s> MPIOC: (ps 0x%p) IOCTL_QCDEV_GET_SERVICE_FILE to 0x%p devType 0x%x buf in(%uB) out(%uB)\n",
                     pAdapter->PortName, PsGetCurrentProcessId(), DeviceObject, pIocDev->Type, inlen, outlen)
                );

                if (pIocDev->Type == MP_DEV_TYPE_CONTROL)
                {
                   UCHAR qmiType = 0;
                   PMPIOC_DEV_INFO pFreeIocDev;
                   int retries = 0;

                   if ((inlen < sizeof(UCHAR)) || (buffer == NULL))
                   {
                      QCNET_DbgPrint
                      (
                         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                         ("<%s> MPIOC: IOCTL_QCDEV_GET_SERVICE_FILE buf too small\n",
                           pAdapter->PortName)
                      );
                      status = STATUS_INVALID_PARAMETER;
                      break;
                   }
                   qmiType = *((PUCHAR)buffer);

                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                      ("<%s> IOCTL_QCDEV_GET_SERVICE_FILE: QMI Type 0x%x\n", 
                        pAdapter->PortName, qmiType)
                   );

                   while (retries < 10)  // 10 retries
                   {
                      NdisAcquireSpinLock(MP_CtlLock);
                      if (pIocDev->GetServiceFileIrp != NULL)
                      {
                         NdisReleaseSpinLock(MP_CtlLock);

                         QCNET_DbgPrint
                         (
                            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                            ("<%s> _GET_SERVICE_FILE: GetServiceFileIrp error - dup req for 0x%p (IRP 0x%p, 0x%p)\n",
                              pAdapter->PortName, pIocDev, pIocDev->GetServiceFileIrp, Irp)
                         );
                         status = STATUS_TRANSACTIONAL_CONFLICT;
                         if (irql <= PASSIVE_LEVEL)
                         {
                            MPMAIN_Wait(-(2 * 1000 * 1000));  // 200ms
                            ++retries;
                            continue;  // back in loop and retry
                         }
                         else
                         {
                            retries = 9999;  // assign a bigger value to exit loop
                            continue;
                         }
                      }
                      pIocDev->GetServiceFileIrp = Irp;
                      NdisReleaseSpinLock(MP_CtlLock);
                      status = STATUS_SUCCESS;
                      retries = 9999;  // assign a bigger value to exit loop
                   }
                   if (status == STATUS_TRANSACTIONAL_CONFLICT)
                   {
                      break;  // out of switch
                   }

                   // Create the IOCDEV
                   //pFreeIocDev = MPIOC_FindFreeClientObject(DeviceObject);
                   
                   RtlStringCchPrintfExA( ClientFileName, 
                                          IOCDEV_CLIENT_FILE_NAME_LEN,
                                          NULL,
                                          NULL,
                                          STRSAFE_IGNORE_NULLS,
                                          "%s\\%d", 
                                          pIocDev->ClientFileName, 
                                          qmiType);
                   MPIOC_AddDevice
                   (
                      &pFreeIocDev,
                      MP_DEV_TYPE_CLIENT,
                      pAdapter,
                      NULL,
                      (PDEVICE_OBJECT)&(pIocDev->FilterDeviceInfo),
                      &(pIocDev->SymbolicName),
                      &(pIocDev->DeviceName),
                      ClientFileName
                   );
                   if (pFreeIocDev == NULL)
                   {
                      // out of resource
                      QCNET_DbgPrint
                      (
                         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                         ("<%s> IOCTL_QCDEV_GET_SERVICE_FILE: out of resource.\n", pAdapter->PortName)
                      );
                      status = STATUS_UNSUCCESSFUL;
                      NdisAcquireSpinLock(MP_CtlLock);
                      pIocDev->GetServiceFileIrp = NULL;
                      NdisReleaseSpinLock(MP_CtlLock);
                      break;
                   }

                   // Init QMI if it's not initialized
                   if (FALSE == MPMAIN_InitializeQMI(pAdapter, QMICTL_RETRIES))
                   { 
                      QCNET_DbgPrint
                      (
                         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                         ("<%s> IOCTL_QCDEV_GET_SERVICE_FILE: QMI init failure\n", pAdapter->PortName)
                      );
                      status = STATUS_UNSUCCESSFUL;
                      break;
                   }

                   #ifdef QCMP_SUPPORT_CTRL_QMIC

                   if ((pAdapter->ExternalQCTLEnabled == TRUE) && (qmiType == QMUX_TYPE_CTL))
                   {
                      // grant one ID for external CTRL client
                      if (pAdapter->CtrlDevAcquired == TRUE)
                      {
                         // fail the request
                         status = STATUS_UNSUCCESSFUL;
                         NdisAcquireSpinLock(MP_CtlLock);
                         pFreeIocDev->ClientId = 0;
                         pFreeIocDev->Acquired = FALSE;
                         pIocDev->GetServiceFileIrp = NULL;
                         NdisReleaseSpinLock(MP_CtlLock);
                      }
                      else
                      {
                         NdisAcquireSpinLock(MP_CtlLock);
                         pFreeIocDev->Type     = MP_DEV_TYPE_EXTERNAL_CTL;
                         pFreeIocDev->ClientId = 0x01;  // assign a bogus non-zero value
                         pFreeIocDev->QMIType  = QMUX_TYPE_CTL;
                         pFreeIocDev->Acquired = TRUE;
                         pAdapter->CtrlDevAcquired = TRUE;
                         NdisReleaseSpinLock(MP_CtlLock);
                      }
                   }
                   else
                   #endif // QCMP_SUPPORT_CTRL_QMIC
                   {
                      QCNET_DbgPrint
                      (
                         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                         ("<%s> MPIOC: _GET_SERVICE_FILE: get CID for 0x%p (IRP 0x%p)\n",
                           pAdapter->PortName, pFreeIocDev, Irp)
                      );
                      MPQCTL_GetClientId
                      (
                         pIocDev->Adapter,
                         qmiType,
                         pFreeIocDev  // Context
                      );
                   }

                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                      ("<%s> MPIOC: _GET_SERVICE_FILE: got CID %u for 0x%p\n",
                        pAdapter->PortName, pFreeIocDev->ClientId,  pFreeIocDev)
                   );

                   if (pFreeIocDev->ClientId != 0)
                   {
                      PUCHAR cf = strrchr((const char*)pIocDev->ClientFileName, '\\');

                      if (cf == NULL)
                      {
                         cf = pIocDev->ClientFileName;
                      }
                      else
                      {
                         ++cf;
                         RtlStringCchPrintfExA( ClientFileName, 
                                                IOCDEV_CLIENT_FILE_NAME_LEN,
                                                NULL,
                                                NULL,
                                                STRSAFE_IGNORE_NULLS,
                                                "%s\\%d", 
                                                cf, 
                                                qmiType);
                      }
                      QCNET_DbgPrint
                      (
                         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                         ("<%s> MPIOC: _GET_SERVICE_FILE for 0x%p/0x%p -(0x%p, 0x%p)\n",
                           pAdapter->PortName, DeviceObject, pFreeIocDev, pFreeIocDev->ClientFileName, ClientFileName)
                      );
                      if ((outlen >= strlen(ClientFileName)) && (strlen(ClientFileName) > 0))
                      {
                         strcpy(buffer, ClientFileName);
                         Irp->IoStatus.Information = strlen(ClientFileName);
                         status = STATUS_SUCCESS;
                         QCNET_DbgPrint
                         (
                            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                            ("<%s> MPIOC: _GET_SERVICE_FILE for 0x%p: succeeded\n",
                              pAdapter->PortName, pFreeIocDev)
                         );
                      }
                      else
                      {
                         QCNET_DbgPrint
                         (
                            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                            ("<%s> IOCTL_QCDEV_GET_SERVICE_FILE: buf too small, req %uB<%s>\n",
                              pAdapter->PortName, strlen(ClientFileName), ClientFileName)
                         );
                         status = STATUS_UNSUCCESSFUL;
                      }

                   }
                   else
                   {
                      status = STATUS_UNSUCCESSFUL;
                      QCNET_DbgPrint
                      (
                         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                         ("<%s> MPIOC: _GET_SERVICE_FILE for 0x%p: failure (Acquired=FALSE, GetServiceFileIrp=NULL)\n",
                           pAdapter->PortName, pFreeIocDev)
                      );
                   }

                   NdisAcquireSpinLock(MP_CtlLock);
                   pIocDev->GetServiceFileIrp = NULL;
                   if (pFreeIocDev->ClientId == 0)
                   {
                      pFreeIocDev->Acquired = FALSE;
                   }
                   else
                   {
                      pFreeIocDev->Acquired = TRUE;
                   }
                   NdisReleaseSpinLock(MP_CtlLock);
                }
                else
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                      ("<%s> MPIOC: IOCTL_QCDEV_GET_SERVICE_FILE wrong DO type\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                }

                break;
             }  // IOCTL_QCDEV_GET_SERVICE_FILE

             case IOCTL_QCDEV_QMI_GET_CLIENT_ID:
             {
                if ((buffer == NULL) || (outlen < sizeof(UCHAR)))
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                      ("<%s> MPIOC: insufficient input for GET_CID\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                   break;
                }
                #ifdef QCMP_SUPPORT_CTRL_QMIC

                if ((pIocDev->Type == MP_DEV_TYPE_CLIENT) || (pIocDev->Type == MP_DEV_TYPE_EXTERNAL_CTL))

                #else

                if (pIocDev->Type == MP_DEV_TYPE_CLIENT)

                #endif  // QCMP_SUPPORT_CTRL_QMIC
                {
                   PUCHAR pRetBuf = (PUCHAR)buffer;

                   *pRetBuf = pIocDev->ClientId;
                   Irp->IoStatus.Information = sizeof(UCHAR);
                }
                else
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                      ("<%s> MPIOC: requested wrong dev for GET_CID\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                }
                break;
             }

             case IOCTL_QCDEV_QMI_GET_SVC_VER:
             {
                UCHAR qmiType = 0;
                PQMI_SERVICE_VERSION verPtr;

                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_CRITICAL,
                   ("<%s> MPIOC: GET_SVC_VER: in(%uB) out(%uB)\n",
                     pAdapter->PortName, inlen, outlen)
                );


                if ((inlen < sizeof(UCHAR)) || (buffer == NULL) || (outlen < sizeof(ULONG)) )
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                      ("<%s> MPIOC: IOCTL_QCDEV_QMI_GET_SVC_VER buf too small\n",
                        pAdapter->PortName)
                   );
                   status = STATUS_INVALID_PARAMETER;
                   break;
                }
                qmiType = *((PUCHAR)buffer);
                if (qmiType >= QMUX_TYPE_MAX)
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                      ("<%s> MPIOC: IOCTL_QCDEV_QMI_GET_SVC_VER: bad QMI type %u\n",
                        pAdapter->PortName, qmiType)
                   );
                   status = STATUS_INVALID_PARAMETER;
                   break;
                }
                verPtr = (PQMI_SERVICE_VERSION)buffer;
                verPtr->Major = pAdapter->QMUXVersion[qmiType].Major;
                verPtr->Minor = pAdapter->QMUXVersion[qmiType].Minor;
                Irp->IoStatus.Information = sizeof(ULONG);
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_CRITICAL,
                   ("<%s> MPIOC: GET_SVC_VER: type(%u) (Major 0x%x Minor 0x%x)\n",
                     pAdapter->PortName, qmiType, verPtr->Major, verPtr->Minor)
                );
                break;
             }

             case IOCTL_QCDEV_QMI_GET_SVC_VER_EX:
             {
                UCHAR qmiType = 0;
                PQMI_SERVICE_VERSION verPtr;
                PCHAR pLabel;
                int labelLen;

                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_CRITICAL,
                   ("<%s> MPIOC: GET_SVC_VER_EX: in(%uB) out(%uB)\n",
                     pAdapter->PortName, inlen, outlen)
                );


                if ((inlen < sizeof(UCHAR)) || (buffer == NULL) || (outlen < sizeof(QMI_SERVICE_VERSION)) )
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                      ("<%s> MPIOC: IOCTL_QCDEV_QMI_GET_SVC_VER_EX buf too small\n",
                        pAdapter->PortName)
                   );
                   status = STATUS_INVALID_PARAMETER;
                   break;
                }
                qmiType = *((PUCHAR)buffer);
                if (qmiType >= QMUX_TYPE_MAX)
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                      ("<%s> MPIOC: IOCTL_QCDEV_QMI_GET_SVC_VER_EX: bad QMI type %u\n",
                        pAdapter->PortName, qmiType)
                   );
                   status = STATUS_INVALID_PARAMETER;
                   break;
                }

                MPQCTL_GetQMICTLVersion(pAdapter, FALSE);
                
                verPtr = (PQMI_SERVICE_VERSION)buffer;
                verPtr->Major = pAdapter->QMUXVersion[qmiType].Major;
                verPtr->Minor = pAdapter->QMUXVersion[qmiType].Minor;
                verPtr->AddendumMajor = pAdapter->QMUXVersion[qmiType].AddendumMajor;
                verPtr->AddendumMinor = pAdapter->QMUXVersion[qmiType].AddendumMinor;

                labelLen = outlen - sizeof(QMI_SERVICE_VERSION);
                if (labelLen > 0)
                {
                   pLabel = (PCHAR)buffer + sizeof(QMI_SERVICE_VERSION);
                   if (labelLen > 255)
                   {
                      labelLen = 255;
                   }
                   RtlCopyMemory(pLabel, pAdapter->AddendumVersionLabel, labelLen); 
                   Irp->IoStatus.Information = sizeof(QMI_SERVICE_VERSION) + labelLen;
                }
                else
                {
                   Irp->IoStatus.Information = sizeof(QMI_SERVICE_VERSION);
                }
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_CRITICAL,
                   ("<%s> MPIOC: GET_SVC_VER_EX: type(%u) (Major 0x%x::0x%x Minor 0x%x::0x%x)\n",
                     pAdapter->PortName, qmiType, verPtr->Major, verPtr->AddendumMajor,
                     verPtr->Minor, verPtr->AddendumMinor)
                );
                break;
             }

             case IOCTL_QCDEV_SET_DBG_UMSK:
             {
                // set USB debug mask
                if ((buffer == NULL) || (inlen < sizeof(LONG)))
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                      ("<%s> MPIOC: insufficient input for UMSK\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                   break;
                }
                if (pIocDev->Type == MP_DEV_TYPE_CONTROL)
                {
                   // Set debug mask for MP layer
                   ULONG newMask = *((PULONG)buffer);
                   // NdisMoveMemory(buffer, (PVOID)&(pIocDev->Adapter->DebugMask), sizeof(ULONG));
#ifndef EVENT_TRACING
                   pIocDev->Adapter->DebugMask  = (newMask & MP_DBG_MASK_MASK);
                   pIocDev->Adapter->DebugLevel = (newMask & MP_DBG_LEVEL_MASK);
#else
                   if (newMask >= 0x01)
                   {
                      pIocDev->Adapter->DebugMask    = 0x01;
                   }
                   else
                   {
                      pIocDev->Adapter->DebugMask  = 0x00;
                   }
#endif

                   // Set debug mask for USB lager
                   USBIF_SetDebugMask(pIocDev->Adapter->USBDo, buffer);
                   Irp->IoStatus.Information = sizeof(ULONG);
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_FORCE, 
                      ("<%s> MP: new DbgMask 0x%x(L%d)\n", pAdapter->PortName,
                        pAdapter->DebugMask, pAdapter->DebugLevel)
                   );
                }
                break;
             }  // IOCTL_QCDEV_SET_DBG_UMSK

             case IOCTL_QCDEV_WAIT_NOTIFY:
             {
                // to handle RM notification for all clients (inlcuding MTU client)
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                   ("<%s> MPIOC: IOCTL_QCDEV_WAIT_NOTIFY, fileObj 0x%p Ioc 0x%p\n", pAdapter->PortName, irpStack->FileObject, pIocDev)
                );

                if (pIocDev->Type == MP_DEV_TYPE_CONTROL)
                {
                   // So, this requires MTU notification to be registered before the removal notification
                   if (pAdapter->MtuClient == irpStack->FileObject)
                   {
                      status = MPIOC_CacheNotificationIrp(pIocDev, Irp, irpStack->Parameters.DeviceIoControl.IoControlCode);
                   }
                   else
                   {
                      // restriction: we now disallow a non-MTU client to register notification on CONTROL device
                      status = STATUS_UNSUCCESSFUL;
                   }
                }
                else
                {
                   // Allow removal notification to be registered on non-CONTROL device
                   status = MPIOC_CacheNotificationIrp(pIocDev, Irp, irpStack->Parameters.DeviceIoControl.IoControlCode);
                }

                if (status == STATUS_PENDING)
                {
                   goto MPIOC_Dispatch_Done; // don't touch the "pending" IRP!
                }
                Irp->IoStatus.Status = status;

                break;
             }
             case IOCTL_QCDEV_IPV4_MTU_NOTIFY:
             case IOCTL_QCDEV_IPV6_MTU_NOTIFY:
             {
                // to handle MTU notifications only
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                   ("<%s> MPIOC: IOCTL_QCDEV_WAIT_NOTIFY, fileObj 0x%p Ioc 0x%p\n", pAdapter->PortName, irpStack->FileObject, pIocDev)
                );

                if (pIocDev->Type == MP_DEV_TYPE_CONTROL)  // MTU service is provided by CONTROL device
                {
                   // Enforce MTU notification be registered on CONTROL device only
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                      ("<%s> MPIOC: QCDEV_WAIT_NOTIFY: cache RM/V4/6 notify fileObj for MTU on IOC_CTL\n", pAdapter->PortName)
                   );
                   pAdapter->MtuClient = irpStack->FileObject;
                   status = MPIOC_CacheNotificationIrp(pIocDev, Irp, irpStack->Parameters.DeviceIoControl.IoControlCode);
                }
                else
                {
                   // Disallow any QMI client to register MTU notification
                   status = STATUS_UNSUCCESSFUL;
                }

                if (status == STATUS_PENDING)
                {
                   goto MPIOC_Dispatch_Done; // don't touch the "pending" IRP!
                }
                Irp->IoStatus.Status = status;

                break;
             }  // IOCTL_QCDEV_WAIT_NOTIFY

             case IOCTL_QCDEV_QMI_GET_MEID:
             {
                if ((buffer == NULL) || (strlen(pAdapter->DeviceSerialNumberMEID) == 0 ) ||
                    (outlen < (strlen(pAdapter->DeviceSerialNumberMEID)+1)))
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                      ("<%s> MPIOC: insufficient input for GET_MEID\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                   break;
                }
                else
                {
                   PUCHAR pRetBuf = (PUCHAR)buffer;
                   strcpy(pRetBuf,pAdapter->DeviceSerialNumberMEID);
                   Irp->IoStatus.Information = strlen(pAdapter->DeviceSerialNumberMEID);
                }
                break;
             }
             
             case IOCTL_QCDEV_DEVICE_REMOVE_EVENTA:
             {
                if ((buffer == NULL) || (inlen == 0) || (outlen == 0) || (outlen < sizeof(ULONGLONG)) || (pAdapter->NamedEventsIndex >= DEVICE_REMOVAL_EVENTS_MAX))
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                      ("<%s> MPIOC: insufficient input for EVENT_NAME\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                   break;
                }
                else
                {
                   CHAR EventNameBuf[256];
                   CHAR EventNameBuf1[256];
                   ANSI_STRING ansiStr;
                   UNICODE_STRING unicodeStr;
                   RtlZeroMemory(EventNameBuf, 256);
                   RtlZeroMemory(EventNameBuf1, 256);
                   RtlCopyMemory(EventNameBuf, buffer, inlen);
                   sprintf(EventNameBuf1, "\\BaseNamedObjects\\%s", EventNameBuf);
                   RtlInitAnsiString( &ansiStr, EventNameBuf1);
                   RtlAnsiStringToUnicodeString(&unicodeStr, &ansiStr, TRUE);
                   pAdapter->NamedEvents[pAdapter->NamedEventsIndex] = IoCreateNotificationEvent(&unicodeStr, &(pAdapter->NamedEventsHandle[pAdapter->NamedEventsIndex]));
                   if (pAdapter->NamedEvents[pAdapter->NamedEventsIndex] != NULL) 
                   { 
                      // Take a reference out on the object so that it does not go away if the application
                      //  goes away unexpectedly
                      ObReferenceObject(pAdapter->NamedEvents[pAdapter->NamedEventsIndex]);
                   
                      (*(ULONGLONG *)buffer) = (ULONGLONG)pAdapter->NamedEventsHandle[pAdapter->NamedEventsIndex];
                      Irp->IoStatus.Information = sizeof(ULONGLONG);
                      status = STATUS_SUCCESS;
                      pAdapter->NamedEventsIndex++;
                   } 
                   else 
                   {
                      status = STATUS_UNSUCCESSFUL;
                   }
                   RtlFreeUnicodeString( &unicodeStr );
                }
                break;
             }
             
             case IOCTL_QCDEV_DEVICE_REMOVE_EVENTW:
             {
                if ((buffer == NULL) || (inlen == 0) || (outlen == 0) || (outlen < sizeof(ULONGLONG)) || (pAdapter->NamedEventsIndex >= DEVICE_REMOVAL_EVENTS_MAX))
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                      ("<%s> MPIOC: insufficient input for EVENT_NAME\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                   break;
                }
                else
                {
                   WCHAR EventNameBuf[256];
                   WCHAR EventNameBuf1[256];
                   UNICODE_STRING unicodeStr;
                   RtlZeroMemory(EventNameBuf, 256*sizeof(WCHAR));
                   RtlZeroMemory(EventNameBuf1, 256*sizeof(WCHAR));
                   RtlCopyMemory(EventNameBuf, buffer, inlen);
                   swprintf(EventNameBuf1, L"\\BaseNamedObjects\\%s", EventNameBuf);
                   RtlInitUnicodeString( &unicodeStr, EventNameBuf1);
                   pAdapter->NamedEvents[pAdapter->NamedEventsIndex] = IoCreateNotificationEvent(&unicodeStr, &(pAdapter->NamedEventsHandle[pAdapter->NamedEventsIndex]));
                   if (pAdapter->NamedEvents[pAdapter->NamedEventsIndex] != NULL) 
                   { 
                      // Take a reference out on the object so that it does not go away if the application
                      //  goes away unexpectedly
                      ObReferenceObject(pAdapter->NamedEvents[pAdapter->NamedEventsIndex]);

                      (*(ULONGLONG *)buffer) = (ULONGLONG)pAdapter->NamedEventsHandle[pAdapter->NamedEventsIndex];
                      Irp->IoStatus.Information = sizeof(ULONGLONG);
                      status = STATUS_SUCCESS;
                      pAdapter->NamedEventsIndex++;
                   } 
                   else 
                   {
                      status = STATUS_UNSUCCESSFUL;
                   }
                }
                break;
             }
             
             case IOCTL_QCDEV_DEVICE_EVENT_CLOSE:
             {
                if ((buffer == NULL) || (inlen == 0) || (inlen < sizeof(ULONGLONG)))
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                      ("<%s> MPIOC: insufficient input for EVENT_NAME\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                   break;
                }
                else
                {
                   //Close the device removal event
                   LONG Count,Move = 0;
                   for (Count = 0; Count < pAdapter->NamedEventsIndex; Count++)
                   {
                      if (pAdapter->NamedEvents[Count] != NULL && ((*(ULONGLONG *)buffer) == (ULONGLONG)pAdapter->NamedEventsHandle[Count]))
                      {
                         _closeHandleG(pAdapter->PortName, pAdapter->NamedEventsHandle[Count], "MPHALT");
                         
                         // Release our reference
                         ObDereferenceObject(pAdapter->NamedEvents[Count]);
                         pAdapter->NamedEvents[Count] = NULL;
                         pAdapter->NamedEventsHandle[Count] = NULL;
                         Move = 1;
                         continue;
                      }
                      if (Move == 1)
                      {
                         pAdapter->NamedEvents[Count - 1] = pAdapter->NamedEvents[Count];
                         pAdapter->NamedEventsHandle[Count - 1] = pAdapter->NamedEvents[Count];
                      }
                   }
                   if (Move == 1)
                   {
                      pAdapter->NamedEventsIndex--;
                      status = STATUS_SUCCESS;
                   }
                   else
                   {
                      status = STATUS_UNSUCCESSFUL;
                   }
                }
                break;
             }
             
#ifdef QCUSB_MUX_PROTOCOL

             case IOCTL_QCDEV_RESUME_DL:
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                   ("<%s> MPIOC: IOCTL_QCDEV_RESUME_DL\n", pAdapter->PortName)
                );
                if (pAdapter->DLPauseEnabled == TRUE)
                {
                   KeSetEvent(&pAdapter->MPThreadDLResumeEvent, IO_NO_INCREMENT, FALSE);
                }
                break;
             }

#endif // QCUSB_MUX_PROTOCOL

#if defined(QCMP_QMAP_V2_SUPPORT) || defined(QCMP_QMAP_V1_SUPPORT)

             case IOCTL_QCDEV_RESUME_QMAP_DL:
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                   ("<%s> MPIOC: IOCTL_QCDEV_RESUME_QMAP_DL\n", pAdapter->PortName)
                );
                KeSetEvent(&pAdapter->MPThreadQMAPDLResumeEvent, IO_NO_INCREMENT, FALSE);
                break;
             }

             case IOCTL_QCDEV_PAUSE_QMAP_DL:
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                   ("<%s> MPIOC: IOCTL_QCDEV_PAUSE_QMAP_DL\n", pAdapter->PortName)
                );
                KeSetEvent(&pAdapter->MPThreadQMAPDLPauseEvent, IO_NO_INCREMENT, FALSE);
                break;
             }
#endif

            case IOCTL_QCDEV_INJECT_PACKET:
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                  ("<%s> MPIOC: IOCTL_QCDEV_INJECT_PACKET\n", pAdapter->PortName)
               );
               
               if ( (buffer == NULL) || (inlen == 0) )
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                     ("<%s> MPIOC: insufficient input for IOCTL_QCDEV_INJECT_PACKET\n", pAdapter->PortName)
                  );
                  status = STATUS_UNSUCCESSFUL;
                  break;
               }
               
               USBIF_InjectPacket(pAdapter->USBDo, buffer, inlen);
               status = STATUS_SUCCESS;
               break;
            }

#ifdef QCMP_DISABLE_QMI

             case IOCTL_QCDEV_MEDIA_CONNECT:
             {
                if (pAdapter->DisableQMI == 1)
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                      ("<%s> MPIOC: IOCTL_QCDEV_MEDIA_CONNECT\n", pAdapter->PortName)
                   );

                   pAdapter->IPv4DataCall = 1;
                   pAdapter->IPv6DataCall = 1;
                   pAdapter->IPSettings.IPV4.Address = 0xAB30A8C0; // IP-192.168.48.171 - endianess
                   
                   if ( (buffer == NULL) || (inlen < sizeof( ULONG )) )
                   {
                      QCNET_DbgPrint
                      (
                         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                         ("<%s> MPIOC: insufficient input for IOCTL_QCDEV_MEDIA_CONNECT\n", pAdapter->PortName)
                      );
                   }
                   else
                   {
                      ULONG IpAddress = *((PULONG)buffer);
                      if (IpAddress != 0x00)
                      {
                         pAdapter->IPSettings.IPV4.Address = IpAddress;
                      }
                   }
                   
                   pAdapter->IPSettings.IPV4.Flags = 1;
                   pAdapter->IPSettings.IPV6.Flags = 1;                   
                   KeSetEvent(&pAdapter->MPThreadMediaConnectEvent, IO_NO_INCREMENT, FALSE);
                   // KeSetEvent(&pAdapter->MPThreadMediaConnectEventIPv6, IO_NO_INCREMENT, FALSE);
                }
                break;
             }

             case IOCTL_QCDEV_MEDIA_DISCONNECT:
             {
                PMP_ADAPTER returnAdapter;
                if (pAdapter->DisableQMI == 1)
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                      ("<%s> MPIOC: IOCTL_QCDEV_MEDIA_DISCONNECT\n", pAdapter->PortName)
                   );

                   pAdapter->IPv4DataCall = 0;
                   pAdapter->IPv6DataCall = 0;
                   pAdapter->IPSettings.IPV4.Address = 0;
                   pAdapter->IPSettings.IPV4.Flags = 0;
                   pAdapter->IPSettings.IPV6.Flags = 0;                   
                
                   pAdapter->ulMediaConnectStatus = NdisMediaStateDisconnected;
                   MPMAIN_MediaDisconnect(pAdapter, TRUE);
                
                   MPMAIN_ResetPacketCount(pAdapter);
                   if ( TRUE == DisconnectedAllAdapters(pAdapter, &returnAdapter))
                   {
                      USBIF_PollDevice(returnAdapter->USBDo, FALSE);
                      // USBIF_CancelWriteThread(returnAdapter->USBDo);
                   }
                }
                break;
             }
#endif      
             case IOCTL_QCDEV_DEVICE_GROUP_INDETIFIER:
             {
                if ((buffer == NULL) || (outlen == 0) || (outlen < sizeof(ULONGLONG)))
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR, 
                      ("<%s> MPIOC: insufficient input for IOCTL_QCDEV_DEVICE_GROUP_INDETIFIER\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                }
                else
                {
                   (*(ULONGLONG *)buffer) = (ULONGLONG)pIocDev->FilterDeviceInfo.pFilterDeviceObject;
                   Irp->IoStatus.Information = sizeof(ULONGLONG);
                   status = STATUS_SUCCESS;
                }
                break;
             }

             case IOCTL_QCDEV_QMI_READY:
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                   ("<%s> MPIOC: IOCTL_QCDEV_QMI_READY (Init/Working) -- %d/%d\n", pAdapter->PortName, pAdapter->QmiInitialized, pAdapter->IsQmiWorking)
                );

                Irp->IoStatus.Information = 0;
                if (pAdapter->IsQmiWorking == FALSE)
                {
                   status = STATUS_UNSUCCESSFUL;
                }
                else
                {
                   status = STATUS_SUCCESS;
                }
                break;
             }

             case IOCTL_QCDEV_GET_PEER_DEV_NAME:
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                   ("<%s> MPIOC: IOCTL_QCDEV_GET_PEER_DEV_NAME\n", pAdapter->PortName)
                );
                status = MPIOC_GetPeerDeviceName(pAdapter, Irp, outlen);
                break;
             }

             case IOCTL_QCDEV_GET_PRIMARY_ADAPTER_NAME:
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                   ("<%s> MPIOC: IOCTL_QCDEV_GET_PRIMARY_ADAPTER_NAME\n", pAdapter->PortName)
                );
                status = MPIOC_GetPrimaryAdapterName(pAdapter, Irp, outlen);
                break;
             }

             case IOCTL_QCDEV_GET_QMI_QUEUE_SIZE:
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                   ("<%s> MPIOC: IOCTL_QCDEV_GET_QMI_QUEUE_SIZE\n", pAdapter->PortName)
                );
                if ((buffer == NULL) || (outlen == 0) || (outlen < sizeof(LONG)))
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                      ("<%s> MPIOC: insufficient input for IOCTL_QCDEV_GET_QMI_QUEUE_SIZE\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                }
                else if (pIocDev->Type == MP_DEV_TYPE_CLIENT)
                {
                   (*(LONG *)buffer) = pAdapter->PendingCtrlRequests[pIocDev->QMIType];
                   Irp->IoStatus.Information = sizeof(LONG);
                   status = STATUS_SUCCESS;
                }
                else
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                      ("<%s> MPIOC: IOCTL_QCDEV_GET_QMI_QUEUE_SIZE not for dev type %d\n",
                        pAdapter->PortName, pIocDev->Type)
                   );
                   status = STATUS_UNSUCCESSFUL;
                }
                break;
             }

             case IOCTL_QCDEV_PURGE_QMI_QUEUE:
             {
                MPIOC_PurgeQueue
                (
                   pIocDev,
                   &pIocDev->WriteIrpQueue,
                   &pIocDev->IoLock,
                   5
                );
                pAdapter->PendingCtrlRequests[pIocDev->QMIType] = 0;
                status = STATUS_SUCCESS;

                break;
             }

             case IOCTL_QCDEV_GET_NET_STATISTICS:
             {
                NDIS_STATISTICS_INFO StatisticsInfo;
                QC_XFER_STATISTICS   usbXferStats;
                PDEVICE_EXTENSION    pDevExt;
                PCHAR p;

                pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;                
                p = (PCHAR)buffer;

                if ((buffer == NULL) || (inlen < sizeof(LONG)))
                {
                   pAdapter->DropDataTest = 0;
                }
                else
                {
                   pAdapter->DropDataTest = *((PULONG)buffer);
                }

                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                   ("<%s> MPIOC: IOCTL_QCDEV_GET_NET_STATISTICS: DropDataTest %ld\n", pAdapter->PortName, pAdapter->DropDataTest)
                );
                if ((buffer == NULL) || (outlen == 0) || (outlen < (RX_THREAD_COUNT+sizeof(NDIS_STATISTICS_INFO)+sizeof(QC_XFER_STATISTICS))))
                {
                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                      ("<%s> MPIOC: insufficient input for IOCTL_QCDEV_GET_NET_STATISTICS\n", pAdapter->PortName)
                   );
                   status = STATUS_UNSUCCESSFUL;
                }
                else
                {
                   NdisZeroMemory(&StatisticsInfo, NDIS_SIZEOF_STATISTICS_INFO_REVISION_1);
                   StatisticsInfo.ifHCInUcastPkts =  pAdapter->GoodReceives;
                   StatisticsInfo.ifHCInMulticastPkts =  pAdapter->GoodReceives;
                   StatisticsInfo.ifHCInBroadcastPkts =  pAdapter->GoodReceives;
                   StatisticsInfo.ifHCInOctets  = pAdapter->RxBytesGood;
                   StatisticsInfo.ifInDiscards  = pAdapter->BadReceives + pAdapter->DroppedReceives;
                   StatisticsInfo.ifInErrors    = pAdapter->BadReceives;
                   StatisticsInfo.ifHCOutUcastPkts = pAdapter->GoodTransmits;
                   StatisticsInfo.ifHCOutMulticastPkts = pAdapter->GoodTransmits;
                   StatisticsInfo.ifHCOutBroadcastPkts = pAdapter->GoodTransmits;
                   StatisticsInfo.ifHCOutOctets = pAdapter->TxBytesGood;
                   StatisticsInfo.ifOutErrors   = pAdapter->TxFramesFailed;
                   StatisticsInfo.ifOutDiscards = pAdapter->TxFramesDropped;
                   StatisticsInfo.ifHCInUcastOctets = pAdapter->RxBytesGood;
                   StatisticsInfo.ifHCInMulticastOctets = pAdapter->RxBytesGood;
                   StatisticsInfo.ifHCInBroadcastOctets = pAdapter->RxBytesGood;
                   StatisticsInfo.ifHCOutUcastOctets = pAdapter->TxBytesGood;
                   StatisticsInfo.ifHCOutMulticastOctets = pAdapter->TxBytesGood;
                   StatisticsInfo.ifHCOutBroadcastOctets = pAdapter->TxBytesGood;

                   usbXferStats = pDevExt->QcXferStats;

                   NdisMoveMemory(buffer, (PVOID)&StatisticsInfo, sizeof(NDIS_STATISTICS_INFO));
                   p += sizeof(NDIS_STATISTICS_INFO);

                   NdisMoveMemory((PVOID)p, (PVOID)&usbXferStats, sizeof(QC_XFER_STATISTICS));
                   p += sizeof(QC_XFER_STATISTICS);

                   #ifdef NDIS60_MINIPORT

                   NdisMoveMemory((PVOID)p, (PVOID)pAdapter->ActiveRxSlot, RX_THREAD_COUNT);
                   NdisZeroMemory((PVOID)pAdapter->ActiveRxSlot, RX_THREAD_COUNT);  // reset
                   Irp->IoStatus.Information = sizeof(NDIS_STATISTICS_INFO) + sizeof(QC_XFER_STATISTICS) + RX_THREAD_COUNT;

                   #else

                   Irp->IoStatus.Information = sizeof(NDIS_STATISTICS_INFO) + sizeof(QC_XFER_STATISTICS);

                   #endif // #ifdef NDIS60_MINIPORT
                   status = STATUS_SUCCESS;

                   QCNET_DbgPrint
                   (
                      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_FORCE,
                      ("<%s> _GET_NET_STATISTICS: (%d/%d) TX10K: %lu TX10K20K: %lu TX20K30K: %lu TX30K %lu\n", pAdapter->PortName,
                        sizeof(NDIS_STATISTICS_INFO), sizeof(QC_XFER_STATISTICS),
                        usbXferStats.TxPktsLessThan10k,
                        usbXferStats.TxPkts10kTo20k,
                        usbXferStats.TxPkts20kTo30k,
                        usbXferStats.TxPktsMoreThan30k)
                   );
                }

                break;
             }

             default:
             {
                QCNET_DbgPrint
                (
                   MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
                   ("<%s> MPIOC: IRP_MJ_DEVICE_CONTROL/UNKNOWN to 0x%p\n", pAdapter->PortName, DeviceObject)
                );
                status = STATUS_UNSUCCESSFUL;
                break;
             }
          }
          break;  
       }

       default:
          break;
    }

   if (status != STATUS_PENDING)
   {

      if (mjFunc != IRP_MJ_CLOSE)
      {
         InterlockedDecrement(&(pIocDev->IrpCount));
      }
      
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> CIRP C 0x%p(0x%x)\n", pAdapter->PortName, Irp, status)
      );
      Irp->IoStatus.Status = status;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
   }

MPIOC_Dispatch_Done:

   NdisAcquireSpinLock(MP_CtlLock);
   if ((mjFunc == IRP_MJ_CREATE) && (status != STATUS_SUCCESS))
   {
      InterlockedDecrement(&(pIocDev->DeviceOpenCount));
   }

#if 0   
   if (pIocDev->DeviceOpenCount == 0) // client is closed
   {
      KeSetEvent(&pIocDev->ClientClosedEvent, IO_NO_INCREMENT, FALSE);
   }
#endif   
   NdisReleaseSpinLock(MP_CtlLock);

   return status;
}  // MPIOC_IRPDispatch

NDIS_STATUS MPIOC_DeregisterDevice(PMP_ADAPTER pAdapter)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
       ("<%s> -->MPIOC_DeregisterDevice\n", pAdapter->PortName)
    );

    MPIOC_DeleteFilterDevice(pAdapter, NULL, NULL);

    QCNET_DbgPrint
    (
       MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
       ("<%s> <--MPIOC_DeregisterDevice\n", pAdapter->PortName)
    );

    return Status;

}  // MPIOC_DeregisterDevice

NDIS_STATUS MPIOC_AddDevice
(
   OUT PPMPIOC_DEV_INFO IocDevice,
   UCHAR           Type,
   PMP_ADAPTER     pAdapter,
   NDIS_HANDLE     NdisDeviceHandle,
   PDEVICE_OBJECT  ControlDeviceObject,
   PUNICODE_STRING SymbolicName,
   PUNICODE_STRING DeviceName,
   PCHAR           ClientFileName
)
{
   PMPIOC_DEV_INFO pIocDev;

   if (IocDevice != NULL)
   {
      *IocDevice = NULL;
   }

   pIocDev = ExAllocatePool(NonPagedPool, sizeof(MPIOC_DEV_INFO));
   if (pIocDev == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("MPIOC: AddDev failure.\n")
      );
      return NDIS_STATUS_FAILURE;
   }

   NdisAcquireSpinLock(MP_CtlLock);
   RtlZeroMemory(pIocDev, sizeof(MPIOC_DEV_INFO));

   pIocDev->Acquired         = FALSE;
   pIocDev->IdleTime         = 0;
   pIocDev->DeviceOpenCount  = 0;
   pIocDev->Type             = Type;
   pIocDev->Adapter          = pAdapter;
   pIocDev->MPStopped        = FALSE;
   pIocDev->NdisDeviceHandle = NdisDeviceHandle;

   //pIocDev->ControlDeviceObject = ControlDeviceObject;
   if (ControlDeviceObject != NULL)
   {
      RtlCopyMemory(&pIocDev->FilterDeviceInfo, (PFILTER_DEVICE_INFO)ControlDeviceObject, sizeof(FILTER_DEVICE_INFO));
   }
   
   pIocDev->ClientIdV6 = 0;
   pIocDev->ClientId  = 0;
   pIocDev->QMIType   = 0;
   pIocDev->QMUXTransactionId = 0;
   pIocDev->GetServiceFileIrp  = NULL;
   pIocDev->pWriteThread       = NULL;
   pIocDev->hWriteThreadHandle = NULL;
   pIocDev->bWtThreadInCreation = FALSE;
   pIocDev->NumOfRetriesOnError = MP_MAX_CRTL_RETRIES;
   KeInitializeEvent(&pIocDev->WriteThreadStartedEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->ClientIdReceivedEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->ClientIdReleasedEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->DataFormatSetEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->CtlSyncEvent, NotificationEvent, FALSE);
   InitializeListHead(&pIocDev->ReadIrpQueue);
   InitializeListHead(&pIocDev->ReadDataQueue);
   InitializeListHead(&pIocDev->IrpCompletionQueue);
   InitializeListHead(&pIocDev->WriteIrpQueue);
   NdisAllocateSpinLock(&pIocDev->IoLock);
   KeInitializeEvent(&pIocDev->WriteCompletionEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->EmptyCompletionQueueEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->WriteCancelCurrentEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->KickWriteEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->CancelWriteEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->WritePurgeEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->WriteStopEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->WtWorkerThreadExitEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pIocDev->ClientClosedEvent, NotificationEvent, TRUE);
   KeInitializeEvent(&pIocDev->ReadDataAvailableEvent, NotificationEvent, FALSE);

   // WDS IP Client
   KeInitializeEvent(&pIocDev->QMIWDSIPAddrReceivedEvent, NotificationEvent, FALSE);

   pIocDev->WriteThreadInCancellation = 0;

   pIocDev->pWriteEvents[IOCDEV_WRITE_COMPLETION_EVENT_INDEX] =
      &pIocDev->WriteCompletionEvent;
   pIocDev->pWriteEvents[IOCDEV_WRITE_CANCEL_CURRENT_EVENT_INDEX] =
      &pIocDev->WriteCancelCurrentEvent;
   pIocDev->pWriteEvents[IOCDEV_EMPTY_COMPLETION_QUEUE_EVENT_INDEX] =
      &pIocDev->EmptyCompletionQueueEvent;
   pIocDev->pWriteEvents[IOCDEV_KICK_WRITE_EVENT_INDEX] =
      &pIocDev->KickWriteEvent;
   pIocDev->pWriteEvents[IOCDEV_CANCEL_EVENT_INDEX] =
      &pIocDev->CancelWriteEvent;
   pIocDev->pWriteEvents[IOCDEV_WRITE_PURGE_EVENT_INDEX] =
      &pIocDev->WritePurgeEvent;
   pIocDev->pWriteEvents[IOCDEV_WRITE_STOP_EVENT_INDEX] =
      &pIocDev->WriteStopEvent;

   if (ControlDeviceObject != NULL)
   {
      pIocDev->SymbolicName.Buffer = SymbolicName->Buffer;
      pIocDev->SymbolicName.Length = SymbolicName->Length;
      pIocDev->SymbolicName.MaximumLength = SymbolicName->MaximumLength;

      pIocDev->DeviceName.Buffer = DeviceName->Buffer;
      pIocDev->DeviceName.Length = DeviceName->Length;
      pIocDev->DeviceName.MaximumLength = DeviceName->MaximumLength;

      RtlCopyMemory(pIocDev->ClientFileName, ClientFileName, IOCDEV_CLIENT_FILE_NAME_LEN);
   }

   pIocDev->NotificationIrp = NULL;
   pIocDev->SecurityReset   = 0;

   InsertTailList(&MP_DeviceList, &pIocDev->List);

   NdisReleaseSpinLock(MP_CtlLock);

#if 0
   if (ControlDeviceObject != NULL)
   {
      // QCNET_DbgPrintG(("MPIOC: CTDO 0x%p: stack_size %d\n", ControlDeviceObject, ControlDeviceObject->StackSize));
      MPIOC_ResetDeviceSecurity(pAdapter, SymbolicName);
      pIocDev->SecurityReset = 1;
   }
#endif

   if (IocDevice != NULL)
   {
      *IocDevice = pIocDev;
   }

   return NDIS_STATUS_SUCCESS;
}  // MPIOC_AddDevice

PMPIOC_DEV_INFO MPIOC_FindFreeClientObject(PDEVICE_OBJECT DeviceObject)
{
   PMPIOC_DEV_INFO pIocDev = NULL, foundDev = NULL;
   PLIST_ENTRY     headOfList, peekEntry;
   PMP_ADAPTER     pAdapter = NULL;

   NdisAcquireSpinLock(MP_CtlLock);

   if (!IsListEmpty(&MP_DeviceList))
   {
      headOfList = &MP_DeviceList;
      peekEntry = headOfList->Flink;

      while ((peekEntry != headOfList) && (foundDev == NULL))
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      MPIOC_DEV_INFO,
                      List
                   );

         // First, locate the correct MP adapter
         if (pAdapter == NULL)
         {
            if (pIocDev->ControlDeviceObject == DeviceObject)
            {
               pAdapter = pIocDev->Adapter;
            }
            else
            {
               peekEntry = peekEntry->Flink;
               continue;
            }
         }

         // Second, get the available DO for that MP adapter
         if ((pIocDev->Adapter == pAdapter) &&
             (pIocDev->ClientId == 0)     &&
             (pIocDev->Acquired == FALSE) &&
             (pIocDev->Type == MP_DEV_TYPE_CLIENT))
         {
            foundDev = pIocDev;
            pIocDev->Acquired = TRUE;
            break;
         }

         peekEntry = peekEntry->Flink;
      }
   }

   NdisReleaseSpinLock(MP_CtlLock);

   return foundDev;
}  // MPIOC_FindFreeClientObject

VOID MPIOC_CheckForAquiredDeviceIdleTime
(
   PMP_ADAPTER pAdapter
)
{
    PMPIOC_DEV_INFO pIocDev;
    PLIST_ENTRY     listHead, thisEntry;

    NdisAcquireSpinLock(MP_CtlLock);

    if (!IsListEmpty(&MP_DeviceList))
    {
        // Start at the head of the list
        listHead = &MP_DeviceList;
        thisEntry = listHead->Flink;

        while (thisEntry != listHead)
        {
            pIocDev = CONTAINING_RECORD(thisEntry, MPIOC_DEV_INFO, List);

            if ((pIocDev->DeviceOpenCount == 0) && (pIocDev->Acquired == TRUE))
            {
               if (pIocDev->IdleTime != 0)
               {
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                     ("<%s> AquiredDeviceIdleTime: dev 0x%p(%u)<%s>, reset Acquired=FALSE\n",
                       pAdapter->PortName, pIocDev, pIocDev->IdleTime, pIocDev->ClientFileName)
                  );
                  if (pIocDev->ClientId != 0)
                  {
                     NdisReleaseSpinLock(MP_CtlLock);
                     MPQCTL_ReleaseClientId
                     (
                        pAdapter,
                        pIocDev->QMIType,
                        (PVOID)pIocDev
                     );
                     NdisAcquireSpinLock(MP_CtlLock);
                  }
                  pIocDev->IdleTime = 0;
                  pIocDev->Acquired = FALSE;
                  pIocDev->QMIType  = 0;
                  pIocDev->ClientId = 0;

                  #ifdef QCMP_SUPPORT_CTRL_QMIC
                  if (pIocDev->Type == MP_DEV_TYPE_EXTERNAL_CTL)
                  {
                     pIocDev->Type = MP_DEV_TYPE_CLIENT;
                  }
                  #endif // QCMP_SUPPORT_CTRL_QMIC
               }
               else
               {
                  ++(pIocDev->IdleTime);
               }
            }

            thisEntry = thisEntry->Flink;
        }
    }

    NdisReleaseSpinLock(MP_CtlLock);

}  // MPIOC_CheckForAquiredDeviceIdleTime

NDIS_STATUS MPIOC_CleanupIOCDeviceList(
   PMP_ADAPTER    pAdapter,
   PLIST_ENTRY LocalList,
   BOOLEAN IsFilter)
{
   PLIST_ENTRY     headOfList, peekEntry, nextEntry;
	PMPIOC_DEV_INFO IocDevice;
	PMPIOC_DEV_INFO pIocDev;
	PFILTER_DEVICE_INFO pFilterDeviceInfo;
    ULONG ReturnBufferLength = 0;


	if (!IsListEmpty(LocalList))
   {
	   headOfList = LocalList;
      peekEntry = headOfList->Flink;
      nextEntry = peekEntry->Flink;

      while (peekEntry != headOfList)
      {

         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      MPIOC_DEV_INFO,
                      List
                   );
         
            /* need to check if this is needed or not: TODO MURALI */
            // terminate the write thread - double check
            MPIOC_CancelWriteThread(pIocDev);
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
               ("<%s> MPIOC_CleanupIOCDeviceList 0x%p: wait for close\n", pAdapter->PortName, pIocDev->ControlDeviceObject)
            );
#if 0            
            KeWaitForSingleObject
            (
               &pIocDev->ClientClosedEvent,
               Executive,
               KernelMode,
               FALSE,
               NULL
            );
#endif
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MPIOC_CleanupIOCDeviceList 0x%p: close signaled\n",
                 pAdapter->PortName, pIocDev->ControlDeviceObject)
            );

            MPIOC_CleanupQueues(pIocDev, 2);

           if (IsFilter == TRUE)
               {
               pFilterDeviceInfo = &(pIocDev->FilterDeviceInfo);
               if ((pIocDev->Type == MP_DEV_TYPE_CONTROL) &&
              	(pIocDev->QMIType == 0) && 
              	(pIocDev->ClientId == 0))
               {
                 // Register the device here
                 MP_USBSendCustomCommand( pAdapter, IOCTL_QCDEV_CLOSE_CTL_FILE, 
              							pFilterDeviceInfo, sizeof(FILTER_DEVICE_INFO), &ReturnBufferLength);
                 MPMAIN_Wait(-(5 * 1000 * 1000)); // +500ms delay here.
                 RtlFreeUnicodeString(&(pIocDev->SymbolicName));
                 RtlFreeUnicodeString(&(pIocDev->DeviceName));
               }
            }
		   else
		   {
            if (pIocDev->ControlDeviceObject != NULL)
            {
               RtlFreeUnicodeString(&(pIocDev->SymbolicName));
               RtlFreeUnicodeString(&(pIocDev->DeviceName));
            }
		   }
            // Just to make sure all IRPs are cancelled
            MPIOC_CleanupQueues(pIocDev, 3);

            while (pIocDev->IrpCount != 0)
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL,
                  MP_DBG_LEVEL_ERROR,
           	  ("<%s> MPIOC_DeleteFilterDevice: IRP count not zero %d (IOC 0x%p)\n", pAdapter->PortName, pIocDev->IrpCount, pIocDev)
               );
               MPMAIN_Wait(-(3 * 1000 * 1000));  // 300ms
            }
		   peekEntry = nextEntry;
		   nextEntry = nextEntry->Flink;
         
            ExFreePool(pIocDev);
	   	}
    }
   return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS MPIOC_DeleteDevice
(
   PMP_ADAPTER    pAdapter,
   PMPIOC_DEV_INFO IocDevice,
   PDEVICE_OBJECT ControlDeviceObject
)
{
   PMPIOC_DEV_INFO pIocDev;
   LIST_ENTRY LocalList;
   PLIST_ENTRY     headOfList, peekEntry, nextEntry;
   PFILTER_DEVICE_INFO pFilterDeviceInfo;
   ULONG ReturnBufferLength = 0;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPIOC_DeleteDevice 0x%p\n", pAdapter->PortName, ControlDeviceObject)
   );

   InitializeListHead(&LocalList);

            NdisAcquireSpinLock(MP_CtlLock);

   if (!IsListEmpty(&MP_DeviceList))
   {
      headOfList = &MP_DeviceList;
      peekEntry = headOfList->Flink;
      nextEntry = peekEntry->Flink;

      while (peekEntry != headOfList)
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      MPIOC_DEV_INFO,
                      List
                   );
         
         if ((pIocDev->Adapter == pAdapter) &&
             ((IocDevice == pIocDev)                                ||
              ((ControlDeviceObject == NULL) && (IocDevice == NULL))) )
         {
            RemoveEntryList(peekEntry);
			InsertTailList(&LocalList, &pIocDev->List);
            peekEntry = nextEntry;
            nextEntry = nextEntry->Flink;

         }
         else
         {
            peekEntry = nextEntry;
            nextEntry = nextEntry->Flink;
         }
      }  // while
   } // if

   NdisReleaseSpinLock(MP_CtlLock);

   MPIOC_CleanupIOCDeviceList(pAdapter, &LocalList, FALSE);
   
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPIOC_DeleteDevice\n", pAdapter->PortName)
   );

   return NDIS_STATUS_SUCCESS;
}  // MPIOC_DeleteDevice

NDIS_STATUS MPIOC_DeleteFilterDevice
(
   PMP_ADAPTER    pAdapter,
   PMPIOC_DEV_INFO IocDevice,
   PDEVICE_OBJECT ControlDeviceObject
)
{
   PMPIOC_DEV_INFO pIocDev;
   LIST_ENTRY LocalList;
   PLIST_ENTRY     headOfList, peekEntry, nextEntry;
   PFILTER_DEVICE_INFO pFilterDeviceInfo;
   ULONG ReturnBufferLength = 0;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPIOC_DeleteFilterDevice 0x%p\n", pAdapter->PortName, ControlDeviceObject)
   );

   InitializeListHead(&LocalList);

   NdisAcquireSpinLock(MP_CtlLock);

   if (!IsListEmpty(&MP_DeviceList))
   {
      headOfList = &MP_DeviceList;
      peekEntry = headOfList->Flink;
      nextEntry = peekEntry->Flink;

      while (peekEntry != headOfList)
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      MPIOC_DEV_INFO,
                      List
                   );
         
         if ((pIocDev->Adapter == pAdapter) &&
             ((IocDevice == pIocDev)                                ||
              ((ControlDeviceObject == NULL) && (IocDevice == NULL))) )
         {
            RemoveEntryList(peekEntry);
			InsertTailList(&LocalList, &pIocDev->List);
            peekEntry = nextEntry;
            nextEntry = nextEntry->Flink;

         }
         else
         {
            peekEntry = nextEntry;
            nextEntry = nextEntry->Flink;
         }
      }  // while
   } // if

   NdisReleaseSpinLock(MP_CtlLock);

   MPIOC_CleanupIOCDeviceList(pAdapter, &LocalList, TRUE);
   
   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPIOC_DeleteFilterDevice\n", pAdapter->PortName)
   );

   return NDIS_STATUS_SUCCESS;
}  // MPIOC_DeleteFilterDevice

NDIS_STATUS MPIOC_RegisterAllDevices
(
   NDIS_HANDLE WrapperConfigurationContext,
   PMP_ADAPTER pAdapter,
   USHORT      NumDev
)
{
   PCHAR myDeviceName, myLinkName;
   LONG myDeviceIndex = 0;
   NDIS_STATUS status = NDIS_STATUS_FAILURE;
   NTSTATUS nts;
   UNICODE_STRING ucDeviceName, ucLinkName, tmpUnicodeString;
   ANSI_STRING    tmpAnsiString;
   PDEVICE_OBJECT ControlDeviceObject = NULL;
   NDIS_HANDLE    NdisDeviceHandle = NULL;
   PDRIVER_DISPATCH DispatchTable[IRP_MJ_MAXIMUM_FUNCTION+1];
   USHORT i;
   ULONG retries = 0;
   UCHAR type;

   #ifdef NDIS60_MINIPORT
   NDIS_DEVICE_OBJECT_ATTRIBUTES   DeviceObjectAttributes;
   #endif  // NDIS60_MINIPORT

   NdisZeroMemory(DispatchTable, (IRP_MJ_MAXIMUM_FUNCTION+1) * sizeof(PDRIVER_DISPATCH));
   DispatchTable[IRP_MJ_CREATE]         = MPIOC_IRPDispatch;
   DispatchTable[IRP_MJ_CLEANUP]        = MPIOC_IRPDispatch;
   DispatchTable[IRP_MJ_CLOSE]          = MPIOC_IRPDispatch;
   DispatchTable[IRP_MJ_DEVICE_CONTROL] = MPIOC_IRPDispatch;
   DispatchTable[IRP_MJ_READ]           = MPIOC_Read;
   DispatchTable[IRP_MJ_WRITE]          = MPIOC_Write;

   myDeviceName = ExAllocatePool(NonPagedPool, 1024);
   if (myDeviceName == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("MPIOC: out of memory.\n")
      );
      return NDIS_STATUS_FAILURE;
   }
   myLinkName = myDeviceName + IOCDEV_CLIENT_FILE_NAME_LEN;

   for (i = 0; i < NumDev; i++)
   {
      retries = 0;
      status = NDIS_STATUS_RESOURCE_CONFLICT;

      _zeroUnicode(ucDeviceName);
      _zeroUnicode(ucLinkName);
      nts = USBUTL_AllocateUnicodeString(&ucDeviceName, 1024, "MPIOCd");
      if (nts != STATUS_SUCCESS)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("MPIOC: out of memory-2.\n")
         );
         _zeroUnicode(ucDeviceName);
         break;
      }

      nts = USBUTL_AllocateUnicodeString(&ucLinkName, 1024, "MPIOCl");
      if (nts != STATUS_SUCCESS)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("MPIOC: out of memory-3.\n")
         );
         _zeroUnicode(ucLinkName);
         break;
      }

      while (status != NDIS_STATUS_SUCCESS)
      { 
         ++myDeviceIndex;
         RtlZeroMemory(myDeviceName, 1024);

         sprintf(myDeviceName, "\\Device\\%s_%u", pAdapter->NetCfgInstanceId.Buffer,
                 myDeviceIndex);
         sprintf(myLinkName, "\\DosDevices\\Global\\%s_%u", pAdapter->NetCfgInstanceId.Buffer,
                 myDeviceIndex);


         // Device Name
         
         RtlInitAnsiString(&tmpAnsiString, myDeviceName);
         RtlAnsiStringToUnicodeString(&tmpUnicodeString, &tmpAnsiString, TRUE);
         RtlCopyUnicodeString(OUT &ucDeviceName,IN &tmpUnicodeString);
         RtlFreeUnicodeString(&tmpUnicodeString);

         // Link Name
         RtlInitAnsiString(&tmpAnsiString, myLinkName);
         RtlAnsiStringToUnicodeString(&tmpUnicodeString, &tmpAnsiString, TRUE);
         RtlCopyUnicodeString(OUT &ucLinkName,IN &tmpUnicodeString);
         RtlFreeUnicodeString(&tmpUnicodeString);

         #ifdef NDIS60_MINIPORT
         if (QCMP_NDIS6_Ok == TRUE)
         {
            NdisZeroMemory(&DeviceObjectAttributes, sizeof(NDIS_DEVICE_OBJECT_ATTRIBUTES));
            DeviceObjectAttributes.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            DeviceObjectAttributes.Header.Revision = NDIS_DEVICE_OBJECT_ATTRIBUTES_REVISION_1;
            DeviceObjectAttributes.Header.Size = sizeof(NDIS_DEVICE_OBJECT_ATTRIBUTES);
            DeviceObjectAttributes.DeviceName = &ucDeviceName;
            DeviceObjectAttributes.SymbolicName = &ucLinkName;
            DeviceObjectAttributes.MajorFunctions = &DispatchTable[0];
            DeviceObjectAttributes.ExtensionSize = 0;
            DeviceObjectAttributes.DefaultSDDLString = NULL;
            DeviceObjectAttributes.DeviceClassGuid = 0;

            status = NdisRegisterDeviceEx
                     (
                        NdisMiniportDriverHandle,
                        &DeviceObjectAttributes,
                        &ControlDeviceObject,
                        &NdisDeviceHandle
                     );
         }
         // else
         #else
         {
            status = NdisMRegisterDevice
                     (
                        MyNdisHandle,
                        &ucDeviceName,
                        &ucLinkName,
                        &DispatchTable[0],
                        &ControlDeviceObject,
                        &NdisDeviceHandle
                     );
         }
         #endif  // NDIS60_MINIPORT
         if (status != NDIS_STATUS_SUCCESS)
         {
            if (++retries > MP_REG_DEV_MAX_RETRIES)
            {
               break;
            }
         }
         else
         {
            // setup flags for IRP_MJ_READ and IRP_MJ_WRITE
            ControlDeviceObject->Flags |= DO_BUFFERED_IO;
            // if (ControlDeviceObject->StackSize < 3)
            // {
            //    ControlDeviceObject->StackSize = 3;
            // }

            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MPIOC: created IF %u device(0x%p flg 0x%x) %s\n", pAdapter->PortName,
                USBIF_GetDataInterfaceNumber(pAdapter->USBDo),
                ControlDeviceObject, ControlDeviceObject->Flags, myDeviceName)
            );
         }
      }  // while 

      if (status == NDIS_STATUS_SUCCESS)
      {
         if (i == 0)
         {
            type = MP_DEV_TYPE_CONTROL;
         }
         else
         {
            type = MP_DEV_TYPE_CLIENT;
         }

         status = MPIOC_AddDevice
                  (
                     NULL,
                     type,
                     pAdapter,
                     NdisDeviceHandle,
                     ControlDeviceObject,
                     &ucLinkName,
                     &ucDeviceName,
                     myLinkName
                  );
         if ((type == MP_DEV_TYPE_CONTROL) && (status == NDIS_STATUS_SUCCESS))
         {
            NDIS_STATUS ndisStatus = NDIS_STATUS_FAILURE;
            NDIS_HANDLE configurationHandle = NULL;

            #ifdef NDIS60_MINIPORT

            NDIS_CONFIGURATION_OBJECT ConfigObject;

            ConfigObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
            ConfigObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
            ConfigObject.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;

            ConfigObject.NdisHandle = pAdapter->AdapterHandle;
            ConfigObject.Flags = 0;

            #endif // NDIS60_MINIPORT

            #ifdef NDIS60_MINIPORT
            if (QCMP_NDIS6_Ok == TRUE)
            {
               ndisStatus = NdisOpenConfigurationEx
                            (
                               &ConfigObject,
                               &configurationHandle
                            );
            }
            #else            
            NdisOpenConfiguration
            (
               &ndisStatus,
               &configurationHandle,
               WrapperConfigurationContext
            );            
            #endif // NDIS60_MINIPORT
            if (ndisStatus != NDIS_STATUS_SUCCESS)
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                  ("MPIOC: NdisOpenConfig: ERR 0x%x\n", ndisStatus)
               );
            }
            else
            {
               USBUTL_NdisConfigurationSetString
               (
                  configurationHandle,
                  NDIS_CTRL_FILE,
                  myLinkName,
                  2
               );

               NdisCloseConfiguration(configurationHandle);
            }
         }
         else if (status != NDIS_STATUS_SUCCESS)
         {
            RtlFreeUnicodeString(&ucDeviceName);
            RtlFreeUnicodeString(&ucLinkName);
         }
      }
      else
      {
         RtlFreeUnicodeString(&ucDeviceName);
         RtlFreeUnicodeString(&ucLinkName);
      }

   }  // for

   ExFreePool(myDeviceName);

   if (status != NDIS_STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("MPIOC: reg dev failed.\n")
      );
      if (ucDeviceName.Buffer != NULL)
      {
         RtlFreeUnicodeString(&ucDeviceName);
      }
      if (ucLinkName.Buffer != NULL)
      {
         RtlFreeUnicodeString(&ucLinkName);
      }
      return NDIS_STATUS_FAILURE;
   }

   return NDIS_STATUS_SUCCESS;

}  // MPIOC_RegisterAllDevices


NDIS_STATUS MPIOC_RegisterFilterDevice
(
   NDIS_HANDLE WrapperConfigurationContext,
   PMP_ADAPTER pAdapter
)
{
   PCHAR myDeviceName, myLinkName;
   LONG myDeviceIndex = 0;
   NDIS_STATUS status = NDIS_STATUS_FAILURE;
   NTSTATUS nts;
   UNICODE_STRING ucDeviceName, ucLinkName, tmpUnicodeString;
   ANSI_STRING    tmpAnsiString;
   PDEVICE_OBJECT ControlDeviceObject = NULL;
   NDIS_HANDLE    NdisDeviceHandle = NULL;
   PDRIVER_DISPATCH DispatchTable[IRP_MJ_MAXIMUM_FUNCTION+1];
   FILTER_DEVICE_INFO FilterDeviceInfo;
   USHORT i;
   ULONG retries = 0;
   ULONG ReturnBufferLength = 0;
   UCHAR type;

   #ifdef NDIS60_MINIPORT
   NDIS_DEVICE_OBJECT_ATTRIBUTES   DeviceObjectAttributes;
   #endif  // NDIS60_MINIPORT

   NdisZeroMemory(DispatchTable, (IRP_MJ_MAXIMUM_FUNCTION+1) * sizeof(PDRIVER_DISPATCH));
   DispatchTable[IRP_MJ_CREATE]         = MPIOC_IRPDispatch;
   DispatchTable[IRP_MJ_CLEANUP]        = MPIOC_IRPDispatch;
   DispatchTable[IRP_MJ_CLOSE]          = MPIOC_IRPDispatch;
   DispatchTable[IRP_MJ_DEVICE_CONTROL] = MPIOC_IRPDispatch;
   DispatchTable[IRP_MJ_READ]           = MPIOC_Read;
   DispatchTable[IRP_MJ_WRITE]          = MPIOC_Write;

   myDeviceName = ExAllocatePool(NonPagedPool, 1024);
   if (myDeviceName == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("MPIOC: out of memory.\n")
      );
      return NDIS_STATUS_FAILURE;
   }
   myLinkName = myDeviceName + IOCDEV_CLIENT_FILE_NAME_LEN;

   retries = 0;
   status = NDIS_STATUS_RESOURCE_CONFLICT;

   _zeroUnicode(ucDeviceName);
   _zeroUnicode(ucLinkName);
   nts = USBUTL_AllocateUnicodeString(&ucDeviceName, 1024, "MPIOCd");
   if (nts != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("MPIOC: out of memory-2.\n")
      );
      _zeroUnicode(ucDeviceName);
      return NDIS_STATUS_FAILURE;
   }

   nts = USBUTL_AllocateUnicodeString(&ucLinkName, 1024, "MPIOCl");
   if (nts != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("MPIOC: out of memory-3.\n")
      );
      _zeroUnicode(ucLinkName);
      return NDIS_STATUS_FAILURE;
   }

   while (status != NDIS_STATUS_SUCCESS)
   { 
      ++myDeviceIndex;
      RtlZeroMemory(myDeviceName, 1024);

      sprintf(myDeviceName, "\\Device\\%s_%u", pAdapter->NetCfgInstanceId.Buffer,
              myDeviceIndex);
      sprintf(myLinkName, "\\DosDevices\\Global\\%s_%u", pAdapter->NetCfgInstanceId.Buffer,
              myDeviceIndex);


      // Device Name
      RtlInitAnsiString(&tmpAnsiString, myDeviceName);
      RtlAnsiStringToUnicodeString(&tmpUnicodeString, &tmpAnsiString, TRUE);
      RtlCopyUnicodeString(OUT &ucDeviceName,IN &tmpUnicodeString);
      RtlFreeUnicodeString(&tmpUnicodeString);

      // Link Name
      RtlInitAnsiString(&tmpAnsiString, myLinkName);
      RtlAnsiStringToUnicodeString(&tmpUnicodeString, &tmpAnsiString, TRUE);
      RtlCopyUnicodeString(OUT &ucLinkName,IN &tmpUnicodeString);
      RtlFreeUnicodeString(&tmpUnicodeString);

      FilterDeviceInfo.pAdapter = pAdapter;
      FilterDeviceInfo.pDeviceName = &ucDeviceName;
      FilterDeviceInfo.pDeviceLinkName = &ucLinkName;
      FilterDeviceInfo.MajorFunctions = &DispatchTable[0];
         
      // Register the device here
      status = MP_USBSendCustomCommand( pAdapter, IOCTL_QCDEV_CREATE_CTL_FILE, 
                                        &FilterDeviceInfo, sizeof(FILTER_DEVICE_INFO), &ReturnBufferLength);

      if (status != NDIS_STATUS_SUCCESS)
      {
         break;
      }
      else
      {
         if (ReturnBufferLength > 0 && (FilterDeviceInfo.pControlDeviceObject != NULL))
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MPIOC: created IF %u device(0x%p flg 0x%x) %s\n", pAdapter->PortName,
                USBIF_GetDataInterfaceNumber(pAdapter->USBDo),
                FilterDeviceInfo.pControlDeviceObject, FilterDeviceInfo.pControlDeviceObject->Flags, myDeviceName)
            );
         }
      }
   }  // while 

   if (status == NDIS_STATUS_SUCCESS && ReturnBufferLength > 0)
   {
      type = MP_DEV_TYPE_CONTROL;
      status = MPIOC_AddDevice
               (
                  NULL,
                  type,
                  pAdapter,
                  NdisDeviceHandle,
                  (PDEVICE_OBJECT)&FilterDeviceInfo,
                  &ucLinkName,
                  &ucDeviceName,
                  myLinkName
               );
      if ((type == MP_DEV_TYPE_CONTROL) && (status == NDIS_STATUS_SUCCESS))
      {
         NDIS_STATUS ndisStatus = NDIS_STATUS_FAILURE;
         NDIS_HANDLE configurationHandle = NULL;

         #ifdef NDIS60_MINIPORT

         NDIS_CONFIGURATION_OBJECT ConfigObject;

         ConfigObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
         ConfigObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
         ConfigObject.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;

         ConfigObject.NdisHandle = pAdapter->AdapterHandle;
         ConfigObject.Flags = 0;

         #endif // NDIS60_MINIPORT

         #ifdef NDIS60_MINIPORT
         if (QCMP_NDIS6_Ok == TRUE)
         {
            ndisStatus = NdisOpenConfigurationEx
                         (
                            &ConfigObject,
                            &configurationHandle
                         );
         }
         #else            
         NdisOpenConfiguration
         (
            &ndisStatus,
            &configurationHandle,
            WrapperConfigurationContext
         );            
         #endif // NDIS60_MINIPORT
         if (ndisStatus != NDIS_STATUS_SUCCESS)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
               ("MPIOC: NdisOpenConfig: ERR 0x%x\n", ndisStatus)
            );
         }
         else
         {
            USBUTL_NdisConfigurationSetString
            (
               configurationHandle,
               NDIS_CTRL_FILE,
               myLinkName,
               2
            );

            NdisCloseConfiguration(configurationHandle);
         }
      }
      else if (status != NDIS_STATUS_SUCCESS)
      {
         RtlFreeUnicodeString(&ucDeviceName);
         RtlFreeUnicodeString(&ucLinkName);
      }
   }
   else
   {
      RtlFreeUnicodeString(&ucDeviceName);
      RtlFreeUnicodeString(&ucLinkName);
   }


   ExFreePool(myDeviceName);

   if (status != NDIS_STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("MPIOC: reg dev failed.\n")
      );
      if (ucDeviceName.Buffer != NULL)
      {
         RtlFreeUnicodeString(&ucDeviceName);
      }
      if (ucLinkName.Buffer != NULL)
      {
         RtlFreeUnicodeString(&ucLinkName);
      }
      return NDIS_STATUS_FAILURE;
   }

   // Get Device ID for shared interrupt
#ifdef QCUSB_SHARE_INTERRUPT
   MP_RequestDeviceID(pAdapter);
#endif

   return NDIS_STATUS_SUCCESS;

}  // MPIOC_RegisterFilterDevice

BOOLEAN QCIOC_IsCorrectQMIRequest
(
   PMPIOC_DEV_INFO   IocDevice,
   PIRP              Irp,
   ULONG             QMIType
)
{
   BOOLEAN            isQMI = FALSE;
   PIO_STACK_LOCATION irpStack;
   PFILE_OBJECT       fileObj;

   if (Irp == NULL || IocDevice == NULL)
   {
      return isQMI;
   }

   irpStack = IoGetCurrentIrpStackLocation(Irp);

   fileObj = irpStack->FileObject;

   if (fileObj == NULL)
   {
      return isQMI;
   }
   
   if (QMIType != 0) 
   {
      if (QMIType == IocDevice->QMIType)
      {
         if (irpStack->MajorFunction == IRP_MJ_CREATE)
         {
            if ((fileObj->FsContext == 0) && (IocDevice->FsContext == 0) && (IocDevice->ClientId != 0))
            {
               isQMI = TRUE;
            }
         }
         else if(fileObj->FsContext == (PVOID)IocDevice->ClientId)
         {
            isQMI = TRUE;
         }
      }
   }
   else
   {
      //no file object passed so just the control device object
      isQMI = TRUE;
   }
   return isQMI;
}  // QCDEV_IsQDLRequest


PMPIOC_DEV_INFO MPIOC_FindIoDevice
(
   PMP_ADAPTER     pAdapter,
   PDEVICE_OBJECT  DeviceObject,
   PQCQMI          pQMI,
   PMPIOC_DEV_INFO IocDevice,
   PIRP            Irp,
   ULONG           QMIType
)
{
   PMPIOC_DEV_INFO pIocDev, foundDO = NULL;
   PLIST_ENTRY     headOfList, peekEntry;
   PDEVICE_EXTENSION pDevExt = NULL;
   PDEVICE_EXTENSION pDevExt1 = NULL;
   BOOLEAN         bReferenceFound = FALSE;

   if ((DeviceObject == NULL) && (pQMI == NULL))
   {
      return NULL;
   }

   NdisAcquireSpinLock(MP_CtlLock);

   if (!IsListEmpty(&MP_DeviceList))
   {
      headOfList = &MP_DeviceList;
      peekEntry = headOfList->Flink;
 
      while ((peekEntry != headOfList) && (foundDO == NULL))
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      MPIOC_DEV_INFO,
                      List
                   );

         if (DeviceObject != NULL)
         {
            
            if (pIocDev->FilterDeviceInfo.pControlDeviceObject == DeviceObject)
            {
               if (QCIOC_IsCorrectQMIRequest(pIocDev, Irp, QMIType) == TRUE)
               {
                  foundDO = pIocDev;
               }
            }
         }
         else
         {
#if 0         
            if ((pIocDev->Adapter == NULL) ||
                (pIocDev->Adapter->UsbRemoved == TRUE) ||
                (pIocDev->Adapter->USBDo == NULL))
            {
               peekEntry = peekEntry->Flink;
               continue;
            }
#endif            

            pDevExt1 = pIocDev->Adapter->USBDo->DeviceExtension;
            if (pAdapter != NULL)
            {
               pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;
            }
            if (IocDevice == NULL) // find the first match
            {
               // TODO: ADD MORE LOGIC to check the correct adapter list
               // Search the primary adapter's secondary adapter list and then IoDev of 
               // each seconday adapter list
               if ((pIocDev->QMIType == pQMI->QMIType)
                   &&
                   ((pDevExt != NULL) && (((pDevExt->MuxInterface.MuxEnabled != 0x01) && (pIocDev->Adapter == pAdapter)) ||
                     ((pDevExt->MuxInterface.MuxEnabled == 0x01) && (pDevExt1->MuxInterface.PhysicalInterfaceNumber == pDevExt->MuxInterface.PhysicalInterfaceNumber) &&
                      (pDevExt1->MuxInterface.FilterDeviceObj == pDevExt->MuxInterface.FilterDeviceObj)))
                   ))
               {
                  #ifdef QCMP_SUPPORT_CTRL_QMIC

                  if (pAdapter->ExternalQCTLEnabled == TRUE)
                  {
                     if (pQMI->QMIType == QMUX_TYPE_CTL)
                     {
                        if ((pIocDev->Acquired == TRUE)             &&
                            (pIocDev->Type == MP_DEV_TYPE_EXTERNAL_CTL))
                        {
                           // QMI type already matches
                           foundDO = pIocDev;
                        }
                     }
                     else
                     {
                        if ((pIocDev->ClientId == pQMI->ClientId) ||
                            (pIocDev->ClientIdV6 == pQMI->ClientId) ||
                            ((pQMI->ClientId == QMUX_BROADCAST_CID) &&
                            (pIocDev->ClientId != 0)))
                        {
                           foundDO = pIocDev;
                        }
                     }
                  }
                  else

                  #endif // QCMP_SUPPORT_CTRL_QMIC
                  {
                     if ((pIocDev->ClientId == pQMI->ClientId) ||
                         (pIocDev->ClientIdV6 == pQMI->ClientId) ||
                         ((pQMI->ClientId == QMUX_BROADCAST_CID) &&
                         (pIocDev->ClientId != 0)))
                     {
                        foundDO = pIocDev;
                     }
                  }
               }
            }
            else                   // find the next IocDev
            {
               if ((bReferenceFound == TRUE) && (pIocDev->ClientId != 0))
               {
                  // for broadcast, we only compare QMI type
                  // TODO: ADD MORE LOGIC to check the correct adapter list
                  // Search the primary adapter's secondary adapter list and then IoDev of 
                  // each seconday adapter list
                  if ((pIocDev->QMIType == pQMI->QMIType)
                      &&
                      ((pDevExt != NULL) && (((pDevExt->MuxInterface.MuxEnabled != 0x01) && (pIocDev->Adapter == pAdapter)) ||
                        ((pDevExt->MuxInterface.MuxEnabled == 0x01) && (pDevExt1->MuxInterface.PhysicalInterfaceNumber == pDevExt->MuxInterface.PhysicalInterfaceNumber) &&
                         (pDevExt1->MuxInterface.FilterDeviceObj == pDevExt->MuxInterface.FilterDeviceObj)))
                      ))
                  {
                     foundDO = pIocDev;  // unconditional, regardless of QMI CID and TYPE
                  }
               }
               if (IocDevice == pIocDev)
               {
                  bReferenceFound = TRUE;
               }
            }
         }
         peekEntry = peekEntry->Flink;
      }  // while
   }

   NdisReleaseSpinLock(MP_CtlLock);

   return foundDO;
}  // MPIOC_FindIoDevice

VOID MPIOC_CancelGetServiceFileIrpRoutine
(
   PDEVICE_OBJECT DeviceObject,
   PIRP           pIrp
)
{
   PMPIOC_DEV_INFO pIocDev;
   PMP_ADAPTER     pAdapter;
   PFILE_OBJECT    fileObj;
   ULONG           QMIType = 0;
   PIO_STACK_LOCATION  irpStack;
   ANSI_STRING        ansiString;
   
   irpStack = IoGetCurrentIrpStackLocation(pIrp);

   IoReleaseCancelSpinLock(pIrp->CancelIrql);
   
   fileObj = irpStack->FileObject;
   
   if (fileObj->FileName.Length != 0)
   {
      RtlUnicodeStringToAnsiString(&ansiString, &(fileObj->FileName), TRUE);
      if (RtlCharToInteger( (PCSZ)&(ansiString.Buffer[1]), 10, &QMIType) != STATUS_SUCCESS)
      {
         QCNET_DbgPrintG(("MPIOC Cxl 0x%p - invalid FileObject 0x%p\n", pIrp, DeviceObject));
         RtlFreeAnsiString(&ansiString);
         return;
      }
      RtlFreeAnsiString(&ansiString);
   }
   
   pIocDev = MPIOC_FindIoDevice(NULL, DeviceObject, NULL, NULL, pIrp, QMIType);
   if (pIocDev == NULL)
   {
      QCNET_DbgPrintG(("MPIOC Cxl RD: invalid called DO 0x%p\n", DeviceObject));
      return;
   }

   pAdapter = pIocDev->Adapter;

   NdisAcquireSpinLock(MP_CtlLock);

   if (IoSetCancelRoutine(pIrp, NULL) != NULL)
   {
      InterlockedDecrement(&(pIocDev->IrpCount));
      pIocDev->GetServiceFileIrp = NULL;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("GetServiceFileIrp NULL for 0x%p - 2\n", pIocDev)
      );
      NdisReleaseSpinLock(MP_CtlLock);
      pIrp->IoStatus.Status = STATUS_CANCELLED;
      pIrp->IoStatus.Information = 0;
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
   }
   else
   {
      NdisReleaseSpinLock(MP_CtlLock);
      // IRP is in cancellation, do nothing
   }

}  // MPIOC_CancelGetServiceFileIrpRoutine

// ================== Read for External Client ===================

NTSTATUS MPIOC_Read
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP           Irp
)
{
   NTSTATUS           nts = STATUS_PENDING;
   PMPIOC_DEV_INFO    pIocDev;
   PMP_ADAPTER        pAdapter;
   ULONG              outlen;
   PVOID              pReadBuffer;
   PFILE_OBJECT    fileObj;
   ULONG           QMIType = 0;
   PIO_STACK_LOCATION  irpStack;
   ANSI_STRING        ansiString;
   
   irpStack = IoGetCurrentIrpStackLocation(Irp);
   
   fileObj = irpStack->FileObject;
   
   if (fileObj->FileName.Length != 0)
   {
      RtlUnicodeStringToAnsiString(&ansiString, &(fileObj->FileName), TRUE);
      if (RtlCharToInteger( (PCSZ)&(ansiString.Buffer[1]), 10, &QMIType) != STATUS_SUCCESS)
      {
         QCNET_DbgPrintG(("CIRP Cu 0x%p - invalid FileObject 0x%p\n", Irp, DeviceObject));
         RtlFreeAnsiString(&ansiString);
         Irp->IoStatus.Information = 0;
         Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
         IoCompleteRequest(Irp, IO_NO_INCREMENT);
         return STATUS_UNSUCCESSFUL;
      }
      RtlFreeAnsiString(&ansiString);
   }
   
   pIocDev = MPIOC_FindIoDevice(NULL, DeviceObject, NULL, NULL, Irp, QMIType);
   if (pIocDev == NULL)
   {
      QCNET_DbgPrintG(("RIRP Cu 0x%p -invalid called DO 0x%p\n", Irp, DeviceObject));
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
   }

   pAdapter = pIocDev->Adapter;

#if 0
   if (pIocDev->SecurityReset == 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> RIRP Cu 0x%p security not reset, rejected\n", pAdapter->PortName, Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
   }
#endif

   if (Irp->MdlAddress != NULL)
   {
      pReadBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress,HighPagePriority);
      outlen = MPIOC_GetMdlTotalCount(Irp->MdlAddress);
   }
   else
   {
      pReadBuffer = Irp->AssociatedIrp.SystemBuffer;
      outlen = irpStack->Parameters.Read.Length;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> RIRP => 0x%p(%uB) DO 0x%p IF %u IOC 0x%p\n", pAdapter->PortName, Irp, outlen,
        DeviceObject, USBIF_GetDataInterfaceNumber(pAdapter->USBDo), pIocDev)
   );

   if ((pIocDev->Type == MP_DEV_TYPE_CONTROL) ||
       (pIocDev->ClientId == 0))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> RIRP Cu 0x%p -wrong client type, rejected\n", pAdapter->PortName, Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
   }

   if (outlen == 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> RIRP Cs 0x%p -zero byte\n", pAdapter->PortName, Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_SUCCESS;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_SUCCESS;
   }

   if (pReadBuffer == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> RIRP Cinv 0x%p -NULL buf\n", pAdapter->PortName, Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_INVALID_PARAMETER;
   }

   if (MP_STATE_STOP(pIocDev))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> RIRP Cu 0x%p -MP stopped, rejected\n", pAdapter->PortName, Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
   }

   NdisAcquireSpinLock(&pIocDev->IoLock);

   if (Irp->Cancel)
   {
      IoSetCancelRoutine(Irp, NULL);
      nts = STATUS_CANCELLED;
      NdisReleaseSpinLock(&pIocDev->IoLock);
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> RIRP cancelled 0x%p\n", pAdapter->PortName, Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = nts;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return nts;
   }

   InterlockedIncrement(&(pIocDev->IrpCount));

   // Try to fill or enqueue
   if (!IsListEmpty(&pIocDev->ReadDataQueue))
   {
      PLIST_ENTRY headOfList;
      PMPIOC_READ_ITEM rdItem;

      headOfList = RemoveHeadList(&pIocDev->ReadDataQueue);
      rdItem = CONTAINING_RECORD(headOfList, MPIOC_READ_ITEM, List);

      outlen = irpStack->Parameters.Read.Length;

      if (outlen >= rdItem->Length)
      {
         NdisMoveMemory
         (
            (PVOID)Irp->AssociatedIrp.SystemBuffer,
            (PVOID)rdItem->Buffer,
            rdItem->Length
         );
         Irp->IoStatus.Information = rdItem->Length;
         Irp->IoStatus.Status = STATUS_SUCCESS;
         nts = STATUS_SUCCESS;
         ExFreePool(rdItem->Buffer);
         ExFreePool(rdItem);
      }
      else
      {
         Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
         nts = STATUS_BUFFER_TOO_SMALL;
         // put data back to queue
         InsertHeadList(&pIocDev->ReadDataQueue, &rdItem->List);
      }
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> MPIOC_Read: RIRP enqueue 0x%p IOC 0x%p\n", pAdapter->PortName, Irp, pIocDev)
      );
      InsertTailList(&pIocDev->ReadIrpQueue, &Irp->Tail.Overlay.ListEntry);

      // Need to install cancel routine here.
      IoSetCancelRoutine(Irp, MPIOC_CancelReadRoutine);
      IoMarkIrpPending(Irp);

   }

   NdisReleaseSpinLock(&pIocDev->IoLock);

   if (nts != STATUS_PENDING)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> RIRP Cd 0x%p(0x%x)\n", pAdapter->PortName, Irp, nts)
      );
      InterlockedDecrement(&(pIocDev->IrpCount));
      
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
   }

   return nts;
}  // MPIOC_Read

VOID MPIOC_CancelReadRoutine
(
   PDEVICE_OBJECT DeviceObject,
   PIRP           pIrp
)
{
   PMPIOC_DEV_INFO pIocDev;
   PMP_ADAPTER     pAdapter;
   PFILE_OBJECT    fileObj;
   ULONG           QMIType = 0;
   PIO_STACK_LOCATION  irpStack;
   ANSI_STRING        ansiString;
   
   irpStack = IoGetCurrentIrpStackLocation(pIrp);

   IoReleaseCancelSpinLock(pIrp->CancelIrql);

   fileObj = irpStack->FileObject;
   
   if (fileObj->FileName.Length != 0)
   {
      RtlUnicodeStringToAnsiString(&ansiString, &(fileObj->FileName), TRUE);
      if (RtlCharToInteger( (PCSZ)&(ansiString.Buffer[1]), 10, &QMIType) != STATUS_SUCCESS)
      {
         QCNET_DbgPrintG(("MPIOC Cxl RD 0x%p - invalid FileObject 0x%p\n", pIrp, DeviceObject));
         RtlFreeAnsiString(&ansiString);
         return;
      }
      RtlFreeAnsiString(&ansiString);
   }
   
   pIocDev = MPIOC_FindIoDevice(NULL, DeviceObject, NULL, NULL, pIrp, QMIType);
   if (pIocDev == NULL)
   {
      QCNET_DbgPrintG(("MPIOC Cxl RD: invalid called DO 0x%p\n", DeviceObject));
      return;
   }

   pAdapter = pIocDev->Adapter;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
      ("<%s> -->MPIOC_CancelReadRoutine: 0x%p IOC 0x%p\n", pAdapter->PortName, pIrp, pIocDev)
   );

   NdisAcquireSpinLock(&pIocDev->IoLock);
   
   if (MPIOC_IsIrpInQueue(pIocDev, pIrp, 0) == TRUE)
   {
      RemoveEntryList(&pIrp->Tail.Overlay.ListEntry);
      pIrp->IoStatus.Status = STATUS_CANCELLED;
      pIrp->IoStatus.Information = 0;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> MPIOC_CancelReadRoutine: enqueue to complete IRP 0x%p (IOC 0x%p)\n", pAdapter->PortName, pIrp, pIocDev)
      );
      InsertTailList(&pIocDev->IrpCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pIocDev->EmptyCompletionQueueEvent, IO_NO_INCREMENT, FALSE);
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> MPIOC_CancelReadRoutine: IRP de-queued 0x%p\n", pAdapter->PortName, pIrp)
      );
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
      ("<%s> <--MPIOC_CancelReadRoutine: IOC 0x%p\n", pAdapter->PortName, pIocDev)
   );

   NdisReleaseSpinLock(&pIocDev->IoLock);

}  // MPIOC_CancelReadRoutine

// ================== Write for External Client ===================

NTSTATUS MPIOC_Write
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP           Irp
)
{
   NTSTATUS              nts = STATUS_PENDING;
   PMPIOC_DEV_INFO       pIocDev;
   PMP_ADAPTER           pAdapter;
   BOOLEAN               bQueueWasEmpty = FALSE;
   ULONG                 inlen = 0;
   PVOID                 pWriteBuffer;
   PFILE_OBJECT    fileObj;
   ULONG           QMIType = 0;
   PIO_STACK_LOCATION  irpStack;
   ANSI_STRING        ansiString;
   
   irpStack = IoGetCurrentIrpStackLocation(Irp);
   
   fileObj = irpStack->FileObject;
   
   if (fileObj->FileName.Length != 0)
   {
      RtlUnicodeStringToAnsiString(&ansiString, &(fileObj->FileName), TRUE);
      if (RtlCharToInteger( (PCSZ)&(ansiString.Buffer[1]), 10, &QMIType) != STATUS_SUCCESS)
      {
         QCNET_DbgPrintG(("CIRP Cu 0x%p - invalid FileObject 0x%p\n", Irp, DeviceObject));
         RtlFreeAnsiString(&ansiString);
         Irp->IoStatus.Information = 0;
         Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
         IoCompleteRequest(Irp, IO_NO_INCREMENT);
         return STATUS_UNSUCCESSFUL;
      }
      RtlFreeAnsiString(&ansiString);
   }
   
   pIocDev = MPIOC_FindIoDevice(NULL, DeviceObject, NULL, NULL, Irp, QMIType);
   if (pIocDev == NULL)
   {
      QCNET_DbgPrintG(("WIRP Cu 0x%p -invalid called DO 0x%p\n", Irp, DeviceObject));
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
   }

   pAdapter = pIocDev->Adapter;

#if 0
   if (pIocDev->SecurityReset == 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> WIRP Cu 0x%p security not reset, rejected\n", pAdapter->PortName, Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
   }
#endif

   if (Irp->MdlAddress != NULL)
   {
      pWriteBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress,HighPagePriority);
      inlen = MPIOC_GetMdlTotalCount(Irp->MdlAddress);
   }
   else
   {
      pWriteBuffer = Irp->AssociatedIrp.SystemBuffer;
      inlen = irpStack->Parameters.Write.Length;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> WIRP => 0x%p(%uB) DO 0x%p IF %u\n", pAdapter->PortName, Irp, inlen,
        DeviceObject, USBIF_GetDataInterfaceNumber(pAdapter->USBDo))
   );

   if (inlen == 0)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("WIRP Cs 0x%p -zero byte\n", Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_SUCCESS; 
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_SUCCESS;
   }

   if (pWriteBuffer == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("WIRP Cinv 0x%p -invalid buf\n", Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_INVALID_PARAMETER;
   }

   if (Irp->Cancel)
   {
      IoSetCancelRoutine(Irp, NULL);
      nts = STATUS_CANCELLED;
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> WIRP cancelled 0x%p\n", pAdapter->PortName, Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = nts;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return nts;
   }

   if (MP_STATE_STOP(pIocDev))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> WIRP Cu 0x%p -MP stopped, rejected\n", pAdapter->PortName, Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
   }

   if ((pIocDev->Type == MP_DEV_TYPE_CONTROL) ||
       (pIocDev->ClientId == 0))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> WIRP Cu 0x%p -wrong client type, rejected\n", pAdapter->PortName, Irp)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
   }

   #ifdef QCMP_SUPPORT_CTRL_QMIC

   if (pAdapter->ExternalQCTLEnabled == FALSE)

   #endif // QCMP_SUPPORT_CTRL_QMIC

   if ((pIocDev->QMIType <= QMUX_TYPE_CTL) ||
       (inlen < (QCQMUX_HDR_SIZE+QCQMUX_MSG_HDR_SIZE)))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> WIRP Cu 0x%p -wrong QMI type or data too short %dB\n", 
           pAdapter->PortName, Irp, inlen)
      );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status      = STATUS_UNSUCCESSFUL;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_UNSUCCESSFUL;
   }

   NdisAcquireSpinLock(&pIocDev->IoLock);

   InterlockedIncrement(&(pIocDev->IrpCount));

   // IoSetCancelRoutine(Irp, MPIOC_CancelWriteRoutine);
   // IoMarkIrpPending(Irp);

   if (IsListEmpty(&pIocDev->WriteIrpQueue))
   {
      bQueueWasEmpty = TRUE;
   }

   // queue IRP to the WriteQueue
   InsertTailList(&pIocDev->WriteIrpQueue, &Irp->Tail.Overlay.ListEntry);

   // make IRP cancellable after it's placed in queue
   // otherwise, cancel routine could be called before en-queuing
   IoSetCancelRoutine(Irp, MPIOC_CancelWriteRoutine);
   IoMarkIrpPending(Irp);

   NdisReleaseSpinLock(&pIocDev->IoLock);

   if (bQueueWasEmpty == TRUE)
   {
      KeSetEvent(&pIocDev->KickWriteEvent, IO_NO_INCREMENT, FALSE);
   }

   return nts;
}  // MPIOC_Write

NTSTATUS MPIOC_WtIrpCompletion
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
)
{
   PMPIOC_DEV_INFO pIocDev = (PMPIOC_DEV_INFO)pContext;

   KeSetEvent(&pIocDev->WriteCompletionEvent, IO_NO_INCREMENT, FALSE);

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // MPIOC_WtIrpCompletion

// Write worker thread created by the system running at IRQL_PASSIVE_LEVEL
void MPIOC_WriteThread
(
   PVOID           Context    // PMPIOC_DEV_INFO
)
{
   PMPIOC_DEV_INFO pIocDev  = (PMPIOC_DEV_INFO)Context;
   PMP_ADAPTER     pAdapter = pIocDev->Adapter;
   PKWAIT_BLOCK    pwbArray = NULL;
   PIRP            pIrp;
   BOOLEAN         bPurged  = FALSE, bCancelled = FALSE;
   PVOID           pActiveBuffer = NULL;
   ULONG           ulReqBytes;
   NTSTATUS        ntStatus;
   PVOID           qmiBuffer       = NULL;
   ULONG           qmiBufferLength = 0;
   ULONG           sendBytes       = 0;
   BOOLEAN         bFirstTime      = TRUE;
   PIO_STACK_LOCATION pNextStack;
   LARGE_INTEGER checkInterval;
   PMP_ADAPTER returnAdapter;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
      ("<%s> ---> MPIOC_WriteThread\n", pAdapter->PortName)
   );

   pwbArray = ExAllocatePool
              (
                 NonPagedPool,
                 (IOCDEV_WRITE_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK)
              );

   if (pwbArray == NULL)
   {
      MPIOC_EmptyIrpCompletionQueue(pIocDev);
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
         ("<%s> MPIOC Wth: out of memory.\n", pAdapter->PortName)
      );
      return;
   }

   pIrp = IoAllocateIrp
          (
             (CCHAR)(pAdapter->USBDo->StackSize+1),
             FALSE
          );

   if (pIrp == NULL)
   {
      MPIOC_EmptyIrpCompletionQueue(pIocDev);
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
         ("<%s> MPIOC Wth: out of memory-IRP.\n", pAdapter->PortName)
      );
      ExFreePool(pwbArray);
      return;
   }

   ntStatus = IoAcquireRemoveLock(pAdapter->pMPRmLock, NULL);
   if (!NT_SUCCESS(ntStatus))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
         ("<%s> MPIOC Wth: rm lock err\n", pAdapter->PortName)
      );
      ExFreePool(pwbArray);
      IoFreeIrp(pIrp);
      return;
   }
   else
   {
      QcStatsIncrement(pAdapter, MP_RML_TH, 100);
   }

   pIocDev->bWriteActive  = FALSE;
   pIocDev->bWriteStopped = FALSE;
   pIocDev->pWriteCurrent = NULL;  // PIRP

   while (TRUE)
   {
     if ((pIocDev->bWriteStopped  == TRUE) ||
         (bPurged                 == TRUE) ||
         (USBIF_IsUsbBroken(pAdapter->USBDo) == TRUE))
     {
         // Purge write Queue
         MPIOC_PurgeQueue
         (
            pIocDev,
            &pIocDev->WriteIrpQueue,
            &pIocDev->IoLock,
            0
         );
         bPurged = FALSE;

         if (bCancelled == TRUE)
         {
            break;
         }
         goto wait_for_completion;
      }

      // De-Queue
      NdisAcquireSpinLock(&pIocDev->IoLock);
      if ((!IsListEmpty(&pIocDev->WriteIrpQueue)) &&
          (pIocDev->bWriteActive == FALSE) &&
          (pAdapter->PendingCtrlRequests[pIocDev->QMIType] < pAdapter->MaxPendingQMIReqs))
      {
         PLIST_ENTRY        headOfList;
         PIO_STACK_LOCATION irpStack;

         headOfList = RemoveHeadList(&pIocDev->WriteIrpQueue);
         pIocDev->pWriteCurrent = CONTAINING_RECORD
                                  (
                                     headOfList,
                                     IRP,
                                     Tail.Overlay.ListEntry
                                  );
         irpStack = IoGetCurrentIrpStackLocation(pIocDev->pWriteCurrent);

         NdisReleaseSpinLock(&pIocDev->IoLock);

         if (pActiveBuffer == NULL)
         {
            if ((pIocDev->pWriteCurrent)->MdlAddress != NULL)
            {
               pActiveBuffer = MmGetSystemAddressForMdlSafe
                               (
                                  (pIocDev->pWriteCurrent)->MdlAddress,
                                  HighPagePriority
                               );
               ulReqBytes = MPIOC_GetMdlTotalCount
                            (
                               (pIocDev->pWriteCurrent)->MdlAddress
                            );

               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                  ("<%s> Wth MDL 0x%p(%uB)\n", pAdapter->PortName, pActiveBuffer, ulReqBytes)
               );
            }
            else
            {
               pActiveBuffer = (pIocDev->pWriteCurrent)->AssociatedIrp.SystemBuffer;
               ulReqBytes = irpStack->Parameters.Write.Length;

               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                  ("<%s> Wth SYSB IRP 0x%p 0x%p(%uB)\n", 
                    pAdapter->PortName, pIocDev->pWriteCurrent, pActiveBuffer, ulReqBytes)
               );
            }

            // make sure we have sufficient QMI buffer
            if ((qmiBuffer == NULL) || (qmiBufferLength < (ulReqBytes+sizeof(QCQMI))))
            {
               if (qmiBuffer != NULL)
               {
                  ExFreePool(qmiBuffer);
               }

               qmiBuffer = ExAllocatePool(NonPagedPool, (ulReqBytes+sizeof(QCQMI)));

               NdisAcquireSpinLock(&pIocDev->IoLock);

               if (qmiBuffer == NULL)
               {
                  PIRP pIrp = pIocDev->pWriteCurrent;

                  // out of memory, complete the de-queued IRP
                  if (IoSetCancelRoutine(pIrp, NULL) != NULL)
                  {
                     pIrp->IoStatus.Status = STATUS_NO_MEMORY;
                     InsertTailList
                     (
                        &pIocDev->IrpCompletionQueue,
                        &pIrp->Tail.Overlay.ListEntry
                     );
                     KeSetEvent
                     (
                        &pIocDev->EmptyCompletionQueueEvent,
                        IO_NO_INCREMENT,
                        FALSE
                     );
                  }
                  else
                  {
                     QCNET_DbgPrint
                     (
                        MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                        ("<%s> WIRP in cancellation 0x%p, NO_MEM\n", pAdapter->PortName, pIrp)
                     );
                     // we should continue completing the IRP as the CancelWriteRoutine does not cancel
                     // the this IRP when it's no longer in WriteIrpQueue
                     pIrp->IoStatus.Status = STATUS_CANCELLED;
                     pIrp->IoStatus.Information = 0;
                     InsertTailList
                     (
                        &pIocDev->IrpCompletionQueue,
                        &pIrp->Tail.Overlay.ListEntry
                     );
                     KeSetEvent
                     (
                        &pIocDev->EmptyCompletionQueueEvent,
                        IO_NO_INCREMENT,
                        FALSE
                     );
                  }
   
                  pIocDev->pWriteCurrent = NULL;
                  qmiBufferLength = 0;

                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
                     ("<%s> IocWth: NO_MEM for QMI", pAdapter->PortName)
                  );
   
                  NdisReleaseSpinLock(&pIocDev->IoLock);

                  continue;  // back to the loop
               }  // if (qmiBuffer == NULL)
   
               // QMI message length including the USB IF type
               qmiBufferLength = ulReqBytes+sizeof(QCQMI); 
   
               NdisReleaseSpinLock(&pIocDev->IoLock);
            }
         }  // if (pActiveBuffer == NULL)
         else
         {
            goto wait_for_completion;
         }

         /*********
         USBUTL_PrintBytes
         (
            pActiveBuffer,
            ulReqBytes,
            ulReqBytes,
            "QMUX-W",
            0,
            MP_DBG_MASK_DAT_CTL,
            MP_DBG_LEVEL_DATA
         );
         **********/

         #ifdef QCMP_SUPPORT_CTRL_QMIC

         // if ExternalQMICtrlCid is 0, we use TID to route messages
         // otherwise we use driver assigned CID and route messages
         // based on QMI type and CID (assuming the device supports
         // non-zero CTL CID
         if ((pAdapter->ExternalQCTLEnabled == TRUE) &&
             (pIocDev->QMIType == QMUX_TYPE_CTL)     &&
             (pAdapter->ExternalQMICtrlCid == 0))
         {
            if (pAdapter->QMIFuzzing == 0)
            {
               MPQMI_QMUXtoQMI
               (
                  pAdapter,
                  pIocDev->QMIType,
                  0,  // CID
                  pActiveBuffer,
                  ulReqBytes,
                  qmiBuffer
               );
               sendBytes = ulReqBytes+sizeof(QCQMI_HDR);
            }
            else
            {
               NdisMoveMemory(qmiBuffer, pActiveBuffer, ulReqBytes);
               sendBytes = ulReqBytes;
            }
         }
         else
         #endif // QCMP_SUPPORT_CTRL_QMIC
         {
            // now we have good QMI buffer, compose QMI
            MPQMI_QMUXtoQMI
            (
               pAdapter,
               pIocDev->QMIType,
               pIocDev->ClientId,
               pActiveBuffer,
               ulReqBytes,
               qmiBuffer
            );
            sendBytes = ulReqBytes+sizeof(QCQMI_HDR);
         }

         NdisAcquireSpinLock(&pIocDev->IoLock);
         InterlockedIncrement(&(pAdapter->PendingCtrlRequests[pIocDev->QMIType]));
         QCNET_DbgPrint
         (
            (MP_DBG_MASK_OID_QMI),
            MP_DBG_LEVEL_DETAIL,
            ("<%s> MPIOC_WriteThread : Number of IOC QMI requests pending for service %d is %d\n",
              pAdapter->PortName,
              pIocDev->QMIType,
              pAdapter->PendingCtrlRequests[pIocDev->QMIType]
            )
         );
         NdisReleaseSpinLock(&pIocDev->IoLock);

         // set internal IRP
         IoReuseIrp(pIrp, STATUS_SUCCESS);
         pNextStack = IoGetNextIrpStackLocation(pIrp);
         pIrp->MdlAddress                    = NULL;
         pIrp->AssociatedIrp.SystemBuffer    = qmiBuffer;
         pNextStack->MajorFunction           = IRP_MJ_DEVICE_CONTROL;
         pNextStack->Parameters.DeviceIoControl.IoControlCode =
                                               IOCTL_QCDEV_SEND_CONTROL;
         pNextStack->Parameters.DeviceIoControl.InputBufferLength = sendBytes;

         // set up DO to be passed to the completion routine
         pNextStack->DeviceObject            = pIocDev->ControlDeviceObject;

         IoSetCompletionRoutine
         (
            pIrp,
            MPIOC_WtIrpCompletion,
            pIocDev,
            TRUE,
            TRUE,
            TRUE
         );

         pIocDev->bWriteActive = TRUE;
         returnAdapter = FindStackDeviceObject(pAdapter);
         if (returnAdapter != NULL)
         {
         ntStatus = QCIoCallDriver(returnAdapter->USBDo, pIrp);
         }
         else
         {
            ntStatus = STATUS_UNSUCCESSFUL;
            pIrp->IoStatus.Status = ntStatus;
            MPIOC_WtIrpCompletion(pAdapter->USBDo, pIrp, pIocDev);
            
         }
         if(ntStatus != STATUS_PENDING)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> IocWth: IoCallDriver rtn 0x%x", pAdapter->PortName, ntStatus)
            );
         }
      }
      else
      {
         NdisReleaseSpinLock(&pIocDev->IoLock);
      }

wait_for_completion:

      if (bFirstTime == TRUE)
      {
         bFirstTime = FALSE;
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPIOC_WriteThread started.\n", pAdapter->PortName)
         );
         KeSetEvent(&pIocDev->WriteThreadStartedEvent, IO_NO_INCREMENT, FALSE);
      }

      checkInterval.QuadPart = -(15 * 1000 * 1000); // 1.5 sec
      ntStatus = KeWaitForMultipleObjects
                 (
                    IOCDEV_WRITE_EVENT_COUNT,
                    (VOID **)&pIocDev->pWriteEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE, // non-alertable
                    &checkInterval,
                    pwbArray
                 );

      switch(ntStatus)
      {
         case IOCDEV_WRITE_COMPLETION_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MP_Wth: WRITE_COMPLETION_EVENT\n", pAdapter->PortName)
            );
            KeClearEvent(&pIocDev->WriteCompletionEvent);
            pIocDev->bWriteActive = FALSE;

            NdisAcquireSpinLock(&pIocDev->IoLock);

            ntStatus = pIrp->IoStatus.Status;

            // complete IRP
            MPIOC_WriteIrpCompletion(pIocDev, ulReqBytes, ntStatus, 0);

            // Reset
            pActiveBuffer          = NULL;
            ulReqBytes             = 0;
            pIocDev->pWriteCurrent = NULL;

            NdisReleaseSpinLock(&pIocDev->IoLock);

            break;
         }  // WRITE_COMPLETION_EVENT_INDEX

         case IOCDEV_EMPTY_COMPLETION_QUEUE_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MP_Wth: IOCDEV_EMPTY_COMPLETION_QUEUE\n", pAdapter->PortName)
            );

            KeClearEvent(&pIocDev->EmptyCompletionQueueEvent);
            MPIOC_EmptyIrpCompletionQueue(pIocDev);

            break;

         }  // IOCDEV_EMPTY_COMPLETION_QUEUE_EVENT_INDEX

         case IOCDEV_WRITE_CANCEL_CURRENT_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MP_Wth: IOCDEV_WRITE_CANCEL_CURRENT_EVENT\n", pAdapter->PortName)
            );
            KeClearEvent(&pIocDev->WriteCancelCurrentEvent);

            NdisAcquireSpinLock(&pIocDev->IoLock);
            if (pIocDev->pWriteCurrent == NULL)
            {
               NdisReleaseSpinLock(&pIocDev->IoLock);
               break;
            }
            IoCancelIrp(pIrp);
            NdisReleaseSpinLock(&pIocDev->IoLock);

            break;
         }

         case IOCDEV_KICK_WRITE_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MP_Wth: IOCDEV_KICK_WRITE_EVENT\n", pAdapter->PortName)
            );
            KeClearEvent(&pIocDev->KickWriteEvent);

            if (pIocDev->bWriteActive == TRUE)
            {
               goto wait_for_completion;
            }
            // else we just go around the loop again, picking up the next
            // write entry
            continue;
         }

         case IOCDEV_CANCEL_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MP_Wth: IOCDEV_CANCEL_EVENT\n", pAdapter->PortName)
            );
            // clear cancel event so we don't reactivate
            KeClearEvent(&pIocDev->CancelWriteEvent);

            // signal the loop that a cancel has occurred
            pIocDev->bWriteStopped = bCancelled = TRUE;
            if (pIocDev->bWriteActive == TRUE)
            {
               // cancel outstanding irp
               IoCancelIrp(pIrp);

               // wait for writes to complete, don't cancel
               // if a write is active, continue to wait for the completion
               // we pick up the canceled status at the top of the service
               // loop
               goto wait_for_completion;
            }
            break; // goto exit_WriteThread; // nothing active exit
         }

         case IOCDEV_WRITE_PURGE_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MP_Wth: IOCDEV_WRITE_PURGE_EVENT\n", pAdapter->PortName)
            );
            // clear purge event so we don't reactivate
            KeClearEvent(&pIocDev->WritePurgeEvent);

            // signal the loop that a purge has occurred
            bPurged = TRUE;
            if (pIocDev->bWriteActive == TRUE)
            {
               // cancel outstanding irp
               IoCancelIrp(pIrp);

               // wait for writes to complete, don't cancel
               // if a write is active, continue to wait for the completion
               // we pick up the canceled status at the top of the service
               // loop
               goto wait_for_completion;
            }
            break;
         }

         case IOCDEV_WRITE_STOP_EVENT_INDEX:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MP_Wth: IOCDEV_WRITE_STOP_EVENT\n", pAdapter->PortName)
            );
            KeClearEvent(&pIocDev->WriteStopEvent);
            pIocDev->bWriteStopped = TRUE;
            break;
         }

         default:
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> MP_Wth: default sig 0x%x", pAdapter->PortName, ntStatus)
            );

            break;
         }
      }  // switch
   }  // end while forever

exit_WriteThread:

   if (pIrp != NULL)
   {
      IoFreeIrp(pIrp);
   }
   if (pwbArray != NULL)
   {
      ExFreePool(pwbArray);
      pwbArray = NULL;
   }
   if (qmiBuffer != NULL)
   {
      ExFreePool(qmiBuffer);
      qmiBuffer = NULL;
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- MPIOC_WriteThread (dev 0x%p, DO 0x%p)\n", pAdapter->PortName, pIocDev, pIocDev->ControlDeviceObject)
   );

   KeSetEvent(&pIocDev->WtWorkerThreadExitEvent, IO_NO_INCREMENT, FALSE);

   QcMpIoReleaseRemoveLock(pAdapter, pAdapter->pMPRmLock, NULL, MP_RML_TH, 100)

}  // MPIOC_WriteThread

VOID MPIOC_EmptyIrpCompletionQueue(PMPIOC_DEV_INFO pIocDev)
{
   PMP_ADAPTER     pAdapter = pIocDev->Adapter;
   PLIST_ENTRY     headOfList;
   PIRP            pIrp;
   BOOLEAN         bComplete;
   BOOLEAN         bCancelled = FALSE;
   KIRQL           irql = KeGetCurrentIrql();

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> --> MPIOC_EmptyIrpCompletionQueue: IOC 0x%p (Cnt %d)\n", pAdapter->PortName, pIocDev, pIocDev->IrpCount)
   );

   NdisAcquireSpinLock(&pIocDev->IoLock);

   while (!IsListEmpty(&pIocDev->IrpCompletionQueue))
   {
      bComplete = TRUE;
      headOfList = RemoveHeadList(&pIocDev->IrpCompletionQueue);
      pIrp = CONTAINING_RECORD(headOfList, IRP, Tail.Overlay.ListEntry);

      // There should be no cancel routine, so we proceed to completion
      if (IoSetCancelRoutine(pIrp, NULL) != NULL)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<%s> EmptyIrpCompletionQueue: FATAL-IRP with cancellation Rtn 0x%p, still proceed\n", pAdapter->PortName, pIrp)
         );
         // bComplete = FALSE;
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<%s> EmptyIrpCompletionQueue: cancelling IRP 0x%p (flag %d)\n", pAdapter->PortName, pIrp, bComplete)
         );
         bCancelled = TRUE;
      }
      NdisReleaseSpinLock(&pIocDev->IoLock);

      if (bComplete == TRUE)
      {
         if ((irql == PASSIVE_LEVEL) && (bCancelled == TRUE))
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> IOC copletion: IRP cancelled 0x%p\n", pAdapter->PortName, pIrp)
            );
            MPMAIN_Wait(-(2 * 1000 * 10));  // 2ms
         }

         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> IRP C 0x%p(0x%x)\n", pAdapter->PortName, pIrp, pIrp->IoStatus.Status)
         );
         IoCompleteRequest(pIrp, IO_NO_INCREMENT);
         InterlockedDecrement(&(pIocDev->IrpCount));
      }

      NdisAcquireSpinLock(&pIocDev->IoLock);
   }

   NdisReleaseSpinLock(&pIocDev->IoLock);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <-- MPIOC_EmptyIrpCompletionQueue: IOC 0x%p (pending %d)\n", pAdapter->PortName, pIocDev, pIocDev->IrpCount)
   );
}  // MPIOC_EmptyIrpCompletionQueue

VOID MPIOC_PurgeQueue
(
   PMPIOC_DEV_INFO pIocDev,
   PLIST_ENTRY     queue,
   PNDIS_SPIN_LOCK pSpinLock,
   UCHAR           cookie
)
{
   PMP_ADAPTER pAdapter = pIocDev->Adapter;
   PLIST_ENTRY headOfList;
   PIRP        pIrp;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> ---> MPIOC_PurgeQueue(%d)\n", pAdapter->PortName, cookie)
   );

   NdisAcquireSpinLock(pSpinLock);

   while (!IsListEmpty(queue))
   {
      headOfList = RemoveHeadList(queue);
      pIrp =  CONTAINING_RECORD
              (
                 headOfList,
                 IRP,
                 Tail.Overlay.ListEntry
              );
      if (IoSetCancelRoutine(pIrp, NULL) != NULL)
      {
         pIrp->IoStatus.Status = STATUS_CANCELLED;
         pIrp->IoStatus.Information = 0;
         InsertTailList
         (
            &pIocDev->IrpCompletionQueue,
            &pIrp->Tail.Overlay.ListEntry
         );
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<%s> MPIOC_PurgeQueue: err-IRP already cancelled 0x%p\n", pAdapter->PortName, pIrp)
         );

         // we should continue IRP completion as the CancelRoutine does not proceed when IRP is not in R/W queue
         pIrp->IoStatus.Status = STATUS_CANCELLED;
         pIrp->IoStatus.Information = 0;
         InsertTailList
         (
            &pIocDev->IrpCompletionQueue,
            &pIrp->Tail.Overlay.ListEntry
         );
      }
   }

   NdisReleaseSpinLock(pSpinLock);

   MPIOC_EmptyIrpCompletionQueue(pIocDev);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--- MPIOC_PurgeQueue(%d)\n", pAdapter->PortName, cookie)
   );

}  // MPIOC_PurgeQueue

ULONG MPIOC_GetMdlTotalCount(PMDL Mdl)
{
   PMDL pCurrentMdlBuffer;
   ULONG totalCount = 0L;

   pCurrentMdlBuffer = Mdl;

   while (pCurrentMdlBuffer != NULL)
   {
      totalCount += MmGetMdlByteCount(pCurrentMdlBuffer);
      pCurrentMdlBuffer = pCurrentMdlBuffer->Next;
   }

   return totalCount;
}  // MPIOC_GetMdlTotalCount

NTSTATUS MPIOC_WriteIrpCompletion
(
   PMPIOC_DEV_INFO   pIocDev,
   ULONG             ulRequestedBytes,
   NTSTATUS          ntStatus,
   UCHAR             cookie
)
{
   PIRP pIrp;
   PMP_ADAPTER pAdapter = pIocDev->Adapter;

   pIrp = pIocDev->pWriteCurrent;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> MP_WIC<%d>: IRP 0x%p (0x%x)\n", pAdapter->PortName, cookie, pIrp, ntStatus)
   );


   if (NT_SUCCESS(ntStatus) || (ntStatus == STATUS_CANCELLED))
   {
      pIocDev->iErrorCount = 0;
   }
   else
   {
      pIocDev->iErrorCount++;

      // after some magic number of times of failure,
      // we mark the device as 'removed'
      if ( (ntStatus != STATUS_INVALID_PARAMETER) &&
             (pAdapter->IgnoreQMICTLErrors != TRUE) )
      {
          // after some magic number of times of failure,
          // we mark the device as 'removed'
          if (pIocDev->iErrorCount > pIocDev->NumOfRetriesOnError)
          {
             QCNET_DbgPrint
             (
                MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                ("<%s> MP_WIC: failure %d, dev removed\n", pAdapter->PortName, pIocDev->iErrorCount)
             );
             pIocDev->bWriteStopped  = TRUE;
             pIocDev->iErrorCount = pIocDev->NumOfRetriesOnError;
          }
      }
   }

   if (pIrp != NULL) // if it wasn't canceled out of the block
   {
      if (IoSetCancelRoutine(pIrp, NULL) == NULL)
      {
         // this happens when MPIOC_CancelWriteRoutine is called
         // MPIOC_CancelWriteRoutine will do nothing since IRP already de-queued
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<%s> MP_WIRPC: WIRP 0x%p already cxled\n", pAdapter->PortName, pIrp)
         );
         // continue completing IRP since MPIOC_CancelWriteRoutine does nothing in this case
      }

      pIrp->IoStatus.Information = ulRequestedBytes;
      pIrp->IoStatus.Status = ntStatus;

      InsertTailList(&pIocDev->IrpCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pIocDev->EmptyCompletionQueueEvent, IO_NO_INCREMENT, FALSE);
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> MP_WIC NULL IRP-%d\n", pAdapter->PortName, cookie)
      );
   }

ExitWriteCompletion:

   return ntStatus;
}  // MPIOC_WriteIrpCompletion

BOOLEAN MPIOC_IsIrpInQueue(PMPIOC_DEV_INFO pIocDev, PIRP WriteIrp, INT QueueFlag)
{
   PLIST_ENTRY listHead, thisEntry, ioQueue;
   BOOLEAN     bMatchFound = FALSE;
   PIRP        pIrp = NULL;

   // Be sure you own IoLock before getting here!!!!

   if (QueueFlag == 0)
   {
      ioQueue = &pIocDev->ReadIrpQueue;
   }
   else
   {
      ioQueue = &pIocDev->WriteIrpQueue;
   }

   if (!IsListEmpty(ioQueue))
   {
      // Start at the head of the list
      listHead = ioQueue;
      thisEntry = listHead->Flink;

      while (thisEntry != listHead)
      {
         pIrp = CONTAINING_RECORD(thisEntry, IRP, Tail.Overlay.ListEntry);
         if (WriteIrp == pIrp)
         {
            bMatchFound = TRUE;
            break;
         }
         thisEntry = thisEntry->Flink;
      }
   }
   return bMatchFound;
}  // MPIOC_IsIrpInQueue

VOID MPIOC_CancelWriteRoutine
(
   PDEVICE_OBJECT DeviceObject,
   PIRP           pIrp
)
{
   PMPIOC_DEV_INFO pIocDev;
   PMP_ADAPTER     pAdapter;
   PFILE_OBJECT    fileObj;
   ULONG           QMIType = 0;
   PIO_STACK_LOCATION  irpStack;
   ANSI_STRING        ansiString;
   
   irpStack = IoGetCurrentIrpStackLocation(pIrp);

   IoReleaseCancelSpinLock(pIrp->CancelIrql);

   fileObj = irpStack->FileObject;
   
   if (fileObj->FileName.Length != 0)
   {
      RtlUnicodeStringToAnsiString(&ansiString, &(fileObj->FileName), TRUE);
      if (RtlCharToInteger( (PCSZ)&(ansiString.Buffer[1]), 10, &QMIType) != STATUS_SUCCESS)
      {
         QCNET_DbgPrintG(("MPIOC: 0x%p - invalid FileObject 0x%p\n", pIrp, DeviceObject));
         RtlFreeAnsiString(&ansiString);
         return;
      }
      RtlFreeAnsiString(&ansiString);
   }
   
   pIocDev = MPIOC_FindIoDevice(NULL, DeviceObject, NULL, NULL, pIrp, QMIType);
   if (pIocDev == NULL)
   {
      QCNET_DbgPrintG(("MPIOC: invalid called DO 0x%p for IRP 0x%p\n", DeviceObject, pIrp));
      return;
   }

   pAdapter = pIocDev->Adapter;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
      ("<%s> MPIOC_CancelWriteRoutine: 0x%p\n", pAdapter->PortName, pIrp)
   );

   NdisAcquireSpinLock(&pIocDev->IoLock);

   if ((pIocDev->pWriteCurrent == pIrp) && (pIocDev->bWriteActive == TRUE))
   {
      // this happens when IRP is in IO process but not completed yet
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> MPIOC_CancelWriteRoutine: IRP in IO processing, restore CxlRtn %p\n", pAdapter->PortName, pIrp)
      );
      IoSetCancelRoutine(pIrp, MPIOC_CancelWriteRoutine);
      KeSetEvent(&pIocDev->WriteCancelCurrentEvent, IO_NO_INCREMENT, FALSE);
   }
   else
   {
      // proceed only if IRP is in queue
      if (MPIOC_IsIrpInQueue(pIocDev, pIrp, 1) == TRUE)
      {
         RemoveEntryList(&pIrp->Tail.Overlay.ListEntry);
         pIrp->IoStatus.Status = STATUS_CANCELLED;
         pIrp->IoStatus.Information = 0;
         InsertTailList(&pIocDev->IrpCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
         KeSetEvent(&pIocDev->EmptyCompletionQueueEvent, IO_NO_INCREMENT, FALSE);
      }
      else
      {
         // this only happens when IRP is in the middle of completion
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
            ("<%s> MPIOC_CancelWriteRoutine: IRP de-queued %p\n", pAdapter->PortName, pIrp)
         );
      }
   }

   NdisReleaseSpinLock(&pIocDev->IoLock);

}  // MPIOC_CancelWriteRoutine

NTSTATUS MPIOC_StartWriteThread(PMPIOC_DEV_INFO pIocDev)
{
   PMP_ADAPTER       pAdapter = pIocDev->Adapter;
   NTSTATUS          ntStatus = STATUS_SUCCESS;
   KIRQL             irql     = KeGetCurrentIrql();
   OBJECT_ATTRIBUTES objAttr; 

   // Make sure the write thread is created with IRQL==PASSIVE_LEVEL
   NdisAcquireSpinLock(&pIocDev->IoLock);

   if (((pIocDev->pWriteThread == NULL)        &&
        (pIocDev->hWriteThreadHandle == NULL)) &&
       (irql > PASSIVE_LEVEL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPIOC_WT IRQL high %d\n", pAdapter->PortName, irql)
      );
      NdisReleaseSpinLock(&pIocDev->IoLock);
      return STATUS_UNSUCCESSFUL;
   }

   if ((pIocDev->hWriteThreadHandle == NULL) &&
       (pIocDev->pWriteThread == NULL))
   {
      NTSTATUS ntStatus;

      if (pIocDev->bWtThreadInCreation == FALSE)
      {
         pIocDev->bWtThreadInCreation = TRUE;
         NdisReleaseSpinLock(&pIocDev->IoLock);
      }
      else
      {
         NdisReleaseSpinLock(&pIocDev->IoLock);
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPIOC_WT th in creation\n", pAdapter->PortName)
         );
         return STATUS_SUCCESS;
      }
 
      KeClearEvent(&pIocDev->WriteThreadStartedEvent);
      InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
      ntStatus = PsCreateSystemThread
                 (
                    OUT &pIocDev->hWriteThreadHandle,
                    IN THREAD_ALL_ACCESS,
                    IN &objAttr,     // POBJECT_ATTRIBUTES  ObjectAttributes
                    IN NULL,         // HANDLE  ProcessHandle
                    OUT NULL,        // PCLIENT_ID  ClientId
                    IN (PKSTART_ROUTINE)MPIOC_WriteThread,
                    IN (PVOID)pIocDev
                 );
      if (ntStatus != STATUS_SUCCESS)
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPIOC_WT th failure 0x%x\n", pAdapter->PortName, ntStatus)
         );
         pIocDev->pWriteThread = NULL;
         pIocDev->hWriteThreadHandle = NULL;
         pIocDev->bWtThreadInCreation = FALSE;
         return ntStatus;
      }

      ntStatus = KeWaitForSingleObject
                 (
                    &pIocDev->WriteThreadStartedEvent,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL
                 );
      KeClearEvent(&pIocDev->WriteThreadStartedEvent);

      ntStatus = ObReferenceObjectByHandle
                 (
                    pIocDev->hWriteThreadHandle,
                    THREAD_ALL_ACCESS,
                    NULL,
                    KernelMode,
                    (PVOID*)&pIocDev->pWriteThread,
                    NULL
                 );
      if (!NT_SUCCESS(ntStatus))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPIOC_WT ObReferenceObjectByHandle failed 0x%x\n", pAdapter->PortName, ntStatus)
         );
         pIocDev->pWriteThread = NULL;
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPIOC_WT handle=0x%p thOb=0x%p\n", pAdapter->PortName,
              pIocDev->hWriteThreadHandle, pIocDev->pWriteThread)
         );
         ZwClose(pIocDev->hWriteThreadHandle);
         pIocDev->hWriteThreadHandle = NULL;
      }

      pIocDev->bWtThreadInCreation = FALSE;
   }
   else
   {
      NdisReleaseSpinLock(&pIocDev->IoLock);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> MPIOC_WT alive-%d    irql-0x%x\n", pAdapter->PortName, ntStatus, irql)
   );
  
   return ntStatus;
}  // MPIOC_StartWriteThread

VOID MPIOC_CancelWriteThread
(
   PMPIOC_DEV_INFO pIocDev
)
{
   PMP_ADAPTER   pAdapter = pIocDev->Adapter;
   NTSTATUS      ntStatus;
   LARGE_INTEGER timeoutValue;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> MPIOC_CxlWtTh-0: 0x%p/0x%p(type %d, id %d)\n", pAdapter->PortName,
        pIocDev, pIocDev->ControlDeviceObject, pIocDev->QMIType, pIocDev->ClientId)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_CRITICAL,
         ("<%s> MPIOC Cxl: wrong IRQL\n", pAdapter->PortName)
      );
      return;
   }

   if (InterlockedIncrement(&pIocDev->WriteThreadInCancellation) > 1)
   {
      while ((pIocDev->hWriteThreadHandle != NULL) || (pIocDev->pWriteThread != NULL))
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL,
            MP_DBG_LEVEL_ERROR,
            ("<%s> MPIOC cxl in pro %d\n", pAdapter->PortName, pIocDev->WriteThreadInCancellation)
         );
         MPMAIN_Wait(-(3 * 1000 * 1000));  // 300ms
      }
      InterlockedDecrement(&pIocDev->WriteThreadInCancellation);
      return;
   }

   if ((pIocDev->hWriteThreadHandle == NULL) &&
       (pIocDev->pWriteThread == NULL))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPIOC_CxlWtTh: already cancelled IODEV 0x%p\n", pAdapter->PortName, pIocDev)
      );
      InterlockedDecrement(&pIocDev->WriteThreadInCancellation);
      return;
   }

   // if (pIocDev->DeviceOpenCount > 0)
   if ((pIocDev->hWriteThreadHandle != NULL) || (pIocDev->pWriteThread != NULL))
   {
      KeClearEvent(&pIocDev->WtWorkerThreadExitEvent);
      KeSetEvent(&pIocDev->CancelWriteEvent, IO_NO_INCREMENT, FALSE);

      if (pIocDev->pWriteThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pIocDev->pWriteThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pIocDev->pWriteThread);
         KeClearEvent(&pIocDev->WtWorkerThreadExitEvent);
         ZwClose(pIocDev->hWriteThreadHandle);
      }
      else // best effort
      {
         timeoutValue.QuadPart = -(50 * 1000 * 1000);   // 5.0 sec
         ntStatus = KeWaitForSingleObject
                    (
                       &pIocDev->WtWorkerThreadExitEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       &timeoutValue
                    );
         if (ntStatus == STATUS_TIMEOUT)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
               ("<%s> MPIOC_CxlWtTh: timeout for dev 0x%p\n", pAdapter->PortName, pIocDev)
            );
         }
         KeClearEvent(&pIocDev->WtWorkerThreadExitEvent);
         ZwClose(pIocDev->hWriteThreadHandle);
      }
      pIocDev->hWriteThreadHandle = NULL;
      pIocDev->pWriteThread       = NULL;
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> MPIOC_CxlWtTh: device already closed, no action.\n", pAdapter->PortName)
      );
   }

   InterlockedDecrement(&pIocDev->WriteThreadInCancellation);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> MPIOC_CxlWtTh-1: 0x%p/0x%p(type %d, id %d)\n", pAdapter->PortName,
        pIocDev, pIocDev->ControlDeviceObject, pIocDev->QMIType, pIocDev->ClientId)
   );
}  // MPIOC_CancelWriteThread

// cleanup I/O queues (ReadIrpQueue/WriteIrpQueue/ReadDataQueue)
VOID MPIOC_CleanupQueues(PMPIOC_DEV_INFO pIocDev, UCHAR cookie)
{
   PMP_ADAPTER      pAdapter = pIocDev->Adapter;
   PLIST_ENTRY      headOfList;
   PMPIOC_READ_ITEM rdItem;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
      ("<%s> ---> MPIOC_CleanupQueues (%d)\n", pAdapter->PortName, cookie));

   MPIOC_PurgeQueue(pIocDev, &pIocDev->ReadIrpQueue, &pIocDev->IoLock, 1);
   MPIOC_PurgeQueue(pIocDev, &pIocDev->WriteIrpQueue, &pIocDev->IoLock, 2);

   // cleanup ReadDataQueue
   NdisAcquireSpinLock(&pIocDev->IoLock);

   while (!IsListEmpty(&pIocDev->ReadDataQueue))
   {
      headOfList = RemoveHeadList(&pIocDev->ReadDataQueue);
      rdItem =  CONTAINING_RECORD
                (
                   headOfList,
                   MPIOC_READ_ITEM,
                   List
                );
      ExFreePool(rdItem->Buffer);
      ExFreePool(rdItem);
   }

   NdisReleaseSpinLock(&pIocDev->IoLock);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL, 
      ("<%s> <--- MPIOC_CleanupQueues (%d)\n", pAdapter->PortName, cookie)
   );
}  // MPIOC_CleanupQueues

VOID MPIOC_SetStopState
(
   PMP_ADAPTER pAdapter,
   BOOLEAN     TerminateAll
)
{
    PMPIOC_DEV_INFO pIocDev;
    PLIST_ENTRY     listHead, thisEntry;

    NdisAcquireSpinLock(MP_CtlLock);

    if (!IsListEmpty(&MP_DeviceList))
    {
        // Start at the head of the list
        listHead = &MP_DeviceList;
        thisEntry = listHead->Flink;

        while (thisEntry != listHead)
        {
            pIocDev = CONTAINING_RECORD(thisEntry, MPIOC_DEV_INFO, List);

            if (pIocDev != NULL &&
                pIocDev->Adapter == pAdapter)
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                  ("<%s> PostRemovalNotification: dev 0x%p(%s) IRP 0x%p\n", pAdapter->PortName,
                    pIocDev, pIocDev->ClientFileName, pIocDev->NotificationIrp)
               );
               MPIOC_PostRemovalNotification(pIocDev);
               MPIOC_PostMtuNotification(pIocDev, 4, 0, TRUE);
               MPIOC_PostMtuNotification(pIocDev, 6, 0, TRUE);
               pIocDev->MPStopped = TRUE;
               if (TerminateAll == TRUE)
               {
#if 0               
                  // Release client ID for TYPE_CLIENT?????
                  // if (pAdapter->Flags & fMP_ADAPTER_SURPRISE_REMOVED)
                  {
                     pIocDev->Acquired = FALSE;
                     pIocDev->ClientId = 0; // force to zero

                     #ifdef QCMP_SUPPORT_CTRL_QMIC
                     if (pIocDev->Type == MP_DEV_TYPE_EXTERNAL_CTL)
                     {
                        pIocDev->Type = MP_DEV_TYPE_CLIENT;
                     }
                     #endif // QCMP_SUPPORT_CTRL_QMIC
                  }
#endif
                  QCNET_DbgPrint
                  (
                     MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                     ("<%s> PostRemovalNotification: dev 0x%p(%s) Acquired=FALSE\n",
                       pAdapter->PortName, pIocDev, pIocDev->ClientFileName)
                  );
                  // terminate the write worker thread
                  // MPIOC_CancelWriteThread(pIocDev);  // cannot run @IRQL2

                  // Cleanup queues
                  MPIOC_CleanupQueues(pIocDev, 1);
               }
            }

            thisEntry = thisEntry->Flink;
        }
    }

    NdisReleaseSpinLock(MP_CtlLock);

}  // MPIOC_SetStopState

VOID MPIOC_InvalidateClients(PMP_ADAPTER pAdapter)
{
    PMPIOC_DEV_INFO pIocDev;
    PLIST_ENTRY     listHead, thisEntry;

    NdisAcquireSpinLock(MP_CtlLock);

    if (!IsListEmpty(&MP_DeviceList))
    {
        // Start at the head of the list
        listHead = &MP_DeviceList;
        thisEntry = listHead->Flink;

        while (thisEntry != listHead)
        {
            pIocDev = CONTAINING_RECORD(thisEntry, MPIOC_DEV_INFO, List);

            if (pIocDev != NULL &&
                pIocDev->Adapter == pAdapter)
            {
               QCNET_DbgPrint
               (
                  MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
                  ("<%s> _InvalidateClients: PostRemovalNotification: dev 0x%p(%s) IRP 0x%p\n", pAdapter->PortName,
                    pIocDev, pIocDev->ClientFileName, pIocDev->NotificationIrp)
               );
               MPIOC_PostRemovalNotification(pIocDev);
               MPIOC_PostMtuNotification(pIocDev, 4, 0, TRUE);
               MPIOC_PostMtuNotification(pIocDev, 6, 0, TRUE);
               if (pIocDev->Type != MP_DEV_TYPE_CONTROL)
               {
                  pIocDev->MPStopped = TRUE;
               }
            }

            thisEntry = thisEntry->Flink;
        }
    }

    NdisReleaseSpinLock(MP_CtlLock);

}  // MPIOC_InvalidateClients

// =================== Custom Notification Support =====================

NTSTATUS MPIOC_CacheNotificationIrp
(
   PMPIOC_DEV_INFO pIocDev,
   PIRP            pIrp,
   ULONG           Type
)
{
   NTSTATUS           ntStatus;
   PIO_STACK_LOCATION irpStack;
   PMP_ADAPTER        pAdapter = pIocDev->Adapter;
   
   pIrp->IoStatus.Information = 0;
   ntStatus = STATUS_INVALID_PARAMETER;

   if (pIrp->AssociatedIrp.SystemBuffer == NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("CacheNotificationIrp: in buf NULL\n")
      );
      return ntStatus;
   }

   irpStack = IoGetCurrentIrpStackLocation(pIrp);
   if (irpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("CacheNotificationIrp: in buf too small\n")
      );
      ntStatus = STATUS_BUFFER_TOO_SMALL;
      return ntStatus;
   }

   if (pIocDev->MPStopped == TRUE)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("CacheNotificationIrp: device stopped, rejected\n")
      );
      return STATUS_UNSUCCESSFUL;
   }

   NdisAcquireSpinLock(&pIocDev->IoLock);

   switch (Type)
   {
      case IOCTL_QCDEV_WAIT_NOTIFY:
      {
   // Got duplicated requests, deny it.
   if (pIocDev->NotificationIrp != NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("CacheNotificationIrp: duplicated req, rejected\n")
      );
      *(ULONG *)pIrp->AssociatedIrp.SystemBuffer = QCDEV_DUPLICATED_NOTIFICATION_REQ;
      pIrp->IoStatus.Information = sizeof( ULONG );
      pIrp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      ntStatus = STATUS_UNSUCCESSFUL;
   }
   else
   {
      // always pend the IRP, never complete it immediately
      ntStatus = STATUS_PENDING;
      IoMarkIrpPending(pIrp);
      pIocDev->NotificationIrp = pIrp;
      IoSetCancelRoutine(pIrp, MPIOC_CancelNotificationRoutine);
   }

         break;
      }

      case IOCTL_QCDEV_IPV4_MTU_NOTIFY:
      {
         // Got duplicated requests, deny it.
         if (pIocDev->IPV4MtuNotificationIrp != NULL)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
               ("CacheNotificationIrp: dup IPV4MtuNotificationIrp, reject\n")
            );
            *(ULONG *)pIrp->AssociatedIrp.SystemBuffer = QCDEV_DUPLICATED_NOTIFICATION_REQ;
            pIrp->IoStatus.Information = sizeof( ULONG );
            pIrp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            ntStatus = STATUS_UNSUCCESSFUL;
         }
         else
         {
            // always pend the IRP, never complete it immediately
            ntStatus = STATUS_PENDING;
            IoMarkIrpPending(pIrp);
            pIocDev->IPV4MtuNotificationIrp = pIrp;
            IoSetCancelRoutine(pIrp, MPIOC_CancelNotificationRoutine);
         }
         break;
      }

      case IOCTL_QCDEV_IPV6_MTU_NOTIFY:
      {
         // Got duplicated requests, deny it.
         if (pIocDev->IPV6MtuNotificationIrp != NULL)
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
               ("CacheNotificationIrp: dup IPV6MtuNotificationIrp, reject\n")
            );
            *(ULONG *)pIrp->AssociatedIrp.SystemBuffer = QCDEV_DUPLICATED_NOTIFICATION_REQ;
            pIrp->IoStatus.Information = sizeof( ULONG );
            pIrp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            ntStatus = STATUS_UNSUCCESSFUL;
         }
         else
         {
            // always pend the IRP, never complete it immediately
            ntStatus = STATUS_PENDING;
            IoMarkIrpPending(pIrp);
            pIocDev->IPV6MtuNotificationIrp = pIrp;
            IoSetCancelRoutine(pIrp, MPIOC_CancelNotificationRoutine);
         }

         break;
      }
   } // switch

   NdisReleaseSpinLock(&pIocDev->IoLock);

   // the dispatch routine will complete the IRP if ntStatus is not pending

   return ntStatus;
}  // MPIOC_CacheNotificationIrp

VOID MPIOC_CancelNotificationRoutine(PDEVICE_OBJECT DeviceObject, PIRP pIrp)
{
   PMPIOC_DEV_INFO pIocDev;
   PMP_ADAPTER     pAdapter;
#ifdef QCUSB_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
#else
   KIRQL levelOrHandle;
#endif
   PFILE_OBJECT    fileObj;
   ULONG           QMIType = 0;
   ANSI_STRING        ansiString;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(pIrp);
   KIRQL irql = KeGetCurrentIrql();

   QCNET_DbgPrintG(("-->MPIOC_CancelNotificationRoutine: 0x%p\n", pIrp));

   IoReleaseCancelSpinLock(pIrp->CancelIrql);

   fileObj = irpStack->FileObject;

   if (fileObj->FileName.Length != 0)
   {
      RtlUnicodeStringToAnsiString(&ansiString, &(fileObj->FileName), TRUE);
      if (RtlCharToInteger( (PCSZ)&(ansiString.Buffer[1]), 10, &QMIType) != STATUS_SUCCESS)
      {
         QCNET_DbgPrintG(("MPIOC 0x%p - invalid FileObject 0x%p\n", pIrp, DeviceObject));
         RtlFreeAnsiString(&ansiString);
         return;
      }
      RtlFreeAnsiString(&ansiString);
   }
   
   pIocDev = MPIOC_FindIoDevice(NULL, DeviceObject, NULL, NULL, pIrp, QMIType);
   if (pIocDev == NULL)
   {
      QCNET_DbgPrintG(("MPIOC: invalid called DO 0x%p\n", DeviceObject));
      return;
   }

   pAdapter = pIocDev->Adapter;

   NdisAcquireSpinLock(&pIocDev->IoLock);

   IoSetCancelRoutine(pIrp, NULL);  // not necessary

   if (pIrp == pIocDev->NotificationIrp)
   {
   pIocDev->NotificationIrp = NULL;
   }
   else if (pIrp == pIocDev->IPV4MtuNotificationIrp)
   {
      pIocDev->IPV4MtuNotificationIrp = NULL;
   }
   else if (pIrp == pIocDev->IPV6MtuNotificationIrp)
   {
      pIocDev->IPV6MtuNotificationIrp = NULL;
   }
   else
   {
      QCNET_DbgPrintG(("MPIOC_CancelNotificationRoutine: unknown IRP 0x%p\n", pIrp));
   }

   pIrp->IoStatus.Status = STATUS_CANCELLED;
   pIrp->IoStatus.Information = 0;

   InsertTailList(&pIocDev->IrpCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
   KeSetEvent(&pIocDev->EmptyCompletionQueueEvent, IO_NO_INCREMENT, FALSE);

   NdisReleaseSpinLock(&pIocDev->IoLock);

   QCNET_DbgPrintG(("<--MPIOC_CancelNotificationRoutine: 0x%p\n", pIrp));

}  // MPIOC_CancelNotificationRoutine

NTSTATUS MPIOC_NotifyClient(PIRP pIrp, ULONG info)
{
   // if (IoSetCancelRoutine(pIrp, NULL) == NULL)
   // {
   //    return STATUS_UNSUCCESSFUL;
   // }
   *(ULONG *)pIrp->AssociatedIrp.SystemBuffer = info;
   pIrp->IoStatus.Information = sizeof( ULONG );
   pIrp->IoStatus.Status = STATUS_SUCCESS;
   return STATUS_SUCCESS;
}  // MPIOC_NotifyClient

void MPIOC_PostRemovalNotification(PMPIOC_DEV_INFO pIocDev)
{
   PMP_ADAPTER pAdapter = pIocDev->Adapter;
   PIRP        tmpIrp;
   NTSTATUS    nts = STATUS_SUCCESS;

   NdisAcquireSpinLock(&pIocDev->IoLock);

   tmpIrp = pIocDev->NotificationIrp;
   if (tmpIrp != NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> PostRemovalNotification: dev 0x%p\n", pAdapter->PortName, pIocDev)
      );
      pIocDev->NotificationIrp = NULL;
      nts = MPIOC_NotifyClient(tmpIrp, MPIOC_REMOVAL_NOTIFICATION);
      if (nts == STATUS_SUCCESS)
      {
         if (IoSetCancelRoutine(tmpIrp, NULL) != NULL)
         {
            InsertTailList(&pIocDev->IrpCompletionQueue, &tmpIrp->Tail.Overlay.ListEntry);
            KeSetEvent(&pIocDev->EmptyCompletionQueueEvent, IO_NO_INCREMENT, FALSE);
         }
         else
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> PostRemovalNotification: IRP in cancallation 0x%p\n", pAdapter->PortName, tmpIrp)
            );
         }
      }
      else
      {
         // cannot be post
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_CRITICAL,
            ("<%s> PostRemovalNotification: critical error: cannot post 0x%p\n", pAdapter->PortName, tmpIrp)
         );
      }
   }
   else
   {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> PostRemovalNotification: no notification IRP\n", pAdapter->PortName)
         );
   }

   NdisReleaseSpinLock(&pIocDev->IoLock);
}  // MPIOC_PostRemovalNotification

VOID MPIOC_PostMtuNotification(PMPIOC_DEV_INFO pIocDev, UCHAR IpVer, ULONG Mtu, BOOLEAN UseSpinlock)
{
   PMP_ADAPTER pAdapter = pIocDev->Adapter;
   PIRP        tmpIrp;
   NTSTATUS    nts = STATUS_SUCCESS;
   BOOLEAN     bPosted = FALSE;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> -->_PostMtuNotification: IPV%d MTU %d for 0x%p(%d, %d)\n", pAdapter->PortName, IpVer, Mtu, pIocDev,
         pIocDev->QMIType, pIocDev->ClientId)
   );

   if (UseSpinlock == TRUE)
   {
      NdisAcquireSpinLock(&pIocDev->IoLock);
   }

   if (IpVer == 4)
   {
      tmpIrp = pIocDev->IPV4MtuNotificationIrp;
   }
   else if (IpVer == 6)
   {
      tmpIrp = pIocDev->IPV6MtuNotificationIrp;
   }
   else
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> <--_PostMtuNotification: unsupported IPV%d\n", pAdapter->PortName, IpVer)
      );
      if (UseSpinlock == TRUE)
      {
         NdisReleaseSpinLock(&pIocDev->IoLock);
      }
      return;
   }

   if (tmpIrp != NULL)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
         ("<%s> PostMtuNotification: IRP 0x%p dev 0x%p (%d, %d)\n", pAdapter->PortName, tmpIrp, pIocDev,
           pIocDev->QMIType, pIocDev->ClientId)
      );
      if (IpVer == 4)
      {
         pIocDev->IPV4MtuNotificationIrp = NULL;
      }
      else
      {
         pIocDev->IPV6MtuNotificationIrp = NULL;
      }
      nts = MPIOC_NotifyClient(tmpIrp, Mtu);
      if (nts == STATUS_SUCCESS)
      {
         if (IoSetCancelRoutine(tmpIrp, NULL) != NULL)
         {
            InsertTailList(&pIocDev->IrpCompletionQueue, &tmpIrp->Tail.Overlay.ListEntry);
            // KeSetEvent(&pIocDev->EmptyCompletionQueueEvent, IO_NO_INCREMENT, FALSE);
            bPosted = TRUE;
         }
         else
         {
            QCNET_DbgPrint
            (
               MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
               ("<%s> PostMtuNotification: IRP in cancallation 0x%p\n", pAdapter->PortName, tmpIrp)
            );
         }
      }
      else
      {
         // cannot be post
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_CRITICAL,
            ("<%s> PostMtuNotification: critical error: cannot post 0x%p\n", pAdapter->PortName, tmpIrp)
         );
      }
   }
   else
   {
      /***
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_VERBOSE,
         ("<%s> PostMtuNotification: no MTU notification IRP from 0x%p(%d, %d)\n", pAdapter->PortName, pIocDev,
            pIocDev->QMIType, pIocDev->ClientId)
      );
      ***/
   }

   if (UseSpinlock == TRUE)
   {
      NdisReleaseSpinLock(&pIocDev->IoLock);
   }

   if (bPosted == TRUE)
   {
      MPIOC_EmptyIrpCompletionQueue(pIocDev);
   }

}  // MPIOC_PostMtuNotification

VOID MPIOC_AdvertiseMTU(PMP_ADAPTER pAdapter, UCHAR IpVersion, ULONG Mtu)
{
   PMPIOC_DEV_INFO pIocDev, foundDO = NULL;
   PLIST_ENTRY     headOfList, peekEntry;

   if ((IpVersion != 4) && (IpVersion != 6))
   {
      return;
   }

   USBIF_SetDataMTU(pAdapter->USBDo, Mtu);

   NdisAcquireSpinLock(MP_CtlLock);

   if (!IsListEmpty(&MP_DeviceList))
   {
      headOfList = &MP_DeviceList;
      peekEntry = headOfList->Flink;

      while (peekEntry != headOfList)
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      MPIOC_DEV_INFO,
                      List
                   );
         if (pIocDev->Adapter == pAdapter)
         {
            MPIOC_PostMtuNotification(pIocDev, IpVersion, Mtu, FALSE);
         }
         peekEntry = peekEntry->Flink;
      } // while
   }

   NdisReleaseSpinLock(MP_CtlLock);

}  // MPIOC_AdvertiseMTU

NTSTATUS MPIOC_CancelNotificationIrp(PMPIOC_DEV_INFO pIocDev, ULONG Type)
{
   PMP_ADAPTER pAdapter = pIocDev->Adapter;
   NTSTATUS    ntStatus = STATUS_SUCCESS;
   PIRP        pIrp;
   PCHAR       pTag;

   NdisAcquireSpinLock(&pIocDev->IoLock);

   if (Type == IOCTL_QCDEV_WAIT_NOTIFY)
   {
   pIrp = pIocDev->NotificationIrp;
      pTag = "RM";
   }
   else if (Type == IOCTL_QCDEV_IPV4_MTU_NOTIFY)
   {
      pIrp = pIocDev->IPV4MtuNotificationIrp;
      pTag = "V4";
   }
   else if (Type == IOCTL_QCDEV_IPV6_MTU_NOTIFY)
   {
      pIrp = pIocDev->IPV6MtuNotificationIrp;
      pTag = "V6";
   }
   else
   {
      pIrp = NULL;
      pTag = "NONE";
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> -->MPIOC_CancelNotificationIrp: <%s> 0x%p\n", pAdapter->PortName, pTag, pIrp)
   );

   if (pIrp != NULL)
   {

      if (IoSetCancelRoutine(pIrp, NULL) != NULL)
      {
         if (Type == IOCTL_QCDEV_WAIT_NOTIFY)
         {
         pIocDev->NotificationIrp = NULL;
         }
         else if (Type == IOCTL_QCDEV_IPV4_MTU_NOTIFY)
         {
            pIocDev->IPV4MtuNotificationIrp = NULL;
         }
         else if (Type == IOCTL_QCDEV_IPV6_MTU_NOTIFY)
         {
            pIocDev->IPV6MtuNotificationIrp = NULL;
         }
         pIrp->IoStatus.Status = STATUS_CANCELLED;
         pIrp->IoStatus.Information = 0;

         InsertTailList(&pIocDev->IrpCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
         KeSetEvent(&pIocDev->EmptyCompletionQueueEvent, IO_NO_INCREMENT, FALSE);
      }
      else
      {
         QCNET_DbgPrint
         (
            MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
            ("<%s> MPIOC_CancelNotificationIrp: IRP in cancallation 0x%p\n", pAdapter->PortName, pIrp)
         );
      }
   }
   NdisReleaseSpinLock(&pIocDev->IoLock);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPIOC_CancelNotificationIrp: <%s> 0x%p/0x%p/0x%p\n", pAdapter->PortName, pTag,
        pIocDev->NotificationIrp, pIocDev->IPV4MtuNotificationIrp, pIocDev->IPV6MtuNotificationIrp)
   );

   return ntStatus;
}  // MPIOC_CancelNotificationIrp

// This function must be called within MP_CtlLock
ULONG MPIOC_GetClientOpenCount(VOID)
{
   PMPIOC_DEV_INFO pIocDev;
   PLIST_ENTRY     headOfList, peekEntry;
   ULONG           count = 0;

   if (!IsListEmpty(&MP_DeviceList))
   {
      headOfList = &MP_DeviceList;
      peekEntry = headOfList->Flink;

      while (peekEntry != headOfList)
      {
         pIocDev = CONTAINING_RECORD
                   (
                      peekEntry,
                      MPIOC_DEV_INFO,
                      List
                   );

         count += pIocDev->DeviceOpenCount;
         peekEntry = peekEntry->Flink;
      }
   }

   return count;
}  // MPIOC_GetClientOpenCount

BOOLEAN MPIOC_LocalProcessing(PIRP Irp)
{
   PIO_STACK_LOCATION  irpStack = IoGetCurrentIrpStackLocation(Irp);
   BOOLEAN localProcessing = TRUE;

   switch (irpStack->MajorFunction)
   {
      case IRP_MJ_CLEANUP:
      case IRP_MJ_CLOSE:
      {
         break;
      }

      case IRP_MJ_DEVICE_CONTROL:
      {
         switch (irpStack->Parameters.DeviceIoControl.IoControlCode)
         {
            case IOCTL_QCDEV_SET_DBG_UMSK:
            {
               break;
            }

            default:
            {
               localProcessing = FALSE;
               break;
            }
         }

         break;
      }

      default:
      {
         localProcessing = FALSE;
         break;
      }

   }  // switch

   return localProcessing;

}  // MPIOC_LocalProcessing

NDIS_STATUS MPIOC_ResetDeviceSecurity
(
   PMP_ADAPTER     pAdapter,
   PUNICODE_STRING DeviceLinkName
)
{
   HANDLE               doHandle = NULL;
   NTSTATUS             ntStatus;
   OBJECT_ATTRIBUTES    objectAttr;
   IO_STATUS_BLOCK      ioStatus;
   PSECURITY_DESCRIPTOR pSecurityDesc;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_TRACE,
      ("<%s> -->_ResetDeviceSecurity\n", pAdapter->PortName)
   );

   // prepare object attributes for ZwOpenFile
   InitializeObjectAttributes
   (
      &objectAttr,
      DeviceLinkName,
      OBJ_KERNEL_HANDLE,  // can only be accessed in kernel mode
      NULL,
      NULL
   );

   // Allocate security descriptor
   pSecurityDesc = ExAllocatePool(NonPagedPool, PAGE_SIZE);
   if (pSecurityDesc == NULL)
   {
      ntStatus = STATUS_INSUFFICIENT_RESOURCES;
      goto ExitPoint;
   }
   RtlZeroMemory(pSecurityDesc, PAGE_SIZE);

   // create a new security descriptor which has no security constraints
   ntStatus = RtlCreateSecurityDescriptor
              (
                 pSecurityDesc,
                 SECURITY_DESCRIPTOR_REVISION
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> _ResetDeviceSecurity: failure A 0x%x\n", pAdapter->PortName, ntStatus)
      );
      goto ExitPoint;
   }

   // Open the device to get handle to our device object
   ntStatus = ZwOpenFile
              (
                 &doHandle,
                 WRITE_DAC,
                 &objectAttr,
                 &ioStatus,
                 0,
                 0
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> _ResetDeviceSecurity: failure B 0x%x\n", pAdapter->PortName, ntStatus)
      );
      goto ExitPoint;
   }

   // now, we set security settings without security constraints
   ntStatus = ZwSetSecurityObject
              (
                 doHandle,
                 DACL_SECURITY_INFORMATION,
                 pSecurityDesc
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_ERROR,
         ("<%s> _ResetDeviceSecurity: failure C 0x%x\n", pAdapter->PortName, ntStatus)
      );
      ZwClose(doHandle);
      goto ExitPoint;
   }

   ntStatus = ZwClose(doHandle);

ExitPoint:

   if (pSecurityDesc != NULL)
   {
      ExFreePool(pSecurityDesc);
   }

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_TRACE,
      ("<%s> <--_ResetDeviceSecurity 0x%x\n", pAdapter->PortName, ntStatus)
   );
   return ntStatus;

} // MPIOC_ResetDeviceSecurity

NTSTATUS MPIOC_GetPeerDeviceNameCompletion
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
)
{
   PMP_ADAPTER pAdapter = (PMP_ADAPTER)pContext;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> MPIOC_GetPeerDeviceNameCompletion (IRP 0x%p IoStatus: 0x%x)\n", pAdapter->PortName,
        pIrp, pIrp->IoStatus.Status)
   );

   KeSetEvent(pIrp->UserEvent, 0, FALSE);

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // MPIOC_GetPeerDeviceNameCompletion

NTSTATUS MPIOC_GetPeerDeviceName(PMP_ADAPTER pAdapter, PIRP Irp, ULONG BufLen)
{
   PIRP pIrp = NULL;
   PIO_STACK_LOCATION nextstack;
   NTSTATUS Status;
   KEVENT Event;
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pAdapter->USBDo->DeviceExtension;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> -->MPIOC_GetPeerDeviceName\n", pAdapter->PortName)
   );

   KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

   pIrp = IoAllocateIrp((CCHAR)(pDevExt->StackDeviceObject->StackSize+2), FALSE );
   if( pIrp == NULL )
   {
       QCNET_DbgPrint
       (
          MP_DBG_MASK_CONTROL,
          MP_DBG_LEVEL_ERROR,
          ("<%s> MPIOC_GetPeerDeviceName failed to allocate an IRP\n", pAdapter->PortName)
       );
       return NDIS_STATUS_FAILURE;
   }

   pIrp->AssociatedIrp.SystemBuffer = Irp->AssociatedIrp.SystemBuffer;

   // set the event
   pIrp->UserEvent = &Event;

   nextstack = IoGetNextIrpStackLocation(pIrp);
   nextstack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
   nextstack->Parameters.DeviceIoControl.IoControlCode = IOCTL_QCDEV_GET_PEER_DEV_NAME;
   nextstack->Parameters.DeviceIoControl.OutputBufferLength = BufLen;

   IoSetCompletionRoutine
   (
      pIrp,
      (PIO_COMPLETION_ROUTINE)MPIOC_GetPeerDeviceNameCompletion,
      (PVOID)pAdapter,
      TRUE,TRUE,TRUE
   );

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> MPIOC_GetPeerDeviceName (IRP 0x%p)\n", pAdapter->PortName, pIrp)
   );

   Status = IoCallDriver(pDevExt->StackDeviceObject, pIrp);

   KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, 0);

   Status = pIrp->IoStatus.Status;

   Irp->IoStatus.Status = Status;
   Irp->IoStatus.Information = pIrp->IoStatus.Information;

   MPMAIN_PrintBytes
   (
      Irp->AssociatedIrp.SystemBuffer,
      Irp->IoStatus.Information,
      Irp->IoStatus.Information,
      "PEER-INFO",
      pAdapter,
      MP_DBG_MASK_CONTROL, MP_DBG_LEVEL_DETAIL
   );

   IoFreeIrp(pIrp);

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_TRACE,
      ("<%s> <--MPIOC_GetPeerDeviceName (IRP 0x%p ST 0x%x)\n", pAdapter->PortName, Irp, Status)
   );
   return Status;
}  // MPIOC_GetPeerDeviceName

// Retrieve the primary adapter
PMP_ADAPTER GetPrimaryAdapter(PMP_ADAPTER pAdapter)
{
   PMP_ADAPTER       pTempAdapter = NULL;

   DisconnectedAllAdapters(pAdapter, &pTempAdapter);

   return pTempAdapter;
}  // GetPrimaryAdapter

NTSTATUS MPIOC_GetPrimaryAdapterName(PMP_ADAPTER pAdapter, PIRP Irp, ULONG BufLen)
{
   PMP_ADAPTER pPrimaryAdapter = NULL;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   int nameLen;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_ERROR,
      ("<%s> -->MPIOC_GetPrimaryAdapterName\n", pAdapter->PortName)
   );

   pPrimaryAdapter = GetPrimaryAdapter(pAdapter);

   if (pPrimaryAdapter == NULL)
   {
      ntStatus = STATUS_UNSUCCESSFUL;
      Irp->IoStatus.Status = ntStatus;
      Irp->IoStatus.Information = 0;

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> MPIOC_GetPrimaryAdapterName: failed to get primary adapter\n", pAdapter->PortName)
      );
      return ntStatus;
   }

   // nameLen = strlen(pPrimaryAdapter->FriendlyName);
   nameLen = pAdapter->FriendlyNameWLen;

   if (BufLen <= nameLen)
   {
      ntStatus = STATUS_BUFFER_TOO_SMALL;
      Irp->IoStatus.Status = ntStatus;
      Irp->IoStatus.Information = 0;

      QCNET_DbgPrint
      (
         MP_DBG_MASK_CONTROL,
         MP_DBG_LEVEL_ERROR,
         ("<%s> MPIOC_GetPrimaryAdapterName: buffer too small\n", pAdapter->PortName)
      );
      return ntStatus;
   }

   RtlZeroMemory(Irp->AssociatedIrp.SystemBuffer, BufLen);
   RtlCopyMemory
   (
      Irp->AssociatedIrp.SystemBuffer,
      pPrimaryAdapter->FriendlyNameW,
      nameLen
   );
   Irp->IoStatus.Status = ntStatus;
   Irp->IoStatus.Information = nameLen;

   QCNET_DbgPrint
   (
      MP_DBG_MASK_CONTROL,
      MP_DBG_LEVEL_DETAIL,
      ("<%s> <--MPIOC_GetPrimaryAdapterName <%ws> %dB\n", pAdapter->PortName,
        pPrimaryAdapter->FriendlyNameW, pAdapter->FriendlyNameWLen)
   );

   return ntStatus;
}  // MPIOC_GetPrimaryAdapterName

#endif  // defined(IOCTL_INTERFACE)
