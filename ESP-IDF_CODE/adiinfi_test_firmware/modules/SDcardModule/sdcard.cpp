#include "sdcard.h"

#if ENABLE_SD

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

SDCard::SDCard() = default;

SDCard::~SDCard() {
    if (initialized_ && card_) {
        esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card_);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SD card unmounted from %s", MOUNT_POINT);
        } else {
            ESP_LOGE(TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
        }
        card_ = nullptr;
        initialized_ = false;
    }
}

SDCard& SDCard::getInstance() {
    static SDCard instance;
    return instance;
}

esp_err_t SDCard::init() {
    if (initialized_) {
        ESP_LOGW(TAG, "SD card already initialized");
        return ESP_OK;
    }

    if (!spi_bus_initialized) {
        ESP_LOGE(TAG, "HSPI bus not initialized for SD card");
        return ESP_FAIL;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = HSPI_HOST;
    host.max_freq_khz = 4000;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = static_cast<gpio_num_t>(SD_CS_PIN);
    slot_config.host_id = HSPI_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        return ret;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "SD card mounted at %s", MOUNT_POINT);
    return ESP_OK;
}

esp_err_t SDCard::writeFile(const char* path, const char* data) {
    if (!initialized_ || !card_) {
        ESP_LOGE(TAG, "SD card not initialized");
        return ESP_FAIL;
    }
    if (!path || !data) {
        ESP_LOGE(TAG, "Invalid path or data");
        return ESP_ERR_INVALID_ARG;
    }

    FILE* f = std::fopen(path, "w");
    if (f == nullptr) {
        ESP_LOGE(TAG, "Failed to open file %s for writing: %s", path, strerror(errno));
        return ESP_FAIL;
    }

    if (std::fprintf(f, "%s", data) < 0) {
        ESP_LOGE(TAG, "Failed to write to file %s: %s", path, strerror(errno));
        std::fclose(f);
        return ESP_FAIL;
    }

    std::fclose(f);
    ESP_LOGI(TAG, "File %s written", path);
    return ESP_OK;
}

esp_err_t SDCard::readFile(const char* path) {
    if (!initialized_ || !card_) {
        ESP_LOGE(TAG, "SD card not initialized");
        return ESP_FAIL;
    }
    if (!path) {
        ESP_LOGE(TAG, "Invalid path");
        return ESP_ERR_INVALID_ARG;
    }

    FILE* f = std::fopen(path, "r");
    if (f == nullptr) {
        ESP_LOGE(TAG, "Failed to open file %s for reading: %s", path, strerror(errno));
        return ESP_FAIL;
    }

    char line[EXAMPLE_MAX_CHAR_SIZE];
    if (std::fgets(line, sizeof(line), f) == nullptr) {
        ESP_LOGE(TAG, "Failed to read from file %s: %s", path, strerror(errno));
        std::fclose(f);
        return ESP_FAIL;
    }

    std::fclose(f);
    char* pos = std::strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file %s: '%s'", path, line);
    return ESP_OK;
}

#endif // ENABLE_SD


