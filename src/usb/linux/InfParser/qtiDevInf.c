/*===========================================================================
FILE:
   qtiDevInf.c
DESCRIPTION:
   Linux Serial USB driver Implementation for QTI hardwar

Copyright (c) 2019,2020 The Linux Foundation. All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
==========================================================================*/

#include "qtiDevInf.h"
#include "../version.h"

#define DRIVER_DESC "QTISubsystemUSB"

int debug_g=0;//For global logging

static int processData(fileInfo_t *pFileInfo, struct file *filp, bool flag);

/**
 * @brief   Local structure to store the available manufacturer list
 */
typedef struct _manufacturerInfo {
    char name[QTIDEV_INF_MAX_KEY_SIZE];
    struct _manufacturerInfo *next;
} manufacturerInfo_t;

/**
 * @brief To extract the device info from the stored data
 *
 * Returns the device info, which is been extracted from
 * INF file and strored in fileInfo
 *
 * @param   pIface      usb_interface info
 * @param   pFileInfo   global Ctx, contains INF info
 *
 * @returns devInfo on Success / NULL on Failure
 */
void* QTIDevInfGetDevInfo(struct usb_interface *pIface, fileInfo_t *pFileInfo)
{
    devInfo_t   *devInfo;
    unsigned int idx = 0;
    unsigned int ifaceNum;
    struct usb_device   *dev;

    /* extract devInfo, dev */
    dev = interface_to_usbdev(pIface);
    if (!pIface || !pFileInfo || !(devInfo = pFileInfo->mDevInfo)||(!dev))
    {
        QC_LOG_ERR("Invalid usb device data\n");
        return NULL;
    }

    QC_LOG_GLOBAL("VID: %X,  PID: %X, InterfaceNumber: %X\n", dev->descriptor.idVendor,
            dev->descriptor.idProduct,
            pIface->cur_altsetting->desc.bInterfaceNumber);

    ifaceNum = pIface->cur_altsetting->desc.bInterfaceNumber;
    /* Traverse in the stored ctx and extract the matched dev info */
    for (idx = 0; idx <= pFileInfo->mNumResp; idx++, devInfo++)
    {
        if ((devInfo->mVid_pid_iface[1] == dev->descriptor.idProduct) &&
             devInfo->mVid_pid_iface[2] == ifaceNum)
        {
            break;
        }
    }

    return (idx <= pFileInfo->mNumResp) ? devInfo : NULL;
}
EXPORT_SYMBOL(QTIDevInfGetDevInfo);

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
int QTIDevInfCheckFileStatus(fileInfo_t *pFileInfo, char *pFilePath)
{
    struct file *filp = NULL;

    int ret = false;

    if (!pFileInfo || !pFilePath)
    {
        QC_LOG_ERR("Invalid data, will be using the stored data\n");
        /* We can ignore this, and can reuse the exixting data */
        return -ENXIO;
    }

    filp = filp_open(pFilePath, O_RDONLY, 0444);
    if (IS_ERR(filp))
    {
        QC_LOG_ERR("Error in opening file, will be using the stored data\n");
        /* Even if we get error, we can reuse the existing data
         * This situation occurs if the "config file gets deleted"
         */
        return -ENXIO;
    }

    filp_close(filp, NULL);
    return ret;
}
EXPORT_SYMBOL(QTIDevInfCheckFileStatus);

static int MyReadLine(struct file *filp, char *data, int len, loff_t *offset)
{
    char val;
    int i = 0;

    if (!filp || !data || len <= 0)
        return -EINVAL;

    while (i < len)
    {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4,13,16))
        if (kernel_read(filp, &val, 1, offset) <= 0)
#else
        if (kernel_read(filp, *offset, &val, 1) <= 0)
#endif
        {
            return -ENXIO;
        }
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,13,16))
	(*offset)++; //Increase offset by i
#endif
        data[i++] = val;
        if (data[i - 1] == '\n')
        {
            break;
        }
    }

    data[i] = '\0';
    return i;
}

void extractDevName(char *name, char *src, int len)
{
    char *ptr;
    if (!name || !src || !len )
    {
        QC_LOG_ERR("%s:%d Invalid data\n", __func__, __LINE__);
        return;
    }

    ptr = src;

    while(*ptr != '\0' && len--)
    {
        if (*ptr == ' ' || *ptr == '(' || *ptr == ')')
        {
            *name++ = '_';
        } else {
            *name++ = *ptr;
        }
        ptr++;
    }
    *name = '\0';

    return;
}

static int _MyReadLine(struct file *filp, char *data, int size, loff_t *offset)
{
    int  last = 0;
    char *line = data;
    int len = 0;

    while ((len = MyReadLine(filp, line+last, size - last, offset)) > 0)
    {
        len = (int)strlen(line) - 1;
        /* Remove empty lines */
        if (len <= 0) {
            continue;
        }
        /* Safety check against buffer overflows */
        if (line[len] != '\n' && len > 0) {
            QC_LOG_ERR("error in parsing\n");
            return -1;
        }
        /* Get rid of \n, \r and spaces at end of line */
        while ((len >= 0 && line[len] == '\n') || isspace(line[len])) {
            line[len] = '\0';
            len--;
        }
        /* Line was entirely \n and/or spaces */
        if (len < 0) {
            len = 0;
            continue;
        }
        if (line[len]=='\\') {
            /* Multi-line value */
            last = len;
            continue ;
        }
        last = 0;
        break;
    }
    return len;
}


/* Strip whitespace chars off end of given string, in place. Return s. */
static char* rstrip(char* s)
{
    char* p = s + strlen(s);
    while (p > s && (isspace((unsigned char)(*--p)) || (*p == '%') || (*p == '\"')))
        *p = '\0';
    return s;
}

/* Return pointer to first non-whitespace char in given string. */
static char* lskip(const char* s)
{
    while (*s && (isspace((unsigned char)(*s)) || *s == '%' || *s == '\"'))
        s++;
    return (char*)s;
}

/* Return pointer to first char (of chars) or inline comment in given string,
 * or pointer to null at end of string if neither found. Inline comment must
 * be prefixed by a whitespace character to register as a comment. */
static char* find_chars_or_comment(const char* s, const char* chars)
{
    while (*s && (!chars || !strchr(chars, *s))) {
        s++;
    }
    return (char*)s;
}

/* Version of strncpy that ensures dest (size bytes) is null-terminated. */
static char* strncpy0(char* dest, const char* src, size_t size)
{
    strncpy(dest, src, size);
    dest[size - 1] = '\0';
    return dest;
}

static void updateVersion(fileInfo_t *pFileInfo, char *key, char *value)
{
    //QC_LOG_ERR("%s:%d) %s : %s\n", __func__, __LINE__, key, value);
    if (!pFileInfo || !key || !value)
    {
        return;
    }

    if ((strlen(QTIDEV_INF_CLASS_STR) == strlen(key)) &&
            !strncasecmp(key, QTIDEV_INF_CLASS_STR, strlen(QTIDEV_INF_CLASS_STR)))
    {
        if (!strncasecmp(value, QTIDEV_INF_CLASS_NET_STR, strlen(QTIDEV_INF_CLASS_NET_STR)))
        {
            pFileInfo->mClass = QTIDEV_INF_CLASS_NET;
        } else if(!strncasecmp(value, QTIDEV_INF_CLASS_MODEM_STR, strlen(QTIDEV_INF_CLASS_MODEM_STR)))
        {
            pFileInfo->mClass = QTIDEV_INF_CLASS_MODEM;
        } else if(!strncasecmp(value, QTIDEV_INF_CLASS_PORTS_STR, strlen(QTIDEV_INF_CLASS_PORTS_STR)))
        {
            pFileInfo->mClass = QTIDEV_INF_CLASS_PORTS;
        } else if(!strncasecmp(value, QTIDEV_INF_CLASS_USB_STR, strlen(QTIDEV_INF_CLASS_USB_STR)))
        {
            pFileInfo->mClass = QTIDEV_INF_CLASS_USB;
        } else
        {
            pFileInfo->mClass = QTIDEV_INF_CLASS_UNKNOWN;
        }
    }
    return;
}

static void updateManufacturer(manufacturerInfo_t **mfgInfo,
        char *value, int len)
{
    manufacturerInfo_t *newmfgInfo = NULL;
    char *data;
    char *tmp;
    char *model;

    if (!mfgInfo || *mfgInfo || !value || !len)
    {
        QC_LOG_ERR("Invalid data\n");
        return;
    }
    data = value;

    //TODO:
    /* manufacturer are separated by ',' or ' '(space) */
    while ((tmp = strsep(&data, ",")))
    {
        newmfgInfo = kzalloc(sizeof(manufacturerInfo_t), GFP_KERNEL);
        if (!newmfgInfo)
        {
            QC_LOG_ERR("error in allocating memory\n");
            return;
        }
        newmfgInfo->next = NULL;
        tmp = lskip(rstrip(tmp));
        if (*mfgInfo)
        {
            snprintf(newmfgInfo->name,
                    QTIDEV_INF_MAX_KEY_SIZE - 1, "%s.%s", model, tmp);
            newmfgInfo->next = *mfgInfo;
            *mfgInfo = newmfgInfo;

        } else {
            snprintf(newmfgInfo->name, QTIDEV_INF_MAX_KEY_SIZE - 1, "%s", tmp);
            model = newmfgInfo->name;
            *mfgInfo = newmfgInfo;

        }
    }
    return;
}

static void updateDeviceInfo(fileInfo_t *pFileInfo, char *key, char *value)
{
    char *data;
    char *tmp;
    int val = 0;
    int idx = 0;

    devInfo_t *newDevInfo;

    if (!pFileInfo || !key || !value)
    {
        QC_LOG_ERR("Invalid parameters\n");
        return;
    }

    if (pFileInfo->mNumResp > pFileInfo->mLength)
    {
        return;
    }

    //TODO: ADD address validation
    newDevInfo = pFileInfo->mDevInfo + pFileInfo->mNumResp;

    newDevInfo->mDevType = QTIDEV_INF_TYPE_UNKNOWN;

    tmp = data = value;
    /* To extact DPL in case of QDSS */
    tmp = strsep(&data, ",");
    if (data)
    {
        /* To extract port %QDSS90AB05%=QdssPort01, USB\VID_05C6...*/
        while (*tmp && !isdigit(*tmp) && tmp++);
        if (*tmp)
        {
            val = simple_strtoul(tmp, NULL, 16);
            //QC_LOG_ERR("%x, %s\n", val, tmp);
        }
        newDevInfo->mDevType = val ? QTIDEV_INF_TYPE_DPL : QTIDEV_INF_TYPE_BULK;
    } else
    {
        data = tmp;
    }

    val =
        sizeof(newDevInfo->mVid_pid_iface)/sizeof(newDevInfo->mVid_pid_iface[0]);

    /* :Extracting based on '_' QdssPort01, USB\VID_05C6&PID_90AB&MI_05 */
    while((tmp = strsep(&data, "_")) && data && (idx < val))
    {
        newDevInfo->mVid_pid_iface[idx] = simple_strtoul(data, NULL, 16);
        idx++;
    }

    strncpy0(newDevInfo->mpKey, key, QTIDEV_INF_MAX_KEY_SIZE);

    pFileInfo->mNumResp++;

    return;
}

static void updateDevStringInfo(fileInfo_t *pFileInfo, char *key, char *value)
{
    devInfo_t *info;
    unsigned int idx = 0;

    if (!pFileInfo || !key || !value)
    {
        return;
    }

    info = pFileInfo->mDevInfo;

    while (info && strncmp(info->mpKey, key, QTIDEV_INF_MAX_KEY_SIZE) &&
            (idx++ <= pFileInfo->mNumResp) && (info++));

    if (!info)
    {
        return;
    }

    extractDevName(info->mpKey, value, QTIDEV_INF_MAX_KEY_SIZE);
    /* In case of failure, mostly don't occur */
    if (!strlen(info->mpKey))
    {
        strncpy0(info->mpKey,
                QTIDEV_INF_DEFAULT_VENDOR, strlen(QTIDEV_INF_DEFAULT_VENDOR));
    }

    return;
}

static int processData(fileInfo_t *pFileInfo, struct file *filp, bool flag)
{
    char line[QTIDEV_INF_MAX_LINE_SIZE + 1] = "";

    char section[QTIDEV_INF_MAX_KEY_SIZE] = "";

    loff_t offset = 0;  /* For seeking */
    ssize_t ret;
    char *start;
    char *end;
    char *name;
    char *value;
    sectionInfo_t sectionInfo = QTIDEV_INF_UNKNOWN;
    bool Filevalid = false;
    unsigned int count = 0;


    manufacturerInfo_t *mfgInfo = NULL;
    manufacturerInfo_t *tmp;

    /* flag = 0 : To get device count
     * flag = 1 : To update the device info
     */
    if (!filp || !filp->f_inode || (flag && !pFileInfo))
    {
        QC_LOG_ERR("Invalid data\n");
        return -1;
    }

    while ((ret = _MyReadLine(filp,
                    line, QTIDEV_INF_MAX_LINE_SIZE, &offset)) >= 0) {
        start = line;

        start = lskip(rstrip(start));

        if (strchr(QTIDEV_INF_START_COMMENT_PREFIXES, *start)) {
            /* Start-of-line comment */
            continue;
        } else if (*start == QTIDEV_INF_START_SECTION[0]) {
            /* [Section] line */

            if ((flag == false) && (sectionInfo == QTIDEV_INF_DEVICE_INFO))
            {
                break;
            }
            sectionInfo = QTIDEV_INF_UNKNOWN;
            end = find_chars_or_comment(start + 1, QTIDEV_INF_END_SECTION);
            if (*end == QTIDEV_INF_END_SECTION[0]) {
                *end = '\0';
                strncpy0(section, start + 1, sizeof(section));
            } else {
                QC_LOG_ERR("Invalid Section\n");
                return -1;
            }
            if (!strncasecmp(section, QTIDEV_INF_MANUFACTURER_STR, strlen(QTIDEV_INF_MANUFACTURER_STR)))
            {
                sectionInfo = QTIDEV_INF_MANUFACTURER;
            } else if (!strncasecmp(section, QTIDEV_INF_VERSION_STR, strlen(QTIDEV_INF_VERSION_STR)))
            {
                sectionInfo = QTIDEV_INF_VERSION;
                Filevalid = true;
            } else if (!strncasecmp(section, QTIDEV_INF_STRING_STR, strlen(QTIDEV_INF_STRING_STR)))
            {
                sectionInfo = QTIDEV_INF_STRING;
            } else if (mfgInfo != NULL)
            {
                tmp = mfgInfo;
                while (tmp && memcmp(section, tmp->name,QTIDEV_INF_MAX_KEY_SIZE))
                {
                    tmp = tmp->next;
                }
		/* matched */
                if (tmp) {
		    sectionInfo = QTIDEV_INF_DEVICE_INFO;
                    /* clear manufacturer data */
		    while(mfgInfo) {
			tmp = mfgInfo;
			mfgInfo = mfgInfo->next;
			kfree(tmp);
                    }
                }
            }
        } else if (*start && (sectionInfo != QTIDEV_INF_UNKNOWN) && Filevalid) {
            /* Not a comment, must be a name[=:]value pair */
            end = find_chars_or_comment(start, QTIDEV_INF_DICT_OPERATOR);

            if (!strchr(QTIDEV_INF_DICT_OPERATOR, *end)) {
                continue;
            }

            *end = '\0';
            name = rstrip(start);
            value = end + 1;

            end = find_chars_or_comment(value, QTIDEV_INF_START_COMMENT_PREFIXES);
            if (*end)
                *end = '\0';

            value = lskip(value);
            rstrip(value);

            switch (sectionInfo)
            {
                case QTIDEV_INF_VERSION:
                    if (flag == true)
                    {
                        updateVersion(pFileInfo, name, value);
                    }
                    break;
                case QTIDEV_INF_MANUFACTURER:
                    updateManufacturer(&mfgInfo, value, strlen(value));
                    break;
                case QTIDEV_INF_DEVICE_INFO:
                    if (flag == true)
                    {
                        if (pFileInfo->mLength <= pFileInfo->mNumResp)
                        {
                            /* No need to process further */
                            continue;
                        }
                        updateDeviceInfo(pFileInfo, name, value);
                    } else
                    {
                        count++;
                    }
                    break;
                case QTIDEV_INF_STRING:
                    if (flag == true)
                    {
                        updateDevStringInfo(pFileInfo, name, value);
                    }
                    break;
                default :
                    break;
            }
        }
    }

    return count;
}

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
int QTIDevInfParse(void *pFilePath, fileInfo_t *pFileInfo)
{
    /* To parse the .inf file and store the info in global context */
    struct file *filp = NULL;
    if (!pFilePath || !pFileInfo)
    {
        QC_LOG_ERR("Invalid INF file\n");
        return -EINVAL;
    }
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(4,13,16))
        mm_segment_t oldfs;
        oldfs = get_fs();
        set_fs(get_ds());
    #endif

    filp = filp_open(pFilePath, O_RDONLY, 0444);
    if (IS_ERR(filp))
    {
        QC_LOG_ERR("Error in opening file : %d\n", (int)IS_ERR(filp));
        #if (LINUX_VERSION_CODE < KERNEL_VERSION(4,13,16))
            set_fs(oldfs);
        #endif
        return -EINVAL;
    }

    if (processData(pFileInfo, filp, true) < 0)
    {
        QC_LOG_ERR("failed to retrieve the device ID\n");
    }

    filp_close(filp, NULL);
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(4,13,16))
        set_fs(oldfs);
    #endif
    return 0;
}

EXPORT_SYMBOL(QTIDevInfParse);

#if 0
static int getInfEntryCount(struct file *filp)
{
    char    line[QTIDEV_INF_MAX_LINE_SIZE + 1] = "";
    char    *section = NULL;

    loff_t  offset = 0;  /* For seeking */
    char    *start;
    char    *end;
    unsigned int    count = 0;
    sectionInfo_t   sectionInfo = QTIDEV_INF_UNKNOWN;


    while (_MyReadLine(filp, line, QTIDEV_INF_MAX_LINE_SIZE, &offset) >= 0) {
        start = line;
        start = lskip(rstrip(start));

        if (strchr(QTIDEV_INF_START_COMMENT_PREFIXES, *start)) {
            /* Start-of-line comment */
            continue;
        } else if (*start == QTIDEV_INF_START_SECTION[0]) {
            /* [Section] line */
            if (sectionInfo == QTIDEV_INF_STRING)
            {
                break;
            }
            end = find_chars_or_comment(start + 1, QTIDEV_INF_END_SECTION);
            if (*end == QTIDEV_INF_END_SECTION[0]) {
                *end = '\0';
                section = start + 1;
            } else {
                QC_LOG_ERR("Invalid Section\n");
                return -1;
            }
            if (!strncasecmp(section, INF_STRING, strlen(INF_STRING)))
            {
                sectionInfo = QTIDEV_INF_STRING;
            }
        } else if (sectionInfo == QTIDEV_INF_STRING) {
            count++;
        }
    }
    return count;
}
#else
static int getInfEntryCount(struct file *filp)
{
    unsigned int count = 0;

    count = processData(NULL, filp, false);
    return count;
}

#endif

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
int QTIDevInfEntrySize(void *pFilePath)
{
    /* To parse the .inf file and store the info in global context */
    struct file *filp = NULL;
    int ret;
    
    if (!pFilePath)
    {
        QC_LOG_ERR("Invalid INF file\n");
        return -EINVAL;
    }
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(4,13,16))
        mm_segment_t oldfs;
        oldfs = get_fs();
        set_fs(get_ds());
    #endif

    filp = filp_open(pFilePath, O_RDONLY, 0444);
    if (IS_ERR(filp))
    {
        QC_LOG_ERR("Error in opening file : %d\n", (int)IS_ERR(filp));
        #if (LINUX_VERSION_CODE < KERNEL_VERSION(4,13,16))
            set_fs(oldfs);
        #endif
        return -EINVAL;
    }

    if ((ret = getInfEntryCount(filp)) < 0)
    {
        QC_LOG_ERR("failed to retrieve the device ID\n");
    }

    filp_close(filp, NULL);
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(4,13,16))
        set_fs(oldfs);
    #endif
    return ret;
}

EXPORT_SYMBOL(QTIDevInfEntrySize);

/*===========================================================================
METHOD:
QdssUSBModInit (Public Method)

DESCRIPTION:
Initialize module

RETURN VALUE:
int - 0 for success
Negative errno for error
===========================================================================*/
static int QdssUSBModInit(void)
{

    // This will be shown whenever driver is loaded
    QC_LOG_INFO( "%s: %s\n", DRIVER_DESC, DRIVER_VERSION );

    return 0;
}

/*===========================================================================
METHOD:
QdssUSBModExit (Public Method)

DESCRIPTION:
Deregister module

RETURN VALUE:
void
===========================================================================*/
static void __exit QdssUSBModExit(void)
{
    QC_LOG_INFO( "Unload %s: %s\n", DRIVER_DESC, DRIVER_VERSION );
    return;
}

module_init(QdssUSBModInit);
module_exit(QdssUSBModExit);

module_param( debug_g, int, S_IRUGO | S_IWUSR );

MODULE_VERSION( DRIVER_VERSION );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("Dual BSD/GPL");
