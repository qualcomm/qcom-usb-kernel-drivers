/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

                             U S B I O C . H

GENERAL DESCRIPTION
  This file contains definitions for IOCTL operations.

  Copyright (c) 2014 Qualcomm Technologies, Inc.
  All rights reserved.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
#ifndef USBIOC_H
#define USBIOC_H

#include "USBMAIN.h"
                      
// User-defined IOCTL code range: 2048-4095
#define QCDEV_IOCTL_INDEX                   2048
#define QCDEV_DUPLICATED_NOTIFICATION_REQ 0x00000002L

/*
 * IOCTLS requiring this header file.
 */

#define IOCTL_QCDEV_WAIT_NOTIFY CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3333 */
#define IOCTL_QCDEV_DRIVER_ID   CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1285, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3334 */
#define IOCTL_QCDEV_GET_SERVICE_KEY CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1286, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3335 */
#define IOCTL_QCDEV_SEND_CONTROL CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1287, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3336 */
#define IOCTL_QCDEV_READ_CONTROL CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1288, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3337 */
#define IOCTL_QCDEV_GET_HDR_LEN CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1289, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3338 - USB debug mask */
#define IOCTL_QCDEV_SET_DBG_UMSK CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1290, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3340 - MP service file */
#define IOCTL_QCDEV_GET_SERVICE_FILE CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+1292, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3343 - QMI Client Id  */
#define IOCTL_QCDEV_QMI_GET_CLIENT_ID CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+1295, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

/* Make the following code as 3344 - versions of QMI services  */
#define IOCTL_QCDEV_QMI_GET_SVC_VER CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+1296, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

/* Make the following code as 3345 - MEID numbers  */
#define IOCTL_QCDEV_QMI_GET_MEID CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+1297, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

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

/* Make the following code as 3349 - Device Remove Event - Pass the event name in ASCII*/
#define IOCTL_QCDEV_DEVICE_REMOVE_EVENTA CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                                 QCDEV_IOCTL_INDEX+1301, \
                                                 METHOD_BUFFERED, \
                                                 FILE_ANY_ACCESS)
   
   /* Make the following code as 3350 - Device Remove Event - Pass the event name in UNICODE*/
#define IOCTL_QCDEV_DEVICE_REMOVE_EVENTW CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                                 QCDEV_IOCTL_INDEX+1302, \
                                                 METHOD_BUFFERED, \
                                                 FILE_ANY_ACCESS)
   
   /* Make the following code as 3351 - Event close - 
   Pass the unique identifier returned as part of IOCTL_QCDEV_DEVICE_REMOVE_EVENT(W/A) */
#define IOCTL_QCDEV_DEVICE_EVENT_CLOSE CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                                 QCDEV_IOCTL_INDEX+1303, \
                                                 METHOD_BUFFERED, \
                                                 FILE_ANY_ACCESS)

/* Make the following code as 3352 - Device ID from filter device*/
#define IOCTL_QCDEV_REQUEST_DEVICEID CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                                 QCDEV_IOCTL_INDEX+1304, \
                                                 METHOD_BUFFERED, \
                                                 FILE_ANY_ACCESS)

/* Make the following code as 3353 - return grouping identifier*/
#define IOCTL_QCDEV_DEVICE_GROUP_INDETIFIER CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                                 QCDEV_IOCTL_INDEX+1305, \
                                                 METHOD_BUFFERED, \
                                                 FILE_ANY_ACCESS)

/* Make the following code as 3354 - Parent device name from filter device*/
#define IOCTL_QCDEV_GET_PARENT_DEV_NAME CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                               QCDEV_IOCTL_INDEX+1306, \
                                               METHOD_BUFFERED, \
                                               FILE_ANY_ACCESS)

/* Make the following code as 3345 */
#define IOCTL_QCUSB_SYSTEM_POWER CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+20, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3346 */
#define IOCTL_QCUSB_DEVICE_POWER CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+21, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as 3347 */
#define IOCTL_QCUSB_QCDEV_NOTIFY CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                  QCDEV_IOCTL_INDEX+22, \
                                  METHOD_BUFFERED, \
                                  FILE_ANY_ACCESS)

/* Make the following code as xxxx - versions of QMI services  */
#define IOCTL_QCDEV_QMI_GET_SVC_VER_EX CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+23, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

/* Make the following code as xxxx - loopback data packet  */
#define IOCTL_QCDEV_LOOPBACK_DATA_PKT CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+24, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

#ifdef QCUSB_MUX_PROTOCOL

#define IOCTL_QCDEV_RESUME_DL CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+25, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#endif // QCUSB_MUX_PROTOCOL

#define IOCTL_QCDEV_PAUSE_QMAP_DL CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+28, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_RESUME_QMAP_DL CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+29, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#ifdef QCMP_DISABLE_QMI

#define IOCTL_QCDEV_MEDIA_CONNECT CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+26, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_MEDIA_DISCONNECT CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+27, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#endif // QCMP_DISABLE_QMI

#define IOCTL_QCDEV_INJECT_PACKET CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+30, \
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

#define IOCTL_QCDEV_GET_PRIMARY_ADAPTER_NAME CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                              QCDEV_IOCTL_INDEX+35, \
                                              METHOD_BUFFERED, \
                                              FILE_ANY_ACCESS)

#define IOCTL_QCDEV_IPV4_MTU_NOTIFY CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                             QCDEV_IOCTL_INDEX+50, \
                                             METHOD_BUFFERED, \
                                             FILE_ANY_ACCESS)

#define IOCTL_QCDEV_IPV6_MTU_NOTIFY CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                             QCDEV_IOCTL_INDEX+51, \
                                             METHOD_BUFFERED, \
                                             FILE_ANY_ACCESS)

#define IOCTL_QCDEV_QMI_READY CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+52, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_GET_QMI_QUEUE_SIZE CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+53, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_PURGE_QMI_QUEUE CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+54, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

#define IOCTL_QCDEV_GET_NET_STATISTICS CTL_CODE(FILE_DEVICE_UNKNOWN, \
                                       QCDEV_IOCTL_INDEX+55, \
                                       METHOD_BUFFERED, \
                                       FILE_ANY_ACCESS)

NTSTATUS USBIOC_CacheNotificationIrp
(
   PDEVICE_OBJECT DeviceObject,
   PVOID ioBuffer,
   PIRP pIrp
);
NTSTATUS USBIOC_NotifyClient(PIRP pIrp, ULONG info);

NTSTATUS USBIOC_GetDriverGUIDString
(
   PDEVICE_OBJECT DeviceObject,
   PVOID ioBuffer,
   PIRP pIrp
);
NTSTATUS USBIOC_GetServiceKey
(
   PDEVICE_OBJECT DeviceObject,
   PVOID ioBuffer,
   PIRP pIrp
);
NTSTATUS USBIOC_GetDataHeaderLength
(
   PDEVICE_OBJECT DeviceObject,
   PVOID          pInBuf,
   PIRP           pIrp
);

#endif // USBIOC_H
