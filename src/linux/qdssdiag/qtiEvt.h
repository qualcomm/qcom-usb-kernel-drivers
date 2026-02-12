/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef QTIEVT_H
#define QTIEVT_H

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>

#include <linux/mm.h>
#include <linux/wait.h>


#ifdef QTI_USE_VM
#define qti_kmalloc(size, flags) kvzalloc(size, flags)
#define qti_kfree(mem_ptr) kvfree(mem_ptr)
#else
#define qti_kmalloc(size, flags) kzalloc(size, flags)
#define qti_kfree(mem_ptr) kfree(mem_ptr)
#endif

typedef struct _qti_event
{
    spinlock_t evt_lock;
    wait_queue_head_t wq;
    unsigned long long events;
    unsigned long long mask;
} sQtiEvent;

sQtiEvent *qti_create_event(void);
void qti_free_event(sQtiEvent *evt);
void qti_initialize_event(sQtiEvent *evt);
bool qti_register_event(sQtiEvent *evt, int event);
bool qti_deregister_event(sQtiEvent *evt, int event);
void qti_set_event(sQtiEvent *evt, int event);
int qti_wait_event(sQtiEvent *evt, unsigned long timeout_ms);
void qti_clear_event(sQtiEvent *evt, unsigned long long event);

#endif
