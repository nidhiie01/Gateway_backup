#ifndef PRODUCT_FEATURES_H
#define PRODUCT_FEATURES_H

#include <stdint.h>

// ============================================================
// Bitmask definitions (order must NEVER change)
// ============================================================

// Network interfaces
#define FEATURE_ETHERNET1 (1u << 0) // Ethernet-1 (Modbus/Internet)
#define FEATURE_ETHERNET2 (1u << 1) // Ethernet-2 (Internet, SPI)
#define FEATURE_RS485 (1u << 2)     // RS485 (1 Port)

// Cellular
#define FEATURE_CELLULAR_4G (1u << 3) // 4G Cellular
#define FEATURE_CELLULAR_5G (1u << 4) // 5G Cellular (Future)

// Wireless
#define FEATURE_WIFI (1u << 5)          // Wi-Fi
#define FEATURE_BLUETOOTH (1u << 6)     // Bluetooth
#define FEATURE_LORAWAN (1u << 7)       // Future: LoRaWAN
#define FEATURE_THREAD_MATTER (1u << 8) // Future: Thread/Matter
#define FEATURE_ZIGBEE (1u << 9)        // Future: Zigbee

// Storage
#define FEATURE_SD_CARD (1u << 10) // SD Card Slot
#define FEATURE_VPN (1u << 11)     // VPN

// I/O
#define FEATURE_DIO_2I2O (1u << 12) // Digital I/O (2I/2O)
#define FEATURE_DIO_4I4O (1u << 13) // Future: Digital I/O (4I/4O)
#define FEATURE_AIO_2I0O (1u << 14) // Future: Analog I/O (2I/0O)

// Debug & expansion
#define FEATURE_DEBUG_UART (1u << 15)    // Debug UART
#define FEATURE_GPS (1u << 16)           // Future: GPS Module
#define FEATURE_DISPLAY_IFACE (1u << 17) // Future: Display Interface
#define FEATURE_USB_HOST (1u << 18)      // Future: USB Host Port
#define FEATURE_CAMERA (1u << 19)        // Future: Camera
#define FEATURE_RESERVED (1u << 20)      // Future: Reserved / Expansion

// RTC & W5500

#define FEATURE_ETHERNETW5500 (1u << 21)  //Ethernet w5500
#define FEATURE_RTC (1u << 22)           //RTC timing


// ============================================================
// Helper Macro
// ============================================================
#define IS_FEATURE_ENABLED(code, feature) (((code) & (feature)) != 0u)

// Function prototype
#ifdef __cplusplus
extern "C"
{
#endif

    void print_feature_status(uint32_t model_code);

#ifdef __cplusplus
}
#endif

#endif // PRODUCT_FEATURES_H
