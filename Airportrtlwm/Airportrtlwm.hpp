#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace airportrtlwm {

enum class DriverState : std::uint8_t {
    kUninitialized = 0,
    kProbed,
    kInitialized,
    kRunning,
    kStopped,
    kRecovering,
};

enum class DriverResult : std::uint8_t {
    kOk = 0,
    kInvalidState,
    kUnsupportedDevice,
    kBackendFailure,
    kFirmwareFailure,
    kCoreFailure,
};

struct PciDeviceId {
    std::uint16_t vendor;
    std::uint16_t device;
};

struct DmaRegion {
    std::uintptr_t physicalAddress;
    std::size_t size;
};

class MacOsBackend {
public:
    virtual ~MacOsBackend() = default;

    virtual DriverResult mapRegisters() = 0;
    virtual void unmapRegisters() = 0;

    virtual DriverResult setupInterrupts() = 0;
    virtual void teardownInterrupts() = 0;

    virtual DriverResult allocateRings() = 0;
    virtual void releaseRings() = 0;

    virtual DriverResult loadFirmwareImage(const char* firmwareName) = 0;
    virtual DriverResult setPowerEnabled(bool enabled) = 0;
};

class LinuxPortedCore {
public:
    virtual ~LinuxPortedCore() = default;

    virtual DriverResult initializeMacPhy() = 0;
    virtual DriverResult bringUpRadio() = 0;
    virtual DriverResult startDataPath() = 0;
    virtual void stopDataPath() = 0;

    virtual DriverResult startScan() = 0;
    virtual DriverResult associate() = 0;
    virtual DriverResult recoverAfterReset() = 0;
};

class Rtl8822ceDriver {
public:
    static constexpr std::uint16_t kRealtekVendorId = 0x10EC;
    static constexpr std::array<PciDeviceId, 1> kSupportedDevices = {{
        {kRealtekVendorId, 0xC822},
    }};

    Rtl8822ceDriver(MacOsBackend& backend, LinuxPortedCore& core);

    DriverResult probe(const PciDeviceId& id);
    DriverResult initialize();
    DriverResult start();
    void stop();

    DriverResult scan();
    DriverResult associate();

    DriverResult handleFatalErrorAndRecover();

    DriverState state() const;

private:
    bool isSupportedDevice(const PciDeviceId& id) const;

    MacOsBackend& backend_;
    LinuxPortedCore& core_;
    DriverState state_;
};

} // namespace airportrtlwm
