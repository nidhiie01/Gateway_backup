
#pragma once

#include "driver/i2c.h"
extern "C"
{
#include "esp_log.h" //  Ensure correct linkage in C++
}
#include "../MacroConfig/MacroConfig.h"

    // #define I2C_MASTER_NUM I2C_NUM_0
    // #define I2C_MASTER_SCL_IO 32
    // #define I2C_MASTER_SDA_IO 15
    // #define I2C_MASTER_FREQ_HZ 100000

    // I2CBusManager is a singleton class to manage a single I2C bus.
    // It ensures the I2C bus is initialized only once.
    class I2CBusManager
{
public:
    // Get the single instance of the class.
    static I2CBusManager &getInstance()
    {
        static I2CBusManager instance;
        return instance;
    }

    // Initialize the I2C bus with configurable parameters.
    // The port, SDA/SCL pins, and frequency can be customized.
    esp_err_t init(i2c_port_t port = I2C_MASTER_NUM, int sda = I2C_MASTER_SDA_IO, int scl = I2C_MASTER_SCL_IO, uint32_t freq = I2C_MASTER_FREQ_HZ)
    {
        if (initialized)
        {
            ESP_LOGW("I2CBusManager", "I2C bus already initialized");
            return ESP_OK;
        }

        i2c_config_t conf = {};
        conf.mode = I2C_MODE_MASTER;
        conf.sda_io_num = static_cast<gpio_num_t>(sda);
        conf.scl_io_num = static_cast<gpio_num_t>(scl);
        conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
        conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
        conf.master.clk_speed = freq;

        esp_err_t ret = i2c_param_config(port, &conf);
        if (ret != ESP_OK)
        {
            ESP_LOGE("I2CBusManager", "I2C param config failed with error: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = i2c_driver_install(port, conf.mode, 0, 0, 0);
        if (ret == ESP_OK)
        {
            initialized = true;
            this->port = port;
            ESP_LOGI("I2CBusManager", "I2C bus on port %d initialized successfully (SDA: %d, SCL: %d)", port, sda, scl);
        }
        else
        {
            ESP_LOGE("I2CBusManager", "I2C driver install failed with error: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    // Returns the port number of the initialized I2C bus.
    i2c_port_t getPort() const { return port; }

private:
    I2CBusManager() : initialized(false) {}
    ~I2CBusManager() {}
    I2CBusManager(const I2CBusManager &) = delete;
    I2CBusManager &operator=(const I2CBusManager &) = delete;

    bool initialized;
    i2c_port_t port;
};
