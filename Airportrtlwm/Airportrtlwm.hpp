/*
 * AirportRtlwm.hpp – Native Airport (IO80211) interface for Realtek WiFi kext
 *
 * This is the Airport-visible kext: macOS treats it as a first-class Wi-Fi
 * adapter indistinguishable from Apple's built-in AirPort hardware.
 *
 * Architecture (mirrors AirportItlwm from OpenIntelWireless/itlwm):
 *
 *   AirportRtlwm
 *     │   inherits IO80211Controller (pre-Monterey)
 *     │         or IO80211ControllerV2 (Monterey+, IO80211FAMILY_V2)
 *     │
 *     ├── AirportRtlwmInterface      – IO80211Interface wrapper
 *     ├── AirportRtlwmEthernetInterface – IOEthernetInterface for pre-Skywalk
 *     ├── AirportRtlwmSkywalkInterface  – IOSkywalk interface for Monterey+
 *     │
 *     └── RtlHalService (via rtlwm kext dependency)
 *           ├── RtlHal_rtw88
 *           └── RtlHal_rtw89
 *
 * Adapted from OpenIntelWireless/itlwm – AirportItlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AirportRtlwm_hpp
#define AirportRtlwm_hpp

#define __IO80211_TARGET __MAC_12_0   /* Monterey baseline; override per build */

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOCommandGate.h>

#include <Airport/Apple80211.h>
#include <HAL/RtlHalService.hpp>
#include <HAL/RtlDriverInfo.hpp>

/* ---- Pick the right IO80211 base class ----------------------------------- */
#ifdef IO80211FAMILY_V2
#   include <Airport/IO80211ControllerV2.h>
    typedef IO80211ControllerV2 IO80211ControllerBase;
#else
#   include <Airport/IO80211Controller.h>
    typedef IO80211Controller   IO80211ControllerBase;
#endif

#include "AirportRtlwmInterface.hpp"

class AirportRtlwm : public IO80211ControllerBase {
    OSDeclareDefaultStructors(AirportRtlwm)

public:
    /* IOService lifecycle */
    bool     init(OSDictionary *dict = nullptr)  override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool     start(IOService *provider)          override;
    void     stop(IOService *provider)           override;
    void     free()                              override;
    bool     createWorkLoop()                    override;
    IOWorkLoop *getWorkLoop() const              override;

    /* IO80211Controller mandatory overrides */
    SInt32  apple80211Request(UInt, int, IO80211Interface *, void *) override;
    SInt32  apple80211VirtualRequest(UInt, int, IO80211VirtualInterface *, void *) override;
    SInt32  stopDMA()                                               override;
    UInt32  hardwareOutputQueueDepth(IO80211Interface *)            override;
    SInt32  performCountryCodeOperation(IO80211Interface *,
                                        IO80211CountryCodeOp)       override;
    SInt32  enableFeature(IO80211FeatureCode, void *)               override;

    /* IOEthernetController */
    IOReturn enable(IONetworkInterface *)   override;
    IOReturn disable(IONetworkInterface *)  override;
    IOReturn getHardwareAddress(IOEthernetAddress *) override;
    bool     configureInterface(IONetworkInterface *) override;
    UInt32   outputPacket(mbuf_t, void *)  override;

    /* Power management */
    IOReturn setPowerState(unsigned long ordinal, IOService *) override;

    /* IOCTL helpers */
    RtlDriverInfo       *getDriverInfo() const;
    RtlDriverController *getDriverController() const;
    bool                 isInterfaceEnabled() const;

private:
    RtlHalService    *halService     {nullptr};
    IOWorkLoop       *workLoop       {nullptr};
    IOCommandGate    *commandGate    {nullptr};
    IOPCIDevice      *pciDevice      {nullptr};

    AirportRtlwmInterface     *infraInterface    {nullptr};
    bool                       ifEnabled         {false};
};

#endif /* AirportRtlwm_hpp */
