/*
 * AirportRtlwmInterface.hpp – IO80211Interface wrapper for Airportrtlwm
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 */

#ifndef AirportRtlwmInterface_hpp
#define AirportRtlwmInterface_hpp

#include <Airport/Apple80211.h>

class AirportRtlwmInterface : public IO80211Interface {
    OSDeclareDefaultStructors(AirportRtlwmInterface)

public:
    bool     init(IONetworkController *controller) override;
    UInt32   inputPacket(mbuf_t packet, UInt32 length = 0,
                         IOOptionBits options = 0, void *param = nullptr) override;
};

#endif /* AirportRtlwmInterface_hpp */
