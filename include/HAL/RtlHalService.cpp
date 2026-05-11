/*
 * RtlHalService.cpp – Abstract HAL service base-class implementation
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "RtlHalService.hpp"

OSDefineMetaClassAndAbstractStructors(RtlHalService, OSObject)

bool RtlHalService::initWithController(IOEthernetController *aController,
                                       IOWorkLoop           *workloop,
                                       IOCommandGate        *commandGate)
{
    if (!OSObject::init())
        return false;

    controller       = aController;
    mainCommandGate  = commandGate;
    mainWorkLoop     = workloop;

    inner_gp_attr = lck_grp_attr_alloc_init();
    inner_gp      = lck_grp_alloc_init("RtlHalService", inner_gp_attr);
    inner_attr    = lck_attr_alloc_init();
    inner_lock    = lck_mtx_alloc_init(inner_gp, inner_attr);

    return true;
}

void RtlHalService::free()
{
    if (inner_lock) {
        lck_mtx_free(inner_lock, inner_gp);
        inner_lock = nullptr;
    }
    if (inner_attr) {
        lck_attr_free(inner_attr);
        inner_attr = nullptr;
    }
    if (inner_gp) {
        lck_grp_free(inner_gp);
        inner_gp = nullptr;
    }
    if (inner_gp_attr) {
        lck_grp_attr_free(inner_gp_attr);
        inner_gp_attr = nullptr;
    }
    OSObject::free();
}

int RtlHalService::tsleep_nsec(void *ident, int priority,
                                const char *wmesg, int timo)
{
    lck_mtx_lock(inner_lock);
    int ret = msleep(ident, inner_lock, priority, wmesg, timo == 0
                     ? NULL
                     : &(struct timespec){ .tv_sec  = 0,
                                          .tv_nsec  = timo });
    lck_mtx_unlock(inner_lock);
    return ret;
}

void RtlHalService::wakeupOn(void *ident)
{
    wakeup(ident);
}

IOEthernetController *RtlHalService::getController()
{
    return controller;
}

IOCommandGate *RtlHalService::getMainCommandGate()
{
    return mainCommandGate;
}

IOWorkLoop *RtlHalService::getMainWorkLoop()
{
    return mainWorkLoop;
}
