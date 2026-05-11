/*
 * AirportRtlwmInterface.cpp – IO80211Interface wrapper
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 */

#include "AirportRtlwmInterface.hpp"

OSDefineMetaClassAndStructors(AirportRtlwmInterface, IO80211Interface)

bool AirportRtlwmInterface::init(IONetworkController *controller)
{
    return IO80211Interface::init(controller);
}

UInt32 AirportRtlwmInterface::inputPacket(mbuf_t packet, UInt32 length,
                                           IOOptionBits options, void *param)
{
    return IO80211Interface::inputPacket(packet, length, options, param);
}
