#include "LedController.h"

static const char *TAG = "LedController";

LedController *LedController::instance_ = nullptr;

// LED patterns
LedPattern steady_on{LedPatternType::ON, 0, {}};
LedPattern steady_off{LedPatternType::OFF, 0, {}};
LedPattern blink2000{LedPatternType::BLINK, 2000, {}};
LedPattern blink3000{LedPatternType::BLINK, 3000, {}};
LedPattern blink5000{LedPatternType::BLINK, 5000, {}};

std::vector<BlinkStep> sd_init_failed_pattern = {
    {true, 1000}, {false, 1000}, {true, 1000}, {false, 5000}};
std::vector<BlinkStep> eth_rmii_init_failed_pattern = {
    {true, 1000}, {false, 1000}, {true, 1000}, {false, 1000}, {true, 1000}, {false, 5000}};
std::vector<BlinkStep> eth_spi_init_failed_pattern = {
    {true, 1000}, {false, 1000}, {true, 1000}, {false, 1000}, {true, 1000}, {false, 1000}, {true, 1000}, {false, 5000}};

LedController::LedController()
{
    mutex = xSemaphoreCreateMutex();
    memset(current_patterns, 0, sizeof(current_patterns));
}

LedController *LedController::get_instance()
{
    if (!instance_)
    {
        instance_ = new LedController();
    }

    return instance_;
}

LedController::~LedController()
{
    running = false;
    if (taskHandle)
        vTaskDelete(taskHandle);
    if (mutex)
        vSemaphoreDelete(mutex);

    delete instance_;
    instance_ = NULL;
}

bool LedController::init()
{

    // Check if I2C bus is initialized
    if (I2CBusManager::getInstance().init() != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C bus not initialized. Cannot initialize LedController.");
        return false;
    }

    // i2c_config_t conf = {};
    // conf.mode = I2C_MODE_MASTER;
    // conf.sda_io_num = I2C_MASTER_SDA_IO;
    // conf.scl_io_num = I2C_MASTER_SCL_IO;
    // conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    // conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    // conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    // esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    // if (err != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
    //     return false;
    // }
    // err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    // if (err != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
    //     return false;
    // }
    // ESP_LOGI(TAG, "I2C initialized on SDA: GPIO%d, SCL: GPIO%d", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    // Set all LEDs off (high/open-drain for PCF8574)
    write(0xFF);
    running = true;
    xTaskCreate(ledTask, "led_task", 4096, this, 10, &taskHandle);
    return true;
}

void LedController::setPattern(uint8_t led_idx, const LedPattern &pattern)
{
    if (led_idx >= LED_COUNT)
    {
        return;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);

    current_patterns[led_idx] = pattern;
    custom_pattern_pos[led_idx] = 0;
    custom_next_tick[led_idx] = xTaskGetTickCount();
    xSemaphoreGive(mutex);
}

void LedController::setAllPatterns(const std::vector<LedPattern> &patterns)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (uint8_t i = 0; i < LED_COUNT && i < patterns.size(); ++i)
    {
        current_patterns[i] = patterns[i];
        custom_pattern_pos[i] = 0;
        custom_next_tick[i] = xTaskGetTickCount();
    }
    xSemaphoreGive(mutex);
}

// ---- I2C Write/Read ----
esp_err_t LedController::write(uint8_t data)
{

    i2c_port_t port = I2CBusManager::getInstance().getPort();
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCF8574_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    // esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK)
        pcf_state = data;
    else
        ESP_LOGE(TAG, "PCF8574 write failed: %s", esp_err_to_name(ret));

    return ret;
}

esp_err_t LedController::read(uint8_t &data)
{
    i2c_port_t port = I2CBusManager::getInstance().getPort();
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCF8574_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    // esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "PCF8574 read failed: %s", esp_err_to_name(ret));
    return ret;
}

// ---- LED Logic ----
void LedController::ledTask(void *arg)
{
    LedController *controller = static_cast<LedController *>(arg);

    while (controller->running)
    {
        // ESP_LOGI(TAG, "LED task running");
        xSemaphoreTake(controller->mutex, portMAX_DELAY);
        for (uint8_t led = 0; led < LED_COUNT; ++led)
        {
            controller->updatePin(led);
        }
        uint8_t out = controller->pcf_state;
        controller->write(out);
        xSemaphoreGive(controller->mutex);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(controller->delay));
        // ESP_LOGI(TAG, "LED task finishing");
    }
    vTaskDelete(nullptr);
}

void LedController::setDelay(TickType_t new_delay)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool delayChange = delay != new_delay;
    if (sd_init_failed || eth_rmii_init_failed || eth_spi_init_failed || wifi_init_failed)
    {
        // ESP_LOGW(TAG, "SD card init failed, resetting delay to default");
        new_delay = DEFAULT_DELAY; // Reset to default if SD init failed
    }
    else if (delayChange)
    {
        delay = new_delay;
    }

    xSemaphoreGive(mutex);

    // Notify the task to wake up with the new delay
    if (delayChange && taskHandle)
    {
        xTaskNotifyGive(taskHandle);
    }
}

void LedController::updatePin(uint8_t led)
{
    const LedPattern &p = current_patterns[led];
    bool state = true; // Default HIGH=OFF

    switch (p.type)
    {
    case LedPatternType::OFF:
        state = true;
        break;
    case LedPatternType::ON:
        state = false;
        break;
    case LedPatternType::BLINK:
    {
        TickType_t now = xTaskGetTickCount();
        bool phase = ((now / pdMS_TO_TICKS(p.blink_interval_ms / 2)) % 2) == 0;
        state = !phase; // ON if phase==0 (low), OFF if 1
        break;
    }
    case LedPatternType::CUSTOM:
    {
        if (p.custom_sequence.empty())
        {
            state = true; // default off
            break;
        }
        size_t &pos = custom_pattern_pos[led];
        TickType_t &next_tick = custom_next_tick[led];
        TickType_t now = xTaskGetTickCount();

        if (now >= next_tick)
        {
            // Move to next step
            pos = (pos + 1) % p.custom_sequence.size();
            next_tick = now + pdMS_TO_TICKS(p.custom_sequence[pos].duration_ms);
        }
        state = !(p.custom_sequence[pos].state); // PCF8574: low=on, high=off
        break;
    }
    default:
        state = true;
    }
    if (state)
        pcf_state |= (1 << led); // Bit high, LED off
    else
        pcf_state &= ~(1 << led); // Bit low, LED on
}

void LedController::test_blink(uint8_t led_idx, uint32_t blink_interval_ms, uint32_t duration_ms)
{
    if (led_idx >= LED_COUNT) {
        ESP_LOGE("LedController", "Invalid LED index: %d", led_idx);
        return;
    }
    
    // Save current delay
    TickType_t original_delay = delay;
    
    // Set faster update rate for smooth blinking (50ms is good for most blink patterns)
    setDelay(pdMS_TO_TICKS(50));
    
    // Create blink pattern
    LedPattern blinkPattern;
    blinkPattern.type = LedPatternType::BLINK;
    blinkPattern.blink_interval_ms = blink_interval_ms;
    
    // Apply pattern
    setPattern(led_idx, blinkPattern);
    
    // Let it blink for specified duration
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    // Turn off LED
    setPattern(led_idx, steady_off);
    
    // Restore original delay
    setDelay(original_delay);
}
