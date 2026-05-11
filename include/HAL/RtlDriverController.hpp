/*
 * RtlDriverController.hpp – Abstract driver control interface
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef RtlDriverController_hpp
#define RtlDriverController_hpp

#include <IOKit/network/IOEthernetController.h>

class RtlDriverController {
public:
    virtual void      clearScanningFlags()                               = 0;
    virtual IOReturn  setMulticastList(IOEthernetAddress *addr, int cnt) = 0;
};

#endif /* RtlDriverController_hpp */
