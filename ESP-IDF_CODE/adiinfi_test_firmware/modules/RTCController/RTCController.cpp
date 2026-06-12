#include "RTCController.h"
#if ENABLE_W5500_ETH
const char *RTCController::TAG = "RTCController";
RTCController *RTCController::instance_ = nullptr;

RTCController::RTCController() {
}

RTCController *RTCController::get_instance() {
    if (!instance_) {
        instance_ = new RTCController();
    }
    return instance_;
}

RTCController::~RTCController() {
    delete instance_;
    instance_ = nullptr;
}

bool RTCController::init() {
    ESP_LOGI(TAG, "Initializing DS3231M RTC at address 0x%02X", DS3231M_ADDR);

    // Check if I2C bus is initialized
    if (I2CBusManager::getInstance().init() != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus not initialized. Cannot initialize RTCController.");
        return false;
    }

    // Check connection
    if (!checkConnection()) {
        ESP_LOGE(TAG, "DS3231M not found at address 0x%02X", DS3231M_ADDR);
        return false;
    }

    // Enable oscillator and disable square wave output
    //Disable square wave output , to display the time we do not need it output  
    uint8_t control = 0x00;
    if (writeRegister(DS3231M_REG_CONTROL, control) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure DS3231M control register");
        return false;
    }

    // Clear oscillator stop flag if set
    //It check that if oscillator flag bit is enable or disable if it is enable then it disable it.
    uint8_t status;
    if (readRegister(DS3231M_REG_STATUS, status) == ESP_OK) {
        if (status & 0x80) {
            ESP_LOGW(TAG, "Oscillator stop flag was set - clearing it");
            status &= ~0x80;
            writeRegister(DS3231M_REG_STATUS, status);
        }
    }

    ESP_LOGI(TAG, "DS3231M RTC initialized successfully");
    
    // Check if RTC has valid time
    if (isRunning()) {
        ESP_LOGI(TAG, "RTC is running with current time:");
        printDateTime();
    } else {
        ESP_LOGW(TAG, "RTC oscillator stopped - time needs to be set");
    }
    
    return true;
}

bool RTCController::checkConnection() {
    i2c_port_t port = I2CBusManager::getInstance().getPort();
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();       //It create the link of commands  like start ,write ,read,stop .store it like queue execute all with begin cmd at one call
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231M_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK);
}

esp_err_t RTCController::writeRegister(uint8_t reg, uint8_t data) {
    i2c_port_t port = I2CBusManager::getInstance().getPort();
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231M_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write register 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t RTCController::readRegister(uint8_t reg, uint8_t &data) {
    i2c_port_t port = I2CBusManager::getInstance().getPort();
    
    // Write register address
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231M_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write register address 0x%02X failed: %s", reg, esp_err_to_name(ret));
        return ret;
    }
    
    // Read data
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231M_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read register 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t RTCController::readRegisters(uint8_t reg, uint8_t *data, size_t len) {
    i2c_port_t port = I2CBusManager::getInstance().getPort();
    
    // Write register address
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231M_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Read data
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231M_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

uint8_t RTCController::bcdToDec(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

uint8_t RTCController::decToBcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

// WRITE OPERATION - Set date and time to RTC
bool RTCController::setDateTime(const RTCDateTime &dt) {
    // Validate input
    if (dt.second > 59 || dt.minute > 59 || dt.hour > 23 ||
        dt.day < 1 || dt.day > 7 || dt.date < 1 || dt.date > 31 ||
        dt.month < 1 || dt.month > 12 || dt.year < 2000 || dt.year > 2099) {
        ESP_LOGE(TAG, "Invalid date/time values");
        return false;
    }

    uint8_t data[7];
    data[0] = decToBcd(dt.second);
    data[1] = decToBcd(dt.minute);
    data[2] = decToBcd(dt.hour);
    data[3] = decToBcd(dt.day);
    data[4] = decToBcd(dt.date);
    data[5] = decToBcd(dt.month);
    data[6] = decToBcd(dt.year - 2000);

    // Write all time registers at once
    i2c_port_t port = I2CBusManager::getInstance().getPort();
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231M_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, DS3231M_REG_SECONDS, true);
    i2c_master_write(cmd, data, 7, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ Time written to RTC: %04d-%02d-%02d %02d:%02d:%02d",
                 dt.year, dt.month, dt.date, dt.hour, dt.minute, dt.second);
        return true;
    } else {
        ESP_LOGE(TAG, "✗ Failed to write time to RTC: %s", esp_err_to_name(ret));
        return false;
    }
}

// READ OPERATION - Get date and time from RTC
bool RTCController::getDateTime(RTCDateTime &dt) {
    uint8_t data[7];
    
    if (readRegisters(DS3231M_REG_SECONDS, data, 7) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time registers from RTC");
        return false;
    }

    dt.second = bcdToDec(data[0] & 0x7F);
    dt.minute = bcdToDec(data[1] & 0x7F);
    dt.hour = bcdToDec(data[2] & 0x3F);
    dt.day = bcdToDec(data[3] & 0x07);
    dt.date = bcdToDec(data[4] & 0x3F);
    dt.month = bcdToDec(data[5] & 0x1F);
    dt.year = bcdToDec(data[6]) + 2000;

    return true;
}

bool RTCController::syncFromNTP(const char* ntp_server, const char* timezone) {
    ESP_LOGI(TAG, "Initializing SNTP for time sync...");
    
    // Configure timezone (India Standard Time = GMT+5:30)
    setenv("TZ", timezone, 1);
    tzset();
    
    // Initialize SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_init();

    ESP_LOGI(TAG, "Waiting for system time to be set from NTP...");
    
    // Wait for time to be set (max 10 seconds)
    int retry = 0;
    const int retry_count = 20;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for time sync... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (retry >= retry_count) {
        ESP_LOGE(TAG, "Failed to sync time from NTP server");
        return false;
    }

    // Get synchronized time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    ESP_LOGI(TAG, "System time synced: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // Now sync RTC from system time
    return syncFromSystemTime();
}

bool RTCController::syncFromSystemTime() {
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    // Basic check: avoid default 1970 time
    int year = timeinfo.tm_year + 1900;
    if (year < 2020 || year > 2099) {
        ESP_LOGW(TAG, "System time not valid (year=%d). Sync from NTP first!", year);
        return false;
    }

    RTCDateTime dt;
    dt.second = timeinfo.tm_sec;
    dt.minute = timeinfo.tm_min;
    dt.hour   = timeinfo.tm_hour;
    dt.date   = timeinfo.tm_mday;        // 1–31
    dt.month  = timeinfo.tm_mon + 1;     // tm_mon: 0–11 → 1–12
    dt.year   = year;
    dt.day    = timeinfo.tm_wday + 1;    // 0=Sun → 1=Sun, 1=Mon → 2=Mon, etc.

    const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    ESP_LOGI(TAG, "Syncing RTC from system time: %s %04d-%02d-%02d %02d:%02d:%02d",
             days[timeinfo.tm_wday], dt.year, dt.month, dt.date,
             dt.hour, dt.minute, dt.second);

    return setDateTime(dt);
}



std::string RTCController::getTimeString() {
    RTCDateTime dt;
    if (!getDateTime(dt)) {
        return "ERROR";
    }

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
    return std::string(buffer);
}

std::string RTCController::getDateString() {
    RTCDateTime dt;
    if (!getDateTime(dt)) {
        return "ERROR";
    }

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", dt.year, dt.month, dt.date);
    return std::string(buffer);
}

std::string RTCController::getDateTimeString() {
    RTCDateTime dt;
    if (!getDateTime(dt)) {
        return "ERROR";
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             dt.year, dt.month, dt.date, dt.hour, dt.minute, dt.second);
    return std::string(buffer);
}

bool RTCController::isRunning() {
    uint8_t status;
    if (readRegister(DS3231M_REG_STATUS, status) != ESP_OK) {
        return false;
    }
    
    return !(status & 0x80);
}

void RTCController::printDateTime() {
    RTCDateTime dt;
    if (getDateTime(dt)) {
        const char *days[] = {"", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        ESP_LOGI(TAG, "%s %04d-%02d-%02d %02d:%02d:%02d",
                 days[dt.day], dt.year, dt.month, dt.date,
                 dt.hour, dt.minute, dt.second);
    } else {
        ESP_LOGE(TAG, "Failed to read date/time from RTC");
    }
}


void RTCController::syncTimeFromInternet(const char* ntp_server, const char* timezone, uint32_t wait_time_ms)
{
    ESP_LOGI(TAG, "Attempting to sync RTC time from internet...");
    
    // Wait for network connection
    ESP_LOGI(TAG, "Waiting %lu ms for network connection...", (unsigned long)wait_time_ms);
    vTaskDelay(pdMS_TO_TICKS(wait_time_ms));
    
    // Try to sync from NTP
    if (syncFromNTP(ntp_server, timezone)) {
        RTCDateTime dt;
        if (getDateTime(dt)) {
            ESP_LOGI(TAG, "✓ RTC synced with current time from NTP: %04d-%02d-%02d %02d:%02d:%02d", 
                     dt.year, dt.month, dt.date, dt.hour, dt.minute, dt.second);
        }
    } else {
        ESP_LOGW(TAG, "Could not sync from NTP - RTC will use existing time");
        RTCDateTime dt;
        if (getDateTime(dt)) {
            ESP_LOGI(TAG, "Current RTC time: %04d-%02d-%02d %02d:%02d:%02d", 
                     dt.year, dt.month, dt.date, dt.hour, dt.minute, dt.second);
        }
    }
}
#endif //ENABLE W5500