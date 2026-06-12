#include "InternetModule.h"
#include <esp_log.h>

const char *InternetModule::TAG = "InternetModule";

InternetModule *InternetModule::instance_ = nullptr;


// SPI/SD global state (accessible from sdcard.cpp)
bool spi_bus_initialized = false;

InternetModule *InternetModule::get_instance()
{
    if (!instance_)
    {
        instance_ = new InternetModule();
    }

    return instance_;
}

InternetModule::InternetModule()
{

    ESP_LOGI(TAG, "InternetModule constructed");
#if ENABLE_WIFI
    wifi_module_ = new WiFiModule(); // Allocate here (or in init() if preferred)
#endif
#if ENABLE_RMII_ETH
    rmii_module_ = new RmiiEthernetModule();
#endif
#if ENABLE_SPI_ETH
    spi_module_ = new SpiEthernetModule();
#endif
#if ENABLE_W5500_ETH
    w5500_module_ = new W5500EthernetModule();
#endif
#if ENABLE_GSM
    gsm_module_ = new GsmModule();
#endif
}

InternetModule::~InternetModule()
{
    // Clean up allocated modules
#if ENABLE_WIFI
    delete wifi_module_;
#endif
#if ENABLE_RMII_ETH
    delete rmii_module_;
#endif
#if ENABLE_SPI_ETH
    delete spi_module_;
#endif
#if ENABLE_W5500_ETH
    delete w5500_module_;
#endif
 #if ENABLE_GSM
    delete gsm_module_;
 #endif
}


#if ENABLE_SD || ENABLE_SPI_ETH || ENABLE_W5500_ETH
// Pins for SPI bus
constexpr int PIN_NUM_MOSI = 13;
constexpr int PIN_NUM_MISO = 12;
constexpr int PIN_NUM_CLK = 14;

// Function to initialize the SPI bus (used both by SPI ethernet and SD)
esp_err_t InternetModule::init_spi_bus(void)
{
    if (spi_bus_initialized)
    {
        return ESP_OK;
    }
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = PIN_NUM_MOSI;
    bus_cfg.miso_io_num = PIN_NUM_MISO;
    bus_cfg.sclk_io_num = PIN_NUM_CLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;
    bus_cfg.data4_io_num = -1;
    bus_cfg.data5_io_num = -1;
    bus_cfg.data6_io_num = -1;
    bus_cfg.data7_io_num = -1;
    bus_cfg.data_io_default_level = 0;
    bus_cfg.flags = 0;
    bus_cfg.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
    bus_cfg.intr_flags = 0;

    esp_err_t ret = spi_bus_initialize(HSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize HSPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    spi_bus_initialized = true;
    ESP_LOGI(TAG, "HSPI bus initialized");
    return ESP_OK;
}
#endif // ENABLE_SD || ENABLE_SPI_ETH || ENABLE_W5500_ETH

bool InternetModule::init()
{
    esp_err_t ret;

    // Initialize TCP/IP stack
    ret = esp_netif_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize esp-netif: %s", esp_err_to_name(ret));
        return false;
    }

    // Create default event loop
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        esp_netif_deinit();
        return false;
    }

#if ENABLE_SD || ENABLE_SPI_ETH || ENABLE_W5500_ETH
    if (init_spi_bus() != ESP_OK)
    {
        ESP_LOGE(TAG, "HSPI bus initialization failed, continuing with RMII Ethernet");
        esp_event_loop_delete_default();
        esp_netif_deinit();
        return false;
    }
#endif

    // Initialize NetworkManager
    ret = NetworkManager::get_instance()->init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "NetworkManager initialization failed: %s", esp_err_to_name(ret));
        esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, &NetworkManager::common_ip_event_handler);
        esp_event_loop_delete_default();
        esp_netif_deinit();
        return false;
    }

#if ENABLE_WIFI
    if (wifi_module_ == nullptr)
    {
        ESP_LOGE(TAG, "WiFiModule is null");
        return false;
    }
    if (ESP_FAIL == wifi_module_->init())
    {
        ESP_LOGE(TAG, "Failed to initialize WiFiModule");
      
        return false;
    }
   
#endif
#if ENABLE_RMII_ETH
    if (rmii_module_ == nullptr)
    {
        ESP_LOGE(TAG, "RMII Module is null");
        return false;
    }
    if (ESP_FAIL == rmii_module_->init())
    {
        ESP_LOGE(TAG, "Failed to initialize RMII Module");
        return false;
    }
#endif
// #if ENABLE_SPI_ETH
//     if (spi_module_ == nullptr)
//     {
//         ESP_LOGE(TAG, "SPI Module is null");
//         return false;
//     }
//     if (ESP_FAIL == spi_module_->init())
//     {
//         ESP_LOGE(TAG, "Failed to initialize SPI Module");
//         return false;
//     }
// #endif
// #if ENABLE_W5500_ETH
//     if (w5500_module_ == nullptr)
//     {
//         ESP_LOGE(TAG, "W5500 Module is null");
//         return false;
//     }
//     if (ESP_FAIL == w5500_module_->init())
//     {
//         ESP_LOGE(TAG, "Failed to initialize  w5500 Module");
//         return false;
//     }
//  #endif
#if ENABLE_GSM
    if (gsm_module_ == nullptr)
    {
        ESP_LOGE(TAG, "GSM Module is null");
        return false;
    }
    xTaskCreate(GsmModule::gsm_init_task, "gsm_init", 4096, gsm_module_, 5, NULL);
#endif

    ESP_LOGI(TAG, "InternetModule initialized");
     //vTaskDelay(pdMS_TO_TICKS(30000));
    // NetworkManager::get_instance()->startPingTask();
    return true;
}

bool InternetModule::init_w5500_module()
{
#if ENABLE_W5500_ETH
    if (w5500_module_ == nullptr)
    {
        ESP_LOGE(TAG, "W5500 Module is null");
        return false;
    }

    if (ESP_FAIL == w5500_module_->init())
    {
        ESP_LOGE(TAG, "Failed to initialize W5500 Module");
        return false;
    }
#endif

    return true;
}

bool InternetModule::init_enc28j60_module()
{
#if ENABLE_SPI_ETH
    if (spi_module_ == nullptr)
    {
        ESP_LOGE(TAG, "SPI Module is null");
        return false;
    }
    if (ESP_FAIL == spi_module_->init())
    {
        ESP_LOGE(TAG, "Failed to initialize SPI Module");
        return false;
    }
#endif

    return true;
}
// bool InternetModule::init_gsm_module()
// {
// #if ENABLE_GSM
//     if (gsm_module_ == nullptr)
//     {
//         ESP_LOGE(TAG, "GSM Module is null");
//         return false;
//     }

//     if (xTaskCreate(GsmModule::gsm_init_task,
//                     "gsm_init",
//                     4096,
//                     gsm_module_,
//                     5,
//                     NULL) != pdPASS)
//     {
//         ESP_LOGE(TAG, "Failed to create GSM init task");
//         return false;
//     }
// #endif

//     return true;
// }
