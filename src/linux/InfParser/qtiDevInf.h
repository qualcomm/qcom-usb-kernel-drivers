/*===========================================================================
FILE:
   qtiDevInf.h
DESCRIPTION:
   Linux Serial USB driver Implementation for QTI hardwar

   Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
   SPDX-License-Identifier: BSD-3-Clause
==========================================================================*/
#ifndef QTIDEVINF_H
#define QTIDEVINF_H

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#define QTIDEV_INF_DEFAULT_VENDOR    "QTI"  /**< Default Vendor */

#define QTIDEV_INF_CLASS_STR         "Class"     /**< INF 'class' Section    */
#define QTIDEV_INF_VERSION_STR       "Version"   /**< INF 'version' Section  */
#define QTIDEV_INF_STRING_STR        "String"    /**< INF 'string' Section   */
#define QTIDEV_INF_MANUFACTURER_STR  "Manufacturer"/**< INF Manufacturer sec */

#define QTIDEV_INF_MAX_KEY_SIZE      64      /**< Maximum key size       */
#define QTIDEV_INF_MAX_LINE_SIZE (512)      /**< Maximum INF Line size  */

#define QTIDEV_INF_DICT_OPERATOR ":="        /**< Possible dict operators    */
#define QTIDEV_INF_START_SECTION "["         /**< Possible start operators   */
#define QTIDEV_INF_END_SECTION   "]"         /**< Possible end operators     */
#define QTIDEV_INF_START_COMMENT_PREFIXES    ";#" /**< Possible comments     */

#define QTIDEV_INF_CLASS_NET_STR     "Net"   /**< For WWan entry   */
#define QTIDEV_INF_CLASS_MODEM_STR   "Modem" /**< For Dun entry    */
#define QTIDEV_INF_CLASS_PORTS_STR   "Ports" /**< For Ports entry  */
#define QTIDEV_INF_CLASS_USB_STR     "USB"   /**< For QDSS entry   */

#define QC_GLOBAL(format, args...)    { \
    if(debug_g == 1) \
        printk( KERN_INFO "%s:%d "format,__FUNCTION__, __LINE__, ##args) ;\
}

#define QC_LOG_GLOBAL(format,args...)    QC_GLOBAL(format, ## args)
#define QC_LOG_ERR(format,args...)    printk( KERN_ERR "%s:%d " format, __FUNCTION__, __LINE__, ##args)
#define QC_LOG_INFO(format,args...)    printk( KERN_INFO "%s:%d " format, __FUNCTION__, __LINE__, ##args)


/**
 * @brief           QTIdev device type Info
 */
typedef enum {
    QTIDEV_INF_TYPE_TRACE_IN,    /**< Type QDSS Trace/ Bulk in   */
    QTIDEV_INF_TYPE_DPL,         /**< Type QDSS DPL/ Bulk in     */
    QTIDEV_INF_TYPE_BULK,        /**< Type QDSS Bulk             */
    /* @note : After extracting data from INF file
     *         we only get the info whether the device is 'DPL (or) Bulk'
     *         need to extract further for 'Bulk in/out'/ 'Trace', based
     *         on number of endpoints
     */
    QTIDEV_INF_TYPE_BULK_IN_OUT, /**< Type QDSS Bulk In Out  */
    QTIDEV_INF_TYPE_LPC,         /**< Type LPC Bulk In Out   */
    QTIDEV_INF_TYPE_UNKNOWN      /**< Unknown QDSS Type      */
} QTIdevtype;

/**
 * @brief           device Class Info (ex: Net, Modem ..etc)
 */
typedef enum {
    QTIDEV_INF_CLASS_NET = 0,    /**< For WWan entry     */
    QTIDEV_INF_CLASS_MODEM,      /**< For Dun entry      */
    QTIDEV_INF_CLASS_PORTS,      /**< For Ports entry    */
    QTIDEV_INF_CLASS_USB,        /**< For QDSS entry     */
    QTIDEV_INF_CLASS_UNKNOWN     /**< Unknown Class Type */
} deviceClass;

typedef enum {
    QTIDEV_INF_VERSION = 0,      /**< Inf 'version' info     */
    QTIDEV_INF_MANUFACTURER,     /**< Inf 'manufacturer' info*/
    QTIDEV_INF_DEVICE_INFO,      /**< Inf 'device info' info */
    QTIDEV_INF_STRING,           /**< Inf 'string' info      */
    QTIDEV_INF_UNKNOWN           /**< Unknown Section        */
} sectionInfo_t;

typedef struct _devInfo {
    QTIdevtype       mDevType;                     /**< Device type */
    char            mpKey[QTIDEV_INF_MAX_KEY_SIZE]; /**< device friendly name */
    int             mVid_pid_iface[3];            /**< VID/PID/INT          */
} devInfo_t;

typedef struct _fileInfo {
    deviceClass         mClass;     /**< INF/Config file class  */
    unsigned int        mLength;    /**< Number of devices can occupy */
    unsigned int        mNumResp;   /**< Number of devices present */
    struct _devInfo     mDevInfo[0];
}fileInfo_t;

/**
 * @brief To extract the device info from the stored data
 *
 * Returns the device info, which is been extracted from
 * INF file and strored in pFileInfo
 *
 * @param   pIface      usb_interface info
 * @param   pFileInfo   global Ctx, contains INF info
 *
 * @returns devInfo on Success / NULL on Failure
 */
void* QTIDevInfGetDevInfo(struct usb_interface *pIface, fileInfo_t *pFileInfo);

/**
 * @brief To verify whether the INF/config file is modified
 *
 * Returns the file status
 *
 * @param   pFileInfo    global Ctx, contains INF info
 * @param   pFilePath    INF/Config file path
 *
 * @returns true    - If no change in config file
 *          false   - Config file got modified
 *          negative error - If file got corrupted/removed
 */
int QTIDevInfCheckFileStatus(fileInfo_t *pFileInfo, char *pFilePath);

/**
 * @brief To get number of entries present in INF/config file
 *
 * Returns the count of device entries
 *
 * @param   pFileInfo    global Ctx, contains INF info
 * @param   pFilePath    INF/Config file path
 *
 * @returns count    - number of devices present
 *          negative error - If file got corrupted/removed
 */
int QTIDevInfEntrySize(void *pFilePath);

/**
 * @brief To extract the device info and store in ctx
 *
 * extracts the device info from file 'pFilePath', and stores in pFileInfo
 *
 * @param   pFilePath    INF/Config file path
 * @param   pFileInfo    global Ctx, contains INF info
 *
 * @returns 0 on Success
 *          negative error - If file got corrupted/removed/invalid
 */
int QTIDevInfParse(void *pFilePath, fileInfo_t *pFileInfo);

/**
 * @brief To extract the device friendly name from source string
 *
 * @param   name   return device friendly name
 * @param   src    source pointer of device friendly name
 * @param   len    source length
 *
 * @returns 0 on Success
 *          negative error - If file got corrupted/removed/invalid
 */
void extractDevName(char *name, char *src, int len);

#endif
