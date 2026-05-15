#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>

class rtlwm : public IOService {
    OSDeclareDefaultStructors(rtlwm)

public:
    bool start(IOService *provider) override {
        if (!super::start(provider)) {
            return false;
        }

        IOLog("rtlwm stub started\\n");
        return true;
    }

    void stop(IOService *provider) override {
        IOLog("rtlwm stub stopped\\n");
        super::stop(provider);
    }
};

OSDefineMetaClassAndStructors(rtlwm, IOService)
