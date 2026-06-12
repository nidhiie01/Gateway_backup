#pragma once

#include "esp_err.h"
#include "../MacroConfig/MacroConfig.h"
#include "../NetworkManager/network_manager.h"

 #if ENABLE_GSM

struct PdpContext
{
};

#include "esp_modem_api.h"
#include "esp_netif_ppp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "driver/i2c.h"
#include "esp_netif.h"

// enum for GSM signal quality
typedef enum
{
    GSM_SIGNAL_NONE = -1,
    GSM_SIGNAL_POOR = 0,
    GSM_SIGNAL_FAIR = 1,
    GSM_SIGNAL_GOOD = 2,
    GSM_SIGNAL_EXCELLENT = 3,
    GSM_SIGNAL_OUTSTANDING = 4
} gsm_signal_quality_t;

class GsmModule
{
public:
    GsmModule();
    ~GsmModule();

    int gsm_signal_rssi = 0;
    int gsm_signal_ber = 0;
 
    esp_modem_dce_t *g_gsm_dce = nullptr;

    void gsm_display_signal_quality();

    static void gsm_init_task(void *pvParameters);

    esp_err_t init();
    esp_err_t deinit();

    EventGroupHandle_t getEventGroup() const { return event_group_; }
    gsm_signal_quality_t get_signal_quality();
    void reset_signal_quality();

    static void on_ppp_changed(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

    esp_netif_t *getPppNetif() const { return ppp_netif; }

private:
    static const char *TAG;
    uint8_t current_state_;
    EventGroupHandle_t event_group_;
    esp_modem_dce_t *dce_;
    esp_netif_t *ppp_netif;

    esp_err_t pcf8574_write(uint8_t data);
    esp_err_t pcf8574_set_pin(uint8_t pin, bool state);
    esp_err_t hard_reset();
    void ppp_stop_safe(esp_netif_t *ppp_netif);
};

#endif // ENABLE_GSM


