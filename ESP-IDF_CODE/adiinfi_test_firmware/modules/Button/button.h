#ifndef BUTTON_H
#define BUTTON_H

#include <string>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_err.h"
#include <dirent.h>
#include <unistd.h>
#include "../MacroConfig/MacroConfig.h"
#define PRESSED_LEVEL  0
#define RELEASED_LEVEL  1

extern portMUX_TYPE s_button_spinlock;

class Button {
public:
    static Button& getInstance();   // Singleton accessor
    bool test_button_press(uint32_t timeoutInSec = 10);  // Test button  functionality
    volatile bool button_pressed_flag  = false;   // becomes true when a "press" is detected in ISR
    volatile bool button_released_flag = false;   // becomes true when a "release" is detected in ISR
    void init();
  //  void run();


private:
    Button() = default;
    ~Button() = default;
    Button(const Button&) = delete;
    Button& operator=(const Button&) = delete;

    static constexpr gpio_num_t BUTTON_GPIO = BUTTON_GPIO_NUM;
    //static constexpr int DEBOUNCE_DELAY_US = BUTTON_DEBOUNCE_DELAY_US;
    static constexpr const char* TAG = "Button";

    //volatile bool button_pressed_flag  = false;   // becomes true when a "press" is detected in ISR
   // volatile bool button_released_flag = false;   // becomes true when a "release" is detected in ISR

    // Device info
    // std::string gatewayName = "IoTGateway";
    // std::string modelCode = "ESP32-WROOM-32D";
    // char uid[9] = {0};
    // char wifiMac[18] = {0};
    // char bleMac[18] = {0};
    // char deviceMac[18] = {0};
    // uint64_t esp32_chip_id = 0;
   // bool deviceConnected = false;
    // uint16_t my_chr_handle;

    // Timing variables
    // int64_t press_time_us = 0;
    // int64_t release_time_us = 0;


    static void IRAM_ATTR isr_handler_static(void *arg); // forward to instance
    void IRAM_ATTR button_isr_handler();
 
   // void button_handler_function();
   // void handle_button_event(int gpio_level, int64_t now_ms);
   // void handle_button_action(uint64_t duration_ms);
    // Event handler - called from run()
   // void handle_button_events();
    
    // Action handlers based on duration
 //   void handle_button_action(int duration_sec);


};

#endif // BUTTON_H