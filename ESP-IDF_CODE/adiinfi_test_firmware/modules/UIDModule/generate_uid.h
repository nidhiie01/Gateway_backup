#pragma once

#include <string>
#include "esp_err.h"
#include "esp_mac.h"


class UIDModule
{
public:
    UIDModule();
    ~UIDModule();

    // Initialize UID system: generate/verify/store in NVS
    bool init();

    // Get UID string
    std::string getUID() const;

private:
    // Generate UID from base MAC
    std::string generateUID();

    // Verify UID with stored one, or store if not found
    bool verifyOrStoreUID();

    std::string uid_;
};
