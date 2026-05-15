/*
 * AirportRtlwm.cpp – Native Airport interface for Realtek WiFi
 *
 * Adapted from OpenIntelWireless/itlwm – AirportItlwm.cpp (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "Airportrtlwm.hpp"

OSDefineMetaClassAndStructors(AirportRtlwm, IO80211ControllerBase)

SInt32 rtlwmHandleStaIoctl(AirportRtlwm *controller, UInt requestType, int request,
                           IO80211Interface *interface, void *data);
SInt32 rtlwmHandleVirtualIoctl(AirportRtlwm *controller, UInt requestType, int request,
                               IO80211VirtualInterface *interface, void *data);

/* --------------------------------------------------------------------------
 * IOService lifecycle
 * -------------------------------------------------------------------------- */

bool AirportRtlwm::init(OSDictionary *dict)
{
    if (!IO80211ControllerBase::init(dict))
        return false;
    workLoop     = nullptr;
    commandGate  = nullptr;
    pciDevice    = nullptr;
    halService   = nullptr;
    infraInterface = nullptr;
    ifEnabled    = false;
    return true;
}

bool AirportRtlwm::start(IOService *provider)
{
    pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!pciDevice) {
        IOLog("AirportRtlwm: provider is not a PCIDevice\n");
        return false;
    }

    if (!IO80211ControllerBase::start(provider))
        return false;

    pciDevice->setMemoryEnable(true);
    pciDevice->setBusMasterEnable(true);

    /*
     * HAL service binding is pending full multi-kext wiring:
     * (via IOService::waitForMatchingService or direct instantiation)
     * and attach it.
     */

    IOLog("AirportRtlwm: started\n");
    registerService();
    return true;
}

void AirportRtlwm::stop(IOService *provider)
{
    if (infraInterface) {
        detachInterface(infraInterface);
        infraInterface = nullptr;
    }
    if (halService) {
        halService->detach(pciDevice);
        halService->release();
        halService = nullptr;
    }
    IO80211ControllerBase::stop(provider);
}

void AirportRtlwm::free()
{
    if (commandGate) {
        commandGate->release();
        commandGate = nullptr;
    }
    if (workLoop) {
        workLoop->release();
        workLoop = nullptr;
    }
    IO80211ControllerBase::free();
}

bool AirportRtlwm::createWorkLoop()
{
    workLoop = IOWorkLoop::workLoop();
    return workLoop != nullptr;
}

IOWorkLoop *AirportRtlwm::getWorkLoop() const
{
    return workLoop;
}

/* --------------------------------------------------------------------------
 * IO80211Controller mandatory overrides
 * -------------------------------------------------------------------------- */

SInt32 AirportRtlwm::apple80211Request(UInt requestType, int request,
                                        IO80211Interface *interface,
                                        void *data)
{
    return rtlwmHandleStaIoctl(this, requestType, request, interface, data);
}

SInt32 AirportRtlwm::apple80211VirtualRequest(UInt requestType, int request,
                                               IO80211VirtualInterface *interface,
                                               void *data)
{
    return rtlwmHandleVirtualIoctl(this, requestType, request, interface, data);
}

SInt32 AirportRtlwm::stopDMA()
{
    if (halService)
        halService->disable(infraInterface);
    return kIOReturnSuccess;
}

UInt32 AirportRtlwm::hardwareOutputQueueDepth(IO80211Interface *)
{
    return RTLWM_TX_RING_SZ;
}

SInt32 AirportRtlwm::performCountryCodeOperation(IO80211Interface *,
                                                   IO80211CountryCodeOp op)
{
    if (op == kIO80211CountryCodeReset) {
        RtlDriverController *controller = getDriverController();
        if (controller)
            controller->clearScanningFlags();
        return kIOReturnSuccess;
    }
    return kIOReturnUnsupported;
}

SInt32 AirportRtlwm::enableFeature(IO80211FeatureCode code, void *data)
{
    (void)data;
    if (code == kIO80211Feature80211n)
        return kIOReturnSuccess;
    return kIOReturnUnsupported;
}

/* --------------------------------------------------------------------------
 * IOEthernetController
 * -------------------------------------------------------------------------- */

IOReturn AirportRtlwm::enable(IONetworkInterface *interface)
{
    if (ifEnabled)
        return kIOReturnSuccess;
    if (!halService)
        return kIOReturnNotReady;
    IOReturn ret = halService->enable(interface);
    if (ret == kIOReturnSuccess)
        ifEnabled = true;
    return ret;
}

IOReturn AirportRtlwm::disable(IONetworkInterface *interface)
{
    if (!ifEnabled)
        return kIOReturnSuccess;
    if (halService)
        halService->disable(interface);
    ifEnabled = false;
    return kIOReturnSuccess;
}

IOReturn AirportRtlwm::getHardwareAddress(IOEthernetAddress *addr)
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

bool AirportRtlwm::configureInterface(IONetworkInterface *netif)
{
    return IO80211ControllerBase::configureInterface(netif);
}

UInt32 AirportRtlwm::outputPacket(mbuf_t m, void *param)
{
    (void)param;
    if (!halService || !ifEnabled) {
        mbuf_freem(m);
        return kIOReturnOutputDropped;
    }

    if (halService->enqueueTxPacket(m)) {
        mbuf_freem(m);
        return kIOReturnOutputSuccess;
    }

    mbuf_freem(m);
    return kIOReturnOutputDropped;
}

/* --------------------------------------------------------------------------
 * Power management
 * -------------------------------------------------------------------------- */

IOReturn AirportRtlwm::setPowerState(unsigned long ordinal, IOService *)
{
    if (!halService || !infraInterface)
        return IOPMAckImplied;

    if (ordinal == 0) {
        if (ifEnabled) {
            halService->disable(infraInterface);
            ifEnabled = false;
        }
        return IOPMAckImplied;
    }

    if (!ifEnabled && halService->enable(infraInterface) == kIOReturnSuccess)
        ifEnabled = true;
    return IOPMAckImplied;
}

RtlDriverInfo *AirportRtlwm::getDriverInfo() const
{
    return halService ? halService->getDriverInfo() : nullptr;
}

RtlDriverController *AirportRtlwm::getDriverController() const
{
    return halService ? halService->getDriverController() : nullptr;
}

bool AirportRtlwm::isInterfaceEnabled() const
{
    return ifEnabled;
}
