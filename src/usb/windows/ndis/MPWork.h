/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          M P W O R K . H

GENERAL DESCRIPTION
  This module contains definitions for miniport worker thread

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef MPWORK_H
#define MPWORK_H

#include "MPMain.h"

BOOLEAN MPWork_ScheduleWorkItem(PMP_ADAPTER pAdapter);

VOID MPWork_WorkItem(PNDIS_WORK_ITEM pWorkItem, PVOID pContext);

BOOLEAN MPWork_IndicateCompleteTx(PMP_ADAPTER pAdapter);

BOOLEAN MPWork_PostDataReads(PMP_ADAPTER  pAdapter);

BOOLEAN MPWork_PostControlReads(PMP_ADAPTER  pAdapter);

PMP_OID_WRITE MPWork_FindRequest(PMP_ADAPTER pAdapter, ULONG ulTxID);

VOID dumpOIDList( PMP_ADAPTER pAdapter );

BOOLEAN MPWork_ResolveRequests(PMP_ADAPTER pAdapter);

BOOLEAN MPWork_PostWriteRequests( PMP_ADAPTER pAdapter );

NTSTATUS MPWork_StartWorkThread(PMP_ADAPTER pAdapter);

NTSTATUS MPWork_CancelWorkThread(PMP_ADAPTER pAdapter);

VOID MPWork_WorkThread(PVOID Context);

#endif // MPWORK_H
