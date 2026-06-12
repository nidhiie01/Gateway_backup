#include "generate_uid.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "UIDModule";

UIDModule::UIDModule() {}
UIDModule::~UIDModule() {}

std::string UIDModule::generateUID()
{
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get base MAC address!");
        return "";
    }

    // FNV-1a hash
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 6; i++)
    {
        hash ^= mac[i];
        hash *= 16777619u;
    }
    hash ^= ((uint32_t)mac[0] << 24) | ((uint32_t)mac[5]);

    // Base36 encode (8 chars)
    const char *base36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char uid[9];
    for (int i = 7; i >= 0; --i)
    {
        uid[i] = base36[hash % 36];
        hash /= 36;
    }
    uid[8] = '\0';

    ESP_LOGI(TAG, "Generated UID: %s (MAC %02X:%02X:%02X:%02X:%02X:%02X)",
             uid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return std::string(uid);
}

bool UIDModule::verifyOrStoreUID()
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("uid_storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Try to read stored UID
    char stored_uid[9] = {0};
    size_t length = sizeof(stored_uid);
    err = nvs_get_str(nvs_handle, "uid", stored_uid, &length);

    std::string generated_uid = generateUID();

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No UID found in NVS, storing new UID: %s", generated_uid.c_str());
        err = nvs_set_str(nvs_handle, "uid", generated_uid.c_str());
        if (err == ESP_OK) nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        if (err == ESP_OK)
        {
            uid_ = generated_uid;
            return true;
        }
        return false;
    }
    else if (err == ESP_OK)
    {
        if (generated_uid == stored_uid)
        {
            ESP_LOGI(TAG, "UID verified: %s", stored_uid);
            uid_ = stored_uid;
            nvs_close(nvs_handle);
            return true;
        }
        else
        {
            ESP_LOGE(TAG, "UID mismatch! Generated: %s, Stored: %s",
                     generated_uid.c_str(), stored_uid);
            nvs_close(nvs_handle);
            return false;
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read UID from NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
}

bool UIDModule::init()
{
    ESP_LOGI(TAG, "UIDModule Initializing....");
    return verifyOrStoreUID();
}

std::string UIDModule::getUID() const
{
    return uid_;
}
