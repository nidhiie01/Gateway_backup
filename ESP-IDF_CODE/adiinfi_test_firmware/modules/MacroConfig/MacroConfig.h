
#ifndef CONFIG_H
#define CONFIG_H

#define FIRMWARE_VERSION "1.0.1"

//   IMPORTANT: HARDWARE VERSION CONFIGURATION
//
//   VERSION 1.0: Uses RMII Ethernet + ENC28J60 (SPI Ethernet)
//                - In SDK Config (menuconfig), enable: ENC28J60 MODULE
//                - Set ENABLE_SPI_ETH = 1 and ENABLE_W5500_ETH = 0
//
//   VERSION 2.0: Uses RMII Ethernet + W5500 (SPI Ethernet)
//                - In SDK Config (menuconfig), enable: W5500 MODULE
//                - Set ENABLE_SPI_ETH = 0 and ENABLE_W5500_ETH = 1
//
//   NOTE: Only ONE SPI Ethernet module can be enabled at a time!
//         The version will be detected at runtime during testing.

#define BOARD_VERSION_1_0 1
#define BOARD_VERSION_2_0 0

#if (BOARD_VERSION_1_0 + BOARD_VERSION_2_0) != 1
#error "Exactly one board version must be selected (BOARD_VERSION_1_0 or BOARD_VERSION_2_0)"
#endif

// ================= INTERFACE ENABLES (derived from version) =================

// Common modules – same for both versions
#define ENABLE_WIFI 1     // Wi-Fi module
#define ENABLE_RMII_ETH 1 // RMII Ethernet (LAN8720)
#define ENABLE_GSM 1      // GSM module (SIM7600)
#define ENABLE_SD 1       // SD card module

// Version-specific SPI Ethernet   Board 1.0: ENC28J60 SPI Ethernet, NO W5500
#if BOARD_VERSION_1_0
#define ENABLE_SPI_ETH 0 // ENC28J60
#define ENABLE_W5500_ETH 1
#elif BOARD_VERSION_2_0 // Board 2.0: W5500 SPI Ethernet, NO ENC28J60
#define ENABLE_SPI_ETH 0
#define ENABLE_W5500_ETH 1
#endif

// #include "LedController.h"
// #include <vector>

// Enable SPI_ETH or GSM at a time
// If both are enabled, the code will throw an error
// Enable (1) or disable (0) network interfaces
// #define ENABLE_WIFI 1     // Wi-Fi module
// #define ENABLE_SPI_ETH 1  // SPI Ethernet module
// #define ENABLE_W5500_ETH   1     // enable W5500 module (SPI Ethernet)
// #define ENABLE_RMII_ETH 1 // RMII Ethernet module
// #define ENABLE_GSM 1     // GSM module (SIM7600)
// #define ENABLE_SD 1       // SD card module

// Wi-Fi configuration
#if ENABLE_WIFI
#define WIFI_SSID "admin"
#define WIFI_PASSWORD "admin123"
#define WIFI_AUTHMODE WIFI_AUTH_WPA2_PSK
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#endif

// gateway_main configuration
#define CONFIG_PAYLOAD_BUFFER_SIZE 1024
#define MQTT_URI "mqtt://emqx.adiinfi.co.in"
#define MQTT_CLIENT_ID "esp32-gateway-c4d8d57e"
#define LOG_INFO(fmt, ...) ESP_LOGI("Main", fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(fmt, ...) ESP_LOGE("Main", fmt, ##__VA_ARGS__)

// MQTT topics
// #define COMMAND_TOPIC "Gateway/c4d8d57e/command"
// #define CONTROL_TOPIC "Gateway/c4d8d57e/ctr"
// #define RESPONSE_TOPIC "Gateway/c4d8d57e/response"
#define CONFIG_FILE_PATH "/spiffs/gateway_config.bin" // Consistent file path

// Chunk sizes for PLC and Alarm tag requests
#define PLC_TAGS_CHUNK_SIZE 10
#define ALARM_TAGS_CHUNK_SIZE 2

// Modbus UART configuration and MQTT topics
#define MODBUS_RX_PIN 16
#define MODBUS_TX_PIN 17
#define MODBUS_DERE_PIN GPIO_NUM_33
#define MODBUS_UART_PORT UART_NUM_2
// #define MQTT_PUBLISH_TOPIC "Gateway/BA0D0DBA/data"
// #define MQTT_ALARM_TOPIC "Gateway/BA0D0DBA/alarm"

// Button configuration
#define BUTTON_GPIO_NUM GPIO_NUM_34      // GPIO pin for the button
#define BUTTON_DEBOUNCE_DELAY_US (50000) // Debounce delay in microseconds

// I2C configuration
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SCL_IO 32
#define I2C_MASTER_SDA_IO 15
#define I2C_MASTER_FREQ_HZ 100000

// LED Controller configuration
#define PCF8574_ADDR 0x20               // I2C address of the PCF8574
#define LED_COUNT 4                     // Number of LEDs
#define P1 1                            // I2C pin for Wi-Fi LED
#define P2 2                            // I2C pin for RED LED
#define P3 3                            // I2C pin for BLUE LED
#define DEFAULT_DELAY 1000              // Default delay for LED task when booting and for any error state (1 Second)
#define NORMAL_DELAY 60 * DEFAULT_DELAY // Normal delay for LED task when everything is fine (60 Second)

// RMII Ethernet configuration
#if ENABLE_RMII_ETH
#define MDC_GPIO 23
#define MDIO_GPIO 18
#define PHY_ADDR 1
#define RST_GPIO -1
#endif // ENABLE_RMII_ETH

// SPI Ethernet configuration
#if ENABLE_SPI_ETH
#define ENC28j60_SPI_HOST SPI2_HOST
#define CS_GPIO 5
#define SPI_PHY_RST_GPIO -1
#define INT_NUM 35
constexpr int SPI_CLOCK_MHZ = (16 * 1000000);
#endif // ENABLE_SPI_ETH

#if ENABLE_W5500_ETH
#define W5500_SPI_HOST SPI2_HOST // SPI1 / HSPI recommended

// Pins based on your menuconfig and schematic
#define W5500_MISO_GPIO 12
#define W5500_MOSI_GPIO 13
#define W5500_SCLK_GPIO 14
#define W5500_CS_GPIO 5
#define W5500_INT_GPIO 35
#define W5500_RESET_GPIO -1
constexpr int W5500_SPI_CLOCK_HZ = (16 * 1000000); // 16 MHz (stable value)
#endif                                             // ENABLE_W5500_ETH

// GSM configuration
#if ENABLE_GSM
#define MODEM_PPP_APN "jionet"
#define PCF8574_ADDR 0x20
#define I2C_MASTER_NUM I2C_NUM_0
#define CONNECT_BIT BIT0
#endif // ENABLE_GSM

// Static IP configurations for each interface
#if ENABLE_WIFI
#define USE_STATIC_IP_FOR_WIFI 0 // Set to 1 for static IP, 0 for DHCP for Wi-Fi
#define WIFI_STATIC_IP "192.168.1.100"
#define WIFI_STATIC_MASK "255.255.255.0"
#define WIFI_STATIC_GW "192.168.1.1"
#endif // ENABLE_WIFI

#if ENABLE_SPI_ETH
#define USE_STATIC_IP_FOR_SPI 0 // Set to 1 for static IP, 0 for DHCP for SPI Ethernet
#define SPI_ETH_STATIC_IP "192.168.1.201"
#define SPI_ETH_STATIC_MASK "255.255.255.0"
#define SPI_ETH_STATIC_GW "192.168.1.1"
#endif // ENABLE_SPI_ETH

// Static IP configuration for W5500
#if ENABLE_W5500_ETH
#define USE_STATIC_IP_FOR_W5500 0 // Set to 1 for static IP, 0 for DHCP for w500 Ethernet
#define W5500_STATIC_IP "192.168.1.202"
#define W5500_STATIC_MASK "255.255.255.0"
#define W5500_STATIC_GW "192.168.1.1"

#endif // ENABLE_W5500_ETH

#if ENABLE_RMII_ETH
#define USE_STATIC_IP_FOR_RMII 0 // Set to 1 for static IP, 0 for DHCP for RMII Ethernet
#define RMII_ETH_STATIC_IP "192.168.1.200"
#define RMII_ETH_STATIC_MASK "255.255.255.0"
#define RMII_ETH_STATIC_GW "192.168.1.1"
#endif // ENABLE_RMII_ETH

#if ENABLE_GSM
#define USE_STATIC_IP_FOR__GSM 0 // Set to 1 for static IP, 0 for DHCP for GSM
#define GSM_STATIC_IP "192.168.1.103"
#define GSM_STATIC_MASK "255.255.255.0"
#define GSM_STATIC_GW "192.168.1.1"
#endif // ENABLE_GSM

#endif // CONFIG_H
