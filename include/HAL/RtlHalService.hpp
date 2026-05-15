/*
 * RtlHalService.hpp – Abstract HAL service base class
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef RtlHalService_hpp
#define RtlHalService_hpp

#include <libkern/c++/OSObject.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <sys/kpi_mbuf.h>

#include "RtlDriverInfo.hpp"
#include "RtlDriverController.hpp"

#include <net80211/ieee80211_var.h>

class RtlHalService : public OSObject {
    OSDeclareAbstractStructors(RtlHalService)

public:
    virtual bool      attach(IOPCIDevice *device)               = 0;
    virtual void      detach(IOPCIDevice *device)               = 0;
    virtual IOReturn  enable(IONetworkInterface *interface)     = 0;
    virtual IOReturn  disable(IONetworkInterface *interface)    = 0;
    virtual bool      enqueueTxPacket(mbuf_t packet)            = 0;
    virtual void      handleInterrupt();

    virtual struct ieee80211com  *get80211Controller()  = 0;
    virtual RtlDriverInfo        *getDriverInfo()       = 0;
    virtual RtlDriverController  *getDriverController() = 0;

    virtual void free() override;

public:
    virtual bool initWithController(IOEthernetController *controller,
                                    IOWorkLoop           *workloop,
                                    IOCommandGate        *commandGate);

protected:
    int  tsleep_nsec(void *ident, int priority, const char *wmesg, int timo);
    void wakeupOn(void *ident);

    IOEthernetController *getController();
    IOCommandGate        *getMainCommandGate();
    IOWorkLoop           *getMainWorkLoop();

private:
    IOEthernetController *controller;
    IOCommandGate        *mainCommandGate;
    IOWorkLoop           *mainWorkLoop;

    lck_grp_t      *inner_gp;
    lck_grp_attr_t *inner_gp_attr;
    lck_attr_t     *inner_attr;
    lck_mtx_t      *inner_lock;
};

#endif /* RtlHalService_hpp */
