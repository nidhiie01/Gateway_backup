#include "SpiEthernetModule.h"

#if ENABLE_SPI_ETH

extern "C" {
#include "esp_eth_driver.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_system.h"
#include "esp_mac.h"
}

const char *SpiEthernetModule::TAG = "eth_spi";

SpiEthernetModule::SpiEthernetModule()
    : eth_handle_(nullptr), netif_(nullptr), mac_(nullptr), phy_(nullptr), spi_handle_(nullptr) {}

SpiEthernetModule::~SpiEthernetModule()
{
    if (eth_handle_)
    {
        esp_eth_stop(eth_handle_);
        esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler);
        esp_eth_driver_uninstall(eth_handle_);
    }
    if (netif_)
    {
        esp_netif_destroy(netif_);
    }
    if (phy_)
    {
        phy_->del(phy_);
    }
    if (mac_)
    {
        mac_->del(mac_);
    }
    if (spi_handle_)
    {
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(SPI2_HOST);
    }
}

esp_err_t SpiEthernetModule::init()
{
    esp_err_t ret;

    // Install GPIO ISR service
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure SPI device
    spi_device_interface_config_t devcfg = {};
    devcfg.command_bits = 0;
    devcfg.address_bits = 0;
    devcfg.mode = 0;
    devcfg.clock_speed_hz = SPI_CLOCK_MHZ;
    devcfg.spics_io_num = CS_GPIO;
    devcfg.queue_size = 20;

    // Add SPI device on SPI2_HOST
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    // Configure MAC
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_enc28j60_config_t enc28j60_config = ETH_ENC28J60_DEFAULT_CONFIG(SPI2_HOST, &devcfg);
    enc28j60_config.int_gpio_num = INT_NUM;
    mac_ = esp_eth_mac_new_enc28j60(&enc28j60_config, &mac_config);
    if (!mac_)
    {
        ESP_LOGE(TAG, "Failed to initialize ENC28J60 MAC");
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(SPI2_HOST);
        spi_handle_ = nullptr;
        return ESP_FAIL;
    }

    // Configure PHY
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 0;
    phy_config.autonego_timeout_ms = 0;
    phy_config.reset_gpio_num = -1;
    phy_ = esp_eth_phy_new_enc28j60(&phy_config);
    if (!phy_)
    {
        ESP_LOGE(TAG, "Failed to initialize ENC28J60 PHY");
        mac_->del(mac_);
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(SPI2_HOST);
        mac_ = nullptr;
        spi_handle_ = nullptr;
        return ESP_FAIL;
    }

    // Install Ethernet driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac_, phy_);
    ret = esp_eth_driver_install(&eth_config, &eth_handle_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI Ethernet driver install failed: %s", esp_err_to_name(ret));
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(SPI2_HOST);
        phy_ = nullptr;
        mac_ = nullptr;
        spi_handle_ = nullptr;
        return ret;
    }

    // Set MAC address
    uint8_t mac_addr[6];
    esp_read_mac(mac_addr, ESP_MAC_WIFI_STA);
    mac_addr[0] |= 0x02; // local bit
    mac_addr[5] += 1;
    ret = esp_eth_ioctl(eth_handle_, ETH_CMD_S_MAC_ADDR, mac_addr);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set ENC28J60 MAC address: %s", esp_err_to_name(ret));
        esp_eth_driver_uninstall(eth_handle_);
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(SPI2_HOST);
        eth_handle_ = nullptr;
        phy_ = nullptr;
        mac_ = nullptr;
        spi_handle_ = nullptr;
        return ret;
    }

    // Create network interface
    esp_netif_inherent_config_t netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    netif_config.if_key = "ETH_SPI";
    netif_config.if_desc = "eth_spi";
    netif_config.route_prio = 95;
    esp_netif_config_t cfg = {};
    cfg.base = &netif_config;
    cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;

    netif_ = esp_netif_new(&cfg);
    if (!netif_)
    {
        ESP_LOGE(TAG, "Failed to create SPI esp-netif");
        esp_eth_driver_uninstall(eth_handle_);
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(SPI2_HOST);
        eth_handle_ = nullptr;
        phy_ = nullptr;
        mac_ = nullptr;
        spi_handle_ = nullptr;
        return ESP_FAIL;
    }

    // Attach network interface
    ret = esp_netif_attach(netif_, esp_eth_new_netif_glue(eth_handle_));
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to attach SPI netif: %s", esp_err_to_name(ret));
        esp_netif_destroy(netif_);
        esp_eth_driver_uninstall(eth_handle_);
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(SPI2_HOST);
        netif_ = nullptr;
        eth_handle_ = nullptr;
        phy_ = nullptr;
        mac_ = nullptr;
        spi_handle_ = nullptr;
        return ret;
    }

    NetworkManager::get_instance()->registerInterface(netif_, NETWORK_INTERFACE_SPI_ETHERNET,NetworkManager::map_iface_name[NETWORK_INTERFACE_SPI_ETHERNET]);

    // Configure static IP if enabled
#if USE_STATIC_IP_FOR_SPI
    esp_netif_dhcpc_stop(netif_);
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = inet_addr(SPI_ETH_STATIC_IP);
    ip_info.netmask.addr = inet_addr(SPI_ETH_STATIC_MASK);
    ip_info.gw.addr = inet_addr(SPI_ETH_STATIC_GW);
    esp_err_t ret = esp_netif_set_ip_info(netif_, &ip_info);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set static IP for SPI Ethernet: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPI Ethernet configured with static IP: %s", SPI_ETH_STATIC_IP);
#else
    ESP_LOGI(TAG, "SPI Ethernet configured to use DHCP");
#endif

    // Register event handler
    // ret = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler, this, nullptr);
    ret = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register SPI Ethernet event handler: %s", esp_err_to_name(ret));
        esp_netif_destroy(netif_);
        esp_eth_driver_uninstall(eth_handle_);
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(SPI2_HOST);
        netif_ = nullptr;
        eth_handle_ = nullptr;
        phy_ = nullptr;
        mac_ = nullptr;
        spi_handle_ = nullptr;
        return ret;
    }

    // Start Ethernet
    ret = esp_eth_start(eth_handle_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start SPI Ethernet: %s", esp_err_to_name(ret));
        esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler);
        esp_netif_destroy(netif_);
        esp_eth_driver_uninstall(eth_handle_);
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(spi_handle_);
        spi_bus_free(SPI2_HOST);
        netif_ = nullptr;
        eth_handle_ = nullptr;
        phy_ = nullptr;
        mac_ = nullptr;
        spi_handle_ = nullptr;
        return ret;
    }

    ESP_LOGI(TAG, "SPI Ethernet (ENC28J60) initialized. Pins: cs=%d int=%d rst=%d", CS_GPIO, INT_NUM, SPI_PHY_RST_GPIO);
    return ESP_OK;
}

void SpiEthernetModule::event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    SpiEthernetModule *spi_eth = static_cast<SpiEthernetModule *>(arg);
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
    if (eth_handle != spi_eth->eth_handle_)
    {
        return;
    }

    uint8_t mac_addr[ETH_ADDR_LEN] = {0};
    esp_err_t ret = esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI: Failed to get MAC address: %s", esp_err_to_name(ret));
        return;
    }

    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "SPI Ethernet Link Up");
        ESP_LOGI(TAG, "SPI Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "SPI Ethernet Link Down");
        

        NetworkManager::get_instance()->setInternetConnected(NETWORK_INTERFACE_SPI_ETHERNET, false);
        NetworkManager::get_instance()->checkAndSetInterface();
        

        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "SPI Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "SPI Ethernet Stopped");
        break;
    default:
        ESP_LOGI(TAG, "SPI Ethernet Unknown Event: %ld", event_id);
        break;
    }
}

#endif // ENABLE_SPI_ETH