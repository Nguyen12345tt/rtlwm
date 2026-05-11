/*
 * RtlHal_rtw88.hpp – HAL for RTW88 family (RTL8822BE/CE, RTL8723DE, RTL8821CE …)
 *
 * These chips are supported by the Linux rtw88 driver
 * (drivers/net/wireless/realtek/rtw88/).
 *
 * This HAL wraps the ported Linux rtw88 code behind the RtlHalService
 * abstract interface so that the Airport / non-Airport upper layers remain
 * chip-agnostic.
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef RtlHal_rtw88_hpp
#define RtlHal_rtw88_hpp

#include <HAL/RtlHalService.hpp>
#include <HAL/RtlDriverInfo.hpp>
#include <HAL/RtlDriverController.hpp>
#include <net80211/ieee80211_var.h>

/* RTW88 chip variant IDs -------------------------------------------------- */
enum rtw88_chip_id {
    RTW88_CHIP_8822B = 0,
    RTW88_CHIP_8822C,
    RTW88_CHIP_8723D,
    RTW88_CHIP_8821C,
    RTW88_CHIP_UNKNOWN,
};

/* RTW88 hardware context -------------------------------------------------- */
struct rtw88_dev {
    IOPCIDevice        *pciDev;
    IOMemoryMap        *mmioMap;
    volatile uint8_t   *mmio;             /* MMIO base */
    enum rtw88_chip_id  chip_id;
    uint8_t             mac_addr[6];
    char                fw_name[64];
    char                fw_version[32];
    bool                running;
    /* TODO: add TX/RX ring descriptors, firmware state, etc. */
};

/* --------------------------------------------------------------------------
 * RtlHal_rtw88 – concrete HAL class for RTW88 chips
 * -------------------------------------------------------------------------- */
class RtlHal_rtw88 : public RtlHalService,
                     public RtlDriverInfo,
                     public RtlDriverController
{
    OSDeclareDefaultStructors(RtlHal_rtw88)

public:
    /* Factory */
    static RtlHal_rtw88 *withDevice(IOPCIDevice *device);

    /* RtlHalService */
    bool      attach(IOPCIDevice *device)            override;
    void      detach(IOPCIDevice *device)            override;
    IOReturn  enable(IONetworkInterface *interface)  override;
    IOReturn  disable(IONetworkInterface *interface) override;
    struct ieee80211com *get80211Controller()        override;
    RtlDriverInfo       *getDriverInfo()             override { return this; }
    RtlDriverController *getDriverController()       override { return this; }
    void free()                                      override;

    /* RtlDriverInfo */
    const char *getFirmwareVersion()     override;
    int16_t     getBSSNoise()            override;
    bool        is5GBandSupport()        override;
    int         getTxNSS()               override;
    const char *getFirmwareName()        override;
    UInt32      supportedFeatures()      override;
    const char *getFirmwareCountryCode() override;
    uint32_t    getTxQueueSize()         override;

    /* RtlDriverController */
    void     clearScanningFlags()                               override;
    IOReturn setMulticastList(IOEthernetAddress *addr, int cnt) override;

private:
    bool initHardware();
    bool loadFirmware();
    void initRF();
    void startTxRx();
    void stopTxRx();

    struct rtw88_dev    hw;
    struct ieee80211com ic;
};

#endif /* RtlHal_rtw88_hpp */
