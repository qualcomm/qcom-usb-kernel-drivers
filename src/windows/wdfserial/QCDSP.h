/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef QCDSP_H
#define QCDSP_H

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL QCDSP_EvtIoDeviceControl;
EVT_WDF_IO_QUEUE_STATE QCDSP_EvtIoReadQueueReady;   // Rx queue state change
EVT_WDF_IO_QUEUE_STATE QCDSP_EvtIoWriteQueueReady;  // Tx queue state change

#endif
