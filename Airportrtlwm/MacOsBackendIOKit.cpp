#include "MacOsBackendIOKit.hpp"

#include <cstring>

namespace airportrtlwm {

DriverResult MacOsBackendIOKit::mapRegisters() {
    registersMapped_ = true;
    return DriverResult::kOk;
}

void MacOsBackendIOKit::unmapRegisters() {
    registersMapped_ = false;
}

DriverResult MacOsBackendIOKit::setupInterrupts() {
    if (!registersMapped_) {
        return DriverResult::kBackendFailure;
    }

    interruptsEnabled_ = true;
    return DriverResult::kOk;
}

void MacOsBackendIOKit::teardownInterrupts() {
    interruptsEnabled_ = false;
}

DriverResult MacOsBackendIOKit::allocateRings() {
    if (!registersMapped_) {
        return DriverResult::kBackendFailure;
    }

    ringsAllocated_ = true;
    return DriverResult::kOk;
}

void MacOsBackendIOKit::releaseRings() {
    ringsAllocated_ = false;
}

DriverResult MacOsBackendIOKit::loadFirmwareImage(const char* firmwareName) {
    if (!ringsAllocated_ || !interruptsEnabled_) {
        return DriverResult::kBackendFailure;
    }

    if (firmwareName == nullptr) {
        return DriverResult::kFirmwareFailure;
    }

    constexpr const char* kSupportedFirmware = "rtl8822ce";
    return std::strcmp(firmwareName, kSupportedFirmware) == 0
               ? DriverResult::kOk
               : DriverResult::kFirmwareFailure;
}

DriverResult MacOsBackendIOKit::setPowerEnabled(bool enabled) {
    if (enabled && (!registersMapped_ || !ringsAllocated_)) {
        return DriverResult::kBackendFailure;
    }

    powerEnabled_ = enabled;
    return DriverResult::kOk;
}

} // namespace airportrtlwm
