#include "RmiiEthernetModule.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"

#if ENABLE_RMII_ETH

static const char *TAG = "RMII_ETH";

esp_eth_handle_t RmiiEthernetModule::module_eth_handle = nullptr;
esp_eth_handle_t RmiiEthernetModule::eth_handle = nullptr;

RmiiEthernetModule::RmiiEthernetModule()
{
    // Constructor (nothing heavy, keep init in init())
}

RmiiEthernetModule::~RmiiEthernetModule()
{
    if (eth_handle)
    {
        esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler);
        ESP_ERROR_CHECK(esp_eth_stop(eth_handle));
        if (netif)
        {
            esp_netif_destroy(netif);
            netif = nullptr;
        }
        // ESP_ERROR_CHECK(esp_eth_driver_uninstall(eth_handle));
        esp_err_t ret = esp_eth_driver_uninstall(eth_handle);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "esp_eth_driver_uninstall failed in destructor: %s", esp_err_to_name(ret));
        }
        phy->del(phy);
        mac->del(mac);
        phy = nullptr;
        mac = nullptr;
        module_eth_handle = nullptr;
        eth_handle = nullptr;
    }
}

void RmiiEthernetModule::event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    // event_data is pointer to esp_eth_handle_t
    RmiiEthernetModule::eth_handle = *(esp_eth_handle_t *)event_data;
    if (RmiiEthernetModule::eth_handle != RmiiEthernetModule::module_eth_handle)
        return;

    uint8_t mac_addr[ETH_ADDR_LEN] = {0};
    esp_err_t ret = esp_eth_ioctl(RmiiEthernetModule::eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "RMII: Failed to get MAC address: %s", esp_err_to_name(ret));
        return;
    }
    // int rmii_index;
    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "RMII Ethernet Link Up");
        ESP_LOGI(TAG, "RMII Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "RMII Ethernet Link Down");

        NetworkManager::get_instance()->setInternetConnected(NETWORK_INTERFACE_RMII_ETHERNET, false);
        NetworkManager::get_instance()->checkAndSetInterface();
        LedController::get_instance()->setPattern(P2, steady_on);  // red led
        LedController::get_instance()->setPattern(P3, steady_off); // blue led

        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "RMII Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "RMII Ethernet Stopped");
        break;
    default:
        ESP_LOGI(TAG, "RMII Ethernet Unknown Event: %ld", event_id);
        break;
    }
}

esp_err_t RmiiEthernetModule::init()
{
    esp_err_t ret;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_gpio.mdc_num = MDC_GPIO;
    esp32_emac_config.smi_gpio.mdio_num = MDIO_GPIO;

    mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    if (mac == nullptr)
    {
        ESP_LOGE(TAG, "Failed to initialize RMII MAC");
        return ESP_FAIL;
    }

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = PHY_ADDR;
    phy_config.reset_gpio_num = RST_GPIO;
    phy = esp_eth_phy_new_lan87xx(&phy_config);
    if (phy == nullptr)
    {
        ESP_LOGE(TAG, "Failed to initialize RMII PHY (LAN8720)");
        mac->del(mac);
        mac = nullptr;
        return ESP_FAIL;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ret = esp_eth_driver_install(&eth_config, &eth_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "RMII Ethernet driver install failed: %s", esp_err_to_name(ret));
        phy->del(phy);
        mac->del(mac);
        phy = nullptr;
        mac = nullptr;
        return ret;
    }

    // store module handle for event checking
    module_eth_handle = eth_handle;

    esp_netif_inherent_config_t netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    netif_config.if_key = "ETH_RMII";
    netif_config.if_desc = "eth_rmii";
    netif_config.route_prio = 100;
    esp_netif_config_t cfg = {};
    cfg.base = &netif_config;
    cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;

    netif = esp_netif_new(&cfg);
    if (netif == NULL)
    {
        ESP_LOGE(TAG, "Failed to create RMII esp-netif");
        esp_eth_driver_uninstall(eth_handle);
        phy->del(phy);
        mac->del(mac);
        eth_handle = NULL;
        phy = NULL;
        mac = NULL;
        return ESP_FAIL;
    }

    ret = esp_netif_attach(netif, esp_eth_new_netif_glue(eth_handle));
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to attach RMII netif: %s", esp_err_to_name(ret));
        esp_netif_destroy(netif);
        esp_eth_driver_uninstall(eth_handle);
        phy->del(phy);
        mac->del(mac);
        netif = NULL;
        eth_handle = NULL;
        phy = NULL;
        mac = NULL;
        return ret;
    }

    NetworkManager::get_instance()->registerInterface(netif, NETWORK_INTERFACE_RMII_ETHERNET, NetworkManager::map_iface_name.at(NETWORK_INTERFACE_RMII_ETHERNET));

#if USE_STATIC_IP_FOR_RMII
    esp_netif_dhcpc_stop(netif);
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = inet_addr(RMII_ETH_STATIC_IP);
    ip_info.netmask.addr = inet_addr(RMII_ETH_STATIC_MASK);
    ip_info.gw.addr = inet_addr(RMII_ETH_STATIC_GW);
    ret = esp_netif_set_ip_info(netif, &ip_info);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set static IP for RMII Ethernet: %s", esp_err_to_name(ret));
        esp_netif_destroy(netif);
        esp_eth_driver_uninstall(eth_handle);
        phy->del(phy);
        mac->del(mac);
        netif = NULL;
        eth_handle = NULL;
        phy = NULL;
        mac = NULL;
        return ret;
    }
    ESP_LOGI(TAG, "RMII Ethernet configured with static IP: %s", RMII_ETH_STATIC_IP);
#else
    ESP_LOGI(TAG, "RMII Ethernet configured to use DHCP");
#endif

    ret = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register RMII Ethernet event handler: %s", esp_err_to_name(ret));
        esp_netif_destroy(netif);
        esp_eth_driver_uninstall(eth_handle);
        phy->del(phy);
        mac->del(mac);
        netif = NULL;
        eth_handle = NULL;
        phy = NULL;
        mac = NULL;
        return ret;
    }

    ret = esp_eth_start(eth_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start RMII Ethernet: %s", esp_err_to_name(ret));
        esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler);
        esp_netif_destroy(netif);
        esp_eth_driver_uninstall(eth_handle);
        phy->del(phy);
        mac->del(mac);
        netif = NULL;
        eth_handle = NULL;
        phy = NULL;
        mac = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "RMII Ethernet (LAN8720) initialized. Pins: mdc=%d mdio=%d rst=%d", MDC_GPIO, MDIO_GPIO, RST_GPIO);

    return ESP_OK;
}

#endif // ENABLE_RMII_ETH