/*
 * AirportVirtualIOCTL.cpp – Virtual interface IOCTL handlers
 *
 * Most GET/SET requests are delegated to the STA handler so that the
 * virtual interface always reflects the same driver state.  Only
 * virtual-interface–specific operations (CREATE/DELETE/ROLE/PARENT)
 * are handled here, and they are currently unsupported.
 */

#include "Airportrtlwm.hpp"
#include <Airport/apple80211_ioctl.h>
#include <sys/errno.h>

/* Forward declaration – implemented in AirportSTAIOCTL.cpp */
SInt32 rtlwmHandleStaIoctl(Airportrtlwm *controller, UInt requestType, int request,
                           IO80211Interface *interface, void *data);

SInt32 rtlwmHandleVirtualIoctl(Airportrtlwm *controller, UInt requestType, int request,
                               IO80211VirtualInterface *interface, void *data)
{
    (void)interface;

    if (!data)
        return EINVAL;
    if (requestType != SIOCGA80211 && requestType != SIOCSA80211)
        return EINVAL;

    if (requestType == SIOCGA80211) {
        switch (request) {
        /* Virtual-interface–specific GET operations (not supported) */
        case APPLE80211_IOC_VIRTUAL_IF_CREATE:
        case APPLE80211_IOC_VIRTUAL_IF_DELETE:
        case APPLE80211_IOC_VIRTUAL_IF_ROLE:
        case APPLE80211_IOC_VIRTUAL_IF_PARENT:
            return EOPNOTSUPP;

        /* All other GET requests are forwarded to the STA handler */
        default:
            return rtlwmHandleStaIoctl(controller, requestType, request,
                                       nullptr, data);
        }
    }

    /* SET requests */
    switch (request) {
    /* Virtual-interface–specific SET operations (not supported) */
    case APPLE80211_IOC_VIRTUAL_IF_CREATE:
    case APPLE80211_IOC_VIRTUAL_IF_DELETE:
    case APPLE80211_IOC_VIRTUAL_IF_ROLE:
    case APPLE80211_IOC_VIRTUAL_IF_PARENT:
        return EOPNOTSUPP;

    /* All other SET requests are forwarded to the STA handler */
    default:
        return rtlwmHandleStaIoctl(controller, requestType, request,
                                   nullptr, data);
    }
}
