/*
 * AirportRtlwmEthernetInterface.cpp
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 */

#include "AirportRtlwmEthernetInterface.hpp"

OSDefineMetaClassAndStructors(AirportRtlwmEthernetInterface, IOEthernetInterface)

bool AirportRtlwmEthernetInterface::init(IONetworkController *controller)
{
    return IOEthernetInterface::init(controller);
}

IOReturn AirportRtlwmEthernetInterface::setProperties(OSObject *properties)
{
    if (!properties)
        return kIOReturnBadArgument;
    return IOEthernetInterface::setProperties(properties);
}
