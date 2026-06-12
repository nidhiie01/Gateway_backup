#pragma once

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "../MacroConfig/MacroConfig.h"
#include "../NetworkManager/network_manager.h"
#include <arpa/inet.h>
// #include "InternetModule.h"
#if ENABLE_RMII_ETH

class RmiiEthernetModule
{
public:
    RmiiEthernetModule();
    ~RmiiEthernetModule();

    // Initialize RMII ethernet (LAN8720)
    esp_err_t init();

    // Event handler (register this with esp_event)
    static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

    // Getters for resources
    esp_eth_handle_t getHandle() const { return eth_handle; }
    esp_netif_t *getNetif() const { return netif; }
    esp_eth_mac_t *getMac() const { return mac; }
    esp_eth_phy_t *getPhy() const { return phy; }

private:
    static esp_eth_handle_t eth_handle;
    esp_netif_t *netif = nullptr;
    esp_eth_mac_t *mac = nullptr;
    esp_eth_phy_t *phy = nullptr;
    static esp_eth_handle_t module_eth_handle;
    bool is_connected = false;
};

#endif // ENABLE_RMII_ETH