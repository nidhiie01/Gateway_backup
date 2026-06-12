#include "network_manager.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_netif_ppp.h"
#include "../InternetModule/InternetModule.h"

const char *NetworkManager::TAG = "network_manager";
NetworkManager *NetworkManager::instance_ = nullptr;

std::map<NETWORK_INTERFACE, IFACE_PRIORITY> NetworkManager::map_iface_priority = {
    {NETWORK_INTERFACE::NETWORK_INTERFACE_W5500_ETHERNET, IFACE_PRIORITY::PRIORITY_W5500_ETH},
    {NETWORK_INTERFACE::NETWORK_INTERFACE_SPI_ETHERNET, IFACE_PRIORITY::PRIORITY_SPI_ETH},
    {NETWORK_INTERFACE::NETWORK_INTERFACE_RMII_ETHERNET, IFACE_PRIORITY::PRIORITY_RMII_ETH},
    {NETWORK_INTERFACE::NETWORK_INTERFACE_WIFI, IFACE_PRIORITY::PRIORITY_WIFI},
    {NETWORK_INTERFACE::NETWORK_INTERFACE_GSM, IFACE_PRIORITY::PRIORITY_GSM}};

std::map<NETWORK_INTERFACE, const char *> NetworkManager::map_iface_name = {
    {NETWORK_INTERFACE::NETWORK_INTERFACE_W5500_ETHERNET, "W5500 Ethernet"},
    {NETWORK_INTERFACE::NETWORK_INTERFACE_SPI_ETHERNET, "SPI Ethernet"},
    {NETWORK_INTERFACE::NETWORK_INTERFACE_RMII_ETHERNET, "RMII Ethernet"},
    {NETWORK_INTERFACE::NETWORK_INTERFACE_WIFI, "Wi-Fi"},
    {NETWORK_INTERFACE::NETWORK_INTERFACE_GSM, "GSM"}};

NetworkManager::NetworkManager()
{

    this->isLreadyInCheckAndSetInterface = false;
    // Initialize arrays
    for (int i = 0; i < MAX_INTERFACES; i++)
    {
        netifs_[i] = nullptr;
        internet_connected_[i] = false;
        ping_task_started_[i] = false;
    }
}

NetworkManager::~NetworkManager()
{
    // Unregister event handler
    esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, &common_ip_event_handler);
}

NetworkManager *NetworkManager::get_instance()
{
    if (!instance_)
    {
        instance_ = new NetworkManager();
    }

    return instance_;
}

esp_err_t NetworkManager::init()
{
    // Register IP event handler
    esp_err_t ret = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &common_ip_event_handler, nullptr);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t NetworkManager::registerInterface(esp_netif_t *netif, int iface_index, const char *iface_name)
{
    if (!netif)
    {
        ESP_LOGE(TAG, "Invalid netif or interface name");
        return ESP_ERR_INVALID_ARG;
    }

    netifs_[iface_index] = netif;

    ESP_LOGI(TAG, "%s netif registered at index %d (netif=%p)", iface_name, iface_index, netif);
    return ESP_OK;
}

void NetworkManager::setInternetConnected(int index, bool state)
{
    if (index < 0 || index >= MAX_INTERFACES)
    {
        ESP_LOGE(TAG, "Invalid interface index for setting connected status: %d", index);
        return;
    }
    internet_connected_[index] = state;
    // ESP_LOGI(TAG, "Internet connected status for index %d set to %s", index, state ? "true" : "false");
}

esp_netif_t *NetworkManager::getNetif(int index) const
{
    if (index < 0 || index >= MAX_INTERFACES)
    {
        return nullptr;
    }
    return netifs_[index];
}

uint16_t NetworkManager::icmpChecksum(void *addr, int count)
{
    uint32_t sum = 0;
    uint16_t *ptr = static_cast<uint16_t *>(addr);
    while (count > 1)
    {
        sum += *ptr++;
        count -= 2;
    }
    if (count > 0)
    {
        sum += *reinterpret_cast<uint8_t *>(ptr);
    }
    while (sum >> 16)
    {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return ~sum;
}

bool NetworkManager::pingInterface(int iface_index)
{

    if (iface_index < 0 || iface_index > MAX_INTERFACES)
    {

        ESP_LOGE(TAG, "Invalid interface index: %d", iface_index);
        return false;
    }
    if (netifs_[iface_index] == nullptr)
    {

        ESP_LOGE(TAG, "%s not initialized yet", map_iface_name.at((NETWORK_INTERFACE)iface_index));
        return false;
    }
    if (!esp_netif_is_netif_up(netifs_[iface_index]))
    {

        ESP_LOGW(TAG, "%s interface is down,skipping ping", map_iface_name.at((NETWORK_INTERFACE)iface_index));

        return false;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netifs_[iface_index], &ip_info) != ESP_OK)
    {

        ESP_LOGW(TAG, "Interface %d has no valid IP", iface_index);
        return false;
    }
    if (ip_info.ip.addr == 0)
    {

        ESP_LOGW(TAG, "..Interface %d has no valid IP", iface_index);
        return false;
    }

    const char *iface_name = map_iface_name.at((NETWORK_INTERFACE)iface_index);

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Socket creation failed for %s: %s", iface_name, strerror(errno));
        return false;
    }

    struct sockaddr_in local_addr = {};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = ip_info.ip.addr;
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        ESP_LOGE(TAG, "Bind failed for %s: %s", iface_name, strerror(errno));
        close(sock);
        return false;
    }

    struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        ESP_LOGE(TAG, "Setsockopt failed for %s: %s", iface_name, strerror(errno));
        close(sock);
        return false;
    }

    struct sockaddr_in servaddr = {};
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("8.8.8.8");

    icmp_echo_hdr_t echo_hdr = {
        .type = 8,
        .code = 0,
        .checksum = 0,
        .identifier = htons(12345 + iface_index),
        .sequence = htons(1)};
    char data[32] = "ping";
    int total_len = sizeof(icmp_echo_hdr_t) + sizeof(data);
    uint8_t packet[64];
    memcpy(packet, &echo_hdr, sizeof(icmp_echo_hdr_t));
    memcpy(packet + sizeof(icmp_echo_hdr_t), data, sizeof(data));

    echo_hdr.checksum = icmpChecksum(packet, total_len);
    memcpy(packet, &echo_hdr, sizeof(icmp_echo_hdr_t));

    if (sendto(sock, packet, total_len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        ESP_LOGE(TAG, "Ping send failed from %s: %s", iface_name, strerror(errno));
        close(sock);
        return false;
    }

    uint8_t recv_buf[128];
    socklen_t addr_len = sizeof(servaddr);
    int recv_len = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&servaddr, &addr_len);
    close(sock);

    if (recv_len > 20)
    {
        icmp_echo_hdr_t *reply_hdr = reinterpret_cast<icmp_echo_hdr_t *>(recv_buf + 20);
        if (reply_hdr->type == 0 && reply_hdr->code == 0 && reply_hdr->identifier == echo_hdr.identifier)
        {
            ESP_LOGI(TAG, "Ping success from %s", iface_name);
#if ENABLE_WIFI
            // Get signal quality code
            if (iface_index == NETWORK_INTERFACE_WIFI)
            {
                auto *wifi = InternetModule::get_instance()->getWifiModule();
                if (wifi)
                {
                    wifi->wifi_display_signal_quality();
                }
                else
                {
                    ESP_LOGE(TAG, "WiFi module not available");
                }
            }
#endif
#if ENABLE_GSM
            // Get signal quality code
            if (iface_index == NETWORK_INTERFACE_GSM)
            {
                auto *gsm = InternetModule::get_instance()->getGsmModule();
                if (gsm)
                {
                    gsm->gsm_display_signal_quality();
                }
                else
                {
                    ESP_LOGE(TAG, "GSM module not available");
                }
            }
#endif
            return true;
        }
        else
        {
            ESP_LOGW(TAG, "Invalid ICMP reply on %s: type=%d code=%d id=%d",
                     iface_name, reply_hdr->type, reply_hdr->code, ntohs(reply_hdr->identifier));
            return false;
        }
    }
    else
    {
        ESP_LOGW(TAG, "Ping timeout or invalid reply on %s", iface_name);

        return false;
    }
}

// void NetworkManager::startPingTask()
// {

//     TaskContextNM *args = new TaskContextNM(get_instance());

//     // Start ping task
//     xTaskCreate(pingTask, "ping_task", 4096, (void *)args, 5, nullptr);
// }

bool NetworkManager::testConnectivity(int interface_index, int retries)
{
    for (int i = 0; i < retries; i++)
    {
        if (pingInterface(interface_index))
        {
            return true;
        }
        if (i < retries - 1)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    return false;
}

// void NetworkManager::pingTask(void *pvParameters)
// {
//     NetworkManager *nm = static_cast<TaskContextNM *>(pvParameters)->nm;

//     while (true)
//     {
//         // ESP_LOGI(TAG, "ping Task =============================================================");
//         // Skip Ping check in case task is already inside CheckAndSetInterface function call.
//         // As you can see CheckAndSetInterface function is taking more than 10 seconds and in case of
//         // pingtask interval is less than that it  will be a race condition and needs to be taken care of.
//         // Also during any interface change (like WiFi/Eth connect disconnet), it again calls CheckAndSetInterface
//         // function  out side of  pingtask which again creates a race condition. Both needs to be taken care of.
//         // As we have used MQTTModule as a singleton class we can use isLreadyInCheckAndSetInterface flag directly to
//         // skip the function call instead of waiting
//         if (nm->isLreadyInCheckAndSetInterface == false)
//         {
//             if (nm->current_default_index_ >= 0 && nm->netifs_[nm->current_default_index_])
//             {
//                 bool success = false;
//                 for (int attempt = 1; attempt <= PING_RETRY_COUNT; attempt++)
//                 {
//                     success = nm->pingInterface(nm->current_default_index_);
//                     if (success)
//                         break;
//                     ESP_LOGW(TAG, "Ping attempt %d/%d failed on index %d", attempt, PING_RETRY_COUNT, nm->current_default_index_);
//                     vTaskDelay(pdMS_TO_TICKS(2000));
//                 }
//                 nm->internet_connected_[nm->current_default_index_] = success;
//                 if (!success)
//                 {
//                     ESP_LOGE(TAG, "Default interface failed %d times — switching...", DEFAULT_RETRY_LIMIT);
//                     nm->checkAndSetInterface();
//                 }
//                 else
//                 {
//                     ESP_LOGI(TAG, "Default interface ping successful on index %d and %s", nm->current_default_index_,
//                              map_iface_name.at((NETWORK_INTERFACE)nm->current_default_index_));
//                 }
//             }
//             else
//             {
//                 ESP_LOGW(TAG, "No valid default interface — attempting to select one.");
//                 nm->checkAndSetInterface();
//             }
//         }
//         // ESP_LOGI(TAG, "ping Task END=============================================================");
//         vTaskDelay(pdMS_TO_TICKS(PING_INTERVAL_MS));
//     }
// }

NETWORK_INTERFACE NetworkManager::getInterfaceIndexByPriority(int priority)
{
    // Loop over all entries in map_iface_priority
    for (const auto &element : map_iface_priority)
    {
        // element's first is interface index and second is priority

        if (static_cast<int>(element.second) == priority)
        {
            return element.first;
        }
    }

    return (NETWORK_INTERFACE)-1; // no matching priority found
}

void NetworkManager::checkAndSetInterface()
{
    ESP_LOGI(TAG, "checkAndSetInterface=============================================================");

    if (this->isLreadyInCheckAndSetInterface == false)
    {
        this->isLreadyInCheckAndSetInterface = true;

        ESP_LOGW(TAG, "W5500_INDEX=%d ,SPI_INDEX=%d, RMII_INDEX=%d, WIFI_INDEX=%d,  GSM_INDEX=%d",
                 NETWORK_INTERFACE_W5500_ETHERNET, NETWORK_INTERFACE_SPI_ETHERNET, NETWORK_INTERFACE_RMII_ETHERNET,
                 NETWORK_INTERFACE_WIFI, NETWORK_INTERFACE_GSM);

        bool isSuccess = false;

        for (int i_priority = 0; i_priority < TOTAL_NUMBER_OF_PRIORITIES; i_priority++)
        {
            int ifaceIndex = getInterfaceIndexByPriority(i_priority);

            // Skip disabled interfaces
            bool isEnabled = false;
            switch (ifaceIndex)
            {
            case NETWORK_INTERFACE_WIFI:
#if ENABLE_WIFI
                isEnabled = true;
#endif
                break;
            case NETWORK_INTERFACE_SPI_ETHERNET:
#if ENABLE_SPI_ETH
                isEnabled = true;
#endif
                break;
            case NETWORK_INTERFACE_W5500_ETHERNET:
#if ENABLE_W5500_ETH
                isEnabled = true;
#endif
                break;
            case NETWORK_INTERFACE_RMII_ETHERNET:
#if ENABLE_RMII_ETH
                isEnabled = true;
#endif
                break;
            case NETWORK_INTERFACE_GSM:
#if ENABLE_GSM
                isEnabled = true;
#endif
                break;
            default:
                break;
            }

            if (!isEnabled)
            {
                ESP_LOGI(TAG, "Skipping disabled interface: %s", map_iface_name.at((NETWORK_INTERFACE)ifaceIndex));
                continue;
            }

            // Try ping with retries
            for (int retry = 0; retry < DEFAULT_RETRY_LIMIT; retry++)
            {
                isSuccess = pingInterface(ifaceIndex);
                if (isSuccess)
                    break;
                ESP_LOGW(TAG, "Ping failed on %s, retry %d/%d", map_iface_name.at((NETWORK_INTERFACE)ifaceIndex), retry + 1, DEFAULT_RETRY_LIMIT);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }

            ESP_LOGE(TAG, "isSuccess: %d in checkAndSetInterface()", isSuccess);
            internet_connected_[ifaceIndex] = isSuccess;
#if ENABLE_WIFI
            if (ifaceIndex == NETWORK_INTERFACE_WIFI && !isSuccess)
            {
                auto *wifi = InternetModule::get_instance()->getWifiModule();
                if (wifi)
                {
                    wifi->reset_signal_quality();
                    // ESP_LOGW(TAG, "Wi-Fi ping failed → signal quality forced to -1");
                }
            }
#endif

            if (isSuccess)
            {
                bool default_changed = (current_default_index_ != ifaceIndex);
                ESP_LOGE(TAG, "default_changed: %d, current_default_index_:%d", default_changed, current_default_index_);

                esp_netif_set_default_netif(netifs_[ifaceIndex]);
                current_default_index_ = ifaceIndex;
#if ENABLE_WIFI
                if (ifaceIndex == NETWORK_INTERFACE_WIFI)
                {
                    // ESP_LOGI(TAG, "Wi-Fi is the default interface now, restoring signal quality");
                    auto *wifi = InternetModule::get_instance()->getWifiModule();
                    if (wifi)
                    {
                        wifi->restore_signal_quality();
                    }
                }
#endif

                ESP_LOGI(TAG, "%s is confirmed to have internet connectivity.", map_iface_name.at((NETWORK_INTERFACE)ifaceIndex));

                this->isLreadyInCheckAndSetInterface = false;
                return; // Stop after setting default
            }
        }
        if (isSuccess == false)
        {
            // If no interface succeeded
            ESP_LOGI(TAG, "All interfaces failed to get internet, checking for other Interface.");
            ESP_LOGI(TAG, "Stop MQTT Module");

#if ENABLE_WIFI
            InternetModule::get_instance()->getWifiModule()->lookForWiFiAgain();
#endif
        }
    }
    this->isLreadyInCheckAndSetInterface = false;
    ESP_LOGI(TAG, "checkAndSetInterface END -------------------------------------------------------------");
}
// new end

void NetworkManager::common_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    NetworkManager *nm = get_instance();
    int index = -1;

    if (event_base == IP_EVENT)
    {
        ESP_LOGW(TAG, ">>>>> event_id: %d ", (int)event_id);

        esp_netif_t *netif = nullptr;

        // auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(event_data);
        if (!event)
        {
            ESP_LOGW(TAG, "Got IP event but event_data is null (event_id=%" PRId32 ")", event_id);
            return;
        }
        // ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(event_data);
        netif = event->esp_netif;
        const esp_netif_ip_info_t *ip_info = &event->ip_info;
        // If esp_netif pointer is NULL, don't log as "Unknown Got IP"
        if (!netif)
        {
            ESP_LOGW(TAG, "Got IP event with null netif (event_id=%" PRId32 ")", event_id);
            return;
        }
        for (int i = 0; i < MAX_INTERFACES; i++)
        {
            if (nm->netifs_[i] == netif)
            {
                index = i;
                break;
            }
        }
        char if_key[16] = {};
        esp_netif_get_netif_impl_name(netif, if_key);
        if (index < 0)
        {
            ESP_LOGW(TAG, "Got IP from netif=%p (%s) but not registered yet; ignoring this event", netif, if_key);
            return;
        }

        if (event_id == IP_EVENT_STA_GOT_IP || event_id == IP_EVENT_ETH_GOT_IP || event_id == IP_EVENT_PPP_GOT_IP)
        {
            nm->internet_connected_[index] = true;
            ESP_LOGI(TAG, "Event from netif: %s (netif=%p)", if_key, netif);

            ESP_LOGI(TAG, "%s Got IP Address", map_iface_name.at((NETWORK_INTERFACE)index));
            ESP_LOGI(TAG, "~~~~~~~~~~~");
            ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip_info->ip));
            ESP_LOGI(TAG, "MASK: " IPSTR, IP2STR(&ip_info->netmask));
            ESP_LOGI(TAG, "GW: " IPSTR, IP2STR(&ip_info->gw));
            ESP_LOGI(TAG, "~~~~~~~~~~~");

            LedController::get_instance()->setPattern(P3, steady_on);  // blue led
            LedController::get_instance()->setPattern(P2, steady_off); // red led

#if ENABLE_WIFI
            if (index == NETWORK_INTERFACE_WIFI)
            {
                auto *wifi = InternetModule::get_instance()->getWifiModule();
                if (wifi)
                {
                    xEventGroupSetBits(wifi->wifi_event_group_, WIFI_CONNECTED_BIT);
                }
            }
#endif
#if ENABLE_GSM
            if (index == NETWORK_INTERFACE_GSM && event_id == IP_EVENT_PPP_GOT_IP)
            {
                auto *gsm = InternetModule::get_instance()->getGsmModule();
                if (gsm)
                {
                    // xEventGroupSetBits(nm->getReadyEventGroup(), GSM_GOT_IP_BIT);
                    xEventGroupSetBits(gsm->getEventGroup(), CONNECT_BIT);
                    esp_netif_dns_info_t dns_info;
                    esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
                    ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
                    esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info);
                    ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
                }
                else
                {
                    ESP_LOGE(TAG, "GSM module not available");
                }
            }
#endif
            nm->checkAndSetInterface();
        }

        if (event_id == IP_EVENT_STA_LOST_IP || event_id == IP_EVENT_ETH_LOST_IP || event_id == IP_EVENT_PPP_LOST_IP)
        {
            nm->internet_connected_[index] = false;

// #if ENABLE_GSM
//             if (event_id == IP_EVENT_PPP_LOST_IP)
//             {
//                 xEventGroupClearBits(nm->getReadyEventGroup(), GSM_GOT_IP_BIT);
//                 auto *gsm = InternetModule::get_instance()->getGsmModule();
//                 xEventGroupClearBits(gsm->getEventGroup(), CONNECT_BIT);
//             }
// #endif
#if ENABLE_GSM
            if (event_id == IP_EVENT_PPP_LOST_IP)
            {
                // xEventGroupClearBits(nm->getReadyEventGroup(), GSM_GOT_IP_BIT);
                auto *gsm = InternetModule::get_instance()->getGsmModule();
                if (gsm)
                {
                    xEventGroupClearBits(gsm->getEventGroup(), CONNECT_BIT);
                    gsm->reset_signal_quality();
                    ESP_LOGW(TAG, "GSM signal quality reset to -1 (Lost IP)");
                }
            }
#endif

#if ENABLE_WIFI
            if (index == NETWORK_INTERFACE_WIFI)
            {
                auto *wifi = InternetModule::get_instance()->getWifiModule();
                if (wifi)
                {
                    xEventGroupClearBits(wifi->wifi_event_group_, WIFI_CONNECTED_BIT);
                    wifi->get_signal_quality();
                    ESP_LOGW(TAG, "WiFi signal quality reset to -1 (Lost IP)");
                }
            }
#endif

            ESP_LOGI(TAG, "%s Lost IP Address", map_iface_name.at((NETWORK_INTERFACE)index));

            // Check default interface ping if work fine then no need to go for checkAndSetInterface
            bool ret1 = nm->pingInterface(nm->current_default_index_);
            if (ret1 == false)
                nm->checkAndSetInterface();
        }
    }
}
