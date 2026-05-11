/*
 * AirportSTAIOCTL.cpp – Station-mode IOCTL dispatch table
 *
 * Handles APPLE80211_IOC_* get/set requests forwarded from
 * Airportrtlwm::apple80211Request().
 *
 * Each handler reads or writes state via the RtlHalService / ieee80211com
 * 802.11 stack, then encodes the result into the apple80211req data buffer.
 *
 * Adapted from OpenIntelWireless/itlwm – AirportSTAIOCTL.cpp (GPLv2)
 *
 * TODO: implement each handler body.  The function signatures mirror those
 *       in AirportItlwm/AirportSTAIOCTL.cpp; consult that file for guidance.
 */

#include "Airportrtlwm.hpp"
#include <Airport/apple80211_ioctl.h>
#include <Airport/apple80211_var.h>

/* ---- Helpers ------------------------------------------------------------- */
#define RTL_IOCTL_GET(name) \
    static SInt32 get_##name(Airportrtlwm *c, IO80211Interface *i, \
                             IO80211VirtualInterface *v, apple80211req *req, bool b)

#define RTL_IOCTL_SET(name) \
    static SInt32 set_##name(Airportrtlwm *c, IO80211Interface *i, \
                             IO80211VirtualInterface *v, apple80211req *req, bool b)

/* ---- GET handlers -------------------------------------------------------- */

RTL_IOCTL_GET(SSID)         { return EOPNOTSUPP; /* TODO */ }
RTL_IOCTL_GET(AUTH_TYPE)    { return EOPNOTSUPP; }
RTL_IOCTL_GET(CHANNEL)      { return EOPNOTSUPP; }
RTL_IOCTL_GET(BSSID)        { return EOPNOTSUPP; }
RTL_IOCTL_GET(RSSI)         { return EOPNOTSUPP; }
RTL_IOCTL_GET(NOISE)        { return EOPNOTSUPP; }
RTL_IOCTL_GET(RATE)         { return EOPNOTSUPP; }
RTL_IOCTL_GET(STATE)        { return EOPNOTSUPP; }
RTL_IOCTL_GET(PHY_MODE)     { return EOPNOTSUPP; }
RTL_IOCTL_GET(OP_MODE)      { return EOPNOTSUPP; }
RTL_IOCTL_GET(POWER)        { return EOPNOTSUPP; }
RTL_IOCTL_GET(SCAN_RESULT)  { return EOPNOTSUPP; }
RTL_IOCTL_GET(CARD_CAPABILITIES) { return EOPNOTSUPP; }
RTL_IOCTL_GET(SUPPORTED_CHANNELS) { return EOPNOTSUPP; }
RTL_IOCTL_GET(LOCALE)       { return EOPNOTSUPP; }
RTL_IOCTL_GET(TX_NSS)       { return EOPNOTSUPP; }
RTL_IOCTL_GET(DRIVER_VERSION) { return EOPNOTSUPP; }
RTL_IOCTL_GET(HARDWARE_VERSION) { return EOPNOTSUPP; }
RTL_IOCTL_GET(COUNTRY_CODE) { return EOPNOTSUPP; }
RTL_IOCTL_GET(MCS)          { return EOPNOTSUPP; }
RTL_IOCTL_GET(RSN_IE)       { return EOPNOTSUPP; }
RTL_IOCTL_GET(ASSOCIATE_RESULT) { return EOPNOTSUPP; }

/* ---- SET handlers -------------------------------------------------------- */

RTL_IOCTL_SET(SSID)         { return EOPNOTSUPP; /* TODO */ }
RTL_IOCTL_SET(AUTH_TYPE)    { return EOPNOTSUPP; }
RTL_IOCTL_SET(CIPHER_KEY)   { return EOPNOTSUPP; }
RTL_IOCTL_SET(CHANNEL)      { return EOPNOTSUPP; }
RTL_IOCTL_SET(POWERSAVE)    { return EOPNOTSUPP; }
RTL_IOCTL_SET(POWER)        { return EOPNOTSUPP; }
RTL_IOCTL_SET(SCAN_REQ)     { return EOPNOTSUPP; }
RTL_IOCTL_SET(ASSOCIATE)    { return EOPNOTSUPP; }
RTL_IOCTL_SET(DISASSOCIATE) { return EOPNOTSUPP; }
RTL_IOCTL_SET(DEAUTH)       { return EOPNOTSUPP; }
RTL_IOCTL_SET(RSN_IE)       { return EOPNOTSUPP; }
RTL_IOCTL_SET(COUNTRY_CODE) { return EOPNOTSUPP; }
