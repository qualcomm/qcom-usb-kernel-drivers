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
#include "qtiEvt.h"

sQtiEvent *qti_create_event(void)
{
    sQtiEvent *evt;

    if (NULL != (evt = (sQtiEvent *)qti_kmalloc(sizeof(sQtiEvent), GFP_KERNEL)))
    {
        spin_lock_init(&evt->evt_lock);
        init_waitqueue_head(&evt->wq);
        evt->events = 0;
        evt->mask = 0;
    }
    return evt;
}

void qti_free_event(sQtiEvent *evt)
{
    qti_kfree(evt);
}

void qti_initialize_event(sQtiEvent *evt)
{
    spin_lock_init(&evt->evt_lock);
    init_waitqueue_head(&evt->wq);
    evt->events = 0;
    evt->mask = 0;
}

bool qti_register_event(sQtiEvent *evt, int event)
{
    if ((event <= 0) || (event > 64))
    {
        return false;
    }
    spin_lock(&evt->evt_lock);
    evt->mask |= (unsigned long long)0x1 << (event - 1);
    spin_unlock(&evt->evt_lock);
    return true;
}

bool qti_deregister_event(sQtiEvent *evt, int event)
{
    if ((event <= 0) || (event > 64))
    {
        return false;
    }
    spin_lock(&evt->evt_lock);
    evt->mask &= ~((unsigned long long)0x1 << (event - 1));
    spin_unlock(&evt->evt_lock);
    return true;
}

void qti_set_event(sQtiEvent *evt, int event)
{
    unsigned long long internal_evt;

    if ((event <= 0) || (event > 64))
    {
        return;
    }
    internal_evt = (unsigned long long)0x1 << (event - 1);

    // if registered
    if (evt->mask & internal_evt)
    {
        spin_lock(&evt->evt_lock);
        evt->events |= internal_evt;
        spin_unlock(&evt->evt_lock);
        wake_up_interruptible(&evt->wq);
    }
}

int qti_wait_event(sQtiEvent *evt, unsigned long timeout_ms)
{
    int i, ret;

    ret = wait_event_interruptible_timeout(evt->wq, evt->events & evt->mask, msecs_to_jiffies(timeout_ms));
    if (ret > 0)
    {
        for (i = 0; i < 64; i++)
        {
           if ((evt->events >> i) & 0x01)
           {
              return (i+1);
           }
        }
    }
    return ret; // interrupted or timed out
}

void qti_clear_event(sQtiEvent *evt, unsigned long long event)
{
    if ((event <= 0) || (event > 64))
    {
        return;
    }
    spin_lock(&evt->evt_lock);
    evt->events &= ~((unsigned long long)0x1 << (event - 1));
    spin_unlock(&evt->evt_lock);
}

