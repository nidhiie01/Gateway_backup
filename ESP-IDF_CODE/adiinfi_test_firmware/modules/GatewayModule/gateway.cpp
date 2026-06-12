#include "gateway.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>  // For open
#include <unistd.h> // For write, close
#include <errno.h>  // For errno
#include "esp_system.h"
#include "mbcontroller.h"
#include "esp_modbus_master.h"
#include "esp_sntp.h"
#define MODBUS_PORT_NUM    MODBUS_UART_PORT
#define MODBUS_TX_PIN        17
#define MODBUS_RX_PIN        16
#define MODBUS_RTS_PIN       33

#define MODBUS_SLAVE_ADDR    1
#define MODBUS_START_ADDR    0    // Start address for both read and write
#define MODBUS_REG_COUNT     5    // Number of registers (5 registers: 0-4)


// ---------- test tag ----------
static const char* TEST_TAG = "GATEWAY_TEST";

// ---------- helper: show only test logs ----------
static void enable_test_logs_only()
{
    esp_log_level_set("*", ESP_LOG_NONE);      // mute everything
    esp_log_level_set(TEST_TAG, ESP_LOG_INFO); // allow only ModuleTest logs
}

static void restore_log_levels()
{
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("*", ESP_LOG_INFO);
}

static esp_err_t init_modbus_master_for_test(void **handle)
{
    // Initialize Modbus master with new API
    esp_err_t err = mbc_master_init(MB_PORT_SERIAL_MASTER, handle);
    if (err != ESP_OK || *handle == NULL) {
        ESP_LOGE(TEST_TAG, "Modbus init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Setup communication parameters
    mb_communication_info_t comm_info = {};
    comm_info.mode = MB_MODE_RTU;
    comm_info.port = MODBUS_PORT_NUM;
    comm_info.baudrate = 9600;
    comm_info.parity = UART_PARITY_DISABLE;
    
    err = mbc_master_setup(&comm_info);
    if (err != ESP_OK) {
        ESP_LOGE(TEST_TAG, "Modbus setup failed: %s", esp_err_to_name(err));
        mbc_master_destroy();
        *handle = NULL;
        return err;
    }

    // Set UART pins
    err = uart_set_pin(MODBUS_PORT_NUM, MODBUS_TX_PIN, MODBUS_RX_PIN, 
                       MODBUS_RTS_PIN, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TEST_TAG, "UART set pin failed: %s", esp_err_to_name(err));
        mbc_master_destroy();
        *handle = NULL;
        return err;
    }

    // Start Modbus master
    err = mbc_master_start();
    if (err != ESP_OK) {
        ESP_LOGE(TEST_TAG, "Modbus start failed: %s", esp_err_to_name(err));
        mbc_master_destroy();
        *handle = NULL;
        return err;
    }

    // Set Half Duplex mode for RS485
    err = uart_set_mode(MODBUS_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TEST_TAG, "UART set mode failed: %s", esp_err_to_name(err));
        mbc_master_destroy();
        *handle = NULL;
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

void Gateway::rtc_display_task_wrapper(void *param)
{
    Gateway *gateway = static_cast<Gateway *>(param);
    gateway->rtc_display_task();
}
#if ENABLE_W5500_ETH

void Gateway::rtc_display_task()
{
    RTCController* rtc = RTCController::get_instance();
    if (!rtc) {
        vTaskDelete(NULL);
        return;
    }
    while (true) {
        if (pause_rtc_display_) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        RTCDateTime dt;
        if (rtc->getDateTime(dt)) {
            const char *days[] = {"", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            ESP_LOGI(TEST_TAG, "RTC Time: %s %04d-%02d-%02d %02d:%02d:%02d", days[dt.day], dt.year, dt.month, dt.date, dt.hour, dt.minute, dt.second);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
bool Gateway::sync_system_time_from_ntp(const char* ntp_server, const char* timezone)
{
    setenv("TZ", timezone, 1);
    tzset();
    
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_init();

    int retry = 0;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    return (retry < 20);
}
#endif
static bool wait_for_yes_no(const char* prompt, uint32_t timeout_ms = 30000)
{
    printf("\n%s\n", prompt);
    printf("Press 'y' for YES or 'n' for NO: ");
    fflush(stdout);
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while (1) {
        // Check for timeout
        if ((xTaskGetTickCount() - start_time) > timeout_ticks) {
            printf("\nTimeout! Assuming NO\n");
            return false;
        }
        
        int c = getchar();
        if (c == EOF || c == 0xFF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        // Convert to lowercase
        c = tolower(c);
        if (c == 'y') {
            printf("y\nYES selected\n");
            return true;
        } else if (c == 'n') {
            printf("n\nNO selected\n");
            return false;
        }
        // Ignore other characters and keep waiting
    }
}

static const char *TAG = "GatewayModule";

Gateway *Gateway::instance_ = nullptr;

Gateway::Gateway(InternetModule *internet) : internet_module_(internet), state_(GATEWAY_STATE_INIT), gateway_task_handle_(nullptr)
{
    ESP_LOGI(TAG, "Gateway constructed");
  
}

Gateway *Gateway::get_instance(InternetModule *internet)
{
    if (!instance_)
    {
        if (internet == nullptr)
        {
            internet = InternetModule::get_instance(); // Fallback to singleton if not provided
        }
        if (!internet)
        {
            ESP_LOGE(TAG, "Cannot create Gateway instance without all modules");
            return nullptr;
        }
        instance_ = new Gateway(internet);
    }
    return instance_;
}

void Gateway::test_modbus_module()
{
    test_modbus_ok = false;
    
    enable_test_logs_only();
    
    ESP_LOGI(TEST_TAG, "================== ...Modbus Testing Started... ====================");
    
    // Initialize Modbus
    if (modbus_serial_handle_ == nullptr) {
        ESP_LOGI(TEST_TAG, "Initializing Modbus master...");
        if (init_modbus_master_for_test(&modbus_serial_handle_) != ESP_OK) {
            ESP_LOGI(TEST_TAG, "Modbus Not connected (Init failed)");
            return;
        }
        modbus_initialized_ = true;
     //   ESP_LOGI(TEST_TAG, "Init successful, waiting for stabilization...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    // Test data to write
    const uint16_t TEST_VALUES[5] = {100, 200, 300, 400, 500};
    
    // Try different configurations
    const struct {
        uint8_t slave_addr;
        uint16_t reg_start;
        const char* desc;
    } test_configs[] = {
        {1, 0, "Slave:1, Reg:0"},
        {1, 1, "Slave:1, Reg:1"},
        {1, 40001, "Slave:1, Reg:40001"},
        {2, 0, "Slave:2, Reg:0"}
    };
    
    bool comm_ok = false;
    
    for (int i = 0; i < 4 && !comm_ok; i++) {
        ESP_LOGI(TEST_TAG, "Testing: %s", test_configs[i].desc);
        
        // ============ WRITE OPERATION ============
        ESP_LOGI(TEST_TAG, "Writing test values to registers...");
        
        // Create mutable copy of test values for write operation
        uint16_t write_values[5];
        memcpy(write_values, TEST_VALUES, sizeof(TEST_VALUES));
        
        mb_param_request_t write_req = {
            .slave_addr = test_configs[i].slave_addr,
            .command = 0x10,  // Write Multiple Registers (Function Code 16)
            .reg_start = test_configs[i].reg_start,
            .reg_size = 5
        };
        
        esp_err_t write_err = mbc_master_send_request(&write_req, write_values);
        
        if (write_err != ESP_OK) {
            ESP_LOGE(TEST_TAG, "✗ Write Multiple (0x10) failed: %s", esp_err_to_name(write_err));
            
            // Try alternative: Write Single Register (0x06) for first register only
            ESP_LOGI(TEST_TAG, "Trying Write Single Register (0x06) as fallback...");
            mb_param_request_t single_write_req = {
                .slave_addr = test_configs[i].slave_addr,
                .command = 0x06,  // Write Single Register
                .reg_start = test_configs[i].reg_start,
                .reg_size = 1
            };
            
            write_err = mbc_master_send_request(&single_write_req, &write_values[0]);
            if (write_err != ESP_OK) {
                ESP_LOGE(TEST_TAG, "✗ Write Single (0x06) also failed: %s", esp_err_to_name(write_err));
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;  // Try next configuration
            }
            ESP_LOGI(TEST_TAG, "✓ Write Single successful (wrote %u to first register)", write_values[0]);
        } else {
            ESP_LOGI(TEST_TAG, "✓ Write Multiple successful");
        }
        
        ESP_LOGI(TEST_TAG, "Written values: %u, %u, %u, %u, %u", 
                 TEST_VALUES[0], TEST_VALUES[1], TEST_VALUES[2], 
                 TEST_VALUES[3], TEST_VALUES[4]);
        
        // Small delay between write and read
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // ============ READ OPERATION ============
        ESP_LOGI(TEST_TAG, "Reading back values from registers...");
        uint16_t read_values[5] = {0};
        mb_param_request_t read_req = {
            .slave_addr = test_configs[i].slave_addr,
            .command = 0x03,  // Read Holding Registers (Function Code 3)
            .reg_start = test_configs[i].reg_start,
            .reg_size = 5
        };
        
        esp_err_t read_err = mbc_master_send_request(&read_req, read_values);
        
        if (read_err != ESP_OK) {
            ESP_LOGE(TEST_TAG, "✗ Read failed: %s", esp_err_to_name(read_err));
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;  // Try next configuration
        }
        
        ESP_LOGI(TEST_TAG, "✓ Read successful");
        ESP_LOGI(TEST_TAG, "Read values: %u, %u, %u, %u, %u", 
                 read_values[0], read_values[1], read_values[2], 
                 read_values[3], read_values[4]);
        
        // ============ VERIFY DATA ============
        bool data_matches = true;
        for (int j = 0; j < 5; j++) {
            if (read_values[j] != TEST_VALUES[j]) {
                data_matches = false;
                ESP_LOGW(TEST_TAG, "Mismatch at index %d: wrote %u, read %u", 
                         j, TEST_VALUES[j], read_values[j]);
            }
        }
        
        if (data_matches) {
            ESP_LOGI(TEST_TAG, "✓ Data verification PASSED - Write/Read match!");
            comm_ok = true;
            test_modbus_ok = true;
        } else {
            ESP_LOGW(TEST_TAG, "✗ Data verification FAILED - Mismatch detected");
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // ============ FINAL RESULT ============
    ESP_LOGI(TEST_TAG, "");
    if (comm_ok) {
        ESP_LOGI(TEST_TAG, "           MODBUS TEST PASSED                ");
        ESP_LOGI(TEST_TAG, "   Write ✓ | Read ✓ | Verification ✓                    ");
        ESP_LOGI(TEST_TAG, "");
    } else {

        ESP_LOGE(TEST_TAG, " MODBUS TEST FAILED  ");
    }
}
void Gateway::test_led_module()
{
    test_led_ok = false;
    test_led_wifi_ok = false;
    test_led_status_ok = false;
    enable_test_logs_only();

    ESP_LOGI(TEST_TAG, "==================== ...LED Test Started... =================");

    LedController* led = LedController::get_instance();
    if (!led) {
        ESP_LOGI(TEST_TAG, "LED Controller Not initialized");
        return;
    }

    const uint8_t WIFI_LED_PIN = P1;    // WiFi LED
    const uint8_t STATUS_LED_PIN = P3;  // Status LED

     // Create blink pattern for testing
    LedPattern test_blink_pattern;
    test_blink_pattern.type = LedPatternType::BLINK;
    test_blink_pattern.blink_interval_ms = 500;  // 500ms blink interval


    ESP_LOGI(TEST_TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TEST_TAG, "║          >>> WiFi STATUS LED TEST <<<                      ║");
    ESP_LOGI(TEST_TAG, "║     Watch the WiFi LED (P%d) - it should blink now         ║", WIFI_LED_PIN);
    ESP_LOGI(TEST_TAG, "║              (Blinking for 5 seconds)                      ║");
    ESP_LOGI(TEST_TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TEST_TAG, "");
    
    // led->test_blink(WIFI_LED_PIN, 500, 5000);
    
    // test_led_wifi_ok = wait_for_yes_no("Did you see the WiFi LED (P1) blinking?", 50000);
    led->setDelay(pdMS_TO_TICKS(50));  // Fast update rate for smooth blinking
    led->setPattern(WIFI_LED_PIN, test_blink_pattern);
    test_led_wifi_ok = wait_for_yes_no("Did you see the WiFi LED (P1) blinking?", 50000);                   // Wait for user response while LED is blinking
    led->setPattern(WIFI_LED_PIN, steady_off);            // Turn off WiFi LED after user responds

    if (test_led_wifi_ok) {
        ESP_LOGI(TEST_TAG, "WiFi LED : PASS");
    } else {
        ESP_LOGI(TEST_TAG, "WiFi LED : FAIL");
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); 

    ESP_LOGI(TEST_TAG, "");
    ESP_LOGI(TEST_TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TEST_TAG, "║                   >>> STATUS LED TEST <<<                  ║");
    ESP_LOGI(TEST_TAG, "║    Watch the Status LED (P%d) - it should blink now        ║", STATUS_LED_PIN);
    ESP_LOGI(TEST_TAG, "║              (Blinking for 5 seconds)                      ║");
    ESP_LOGI(TEST_TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TEST_TAG, "");
    
    // led->test_blink(STATUS_LED_PIN, 500, 5000);
    // test_led_status_ok = wait_for_yes_no("Did you see the Status LED (P3) blinking?", 50000);
    led->setPattern(STATUS_LED_PIN, test_blink_pattern);
    test_led_status_ok = wait_for_yes_no("Did you see the Status LED (P3) blinking?", 50000);
    led->setPattern(STATUS_LED_PIN, steady_off);

    if (test_led_status_ok) {
        ESP_LOGI(TEST_TAG, "Status LED : PASS");
    } else {
        ESP_LOGI(TEST_TAG, "Status LED :FAIL");
    }
    // Restore default delay
    led->setDelay(pdMS_TO_TICKS(1000));
    
    test_led_ok = (test_led_wifi_ok && test_led_status_ok);
    
    ESP_LOGI(TEST_TAG, "");
    ESP_LOGI(TEST_TAG, "========== LED Test Complete ==========");
    if (test_led_ok) {
        ESP_LOGI(TEST_TAG, "All LEDs working correctly");
    } else {
        if (!test_led_wifi_ok && !test_led_status_ok) {
            ESP_LOGI(TEST_TAG, "WiFi LED is not blinking and Status LED is not blinking");
        } else if (!test_led_wifi_ok) {
            ESP_LOGI(TEST_TAG, "WiFi LED : FAIL");
        } else if (!test_led_status_ok) {
            ESP_LOGI(TEST_TAG, "Status LED  : FAIL");
        }
    }
}

void Gateway::test_button_restart_module()
{
    test_button_restart_ok = false;
    enable_test_logs_only();
    
    ESP_LOGI(TEST_TAG, "========== Button Restart Test ==========");
    
    Button& button = Button::getInstance();
    
    if (!gpio_get_level(BUTTON_GPIO_NUM))
    {
        ESP_LOGI(TEST_TAG, "Button GPIO not configured properly");
        return;
    }
    
    ESP_LOGI(TEST_TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TEST_TAG, "║          >>> PLEASE PRESS THE BUTTON NOW <<<               ║");
    ESP_LOGI(TEST_TAG, "║              (Waiting for 10 seconds...)                   ║");
    ESP_LOGI(TEST_TAG, "╚════════════════════════════════════════════════════════════╝");
    
    // Test button press with 10 second timeout
    bool button_detected = button.test_button_press(10);
    
    if (button_detected)
    {
        test_button_restart_ok = true;
        ESP_LOGI(TEST_TAG, "BUTTON TEST : PASS");
    }
    else
    {
        test_button_restart_ok = false;
        ESP_LOGI(TEST_TAG, "BUTTON TEST : FAIL");
    }
}

void Gateway::test_ethernet_module()
{
    ESP_LOGI(TEST_TAG, "================ ...RMII Ethernet Testing Started... =================");
    test_eth_ok = false;

#if ENABLE_RMII_ETH
    InternetModule* internet = InternetModule::get_instance();
    if (!internet) {
        ESP_LOGE(TEST_TAG, "InternetModule Instance is NULL");
        ESP_LOGI(TEST_TAG, "RMII ETH TEST : FAIL");
        return;
    }

    RmiiEthernetModule* rmii = internet->getRmiiModule();
    if (!rmii) {
        ESP_LOGE(TEST_TAG, "RMII Ethernet Module not initialized");
        ESP_LOGI(TEST_TAG, "RMII ETH TEST : FAIL");
        return;
    }

    esp_netif_t* rmii_netif = rmii->getNetif();
    if (!rmii_netif) {
        ESP_LOGE(TEST_TAG, "RMII netif is NULL");
        ESP_LOGI(TEST_TAG, "RMII ETH TEST : FAIL");
        return;
    }
    // Verify interface is up
    if (!esp_netif_is_netif_up(rmii_netif)) {
        ESP_LOGI(TEST_TAG, "RMII ETH TEST : FAIL (Interface not up)");
        return;
    }

    // Read IP Information 
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(rmii_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGI(TEST_TAG, "RMII ETH TEST : FAIL (Cannot get IP info)");
        return;
    }
    
    if (ip_info.ip.addr == 0) {
        ESP_LOGI(TEST_TAG, "RMII ETH TEST : FAIL (No IP address)");
        return;
    }

    // Log IP address
    ESP_LOGI(TEST_TAG, "RMII ETH IP: " IPSTR, IP2STR(&ip_info.ip));

    test_eth_ok = true;
    ESP_LOGI(TEST_TAG, "RMII ETH TEST : PASS (Got Ip Address)");
   
#else
    ESP_LOGI(TEST_TAG, "RMII ETH TEST : FAIL (RMIII ETHERNET not enabled)");
#endif
}

void Gateway::test_wifi_module()
{
    ESP_LOGI(TEST_TAG, "=============== ...WiFi Testing Started... ==============");
    test_wifi_ok = false;

#if ENABLE_WIFI
    WiFiModule* wifi = internet_module_ ? internet_module_->getWifiModule() : nullptr;
    
    if (!wifi) {
        ESP_LOGI(TEST_TAG, "WiFi TEST : FAIL");
        return;
    }

    // Check if WiFi netif exists
    esp_netif_t* wifi_netif = wifi->getNetif();
    if (!wifi_netif) {
        ESP_LOGI(TEST_TAG, "WiFi TEST : FAIL");
        return;
    }

    // Check event group
    EventGroupHandle_t eg = WiFiModule::wifi_event_group_;
    if (!eg) {
        ESP_LOGI(TEST_TAG, "WiFi TEST : FAIL");
        return;
    }

    // Wait for WiFi connection (15 seconds timeout)
    ESP_LOGI(TEST_TAG, "Waiting for WiFi connection (10s timeout)...");
    EventBits_t wifi_event_bits = xEventGroupWaitBits(eg, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
    
    if (!(wifi_event_bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGI(TEST_TAG, "WiFi TEST : FAIL");
        return;
    }

    // Verify interface is up
    if (!esp_netif_is_netif_up(wifi_netif)) {
        ESP_LOGI(TEST_TAG, "WiFi TEST : FAIL");
        return;
    }

    // Verify has IP address
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(wifi_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGI(TEST_TAG, "WiFi TEST : FAIL");
        return;
    }
    
    if (ip_info.ip.addr == 0) {
        ESP_LOGI(TEST_TAG, "WiFi TEST : FAIL");
        return;
    }

    // Log IP for debugging
    ESP_LOGI(TEST_TAG, "WiFi IP: " IPSTR, IP2STR(&ip_info.ip));
    wifi->wifi_display_signal_quality();
    
    test_wifi_ok =true;
    ESP_LOGI(TEST_TAG, "WiFi TEST : PASS");
       
#else
    ESP_LOGI(TEST_TAG, "WiFi TEST : FAIL");
#endif
}

void Gateway::test_gsm_module()
{
    ESP_LOGI(TEST_TAG, "=============== ...GSM Testing Started... ==============");
    test_gsm_ok = false;

#if ENABLE_GSM
    GsmModule* gsm = internet_module_ ? internet_module_->getGsmModule() : gsm_module_;
    
    if (!gsm) {
        ESP_LOGI(TEST_TAG, "GSM TEST : FAIL (Module not available)");
        return;
    }
    // Check if GSM netif exists
    esp_netif_t* gsm_netif = gsm->getPppNetif();
    if (!gsm_netif) {
        ESP_LOGI(TEST_TAG, "GSM TEST : FAIL (Netif not initialized)");
        return;
    }

    // Check event group
    EventGroupHandle_t eg = gsm->getEventGroup();
    if (!eg) {
        ESP_LOGI(TEST_TAG, "GSM TEST : FAIL (Event group not initialized)");
        return;
    }

    // Wait for GSM connection (20 seconds timeout)
    ESP_LOGI(TEST_TAG, "Waiting for GSM connection (20s timeout)...");
    EventBits_t gsm_event_bits = xEventGroupWaitBits(eg, CONNECT_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(20000));
    
    if (!(gsm_event_bits & CONNECT_BIT)) {
        ESP_LOGI(TEST_TAG, "GSM TEST : FAIL (Connection timeout)");
        return;
    }

    // Verify interface is up
    if (!esp_netif_is_netif_up(gsm_netif)) {
        ESP_LOGI(TEST_TAG, "GSM TEST : FAIL (Interface not up)");
        return;
    }

    // Verify has IP address
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(gsm_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGI(TEST_TAG, "GSM TEST : FAIL (Cannot get IP info)");
        return;
    }
    
    if (ip_info.ip.addr == 0) {
        ESP_LOGI(TEST_TAG, "GSM TEST : FAIL (No IP address)");
        return;
    }


    // Log IP address
    ESP_LOGI(TEST_TAG, "GSM IP: " IPSTR, IP2STR(&ip_info.ip));

    test_gsm_ok = true;
    ESP_LOGI(TEST_TAG, "GSM TEST : PASS (Got Ip Address)");
   
#else
    ESP_LOGI(TEST_TAG, "GSM TEST : FAIL (GSM not enabled)");
#endif
}

void Gateway::test_sdcard_module()
{
    test_sd_ok = false;
    enable_test_logs_only();
    ESP_LOGI(TEST_TAG, "=================...SD Card Testing Started...===============");

#if ENABLE_SD

    SDCard* sd = sdcard_ ? sdcard_ : &SDCard::getInstance();

    if (!sd) {
        ESP_LOGI(TEST_TAG, "SD CARD TEST : FAIL");
        test_sd_ok = false;
        return;
    }
    esp_err_t ret = sd->init();
    if (ret != ESP_OK) {
        test_sd_ok = false;
        ESP_LOGI(TEST_TAG, "SD CARD TEST : FAIL");
        return;
    }
    const char* path = "/sdcard/sd_test.txt";
    const char* text = "hello from sd card test\n";

    if (sd->writeFile(path, text) != ESP_OK) {
        test_sd_ok = false;
        ESP_LOGI(TEST_TAG, "SD CARD TEST : FAIL");
        return;
    }

    if (sd->readFile(path) != ESP_OK) {
        test_sd_ok = false;
        ESP_LOGI(TEST_TAG, "SD CARD TEST: FAIL");
        return;
    }

    test_sd_ok = true;
    ESP_LOGI(TEST_TAG, "SD CARD TEST : PASS");

#else
    test_sd_ok = false;
    ESP_LOGI(TEST_TAG, "SD CARD TEST : FAIL");
#endif
}

void Gateway::test_w5500_module()
{
    ESP_LOGI(TEST_TAG, "================ ...W5500 Testing Started... =================");
    test_w5500_ok = false;

#if ENABLE_W5500_ETH
//Deinitialize GSM if enabled (to release shared pins)
#if ENABLE_GSM
    if (gsm_module_) {
        esp_err_t deinit_result = gsm_module_->deinit();
        if (deinit_result == ESP_OK) {
            ESP_LOGI(TEST_TAG, "GSM deinitialized successfully");
          //  ESP_LOGI(TEST_TAG, "Waiting for pins to be released (2 seconds)...");
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for complete cleanup
        } else {
            ESP_LOGE(TEST_TAG, "GSM deinit failed: %s", esp_err_to_name(deinit_result));
            ESP_LOGW(TEST_TAG, "W5500 may experience pin conflicts!");
        }
    } else {
        ESP_LOGW(TEST_TAG, "GSM module pointer is null, skipping deinit");
    }
#endif
   
    //  Initialize W5500 module using init_w5500_module()
    // This will create netif and initialize everything

    ESP_LOGI(TEST_TAG, "");
    ESP_LOGI(TEST_TAG, "Initializing W5500 module...");
    
    InternetModule* im = InternetModule::get_instance();
    if (!im) {
        ESP_LOGE(TEST_TAG, "InternetModule not available");
        ESP_LOGI(TEST_TAG, "W5500 TEST : FAIL (InternetModule not available)");
        return;
    }
    
    // Call init_w5500_module() which internally calls w5500_module_->init()
    // This creates netif, registers with NetworkManager, and starts W5500
    if (!im->init_w5500_module()) {
        ESP_LOGE(TEST_TAG, "W5500 initialization failed");
        ESP_LOGI(TEST_TAG, "W5500 TEST : FAIL (Initialization failed)");
        return;
    }
    
    ESP_LOGI(TEST_TAG, "W5500 initialized successfully");
    ESP_LOGI(TEST_TAG, "Waiting for W5500 to stabilize and get IP (10 seconds)...");
    vTaskDelay(pdMS_TO_TICKS(10000)); // Wait for W5500 to get IP address
    
    // Verify W5500 is working (netif, IP, ping)
    // Get W5500 module
    W5500EthernetModule* w5500 = im->getW5500Module();
    if (!w5500) {
        ESP_LOGE(TEST_TAG, "W5500 module pointer is NULL!");
        ESP_LOGI(TEST_TAG, "W5500 ETH TEST : FAIL (Module not available)");
        return;
    }
    
    // Get netif from NetworkManager (more reliable than w5500->getNetif())
    //NetworkManager* nm = NetworkManager::get_instance();
    //esp_netif_t* netif = nm ? nm->getNetif(NETWORK_INTERFACE_W5500_ETHERNET) : nullptr;
    esp_netif_t* w5500_netif = w5500->getNetif();
    if (!w5500_netif) {
        ESP_LOGE(TEST_TAG, "W5500 netif is NULL!");
        ESP_LOGI(TEST_TAG, "W5500 ETH TEST : FAIL (Netif not created)");
        return;
    }
    
   // ESP_LOGI(TEST_TAG, "W5500 netif pointer: %p", (void*)netif);
    
    // Verify interface is up
    bool is_up = esp_netif_is_netif_up(w5500_netif);
    ESP_LOGI(TEST_TAG, "W5500 interface is_up: %s", is_up ? "YES" : "NO");
    
    if (!is_up) {
        ESP_LOGW(TEST_TAG, "Interface is DOWN, waiting additional 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        is_up = esp_netif_is_netif_up(w5500_netif);
        ESP_LOGI(TEST_TAG, "After wait, is_up: %s", is_up ? "YES" : "NO");
    }
    
    if (!is_up) {
        ESP_LOGI(TEST_TAG, "W5500 ETH TEST : FAIL (Interface not up)");
        return;
    }

    // Verify has IP address
    esp_netif_ip_info_t ip_info = {};
    esp_err_t ret = esp_netif_get_ip_info(w5500_netif, &ip_info);
    ESP_LOGI(TEST_TAG, "esp_netif_get_ip_info returned: %s", esp_err_to_name(ret));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TEST_TAG, "Failed to get IP info!");
        ESP_LOGI(TEST_TAG, "W5500 ETH TEST : FAIL (Cannot get IP info)");
        return;
    }
    
   // ESP_LOGI(TEST_TAG, "IP address value: 0x%08lx", (unsigned long)ip_info.ip.addr);
    
    if (ip_info.ip.addr == 0) {
        ESP_LOGI(TEST_TAG, "W5500 ETH TEST : FAIL (No IP address)");
        return;
    }
    // Log IP for debugging
    ESP_LOGI(TEST_TAG, "W5500 ETH IP: " IPSTR, IP2STR(&ip_info.ip));

    test_w5500_ok = true;
    ESP_LOGI(TEST_TAG, "W5500 ETH TEST : PASS (Got IP address)");

#else
    ESP_LOGI(TEST_TAG, "W5500 TEST : FAIL (W5500 not enabled)");
#endif
}

void Gateway::test_spi_module()
{
    ESP_LOGI(TEST_TAG, "================ ...ENC28j60 Testing Started... =================");
    test_spi_ok = false;

#if ENABLE_SPI_ETH
//Deinitialize GSM if enabled (to release shared pins)
#if ENABLE_GSM
    if (gsm_module_) {
        esp_err_t deinit_result = gsm_module_->deinit();
        if (deinit_result == ESP_OK) {
            ESP_LOGI(TEST_TAG, "GSM deinitialized successfully");
          //  ESP_LOGI(TEST_TAG, "Waiting for pins to be released (2 seconds)...");
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for complete cleanup
        } else {
            ESP_LOGE(TEST_TAG, "GSM deinit failed: %s", esp_err_to_name(deinit_result));
            ESP_LOGW(TEST_TAG, "ENC28j60 may experience pin conflicts!");
        }
    } else {
        ESP_LOGW(TEST_TAG, "GSM module pointer is null, skipping deinit");
    }
#endif
   
    //  Initialize enc28j60 module using init_enc28j60_module()
    // This will create netif and initialize everything

    //ESP_LOGI(TEST_TAG, "");
    ESP_LOGI(TEST_TAG, "Initializing ENC28j60 module...");
    
    InternetModule* im = InternetModule::get_instance();
    if (!im) {
        ESP_LOGE(TEST_TAG, "InternetModule not available");
        ESP_LOGI(TEST_TAG, "ENC28j60 TEST : FAIL (InternetModule not available)");
        return;
    }
    
    // Call init_enc28j60 _module() which internally calls enc28j60_module_->init()
    // This creates netif, registers with NetworkManager, and starts W5500
    if (!im->init_enc28j60_module()) {
        ESP_LOGE(TEST_TAG, "ENC28j60  initialization failed");
        ESP_LOGI(TEST_TAG, "ENC28j60  TEST : FAIL (Initialization failed)");
        return;
    }
    
    ESP_LOGI(TEST_TAG, "ENC28j60  initialized successfully");
    ESP_LOGI(TEST_TAG, "Waiting for ENC28j60  to stabilize and get IP (10 seconds)...");
    vTaskDelay(pdMS_TO_TICKS(10000)); // Wait for ENC28j60  to get IP address
    
    // Verify ENC28j60  is working (netif, IP, ping)
    // Get ENC28j60  module
    SpiEthernetModule* spi = im->getSpiModule();
    if (!spi) {
        ESP_LOGE(TEST_TAG, "ENC28j60  module pointer is NULL!");
        ESP_LOGI(TEST_TAG, "ENC28j60 TEST : FAIL (Module not available)");
        return;
    }
    
    // Get netif from NetworkManager (more reliable than w5500->getNetif())
    NetworkManager* nm = NetworkManager::get_instance();
    esp_netif_t* netif = nm ? nm->getNetif(NETWORK_INTERFACE_SPI_ETHERNET) : nullptr;
    
    if (!netif) {
        ESP_LOGE(TEST_TAG, "ENC28j60  netif is NULL!");
        ESP_LOGI(TEST_TAG, "ENC28j60  TEST : FAIL (Netif not created)");
        return;
    }
    
   // ESP_LOGI(TEST_TAG, "ENC28j60  netif pointer: %p", (void*)netif);
    
    // Verify interface is up
    bool is_up = esp_netif_is_netif_up(netif);
    ESP_LOGI(TEST_TAG, "W5500 interface is_up: %s", is_up ? "YES" : "NO");
    
    if (!is_up) {
        ESP_LOGW(TEST_TAG, "Interface is DOWN, waiting additional 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        is_up = esp_netif_is_netif_up(netif);
        ESP_LOGI(TEST_TAG, "After wait, is_up: %s", is_up ? "YES" : "NO");
    }
    
    if (!is_up) {
        ESP_LOGI(TEST_TAG, "W5500 ETH TEST : FAIL (Interface not up)");
        return;
    }

    // Verify has IP address
    esp_netif_ip_info_t ip_info = {};
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    ESP_LOGI(TEST_TAG, "esp_netif_get_ip_info returned: %s", esp_err_to_name(ret));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TEST_TAG, "Failed to get IP info!");
        ESP_LOGI(TEST_TAG, "ENC28j60 TEST : FAIL (Cannot get IP info)");
        return;
    }
    
   // ESP_LOGI(TEST_TAG, "IP address value: 0x%08lx", (unsigned long)ip_info.ip.addr);
    
    if (ip_info.ip.addr == 0) {
        ESP_LOGI(TEST_TAG, "ENC28j60 TEST : FAIL (No IP address)");
        return;
    }
    // Log IP for debugging
    ESP_LOGI(TEST_TAG, "ENC28j60  IP: " IPSTR, IP2STR(&ip_info.ip));

    test_spi_ok = true;
    ESP_LOGI(TEST_TAG, "ENC28j60 TEST : PASS (Got IP address)");

#else
    ESP_LOGI(TEST_TAG, "ENC28j60  TEST : FAIL (ENC28j60  not enabled)");
#endif
}
void Gateway::test_rtc_module()
{
    
#if ENABLE_W5500_ETH
    test_rtc_ok = false;
    enable_test_logs_only();
    
    ESP_LOGI(TEST_TAG, "================ ...RTC Testing Started... =================");
    
    RTCController* rtc = RTCController::get_instance();
    if (!rtc) {
        ESP_LOGI(TEST_TAG, "RTC TEST : FAIL (RTC not available)");
        return;
    }

    const char *days[] = {"", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  
    //  READ from RTC
    ESP_LOGI(TEST_TAG, "TEST 1: Reading from RTC...");
    RTCDateTime read_time;
    if (!rtc->getDateTime(read_time)) {
        ESP_LOGI(TEST_TAG, "RTC TEST : FAIL (Read failed)");
        return;
    }
    
    ESP_LOGI(TEST_TAG, "Read from RTC: %s %04d-%02d-%02d %02d:%02d:%02d",days[read_time.day], read_time.year, read_time.month, read_time.date,read_time.hour, read_time.minute, read_time.second);
    
    // WRITE to RTC (set a known test time)
   
    ESP_LOGI(TEST_TAG, "Writing test time to RTC...");
    
    // Create a test time to write (2025-12-05 14:30:00 Thursday)
    RTCDateTime test_time;
    test_time.year = 2025;
    test_time.month = 12;
    test_time.date = 5;
    test_time.day = 5;  // Thursday
    test_time.hour = 10;
    test_time.minute = 30;
    test_time.second = 0;
    
    if (!rtc->setDateTime(test_time)) {
        ESP_LOGI(TEST_TAG, "RTC TEST : FAIL (Write failed)");
        return;
    }
    
    ESP_LOGI(TEST_TAG, "Written to RTC: %s %04d-%02d-%02d %02d:%02d:%02d",days[test_time.day], test_time.year, test_time.month, test_time.date, test_time.hour, test_time.minute, test_time.second);
    
    vTaskDelay(pdMS_TO_TICKS(100));
   
    //  READ again to verify write
    ESP_LOGI(TEST_TAG, "TEST 3: Reading back to verify write...");
    RTCDateTime verify_time;
    if (!rtc->getDateTime(verify_time)) {
        ESP_LOGI(TEST_TAG, "RTC TEST : FAIL (Read verification failed)");
        return;
    }
    
    ESP_LOGI(TEST_TAG, "Verify read: %s %04d-%02d-%02d %02d:%02d:%02d", days[verify_time.day], verify_time.year, verify_time.month,  verify_time.date, verify_time.hour, verify_time.minute, verify_time.second);
    
    // Verify the written time matches
    bool time_matches = (verify_time.year == test_time.year &&verify_time.month == test_time.month &&verify_time.date == test_time.date &&verify_time.hour == test_time.hour &&verify_time.minute == test_time.minute);
    
    if (time_matches) {
        test_rtc_ok = true;
        ESP_LOGI(TEST_TAG, "RTC TEST : PASS (Read and Write both working)");
    } else {
        ESP_LOGI(TEST_TAG, "RTC TEST : FAIL (Written time doesn't match read time)");
    }

#endif

}

void Gateway::print_test_summary()
{   
    ESP_LOGI(TEST_TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TEST_TAG, "                          TEST SUMMARY                        ");
    ESP_LOGI(TEST_TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TEST_TAG, "LED              : %s", test_led_ok ? "PASS" : "FAIL");
    ESP_LOGI(TEST_TAG, "  - WiFi LED     : %s", test_led_wifi_ok ?  "PASS" : "FAIL");
    ESP_LOGI(TEST_TAG, "  - Status LED   : %s", test_led_status_ok ?  "PASS" : "FAIL");
    ESP_LOGI(TEST_TAG, "WiFi             : %s", test_wifi_ok ?  "PASS" : "FAIL");
    ESP_LOGI(TEST_TAG, "SD Card          : %s", test_sd_ok ?  "PASS" : "FAIL");
    ESP_LOGI(TEST_TAG, "RMII Ethernet    : %s", test_eth_ok ?  "PASS" : "FAIL");
#if ENABLE_SPI_ETH
    ESP_LOGI(TEST_TAG, "ENC28J60 Ethernet: %s", test_spi_ok ? "PASS" : "FAIL");
#endif
#if ENABLE_W5500_ETH
    ESP_LOGI(TEST_TAG, "W5500 Ethernet   : %s", test_w5500_ok ?  "PASS" : "FAIL");
    ESP_LOGI(TEST_TAG, "RTC              : %s", test_rtc_ok ? "PASS" : "FAIL");
#endif
    ESP_LOGI(TEST_TAG, "GSM              : %s", test_gsm_ok ?  "PASS" : "FAIL");
    ESP_LOGI(TEST_TAG, "Modbus           : %s", test_modbus_ok ? "PASS" : "FAIL");
    ESP_LOGI(TEST_TAG, "Button Restart   : %s", test_button_restart_ok ?  "PASS" : "FAIL");
    ESP_LOGI(TEST_TAG, "================================================================");

// Count successful tests
    int passed = 0;
    int total = 0;
    
    if (test_led_ok) passed++;
    total++;
    if (test_button_restart_ok) passed++;
    total++;
#if ENABLE_WIFI
    if (test_wifi_ok) passed++;
    total++;
#endif
#if ENABLE_SD
   if (test_sd_ok) passed++;
   total++;
#endif
#if ENABLE_RMII_ETH
    if (test_eth_ok) passed++;
    total++;
#endif
#if ENABLE_SPI_ETH
    if (test_spi_ok) passed++;
    total++;
#endif
#if ENABLE_W5500_ETH
    if (test_w5500_ok) passed++; 
    total++;
    if (test_rtc_ok) passed++; 
    total++;      
#endif
#if ENABLE_GSM
    if (test_gsm_ok) passed++;
    total++;
#endif
                
    if (test_modbus_ok) passed++;
    total++;

    ESP_LOGI(TEST_TAG, "Tests Passed: %d/%d", passed, total);
    ESP_LOGI(TEST_TAG, "===========================================================");
}

// ---------- Run all tests sequentially (calls each test and prints summary) ----------
void Gateway::run_all_module_tests()
{
    enable_test_logs_only();
    pause_rtc_display(true);
    test_led_ok = false;
    test_led_wifi_ok = false;
    test_led_status_ok = false;
    test_wifi_ok = false;
    test_sd_ok = false;
    test_gsm_ok = false;
    test_eth_ok = false;
    test_spi_ok = false;
    test_w5500_ok = false;
    test_modbus_ok = false;  
    test_rtc_ok = false; 
    test_button_restart_ok = false;

    UIDModule uid; uid.init();

     // ========== Display Version / Config Info ==========
    ESP_LOGI(TEST_TAG, "");
    ESP_LOGI(TEST_TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TEST_TAG, "║                 >>> BOARD VERSION NOTE <<<                 ║");
#if BOARD_VERSION_1_0
    ESP_LOGI(TEST_TAG, "║  Firmware compiled as BOARD_VERSION_1_0                    ║");
    ESP_LOGI(TEST_TAG, "║  HW : RMII + ENC28J60 (SPI Ethernet)                       ║");
    ESP_LOGI(TEST_TAG, "║  SDKCONFIG:                                                ║");
    ESP_LOGI(TEST_TAG, "║    - ENC28J60 driver must be ENABLED                       ║");
    ESP_LOGI(TEST_TAG, "║    - W5500 driver must be DISABLED                         ║");
#elif BOARD_VERSION_2_0
    ESP_LOGI(TEST_TAG, "║  Firmware compiled as BOARD_VERSION_2_0                    ║");
    ESP_LOGI(TEST_TAG, "║  HW : RMII + W5500 (SPI Ethernet)                          ║");
    ESP_LOGI(TEST_TAG, "║  SDKCONFIG:                                                ║");
    ESP_LOGI(TEST_TAG, "║    - W5500 driver must be ENABLED                          ║");
    ESP_LOGI(TEST_TAG, "║    - ENC28J60 driver must be DISABLED                      ║");
#endif
    ESP_LOGI(TEST_TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TEST_TAG, "");

    // ========== Ask user which board version is on table ==========
    int board_version = 0; // 1 = v1.0, 2 = v2.0

    if (wait_for_yes_no("Is this Gateway board Version 1.0 (RMII + ENC28J60)?", 60000)) {
        board_version = 1;
    } else if (wait_for_yes_no("Is this Gateway board Version 2.0 (RMII + W5500)?", 60000)) {
        board_version = 2;
    } else {
        ESP_LOGE(TEST_TAG, "");
        ESP_LOGE(TEST_TAG, "╔════════════════════════════════════════════════════════════╗");
        ESP_LOGE(TEST_TAG, "║              >>> TEST ABORTED <<<                          ║");
        ESP_LOGE(TEST_TAG, "║   Board version not confirmed (neither 1.0 nor 2.0)        ║");
        ESP_LOGE(TEST_TAG, "╚════════════════════════════════════════════════════════════╝");
        ESP_LOGE(TEST_TAG, "");
        pause_rtc_display(false);
        return;
    }

    // ========== Display Prerequisites ==========
    ESP_LOGI(TEST_TAG, "");
    ESP_LOGI(TEST_TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TEST_TAG, "║              >>> GATEWAY TESTING PREREQUISITES <<<         ║");
    ESP_LOGI(TEST_TAG, "║                                                            ║");
    ESP_LOGI(TEST_TAG, "║     Before starting the test, please ensure:               ║");
    ESP_LOGI(TEST_TAG, "║                                                            ║");
    ESP_LOGI(TEST_TAG, "║  1. WiFi/GSM Antenna is properly attached                  ║");
    ESP_LOGI(TEST_TAG, "║  2. SD Card is inserted into the SD card slot              ║");
    ESP_LOGI(TEST_TAG, "║  3. SIM Card is inserted (for GSM test)                    ║");
    ESP_LOGI(TEST_TAG, "║  4. USB Cable is connected to PC                           ║");
    ESP_LOGI(TEST_TAG, "║  5. Ethernet Cable is inserted in the Ethernet port        ║");
    ESP_LOGI(TEST_TAG, "║  6. Modbus Simulator connected via RS485 (A, B, GND)       ║");
    ESP_LOGI(TEST_TAG, "║  7. Power supply is stable and adequate                    ║");
    ESP_LOGI(TEST_TAG, "║                                                            ║");
    ESP_LOGI(TEST_TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TEST_TAG, "");
    
    // Ask user to confirm prerequisites
    bool prerequisites_ok = wait_for_yes_no("Have you completed all the prerequisites listed above?", 60000);
    
    if (!prerequisites_ok) {
        ESP_LOGE(TEST_TAG, "");
        ESP_LOGE(TEST_TAG, "╔════════════════════════════════════════════════════════════╗");
        ESP_LOGE(TEST_TAG, "║              >>> TEST ABORTED <<<                          ║");
        ESP_LOGE(TEST_TAG, "║   Please complete all prerequisites and restart the test   ║");
        ESP_LOGE(TEST_TAG, "╚════════════════════════════════════════════════════════════╝");
        ESP_LOGE(TEST_TAG, "");
        return;  // Exit test if prerequisites not met
    }


    ESP_LOGI(TEST_TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TEST_TAG, "║                        GATEWAY ID : %s                     ║", uid.getUID().c_str());
    ESP_LOGI(TEST_TAG, "║                SELECTED BOARD VERSION : %s                 ║",
                      (board_version == 1) ? "1.0 (RMII + ENC28J60)" : "2.0 (RMII + W5500)");
    ESP_LOGI(TEST_TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TEST_TAG, "|---------------...Gateway Testing Started...----------------");

     vTaskDelay(pdMS_TO_TICKS(2000));  // Give user time to read
    
    test_led_module();                      // 1) LED
    test_wifi_module();                       // 2) WiFi       
    test_sdcard_module();                    // 3) SD Card
    test_ethernet_module();                     // 4)RMII Ethernet
    test_gsm_module();                          // 6) GSM
    test_modbus_module();                            // 7) Modbus RS485 Test 
  
      // ========== Version-specific SPI Ethernet test ==========
    if (board_version == 1) {
#if ENABLE_SPI_ETH
        test_spi_module();      // ENC28J60 (Version 1.0)
#endif
    } else if (board_version == 2) {
#if ENABLE_W5500_ETH
        test_w5500_module();    // W5500 (Version 2.0)
        test_rtc_module();                      // 8) RTC -timer
#endif
    }
    test_button_restart_module();                   // 9) Button restart
    print_test_summary();                           // Print final summary 
    pause_rtc_display(false);
   // ESP_LOGI(TEST_TAG, "INSIDE RUN ALL MODULES");
    restore_log_levels();
}

bool Gateway::init()
{
    ESP_LOGI(TAG, "Initializing Gateway");
    ESP_LOGI(TAG, "Free heap before init: %lu", (unsigned long)esp_get_free_heap_size());

    // Initialize the LedController
    if (!LedController::get_instance()->init())
    {
        ESP_LOGE(TAG, "Failed to initialize LedController");
        return false;
    }
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize InternetModule
    if (!internet_module_->init())
    {
        ESP_LOGE(TAG, "Failed to initialize InternetModule");
        return false;
    }

    sdcard_ = &SDCard::getInstance();
    esp_err_t sd_ret = sdcard_->init();
        
    if (sd_ret == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted at /sdcard");
        
        const char* kTestPath = "/sdcard/test.txt";
        if (sdcard_->writeFile(kTestPath, "hello from gateway sd test\n") == ESP_OK) {
            if (sdcard_->readFile(kTestPath) == ESP_OK) {
                ESP_LOGI(TAG, "SD read/write test OK");
            } else {
                ESP_LOGW(TAG, "SD read test failed");
            }
        } else {
            ESP_LOGW(TAG, "SD write test failed");
        }
    } else {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(sd_ret));
        // return false; // if you want SD to be mandatory
    }

#if ENABLE_W5500_ETH
    RTCController* rtc = RTCController::get_instance();
    if (!rtc || !rtc->init()) {
        ESP_LOGE(TAG, "Failed to initialize RTC Controller");
    } else {
        ESP_LOGI(TAG, "RTC Controller initialized successfully");
        
        // Sync RTC with current time from internet (handles WiFi check internally)
        rtc->syncTimeFromInternet();
        
        // Start RTC display task
        if (rtc_display_task_handle_ == nullptr) {
            xTaskCreate(&rtc_display_task_wrapper, "RTCDisplayTask", 4096, this, 4, &rtc_display_task_handle_);
            ESP_LOGI(TAG, "RTC display task started");
        }
    }
#endif
    // Start Gateway task
    const uint32_t GATEWAY_TASK_STACK_SIZE = 12288;
    const UBaseType_t GATEWAY_TASK_PRIORITY = 5;
    if (xTaskCreate(&gateway_task_wrapper, "GatewayTask", GATEWAY_TASK_STACK_SIZE, this, GATEWAY_TASK_PRIORITY, &gateway_task_handle_) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create GatewayTask");
        return false;
    }

    ESP_LOGI(TAG, "Gateway initialized successfully");
    ESP_LOGI(TAG, "Stack high water mark after init: %u", uxTaskGetStackHighWaterMark(NULL));

    return true;
}


void Gateway::gateway_task_wrapper(void *param)
{
    Gateway *gateway = static_cast<Gateway *>(param);
    gateway->gateway_task();
}

void Gateway::gateway_task()
{
    ESP_LOGI(TAG, "Gateway task started");

    while (true)
    {
        ESP_LOGI(TAG, "Gateway is running...");

        vTaskDelay(pdMS_TO_TICKS(state_ == GATEWAY_STATE_DISCONNECTED ? 5000 : 10000));
    }
}

Gateway::~Gateway()
{
    if (!heap_caps_check_integrity_all(true))
    {
        ESP_LOGE(TAG, "Failed to delete heap memory");
    }

    if (gateway_task_handle_ != NULL)
    {
        vTaskDelete(gateway_task_handle_);
        gateway_task_handle_ = NULL;
    }

    if (modbus_serial_handle_ != nullptr && modbus_initialized_) {
        ESP_LOGI(TAG, "Stopping Modbus master");
        mbc_master_destroy();  // Changed from mbc_master_stop
        modbus_serial_handle_ = nullptr;
        modbus_initialized_ = false;
    }
    
    ESP_LOGI(TAG, "Deleting Gateway instance");

    internet_module_ = nullptr;

    instance_ = NULL;
}
