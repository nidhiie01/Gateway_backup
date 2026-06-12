#ifndef MODULE_INTERNET_H
#define MODULE_INTERNET_H

#include "WiFiModule.h"
#include "LedController.h"
#include "../MacroConfig/MacroConfig.h"
#include "network_manager.h"
#include "../SDcardModule/sdcard.h"

#if ENABLE_WIFI
#include "../WiFiModule/WiFiModule.h"
#endif
#if ENABLE_GSM
#include "GSMModule.h"
#endif
#if ENABLE_RMII_ETH
#include "RmiiEthernetModule.h"
#endif
#if ENABLE_W5500_ETH
#include "W5500EthernetModule.h"
#endif
#if ENABLE_SPI_ETH
#include "SpiEthernetModule.h"
#endif

extern "C"
{
    struct PdpContext; // forward declare matches C struct type
#include "esp_netif_ppp.h"
#include "esp_modem_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "ethernet_init.h"
// #include "nvs_flash.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_eth_driver.h"
#include "esp_system.h"
#include "esp_intr_alloc.h"
}

class InternetModule
{
private:
    static InternetModule *instance_;

    static const char *TAG;
    static esp_err_t init_spi_bus(void);


#if ENABLE_WIFI
    WiFiModule *wifi_module_;
#endif
#if ENABLE_RMII_ETH
    RmiiEthernetModule *rmii_module_;
#endif
#if ENABLE_SPI_ETH
    SpiEthernetModule *spi_module_;
#endif
#if ENABLE_W5500_ETH
    W5500EthernetModule *w5500_module_;
#endif
#if ENABLE_GSM
    GsmModule *gsm_module_;
#endif
#if ENABLE_SD
    SDCard &sdcard = SDCard::getInstance();
#endif

    // Private constructor/destructor to enforce singleton
    InternetModule();
    ~InternetModule();

public:
    // Singleton access
    static InternetModule *get_instance();


    // Delete copy/assign to prevent copies
    InternetModule(const InternetModule &) = delete;
    InternetModule &operator=(const InternetModule &) = delete;

    // Public getters for the modules (const to prevent modification)
#if ENABLE_WIFI
    WiFiModule *getWifiModule() const { return wifi_module_; }
#endif
#if ENABLE_RMII_ETH
    RmiiEthernetModule *getRmiiModule() const { return rmii_module_; }
#endif
#if ENABLE_SPI_ETH
    SpiEthernetModule *getSpiModule() const { return spi_module_; }
#endif
#if ENABLE_W5500_ETH
    W5500EthernetModule *getW5500Module() const { return w5500_module_; }
#endif
#if ENABLE_GSM
    GsmModule *getGsmModule() const { return gsm_module_; }
#endif

    // InternetModule(WiFiModule *wifi);
    // InternetModule(WiFiModule *wifi = nullptr, RmiiEthernetModule *rmii = nullptr, SpiEthernetModule *spi = nullptr, GsmModule *gsm = nullptr)
    //     : wifi_module_(wifi), rmii_module_(rmii), spi_module_(spi), gsm_module_(gsm) {};
    bool init();
    bool init_w5500_module();
    bool init_enc28j60_module();
   // bool init_gsm_module();
};

#endif // MODULE_INTERNET_H
       // #endif // MODULE_INTERNET_H