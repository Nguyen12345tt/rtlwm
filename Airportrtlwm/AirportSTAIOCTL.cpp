/*
 * AirportSTAIOCTL.cpp – Station-mode IOCTL dispatch table
 */

#include "Airportrtlwm.hpp"
#include <Airport/apple80211_ioctl.h>
#include <Airport/apple80211_var.h>
#include <sys/errno.h>
#include <string.h>

static bool rtlwmIsGetRequest(UInt requestType)
{
    return requestType == SIOCGA80211;
}

static bool rtlwmIsSetRequest(UInt requestType)
{
    return requestType == SIOCSA80211;
}

static void rtlwmSetCapabilityBit(struct apple80211_capability_data *cap, uint32_t bit)
{
    const uint32_t byteIndex = bit / 8;
    const uint32_t bitMask = 1u << (bit % 8);
    if (byteIndex < sizeof(cap->capabilities))
        cap->capabilities[byteIndex] |= static_cast<uint8_t>(bitMask);
}

static SInt32 rtlwmGetSsid(apple80211_ssid_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetAuthType(apple80211_authtype_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->authtype_lower = APPLE80211_AUTHTYPE_OPEN;
    out->authtype_upper = APPLE80211_AUTHTYPE_NONE;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetChannel(apple80211_channel_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->channel.version = APPLE80211_VERSION;
    out->channel.channel = 1;
    out->channel.flags = APPLE80211_C_FLAG_2GHZ | APPLE80211_C_FLAG_ACTIVE | APPLE80211_C_FLAG_20MHZ;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetBssid(apple80211_bssid_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetRssi(apple80211_rssi_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->num_radios = 1;
    out->rssi_unit = APPLE80211_UNIT_DBM;
    out->rssi[0] = -95;
    out->aggregate_rssi = out->rssi[0];
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetNoise(Airportrtlwm *controller, apple80211_noise_data *out)
{
    if (!controller || !out)
        return EINVAL;

    int32_t noise = -95;
    if (RtlDriverInfo *info = controller->getDriverInfo())
        noise = info->getBSSNoise();

    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->num_radios = 1;
    out->noise_unit = APPLE80211_UNIT_DBM;
    out->noise[0] = noise;
    out->aggregate_noise = noise;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetRate(apple80211_rate_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->num_radios = 1;
    out->rate[0] = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetState(Airportrtlwm *controller, apple80211_state_data *out)
{
    if (!controller || !out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->state = controller->isInterfaceEnabled() ? APPLE80211_S_SCAN : APPLE80211_S_INIT;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetPhyMode(apple80211_phymode_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->phy_mode = APPLE80211_MODE_11B | APPLE80211_MODE_11G | APPLE80211_MODE_11N;
    out->active_phy_mode = APPLE80211_MODE_AUTO;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetOpMode(apple80211_opmode_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->op_mode = APPLE80211_M_STA;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetPower(Airportrtlwm *controller, apple80211_power_data *out)
{
    if (!controller || !out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->num_radios = 1;
    out->power_state[0] = controller->isInterfaceEnabled() ? APPLE80211_POWER_ON : APPLE80211_POWER_OFF;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetCapabilities(apple80211_capability_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    rtlwmSetCapabilityBit(out, APPLE80211_CAP_IBSS);
    rtlwmSetCapabilityBit(out, APPLE80211_CAP_PMGT);
    rtlwmSetCapabilityBit(out, APPLE80211_CAP_TXPMGT);
    rtlwmSetCapabilityBit(out, APPLE80211_CAP_SHSLOT);
    rtlwmSetCapabilityBit(out, APPLE80211_CAP_WPA2);
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetSupportedChannels(Airportrtlwm *controller, apple80211_sup_channel_data *out)
{
    if (!controller || !out)
        return EINVAL;

    const bool has5g = controller->getDriverInfo() ? controller->getDriverInfo()->is5GBandSupport() : false;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;

    uint32_t idx = 0;
    for (uint32_t ch = 1; ch <= 11 && idx < APPLE80211_MAX_CHANNELS; ch++) {
        out->supported_channels[idx].version = APPLE80211_VERSION;
        out->supported_channels[idx].channel = ch;
        out->supported_channels[idx].flags = APPLE80211_C_FLAG_2GHZ | APPLE80211_C_FLAG_ACTIVE | APPLE80211_C_FLAG_20MHZ;
        idx++;
    }

    if (has5g) {
        const uint32_t basic5g[] = {36, 40, 44, 48, 149, 153, 157, 161};
        for (uint32_t i = 0; i < sizeof(basic5g) / sizeof(basic5g[0]) && idx < APPLE80211_MAX_CHANNELS; i++) {
            out->supported_channels[idx].version = APPLE80211_VERSION;
            out->supported_channels[idx].channel = basic5g[i];
            out->supported_channels[idx].flags = APPLE80211_C_FLAG_5GHZ | APPLE80211_C_FLAG_ACTIVE | APPLE80211_C_FLAG_20MHZ;
            idx++;
        }
    }

    out->num_channels = idx;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetLocale(apple80211_locale_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->locale = APPLE80211_LOCALE_FCC;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetTxNss(Airportrtlwm *controller, apple80211_tx_nss_data *out)
{
    if (!controller || !out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->nss = controller->getDriverInfo() ? static_cast<uint8_t>(controller->getDriverInfo()->getTxNSS()) : 1;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetVersion(apple80211_version_data *out, const char *version)
{
    if (!out || !version)
        return EINVAL;

    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->string_len = static_cast<uint16_t>(strnlen(version, APPLE80211_MAX_VERSION_LEN - 1));
    memcpy(out->string, version, out->string_len);
    out->string[out->string_len] = '\0';
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetCountryCode(Airportrtlwm *controller, apple80211_country_code_data *out)
{
    if (!controller || !out)
        return EINVAL;

    const char *country = "US";
    if (RtlDriverInfo *info = controller->getDriverInfo()) {
        const char *cc = info->getFirmwareCountryCode();
        if (cc && cc[0])
            country = cc;
    }

    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    const size_t ccLen = strnlen(country, APPLE80211_MAX_CC_LEN);
    memcpy(out->cc, country, ccLen);
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetMcs(apple80211_mcs_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->index = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetAssociateResult(apple80211_assoc_result_data *out)
{
    if (!out)
        return EINVAL;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->result = APPLE80211_RESULT_UNAVAILABLE;
    return kIOReturnSuccess;
}

SInt32 rtlwmHandleStaIoctl(Airportrtlwm *controller, UInt requestType, int request,
                           IO80211Interface *interface, void *data)
{
    (void)interface;
    if (!controller || !data)
        return EINVAL;
    if (!rtlwmIsGetRequest(requestType) && !rtlwmIsSetRequest(requestType))
        return EINVAL;

    if (rtlwmIsGetRequest(requestType)) {
        switch (request) {
        case APPLE80211_IOC_SSID:
            return rtlwmGetSsid(static_cast<apple80211_ssid_data *>(data));
        case APPLE80211_IOC_AUTH_TYPE:
            return rtlwmGetAuthType(static_cast<apple80211_authtype_data *>(data));
        case APPLE80211_IOC_CHANNEL:
            return rtlwmGetChannel(static_cast<apple80211_channel_data *>(data));
        case APPLE80211_IOC_BSSID:
            return rtlwmGetBssid(static_cast<apple80211_bssid_data *>(data));
        case APPLE80211_IOC_RSSI:
            return rtlwmGetRssi(static_cast<apple80211_rssi_data *>(data));
        case APPLE80211_IOC_NOISE:
            return rtlwmGetNoise(controller, static_cast<apple80211_noise_data *>(data));
        case APPLE80211_IOC_RATE:
            return rtlwmGetRate(static_cast<apple80211_rate_data *>(data));
        case APPLE80211_IOC_STATE:
            return rtlwmGetState(controller, static_cast<apple80211_state_data *>(data));
        case APPLE80211_IOC_PHY_MODE:
            return rtlwmGetPhyMode(static_cast<apple80211_phymode_data *>(data));
        case APPLE80211_IOC_OP_MODE:
            return rtlwmGetOpMode(static_cast<apple80211_opmode_data *>(data));
        case APPLE80211_IOC_POWER:
            return rtlwmGetPower(controller, static_cast<apple80211_power_data *>(data));
        case APPLE80211_IOC_CARD_CAPABILITIES:
            return rtlwmGetCapabilities(static_cast<apple80211_capability_data *>(data));
        case APPLE80211_IOC_SUPPORTED_CHANNELS:
        case APPLE80211_IOC_HW_SUPPORTED_CHANNELS:
            return rtlwmGetSupportedChannels(controller, static_cast<apple80211_sup_channel_data *>(data));
        case APPLE80211_IOC_LOCALE:
            return rtlwmGetLocale(static_cast<apple80211_locale_data *>(data));
        case APPLE80211_IOC_TX_NSS:
            return rtlwmGetTxNss(controller, static_cast<apple80211_tx_nss_data *>(data));
        case APPLE80211_IOC_DRIVER_VERSION:
            return rtlwmGetVersion(static_cast<apple80211_version_data *>(data), "rtlwm-airport");
        case APPLE80211_IOC_HARDWARE_VERSION:
            return rtlwmGetVersion(static_cast<apple80211_version_data *>(data), "realtek-pcie");
        case APPLE80211_IOC_COUNTRY_CODE:
            return rtlwmGetCountryCode(controller, static_cast<apple80211_country_code_data *>(data));
        case APPLE80211_IOC_MCS:
            return rtlwmGetMcs(static_cast<apple80211_mcs_data *>(data));
        case APPLE80211_IOC_ASSOCIATE_RESULT:
            return rtlwmGetAssociateResult(static_cast<apple80211_assoc_result_data *>(data));
        default:
            return EOPNOTSUPP;
        }
    }

    switch (request) {
    case APPLE80211_IOC_POWER:
    case APPLE80211_IOC_SCAN_REQ:
        return kIOReturnSuccess;
    case APPLE80211_IOC_DISASSOCIATE:
    case APPLE80211_IOC_DEAUTH:
        if (RtlDriverController *driverController = controller->getDriverController())
            driverController->clearScanningFlags();
        return kIOReturnSuccess;
    default:
        return EOPNOTSUPP;
    }
}
