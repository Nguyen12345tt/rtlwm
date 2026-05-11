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
    /* TODO: enqueue to HAL TX ring */
    mbuf_freem(packet);
    return kIOReturnOutputDropped;
}

UInt32 AirportRtlwmSkywalkInterface::inputPacket(mbuf_t packet)
{
    /* TODO: deliver to Skywalk layer */
    return 0;
}

IOReturn AirportRtlwmSkywalkInterface::setLinkState(IO80211LinkState state,
                                                      UInt32 reason)
{
    /* TODO: propagate link state changes to the 802.11 stack */
    return kIOReturnSuccess;
}
