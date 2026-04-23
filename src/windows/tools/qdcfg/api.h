/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          A P I . H

GENERAL DESCRIPTION
    This file defines IOCTL codes, constants, and function prototypes
    for the qdcfg driver configuration API.

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef API_H
#define API_H

#include <winioctl.h>

typedef enum _QMI_SERVICE_TYPE
{
    QMUX_TYPE_CTL = 0x00,
    QMUX_TYPE_WDS = 0x01,
    QMUX_TYPE_DMS = 0x02,
    QMUX_TYPE_NAS = 0x03,
    QMUX_TYPE_QOS = 0x04,
    QMUX_TYPE_WMS = 0x05,
    QMUX_TYPE_PDS = 0x06,
    QMUX_TYPE_MAX,
    QMUX_TYPE_ALL = 0xFF
} QMI_SERVICE_TYPE;

#define SERVICE_FILE_BUF_LEN    256
#define QMUX_NUM_THREADS        3
#define QMUX_MAX_DATA_LEN       2048
#define QMUX_MAX_CMD_LEN        2048

// User-defined IOCTL code range: 2048-4095
#define QCDEV_IOCTL_INDEX                   2048
#define QCOMSER_IOCTL_INDEX                 2048
#define QCDEV_DUPLICATED_NOTIFICATION_REQ   0x00000002L

#define IOCTL_QCDEV_WAIT_NOTIFY CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3338 - USB debug mask */
#define IOCTL_QCDEV_SET_DBG_UMSK CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1290, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3339 - MP debug mask */
#define IOCTL_QCDEV_SET_DBG_MMSK CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1291, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3340 - MP debug mask */
#define IOCTL_QCDEV_GET_SERVICE_FILE CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1292, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3343 - QMI Client Id  */
#define IOCTL_QCDEV_QMI_GET_CLIENT_ID CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+1295, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

#define IOCTL_QCDEV_RESUME_DL CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+25, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_MEDIA_CONNECT CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+26, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_MEDIA_DISCONNECT CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+27, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

/* Make the following code as 3338 - USB debug mask */
#define IOCTL_QCUSB_SET_DBG_UMSK CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCOMSER_IOCTL_INDEX+30, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

#define IOCTL_QCDEV_PAUSE_QMAP_DL CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+28, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_RESUME_QMAP_DL CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+29, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

typedef VOID(_stdcall *NOTIFICATION_CALLBACK)(HANDLE, ULONG);

typedef struct _NOTIFICATION_CONTEXT
{
    HANDLE                ServiceHandle;
    NOTIFICATION_CALLBACK CallBack;
} NOTIFICATION_CONTEXT, *PNOTIFICATION_CONTEXT;

#endif // API_H
