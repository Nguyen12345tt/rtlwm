/*
 * RtlDriverInfo.hpp – Abstract driver information interface
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef RtlDriverInfo_hpp
#define RtlDriverInfo_hpp

#include <IOKit/IOTypes.h>

class RtlDriverInfo {
public:
    virtual const char *getFirmwareVersion()     = 0;
    virtual int16_t     getBSSNoise()            = 0;
    virtual bool        is5GBandSupport()        = 0;
    virtual int         getTxNSS()               = 0;
    virtual const char *getFirmwareName()        = 0;
    virtual UInt32      supportedFeatures()      = 0;
    virtual const char *getFirmwareCountryCode() = 0;
    virtual uint32_t    getTxQueueSize()         = 0;
    virtual const uint8_t *getMacAddress()       = 0;
    virtual bool        setMacAddress(const uint8_t *addr) = 0;
};

#endif /* RtlDriverInfo_hpp */
