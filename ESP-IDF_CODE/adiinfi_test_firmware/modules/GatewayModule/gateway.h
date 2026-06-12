#ifndef MODULE_GATEWAY_H
#define MODULE_GATEWAY_H
#include "generate_uid.h"
#include "../InternetModule/InternetModule.h"
#include "./MacroConfig/MacroConfig.h"
#include <string>
#include <sys/stat.h>
#include "LedController.h"
#include "i2c_init.h"
#include "network_manager.h"
#include "freertos/FreeRTOS.h"
#include <freertos/event_groups.h>
#include "esp_event.h"
#include "WiFiModule.h"
#include "GSMModule.h"
#include "RmiiEthernetModule.h"
#include "SpiEthernetModule.h"
//#include "W55500EthernetModule.h"
#include "button.h"
#include "sdcard.h"
#include "mbcontroller.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "RTCController.h"


// extern TopicManager *topic_manager;

// Global callback declaration
void mqtt_global_callback(void *user_data, const char *topic, const char *payload, size_t payload_len);

// Friend declaration to allow access to private members
class Gateway;
void mqtt_global_callback(void *user_data, const char *topic, const char *payload, size_t payload_len);

typedef enum
{
    GATEWAY_STATE_INIT = 0,
    GATEWAY_STATE_NVS_INITIALIZED,
    GATEWAY_STATE_UID_GENERATED,
    GATEWAY_STATE_LED_INITIALIZED,
    GATEWAY_STATE_FS_INITIALIZED,
    GATEWAY_STATE_CONFIG_LOADED,
    GATEWAY_STATE_NO_CONFIG,
    GATEWAY_STATE_RUNNING,
    GATEWAY_STATE_ERROR,
    GATEWAY_STATE_WIFI_CONNECTING,
    GATEWAY_STATE_MQTT_CONNECTING,
    GATEWAY_STATE_CONNECTED,
    GATEWAY_STATE_DISCONNECTED,
    GATEWAY_STATE_CONFIG_RECEIVING,
    GATEWAY_STATE_CONFIG_SAVED,
    GATEWAY_STATE_NO_CONFIG_EXIST_IN_FS,
    GATEWAY_STATE_CONFIG_PARSE_ERROR,
    GATEWAY_STATE_OTA_IN_PROGRESS,
    GATEWAY_STATE_CONFIG_PARSED,
} gateway_state_t;

typedef enum
{
    ADINEXUS_CMD_UNKNOWN = -1,
    ADINEXUS_CMD_GET_GATEWAY_SETTINGS, // command for getting config file from MQTT
    ADINEXUS_CMD_CONFIG_UPDATE_STATUS,
    ADINEXUS_CMD_OTA_START, // OTA update command
    ADINEXUS_CMD_OTA_START_RESPONSE,
    ADINEXUS_CMD_GET_STATUS, // gateway status command
    ADINEXUS_CMD_GET_STATUS_RESPONSE,
    ADINEXUS_CMD_START_COMMISSION,
    ADINEXUS_CMD_GET_PLC_CONFIG_DATA,
    ADINEXUS_CMD_GET_MB_TAGS,
    ADINEXUS_CMD_GET_ALARM_CONFIG_DATA,
    ADINEXUS_CMD_GET_ALARMS,
    ADINEXUS_CMD_GET_MB_CMD_INTERVAL,
    ADINEXUS_CMD_GET_MB_CMD_PLC_GRP,
    ADINEXUS_CMD_GET_MB_CMD_TAGS
} mqtt_commands_t;

class Gateway
{
    friend void mqtt_global_callback(void *user_data, const char *topic, const char *payload, size_t payload_len);

private:
    static Gateway *instance_;

    UIDModule uid_;
    std::string gateway_id_;      
    InternetModule *internet_module_;
    SDCard *sdcard_ = nullptr;
    RmiiEthernetModule *rmii_module_ = nullptr;
    GsmModule *gsm_module_ = nullptr;

    gateway_state_t state_;
    TaskHandle_t gateway_task_handle_;

    void *modbus_serial_handle_ = nullptr;
    bool modbus_initialized_ = false;

    uint8_t retry_count = 0;
    const uint8_t MAX_RETRIES = 5;
    TickType_t RETRY_DELAY_MS = 2000; // 2 seconds

    bool pause_rtc_display_ = false;

    Gateway(InternetModule *internet);
    void gateway_task();
    // bool connect_modules();
    static void gateway_task_wrapper(void *param);

    TaskHandle_t rtc_display_task_handle_ = nullptr;
    static void rtc_display_task_wrapper(void *param);
    void rtc_display_task();
    bool sync_system_time_from_ntp(const char* ntp_server, const char* timezone);
   
public:
   
    bool test_led_ok;
    bool test_led_wifi_ok;      
    bool test_led_status_ok; 
    bool test_button_restart_ok;
    bool test_wifi_ok;
    bool test_sd_ok;
    bool test_eth_ok;
    bool test_w5500_ok;
    bool test_spi_ok;
    bool test_gsm_ok;
    bool test_modbus_ok;  
    bool test_rtc_ok;

  
    const std::string& get_gateway_id() const { return gateway_id_; }
    static Gateway *get_instance(InternetModule *internet = nullptr);

    void run_all_module_tests();   
    void test_led_module();
    void test_wifi_module();
    void test_gsm_module();
    void test_ethernet_module();
    void test_w5500_module();
    void test_spi_module();
    void test_modbus_module(); 
    void test_sdcard_module();
    void test_button_restart_module();
    void test_rtc_module();
    void print_test_summary();  
    void pause_rtc_display(bool pause) { pause_rtc_display_ = pause; }   
    bool init();
    void set_state(gateway_state_t new_state);
    gateway_state_t get_state() const { return state_; }
    

    ~Gateway();
};

#endif // MODULE_GATEWAY_H