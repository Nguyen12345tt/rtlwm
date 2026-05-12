/*
 * AirportRtlwmSkywalkInterface.cpp – Skywalk interface (Monterey+)
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 */

#include "AirportRtlwmSkywalkInterface.hpp"

OSDefineMetaClassAndStructors(AirportRtlwmSkywalkInterface,
                              IO80211SkywalkInterface)

bool AirportRtlwmSkywalkInterface::init(IONetworkController *controller)
{
    return IO80211SkywalkInterface::init(controller);
}

IOReturn AirportRtlwmSkywalkInterface::outputPacket(mbuf_t packet, void *param)
{
    return IO80211SkywalkInterface::outputPacket(packet, param);
}

UInt32 AirportRtlwmSkywalkInterface::inputPacket(mbuf_t packet)
{
    return IO80211SkywalkInterface::inputPacket(packet);
}

IOReturn AirportRtlwmSkywalkInterface::setLinkState(IO80211LinkState state,
                                                      UInt32 reason)
{
    return IO80211SkywalkInterface::setLinkState(state, reason)
           ? kIOReturnSuccess
           : kIOReturnError;
}
