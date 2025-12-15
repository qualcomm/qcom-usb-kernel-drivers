/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                          Q C F I L T E R I O C . h

GENERAL DESCRIPTION

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

#ifndef FILTERIOC_H
#define FILTERIOC_H

// User-defined IOCTL code range: 2048-4095
#define QCDEV_IOCTL_INDEX                   2048

/*
 * IOCTLS requiring this header file.
 */

/* Make the following code as 3346 */
#define IOCTL_QCDEV_CREATE_CTL_FILE CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+1298, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

/* Make the following code as 3347 */
#define IOCTL_QCDEV_CLOSE_CTL_FILE CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+1299, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

/* Make the following code as 3352 - Device ID from filter device*/
#define IOCTL_QCDEV_REQUEST_DEVICEID CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                               QCDEV_IOCTL_INDEX+1304, \
                                               METHOD_BUFFERED, \
                                               FILE_ANY_ACCESS)

/* Make the following code as 3354 - Parent device name from filter device*/
#define IOCTL_QCDEV_GET_PARENT_DEV_NAME CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                               QCDEV_IOCTL_INDEX+1306, \
                                               METHOD_BUFFERED, \
                                               FILE_ANY_ACCESS)

#define IOCTL_QCDEV_REPORT_DEV_NAME CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+34, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_GET_PEER_DEV_NAME CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+32, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_GET_MUX_INTERFACE CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+33, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

#endif // FILTERIOC_H

