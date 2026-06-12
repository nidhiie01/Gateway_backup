#include "W5500EthernetModule.h"

#if ENABLE_W5500_ETH

extern "C" {
#include "esp_eth_driver.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_eth_phy.h"
#include "esp_eth_mac.h"
}

const char *W5500EthernetModule::TAG = "eth_W5500";

W5500EthernetModule::W5500EthernetModule()
    : eth_handle_(nullptr), netif_(nullptr), mac_(nullptr), phy_(nullptr), w5500_handle_(nullptr) {}

W5500EthernetModule::~W5500EthernetModule()
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
    if (w5500_handle_)
    {
        spi_bus_remove_device(w5500_handle_);
        // Note: Don't free SPI bus here as it might be shared with SD card
    }
}


esp_err_t W5500EthernetModule::init()
{
    esp_err_t ret;

    // Install GPIO ISR service
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure SPI device for W5500
    spi_device_interface_config_t devcfg = {};
    devcfg.command_bits = 16;  // W5500 uses 16-bit command
    devcfg.address_bits = 8;   // W5500 uses 8-bit address
    devcfg.mode = 0;           // SPI mode 0
    devcfg.clock_speed_hz = W5500_SPI_CLOCK_HZ;
    devcfg.spics_io_num = W5500_CS_GPIO;
    devcfg.queue_size = 20;
    devcfg.pre_cb = NULL;
    devcfg.post_cb = NULL;

    // // Add SPI device on HSPI (SPI2_HOST)
    // ret = spi_bus_add_device(W5500_SPI_HOST, &devcfg, &w5500_handle_);
    // if (ret != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
    //     return ret;
    // }
    // ESP_LOGI(TAG, "W5500 SPI device added successfully");

    
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(W5500_SPI_HOST, &devcfg);
    w5500_config.int_gpio_num = W5500_INT_GPIO;
    
   // Configure MAC - W5500 uses SPI, not MDIO/MDC
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    
    mac_ = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    if (!mac_)
    {
        ESP_LOGE(TAG, "Failed to initialize W5500 MAC");
        spi_bus_remove_device(w5500_handle_);
        w5500_handle_ = nullptr;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "W5500 MAC initialized");

    // Configure PHY (W5500 has internal PHY)
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 1;  // W5500 internal PHY address
    phy_config.reset_gpio_num = W5500_RESET_GPIO;
    
    phy_ = esp_eth_phy_new_w5500(&phy_config);
    if (!phy_)
    {
        ESP_LOGE(TAG, "Failed to initialize W5500 PHY");
        mac_->del(mac_);
        spi_bus_remove_device(w5500_handle_);
        mac_ = nullptr;
        w5500_handle_ = nullptr;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "W5500 PHY initialized");

    // Install Ethernet driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac_, phy_);
    ret = esp_eth_driver_install(&eth_config, &eth_handle_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "W5500 Ethernet driver install failed: %s", esp_err_to_name(ret));
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(w5500_handle_);
        phy_ = nullptr;
        mac_ = nullptr;
        w5500_handle_ = nullptr;
        return ret;
    }
    ESP_LOGI(TAG, "W5500 Ethernet driver installed");

    // Set MAC address
    uint8_t mac_addr[6];
    esp_read_mac(mac_addr, ESP_MAC_WIFI_STA);
    mac_addr[0] |= 0x02; // Set local bit
    mac_addr[5] += 2;    // Different from other interfaces
    ret = esp_eth_ioctl(eth_handle_, ETH_CMD_S_MAC_ADDR, mac_addr);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set W5500 MAC address: %s", esp_err_to_name(ret));
        esp_eth_driver_uninstall(eth_handle_);
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(w5500_handle_);
       // spi_bus_free(SPI2_HOST);
        eth_handle_ = nullptr;
        phy_ = nullptr;
        mac_ = nullptr;
        w5500_handle_ = nullptr;
        return ret;
    }
    ESP_LOGI(TAG, "W5500 MAC address set: %02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    // Create network interface
    esp_netif_inherent_config_t netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    netif_config.if_key = "ETH_W5500";
    netif_config.if_desc = "eth_w5500";
    netif_config.route_prio = 90;  // Higher priority than WiFi (lower number = higher priority)
    esp_netif_config_t cfg = {};
    cfg.base = &netif_config;
    cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;

    netif_ = esp_netif_new(&cfg);
    if (!netif_)
    {
        ESP_LOGE(TAG, "Failed to create W5500 esp-netif");
        esp_eth_driver_uninstall(eth_handle_);
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(w5500_handle_);
       // spi_bus_free(SPI2_HOST);
        eth_handle_ = nullptr;
        phy_ = nullptr;
        mac_ = nullptr;
        w5500_handle_ = nullptr;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "W5500 netif created");

    // Attach network interface
    ret = esp_netif_attach(netif_, esp_eth_new_netif_glue(eth_handle_));
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to attach W5500 netif: %s", esp_err_to_name(ret));
        esp_netif_destroy(netif_);
        esp_eth_driver_uninstall(eth_handle_);
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(w5500_handle_);
        //spi_bus_free(SPI2_HOST);
        netif_ = nullptr;
        eth_handle_ = nullptr;
        phy_ = nullptr;
        mac_ = nullptr;
        w5500_handle_ = nullptr;
        return ret;
    }
    ESP_LOGI(TAG, "W5500 netif attached");

    // Register with NetworkManager
    NetworkManager::get_instance()->registerInterface(netif_, NETWORK_INTERFACE_W5500_ETHERNET,NetworkManager::map_iface_name[NETWORK_INTERFACE_W5500_ETHERNET]);

    // Configure static IP if enabled
#if USE_STATIC_IP_FOR_W5500
    esp_netif_dhcpc_stop(netif_);
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = inet_addr(W5500_STATIC_IP);
    ip_info.netmask.addr = inet_addr(W5500_STATIC_MASK);
    ip_info.gw.addr = inet_addr(W5500_STATIC_GW);
    esp_err_t ret = esp_netif_set_ip_info(netif_, &ip_info);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set static IP for W5500 Ethernet: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "W5500 Ethernet configured with static IP: %s", W5500_STATIC_IP);
#else
    ESP_LOGI(TAG, "W5500 Ethernet configured to use DHCP");
#endif

  
    // Register event handler
    ret = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler, this); //this
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register W5500 Ethernet event handler: %s", esp_err_to_name(ret));
        esp_netif_destroy(netif_);
        esp_eth_driver_uninstall(eth_handle_);
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(w5500_handle_);
       // spi_bus_free(SPI2_HOST);
        netif_ = nullptr;
        eth_handle_ = nullptr;
        phy_ = nullptr;
        mac_ = nullptr;
        w5500_handle_ = nullptr;
        return ret;
    }
    ESP_LOGI(TAG, "W5500 event handler registered");

    // Start Ethernet
    ret = esp_eth_start(eth_handle_);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start W5500 Ethernet: %s", esp_err_to_name(ret));
        esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler);
        esp_netif_destroy(netif_);
        esp_eth_driver_uninstall(eth_handle_);
        phy_->del(phy_);
        mac_->del(mac_);
        spi_bus_remove_device(w5500_handle_);
        //spi_bus_free(SPI2_HOST);
        netif_ = nullptr;
        eth_handle_ = nullptr;
        phy_ = nullptr;
        mac_ = nullptr;
        w5500_handle_ = nullptr;
        return ret;
    }

    ESP_LOGI(TAG, "W5500 Ethernet initialized successfully. Pins: CS=%d INT=%d RST=%d",  W5500_CS_GPIO, W5500_INT_GPIO, W5500_RESET_GPIO);
    return ESP_OK;
}

void W5500EthernetModule::event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    W5500EthernetModule *w5500_eth = static_cast<W5500EthernetModule *>(arg);
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
    
    if (eth_handle != w5500_eth->eth_handle_)
    {
        return;
    }

    uint8_t mac_addr[ETH_ADDR_LEN] = {0};
    esp_err_t ret = esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
    if (ret != ESP_OK)
    {  
       
        ESP_LOGE(TAG, "W5500: Failed to get MAC address: %s", esp_err_to_name(ret));
        return;
    }
  
    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
    {  
        ESP_LOGI(TAG, "W5500 Ethernet Link Up");
        ESP_LOGI(TAG, "W5500 Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

        // Check if netif is up
        if (w5500_eth->netif_) {  
            bool is_up = esp_netif_is_netif_up(w5500_eth->netif_);
            ESP_LOGI(TAG, "W5500 netif is_up: %s", is_up ? "YES" : "NO");
        }
        
        break;
    } 
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "W5500 Ethernet Link Down");
        NetworkManager::get_instance()->setInternetConnected(NETWORK_INTERFACE_W5500_ETHERNET, false);
        NetworkManager::get_instance()->checkAndSetInterface();
        break;
        
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "W5500 Ethernet Started");
        break;
        
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "W5500 Ethernet Stopped");
        break;
        
    default:
        ESP_LOGI(TAG, "W5500 Ethernet Unknown Event: %ld", event_id);
        break;
    }
}

esp_err_t W5500EthernetModule::deinit()
{
    ESP_LOGI(TAG, "Deinitializing W5500 Ethernet...");

    if (eth_handle_)
    {
        esp_eth_stop(eth_handle_);
        esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler);
        esp_eth_driver_uninstall(eth_handle_);
        eth_handle_ = nullptr;
    }

    if (netif_)
    {
        esp_netif_destroy(netif_);
        netif_ = nullptr;
    }

    if (phy_)
    {
        phy_->del(phy_);
        phy_ = nullptr;
    }

    if (mac_)
    {
        mac_->del(mac_);
        mac_ = nullptr;
    }

    if (w5500_handle_)
    {
        spi_bus_remove_device(w5500_handle_);
        w5500_handle_ = nullptr;
        // SPI bus not freed because it may be shared
    }

    ESP_LOGI(TAG, "W5500 Ethernet deinit complete");
    return ESP_OK;
}



#endif // ENABLE_W5500_ETH