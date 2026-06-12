#include "WiFiModule.h"

#if ENABLE_WIFI

const char *WiFiModule::TAG = "wifi";
EventGroupHandle_t WiFiModule::wifi_event_group_ = nullptr;

WiFiModule::WiFiModule() : wifi_netif_(nullptr) {}

WiFiModule::~WiFiModule()
{
    if (wifi_event_group_)
    {
        vEventGroupDelete(wifi_event_group_);
    }
    if (wifi_netif_)
    {
        esp_netif_destroy(wifi_netif_);
    }
}

static const int WIFI_RETRY_ATTEMPT = 3;
static int wifi_retry_count = 0;

wifi_signal_quality_t wifi_signal_quality_enum = WIFI_SIGNAL_NONE;

void WiFiModule::wifi_display_signal_quality()
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
        wifi_signal_rssi = ap_info.rssi;
    }
    // Map RSSI (-90 to -30) to percentage (0 to 100)
    int percent = 0;
    if (wifi_signal_rssi >= -30)
        percent = 100;
    else if (wifi_signal_rssi <= -90)
        percent = 0;
    else
        percent = (wifi_signal_rssi + 90) * 100 / 60;

    // Set enum value based on percentage
    if (percent == 0)
    {
        wifi_signal_quality_enum = WIFI_SIGNAL_NONE;
    }
    else if (percent <= 25)
    {
        wifi_signal_quality_enum = WIFI_SIGNAL_POOR;
    }
    else if (percent <= 51)
    {
        wifi_signal_quality_enum = WIFI_SIGNAL_FAIR;
    }
    else if (percent <= 77)
    {
        wifi_signal_quality_enum = WIFI_SIGNAL_GOOD;
    }
    else
    {
        wifi_signal_quality_enum = WIFI_SIGNAL_EXCELLENT;
    }
    ESP_LOGI(TAG, "Wi-Fi Signal Quality: RSSI = %d dBm (%d%%), ENUM = %d", wifi_signal_rssi, percent, wifi_signal_quality_enum);
}

wifi_signal_quality_t WiFiModule::get_signal_quality()
{

    if (force_signal_none)
    {
        return WIFI_SIGNAL_NONE; // -1
    }
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
        int rssi = ap_info.rssi;
        // Map RSSI (-90 to -30) to percentage (0 to 100)
        int percent = 0;
        if (rssi >= -30)
            percent = 100;
        else if (rssi <= -90)
            percent = 0;
        else
            percent = (rssi + 90) * 100 / 60;

        // Decide enum based on percentage

        if (percent == 0)
        {
            return WIFI_SIGNAL_NONE;
        }
        else if (percent <= 25)
        {
            return WIFI_SIGNAL_POOR;
        }
        else if (percent <= 51)
        {
            return WIFI_SIGNAL_FAIR;
        }
        else if (percent <= 77)
        {
            return WIFI_SIGNAL_GOOD;
        }
        else
        {
            return WIFI_SIGNAL_EXCELLENT;
        }
    }
    else
    {
        // If not connected / no info
        return WIFI_SIGNAL_NONE;
    }
}

esp_err_t WiFiModule::init()
{
    // Create event group
    wifi_event_group_ = xEventGroupCreate();
    if (!wifi_event_group_)
    {
        ESP_LOGE(TAG, "Failed to create Wi-Fi event group");
        return ESP_FAIL;
    }

    // Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // Create default Wi-Fi station network interface
    wifi_netif_ = esp_netif_create_default_wifi_sta();
    if (!wifi_netif_)
    {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi netif");
        vEventGroupDelete(wifi_event_group_);
        return ESP_FAIL;
    }

    NetworkManager::get_instance()->registerInterface(wifi_netif_, NETWORK_INTERFACE_WIFI, NetworkManager::map_iface_name.at(NETWORK_INTERFACE_WIFI));

    // Configure Wi-Fi station
    esp_err_t ret = configure_wifi_station();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure Wi-Fi station: %s", esp_err_to_name(ret));
        esp_netif_destroy(wifi_netif_);
        vEventGroupDelete(wifi_event_group_);
        return ret;
    }
    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this, nullptr));
    // Start Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

esp_err_t WiFiModule::configure_wifi_station()
{
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTHMODE;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

#if USE_STATIC_IP_FOR_WIFI
    return configure_static_ip(wifi_netif_);
#else
    ESP_LOGI(TAG, "Wi-Fi configured to use DHCP");
    return ESP_OK;
#endif
}

esp_err_t WiFiModule::configure_static_ip(esp_netif_t *netif)
{
    esp_netif_dhcpc_stop(netif);
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = inet_addr(WIFI_STATIC_IP);
    ip_info.netmask.addr = inet_addr(WIFI_STATIC_MASK);
    ip_info.gw.addr = inet_addr(WIFI_STATIC_GW);
    esp_err_t ret = esp_netif_set_ip_info(netif, &ip_info);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set static IP for Wi-Fi: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Wi-Fi configured with static IP: %s", WIFI_STATIC_IP);
    return ESP_OK;
}

void WiFiModule::lookForWiFiAgain()
{
    wifi_retry_count = 0;
    esp_wifi_connect();
}

void WiFiModule::event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    // WiFiModule *wifi_manager = static_cast<WiFiModule *>(arg);
    if (event_base == WIFI_EVENT)
    {
        ESP_LOGW(TAG, ">>>>> event_id: %d ", (int)event_id);

        // int wifi_index;
        switch (event_id)
        {

        case (WIFI_EVENT_WIFI_READY):
            ESP_LOGI(TAG, "Wi-Fi ready");
            break;
        case (WIFI_EVENT_SCAN_DONE):
            ESP_LOGI(TAG, "Wi-Fi scan done");
            break;
        case (WIFI_EVENT_STA_START):
            ESP_LOGI(TAG, "Wi-Fi started, connecting to AP...");
            esp_wifi_connect();
            break;
        case (WIFI_EVENT_STA_STOP):
            ESP_LOGI(TAG, "Wi-Fi stopped");
            break;
        case (WIFI_EVENT_STA_CONNECTED):
            LedController::get_instance()->setPattern(P1, steady_on); // Steady on Wi-Fi LED
            LedController::get_instance()->setDelay(NORMAL_DELAY);
            ESP_LOGI(TAG, "Wi-Fi connected");
            break;
        case (WIFI_EVENT_STA_DISCONNECTED):
            LedController::get_instance()->setPattern(P1, blink2000); // Blink Wi-Fi LED on 2 seconds interval
            LedController::get_instance()->setDelay(DEFAULT_DELAY);
            LedController::get_instance()->setPattern(P2, steady_on);  // red led
            LedController::get_instance()->setPattern(P3, steady_off); // blue ledF
            ESP_LOGI(TAG, "Wi-Fi disconnected");
            if (wifi_retry_count < WIFI_RETRY_ATTEMPT)
            {
                ESP_LOGI(TAG, "Retrying to connect to Wi-Fi network...");
                esp_wifi_connect();
                wifi_retry_count++;
            }
            else
            {
                ESP_LOGI(TAG, "Failed to connect to Wi-Fi network");
                xEventGroupSetBits(wifi_event_group_, WIFI_FAIL_BIT);
                NetworkManager::get_instance()->setInternetConnected(NETWORK_INTERFACE_WIFI, false);
            }
            break;
        case (WIFI_EVENT_STA_AUTHMODE_CHANGE):
            ESP_LOGI(TAG, "Wi-Fi authmode changed");
            break;

        default:
            break;
        }
    }
}

#endif // ENABLE_WIFI
