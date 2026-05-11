/*
 * AirportVirtualIOCTL.cpp – Virtual interface IOCTL handlers
 */

#include "Airportrtlwm.hpp"
#include <Airport/apple80211_ioctl.h>
#include <sys/errno.h>

SInt32 rtlwmHandleVirtualIoctl(Airportrtlwm *controller, UInt requestType, int request,
                               IO80211VirtualInterface *interface, void *data)
{
    (void)controller;
    (void)interface;
    (void)data;

    if (requestType != SIOCGA80211 && requestType != SIOCSA80211)
        return EINVAL;

    switch (request) {
    case APPLE80211_IOC_VIRTUAL_IF_CREATE:
    case APPLE80211_IOC_VIRTUAL_IF_DELETE:
    case APPLE80211_IOC_VIRTUAL_IF_ROLE:
    case APPLE80211_IOC_VIRTUAL_IF_PARENT:
        return kIOReturnUnsupported;
    default:
        return EOPNOTSUPP;
    }
}
