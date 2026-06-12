
#include "GSMModule.h"

#if ENABLE_GSM

const char *GsmModule::TAG = "gsm";

// int gsm_index = NetworkManager::get_instance()->getGsmIndex();

GsmModule::GsmModule() : current_state_(0xFF), event_group_(nullptr), dce_(nullptr)
{
    event_group_ = xEventGroupCreate();
    if (!event_group_)
    {
        ESP_LOGE(TAG, "Failed to create GSM event group");
    }
}

GsmModule::~GsmModule()
{
    if (dce_)
    {
        esp_modem_destroy(dce_);
    }
    if (event_group_)
    {
        vEventGroupDelete(event_group_);
    }
}

static gsm_signal_quality_t gsm_signal_quality_enum = GSM_SIGNAL_NONE;
void GsmModule::reset_signal_quality()
{
    gsm_signal_rssi = -1; // mark invalid
    gsm_signal_ber = -1;
}

void GsmModule::gsm_display_signal_quality()
{
    // int rssi = 0, ber = 0;
    // if (g_gsm_dce && esp_modem_get_signal_quality(g_gsm_dce, rssi, ber) == ESP_OK)
    // {
    //     gsm_signal_rssi = rssi;
    //     gsm_signal_ber = ber;
    //     ESP_LOGW(TAG, "rssi = %d ber = %d",  gsm_signal_rssi, gsm_signal_ber);
    // }
    // if (gsm_signal_rssi == -1)
    // {
    //     return GSM_SIGNAL_NONE; // directly return -1
    // }
    // Map RSSI (0 to 31) to percentage (0 to 100)
    int percent = 0;
    if (gsm_signal_rssi >= 31)
    {
        percent = 100;
    }
    else if (gsm_signal_rssi <= 0)
    {
        percent = 0;
    }
    else
    {
        percent = (gsm_signal_rssi * 100) / 31;
    }

    // Set enum value based on percentage
    if (percent == 0)
    {
        gsm_signal_quality_enum = GSM_SIGNAL_NONE;
    }
    else if (percent <= 20)
    {
        gsm_signal_quality_enum = GSM_SIGNAL_POOR;
    }
    else if (percent <= 40)
    {
        gsm_signal_quality_enum = GSM_SIGNAL_FAIR;
    }
    else if (percent <= 60)
    {
        gsm_signal_quality_enum = GSM_SIGNAL_GOOD;
    }
    else if (percent <= 80)
    {
        gsm_signal_quality_enum = GSM_SIGNAL_EXCELLENT;
    }
    else
    {
        gsm_signal_quality_enum = GSM_SIGNAL_OUTSTANDING;
    }

    ESP_LOGI(TAG, "GSM Signal Quality: RSSI = %d (%d%%), BER = %d, ENUM = %d", gsm_signal_rssi, percent, gsm_signal_ber, gsm_signal_quality_enum);
}

gsm_signal_quality_t GsmModule::get_signal_quality()
{
    // int rssi = 0, ber = 0;
    // if (g_gsm_dce && esp_modem_get_signal_quality(g_gsm_dce, rssi, ber) == ESP_OK)
    // {
    //     gsm_signal_rssi = rssi;
    //     gsm_signal_ber = ber;
    // }
    if (gsm_signal_rssi == -1)
    {
        return GSM_SIGNAL_NONE; // directly return -1
    }
    // Map RSSI (0 to 31) to percentage (0 to 100)
    int percent = 0;
    if (gsm_signal_rssi >= 31)
        percent = 100;
    else if (gsm_signal_rssi <= 0)
        percent = 0;
    else
        percent = (gsm_signal_rssi * 100) / 31;

    // Decide enum based on percentage
    if (percent == 0)
        return GSM_SIGNAL_NONE;
    else if (percent <= 20)
        return GSM_SIGNAL_POOR;
    else if (percent <= 40)
        return GSM_SIGNAL_FAIR;
    else if (percent <= 60)
        return GSM_SIGNAL_GOOD;
    else if (percent <= 80)
        return GSM_SIGNAL_EXCELLENT;
    else
        return GSM_SIGNAL_OUTSTANDING;
}

void GsmModule::gsm_init_task(void *pvParameters)
{
    GsmModule *self = static_cast<GsmModule *>(pvParameters);
    esp_err_t ret = self->init();
    if (ret == ESP_OK && self->getPppNetif())
    {
        ESP_LOGI(TAG, "GSM initialized successfully and netif registered");
    }
    else
    {
        ESP_LOGE(TAG, "GSM initialization or connection failed");
        // Optional: Add LED error pattern here if desired, similar to other modules.
    }
    vTaskDelete(NULL);
    // return;
}

esp_err_t GsmModule::init()
{
    esp_err_t ret = hard_reset();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "GSM hard reset failed");
        return ret;
    }

    // Wait for modem to boot
    vTaskDelay(pdMS_TO_TICKS(20000));

    esp_netif_config_t cfg_ppp = ESP_NETIF_DEFAULT_PPP();
    ppp_netif = esp_netif_new(&cfg_ppp);
    if (!ppp_netif)
    {
        ESP_LOGE(TAG, "Failed to create PPP netif");
        return ESP_FAIL;
    }

    NetworkManager::get_instance()->registerInterface(ppp_netif, NETWORK_INTERFACE_GSM, NetworkManager::map_iface_name.at(NETWORK_INTERFACE_GSM));
    // xEventGroupSetBits(NetworkManager::get_instance()->getReadyEventGroup(), GSM_NETIF_READY_BIT);
    // Optionally set default netif early (helps route control)
    esp_netif_set_default_netif(ppp_netif);

#if USE_STATIC_IP_FOR__GSM
    esp_netif_dhcpc_stop(esp_netif);
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = inet_addr(GSM_STATIC_IP);
    ip_info.netmask.addr = inet_addr(GSM_STATIC_MASK);
    ip_info.gw.addr = inet_addr(GSM_STATIC_GW);
    ret = esp_netif_set_ip_info(esp_netif, &ip_info);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set static IP for GSM: %s", esp_err_to_name(ret));
        esp_netif_destroy(esp_netif);
        return ret;
    }
    ESP_LOGI(TAG, "GSM configured with static IP: %s", GSM_STATIC_IP);
#else
    ESP_LOGI(TAG, "GSM configured to use DHCP");
#endif

    // Configure DTE & DCE, create modem DCE
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.tx_io_num = CONFIG_EXAMPLE_MODEM_UART_TX_PIN;
    dte_config.uart_config.rx_io_num = CONFIG_EXAMPLE_MODEM_UART_RX_PIN;
    dte_config.uart_config.rts_io_num = CONFIG_EXAMPLE_MODEM_UART_RTS_PIN;
    dte_config.uart_config.cts_io_num = CONFIG_EXAMPLE_MODEM_UART_CTS_PIN;
    // dte_config.uart_config.flow_control = EXAMPLE_FLOW_CONTROL;
    dte_config.uart_config.flow_control = ESP_MODEM_FLOW_CONTROL_NONE; // or ESP_MODEM_FLOW_CONTROL_HW / SW

    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(MODEM_PPP_APN);

    dce_ = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dte_config, &dce_config, ppp_netif);
    if (!dce_)
    {
        ESP_LOGE(TAG, "Failed to create modem DCE");
        // xEventGroupClearBits(NetworkManager::get_instance()->getReadyEventGroup(), GSM_NETIF_READY_BIT);
        esp_netif_destroy(ppp_netif);
        return ESP_FAIL;
    }

    g_gsm_dce = dce_; // for global access

#if CONFIG_EXAMPLE_NEED_SIM_PIN == 1
    if (esp_modem_set_pin(dce_, CONFIG_EXAMPLE_SIM_PIN) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set SIM PIN");
        esp_modem_destroy(dce_);
        dce_ = nullptr;
        // xEventGroupClearBits(NetworkManager::get_instance()->getReadyEventGroup(), GSM_NETIF_READY_BIT);
        esp_netif_destroy(ppp_netif);
        return ESP_FAIL;
    }
#endif

    if (dte_config.uart_config.flow_control == ESP_MODEM_FLOW_CONTROL_HW)
    {
        esp_err_t err = esp_modem_set_flow_control(dce_, 2, 2);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set flow control");
            esp_modem_destroy(dce_);
            dce_ = nullptr;
            // xEventGroupClearBits(NetworkManager::get_instance()->getReadyEventGroup(), GSM_NETIF_READY_BIT);
            esp_netif_destroy(ppp_netif);
            return err;
        }
    }

    // get signal quality (optional)
    int rssi = 0, ber = 0;
    if (esp_modem_get_signal_quality(dce_, &rssi, &ber) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get signal quality");
        esp_modem_destroy(dce_);
        dce_ = nullptr;
        // xEventGroupClearBits(NetworkManager::get_instance()->getReadyEventGroup(), GSM_NETIF_READY_BIT);
        esp_netif_destroy(ppp_netif);
        return ESP_FAIL;
    }
    gsm_signal_rssi = rssi;
    gsm_signal_ber = ber;
    ESP_LOGI(TAG, "Initial Signal Quality: RSSI=%d, BER=%d", gsm_signal_rssi, gsm_signal_ber);

    if (esp_modem_set_mode(dce_, ESP_MODEM_MODE_DATA) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set modem to data mode");
        ppp_stop_safe(ppp_netif);
        esp_modem_destroy(dce_);
        dce_ = nullptr;
        // xEventGroupClearBits(NetworkManager::get_instance()->getReadyEventGroup(), GSM_NETIF_READY_BIT);
        esp_netif_destroy(ppp_netif);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Waiting for IP address");
    if(!event_group_)
    {
        ESP_LOGE(TAG, "Event group is null");
        return ESP_FAIL;
    }
    xEventGroupClearBits(event_group_, CONNECT_BIT);
    EventBits_t bits = xEventGroupWaitBits(event_group_, CONNECT_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(20000));
    if (bits & CONNECT_BIT)
    {
        ESP_LOGI(TAG, "PPP Connected successfully!");
        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to get IP address");
        gsm_signal_rssi = -1;
        gsm_signal_ber = -1;
        ESP_LOGW(TAG, "GSM signal quality reset to -1 (Failed to get IP)");
        ppp_stop_safe(ppp_netif);
        esp_modem_destroy(dce_);
        dce_ = nullptr;
        // xEventGroupClearBits(NetworkManager::get_instance()->getReadyEventGroup(), GSM_NETIF_READY_BIT);
        esp_netif_destroy(ppp_netif);
        xEventGroupClearBits(event_group_, CONNECT_BIT);
        return ESP_FAIL;
    }
}

esp_err_t GsmModule::pcf8574_write(uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCF8574_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK)
    {
        current_state_ = data;
    }
    else
    {
        ESP_LOGE(TAG, "PCF8574 write failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t GsmModule::pcf8574_set_pin(uint8_t pin, bool state)
{
    if (pin > 7)
    {
        ESP_LOGE(TAG, "Invalid pin number: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t new_state = current_state_;
    if (state)
    {
        new_state |= (1 << pin);
    }
    else
    {
        new_state &= ~(1 << pin);
    }
    return pcf8574_write(new_state);
}

esp_err_t GsmModule::hard_reset()
{
    ESP_LOGI(TAG, "Performing hard reset of SIM7600 modem...");
    // Set powerkey pin(P5) low
    esp_err_t ret = pcf8574_set_pin(5, false);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set P5 low");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    // Set powerkey pin(P5) high
    ret = pcf8574_set_pin(5, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set P5 high");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Set reset pin(P6) low
    ret = pcf8574_set_pin(6, false);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set P6 low");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1500));
    // Set reset pin(P6) high
    ret = pcf8574_set_pin(6, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set P6 high");
        return ret;
    }
    ESP_LOGI(TAG, "SIM7600 hard reset completed.");
    return ESP_OK;
}

void GsmModule::ppp_stop_safe(esp_netif_t *ppp_netif)
{
    if (!ppp_netif)
        return;
    esp_netif_action_stop(ppp_netif, nullptr, 0, nullptr);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void GsmModule::on_ppp_changed(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "PPP state changed event %" PRIu32, (uint32_t)event_id);
    if (event_id == NETIF_PPP_ERRORCONNECT || event_id == NETIF_PPP_ERRORUSER)
        if (event_id == NETIF_PPP_ERRORUSER)
        {
            NetworkManager::get_instance()->setInternetConnected(NETWORK_INTERFACE_GSM, false);
            NetworkManager::get_instance()->checkAndSetInterface();
        }
}

esp_err_t GsmModule::deinit()
{
    ESP_LOGI(TAG, "Deinitializing GSM module...");

    // Stop PPP safely if netif exists
    if (ppp_netif)
    {
        ppp_stop_safe(ppp_netif);
    }

    // Destroy modem DCE
    if (dce_)
    {
        esp_modem_destroy(dce_);
        dce_ = nullptr;
        g_gsm_dce = nullptr;
    }

    // Destroy PPP netif
    if (ppp_netif)
    {
        esp_netif_destroy(ppp_netif);
        ppp_netif = nullptr;
    }

    // Delete event group
    if (event_group_)
    {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }

    // Reset signal quality
    gsm_signal_rssi = -1;
    gsm_signal_ber = -1;

    ESP_LOGI(TAG, "GSM module deinit complete");
    return ESP_OK;
}


#endif // ENABLE_GSM
