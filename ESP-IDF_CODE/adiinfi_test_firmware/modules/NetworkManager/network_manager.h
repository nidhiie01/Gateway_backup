#pragma once

#include <map>
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../MacroConfig/MacroConfig.h"
#if ENABLE_WIFI
#include "../WiFiModule/WiFiModule.h"
#endif
#if ENABLE_GSM
#include "GSMModule.h"
#endif
#if ENABLE_RMII_ETH
#include "RmiiEthernetModule.h"
#endif
#if ENABLE_SPI_ETH
#include "SpiEthernetModule.h"
#endif
#if ENABLE_W5500_ETH
#include "W5500EthernetModule.h"
#endif 
#define WIFI_CONNECTED_BIT BIT0
#define CONNECT_BIT BIT0
// Global sync bits for GSM readiness

#define GSM_GOT_IP_BIT BIT1

// max number of network interfaces
// #define MAX_INTERFACES (ENABLE_GSM + ENABLE_WIFI + ENABLE_SPI_ETH + ENABLE_W5500_ETH + ENABLE_RMII_ETH)
#define MAX_INTERFACES 5
#define TOTAL_NUMBER_OF_PRIORITIES 5

// Interface priority macros (lower number = higher priority)
// 0 is the highest priority, 4 is the lowest
// no need to change or comment out any interface for priority just change the
// priority value and it will be applied automatically even if the interface having
// higher priority is disabled (The code will manage it automatically).
enum IFACE_PRIORITY
{
    // First will be the highest priority : 0
    PRIORITY_WIFI,
    PRIORITY_RMII_ETH,
    PRIORITY_GSM,
    PRIORITY_SPI_ETH,
    PRIORITY_W5500_ETH
};

enum NETWORK_INTERFACE
{
    NETWORK_INTERFACE_RMII_ETHERNET,
    NETWORK_INTERFACE_WIFI,
    NETWORK_INTERFACE_SPI_ETHERNET,
    NETWORK_INTERFACE_W5500_ETHERNET,
    NETWORK_INTERFACE_GSM
};

class NetworkManager
{
public:
    bool isLreadyInCheckAndSetInterface;
    // Singleton access
    static NetworkManager *get_instance();
    static std::map<NETWORK_INTERFACE, IFACE_PRIORITY> map_iface_priority;
    static std::map<NETWORK_INTERFACE, const char *> map_iface_name;

    // Initialize NetworkManager (register event handlers, start ping task)
    esp_err_t init();

    // Register a network interface and return its assigned index
    esp_err_t registerInterface(esp_netif_t *netif, int iface_index, const char *iface_name);
    int getCurrentDefaultIndex() const { return current_default_index_; }

    // Getters for interface state
    void setInternetConnected(int index, bool state);
    // bool isPingTaskStarted(int index) const;
    esp_netif_t *getNetif(int index) const;
    static void common_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

    void checkAndSetInterface();
    NETWORK_INTERFACE getInterfaceIndexByPriority(int priority);
   // void startPingTask();
    bool testConnectivity(int interface_index, int retries = 3);

private:
    static NetworkManager *instance_;

    NetworkManager();
    ~NetworkManager();

    // Delete copy constructor and assignment operator to enforce singleton
    NetworkManager(const NetworkManager &) = delete;
    NetworkManager &operator=(const NetworkManager &) = delete;

    // Constants
    static constexpr int DEFAULT_RETRY_LIMIT = 3;
    static constexpr int PING_INTERVAL_MS = 300000; // 5 minutes=300000ms , 1 minute=60000ms ,10 seconds=10000ms
    static constexpr int PING_RETRY_COUNT = 3;

    // ICMP echo header structure
    struct icmp_echo_hdr_t
    {
        uint8_t type;
        uint8_t code;
        uint16_t checksum;
        uint16_t identifier;
        uint16_t sequence;
    } __attribute__((packed));

    // Member variables
    esp_netif_t *netifs_[MAX_INTERFACES] = {nullptr};
    bool ping_task_started_[MAX_INTERFACES] = {false};
    bool internet_connected_[MAX_INTERFACES] = {false};

    int current_default_index_ = -1;
    int default_fail_count_ = 0;

    // Static logger tag
    static const char *TAG;

    // Helper methods
    static uint16_t icmpChecksum(void *addr, int count);
    bool pingInterface(int index);
   // static void pingTask(void *pvParameters);
};

struct TaskContextNM
{
    NetworkManager *nm;
};
