/*
 * rtlwm.cpp – Main macOS kext driver for Realtek 802.11 WiFi (non-Airport)
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtlwm.hpp"
#include "hal_rtw88/RtlHal_rtw88.hpp"
#include "hal_rtw89/RtlHal_rtw89.hpp"
#include <limits.h>

/* PCI vendor / device IDs for supported Realtek chips -------------------- */
static const struct {
    uint16_t vid;
    uint16_t pid;
    const char *name;
} rtlwm_devices[] = {
    /* rtw88 family */
    { 0x10EC, 0xB822, "RTL8822BE" },
    { 0x10EC, 0xC822, "RTL8822CE" },
    { 0x10EC, 0xD723, "RTL8723DE" },
    { 0x10EC, 0xC821, "RTL8821CE" },
    { 0x10EC, 0xC82F, "RTL8821CE (variant)" },
    /* rtw89 family */
    { 0x10EC, 0x8852, "RTL8852AE" },
    { 0x10EC, 0xB852, "RTL8852BE" },
    { 0x10EC, 0x8851, "RTL8851BE" },
    { 0x10EC, 0xA852, "RTL8852CE" },
};
#define RTLWM_NDEVICES (sizeof(rtlwm_devices) / sizeof(rtlwm_devices[0]))

OSDefineMetaClassAndStructors(rtlwm, IOEthernetController)

/* --------------------------------------------------------------------------
 * IOService lifecycle
 * -------------------------------------------------------------------------- */

bool rtlwm::init(OSDictionary *dict)
{
    if (!IOEthernetController::init(dict))
        return false;

    workLoop    = nullptr;
    commandGate = nullptr;
    intrSource  = nullptr;
    pciDevice   = nullptr;
    netInterface = nullptr;
    halService  = nullptr;
    ifEnabled   = false;
    ifRunning   = false;
    powerState  = 0;
    return true;
}

bool rtlwm::start(IOService *provider)
{
    pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!pciDevice) {
        IOLog("rtlwm: provider is not a PCIDevice\n");
        return false;
    }

    if (!IOEthernetController::start(provider))
        return false;

    /* Enable memory-mapped I/O and bus-mastering */
    pciDevice->setMemoryEnable(true);
    pciDevice->setBusMasterEnable(true);

    /* Select and probe the HAL for the detected chip */
    halService = probeHardware(pciDevice);
    if (!halService) {
        IOLog("rtlwm: unsupported device %04x:%04x\n",
              pciDevice->configRead16(kIOPCIConfigVendorID),
              pciDevice->configRead16(kIOPCIConfigDeviceID));
        goto fail_no_hal;
    }

    if (!halService->initWithController(this, workLoop, commandGate)) {
        IOLog("rtlwm: HAL init failed\n");
        goto fail_hal_init;
    }

    if (!halService->attach(pciDevice)) {
        IOLog("rtlwm: HAL attach failed\n");
        goto fail_hal_attach;
    }

    /* Set up interrupt event source */
    intrSource = IOInterruptEventSource::interruptEventSource(
        this,
        &rtlwm::interruptOccurred,
        provider, 0);
    if (!intrSource || workLoop->addEventSource(intrSource) != kIOReturnSuccess) {
        IOLog("rtlwm: failed to register interrupt\n");
        goto fail_intr;
    }
    intrSource->enable();

    /* Watchdog timer (1-second interval) */
    watchdogTimer = IOTimerEventSource::timerEventSource(
        this,
        &rtlwm::watchdogTimeout);
    if (!watchdogTimer ||
        workLoop->addEventSource(watchdogTimer) != kIOReturnSuccess) {
        IOLog("rtlwm: failed to create watchdog timer\n");
        goto fail_watchdog;
    }
    watchdogTimer->setTimeoutMS(1000);

    if (!attachInterface((IONetworkInterface **)&netInterface))
        goto fail_attach_if;

    registerService();
    IOLog("rtlwm: started successfully\n");
    return true;

fail_attach_if:
    workLoop->removeEventSource(watchdogTimer);
    watchdogTimer->release();
    watchdogTimer = nullptr;
fail_watchdog:
    workLoop->removeEventSource(intrSource);
    intrSource->release();
    intrSource = nullptr;
fail_intr:
    halService->detach(pciDevice);
fail_hal_attach:
fail_hal_init:
    halService->release();
    halService = nullptr;
fail_no_hal:
    IOEthernetController::stop(provider);
    return false;
}

void rtlwm::stop(IOService *provider)
{
    if (ifRunning) {
        halService->disable(netInterface);
        ifRunning = false;
    }
    if (netInterface) {
        detachInterface(netInterface);
        netInterface = nullptr;
    }
    if (watchdogTimer) {
        watchdogTimer->cancelTimeout();
        workLoop->removeEventSource(watchdogTimer);
        watchdogTimer->release();
        watchdogTimer = nullptr;
    }
    if (intrSource) {
        intrSource->disable();
        workLoop->removeEventSource(intrSource);
        intrSource->release();
        intrSource = nullptr;
    }
    if (halService) {
        halService->detach(pciDevice);
        halService->release();
        halService = nullptr;
    }
    IOEthernetController::stop(provider);
}

void rtlwm::free()
{
    if (commandGate) {
        commandGate->release();
        commandGate = nullptr;
    }
    if (workLoop) {
        workLoop->release();
        workLoop = nullptr;
    }
    IOEthernetController::free();
}

bool rtlwm::createWorkLoop()
{
    workLoop = IOWorkLoop::workLoop();
    return workLoop != nullptr;
}

IOWorkLoop *rtlwm::getWorkLoop() const
{
    return workLoop;
}

/* --------------------------------------------------------------------------
 * IOEthernetController
 * -------------------------------------------------------------------------- */

IOReturn rtlwm::enable(IONetworkInterface *netif)
{
    if (ifRunning)
        return kIOReturnSuccess;
    if (!netif)
        return kIOReturnBadArgument;
    if (!halService)
        return kIOReturnNotReady;

    IOReturn ret = halService->enable(netif);
    if (ret == kIOReturnSuccess) {
        if (!watchdogTimer || !outputQueue) {
            (void)halService->disable(netif);
            return kIOReturnNotReady;
        }
        ifRunning = true;
        watchdogTimer->setTimeoutMS(1000);
        outputQueue->setCapacity(RTLWM_TX_RING_SZ);
        outputQueue->start();
    }
    return ret;
}

IOReturn rtlwm::disable(IONetworkInterface *netif)
{
    if (!ifRunning)
        return kIOReturnSuccess;

    if (outputQueue) {
        outputQueue->stop();
        outputQueue->flush();
    }
    if (watchdogTimer)
        watchdogTimer->cancelTimeout();
    IOReturn ret = halService ? halService->disable(netif) : kIOReturnNotReady;
    ifRunning = false;
    return ret;
}

IOReturn rtlwm::getHardwareAddress(IOEthernetAddress *addr)
{
    if (!addr)
        return kIOReturnBadArgument;

    if (!halService)
        return kIOReturnNotReady;

    RtlDriverInfo *info = halService->getDriverInfo();
    const uint8_t *mac  = info ? info->getMacAddress() : nullptr;
    if (!mac)
        return kIOReturnNotFound;

    memcpy(addr->bytes, mac, sizeof(addr->bytes));
    return kIOReturnSuccess;
}

IOReturn rtlwm::setHardwareAddress(const IOEthernetAddress *addr)
{
    if (!addr)
        return kIOReturnBadArgument;
    if (!halService)
        return kIOReturnNotReady;

    RtlDriverInfo *info = halService->getDriverInfo();
    if (!info || !info->setMacAddress(addr->bytes))
        return kIOReturnUnsupported;
    return kIOReturnSuccess;
}

IOReturn rtlwm::setMulticastList(IOEthernetAddress *addrs, UInt32 count)
{
    if (!halService)
        return kIOReturnNotReady;
    if (count > static_cast<UInt32>(INT_MAX))
        return kIOReturnBadArgument;

    RtlDriverController *controller = halService->getDriverController();
    if (!controller)
        return kIOReturnNotReady;
    if (count > 0 && !addrs)
        return kIOReturnBadArgument;

    return controller->setMulticastList(addrs, static_cast<int>(count));
}

IOReturn rtlwm::setPromiscuousMode(bool active)
{
    (void)active;
    return kIOReturnSuccess;
}

IOReturn rtlwm::getMaxPacketSize(UInt32 *maxSize) const
{
    if (!maxSize)
        return kIOReturnBadArgument;
    *maxSize = kIOEthernetMaxPacketSize;
    return kIOReturnSuccess;
}

bool rtlwm::configureInterface(IONetworkInterface *netif)
{
    if (!IOEthernetController::configureInterface(netif))
        return false;

    IONetworkData *nd = netif->getNetworkData(kIONetworkStatsKey);
    if (!nd)
        return false;

    outputQueue = getOutputQueue();
    return outputQueue != nullptr;
}

UInt32 rtlwm::outputPacket(mbuf_t m, void *param)
{
    (void)param;
    if (!halService || !ifRunning) {
        mbuf_freem(m);
        return kIOReturnOutputDropped;
    }

    /* TX queue handoff remains pending a dedicated HAL TX API. */
    mbuf_freem(m);
    return kIOReturnOutputDropped;
}

/* --------------------------------------------------------------------------
 * Power management
 * -------------------------------------------------------------------------- */

IOReturn rtlwm::registerWithPolicyMaker(IOService *policyMaker)
{
    static IOPMPowerState states[2] = {
        { 1, 0,                  0,                  0,         0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 },
    };
    return policyMaker->registerPowerDriver(this, states, 2);
}

IOReturn rtlwm::setPowerState(unsigned long ordinal, IOService *)
{
    powerState = (UInt32)ordinal;
    if (!halService || !netInterface)
        return IOPMAckImplied;

    if (ordinal == 0) {
        (void)disable(netInterface);
        return IOPMAckImplied;
    }

    if (!ifRunning)
        (void)enable(netInterface);
    return IOPMAckImplied;
}

/* --------------------------------------------------------------------------
 * Interrupt / watchdog
 * -------------------------------------------------------------------------- */

void rtlwm::interruptOccurred(OSObject *owner, IOInterruptEventSource *, int)
{
    rtlwm *self = OSDynamicCast(rtlwm, owner);
    if (self)
        self->handleInterrupt();
}

void rtlwm::handleInterrupt()
{
    /* Interrupt dispatch to HAL remains pending a dedicated HAL IRQ API. */
}

void rtlwm::watchdogTimeout(OSObject *owner, IOTimerEventSource *)
{
    rtlwm *self = OSDynamicCast(rtlwm, owner);
    if (self)
        self->handleWatchdog();
}

void rtlwm::handleWatchdog()
{
    struct ieee80211com *ic = halService ? halService->get80211Controller() : nullptr;
    if (ic)
        ieee80211_watchdog(ic);
    watchdogTimer->setTimeoutMS(1000);
}

/* --------------------------------------------------------------------------
 * HAL selection
 * -------------------------------------------------------------------------- */

RtlHalService *rtlwm::probeHardware(IOPCIDevice *dev)
{
    uint16_t vid = dev->configRead16(kIOPCIConfigVendorID);
    uint16_t pid = dev->configRead16(kIOPCIConfigDeviceID);

    for (size_t i = 0; i < RTLWM_NDEVICES; i++) {
        if (rtlwm_devices[i].vid != vid || rtlwm_devices[i].pid != pid)
            continue;

        IOLog("rtlwm: detected %s\n", rtlwm_devices[i].name);

        /* RTW89 family */
        if (pid == 0x8852 || pid == 0xB852 || pid == 0x8851 || pid == 0xA852)
            return RtlHal_rtw89::withDevice(dev);

        /* RTW88 family (default) */
        return RtlHal_rtw88::withDevice(dev);
    }
    return nullptr;
}
