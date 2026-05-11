/*
 * AirportRtlwmEthernetInterface.hpp – IOEthernetInterface subclass for pre-Skywalk
 *
 * Used on macOS < Monterey (pre-IO80211FAMILY_V2).
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 */

#ifndef AirportRtlwmEthernetInterface_hpp
#define AirportRtlwmEthernetInterface_hpp

#include <IOKit/network/IOEthernetInterface.h>

class AirportRtlwmEthernetInterface : public IOEthernetInterface {
    OSDeclareDefaultStructors(AirportRtlwmEthernetInterface)

public:
    bool init(IONetworkController *controller) override;

    /*
     * Override setProperties so the Airport family sees the interface as
     * a Wi-Fi interface rather than a plain Ethernet controller.
     */
    IOReturn setProperties(OSObject *properties) override;
};

#endif /* AirportRtlwmEthernetInterface_hpp */
