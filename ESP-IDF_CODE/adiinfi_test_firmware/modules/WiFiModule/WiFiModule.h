#pragma once

#include "esp_err.h"
#include <arpa/inet.h>
#include "../MacroConfig/MacroConfig.h"
#include "../NetworkManager/network_manager.h"
#include "LedController.h"

#if ENABLE_WIFI
typedef enum
{
    WIFI_SIGNAL_NONE = -1,
    WIFI_SIGNAL_POOR = 0,
    WIFI_SIGNAL_FAIR = 1,
    WIFI_SIGNAL_GOOD = 2,
    WIFI_SIGNAL_EXCELLENT = 3
} wifi_signal_quality_t;

extern "C"
{
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi_default.h"
#include <cstring>
}

class WiFiModule
{
public:
    WiFiModule();
    ~WiFiModule();

    // Initialize Wi-Fi and return the netif pointer via out parameter
    esp_err_t init();
    int wifi_signal_rssi = 0;
    bool force_signal_none = false;
    void reset_signal_quality()
    {
        force_signal_none = true;
    }
    void restore_signal_quality()
    {
        force_signal_none = false;
    }
    // Getter for the Wi-Fi event group
    // EventGroupHandle_t getEventGroup() const { return wifi_event_group_; }
    static EventGroupHandle_t wifi_event_group_;
    esp_netif_t *getNetif() const { return wifi_netif_; }
    void wifi_display_signal_quality();

    wifi_signal_quality_t get_signal_quality();
    void lookForWiFiAgain();

private:
    static const char *TAG;

    esp_netif_t *wifi_netif_;
    static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
    esp_err_t configure_static_ip(esp_netif_t *netif);
    esp_err_t configure_wifi_station();
};

#endif // ENABLE_WIFI
