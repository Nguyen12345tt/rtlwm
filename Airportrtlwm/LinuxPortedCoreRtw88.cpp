#include "LinuxPortedCoreRtw88.hpp"

namespace airportrtlwm {

DriverResult LinuxPortedCoreRtw88::initializeMacPhy() {
    macPhyReady_ = true;
    return DriverResult::kOk;
}

DriverResult LinuxPortedCoreRtw88::bringUpRadio() {
    if (!macPhyReady_) {
        return DriverResult::kCoreFailure;
    }

    radioUp_ = true;
    return DriverResult::kOk;
}

DriverResult LinuxPortedCoreRtw88::startDataPath() {
    if (!radioUp_) {
        return DriverResult::kCoreFailure;
    }

    dataPathUp_ = true;
    return DriverResult::kOk;
}

void LinuxPortedCoreRtw88::stopDataPath() {
    dataPathUp_ = false;
}

DriverResult LinuxPortedCoreRtw88::startScan() {
    return dataPathUp_ ? DriverResult::kOk : DriverResult::kInvalidState;
}

DriverResult LinuxPortedCoreRtw88::associate() {
    return dataPathUp_ ? DriverResult::kOk : DriverResult::kInvalidState;
}

DriverResult LinuxPortedCoreRtw88::recoverAfterReset() {
    if (!macPhyReady_) {
        return DriverResult::kCoreFailure;
    }

    radioUp_ = true;
    dataPathUp_ = false;
    return DriverResult::kOk;
}

} // namespace airportrtlwm
