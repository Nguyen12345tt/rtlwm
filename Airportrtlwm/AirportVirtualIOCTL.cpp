/*
 * AirportVirtualIOCTL.cpp – Virtual interface IOCTL handlers
 *
 * Handles IOCTL requests for virtual interfaces (P2P, AWDL).
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * TODO: implement if P2P / AWDL support is desired.
 */

#include "Airportrtlwm.hpp"

SInt32 Airportrtlwm::apple80211VirtualRequest(UInt request, int type,
                                               IO80211VirtualInterface *iface,
                                               void *data)
{
    /* TODO: dispatch virtual interface IOCTL requests */
    return EOPNOTSUPP;
}
