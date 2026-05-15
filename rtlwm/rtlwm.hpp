/*
 * rtlwm.hpp – Main macOS kext driver class for Realtek 802.11 WiFi
 *
 * This is the non-Airport (HeliPort-compatible) variant.
 * It provides IOEthernetController-based networking for Realtek PCIe WiFi
 * cards using Linux rtw88 / rtw89 driver logic ported through rtl80211.
 *
 * Architecture:
 *   rtlwm (IOEthernetController)
 *     └── RtlHalService  (abstract HAL)
 *           ├── RtlHal_rtw88  (RTL8822BE/CE, RTL8723DE, RTL8821CE …)
 *           └── RtlHal_rtw89  (RTL8852AE/BE, RTL8851BE, RTL8922AE …)
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 */

#ifndef rtlwm_hpp
#define rtlwm_hpp

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IOGatedOutputQueue.h>

#include <HAL/RtlHalService.hpp>
#include <HAL/RtlDriverInfo.hpp>
#include <HAL/RtlDriverController.hpp>
#include <net80211/ieee80211_var.h>

#define RTLWM_MAX_PEERS   32
#define RTLWM_RX_RING_SZ 256
#define RTLWM_TX_RING_SZ 256

class rtlwm : public IOEthernetController {
    OSDeclareDefaultStructors(rtlwm)

public:
    /* IOService lifecycle */
    virtual bool     init(OSDictionary *dict = nullptr)     override;
    virtual bool     start(IOService *provider)             override;
    virtual void     stop(IOService *provider)              override;
    virtual void     free()                                 override;
    virtual bool     createWorkLoop()                       override;
    virtual IOWorkLoop *getWorkLoop() const                 override;

    /* IOEthernetController */
    virtual IOReturn enable(IONetworkInterface *netif)      override;
    virtual IOReturn disable(IONetworkInterface *netif)     override;
    virtual IOReturn getHardwareAddress(IOEthernetAddress *) override;
    virtual IOReturn setHardwareAddress(const IOEthernetAddress *) override;
    virtual IOReturn setMulticastList(IOEthernetAddress *, UInt32) override;
    virtual IOReturn setPromiscuousMode(bool active)        override;
    virtual IOReturn getMaxPacketSize(UInt32 *maxSize) const override;
    virtual bool     configureInterface(IONetworkInterface *) override;
    virtual UInt32   outputPacket(mbuf_t m, void *param)    override;

    /* Power management */
    virtual IOReturn registerWithPolicyMaker(IOService *policyMaker) override;
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal,
                                   IOService *whatDevice)            override;

private:
    IOReturn startIfNeeded(IONetworkInterface *netif);
    IOReturn stopIfRunning(IONetworkInterface *netif);

    /* HAL selection and probe */
    RtlHalService *probeHardware(IOPCIDevice *pciDev);

    /* Interrupt handling */
    static void interruptOccurred(OSObject *owner,
                                  IOInterruptEventSource *src,
                                  int count);
    void        handleInterrupt();

    /* Watchdog timer */
    static void watchdogTimeout(OSObject *owner,
                                IOTimerEventSource *sender);
    void        handleWatchdog();

    /* Private members */
    IOWorkLoop              *workLoop       {nullptr};
    IOCommandGate           *commandGate    {nullptr};
    IOInterruptEventSource  *intrSource     {nullptr};
    IOTimerEventSource      *watchdogTimer  {nullptr};
    IOPCIDevice             *pciDevice      {nullptr};
    IOEthernetInterface     *netInterface   {nullptr};
    IOGatedOutputQueue      *outputQueue    {nullptr};
    RtlHalService           *halService     {nullptr};

    bool   ifEnabled        {false};
    bool   ifRunning        {false};
    bool   pmRestoreOnWake  {false};
    UInt32 powerState       {0};
};

#endif /* rtlwm_hpp */
