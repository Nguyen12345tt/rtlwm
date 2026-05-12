/*
 * RtlHal_rtw89.hpp – HAL for RTW89 family (RTL8852AE/BE, RTL8851BE, RTL8922AE …)
 *
 * These chips are supported by the Linux rtw89 driver
 * (drivers/net/wireless/realtek/rtw89/).
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef RtlHal_rtw89_hpp
#define RtlHal_rtw89_hpp

#include <HAL/RtlHalService.hpp>
#include <HAL/RtlDriverInfo.hpp>
#include <HAL/RtlDriverController.hpp>
#include <net80211/ieee80211_var.h>

/* RTW89 chip variant IDs -------------------------------------------------- */
enum rtw89_chip_id {
    RTW89_CHIP_8852A = 0,
    RTW89_CHIP_8852B,
    RTW89_CHIP_8851B,
    RTW89_CHIP_8852C,
    RTW89_CHIP_8922A,
    RTW89_CHIP_UNKNOWN,
};

struct rtw89_dma_desc {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t ctl0;
    uint32_t ctl1;
};

/* RTW89 hardware context -------------------------------------------------- */
struct rtw89_dev {
    IOPCIDevice        *pciDev;
    IOMemoryMap        *mmioMap;
    volatile uint8_t   *mmio;
    enum rtw89_chip_id  chip_id;
    uint8_t             mac_addr[6];
    char                fw_name[64];
    char                fw_version[32];
    bool                running;
    bool                tx_active;
    bool                rx_active;
    uint32_t            irq_count;
    uint16_t            tx_prod;
    uint16_t            tx_cons;
    uint16_t            rx_prod;
    uint16_t            rx_cons;
    struct rtw89_dma_desc *tx_desc;
    struct rtw89_dma_desc *rx_desc;
    uint16_t            tx_desc_cnt;
    uint16_t            rx_desc_cnt;
    bool                efuse_valid;
    bool                iqk_done;
    bool                dpk_done;
    uint8_t             efuse_map[1024];
    uint16_t            efuse_len;
    uint8_t             txpwr_2g;
    uint8_t             txpwr_5g;
    /* Pending: CAM tables and BE-specific MAC structures. */
};

/* --------------------------------------------------------------------------
 * RtlHal_rtw89 – concrete HAL class for RTW89 chips
 * -------------------------------------------------------------------------- */
class RtlHal_rtw89 : public RtlHalService,
                     public RtlDriverInfo,
                     public RtlDriverController
{
    OSDeclareDefaultStructors(RtlHal_rtw89)

public:
    /* Factory */
    static RtlHal_rtw89 *withDevice(IOPCIDevice *device);

    /* RtlHalService */
    bool      attach(IOPCIDevice *device)            override;
    void      detach(IOPCIDevice *device)            override;
    IOReturn  enable(IONetworkInterface *interface)  override;
    IOReturn  disable(IONetworkInterface *interface) override;
    void      handleInterrupt()                      override;
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
    const uint8_t *getMacAddress()       override;
    bool        setMacAddress(const uint8_t *addr) override;

    /* RtlDriverController */
    void     clearScanningFlags()                               override;
    IOReturn setMulticastList(IOEthernetAddress *addr, int cnt) override;

private:
    bool initHardware();
    bool loadFirmware();
    void initRF();
    void startTxRx();
    void stopTxRx();

    struct rtw89_dev    hw;
    struct ieee80211com ic;
};

#endif /* RtlHal_rtw89_hpp */
