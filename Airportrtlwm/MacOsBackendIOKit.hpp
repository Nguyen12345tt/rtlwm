#pragma once

#include "Airportrtlwm.hpp"

namespace airportrtlwm {

class MacOsBackendIOKit final : public MacOsBackend {
public:
    DriverResult mapRegisters() override;
    void unmapRegisters() override;

    DriverResult setupInterrupts() override;
    void teardownInterrupts() override;

    DriverResult allocateRings() override;
    void releaseRings() override;

    DriverResult loadFirmwareImage(const char* firmwareName) override;
    DriverResult setPowerEnabled(bool enabled) override;

private:
    bool registersMapped_ = false;
    bool interruptsEnabled_ = false;
    bool ringsAllocated_ = false;
    bool powerEnabled_ = false;
};

} // namespace airportrtlwm
