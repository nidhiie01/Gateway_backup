#pragma once

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "../MacroConfig/MacroConfig.h"
#include "../NetworkManager/network_manager.h"

#if ENABLE_SPI_ETH

extern "C"
{
#include "esp_eth_enc28j60.h"
#include "esp_log.h"
}

class SpiEthernetModule
{
public:
    SpiEthernetModule();
    ~SpiEthernetModule();

    // Initialize SPI Ethernet and return the netif pointer
    esp_err_t init();

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
    spi_device_handle_t spi_handle_;
};

#endif // ENABLE_SPI_ETH
