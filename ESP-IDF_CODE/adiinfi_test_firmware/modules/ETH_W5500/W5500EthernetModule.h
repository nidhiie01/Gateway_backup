#pragma once

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "../MacroConfig/MacroConfig.h"
#include "../NetworkManager/network_manager.h"

extern "C"
{
#include "esp_eth_driver.h"
#include "driver/spi_master.h"
#include "esp_log.h"
}

#if ENABLE_W5500_ETH

class W5500EthernetModule
{
public:
    W5500EthernetModule();
    ~W5500EthernetModule();

    // Initialize w5500 Ethernet and return the netif pointer
    esp_err_t init();
    esp_err_t deinit();

    // Event handler (register this with esp_event)
    static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

    // Getters for resources
    esp_eth_handle_t getHandle() const { return eth_handle_; }
    esp_netif_t *getNetif() const { return netif_; }
    esp_eth_mac_t *getMac() const { return mac_; }
    esp_eth_phy_t *getPhy() const { return phy_; }

private:
    static const char *TAG;
    esp_eth_handle_t eth_handle_;
    esp_netif_t *netif_;
    esp_eth_mac_t *mac_;
    esp_eth_phy_t *phy_;
    spi_device_handle_t w5500_handle_;
};

#endif // ENABLE_W5500_ETH
