#include "AirportrtlwmDriver.hpp"

namespace airportrtlwm {

AirportrtlwmDriver::AirportrtlwmDriver(MacOsBackend& backend, LinuxPortedCore& core)
    : driver_(backend, core) {}

DriverResult AirportrtlwmDriver::probeAndInitialize(const PciDeviceId& id) {
    const auto probeResult = driver_.probe(id);
    if (probeResult != DriverResult::kOk) {
        return probeResult;
    }

    return driver_.initialize();
}

DriverResult AirportrtlwmDriver::start() {
    return driver_.start();
}

void AirportrtlwmDriver::stop() {
    driver_.stop();
}

DriverResult AirportrtlwmDriver::scan() {
    return driver_.scan();
}

DriverResult AirportrtlwmDriver::associate() {
    return driver_.associate();
}

DriverResult AirportrtlwmDriver::recover() {
    return driver_.handleFatalErrorAndRecover();
}

DriverState AirportrtlwmDriver::state() const {
    return driver_.state();
}

} // namespace airportrtlwm
