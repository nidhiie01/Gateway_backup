#include "gateway.h"
#include "../WiFiModule/WiFiModule.h"
#include "../InternetModule/InternetModule.h"
#include "../MacroConfig/MacroConfig.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "LedController.h"
#include "button.h"
#include "esp_system.h"

// Declaration of the button instance
Button &button = Button::getInstance();

void setup()
{
    ESP_LOGI("HEAP", "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    // Set log level early
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("*", ESP_LOG_INFO);

    LedController::get_instance(); // Turn off all LEDs at startup

    // Validate initial heap
    if (!heap_caps_check_integrity_all(true))
    {
        LOG_CRITICAL("Initial heap corruption detected");
        return;
    }

    // Initialize modules
    InternetModule *internet_module = InternetModule::get_instance();

    // Check for allocation failures
    if (!internet_module)
    {
        LOG_CRITICAL("Failed to allocate memory for internet module");
        // Clean up allocated modules

        internet_module = nullptr;
        return;
    }

    LOG_INFO("Modules initialized successfully");

    Gateway *gateway = Gateway::get_instance(internet_module);
    if (!gateway)
    {
        LOG_CRITICAL("Failed to get Gateway instance");
        // Clean up allocated modules

        internet_module = nullptr;
        return;
    }

    LOG_INFO("Stack high water mark after init: %u", uxTaskGetStackHighWaterMark(NULL));
    
    // Initialize gateway
    if (!gateway->init())
    {
        LOG_CRITICAL("Gateway init failed");
        internet_module = nullptr;
        return;
    }
  
    internet_module = nullptr; // Gateway now owns the InternetModule
    // Initialize the button handler
    button.init();

    ESP_LOGI("MAIN", "Button handler initialized");
    
    ESP_LOGI("MAIN", "Waiting 5 seconds for all modules to initialize...");
    vTaskDelay(pdMS_TO_TICKS(5000));
  
    gateway->run_all_module_tests();
    ESP_LOGI("MAIN", "TEST MODULE GET COMPLETED!");

}

void loop()
{
    // while (true)
    // {
    //     ESP_LOGI("MAIN", ".");
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }
    //Run the button handler
    // button.run();
    uint8_t press_counter = 0;   // counts seconds while button is pressed
    
    while (true)
    {
        bool is_pressed = false;
        
        // Read the flag with lock (shared with ISR)
        portENTER_CRITICAL(&s_button_spinlock);
        is_pressed = button.button_pressed_flag;
        portEXIT_CRITICAL(&s_button_spinlock);
        
        if (is_pressed)
        {
            press_counter++;
            ESP_LOGI("MAIN", "Button pressed, counter = %d", press_counter);
            
            if (press_counter >= 2)
            {
                ESP_LOGI("MAIN", "Button pressed for 2 seconds, restarting system...");
               // vTaskDelay(pdMS_TO_TICKS(2000));  // small delay
                esp_restart();
            }
        }
        else
        {
            if (press_counter != 0)
            {
                ESP_LOGI("MAIN", "Button released, counter reset");
            }
            press_counter = 0;
        }
        
        // Called every 1 second
      //  vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
}

extern "C" void app_main()
{
    setup();
    //ESP_LOGI("MAIN", "BEFORE LOOP");
    loop();
    // while (true)
    // {
    //     loop();
    // }
    
    
}