# =============================================================================
# CMakeLists.txt — IEC 104 Client Component
#
# This file registers the 'iec104' directory as an ESP-IDF component.
#
# ESP-IDF component structure requirements:
#   - SRCS         : Source files to compile (.c or .cpp)
#   - INCLUDE_DIRS : Directories exposed to other components that use this one
#   - REQUIRES     : Other ESP-IDF components this component depends on
#
# Dependencies explained:
#   esp_wifi   — WiFi driver APIs (esp_wifi_init, esp_wifi_start, etc.)
#   esp_event  — Event loop APIs (esp_event_handler_instance_register, etc.)
#   esp_netif  — Network interface layer (esp_netif_init, etc.)
#   nvs_flash  — Non-volatile storage (required before WiFi initialisation)
#   lwip       — lwIP TCP/IP stack (socket, connect, send, recv, etc.)
# =============================================================================

idf_component_register(
    SRCS
        "iec104_client.cpp"          # Our IEC 104 protocol implementation

    INCLUDE_DIRS
        "include"                    # Exposes iec104_client.h to main and others

    REQUIRES
        esp_wifi                     # WiFi station mode
        esp_event                    # System event loop
        esp_netif                    # TCP/IP network interface
        nvs_flash                    # NVS (needed by WiFi driver)
        lwip                         # TCP socket APIs
)