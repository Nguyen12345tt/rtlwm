#include "Airportrtlwm.hpp"

namespace airportrtlwm {

Rtl8822ceDriver::Rtl8822ceDriver(MacOsBackend& backend, LinuxPortedCore& core)
    : backend_(backend), core_(core), state_(DriverState::kUninitialized) {}

bool Rtl8822ceDriver::isSupportedDevice(const PciDeviceId& id) const {
    for (const auto& supported : kSupportedDevices) {
        if (supported.vendor == id.vendor && supported.device == id.device) {
            return true;
        }
    }
    return false;
}

DriverResult Rtl8822ceDriver::probe(const PciDeviceId& id) {
    if (!isSupportedDevice(id)) {
        return DriverResult::kUnsupportedDevice;
    }

    state_ = DriverState::kProbed;
    return DriverResult::kOk;
}

DriverResult Rtl8822ceDriver::initialize() {
    if (state_ != DriverState::kProbed && state_ != DriverState::kStopped) {
        return DriverResult::kInvalidState;
    }

    if (backend_.mapRegisters() != DriverResult::kOk) {
        return DriverResult::kBackendFailure;
    }

    if (backend_.allocateRings() != DriverResult::kOk) {
        backend_.unmapRegisters();
        return DriverResult::kBackendFailure;
    }

    if (backend_.setupInterrupts() != DriverResult::kOk) {
        backend_.releaseRings();
        backend_.unmapRegisters();
        return DriverResult::kBackendFailure;
    }

    if (backend_.loadFirmwareImage("rtl8822ce") != DriverResult::kOk) {
        backend_.teardownInterrupts();
        backend_.releaseRings();
        backend_.unmapRegisters();
        return DriverResult::kFirmwareFailure;
    }

    if (core_.initializeMacPhy() != DriverResult::kOk) {
        backend_.teardownInterrupts();
        backend_.releaseRings();
        backend_.unmapRegisters();
        return DriverResult::kCoreFailure;
    }

    state_ = DriverState::kInitialized;
    return DriverResult::kOk;
}

DriverResult Rtl8822ceDriver::start() {
    if (state_ != DriverState::kInitialized) {
        return DriverResult::kInvalidState;
    }

    if (backend_.setPowerEnabled(true) != DriverResult::kOk) {
        return DriverResult::kBackendFailure;
    }

    if (core_.bringUpRadio() != DriverResult::kOk) {
        return DriverResult::kCoreFailure;
    }

    if (core_.startDataPath() != DriverResult::kOk) {
        return DriverResult::kCoreFailure;
    }

    state_ = DriverState::kRunning;
    return DriverResult::kOk;
}

void Rtl8822ceDriver::stop() {
    if (state_ == DriverState::kUninitialized || state_ == DriverState::kStopped) {
        return;
    }

    core_.stopDataPath();
    backend_.setPowerEnabled(false);
    backend_.teardownInterrupts();
    backend_.releaseRings();
    backend_.unmapRegisters();

    state_ = DriverState::kStopped;
}

DriverResult Rtl8822ceDriver::scan() {
    if (state_ != DriverState::kRunning) {
        return DriverResult::kInvalidState;
    }

    return core_.startScan();
}

DriverResult Rtl8822ceDriver::associate() {
    if (state_ != DriverState::kRunning) {
        return DriverResult::kInvalidState;
    }

    return core_.associate();
}

DriverResult Rtl8822ceDriver::handleFatalErrorAndRecover() {
    if (state_ != DriverState::kRunning) {
        return DriverResult::kInvalidState;
    }

    state_ = DriverState::kRecovering;
    core_.stopDataPath();

    const auto recoverResult = core_.recoverAfterReset();
    if (recoverResult != DriverResult::kOk) {
        stop();
        return recoverResult;
    }

    const auto dataPathResult = core_.startDataPath();
    if (dataPathResult != DriverResult::kOk) {
        stop();
        return dataPathResult;
    }

    state_ = DriverState::kRunning;
    return DriverResult::kOk;
}

DriverState Rtl8822ceDriver::state() const {
    return state_;
}

} // namespace airportrtlwm
