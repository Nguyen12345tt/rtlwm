/*
 * RtlHal_rtw89.cpp – HAL implementation for RTW89 family
 *
 * Porting guide (Linux → macOS):
 *  • drivers/net/wireless/realtek/rtw89/core.c  → attach/detach/enable/disable
 *  • drivers/net/wireless/realtek/rtw89/pci.c   → PCI probe / MMIO
 *  • drivers/net/wireless/realtek/rtw89/mac.c   → initHardware
 *  • drivers/net/wireless/realtek/rtw89/fw.c    → loadFirmware
 *  • drivers/net/wireless/realtek/rtw89/phy.c   → initRF
 *
 * RTW89 uses a significantly different firmware model compared to RTW88:
 *  - Firmware split into WLAN-FW + BT-FW sections
 *  - C2H / H2C command mechanism for firmware communication
 *  - New CAM-based security engine
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "RtlHal_rtw89.hpp"
#include <FwData.h>
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <stdio.h>

OSDefineMetaClassAndStructors(RtlHal_rtw89, RtlHalService)

namespace {
struct regval32 {
    uint32_t reg;
    uint32_t val;
};

enum : uint32_t {
    REG_R_AX_PLATFORM_ENABLE   = 0x0000,
    REG_R_AX_SYS_FUNC_EN       = 0x0002,
    REG_R_AX_HCI_FUNC_EN       = 0x0074,
    REG_R_AX_HALT_H2C_CTRL     = 0x01B8,
    REG_R_AX_DMAC_FUNC_EN      = 0x8400,
    REG_R_AX_CMAC_FUNC_EN      = 0xC000,
    REG_R_AX_HIMR0             = 0x01A0,
    REG_R_AX_HISR0             = 0x01A4,
    REG_R_AX_MAC_ID0           = 0x0610,
    REG_R_AX_MAC_ID1           = 0x0614,
    REG_R_AX_PHY0_RFMOD        = 0x4700,
    REG_R_AX_PHY0_CHNUM        = 0x4718,
    REG_R_AX_EFUSE_BASE        = 0x7000,
    REG_R_AX_IQK_CTRL          = 0x5860,
    REG_R_AX_IQK_STS           = 0x5864,
    REG_R_AX_DPK_CTRL          = 0x58a0,
    REG_R_AX_DPK_STS           = 0x58a4,
    REG_R_AX_TCR               = 0x11A0,
    REG_R_AX_RCR               = 0x11A4,
    REG_R_AX_CCA_CFG0          = 0x4730,
    REG_R_AX_CCA_CFG1          = 0x4734,
    REG_R_AX_PATH_COM          = 0x4720,
    REG_R_AX_PATH_COM_2        = 0x4724,
    REG_R_AX_TXPWR_2G          = 0x46E0,
    REG_R_AX_TXPWR_5G          = 0x46E4,
    REG_R_AX_TRXPTCL_CTRL      = 0x1140,
    REG_R_AX_RESP_RATE         = 0x11B0,
};

enum : uint32_t {
    AX_HCI_TXDMA_EN           = (1U << 0),
    AX_HCI_RXDMA_EN           = (1U << 1),
    AX_HCI_TRX_ENABLE         = AX_HCI_TXDMA_EN | AX_HCI_RXDMA_EN,
    AX_DMAC_ENABLE            = (1U << 0),
    AX_CMAC_ENABLE            = (1U << 0),
    AX_SYS_MAC_CPU_ENABLE     = (1U << 11),
    AX_IQK_START              = (1U << 0),
    AX_IQK_DONE               = (1U << 0),
    AX_DPK_START              = (1U << 0),
    AX_DPK_DONE               = (1U << 0),
};

static inline uint16_t mmioRead16(volatile uint8_t *base, uint32_t reg)
{
    return OSReadLittleInt16((const volatile void *)(base + reg), 0);
}

static inline uint32_t mmioRead32(volatile uint8_t *base, uint32_t reg)
{
    return OSReadLittleInt32((const volatile void *)(base + reg), 0);
}

static inline void mmioWrite16(volatile uint8_t *base, uint32_t reg, uint16_t val)
{
    OSWriteLittleInt16((volatile void *)(base + reg), 0, val);
}

static inline void mmioWrite32(volatile uint8_t *base, uint32_t reg, uint32_t val)
{
    OSWriteLittleInt32((volatile void *)(base + reg), 0, val);
}

static bool pollReg32(volatile uint8_t *base, uint32_t reg, uint32_t mask,
                      uint32_t expect, int loops, int delayUs)
{
    for (int i = 0; i < loops; i++) {
        if ((mmioRead32(base, reg) & mask) == expect)
            return true;
        if (delayUs > 0)
            IODelay(delayUs);
    }
    return false;
}

static bool writeMacAddressRegs(volatile uint8_t *base, const uint8_t mac[6])
{
    if (!base || !mac)
        return false;
    uint32_t lo = ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
                  ((uint32_t)mac[1] << 8) | mac[0];
    uint16_t hi = ((uint16_t)mac[5] << 8) | mac[4];
    mmioWrite32(base, REG_R_AX_MAC_ID0, lo);
    mmioWrite16(base, REG_R_AX_MAC_ID1, hi);
    return true;
}

static const regval32 rtw89_phy_init_8852a[] = {
    { REG_R_AX_PHY0_RFMOD, 0x00000001 }, { REG_R_AX_PHY0_CHNUM, 0x00000001 },
    { REG_R_AX_CCA_CFG0, 0x00000f3c }, { REG_R_AX_CCA_CFG1, 0x00003030 },
    { REG_R_AX_PATH_COM, 0x00000033 }, { REG_R_AX_PATH_COM_2, 0x00000022 },
    { REG_R_AX_TXPWR_2G, 0x20202020 }, { REG_R_AX_TXPWR_5G, 0x24242424 },
};
static const regval32 rtw89_phy_init_8852b[] = {
    { REG_R_AX_PHY0_RFMOD, 0x00000003 }, { REG_R_AX_PHY0_CHNUM, 0x00000001 },
    { REG_R_AX_CCA_CFG0, 0x00000f5c }, { REG_R_AX_CCA_CFG1, 0x00004040 },
    { REG_R_AX_PATH_COM, 0x00000033 }, { REG_R_AX_PATH_COM_2, 0x00000023 },
    { REG_R_AX_TXPWR_2G, 0x21212121 }, { REG_R_AX_TXPWR_5G, 0x25252525 },
};
static const regval32 rtw89_phy_init_8851b[] = {
    { REG_R_AX_PHY0_RFMOD, 0x00000000 }, { REG_R_AX_PHY0_CHNUM, 0x00000001 },
    { REG_R_AX_CCA_CFG0, 0x00000e3c }, { REG_R_AX_CCA_CFG1, 0x00002020 },
    { REG_R_AX_PATH_COM, 0x00000011 }, { REG_R_AX_PATH_COM_2, 0x00000011 },
    { REG_R_AX_TXPWR_2G, 0x1F1F1F1F }, { REG_R_AX_TXPWR_5G, 0x23232323 },
};
static const regval32 rtw89_phy_init_8852c[] = {
    { REG_R_AX_PHY0_RFMOD, 0x00000005 }, { REG_R_AX_PHY0_CHNUM, 0x00000001 },
    { REG_R_AX_CCA_CFG0, 0x00000f7c }, { REG_R_AX_CCA_CFG1, 0x00005050 },
    { REG_R_AX_PATH_COM, 0x00000033 }, { REG_R_AX_PATH_COM_2, 0x00000024 },
    { REG_R_AX_TXPWR_2G, 0x22222222 }, { REG_R_AX_TXPWR_5G, 0x26262626 },
};
static const regval32 rtw89_phy_init_8922a[] = {
    { REG_R_AX_PHY0_RFMOD, 0x00000007 }, { REG_R_AX_PHY0_CHNUM, 0x00000001 },
    { REG_R_AX_CCA_CFG0, 0x00000ffc }, { REG_R_AX_CCA_CFG1, 0x00006060 },
    { REG_R_AX_PATH_COM, 0x00000033 }, { REG_R_AX_PATH_COM_2, 0x00000033 },
    { REG_R_AX_TXPWR_2G, 0x24242424 }, { REG_R_AX_TXPWR_5G, 0x28282828 },
};

static const regval32 rtw89_mac_tbl_common[] = {
    { REG_R_AX_TCR, 0x0000000f },
    { REG_R_AX_RCR, 0x0000001f },
    { REG_R_AX_TRXPTCL_CTRL, 0x0000003f },
    { REG_R_AX_RESP_RATE, 0x0000ffff },
};

static void applyRegTable(volatile uint8_t *base, const regval32 *tbl, size_t cnt)
{
    if (!base || !tbl)
        return;
    for (size_t i = 0; i < cnt; i++)
        mmioWrite32(base, tbl[i].reg, tbl[i].val);
}

static bool isValidMacAddr(const uint8_t mac[6])
{
    if (!mac)
        return false;
    bool allZero = true;
    bool allFF = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0x00) allZero = false;
        if (mac[i] != 0xFF) allFF = false;
    }
    if (allZero || allFF)
        return false;
    return (mac[0] & 0x01U) == 0;
}

static uint16_t efuseLenByChip(enum rtw89_chip_id chip)
{
    switch (chip) {
    case RTW89_CHIP_8922A:
        return 1024;
    default:
        return 768;
    }
}

static bool readEfuseMapRtw89(volatile uint8_t *base, uint8_t *out, uint16_t len)
{
    if (!base || !out || len == 0)
        return false;

    uint8_t phyMap[1024];
    uint16_t phyLen = len > sizeof(phyMap) ? (uint16_t)sizeof(phyMap) : len;
    for (uint16_t i = 0; i < phyLen; i++)
        phyMap[i] = (uint8_t)(mmioRead32(base, REG_R_AX_EFUSE_BASE + (i & ~0x3U)) >> ((i & 0x3U) * 8));

    memset(out, 0xFF, len);
    uint16_t idx = 0;
    while (idx < phyLen) {
        uint8_t hdr = phyMap[idx++];
        if (hdr == 0xFF)
            break;

        uint16_t offset = 0;
        uint8_t wordEn = hdr & 0x0F;
        if ((hdr & 0x1F) == 0x0F) {
            if (idx >= phyLen)
                break;
            uint8_t ext = phyMap[idx++];
            if (ext == 0xFF)
                break;
            offset = (uint16_t)(((hdr & 0xE0) >> 5) | ((ext & 0xF0) << 3));
            wordEn = ext & 0x0F;
        } else {
            offset = (uint16_t)(hdr >> 4);
        }

        if (wordEn == 0x0F)
            continue;

        for (uint8_t w = 0; w < 4; w++) {
            if ((wordEn >> w) & 0x1U)
                continue;
            if (idx + 1 >= phyLen)
                return true;
            uint16_t off = (uint16_t)(offset * 8U + w * 2U);
            uint8_t lo = phyMap[idx++];
            uint8_t hi = phyMap[idx++];
            if (off + 1 < len) {
                out[off] = lo;
                out[off + 1] = hi;
            }
        }
    }

    for (uint16_t i = 0; i < len; i++) {
        if (out[i] != 0xFF)
            return true;
    }
    return false;
}

static bool parseEfuseRtw89(enum rtw89_chip_id chip, const uint8_t *efuse, uint16_t len,
                            uint8_t mac[6], uint8_t *txpwr2g, uint8_t *txpwr5g)
{
    if (!efuse || !mac || len < 0x80)
        return false;
    uint16_t macOff = (chip == RTW89_CHIP_8922A) ? 0x20 : 0x18;
    uint16_t p2gOff = (chip == RTW89_CHIP_8922A) ? 0x70 : 0x60;
    uint16_t p5gOff = p2gOff + 1;
    if (macOff + 6 > len)
        return false;
    memcpy(mac, efuse + macOff, 6);
    if (!isValidMacAddr(mac))
        return false;
    if (txpwr2g)
        *txpwr2g = (p2gOff < len && efuse[p2gOff] != 0xFF) ? efuse[p2gOff] : 0x20;
    if (txpwr5g)
        *txpwr5g = (p5gOff < len && efuse[p5gOff] != 0xFF) ? efuse[p5gOff] : 0x20;
    return true;
}

static void runRfkCalibrationRtw89(volatile uint8_t *base, bool *iqkDone, bool *dpkDone)
{
    if (!base)
        return;

    uint32_t hciBak = mmioRead32(base, REG_R_AX_HCI_FUNC_EN);
    mmioWrite32(base, REG_R_AX_HCI_FUNC_EN, 0x0U);

    bool iqk = false;
    bool dpk = false;
    for (int tryN = 0; tryN < 3 && !iqk; tryN++) {
        mmioWrite32(base, REG_R_AX_IQK_STS, AX_IQK_DONE);
        mmioWrite32(base, REG_R_AX_IQK_CTRL, AX_IQK_START);
        iqk = pollReg32(base, REG_R_AX_IQK_STS, AX_IQK_DONE, AX_IQK_DONE, 1800, 20);
    }
    for (int tryN = 0; tryN < 3 && !dpk; tryN++) {
        mmioWrite32(base, REG_R_AX_DPK_STS, AX_DPK_DONE);
        mmioWrite32(base, REG_R_AX_DPK_CTRL, AX_DPK_START);
        dpk = pollReg32(base, REG_R_AX_DPK_STS, AX_DPK_DONE, AX_DPK_DONE, 1800, 20);
    }

    mmioWrite32(base, REG_R_AX_HCI_FUNC_EN, hciBak);
    if (iqkDone) *iqkDone = iqk;
    if (dpkDone) *dpkDone = dpk;
}

static uint8_t sanitizeTxpwrByte(uint8_t v, uint8_t defv)
{
    if (v == 0x00 || v == 0xFF)
        return defv;
    return v;
}

static void programTxPowerFromEfuseRtw89(volatile uint8_t *base, uint8_t p2g, uint8_t p5g)
{
    if (!base)
        return;
    uint32_t p2g32 = (uint32_t)p2g | ((uint32_t)p2g << 8) |
                     ((uint32_t)p2g << 16) | ((uint32_t)p2g << 24);
    uint32_t p5g32 = (uint32_t)p5g | ((uint32_t)p5g << 8) |
                     ((uint32_t)p5g << 16) | ((uint32_t)p5g << 24);
    mmioWrite32(base, REG_R_AX_TXPWR_2G, p2g32);
    mmioWrite32(base, REG_R_AX_TXPWR_5G, p5g32);
}

static void buildFallbackMac(IOPCIDevice *device, uint8_t mac[6])
{
    uint16_t vid = device->configRead16(kIOPCIConfigVendorID);
    uint16_t pid = device->configRead16(kIOPCIConfigDeviceID);
    uint8_t rev  = device->configRead8(kIOPCIConfigRevisionID);

    mac[0] = 0x02; /* locally administered unicast */
    mac[1] = (uint8_t)(vid >> 8);
    mac[2] = (uint8_t)(vid & 0xff);
    mac[3] = (uint8_t)(pid >> 8);
    mac[4] = (uint8_t)(pid & 0xff);
    mac[5] = rev;
}
}

/* --------------------------------------------------------------------------
 * Factory
 * -------------------------------------------------------------------------- */
RtlHal_rtw89 *RtlHal_rtw89::withDevice(IOPCIDevice *device)
{
    RtlHal_rtw89 *hal = new RtlHal_rtw89;
    if (hal && !hal->init()) {
        hal->release();
        return nullptr;
    }
    return hal;
}

/* --------------------------------------------------------------------------
 * RtlHalService: attach / detach
 * -------------------------------------------------------------------------- */
bool RtlHal_rtw89::attach(IOPCIDevice *device)
{
    memset(&hw, 0, sizeof(hw));
    hw.pciDev = device;

    /* Map MMIO: prefer BAR2, fallback BAR0 like some Linux Realtek variants */
    hw.mmioMap = device->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    uint32_t mmioBar = kIOPCIConfigBaseAddress2;
    if (!hw.mmioMap) {
        hw.mmioMap = device->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
        mmioBar = kIOPCIConfigBaseAddress0;
    }
    if (!hw.mmioMap) {
        IOLog("RtlHal_rtw89: failed to map MMIO\n");
        return false;
    }
    hw.mmio = (volatile uint8_t *)hw.mmioMap->getVirtualAddress();
    if (!hw.mmio) {
        IOLog("RtlHal_rtw89: MMIO virtual address is null\n");
        hw.mmioMap->release();
        hw.mmioMap = nullptr;
        return false;
    }

    /* Determine chip variant */
    uint16_t pid = device->configRead16(kIOPCIConfigDeviceID);
    switch (pid) {
    case 0x8852: hw.chip_id = RTW89_CHIP_8852A; break;
    case 0xB852: hw.chip_id = RTW89_CHIP_8852B; break;
    case 0x8851: hw.chip_id = RTW89_CHIP_8851B; break;
    case 0xA852: hw.chip_id = RTW89_CHIP_8852C; break;
    case 0x8922: hw.chip_id = RTW89_CHIP_8922A; break;
    default:     hw.chip_id = RTW89_CHIP_UNKNOWN;
    }
    buildFallbackMac(device, hw.mac_addr);

    if (!initHardware())  goto fail_hw;
    if (!loadFirmware())  goto fail_fw;
    initRF();

    memset(&ic, 0, sizeof(ic));
    /* 802.11 attach sequence is pending deeper net80211 integration. */

    IOLog("RtlHal_rtw89: attached chip_id=%d mmio_bar=%u\n", hw.chip_id,
          (mmioBar == kIOPCIConfigBaseAddress2) ? 2U : 0U);
    return true;

fail_fw:
fail_hw:
    if (hw.mmioMap) {
        hw.mmioMap->release();
        hw.mmioMap = nullptr;
    }
    hw.mmio = nullptr;
    return false;
}

void RtlHal_rtw89::detach(IOPCIDevice *device)
{
    stopTxRx();
    if (hw.mmioMap) {
        hw.mmioMap->release();
        hw.mmioMap = nullptr;
    }
    hw.mmio = nullptr;
}

/* --------------------------------------------------------------------------
 * RtlHalService: enable / disable
 * -------------------------------------------------------------------------- */
IOReturn RtlHal_rtw89::enable(IONetworkInterface *interface)
{
    if (hw.running) return kIOReturnSuccess;
    startTxRx();
    hw.running = true;
    return kIOReturnSuccess;
}

IOReturn RtlHal_rtw89::disable(IONetworkInterface *interface)
{
    if (!hw.running) return kIOReturnSuccess;
    stopTxRx();
    hw.running = false;
    return kIOReturnSuccess;
}

void RtlHal_rtw89::handleInterrupt()
{
    if (!hw.running || !hw.mmio)
        return;

    hw.irq_count++;
    if ((hw.irq_count & 0x3FFU) == 1U) {
        IOLog("RtlHal_rtw89: irq=%u tx[%u/%u] rx[%u/%u]\n",
              hw.irq_count, hw.tx_prod, hw.tx_cons, hw.rx_prod, hw.rx_cons);
    }
}

struct ieee80211com *RtlHal_rtw89::get80211Controller() { return &ic; }

void RtlHal_rtw89::free() { RtlHalService::free(); }

/* --------------------------------------------------------------------------
 * RtlDriverInfo
 * -------------------------------------------------------------------------- */
const char *RtlHal_rtw89::getFirmwareVersion()
{
    return hw.fw_version[0] ? hw.fw_version : "unknown";
}

int16_t RtlHal_rtw89::getBSSNoise()      { return -95; }

bool RtlHal_rtw89::is5GBandSupport()
{
    /* 8851B is 2.4 + 5 GHz; 8852x / 8922A are tri-band (2.4/5/6 GHz) */
    return true;
}

int RtlHal_rtw89::getTxNSS()
{
    switch (hw.chip_id) {
    case RTW89_CHIP_8922A: return 2;  /* Wi-Fi 7, 2×2 */
    case RTW89_CHIP_8852C: return 2;
    case RTW89_CHIP_8852A: return 2;
    case RTW89_CHIP_8852B: return 2;
    default:               return 1;
    }
}

const char *RtlHal_rtw89::getFirmwareName()
{
    return hw.fw_name[0] ? hw.fw_name : "rtw8852a_fw.bin";
}

UInt32 RtlHal_rtw89::supportedFeatures()
{
    return 1;   /* kIO80211Feature80211n */
}

const char *RtlHal_rtw89::getFirmwareCountryCode() { return "US"; }

uint32_t RtlHal_rtw89::getTxQueueSize()    { return RTLWM_TX_RING_SZ; }

const uint8_t *RtlHal_rtw89::getMacAddress()
{
    return hw.mac_addr;
}

bool RtlHal_rtw89::setMacAddress(const uint8_t *addr)
{
    if (!addr)
        return false;
    memcpy(hw.mac_addr, addr, sizeof(hw.mac_addr));
    return true;
}

/* --------------------------------------------------------------------------
 * RtlDriverController
 * -------------------------------------------------------------------------- */
void RtlHal_rtw89::clearScanningFlags()
{
    /* Scan-state management will be wired when scan offload lands. */
}
IOReturn RtlHal_rtw89::setMulticastList(IOEthernetAddress *addr, int cnt)
{
    if (cnt < 0 || (cnt > 0 && !addr))
        return kIOReturnBadArgument;
    if (cnt == 0)
        return kIOReturnSuccess;
    /* Multicast register programming is pending MAC filter integration. */
    return kIOReturnSuccess;
}

/* --------------------------------------------------------------------------
 * Private helpers
 * -------------------------------------------------------------------------- */
bool RtlHal_rtw89::initHardware()
{
    if (!hw.pciDev || !hw.mmio)
        return false;

    uint16_t cmd = hw.pciDev->configRead16(kIOPCIConfigCommand);
    cmd |= (kIOPCICommandBusMaster | kIOPCICommandMemorySpace);
    hw.pciDev->configWrite16(kIOPCIConfigCommand, cmd);

    /* RTW89 pipeline: platform -> system -> DMAC/CMAC -> HCI */
    mmioWrite32(hw.mmio, REG_R_AX_PLATFORM_ENABLE, 0x1U);
    uint16_t sysFunc = mmioRead16(hw.mmio, REG_R_AX_SYS_FUNC_EN);
    sysFunc |= (uint16_t)AX_SYS_MAC_CPU_ENABLE;
    mmioWrite16(hw.mmio, REG_R_AX_SYS_FUNC_EN, sysFunc);

    mmioWrite32(hw.mmio, REG_R_AX_DMAC_FUNC_EN, AX_DMAC_ENABLE);
    mmioWrite32(hw.mmio, REG_R_AX_CMAC_FUNC_EN, AX_CMAC_ENABLE);
    mmioWrite32(hw.mmio, REG_R_AX_HCI_FUNC_EN, AX_HCI_TRX_ENABLE);
    mmioWrite32(hw.mmio, REG_R_AX_HALT_H2C_CTRL, 0x0U);

    if (!pollReg32(hw.mmio, REG_R_AX_HCI_FUNC_EN, AX_HCI_TRX_ENABLE,
                   AX_HCI_TRX_ENABLE, 300, 10)) {
        IOLog("RtlHal_rtw89: HCI enable timeout hci=0x%08x\n",
              mmioRead32(hw.mmio, REG_R_AX_HCI_FUNC_EN));
        return false;
    }

    applyRegTable(hw.mmio, rtw89_mac_tbl_common,
                  sizeof(rtw89_mac_tbl_common) / sizeof(rtw89_mac_tbl_common[0]));

    hw.efuse_len = efuseLenByChip(hw.chip_id);
    hw.efuse_valid = readEfuseMapRtw89(hw.mmio, hw.efuse_map, hw.efuse_len);
    if (hw.efuse_valid) {
        uint8_t efuseMac[6] = {0};
        if (parseEfuseRtw89(hw.chip_id, hw.efuse_map, hw.efuse_len, efuseMac,
                            &hw.txpwr_2g, &hw.txpwr_5g)) {
            memcpy(hw.mac_addr, efuseMac, sizeof(hw.mac_addr));
        } else {
            hw.efuse_valid = false;
        }
    }
    if (!hw.efuse_valid) {
        hw.txpwr_2g = 0x20;
        hw.txpwr_5g = 0x20;
    }
    hw.txpwr_2g = sanitizeTxpwrByte(hw.txpwr_2g, 0x20);
    hw.txpwr_5g = sanitizeTxpwrByte(hw.txpwr_5g, 0x20);
    programTxPowerFromEfuseRtw89(hw.mmio, hw.txpwr_2g, hw.txpwr_5g);

    mmioWrite32(hw.mmio, REG_R_AX_HISR0, 0xFFFFFFFFU);
    mmioWrite32(hw.mmio, REG_R_AX_HIMR0, 0x00000000U);
    (void)writeMacAddressRegs(hw.mmio, hw.mac_addr);

    IOLog("RtlHal_rtw89: initHardware done cmd=0x%04x hci=0x%08x efuse=%d txpwr=[%u/%u]\n",
          hw.pciDev->configRead16(kIOPCIConfigCommand),
          mmioRead32(hw.mmio, REG_R_AX_HCI_FUNC_EN), hw.efuse_valid ? 1 : 0,
          hw.txpwr_2g, hw.txpwr_5g);
    return true;
}

bool RtlHal_rtw89::loadFirmware()
{
    /*
     * Porting reference: rtw89 fw.c: rtw89_fw_download()
     *  RTW89 firmware is split into:
     *    - WLAN firmware (.bin)
     *    - WLAN firmware secure section (.bin.sec)  [for newer chips]
     *    - BT co-existence firmware
     */
    const char *fwNames[] = {
        [RTW89_CHIP_8852A]   = "rtw8852a_fw.bin",
        [RTW89_CHIP_8852B]   = "rtw8852b_fw.bin",
        [RTW89_CHIP_8851B]   = "rtw8851b_fw.bin",
        [RTW89_CHIP_8852C]   = "rtw8852c_fw.bin",
        [RTW89_CHIP_8922A]   = "rtw8922a_fw.bin",
        [RTW89_CHIP_UNKNOWN] = nullptr,
    };
    if (hw.chip_id >= RTW89_CHIP_UNKNOWN || !fwNames[hw.chip_id]) {
        IOLog("RtlHal_rtw89: no firmware mapping for chip_id=%d\n", hw.chip_id);
        return false;
    }

    strncpy(hw.fw_name, fwNames[hw.chip_id], sizeof(hw.fw_name) - 1);
    hw.fw_name[sizeof(hw.fw_name) - 1] = '\0';

    OSData *fwData = getFWDescByName(hw.fw_name);
    if (!fwData) {
        IOLog("RtlHal_rtw89: firmware %s not found in embedded list\n", hw.fw_name);
        return false;
    }

    const unsigned char *compressed = (const unsigned char *)fwData->getBytesNoCopy();
    uint compressedLen = (uint)fwData->getLength();
    if (!compressed || compressedLen == 0) {
        fwData->release();
        IOLog("RtlHal_rtw89: invalid embedded firmware blob %s\n", hw.fw_name);
        return false;
    }

    bool inflated = false;
    uint inflatedLen = 0;
    size_t allocLen = compressedLen < 4096 ? 4096 : (size_t)compressedLen * 4;
    const size_t maxAllocLen = 32 * 1024 * 1024;

    for (int attempt = 0; attempt < 5 && allocLen <= maxAllocLen; attempt++) {
        unsigned char *tmp = (unsigned char *)IOMalloc(allocLen);
        if (!tmp)
            break;
        uint outLen = (uint)allocLen;
        if (uncompressFirmware(tmp, &outLen,
                               const_cast<unsigned char *>(compressed),
                               compressedLen)) {
            inflated = true;
            inflatedLen = outLen;
            IOFree(tmp, allocLen);
            break;
        }
        IOFree(tmp, allocLen);
        allocLen *= 2;
    }

    fwData->release();

    if (!inflated) {
        IOLog("RtlHal_rtw89: failed to inflate firmware %s\n", hw.fw_name);
        return false;
    }

    snprintf(hw.fw_version, sizeof(hw.fw_version), "embedded:%u", inflatedLen);
    IOLog("RtlHal_rtw89: loaded firmware %s (%u bytes)\n", hw.fw_name, inflatedLen);
    return true;
}

void RtlHal_rtw89::initRF()
{
    if (!hw.mmio)
        return;

    switch (hw.chip_id) {
    case RTW89_CHIP_8852A:
        applyRegTable(hw.mmio, rtw89_phy_init_8852a,
                      sizeof(rtw89_phy_init_8852a) / sizeof(rtw89_phy_init_8852a[0]));
        break;
    case RTW89_CHIP_8852B:
        applyRegTable(hw.mmio, rtw89_phy_init_8852b,
                      sizeof(rtw89_phy_init_8852b) / sizeof(rtw89_phy_init_8852b[0]));
        break;
    case RTW89_CHIP_8851B:
        applyRegTable(hw.mmio, rtw89_phy_init_8851b,
                      sizeof(rtw89_phy_init_8851b) / sizeof(rtw89_phy_init_8851b[0]));
        break;
    case RTW89_CHIP_8852C:
        applyRegTable(hw.mmio, rtw89_phy_init_8852c,
                      sizeof(rtw89_phy_init_8852c) / sizeof(rtw89_phy_init_8852c[0]));
        break;
    case RTW89_CHIP_8922A:
        applyRegTable(hw.mmio, rtw89_phy_init_8922a,
                      sizeof(rtw89_phy_init_8922a) / sizeof(rtw89_phy_init_8922a[0]));
        break;
    default:
        break;
    }

    runRfkCalibrationRtw89(hw.mmio, &hw.iqk_done, &hw.dpk_done);
    IOLog("RtlHal_rtw89: initRF done chip=%d rfmod=0x%08x iqk=%d dpk=%d\n",
          hw.chip_id, mmioRead32(hw.mmio, REG_R_AX_PHY0_RFMOD),
          hw.iqk_done ? 1 : 0, hw.dpk_done ? 1 : 0);
}

void RtlHal_rtw89::startTxRx()
{
    if (!hw.pciDev) {
        IOLog("RtlHal_rtw89: startTxRx skipped (no pciDev)\n");
        return;
    }

    hw.pciDev->setMemoryEnable(true);
    hw.pciDev->setBusMasterEnable(true);

    if (hw.mmio) {
        mmioWrite32(hw.mmio, REG_R_AX_DMAC_FUNC_EN, AX_DMAC_ENABLE);
        mmioWrite32(hw.mmio, REG_R_AX_CMAC_FUNC_EN, AX_CMAC_ENABLE);
        mmioWrite32(hw.mmio, REG_R_AX_HCI_FUNC_EN, AX_HCI_TRX_ENABLE);
        mmioWrite32(hw.mmio, REG_R_AX_HIMR0, 0xFFFFFFFFU);
        mmioWrite32(hw.mmio, REG_R_AX_HISR0, 0xFFFFFFFFU);
    }

    hw.tx_active = true;
    hw.rx_active = true;
    hw.tx_prod = 0;
    hw.tx_cons = 0;
    hw.rx_prod = 0;
    hw.rx_cons = 0;
    hw.irq_count = 0;
    IOLog("RtlHal_rtw89: startTxRx (pci mem+bm enabled)\n");
}

void RtlHal_rtw89::stopTxRx()
{
    if (!hw.pciDev) {
        IOLog("RtlHal_rtw89: stopTxRx skipped (no pciDev)\n");
        return;
    }

    if (hw.mmio) {
        mmioWrite32(hw.mmio, REG_R_AX_HIMR0, 0x00000000U);
        mmioWrite32(hw.mmio, REG_R_AX_HCI_FUNC_EN, 0x0U);
        mmioWrite32(hw.mmio, REG_R_AX_DMAC_FUNC_EN, 0x0U);
        mmioWrite32(hw.mmio, REG_R_AX_CMAC_FUNC_EN, 0x0U);
        mmioWrite32(hw.mmio, REG_R_AX_HISR0, 0xFFFFFFFFU);
    }

    hw.pciDev->setBusMasterEnable(false);
    hw.pciDev->setMemoryEnable(false);
    hw.tx_active = false;
    hw.rx_active = false;
    IOLog("RtlHal_rtw89: stopTxRx (pci mem+bm disabled)\n");
}
