/*
 * RtlHal_rtw88.cpp – HAL implementation for RTW88 family
 *
 * Porting guide (Linux → macOS):
 *  • drivers/net/wireless/realtek/rtw88/main.c  → attach/detach/enable/disable
 *  • drivers/net/wireless/realtek/rtw88/pci.c   → PCI probe / MMIO setup
 *  • drivers/net/wireless/realtek/rtw88/mac.c   → initHardware / startTxRx
 *  • drivers/net/wireless/realtek/rtw88/fw.c    → loadFirmware
 *  • drivers/net/wireless/realtek/rtw88/phy.c   → initRF
 *
 * Linux kernel API shims live in rtl80211/compat.h and rtl80211/linux/types.h.
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "RtlHal_rtw88.hpp"
#include <FwData.h>
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <stdio.h>

OSDefineMetaClassAndStructors(RtlHal_rtw88, RtlHalService)

namespace {
struct regval32 {
    uint32_t reg;
    uint32_t val;
};

enum : uint32_t {
    REG_CR            = 0x0100,
    REG_HIMR0         = 0x00B0,
    REG_HISR0         = 0x00B4,
    REG_SYS_FUNC_EN   = 0x0002,
    REG_RF_CTRL       = 0x001F,
    REG_BB_CTRL       = 0x0800,
    REG_PHY_BW        = 0x0810,
    REG_PHY_CHAN      = 0x0814,
    REG_MAC_ADDR_0    = 0x0610,
    REG_MAC_ADDR_4    = 0x0614,
    REG_EFUSE_BASE    = 0x1D00,
    REG_IQK_CTRL      = 0x1B00,
    REG_IQK_STATUS    = 0x1B04,
    REG_DPK_CTRL      = 0x1B08,
    REG_DPK_STATUS    = 0x1B0C,
    REG_EDCA_BE       = 0x0500,
    REG_EDCA_BK       = 0x0504,
    REG_EDCA_VI       = 0x0508,
    REG_EDCA_VO       = 0x050C,
    REG_RRSR          = 0x0440,
    REG_AMPDU_MAX     = 0x04CA,
    REG_AGC_CFG       = 0x081C,
    REG_RX_PATH       = 0x0820,
    REG_TX_PATH       = 0x0824,
    REG_RF_GAIN       = 0x0828,
};

enum : uint32_t {
    CR_TXDMA_EN       = (1U << 4),
    CR_RXDMA_EN       = (1U << 5),
    CR_PROTOCOL_EN    = (1U << 6),
    CR_SCHEDULER_EN   = (1U << 7),
    CR_TRX_ENABLE     = CR_TXDMA_EN | CR_RXDMA_EN | CR_PROTOCOL_EN | CR_SCHEDULER_EN,

    SYS_FUNC_CPU_EN   = (1U << 10),
    SYS_FUNC_MAC_EN   = (1U << 11),
    SYS_FUNC_EN_ALL   = SYS_FUNC_CPU_EN | SYS_FUNC_MAC_EN,
    IQK_START         = (1U << 0),
    IQK_DONE          = (1U << 0),
    DPK_START         = (1U << 0),
    DPK_DONE          = (1U << 0),
};

static inline uint8_t mmioRead8(volatile uint8_t *base, uint32_t reg)
{
    return OSReadLittleInt8((const volatile void *)(base + reg), 0);
}

static inline uint16_t mmioRead16(volatile uint8_t *base, uint32_t reg)
{
    return OSReadLittleInt16((const volatile void *)(base + reg), 0);
}

static inline uint32_t mmioRead32(volatile uint8_t *base, uint32_t reg)
{
    return OSReadLittleInt32((const volatile void *)(base + reg), 0);
}

static inline void mmioWrite8(volatile uint8_t *base, uint32_t reg, uint8_t val)
{
    OSWriteLittleInt8((volatile void *)(base + reg), 0, val);
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
    mmioWrite32(base, REG_MAC_ADDR_0, lo);
    mmioWrite16(base, REG_MAC_ADDR_4, hi);
    return true;
}

static const regval32 rtw88_phy_init_8822b[] = {
    { REG_BB_CTRL, 0x00000031 }, { REG_PHY_BW, 0x00000000 }, { REG_PHY_CHAN, 0x00000001 },
    { REG_AGC_CFG, 0x00f0f0f0 }, { REG_RX_PATH, 0x00000003 }, { REG_TX_PATH, 0x00000003 },
    { REG_RF_GAIN, 0x00000022 }, { REG_EDCA_BE, 0x005EA42B }, { REG_EDCA_BK, 0x0000A44F },
    { REG_EDCA_VI, 0x005EA324 }, { REG_EDCA_VO, 0x002FA226 }, { REG_RRSR, 0x000FFFFF },
};
static const regval32 rtw88_phy_init_8822c[] = {
    { REG_BB_CTRL, 0x00000033 }, { REG_PHY_BW, 0x00000000 }, { REG_PHY_CHAN, 0x00000001 },
    { REG_AGC_CFG, 0x00e0e0e0 }, { REG_RX_PATH, 0x00000003 }, { REG_TX_PATH, 0x00000003 },
    { REG_RF_GAIN, 0x00000024 }, { REG_EDCA_BE, 0x005EA42B }, { REG_EDCA_BK, 0x0000A44F },
    { REG_EDCA_VI, 0x005EA324 }, { REG_EDCA_VO, 0x002FA226 }, { REG_RRSR, 0x000FFFFF },
};
static const regval32 rtw88_phy_init_8723d[] = {
    { REG_BB_CTRL, 0x00000011 }, { REG_PHY_BW, 0x00000000 }, { REG_PHY_CHAN, 0x00000001 },
    { REG_AGC_CFG, 0x00a0a0a0 }, { REG_RX_PATH, 0x00000001 }, { REG_TX_PATH, 0x00000001 },
    { REG_RF_GAIN, 0x0000001c }, { REG_EDCA_BE, 0x005EA42B }, { REG_EDCA_BK, 0x0000A44F },
    { REG_EDCA_VI, 0x005EA324 }, { REG_EDCA_VO, 0x002FA226 }, { REG_RRSR, 0x0003FFFF },
};
static const regval32 rtw88_phy_init_8821c[] = {
    { REG_BB_CTRL, 0x00000021 }, { REG_PHY_BW, 0x00000000 }, { REG_PHY_CHAN, 0x00000001 },
    { REG_AGC_CFG, 0x00c0c0c0 }, { REG_RX_PATH, 0x00000001 }, { REG_TX_PATH, 0x00000001 },
    { REG_RF_GAIN, 0x00000020 }, { REG_EDCA_BE, 0x005EA42B }, { REG_EDCA_BK, 0x0000A44F },
    { REG_EDCA_VI, 0x005EA324 }, { REG_EDCA_VO, 0x002FA226 }, { REG_RRSR, 0x0007FFFF },
};

static const regval32 rtw88_mac_tbl_common[] = {
    { REG_EDCA_BE, 0x005EA42B }, { REG_EDCA_BK, 0x0000A44F },
    { REG_EDCA_VI, 0x005EA324 }, { REG_EDCA_VO, 0x002FA226 },
    { REG_RRSR, 0x000FFFFF }, { REG_AMPDU_MAX, 0x0000001B },
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

static uint16_t efuseLenByChip(enum rtw88_chip_id chip)
{
    switch (chip) {
    case RTW88_CHIP_8822B:
    case RTW88_CHIP_8822C:
        return 1024;
    case RTW88_CHIP_8723D:
    case RTW88_CHIP_8821C:
        return 512;
    default:
        return 512;
    }
}

static bool readEfuseMapRtw88(volatile uint8_t *base, uint8_t *out, uint16_t len)
{
    if (!base || !out || len == 0)
        return false;

    uint16_t ffCnt = 0;
    for (uint16_t i = 0; i < len; i++) {
        out[i] = mmioRead8(base, REG_EFUSE_BASE + i);
        if (out[i] == 0xFF)
            ffCnt++;
    }
    return ffCnt < len;
}

static bool parseEfuseRtw88(enum rtw88_chip_id chip, const uint8_t *efuse,
                            uint16_t len, uint8_t mac[6], uint8_t *txpwrCck,
                            uint8_t *txpwrOfdm)
{
    if (!efuse || !mac || len < 0x40)
        return false;

    uint16_t macOff = (chip == RTW88_CHIP_8723D) ? 0x11 : 0x10;
    if (macOff + 6 > len)
        return false;
    memcpy(mac, efuse + macOff, 6);

    if (txpwrCck)
        *txpwrCck = (0x5A < len) ? efuse[0x5A] : 0x20;
    if (txpwrOfdm)
        *txpwrOfdm = (0x5B < len) ? efuse[0x5B] : 0x20;
    return true;
}

static void runRfkCalibrationRtw88(volatile uint8_t *base, bool *iqkDone, bool *dpkDone)
{
    if (!base)
        return;
    mmioWrite32(base, REG_IQK_CTRL, IQK_START);
    bool iqk = pollReg32(base, REG_IQK_STATUS, IQK_DONE, IQK_DONE, 1000, 20);
    mmioWrite32(base, REG_DPK_CTRL, DPK_START);
    bool dpk = pollReg32(base, REG_DPK_STATUS, DPK_DONE, DPK_DONE, 1000, 20);
    if (iqkDone) *iqkDone = iqk;
    if (dpkDone) *dpkDone = dpk;
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
RtlHal_rtw88 *RtlHal_rtw88::withDevice(IOPCIDevice *device)
{
    RtlHal_rtw88 *hal = new RtlHal_rtw88;
    if (hal && !hal->init()) {
        hal->release();
        return nullptr;
    }
    return hal;
}

/* --------------------------------------------------------------------------
 * RtlHalService: attach / detach
 * -------------------------------------------------------------------------- */
bool RtlHal_rtw88::attach(IOPCIDevice *device)
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
        IOLog("RtlHal_rtw88: failed to map MMIO\n");
        return false;
    }
    hw.mmio = (volatile uint8_t *)hw.mmioMap->getVirtualAddress();
    if (!hw.mmio) {
        IOLog("RtlHal_rtw88: MMIO virtual address is null\n");
        hw.mmioMap->release();
        hw.mmioMap = nullptr;
        return false;
    }

    /* Determine chip variant */
    uint16_t pid = device->configRead16(kIOPCIConfigDeviceID);
    switch (pid) {
    case 0xB822: hw.chip_id = RTW88_CHIP_8822B; break;
    case 0xC822: hw.chip_id = RTW88_CHIP_8822C; break;
    case 0xD723: hw.chip_id = RTW88_CHIP_8723D; break;
    case 0xC821: /* fall through */
    case 0xC82F: hw.chip_id = RTW88_CHIP_8821C; break;
    default:     hw.chip_id = RTW88_CHIP_UNKNOWN;
    }
    buildFallbackMac(device, hw.mac_addr);

    if (!initHardware())
        goto fail_hw;

    if (!loadFirmware())
        goto fail_fw;

    initRF();

    /* Initialise the 802.11 stack */
    memset(&ic, 0, sizeof(ic));
    /* 802.11 attach sequence is pending deeper net80211 integration. */

    IOLog("RtlHal_rtw88: attached chip_id=%d mmio_bar=%u\n", hw.chip_id,
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

void RtlHal_rtw88::detach(IOPCIDevice *device)
{
    stopTxRx();
    /* 802.11 detach sequence is pending deeper net80211 integration. */
    if (hw.mmioMap) {
        hw.mmioMap->release();
        hw.mmioMap = nullptr;
    }
    hw.mmio = nullptr;
}

/* --------------------------------------------------------------------------
 * RtlHalService: enable / disable
 * -------------------------------------------------------------------------- */
IOReturn RtlHal_rtw88::enable(IONetworkInterface *interface)
{
    if (hw.running)
        return kIOReturnSuccess;
    startTxRx();
    hw.running = true;
    return kIOReturnSuccess;
}

IOReturn RtlHal_rtw88::disable(IONetworkInterface *interface)
{
    if (!hw.running)
        return kIOReturnSuccess;
    stopTxRx();
    hw.running = false;
    return kIOReturnSuccess;
}

void RtlHal_rtw88::handleInterrupt()
{
    if (!hw.running || !hw.mmio)
        return;

    hw.irq_count++;
    if ((hw.irq_count & 0x3FFU) == 1U) {
        IOLog("RtlHal_rtw88: irq=%u tx[%u/%u] rx[%u/%u]\n",
              hw.irq_count, hw.tx_prod, hw.tx_cons, hw.rx_prod, hw.rx_cons);
    }
}

struct ieee80211com *RtlHal_rtw88::get80211Controller()
{
    return &ic;
}

void RtlHal_rtw88::free()
{
    RtlHalService::free();
}

/* --------------------------------------------------------------------------
 * RtlDriverInfo
 * -------------------------------------------------------------------------- */
const char *RtlHal_rtw88::getFirmwareVersion()
{
    return hw.fw_version[0] ? hw.fw_version : "unknown";
}

int16_t RtlHal_rtw88::getBSSNoise()
{
    /* Default fallback until noise-floor register path is integrated. */
    return -95;
}

bool RtlHal_rtw88::is5GBandSupport()
{
    /* RTL8723D is 2.4 GHz only */
    return hw.chip_id != RTW88_CHIP_8723D;
}

int RtlHal_rtw88::getTxNSS()
{
    /* 8822x = 2-stream; 8821C/8723D = 1-stream */
    return (hw.chip_id == RTW88_CHIP_8822B ||
            hw.chip_id == RTW88_CHIP_8822C) ? 2 : 1;
}

const char *RtlHal_rtw88::getFirmwareName()
{
    return hw.fw_name[0] ? hw.fw_name : "rtl8822c_fw.bin";
}

UInt32 RtlHal_rtw88::supportedFeatures()
{
    return 1;   /* kIO80211Feature80211n */
}

const char *RtlHal_rtw88::getFirmwareCountryCode()
{
    return "US";
}

uint32_t RtlHal_rtw88::getTxQueueSize()
{
    return RTLWM_TX_RING_SZ;
}

const uint8_t *RtlHal_rtw88::getMacAddress()
{
    return hw.mac_addr;
}

bool RtlHal_rtw88::setMacAddress(const uint8_t *addr)
{
    if (!addr)
        return false;
    memcpy(hw.mac_addr, addr, sizeof(hw.mac_addr));
    return true;
}

/* --------------------------------------------------------------------------
 * RtlDriverController
 * -------------------------------------------------------------------------- */
void RtlHal_rtw88::clearScanningFlags()
{
    /* Scan-state management will be wired when scan offload lands. */
}

IOReturn RtlHal_rtw88::setMulticastList(IOEthernetAddress *addr, int cnt)
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
bool RtlHal_rtw88::initHardware()
{
    if (!hw.pciDev || !hw.mmio)
        return false;

    /* PCI command enable (Linux rtw_pci_probe equivalent bootstrap). */
    uint16_t cmd = hw.pciDev->configRead16(kIOPCIConfigCommand);
    cmd |= (kIOPCICommandBusMaster | kIOPCICommandMemorySpace);
    hw.pciDev->configWrite16(kIOPCIConfigCommand, cmd);

    /* Bring MAC/CPU blocks up before touching CR. */
    uint16_t sysFunc = mmioRead16(hw.mmio, REG_SYS_FUNC_EN);
    sysFunc |= (uint16_t)SYS_FUNC_EN_ALL;
    mmioWrite16(hw.mmio, REG_SYS_FUNC_EN, sysFunc);

    /* Stop TRX first, clear stale status, then re-enable core MAC pipeline. */
    uint8_t cr = mmioRead8(hw.mmio, REG_CR);
    cr &= (uint8_t)~CR_TRX_ENABLE;
    mmioWrite8(hw.mmio, REG_CR, cr);
    mmioWrite32(hw.mmio, REG_HISR0, 0xFFFFFFFFU);
    mmioWrite32(hw.mmio, REG_HIMR0, 0x00000000U);

    cr |= (uint8_t)CR_TRX_ENABLE;
    mmioWrite8(hw.mmio, REG_CR, cr);
    if (!pollReg32(hw.mmio, REG_CR, CR_TRX_ENABLE, CR_TRX_ENABLE, 200, 10)) {
        IOLog("RtlHal_rtw88: MAC enable timeout CR=0x%08x\n", mmioRead32(hw.mmio, REG_CR));
        return false;
    }

    applyRegTable(hw.mmio, rtw88_mac_tbl_common,
                  sizeof(rtw88_mac_tbl_common) / sizeof(rtw88_mac_tbl_common[0]));

    hw.efuse_len = efuseLenByChip(hw.chip_id);
    hw.efuse_valid = readEfuseMapRtw88(hw.mmio, hw.efuse_map, hw.efuse_len);
    if (hw.efuse_valid) {
        uint8_t efuseMac[6] = {0};
        if (parseEfuseRtw88(hw.chip_id, hw.efuse_map, hw.efuse_len, efuseMac,
                            &hw.txpwr_cck, &hw.txpwr_ofdm) &&
            isValidMacAddr(efuseMac)) {
            memcpy(hw.mac_addr, efuseMac, sizeof(hw.mac_addr));
        } else {
            hw.efuse_valid = false;
        }
    }
    if (!hw.efuse_valid) {
        hw.txpwr_cck = 0x20;
        hw.txpwr_ofdm = 0x20;
    }

    /* Program current MAC address into MACID0 registers. */
    (void)writeMacAddressRegs(hw.mmio, hw.mac_addr);

    IOLog("RtlHal_rtw88: initHardware done cmd=0x%04x cr=0x%02x efuse=%d txpwr=[%u/%u]\n",
          hw.pciDev->configRead16(kIOPCIConfigCommand), mmioRead8(hw.mmio, REG_CR),
          hw.efuse_valid ? 1 : 0, hw.txpwr_cck, hw.txpwr_ofdm);
    return true;
}

bool RtlHal_rtw88::loadFirmware()
{
    /*
     * Porting reference: rtw88 fw.c: rtw_fw_download()
     * Steps:
     *  1. Choose firmware file name based on chip_id
     *  2. Call getFWDescByName() from include/FwData.h
     *  3. Decompress with uncompressFirmware() if compressed
     *  4. Write firmware header + sections to device via MMIO
     *  5. Poll for firmware ready flag
     */
    const char *fwNames[] = {
        [RTW88_CHIP_8822B] = "rtw8822b_fw.bin",
        [RTW88_CHIP_8822C] = "rtw8822c_fw.bin",
        [RTW88_CHIP_8723D] = "rtw8723d_fw.bin",
        [RTW88_CHIP_8821C] = "rtw8821c_fw.bin",
        [RTW88_CHIP_UNKNOWN] = nullptr,
    };
    if (hw.chip_id >= RTW88_CHIP_UNKNOWN || !fwNames[hw.chip_id]) {
        IOLog("RtlHal_rtw88: no firmware mapping for chip_id=%d\n", hw.chip_id);
        return false;
    }

    strncpy(hw.fw_name, fwNames[hw.chip_id], sizeof(hw.fw_name) - 1);
    hw.fw_name[sizeof(hw.fw_name) - 1] = '\0';

    OSData *fwData = getFWDescByName(hw.fw_name);
    if (!fwData) {
        IOLog("RtlHal_rtw88: firmware %s not found in embedded list\n", hw.fw_name);
        return false;
    }

    const unsigned char *compressed = (const unsigned char *)fwData->getBytesNoCopy();
    uint compressedLen = (uint)fwData->getLength();
    if (!compressed || compressedLen == 0) {
        fwData->release();
        IOLog("RtlHal_rtw88: invalid embedded firmware blob %s\n", hw.fw_name);
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
        IOLog("RtlHal_rtw88: failed to inflate firmware %s\n", hw.fw_name);
        return false;
    }

    snprintf(hw.fw_version, sizeof(hw.fw_version), "embedded:%u", inflatedLen);
    IOLog("RtlHal_rtw88: loaded firmware %s (%u bytes)\n", hw.fw_name, inflatedLen);
    return true;
}

void RtlHal_rtw88::initRF()
{
    if (!hw.mmio)
        return;

    /* Minimal per-chip PHY bootstrap table (rtw88 phy init skeleton). */
    switch (hw.chip_id) {
    case RTW88_CHIP_8822B:
        applyRegTable(hw.mmio, rtw88_phy_init_8822b,
                      sizeof(rtw88_phy_init_8822b) / sizeof(rtw88_phy_init_8822b[0]));
        break;
    case RTW88_CHIP_8822C:
        applyRegTable(hw.mmio, rtw88_phy_init_8822c,
                      sizeof(rtw88_phy_init_8822c) / sizeof(rtw88_phy_init_8822c[0]));
        break;
    case RTW88_CHIP_8723D:
        applyRegTable(hw.mmio, rtw88_phy_init_8723d,
                      sizeof(rtw88_phy_init_8723d) / sizeof(rtw88_phy_init_8723d[0]));
        break;
    case RTW88_CHIP_8821C:
        applyRegTable(hw.mmio, rtw88_phy_init_8821c,
                      sizeof(rtw88_phy_init_8821c) / sizeof(rtw88_phy_init_8821c[0]));
        break;
    default:
        break;
    }

    /* RF gate on for later channel/calibration work. */
    mmioWrite8(hw.mmio, REG_RF_CTRL, (uint8_t)(mmioRead8(hw.mmio, REG_RF_CTRL) | 0x01U));
    runRfkCalibrationRtw88(hw.mmio, &hw.iqk_done, &hw.dpk_done);
    IOLog("RtlHal_rtw88: initRF done chip=%d rf_ctrl=0x%02x\n",
          hw.chip_id, mmioRead8(hw.mmio, REG_RF_CTRL));
}

void RtlHal_rtw88::startTxRx()
{
    /*
     * Porting reference: rtw88 mac.c: rtw_mac_start_tx_rx()
     * Steps:
     *  1. Enable DMA TX/RX paths
     *  2. Enable interrupt mask
     *  3. Start TX scheduler
     */
    if (!hw.pciDev) {
        IOLog("RtlHal_rtw88: startTxRx skipped (no pciDev)\n");
        return;
    }

    hw.pciDev->setMemoryEnable(true);
    hw.pciDev->setBusMasterEnable(true);

    if (hw.mmio) {
        uint8_t cr = mmioRead8(hw.mmio, REG_CR);
        mmioWrite8(hw.mmio, REG_CR, (uint8_t)(cr | CR_TRX_ENABLE));
        mmioWrite32(hw.mmio, REG_HIMR0, 0xFFFFFFFFU);
        mmioWrite32(hw.mmio, REG_HISR0, 0xFFFFFFFFU);
    }

    hw.tx_active = true;
    hw.rx_active = true;
    hw.tx_prod = 0;
    hw.tx_cons = 0;
    hw.rx_prod = 0;
    hw.rx_cons = 0;
    hw.irq_count = 0;
    IOLog("RtlHal_rtw88: startTxRx (pci mem+bm enabled)\n");
}

void RtlHal_rtw88::stopTxRx()
{
    /*
     * Porting reference: rtw88 mac.c: rtw_mac_stop_tx_rx()
     */
    if (!hw.pciDev) {
        IOLog("RtlHal_rtw88: stopTxRx skipped (no pciDev)\n");
        return;
    }

    if (hw.mmio) {
        mmioWrite32(hw.mmio, REG_HIMR0, 0x00000000U);
        uint8_t cr = mmioRead8(hw.mmio, REG_CR);
        mmioWrite8(hw.mmio, REG_CR, (uint8_t)(cr & (uint8_t)~CR_TRX_ENABLE));
        mmioWrite32(hw.mmio, REG_HISR0, 0xFFFFFFFFU);
    }

    hw.pciDev->setBusMasterEnable(false);
    hw.pciDev->setMemoryEnable(false);
    hw.tx_active = false;
    hw.rx_active = false;
    IOLog("RtlHal_rtw88: stopTxRx (pci mem+bm disabled)\n");
}
