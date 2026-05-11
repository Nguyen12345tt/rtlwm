/*
 * AirportRtlwmSkywalkInterface.hpp – IO80211SkywalkInterface for Monterey+
 *
 * Monterey introduced IO80211FAMILY_V2 and Skywalk networking.
 * This file provides the Skywalk-based interface used when the kext is
 * built with -DIO80211FAMILY_V2.
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 */

#ifndef AirportRtlwmSkywalkInterface_hpp
#define AirportRtlwmSkywalkInterface_hpp

#define IO80211FAMILY_V2 1
#define __IO80211_TARGET __MAC_12_0

#include <Airport/Apple80211.h>
#include <Airport/IO80211SkywalkInterface.h>

class AirportRtlwmSkywalkInterface : public IO80211SkywalkInterface {
    OSDeclareDefaultStructors(AirportRtlwmSkywalkInterface)

public:
    bool init(IONetworkController *controller) override;

    /* Skywalk packet path */
    IOReturn  outputPacket(mbuf_t packet, void *param) override;
    UInt32    inputPacket(mbuf_t packet)               override;

    /* Channel / scan helpers */
    IOReturn  setLinkState(IO80211LinkState state, UInt32 reason);
};

#endif /* AirportRtlwmSkywalkInterface_hpp */
