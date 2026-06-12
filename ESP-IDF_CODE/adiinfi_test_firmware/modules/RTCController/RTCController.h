#pragma once

#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "i2c_init.h"
#include <ctime>
#include <string>

#if ENABLE_W5500_ETH
// DS3231M I2C Address
#define DS3231M_ADDR 0x68

// DS3231M Register Addresses
#define DS3231M_REG_SECONDS    0x00
#define DS3231M_REG_MINUTES    0x01
#define DS3231M_REG_HOURS      0x02
#define DS3231M_REG_DAY        0x03
#define DS3231M_REG_DATE       0x04
#define DS3231M_REG_MONTH      0x05
#define DS3231M_REG_YEAR       0x06
#define DS3231M_REG_CONTROL    0x0E
#define DS3231M_REG_STATUS     0x0F

struct RTCDateTime {
    uint8_t second;   // 0-59
    uint8_t minute;   // 0-59
    uint8_t hour;     // 0-23 (24-hour format)
    uint8_t day;      // 1-7 (day of week: 1=Sunday, 2=Monday, etc.)
    uint8_t date;     // 1-31
    uint8_t month;    // 1-12
    uint16_t year;    // 2000-2099
};

class RTCController {
public:
    // Delete copy & move
    RTCController(const RTCController &) = delete;
    RTCController &operator=(const RTCController &) = delete;

    static RTCController *get_instance();

    // Initialize RTC
    bool init();
    
    // Time setting and getting (READ/WRITE operations)
    bool setDateTime(const RTCDateTime &dt);
    bool getDateTime(RTCDateTime &dt);
    bool syncFromNTP(const char* ntp_server = "pool.ntp.org", const char* timezone = "GMT-5:30");
    bool syncFromSystemTime();
    // Get formatted time strings (for easy reading)
    std::string getTimeString();        // Returns: "HH:MM:SS"
    std::string getDateString();        // Returns: "YYYY-MM-DD"
    std::string getDateTimeString();    // Returns: "YYYY-MM-DD HH:MM:SS"
    
    // Print current date/time to console
    void printDateTime();
    
    // Check if oscillator is running
    bool isRunning();

     // Sync RTC time from internet (checks WiFi/network availability internally)
    void syncTimeFromInternet(const char* ntp_server = "pool.ntp.org", const char* timezone = "IST-5:30", uint32_t wait_time_ms = 15000);

private:
    static RTCController *instance_;
    static const char *TAG;

    RTCController();
    ~RTCController();

    // Low level I2C operations
    esp_err_t writeRegister(uint8_t reg, uint8_t data);
    esp_err_t readRegister(uint8_t reg, uint8_t &data);
    esp_err_t readRegisters(uint8_t reg, uint8_t *data, size_t len);

    // BCD conversion helpers
    uint8_t bcdToDec(uint8_t bcd);
    uint8_t decToBcd(uint8_t dec);
    
    // Validate I2C connection
    bool checkConnection();
};

#endif