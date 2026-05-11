#pragma once

#include "Airportrtlwm.hpp"

namespace airportrtlwm {

class LinuxPortedCoreRtw88 final : public LinuxPortedCore {
public:
    DriverResult initializeMacPhy() override;
    DriverResult bringUpRadio() override;
    DriverResult startDataPath() override;
    void stopDataPath() override;

    DriverResult startScan() override;
    DriverResult associate() override;
    DriverResult recoverAfterReset() override;

private:
    bool macPhyReady_ = false;
    bool radioUp_ = false;
    bool dataPathUp_ = false;
};

} // namespace airportrtlwm
