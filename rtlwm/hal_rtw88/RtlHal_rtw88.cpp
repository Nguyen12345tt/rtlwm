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

OSDefineMetaClassAndStructors(RtlHal_rtw88, RtlHalService)

namespace {
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

    /* Map MMIO BAR 2 */
    hw.mmioMap = device->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if (!hw.mmioMap) {
        IOLog("RtlHal_rtw88: failed to map MMIO\n");
        return false;
    }
    hw.mmio = (volatile uint8_t *)hw.mmioMap->getVirtualAddress();

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
    /* TODO: set ic_caps, ic_sup_rates, ic_phytype, call ieee80211_ifattach() */

    IOLog("RtlHal_rtw88: attached chip_id=%d\n", hw.chip_id);
    return true;

fail_fw:
fail_hw:
    hw.mmioMap->release();
    hw.mmioMap = nullptr;
    return false;
}

void RtlHal_rtw88::detach(IOPCIDevice *device)
{
    stopTxRx();
    /* TODO: ieee80211_ifdetach(&ic) */
    if (hw.mmioMap) {
        hw.mmioMap->release();
        hw.mmioMap = nullptr;
    }
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
    /* TODO: read from HAL noise floor register */
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
    /* TODO: clear scan state in HAL */
}

IOReturn RtlHal_rtw88::setMulticastList(IOEthernetAddress *addr, int cnt)
{
    /* TODO: program multicast filter registers */
    return kIOReturnSuccess;
}

/* --------------------------------------------------------------------------
 * Private helpers
 * -------------------------------------------------------------------------- */
bool RtlHal_rtw88::initHardware()
{
    /*
     * TODO: port rtw88 main.c: rtw_core_init() / rtw_pci_probe()
     * Steps:
     *  1. Disable MAC
     *  2. Load efuse / OTP to read MAC address & RF calibration data
     *  3. Power on RF section
     *  4. Init MAC registers (mac.c: rtw_mac_power_on())
     *  5. Init BB registers (phy.c: rtw_phy_init())
     */
    IOLog("RtlHal_rtw88: initHardware (TODO)\n");
    return true;
}

bool RtlHal_rtw88::loadFirmware()
{
    /*
     * TODO: port rtw88 fw.c: rtw_fw_download()
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
    if (hw.chip_id < RTW88_CHIP_UNKNOWN)
        strncpy(hw.fw_name, fwNames[hw.chip_id], sizeof(hw.fw_name) - 1);

    IOLog("RtlHal_rtw88: loadFirmware %s (TODO)\n", hw.fw_name);
    return true;
}

void RtlHal_rtw88::initRF()
{
    /*
     * TODO: port rtw88 phy.c: rtw_phy_init()
     * Steps:
     *  1. Write BB registers (band / channel agnostic baseline)
     *  2. Write initial RF register values
     *  3. Trigger calibration sequences (IQK, DPK, etc.)
     */
    IOLog("RtlHal_rtw88: initRF (TODO)\n");
}

void RtlHal_rtw88::startTxRx()
{
    /*
     * TODO: port rtw88 mac.c: rtw_mac_start_tx_rx()
     * Steps:
     *  1. Enable DMA TX/RX paths
     *  2. Enable interrupt mask
     *  3. Start TX scheduler
     */
    IOLog("RtlHal_rtw88: startTxRx (TODO)\n");
}

void RtlHal_rtw88::stopTxRx()
{
    /*
     * TODO: port rtw88 mac.c: rtw_mac_stop_tx_rx()
     */
    IOLog("RtlHal_rtw88: stopTxRx (TODO)\n");
}
