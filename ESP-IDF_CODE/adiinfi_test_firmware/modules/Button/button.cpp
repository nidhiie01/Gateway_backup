#include "button.h"

// Spinlock for button flags (shared between ISR and tasks)
 portMUX_TYPE s_button_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Singleton instance
Button &Button::getInstance()
{
    static Button instance;
    return instance;
}

// ISR handler static wrapper
void Button::isr_handler_static(void *arg)
{
    getInstance().button_isr_handler();
}

// ISR handler
void Button::button_isr_handler()
{
    int level = gpio_get_level(BUTTON_GPIO_NUM);

    portENTER_CRITICAL_ISR(&s_button_spinlock);
    if (level == PRESSED_LEVEL)
    {
        // Button moved to PRESSED state
        button_pressed_flag  = true;
    }
    else
    {
        // Button moved to RELEASED state
        button_released_flag = true;
    }
    portEXIT_CRITICAL_ISR(&s_button_spinlock);
}

void Button::init()
{
    // Initialize GPIO for button
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE; // Trigger on falling edge
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUTTON_GPIO);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Enable pull-up resistor
    gpio_config(&io_conf);

    // Install ISR handler
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(BUTTON_GPIO, isr_handler_static, NULL);
}

bool Button::test_button_press(uint32_t timeoutInSec)
{
    // Clear the pressed flag with lock
    portENTER_CRITICAL(&s_button_spinlock);
    button_pressed_flag = false;
    portEXIT_CRITICAL(&s_button_spinlock);
    
    ESP_LOGI(TAG, "Waiting for button press (timeout: %lu sec)...", timeoutInSec);
    
    uint32_t elapsedTimeInSec = 0;
    uint32_t delayInLoopInSec = 1;  // Check every 1 second
    
    // Wait for button press or timeout
    while (elapsedTimeInSec < timeoutInSec)
    {
        // Check flag with lock
        bool pressed = false;
        portENTER_CRITICAL(&s_button_spinlock);
        pressed = button_pressed_flag;
        portEXIT_CRITICAL(&s_button_spinlock);
        
        if (pressed)
        {
            ESP_LOGI(TAG, "Button press detected!");
            
            // Clear flag for next use
            portENTER_CRITICAL(&s_button_spinlock);
            button_pressed_flag = false;
            portEXIT_CRITICAL(&s_button_spinlock);
            
            return true;
        }
        
        // Wait 1 second before checking again
        vTaskDelay(pdMS_TO_TICKS(delayInLoopInSec * 1000));
        elapsedTimeInSec += delayInLoopInSec;
    }
    
    // Timeout - button was not pressed
    ESP_LOGW(TAG, "Button press timeout");
    return false;
}

// void Button::handle_button_events()
// {
//     // Handle PRESSED event
//     if (button_pressed_flag) {
//         button_pressed_flag = false;  // Reset flag immediately
        
//         // Double-check GPIO level to avoid spurious interrupts
//         int level = gpio_get_level(BUTTON_GPIO);
//         if (level == PRESSED_LEVEL) {
//             ESP_LOGI(TAG, "Button pressed detected");
//              press_time_us = esp_timer_get_time();  // Record press time
//         }
//     }

//     // Handle RELEASED event
//     if (button_released_flag) {
//         button_released_flag = false;  // Reset flag immediately
        
//         // Double-check GPIO level
//         int level = gpio_get_level(BUTTON_GPIO);
//         if (level == RELEASED_LEVEL) {
//             release_time_us = esp_timer_get_time();  // Record release time
            
//             if (press_time_us != 0) {
//                 // Calculate duration in seconds
//                 int64_t diff_us = release_time_us - press_time_us;
//                 int duration_sec = (int)(diff_us / 1000000);
                
//                 ESP_LOGI(TAG, "Button released after %d seconds", duration_sec);
                
//                 // Perform action based on duration
//                 handle_button_action(duration_sec); //  have to create this function for the diffrent second press what have to peform 
                
//                 // Reset timing variables
//                 press_time_us = 0;
//                 release_time_us = 0;
//             }
//         }
//     }
// }


