/*===========================================================================
FILE:
   qtiEvt.h

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
//---------------------------------------------------------------------------
// Include Files
//---------------------------------------------------------------------------
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
