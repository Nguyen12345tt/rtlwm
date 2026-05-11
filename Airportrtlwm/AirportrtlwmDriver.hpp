#pragma once

#include "Airportrtlwm.hpp"

namespace airportrtlwm {

class AirportrtlwmDriver {
public:
    AirportrtlwmDriver(MacOsBackend& backend, LinuxPortedCore& core);

    DriverResult probeAndInitialize(const PciDeviceId& id);
    DriverResult start();
    void stop();

    DriverResult scan();
    DriverResult associate();
    DriverResult recover();

    DriverState state() const;

private:
    Rtl8822ceDriver driver_;
};

} // namespace airportrtlwm
