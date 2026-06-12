#pragma once

#include "driver/i2c.h"
#include "esp_log.h"
#include "i2c_init.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
#include <vector>
#include <atomic>
#include <cstring>
#include "../MacroConfig/MacroConfig.h"


// ---- LED Pattern Types ----
struct BlinkStep
{
    bool state;           // true=HIGH(OFF, open-drain), false=LOW(ON)
    uint32_t duration_ms; // Duration to hold this state
};

enum class LedPatternType
{
    OFF,
    ON,
    BLINK,
    CUSTOM
};

struct LedPattern
{
    LedPatternType type = LedPatternType::OFF;
    uint32_t blink_interval_ms = 500;
    std::vector<BlinkStep> custom_sequence;
};


class LedController
{
public:
    // Delete copy & move
    LedController(const LedController &) = delete;
    LedController &operator=(const LedController &) = delete;
    void test_blink(uint8_t led_idx, uint32_t blink_interval_ms, uint32_t duration_ms);
    
    static LedController *get_instance();

    bool init();
    void setPattern(uint8_t led_idx, const LedPattern &pattern);
    void setAllPatterns(const std::vector<LedPattern> &patterns);
    void setDelay(TickType_t new_delay);
    bool sd_init_failed = false;       // Track if SD card init failed
    bool eth_rmii_init_failed = false; // Track if RMII Ethernet init failed
    bool eth_spi_init_failed = false;  // Track if SPI Ethernet init failed
    bool wifi_init_failed = false;     // Track if Wi-Fi init failed


private:
    static LedController *instance_;

    LedController();
    ~LedController();

    // Low level I2C/PCF8574
    esp_err_t write(uint8_t data);
    esp_err_t read(uint8_t &data);

    // Blinking logic
    static void ledTask(void *arg);
    void updatePins();
    void updatePin(uint8_t led);

    // State
    LedPattern current_patterns[LED_COUNT];
    size_t custom_pattern_pos[LED_COUNT] = {0};
    TickType_t custom_next_tick[LED_COUNT] = {0};
    uint8_t pcf_state{0xFF};
    SemaphoreHandle_t mutex;
    TaskHandle_t taskHandle = nullptr;
    std::atomic<bool> running{false};
    TickType_t delay = 1000; // Default delay for task notifications
};
// LED patterns for different states (declared here, defined in .cpp)
extern LedPattern steady_on;
extern LedPattern steady_off;
extern LedPattern blink2000;
extern LedPattern blink3000;
extern LedPattern blink5000;

extern std::vector<BlinkStep> sd_init_failed;
extern std::vector<BlinkStep> eth_rmii_init_failed;
extern std::vector<BlinkStep> eth_spi_init_failed;
