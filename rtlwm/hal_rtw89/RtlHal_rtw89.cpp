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

OSDefineMetaClassAndStructors(RtlHal_rtw89, RtlHalService)

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

    /* Map MMIO BAR 2 */
    hw.mmioMap = device->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if (!hw.mmioMap) {
        IOLog("RtlHal_rtw89: failed to map MMIO\n");
        return false;
    }
    hw.mmio = (volatile uint8_t *)hw.mmioMap->getVirtualAddress();

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

    IOLog("RtlHal_rtw89: attached chip_id=%d\n", hw.chip_id);
    return true;

fail_fw:
fail_hw:
    hw.mmioMap->release();
    hw.mmioMap = nullptr;
    return false;
}

void RtlHal_rtw89::detach(IOPCIDevice *device)
{
    stopTxRx();
    if (hw.mmioMap) {
        hw.mmioMap->release();
        hw.mmioMap = nullptr;
    }
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
    if (!addr || cnt < 0)
        return kIOReturnBadArgument;
    /* Multicast register programming is pending MAC filter integration. */
    return kIOReturnSuccess;
}

/* --------------------------------------------------------------------------
 * Private helpers
 * -------------------------------------------------------------------------- */
bool RtlHal_rtw89::initHardware()
{
    /*
     * Porting reference: rtw89 core.c: rtw89_core_init()
     *  1. Power on sequence (rtw89_chip_ops.power_on)
     *  2. Read efuse for MAC addr + RF calibration
     *  3. MAC init (rtw89_mac_init)
     *  4. BB init  (rtw89_phy_init)
     *
     * Key difference from rtw88: RTW89 uses new "DMAC/CMAC" architecture
     * with separate data and control MAC paths.
     */
    IOLog("RtlHal_rtw89: initHardware (stub)\n");
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
    if (hw.chip_id < RTW89_CHIP_UNKNOWN)
        strncpy(hw.fw_name, fwNames[hw.chip_id], sizeof(hw.fw_name) - 1);

    IOLog("RtlHal_rtw89: loadFirmware %s (stub)\n", hw.fw_name);
    return true;
}

void RtlHal_rtw89::initRF()
{
    /*
     * Porting reference: rtw89 phy.c
     *  RTW89 uses a register-format BB/RF init table loaded from firmware
     *  rather than hard-coded register writes.
     */
    IOLog("RtlHal_rtw89: initRF (stub)\n");
}

void RtlHal_rtw89::startTxRx()
{
    IOLog("RtlHal_rtw89: startTxRx (stub)\n");
}

void RtlHal_rtw89::stopTxRx()
{
    IOLog("RtlHal_rtw89: stopTxRx (stub)\n");
}
