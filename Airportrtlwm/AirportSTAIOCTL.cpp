/*
 * AirportSTAIOCTL.cpp – Station-mode IOCTL dispatch table
 */

#include "Airportrtlwm.hpp"
#include <Airport/apple80211_ioctl.h>
#include <Airport/apple80211_var.h>
#include <sys/errno.h>
#include <string.h>

static uint32_t sAuthTypeLower = APPLE80211_AUTHTYPE_OPEN;
static uint32_t sAuthTypeUpper = APPLE80211_AUTHTYPE_NONE;
static uint32_t sPowersaveMode = APPLE80211_POWERSAVE_MODE_DISABLED;
static uint32_t sSavedSsidLen = 0;
static uint8_t sSavedSsid[APPLE80211_MAX_SSID_LEN] = {};
static bool sHasSavedBssid = false;
static ether_addr sSavedBssid = {};
static uint16_t sSavedRsnIeLen = 0;
static uint8_t sSavedRsnIe[APPLE80211_MAX_RSN_IE_LEN] = {};
static uint32_t sAssociationState = APPLE80211_S_INIT;
static uint32_t sAssociationStatus = APPLE80211_STATUS_UNAVAILABLE;
static uint32_t sAssociationResult = APPLE80211_RESULT_UNAVAILABLE;
static bool sAssociationActive = false;

static bool rtlwmIsGetRequest(UInt requestType)
{
    return requestType == SIOCGA80211;
}

static bool rtlwmIsSetRequest(UInt requestType)
{
    return requestType == SIOCSA80211;
}

template <typename T>
static bool rtlwmInitVersioned(T *out)
{
    if (!out)
        return false;

    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    return true;
}

static void rtlwmSetCapabilityBit(struct apple80211_capability_data *cap, uint32_t bit)
{
    const uint32_t byteIndex = bit / 8;
    const uint32_t bitMask = 1u << (bit % 8);
    if (byteIndex < sizeof(cap->capabilities))
        cap->capabilities[byteIndex] |= static_cast<uint8_t>(bitMask);
}

static void rtlwmRememberSsid(const uint8_t *ssid, uint32_t ssidLen)
{
    sSavedSsidLen = ssidLen > APPLE80211_MAX_SSID_LEN ? APPLE80211_MAX_SSID_LEN : ssidLen;
    bzero(sSavedSsid, sizeof(sSavedSsid));
    if (ssid && sSavedSsidLen)
        memcpy(sSavedSsid, ssid, sSavedSsidLen);
}

static void rtlwmRememberBssid(const ether_addr *bssid)
{
    if (!bssid) {
        sHasSavedBssid = false;
        bzero(&sSavedBssid, sizeof(sSavedBssid));
        return;
    }

    static const ether_addr emptyBssid = {};
    if (memcmp(bssid, &emptyBssid, sizeof(emptyBssid)) == 0) {
        sHasSavedBssid = false;
        bzero(&sSavedBssid, sizeof(sSavedBssid));
        return;
    }

    sHasSavedBssid = true;
    memcpy(&sSavedBssid, bssid, sizeof(sSavedBssid));
}

static void rtlwmRememberRsnIe(const uint8_t *ie, uint16_t ieLen)
{
    sSavedRsnIeLen = ieLen > APPLE80211_MAX_RSN_IE_LEN ? APPLE80211_MAX_RSN_IE_LEN : ieLen;
    bzero(sSavedRsnIe, sizeof(sSavedRsnIe));
    if (ie && sSavedRsnIeLen)
        memcpy(sSavedRsnIe, ie, sSavedRsnIeLen);
}

static void rtlwmClearAssociationState()
{
    rtlwmRememberSsid(nullptr, 0);
    rtlwmRememberBssid(nullptr);
    rtlwmRememberRsnIe(nullptr, 0);
}

static void rtlwmSetAssociationUnavailable(uint32_t state)
{
    sAssociationState = state;
    sAssociationStatus = APPLE80211_STATUS_UNAVAILABLE;
    sAssociationResult = APPLE80211_RESULT_UNAVAILABLE;
    sAssociationActive = false;
}

static void rtlwmSetAssociationSuccess()
{
    sAssociationState = APPLE80211_S_RUN;
    sAssociationStatus = APPLE80211_STATUS_SUCCESS;
    sAssociationResult = APPLE80211_RESULT_SUCCESS;
    sAssociationActive = true;
}

static void rtlwmSetAssociationFailure(uint32_t state, uint32_t status, uint32_t result)
{
    sAssociationState = state;
    sAssociationStatus = status;
    sAssociationResult = result;
    sAssociationActive = false;
}

static SInt32 rtlwmGetSsid(apple80211_ssid_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->ssid_len = sSavedSsidLen;
    if (sSavedSsidLen)
        memcpy(out->ssid_bytes, sSavedSsid, sSavedSsidLen);
    return kIOReturnSuccess;
}

static SInt32 rtlwmSetSsid(const apple80211_ssid_data *in)
{
    if (!in)
        return EINVAL;

    rtlwmRememberSsid(in->ssid_bytes, in->ssid_len);
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetAuthType(apple80211_authtype_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->authtype_lower = sAuthTypeLower;
    out->authtype_upper = sAuthTypeUpper;
    return kIOReturnSuccess;
}

static SInt32 rtlwmSetAuthType(const apple80211_authtype_data *in)
{
    if (!in)
        return EINVAL;

    sAuthTypeLower = in->authtype_lower;
    sAuthTypeUpper = in->authtype_upper;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetChannel(apple80211_channel_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->channel.version = APPLE80211_VERSION;
    out->channel.channel = 1;
    out->channel.flags = APPLE80211_C_FLAG_2GHZ | APPLE80211_C_FLAG_ACTIVE | APPLE80211_C_FLAG_20MHZ;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetProtMode(apple80211_protmode_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->protmode = APPLE80211_PROTMODE_AUTO;
    out->threshold = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetTxPower(apple80211_txpower_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->txpower_unit = APPLE80211_UNIT_DBM;
    out->txpower = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetBssid(apple80211_bssid_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    if (sHasSavedBssid)
        memcpy(&out->bssid, &sSavedBssid, sizeof(out->bssid));
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetScanResult(apple80211_scan_result *out)
{
    if (!out)
        return EINVAL;

    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    return kIOReturnNotFound;
}

static SInt32 rtlwmGetRssi(apple80211_rssi_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->num_radios = 1;
    out->rssi_unit = APPLE80211_UNIT_DBM;
    out->rssi[0] = -95;
    out->aggregate_rssi = out->rssi[0];
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetNoise(Airportrtlwm *controller, apple80211_noise_data *out)
{
    if (!controller || !rtlwmInitVersioned(out))
        return EINVAL;

    int32_t noise = -95;
    if (RtlDriverInfo *info = controller->getDriverInfo())
        noise = info->getBSSNoise();

    out->num_radios = 1;
    out->noise_unit = APPLE80211_UNIT_DBM;
    out->noise[0] = noise;
    out->aggregate_noise = noise;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetRate(apple80211_rate_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->num_radios = 1;
    out->rate[0] = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetCapabilities(apple80211_capability_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    rtlwmSetCapabilityBit(out, APPLE80211_CAP_IBSS);
    rtlwmSetCapabilityBit(out, APPLE80211_CAP_PMGT);
    rtlwmSetCapabilityBit(out, APPLE80211_CAP_TXPMGT);
    rtlwmSetCapabilityBit(out, APPLE80211_CAP_SHSLOT);
    rtlwmSetCapabilityBit(out, APPLE80211_CAP_WPA2);
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetState(Airportrtlwm *controller, apple80211_state_data *out)
{
    if (!controller || !rtlwmInitVersioned(out))
        return EINVAL;

    if (sAssociationActive) {
        out->state = APPLE80211_S_RUN;
    } else if (sAssociationState != APPLE80211_S_INIT) {
        out->state = sAssociationState;
    } else {
        out->state = controller->isInterfaceEnabled() ? APPLE80211_S_SCAN : APPLE80211_S_INIT;
    }
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetPhyMode(apple80211_phymode_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->phy_mode = APPLE80211_MODE_11B | APPLE80211_MODE_11G | APPLE80211_MODE_11N;
    out->active_phy_mode = APPLE80211_MODE_AUTO;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetOpMode(apple80211_opmode_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->op_mode = APPLE80211_M_STA;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetIntMit(apple80211_intmit_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->int_mit = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetPower(Airportrtlwm *controller, apple80211_power_data *out)
{
    if (!controller || !rtlwmInitVersioned(out))
        return EINVAL;

    out->num_radios = 1;
    out->power_state[0] = controller->isInterfaceEnabled() ? APPLE80211_POWER_ON : APPLE80211_POWER_OFF;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetAssociateResult(apple80211_assoc_result_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->result = sAssociationResult;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetAssocStatus(apple80211_assoc_status_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->status = sAssociationStatus;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetRateSet(apple80211_rate_set_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->num_rates = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetSupportedChannels(Airportrtlwm *controller, apple80211_sup_channel_data *out)
{
    if (!controller || !rtlwmInitVersioned(out))
        return EINVAL;

    const bool has5g = controller->getDriverInfo() ? controller->getDriverInfo()->is5GBandSupport() : false;
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
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->locale = APPLE80211_LOCALE_FCC;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetAntenna(apple80211_antenna_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->num_radios = 1;
    out->antenna_index[0] = 0;
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

static SInt32 rtlwmGetRsnIe(apple80211_rsn_ie_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->len = sSavedRsnIeLen;
    if (sSavedRsnIeLen)
        memcpy(out->ie, sSavedRsnIe, sSavedRsnIeLen);
    return kIOReturnSuccess;
}

static SInt32 rtlwmSetRsnIe(const apple80211_rsn_ie_data *in)
{
    if (!in)
        return EINVAL;

    rtlwmRememberRsnIe(in->ie, in->len);
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetApIeList(apple80211_ap_ie_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->len = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetCountryCode(Airportrtlwm *controller, apple80211_country_code_data *out)
{
    if (!controller || !rtlwmInitVersioned(out))
        return EINVAL;

    const char *country = "US";
    if (RtlDriverInfo *info = controller->getDriverInfo()) {
        const char *cc = info->getFirmwareCountryCode();
        if (cc && cc[0])
            country = cc;
    }

    const size_t ccLen = strnlen(country, APPLE80211_MAX_CC_LEN);
    memcpy(out->cc, country, ccLen);
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetRadioInfo(Airportrtlwm *controller, apple80211_radio_info_data *out)
{
    if (!controller || !rtlwmInitVersioned(out))
        return EINVAL;

    uint32_t radioCount = 1;
    if (RtlDriverInfo *info = controller->getDriverInfo()) {
        const int txNss = info->getTxNSS();
        if (txNss > 1)
            radioCount = static_cast<uint32_t>(txNss);
    }

    out->count = radioCount;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetMcs(apple80211_mcs_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->index = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetMcsVht(Airportrtlwm *controller, apple80211_mcs_vht_data *out)
{
    if (!controller || !rtlwmInitVersioned(out))
        return EINVAL;

    out->index = 0;
    out->nss = controller->getDriverInfo() ? static_cast<uint32_t>(controller->getDriverInfo()->getTxNSS()) : 1;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetMcsIndexSet(apple80211_mcs_index_set_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->mcs_set_map[0] = 0x1;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetVhtMcsIndexSet(apple80211_vht_mcs_index_set_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->mcs_map = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetTxNss(Airportrtlwm *controller, apple80211_tx_nss_data *out)
{
    if (!controller || !rtlwmInitVersioned(out))
        return EINVAL;

    out->nss = controller->getDriverInfo() ? static_cast<uint8_t>(controller->getDriverInfo()->getTxNSS()) : 1;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetNss(Airportrtlwm *controller, apple80211_nss_data *out)
{
    if (!controller || !rtlwmInitVersioned(out))
        return EINVAL;

    out->nss = controller->getDriverInfo() ? static_cast<uint8_t>(controller->getDriverInfo()->getTxNSS()) : 1;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetRoamThreshold(apple80211_roam_threshold_data *out)
{
    if (!out)
        return EINVAL;

    out->threshold = 0;
    out->count = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetLinkChangedEventData(Airportrtlwm *controller, apple80211_link_changed_event_data *out)
{
    if (!controller || !out)
        return EINVAL;

    bzero(out, sizeof(*out));
    out->isLinkDown = !controller->isInterfaceEnabled();
    out->rssi = 0;
    out->nf = 0;
    out->cca = 0;
    out->voluntary = false;
    out->reason = 0;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetPowersave(apple80211_powersave_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    out->powersave_level = sPowersaveMode;
    return kIOReturnSuccess;
}

static SInt32 rtlwmSetPowersave(const apple80211_powersave_data *in)
{
    if (!in)
        return EINVAL;

    sPowersaveMode = in->powersave_level;
    return kIOReturnSuccess;
}

static SInt32 rtlwmGetWowParameters(apple80211_wow_parameter_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    return kIOReturnSuccess;
}

static SInt32 rtlwmGetRoamProfile(apple80211_roam_profile_band_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    return kIOReturnSuccess;
}

static SInt32 rtlwmGetIe(apple80211_ie_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    return kIOReturnSuccess;
}

static SInt32 rtlwmGetBtcProfiles(apple80211_btc_profiles_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    return kIOReturnSuccess;
}

static SInt32 rtlwmGetBtcConfig(apple80211_btc_config_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    return kIOReturnSuccess;
}

static SInt32 rtlwmGetBtcMode(apple80211_btc_mode_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    return kIOReturnSuccess;
}

static SInt32 rtlwmGetBtcOptions(apple80211_btc_options_data *out)
{
    if (!rtlwmInitVersioned(out))
        return EINVAL;

    return kIOReturnSuccess;
}

static SInt32 rtlwmHandleDisassociate(Airportrtlwm *controller)
{
    if (RtlDriverController *driverController = controller ? controller->getDriverController() : nullptr)
        driverController->clearScanningFlags();
    rtlwmClearAssociationState();
    rtlwmSetAssociationUnavailable(controller && controller->isInterfaceEnabled() ? APPLE80211_S_SCAN : APPLE80211_S_INIT);
    return kIOReturnSuccess;
}

static SInt32 rtlwmHandleAssociate(const apple80211_assoc_data *in)
{
    if (!in)
        return EINVAL;
    if (in->ad_ssid_len > APPLE80211_MAX_SSID_LEN) {
        rtlwmSetAssociationFailure(APPLE80211_S_SCAN, APPLE80211_STATUS_UNSPECIFIED_FAILURE, APPLE80211_RESULT_UNSPECIFIED_FAILURE);
        return EINVAL;
    }

    static const ether_addr emptyBssid = {};
    const bool hasSsid = in->ad_ssid_len > 0;
    const bool hasBssid = memcmp(&in->ad_bssid, &emptyBssid, sizeof(emptyBssid)) != 0;
    if (!hasSsid && !hasBssid) {
        rtlwmSetAssociationFailure(APPLE80211_S_SCAN, APPLE80211_STATUS_ASSOCIATION_DENIED, APPLE80211_RESULT_ASSOCIATION_DENIED);
        return EINVAL;
    }

    sAssociationState = APPLE80211_S_ASSOC;
    rtlwmRememberSsid(in->ad_ssid, in->ad_ssid_len);
    rtlwmRememberBssid(&in->ad_bssid);
    rtlwmRememberRsnIe(in->ad_rsn_ie, static_cast<uint16_t>(in->ad_rsn_ie_len));
    sAuthTypeLower = in->ad_auth_lower;
    sAuthTypeUpper = in->ad_auth_upper;
    rtlwmSetAssociationSuccess();
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
        case APPLE80211_IOC_PROTMODE:
            return rtlwmGetProtMode(static_cast<apple80211_protmode_data *>(data));
        case APPLE80211_IOC_TXPOWER:
            return rtlwmGetTxPower(static_cast<apple80211_txpower_data *>(data));
        case APPLE80211_IOC_RATE:
            return rtlwmGetRate(static_cast<apple80211_rate_data *>(data));
        case APPLE80211_IOC_BSSID:
            return rtlwmGetBssid(static_cast<apple80211_bssid_data *>(data));
        case APPLE80211_IOC_SCAN_RESULT:
            return rtlwmGetScanResult(static_cast<apple80211_scan_result *>(data));
        case APPLE80211_IOC_CARD_CAPABILITIES:
            return rtlwmGetCapabilities(static_cast<apple80211_capability_data *>(data));
        case APPLE80211_IOC_STATE:
            return rtlwmGetState(controller, static_cast<apple80211_state_data *>(data));
        case APPLE80211_IOC_PHY_MODE:
            return rtlwmGetPhyMode(static_cast<apple80211_phymode_data *>(data));
        case APPLE80211_IOC_OP_MODE:
            return rtlwmGetOpMode(static_cast<apple80211_opmode_data *>(data));
        case APPLE80211_IOC_RSSI:
            return rtlwmGetRssi(static_cast<apple80211_rssi_data *>(data));
        case APPLE80211_IOC_NOISE:
            return rtlwmGetNoise(controller, static_cast<apple80211_noise_data *>(data));
        case APPLE80211_IOC_INT_MIT:
            return rtlwmGetIntMit(static_cast<apple80211_intmit_data *>(data));
        case APPLE80211_IOC_POWER:
            return rtlwmGetPower(controller, static_cast<apple80211_power_data *>(data));
        case APPLE80211_IOC_ASSOCIATE_RESULT:
            return rtlwmGetAssociateResult(static_cast<apple80211_assoc_result_data *>(data));
        case APPLE80211_IOC_RATE_SET:
            return rtlwmGetRateSet(static_cast<apple80211_rate_set_data *>(data));
        case APPLE80211_IOC_SUPPORTED_CHANNELS:
        case APPLE80211_IOC_HW_SUPPORTED_CHANNELS:
            return rtlwmGetSupportedChannels(controller, static_cast<apple80211_sup_channel_data *>(data));
        case APPLE80211_IOC_LOCALE:
            return rtlwmGetLocale(static_cast<apple80211_locale_data *>(data));
        case APPLE80211_IOC_TX_ANTENNA:
        case APPLE80211_IOC_ANTENNA_DIVERSITY:
            return rtlwmGetAntenna(static_cast<apple80211_antenna_data *>(data));
        case APPLE80211_IOC_DRIVER_VERSION:
            return rtlwmGetVersion(static_cast<apple80211_version_data *>(data), "rtlwm-airport");
        case APPLE80211_IOC_HARDWARE_VERSION: {
            const char *hardwareVersion = "realtek-pcie";
            if (RtlDriverInfo *info = controller->getDriverInfo()) {
                const char *name = info->getFirmwareName();
                if (name && name[0])
                    hardwareVersion = name;
            }
            return rtlwmGetVersion(static_cast<apple80211_version_data *>(data), hardwareVersion);
        }
        case APPLE80211_IOC_RSN_IE:
            return rtlwmGetRsnIe(static_cast<apple80211_rsn_ie_data *>(data));
        case APPLE80211_IOC_AP_IE_LIST:
            return rtlwmGetApIeList(static_cast<apple80211_ap_ie_data *>(data));
        case APPLE80211_IOC_ASSOCIATION_STATUS:
            return rtlwmGetAssocStatus(static_cast<apple80211_assoc_status_data *>(data));
        case APPLE80211_IOC_COUNTRY_CODE:
            return rtlwmGetCountryCode(controller, static_cast<apple80211_country_code_data *>(data));
        case APPLE80211_IOC_RADIO_INFO:
            return rtlwmGetRadioInfo(controller, static_cast<apple80211_radio_info_data *>(data));
        case APPLE80211_IOC_MCS:
            return rtlwmGetMcs(static_cast<apple80211_mcs_data *>(data));
        case APPLE80211_IOC_MCS_INDEX_SET:
            return rtlwmGetMcsIndexSet(static_cast<apple80211_mcs_index_set_data *>(data));
        case APPLE80211_IOC_VHT_MCS_INDEX_SET:
            return rtlwmGetVhtMcsIndexSet(static_cast<apple80211_vht_mcs_index_set_data *>(data));
        case APPLE80211_IOC_MCS_VHT:
            return rtlwmGetMcsVht(controller, static_cast<apple80211_mcs_vht_data *>(data));
        case APPLE80211_IOC_ROAM_THRESH:
            return rtlwmGetRoamThreshold(static_cast<apple80211_roam_threshold_data *>(data));
        case APPLE80211_IOC_LINK_CHANGED_EVENT_DATA:
            return rtlwmGetLinkChangedEventData(controller, static_cast<apple80211_link_changed_event_data *>(data));
        case APPLE80211_IOC_POWERSAVE:
            return rtlwmGetPowersave(static_cast<apple80211_powersave_data *>(data));
        case APPLE80211_IOC_TX_NSS:
            return rtlwmGetTxNss(controller, static_cast<apple80211_tx_nss_data *>(data));
        case APPLE80211_IOC_NSS:
            return rtlwmGetNss(controller, static_cast<apple80211_nss_data *>(data));
        case APPLE80211_IOC_WOW_PARAMETERS:
            return rtlwmGetWowParameters(static_cast<apple80211_wow_parameter_data *>(data));
        case APPLE80211_IOC_ROAM_PROFILE:
            return rtlwmGetRoamProfile(static_cast<apple80211_roam_profile_band_data *>(data));
        case APPLE80211_IOC_IE:
            return rtlwmGetIe(static_cast<apple80211_ie_data *>(data));
        case APPLE80211_IOC_BTCOEX_PROFILES:
            return rtlwmGetBtcProfiles(static_cast<apple80211_btc_profiles_data *>(data));
        case APPLE80211_IOC_BTCOEX_CONFIG:
            return rtlwmGetBtcConfig(static_cast<apple80211_btc_config_data *>(data));
        case APPLE80211_IOC_BTCOEX_OPTIONS:
            return rtlwmGetBtcOptions(static_cast<apple80211_btc_options_data *>(data));
        case APPLE80211_IOC_BTCOEX_MODE:
            return rtlwmGetBtcMode(static_cast<apple80211_btc_mode_data *>(data));
        default:
            return EOPNOTSUPP;
        }
    }

    switch (request) {
    case APPLE80211_IOC_SSID:
        return rtlwmSetSsid(static_cast<const apple80211_ssid_data *>(data));
    case APPLE80211_IOC_AUTH_TYPE:
        return rtlwmSetAuthType(static_cast<const apple80211_authtype_data *>(data));
    case APPLE80211_IOC_SCAN_REQ:
    case APPLE80211_IOC_SCAN_REQ_MULTIPLE:
    case APPLE80211_IOC_POWER:
    case APPLE80211_IOC_COUNTRY_CODE:
    case APPLE80211_IOC_TX_NSS:
    case APPLE80211_IOC_ROAM:
    case APPLE80211_IOC_ROAM_PROFILE:
    case APPLE80211_IOC_MCS_VHT:
    case APPLE80211_IOC_WOW_PARAMETERS:
    case APPLE80211_IOC_IE:
    case APPLE80211_IOC_P2P_LISTEN:
    case APPLE80211_IOC_P2P_SCAN:
    case APPLE80211_IOC_P2P_GO_CONF:
    case APPLE80211_IOC_BTCOEX_PROFILES:
    case APPLE80211_IOC_BTCOEX_CONFIG:
    case APPLE80211_IOC_BTCOEX_OPTIONS:
    case APPLE80211_IOC_BTCOEX_MODE:
    case APPLE80211_IOC_SCANCACHE_CLEAR:
        return kIOReturnSuccess;
    case APPLE80211_IOC_POWERSAVE:
        return rtlwmSetPowersave(static_cast<const apple80211_powersave_data *>(data));
    case APPLE80211_IOC_ASSOCIATE:
        return rtlwmHandleAssociate(static_cast<const apple80211_assoc_data *>(data));
    case APPLE80211_IOC_DISASSOCIATE:
    case APPLE80211_IOC_DEAUTH:
        return rtlwmHandleDisassociate(controller);
    case APPLE80211_IOC_RSN_IE:
        return rtlwmSetRsnIe(static_cast<const apple80211_rsn_ie_data *>(data));
    case APPLE80211_IOC_CIPHER_KEY:
        return kIOReturnSuccess;
    default:
        return EOPNOTSUPP;
    }
}
