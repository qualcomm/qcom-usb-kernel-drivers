/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q D B D E V . C

GENERAL DESCRIPTION
  This is the file which contains dispatch function definitions for QDSS driver.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef QDBDSP_H
#define QDBDSP_H

VOID QDBDSP_IoDeviceControl
(
   WDFQUEUE   Queue,
   WDFREQUEST Request,
   size_t     OutputBufferLength,
   size_t     InputBufferLength,
   ULONG      IoControlCode
);

VOID QDBDSP_IoStop
(
   WDFQUEUE   Queue,
   WDFREQUEST Request,
   ULONG      ActionFlags
);

VOID QDBDSP_IoResume
(
   WDFQUEUE   Queue,
   WDFREQUEST Request
);

NTSTATUS QDBDSP_IrpIoCompletion
(
   PDEVICE_OBJECT DeviceObject,
   PIRP           Irp,
   PVOID          Context
);

NTSTATUS QDBDSP_GetParentId
(
   PDEVICE_CONTEXT pDevContext,
   PVOID           IoBuffer,
   ULONG           BufferLen
);

#endif // QDBDSP_H
