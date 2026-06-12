#pragma once

#include "esp_err.h"
#include "../MacroConfig/MacroConfig.h"

#if ENABLE_SD

#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"

extern bool spi_bus_initialized;

class SDCard {
public:
    // Singleton access
    static SDCard& getInstance();

    // Initialize the SD card with the specified SPI host
    esp_err_t init();

    // Write data to a file at the specified path
    esp_err_t writeFile(const char* path, const char* data);

    // Read data from a file at the specified path
    esp_err_t readFile(const char* path);

    // Check if the SD card is initialized
    bool isInitialized() const { return initialized_; }

    // Get the SD card handle
    sdmmc_card_t* getCard() const { return card_; }

private:
    SDCard();
    ~SDCard();

    // Delete copy constructor and assignment operator to enforce singleton
    SDCard(const SDCard&) = delete;
    SDCard& operator=(const SDCard&) = delete;

    // Constants
    static constexpr int SD_CS_PIN = 2;
    static constexpr const char* MOUNT_POINT = "/sdcard";
    static constexpr int EXAMPLE_MAX_CHAR_SIZE = 64;
    static constexpr const char* TAG = "sdcard";

    // Member variables
    sdmmc_card_t* card_ = nullptr;
    bool initialized_ = false;
};

#endif // ENABLE_SD

