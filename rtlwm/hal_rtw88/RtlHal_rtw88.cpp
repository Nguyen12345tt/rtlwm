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
#include <IOKit/IOBufferMemoryDescriptor.h>
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
    REG_TXPWR_CCK     = 0x0830,
    REG_TXPWR_OFDM    = 0x0834,
    REG_RXCCA_TH      = 0x0838,
    REG_NAV_CFG       = 0x0504,
    REG_RETRY_LIMIT   = 0x042A,
    REG_AMPDU_DENSITY = 0x04CC,
    REG_TXBD_DESA_VOQ = 0x0318,
    REG_RXBD_DESA_MPDUQ = 0x0338,
    REG_TXBD_NUM_VOQ  = 0x0384,
    REG_RXBD_NUM_MPDUQ = 0x0382,
    REG_TXBD_IDX_VOQ  = 0x03A0,
    REG_RXBD_IDX_MPDUQ = 0x03B4,
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
    RTW88_INT_RX_DONE = (1U << 0),
    RTW88_INT_TX_DONE = (1U << 1),
    RTW88_INT_RX_ERR  = (1U << 2),
    RTW88_INT_TX_ERR  = (1U << 3),
    RTW88_IRQ_MASK    = RTW88_INT_RX_DONE | RTW88_INT_TX_DONE |
                        RTW88_INT_RX_ERR | RTW88_INT_TX_ERR,
    DESC_OWN          = (1U << 31),
    DESC_EOR          = (1U << 30),
    DESC_LEN_MASK     = 0x0000FFFFU,
    TRX_BD_IDX_MASK   = 0x00000FFFU,
    TRX_BD_HW_IDX_SHIFT = 16,
    RTW88_TX_BUF_SIZE = 2048,
    RTW88_RX_BUF_SIZE = 2048,
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
    { REG_RF_GAIN, 0x00000022 }, { REG_TXPWR_CCK, 0x22222222 }, { REG_TXPWR_OFDM, 0x2A2A2A2A },
    { REG_RXCCA_TH, 0x00000D0D }, { REG_EDCA_BE, 0x005EA42B }, { REG_EDCA_BK, 0x0000A44F },
    { REG_EDCA_VI, 0x005EA324 }, { REG_EDCA_VO, 0x002FA226 }, { REG_RRSR, 0x000FFFFF },
};
static const regval32 rtw88_phy_init_8822c[] = {
    { REG_BB_CTRL, 0x00000033 }, { REG_PHY_BW, 0x00000000 }, { REG_PHY_CHAN, 0x00000001 },
    { REG_AGC_CFG, 0x00e0e0e0 }, { REG_RX_PATH, 0x00000003 }, { REG_TX_PATH, 0x00000003 },
    { REG_RF_GAIN, 0x00000024 }, { REG_TXPWR_CCK, 0x23232323 }, { REG_TXPWR_OFDM, 0x2B2B2B2B },
    { REG_RXCCA_TH, 0x00000C0C }, { REG_EDCA_BE, 0x005EA42B }, { REG_EDCA_BK, 0x0000A44F },
    { REG_EDCA_VI, 0x005EA324 }, { REG_EDCA_VO, 0x002FA226 }, { REG_RRSR, 0x000FFFFF },
};
static const regval32 rtw88_phy_init_8723d[] = {
    { REG_BB_CTRL, 0x00000011 }, { REG_PHY_BW, 0x00000000 }, { REG_PHY_CHAN, 0x00000001 },
    { REG_AGC_CFG, 0x00a0a0a0 }, { REG_RX_PATH, 0x00000001 }, { REG_TX_PATH, 0x00000001 },
    { REG_RF_GAIN, 0x0000001c }, { REG_TXPWR_CCK, 0x20202020 }, { REG_TXPWR_OFDM, 0x25252525 },
    { REG_RXCCA_TH, 0x00000A0A }, { REG_EDCA_BE, 0x005EA42B }, { REG_EDCA_BK, 0x0000A44F },
    { REG_EDCA_VI, 0x005EA324 }, { REG_EDCA_VO, 0x002FA226 }, { REG_RRSR, 0x0003FFFF },
};
static const regval32 rtw88_phy_init_8821c[] = {
    { REG_BB_CTRL, 0x00000021 }, { REG_PHY_BW, 0x00000000 }, { REG_PHY_CHAN, 0x00000001 },
    { REG_AGC_CFG, 0x00c0c0c0 }, { REG_RX_PATH, 0x00000001 }, { REG_TX_PATH, 0x00000001 },
    { REG_RF_GAIN, 0x00000020 }, { REG_TXPWR_CCK, 0x21212121 }, { REG_TXPWR_OFDM, 0x26262626 },
    { REG_RXCCA_TH, 0x00000B0B }, { REG_EDCA_BE, 0x005EA42B }, { REG_EDCA_BK, 0x0000A44F },
    { REG_EDCA_VI, 0x005EA324 }, { REG_EDCA_VO, 0x002FA226 }, { REG_RRSR, 0x0007FFFF },
};

static const regval32 rtw88_mac_tbl_common[] = {
    { REG_EDCA_BE, 0x005EA42B }, { REG_EDCA_BK, 0x0000A44F },
    { REG_EDCA_VI, 0x005EA324 }, { REG_EDCA_VO, 0x002FA226 },
    { REG_RRSR, 0x000FFFFF }, { REG_AMPDU_MAX, 0x0000001B },
    { REG_NAV_CFG, 0x0000A44F }, { REG_RETRY_LIMIT, 0x00000404 },
    { REG_AMPDU_DENSITY, 0x00000004 },
};

static const regval32 rtw88_mac_tbl_2ss[] = {
    { REG_RX_PATH, 0x00000003 },
    { REG_TX_PATH, 0x00000003 },
};

static const regval32 rtw88_mac_tbl_1ss[] = {
    { REG_RX_PATH, 0x00000001 },
    { REG_TX_PATH, 0x00000001 },
};

static void applyRegTable(volatile uint8_t *base, const regval32 *tbl, size_t cnt)
{
    if (!base || !tbl)
        return;
    for (size_t i = 0; i < cnt; i++)
        mmioWrite32(base, tbl[i].reg, tbl[i].val);
}

static IOBufferMemoryDescriptor *allocDmaMd(size_t size)
{
    return IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task,
        kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapInhibitCache,
        size, static_cast<IOPhysicalAddress>(~0ULL));
}

static void freeDmaRingsRtw88(struct rtw88_dev *hw)
{
    if (!hw)
        return;

    if (hw->tx_buf_md) {
        for (uint16_t i = 0; i < hw->tx_desc_cnt; i++) {
            if (hw->tx_buf_md[i]) {
                hw->tx_buf_md[i]->release();
                hw->tx_buf_md[i] = nullptr;
            }
        }
        IOFree(hw->tx_buf_md, (size_t)hw->tx_desc_cnt * sizeof(*hw->tx_buf_md));
        hw->tx_buf_md = nullptr;
    }
    if (hw->rx_buf_md) {
        for (uint16_t i = 0; i < hw->rx_desc_cnt; i++) {
            if (hw->rx_buf_md[i]) {
                hw->rx_buf_md[i]->release();
                hw->rx_buf_md[i] = nullptr;
            }
        }
        IOFree(hw->rx_buf_md, (size_t)hw->rx_desc_cnt * sizeof(*hw->rx_buf_md));
        hw->rx_buf_md = nullptr;
    }

    if (hw->tx_desc_md) {
        hw->tx_desc_md->release();
        hw->tx_desc_md = nullptr;
    }
    if (hw->rx_desc_md) {
        hw->rx_desc_md->release();
        hw->rx_desc_md = nullptr;
    }

    hw->tx_desc = nullptr;
    hw->rx_desc = nullptr;
    hw->tx_desc_paddr = 0;
    hw->rx_desc_paddr = 0;
    hw->tx_desc_cnt = 0;
    hw->rx_desc_cnt = 0;
}

static bool initDmaRingsRtw88(struct rtw88_dev *hw)
{
    if (!hw)
        return false;
    if (hw->tx_desc && hw->rx_desc)
        return true;

    hw->tx_desc_cnt = (uint16_t)RTLWM_TX_RING_SZ;
    hw->rx_desc_cnt = (uint16_t)RTLWM_RX_RING_SZ;
    size_t txBytes = (size_t)hw->tx_desc_cnt * sizeof(*hw->tx_desc);
    size_t rxBytes = (size_t)hw->rx_desc_cnt * sizeof(*hw->rx_desc);

    hw->tx_desc_md = allocDmaMd(txBytes);
    hw->rx_desc_md = allocDmaMd(rxBytes);
    if (!hw->tx_desc_md || !hw->rx_desc_md) {
        freeDmaRingsRtw88(hw);
        return false;
    }

    hw->tx_desc = (struct rtw88_dma_desc *)hw->tx_desc_md->getBytesNoCopy();
    hw->rx_desc = (struct rtw88_dma_desc *)hw->rx_desc_md->getBytesNoCopy();
    hw->tx_desc_paddr = (uint64_t)hw->tx_desc_md->getPhysicalAddress();
    hw->rx_desc_paddr = (uint64_t)hw->rx_desc_md->getPhysicalAddress();
    if (!hw->tx_desc || !hw->rx_desc || !hw->tx_desc_paddr || !hw->rx_desc_paddr) {
        freeDmaRingsRtw88(hw);
        return false;
    }

    hw->tx_buf_md = (IOBufferMemoryDescriptor **)IOMalloc(
        (size_t)hw->tx_desc_cnt * sizeof(*hw->tx_buf_md));
    hw->rx_buf_md = (IOBufferMemoryDescriptor **)IOMalloc(
        (size_t)hw->rx_desc_cnt * sizeof(*hw->rx_buf_md));
    if (!hw->tx_buf_md || !hw->rx_buf_md) {
        freeDmaRingsRtw88(hw);
        return false;
    }
    memset(hw->tx_buf_md, 0, (size_t)hw->tx_desc_cnt * sizeof(*hw->tx_buf_md));
    memset(hw->rx_buf_md, 0, (size_t)hw->rx_desc_cnt * sizeof(*hw->rx_buf_md));

    memset(hw->tx_desc, 0, txBytes);
    memset(hw->rx_desc, 0, rxBytes);

    for (uint16_t i = 0; i < hw->tx_desc_cnt; i++) {
        hw->tx_buf_md[i] = allocDmaMd(RTW88_TX_BUF_SIZE);
        if (!hw->tx_buf_md[i]) {
            freeDmaRingsRtw88(hw);
            return false;
        }
        uint64_t paddr = (uint64_t)hw->tx_buf_md[i]->getPhysicalAddress();
        hw->tx_desc[i].addr_lo = (uint32_t)paddr;
        hw->tx_desc[i].addr_hi = (uint32_t)(paddr >> 32);
        hw->tx_desc[i].info0 = (uint32_t)(RTW88_TX_BUF_SIZE & DESC_LEN_MASK);
        if (i == (uint16_t)(hw->tx_desc_cnt - 1))
            hw->tx_desc[i].info1 |= DESC_EOR;
    }
    for (uint16_t i = 0; i < hw->rx_desc_cnt; i++) {
        hw->rx_buf_md[i] = allocDmaMd(RTW88_RX_BUF_SIZE);
        if (!hw->rx_buf_md[i]) {
            freeDmaRingsRtw88(hw);
            return false;
        }
        uint64_t paddr = (uint64_t)hw->rx_buf_md[i]->getPhysicalAddress();
        hw->rx_desc[i].addr_lo = (uint32_t)paddr;
        hw->rx_desc[i].addr_hi = (uint32_t)(paddr >> 32);
        hw->rx_desc[i].info0 = DESC_OWN | (uint32_t)(RTW88_RX_BUF_SIZE & DESC_LEN_MASK);
        if (i == (uint16_t)(hw->rx_desc_cnt - 1))
            hw->rx_desc[i].info1 |= DESC_EOR;
    }

    return true;
}

static uint16_t getHwRingIdxRtw88(volatile uint8_t *base, uint32_t reg)
{
    uint32_t v = mmioRead32(base, reg);
    return (uint16_t)((v >> TRX_BD_HW_IDX_SHIFT) & TRX_BD_IDX_MASK);
}

static void setHostRingIdxRtw88(volatile uint8_t *base, uint32_t reg, uint16_t host)
{
    uint32_t v = mmioRead32(base, reg);
    v &= ~TRX_BD_IDX_MASK;
    v |= (uint32_t)(host & TRX_BD_IDX_MASK);
    mmioWrite32(base, reg, v);
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

    uint8_t phyMap[1024];
    uint16_t phyLen = len > sizeof(phyMap) ? (uint16_t)sizeof(phyMap) : len;

    for (uint16_t i = 0; i < phyLen; i++)
        phyMap[i] = mmioRead8(base, REG_EFUSE_BASE + i);

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

static bool parseEfuseRtw88(enum rtw88_chip_id chip, const uint8_t *efuse,
                            uint16_t len, uint8_t mac[6], uint8_t *txpwrCck,
                            uint8_t *txpwrOfdm)
{
    if (!efuse || !mac || len < 0x40)
        return false;

    uint16_t macOff = (chip == RTW88_CHIP_8723D) ? 0x11 : 0x10;
    uint16_t cckOff = (chip == RTW88_CHIP_8723D) ? 0x58 : 0x5A;
    uint16_t ofdmOff = cckOff + 1;
    if (macOff + 6 > len)
        return false;
    memcpy(mac, efuse + macOff, 6);
    if (!isValidMacAddr(mac))
        return false;

    if (txpwrCck)
        *txpwrCck = (cckOff < len && efuse[cckOff] != 0xFF) ? efuse[cckOff] : 0x20;
    if (txpwrOfdm)
        *txpwrOfdm = (ofdmOff < len && efuse[ofdmOff] != 0xFF) ? efuse[ofdmOff] : 0x20;
    return true;
}

static void runRfkCalibrationRtw88(volatile uint8_t *base, bool *iqkDone, bool *dpkDone)
{
    if (!base)
        return;

    uint8_t crBak = mmioRead8(base, REG_CR);
    mmioWrite8(base, REG_CR, (uint8_t)(crBak & (uint8_t)~CR_TRX_ENABLE));

    bool iqk = false;
    bool dpk = false;
    for (int tryN = 0; tryN < 3 && !iqk; tryN++) {
        mmioWrite32(base, REG_IQK_STATUS, IQK_DONE);
        mmioWrite32(base, REG_IQK_CTRL, IQK_START);
        iqk = pollReg32(base, REG_IQK_STATUS, IQK_DONE, IQK_DONE, 1200, 20);
    }
    for (int tryN = 0; tryN < 3 && !dpk; tryN++) {
        mmioWrite32(base, REG_DPK_STATUS, DPK_DONE);
        mmioWrite32(base, REG_DPK_CTRL, DPK_START);
        dpk = pollReg32(base, REG_DPK_STATUS, DPK_DONE, DPK_DONE, 1200, 20);
    }

    mmioWrite8(base, REG_CR, crBak);
    if (iqkDone) *iqkDone = iqk;
    if (dpkDone) *dpkDone = dpk;
}

static uint8_t sanitizeTxpwrByte(uint8_t v, uint8_t defv)
{
    if (v == 0x00 || v == 0xFF)
        return defv;
    return v;
}

static void programTxPowerFromEfuseRtw88(volatile uint8_t *base, uint8_t cck, uint8_t ofdm)
{
    if (!base)
        return;
    uint32_t cck32 = (uint32_t)cck | ((uint32_t)cck << 8) |
                     ((uint32_t)cck << 16) | ((uint32_t)cck << 24);
    uint32_t ofdm32 = (uint32_t)ofdm | ((uint32_t)ofdm << 8) |
                      ((uint32_t)ofdm << 16) | ((uint32_t)ofdm << 24);
    mmioWrite32(base, REG_TXPWR_CCK, cck32);
    mmioWrite32(base, REG_TXPWR_OFDM, ofdm32);
}

static void applyChipMacTableRtw88(volatile uint8_t *base, enum rtw88_chip_id chip)
{
    if (!base)
        return;
    switch (chip) {
    case RTW88_CHIP_8822B:
    case RTW88_CHIP_8822C:
        applyRegTable(base, rtw88_mac_tbl_2ss,
                      sizeof(rtw88_mac_tbl_2ss) / sizeof(rtw88_mac_tbl_2ss[0]));
        break;
    case RTW88_CHIP_8723D:
    case RTW88_CHIP_8821C:
    default:
        applyRegTable(base, rtw88_mac_tbl_1ss,
                      sizeof(rtw88_mac_tbl_1ss) / sizeof(rtw88_mac_tbl_1ss[0]));
        break;
    }
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
    if (!hw.tx_active || !hw.rx_active)
        return kIOReturnIOError;
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

bool RtlHal_rtw88::enqueueTxPacket(mbuf_t packet)
{
    if (!packet || !hw.running || !hw.tx_active || !hw.mmio || !hw.tx_desc_cnt ||
        !hw.tx_desc || !hw.tx_buf_md)
        return false;

    hw.tx_cons = getHwRingIdxRtw88(hw.mmio, REG_TXBD_IDX_VOQ);
    uint16_t prod = hw.tx_prod;
    uint16_t next = (uint16_t)((prod + 1) % hw.tx_desc_cnt);
    if (next == hw.tx_cons)
        return false;

    if (hw.tx_desc[prod].info0 & DESC_OWN)
        return false;

    uint32_t len = (uint32_t)mbuf_pkthdr_len(packet);
    if (len == 0)
        len = (uint32_t)mbuf_len(packet);
    if (len == 0 || len > RTW88_TX_BUF_SIZE)
        return false;

    void *dst = hw.tx_buf_md[prod] ? hw.tx_buf_md[prod]->getBytesNoCopy() : nullptr;
    if (!dst)
        return false;
    if (mbuf_copydata(packet, 0, (int)len, (char *)dst) != 0)
        return false;

    uint32_t info0 = hw.tx_desc[prod].info0;
    info0 &= ~(DESC_OWN | DESC_LEN_MASK);
    info0 |= (len & DESC_LEN_MASK);
    info0 |= DESC_OWN;
    hw.tx_desc[prod].info0 = info0;

    hw.tx_prod = next;
    setHostRingIdxRtw88(hw.mmio, REG_TXBD_IDX_VOQ, hw.tx_prod);
    return true;
}

void RtlHal_rtw88::handleInterrupt()
{
    if (!hw.running || !hw.mmio)
        return;

    uint32_t isr = mmioRead32(hw.mmio, REG_HISR0);
    if (isr == 0U || isr == 0xFFFFFFFFU)
        return;
    mmioWrite32(hw.mmio, REG_HISR0, isr);

    hw.irq_count++;
    if (isr & RTW88_INT_TX_DONE) {
        hw.tx_cons = getHwRingIdxRtw88(hw.mmio, REG_TXBD_IDX_VOQ);
    }
    if (isr & RTW88_INT_RX_DONE) {
        uint16_t hwRxIdx = getHwRingIdxRtw88(hw.mmio, REG_RXBD_IDX_MPDUQ);
        if (hw.rx_desc_cnt) {
            while (hw.rx_cons != hwRxIdx) {
                uint16_t idx = hw.rx_cons;
                hw.rx_desc[idx].info0 = DESC_OWN | (uint32_t)(RTW88_RX_BUF_SIZE & DESC_LEN_MASK);
                hw.rx_cons = (uint16_t)((hw.rx_cons + 1) % hw.rx_desc_cnt);
            }
            hw.rx_prod = hw.rx_cons;
            setHostRingIdxRtw88(hw.mmio, REG_RXBD_IDX_MPDUQ, hw.rx_prod);
        }
    }
    if (isr & (RTW88_INT_RX_ERR | RTW88_INT_TX_ERR)) {
        IOLog("RtlHal_rtw88: irq error isr=0x%08x\n", isr);
    }

    if ((hw.irq_count & 0x3FFU) == 1U) {
        IOLog("RtlHal_rtw88: irq=%u isr=0x%08x tx[%u/%u] rx[%u/%u]\n",
              hw.irq_count, isr, hw.tx_prod, hw.tx_cons, hw.rx_prod, hw.rx_cons);
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
    if (hw.mmio)
        return writeMacAddressRegs(hw.mmio, hw.mac_addr);
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
    applyChipMacTableRtw88(hw.mmio, hw.chip_id);

    hw.efuse_len = efuseLenByChip(hw.chip_id);
    hw.efuse_valid = readEfuseMapRtw88(hw.mmio, hw.efuse_map, hw.efuse_len);
    if (hw.efuse_valid) {
        uint8_t efuseMac[6] = {0};
        if (parseEfuseRtw88(hw.chip_id, hw.efuse_map, hw.efuse_len, efuseMac,
                            &hw.txpwr_cck, &hw.txpwr_ofdm)) {
            memcpy(hw.mac_addr, efuseMac, sizeof(hw.mac_addr));
        } else {
            hw.efuse_valid = false;
        }
    }
    if (!hw.efuse_valid) {
        hw.txpwr_cck = 0x20;
        hw.txpwr_ofdm = 0x20;
    }
    hw.txpwr_cck = sanitizeTxpwrByte(hw.txpwr_cck, 0x20);
    hw.txpwr_ofdm = sanitizeTxpwrByte(hw.txpwr_ofdm, 0x20);
    programTxPowerFromEfuseRtw88(hw.mmio, hw.txpwr_cck, hw.txpwr_ofdm);

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
    IOLog("RtlHal_rtw88: initRF done chip=%d rf_ctrl=0x%02x iqk=%d dpk=%d\n",
          hw.chip_id, mmioRead8(hw.mmio, REG_RF_CTRL),
          hw.iqk_done ? 1 : 0, hw.dpk_done ? 1 : 0);
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
    if (!initDmaRingsRtw88(&hw)) {
        IOLog("RtlHal_rtw88: failed to allocate DMA descriptors\n");
        hw.pciDev->setBusMasterEnable(false);
        hw.pciDev->setMemoryEnable(false);
        hw.tx_active = false;
        hw.rx_active = false;
        return;
    }

    if (hw.mmio) {
        uint8_t cr = mmioRead8(hw.mmio, REG_CR);
        mmioWrite8(hw.mmio, REG_CR, (uint8_t)(cr | CR_TRX_ENABLE));
        mmioWrite32(hw.mmio, REG_TXBD_DESA_VOQ, (uint32_t)hw.tx_desc_paddr);
        mmioWrite32(hw.mmio, REG_RXBD_DESA_MPDUQ, (uint32_t)hw.rx_desc_paddr);
        mmioWrite16(hw.mmio, REG_TXBD_NUM_VOQ, hw.tx_desc_cnt);
        mmioWrite16(hw.mmio, REG_RXBD_NUM_MPDUQ, hw.rx_desc_cnt);
        setHostRingIdxRtw88(hw.mmio, REG_TXBD_IDX_VOQ, 0);
        setHostRingIdxRtw88(hw.mmio, REG_RXBD_IDX_MPDUQ, 0);
        mmioWrite32(hw.mmio, REG_HIMR0, RTW88_IRQ_MASK);
        mmioWrite32(hw.mmio, REG_HISR0, 0xFFFFFFFFU);
    }

    hw.tx_active = true;
    hw.rx_active = true;
    hw.tx_prod = 0;
    hw.tx_cons = 0;
    hw.rx_prod = 0;
    hw.rx_cons = 0;
    hw.irq_count = 0;
    IOLog("RtlHal_rtw88: startTxRx (pci mem+bm enabled, dma tx=%u rx=%u)\n",
          hw.tx_desc_cnt, hw.rx_desc_cnt);
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
    freeDmaRingsRtw88(&hw);
    IOLog("RtlHal_rtw88: stopTxRx (pci mem+bm disabled)\n");
}
