#include "product_features.h"
#include "esp_log.h"
#include <inttypes.h>   // for PRIu32

static const char *TAG = "FEATURES";

void print_feature_status(uint32_t code)
{
    ESP_LOGI(TAG, "Model Code: %" PRIu32, code);

    ESP_LOGI(TAG, "Ethernet-1 (Modbus/Internet): %s", IS_FEATURE_ENABLED(code, FEATURE_ETHERNET1) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Ethernet-2 (SPI):             %s", IS_FEATURE_ENABLED(code, FEATURE_ETHERNET2) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "RS485 (1 Port):               %s", IS_FEATURE_ENABLED(code, FEATURE_RS485) ? "ENABLED" : "DISABLED");

    ESP_LOGI(TAG, "4G Cellular:                  %s", IS_FEATURE_ENABLED(code, FEATURE_CELLULAR_4G) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "5G Cellular:                  %s", IS_FEATURE_ENABLED(code, FEATURE_CELLULAR_5G) ? "ENABLED" : "DISABLED");

    ESP_LOGI(TAG, "Wi-Fi:                        %s", IS_FEATURE_ENABLED(code, FEATURE_WIFI) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Bluetooth:                    %s", IS_FEATURE_ENABLED(code, FEATURE_BLUETOOTH) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "LoRaWAN (Future):             %s", IS_FEATURE_ENABLED(code, FEATURE_LORAWAN) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Thread/Matter (Future):       %s", IS_FEATURE_ENABLED(code, FEATURE_THREAD_MATTER) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Zigbee (Future):              %s", IS_FEATURE_ENABLED(code, FEATURE_ZIGBEE) ? "ENABLED" : "DISABLED");

    ESP_LOGI(TAG, "SD Card Slot:                 %s", IS_FEATURE_ENABLED(code, FEATURE_SD_CARD) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "VPN:                          %s", IS_FEATURE_ENABLED(code, FEATURE_VPN) ? "ENABLED" : "DISABLED");

    ESP_LOGI(TAG, "Digital I/O (2I/2O):          %s", IS_FEATURE_ENABLED(code, FEATURE_DIO_2I2O) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Digital I/O (4I/4O Future):   %s", IS_FEATURE_ENABLED(code, FEATURE_DIO_4I4O) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Analog I/O (2I/0O Future):    %s", IS_FEATURE_ENABLED(code, FEATURE_AIO_2I0O) ? "ENABLED" : "DISABLED");

    ESP_LOGI(TAG, "Debug UART:                   %s", IS_FEATURE_ENABLED(code, FEATURE_DEBUG_UART) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "GPS (Future):                 %s", IS_FEATURE_ENABLED(code, FEATURE_GPS) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Display Interface (Future):   %s", IS_FEATURE_ENABLED(code, FEATURE_DISPLAY_IFACE) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "USB Host (Future):            %s", IS_FEATURE_ENABLED(code, FEATURE_USB_HOST) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Camera (Future):              %s", IS_FEATURE_ENABLED(code, FEATURE_CAMERA) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Reserved/Expansion (Future):  %s", IS_FEATURE_ENABLED(code, FEATURE_RESERVED) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "ETHERNET W5500:               %s", IS_FEATURE_ENABLED(code, FEATURE_RESERVED) ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "RTC Timing :                  %s", IS_FEATURE_ENABLED(code, FEATURE_RESERVED) ? "ENABLED" : "DISABLED");
}
