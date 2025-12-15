/*
    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause
*/

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

