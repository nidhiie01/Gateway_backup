// master_ascii_example.c

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#define TAG "MB_ASCII"

#define MB_UART_PORT UART_NUM_2
#define MB_TX_PIN GPIO_NUM_17
#define MB_RX_PIN GPIO_NUM_16
#define MB_DE_RE_PIN GPIO_NUM_33

#define MB_BAUDRATE 38400
#define MB_DATA_BITS UART_DATA_7_BITS
#define MB_PARITY UART_PARITY_EVEN
#define MB_STOP_BITS UART_STOP_BITS_1

#define SLAVE_ADDR 1
#define READ_HOLD_ADDR 4572
#define READ_HOLD_COUNT 10
#define READ_COIL_ADDR 2059
#define READ_COIL_COUNT 1

static void ascii_calc_lrc_frame(uint8_t *pdu, size_t pdu_len, char *ascii_buf, size_t *ascii_len)
{
    uint8_t lrc = 0;
    for (size_t i = 0; i < pdu_len; ++i)
        lrc += pdu[i];
    lrc = (~lrc) + 1;

    size_t idx = 0;
    ascii_buf[idx++] = ':';
    for (size_t i = 0; i < pdu_len; ++i)
    {
        sprintf(&ascii_buf[idx], "%02X", pdu[i]);
        idx += 2;
    }
    sprintf(&ascii_buf[idx], "%02X\r\n", lrc);
    idx += 4;
    *ascii_len = idx;
}

static void de_re_ctrl(bool tx)
{
    gpio_set_level(MB_DE_RE_PIN, tx ? 1 : 0);
    ESP_LOGI(TAG, "RS485 DE/RE = %d", tx);
}
static void modbus_send_request(uint8_t func_code, uint16_t addr, uint16_t cnt)
{
    uint8_t pdu[6] = {
        SLAVE_ADDR,
        func_code,
        (uint8_t)((addr - 1) >> 8),
        (uint8_t)((addr - 1) & 0xFF),
        (uint8_t)(cnt >> 8),
        (uint8_t)(cnt & 0xFF)};

    char ascii[64];
    size_t len = 0;
    ascii_calc_lrc_frame(pdu, sizeof(pdu), ascii, &len);

    de_re_ctrl(true);
    uart_write_bytes(MB_UART_PORT, ascii, len);
    uart_wait_tx_done(MB_UART_PORT, pdMS_TO_TICKS(50));
    de_re_ctrl(false);

    ESP_LOGI(TAG, ">> Sent ASCII PDU: %.*s", len, ascii);
}

// static void modbus_ascii_task(void *arg) {
//     ESP_LOGI(TAG, "Sending holding register read:");
//     modbus_send_request(0x03, READ_HOLD_ADDR, READ_HOLD_COUNT);
//     vTaskDelay(pdMS_TO_TICKS(500));

//     ESP_LOGI(TAG, "Sending coil read:");
//     modbus_send_request(0x01, READ_COIL_ADDR, READ_COIL_COUNT);
//     vTaskDelay(pdMS_TO_TICKS(500));

//     ESP_LOGI(TAG, "Done.");
//     vTaskDelete(NULL);
// }
// static void modbus_ascii_task(void *arg) {
//     ESP_LOGI(TAG, "Sending holding register read:");
//     modbus_send_request(0x03, READ_HOLD_ADDR, READ_HOLD_COUNT);
//     vTaskDelay(pdMS_TO_TICKS(500)); // Give PLC time to respond

//     // Read response
//     uint8_t rx_buf[128] = {0};
//     int len = uart_read_bytes(MB_UART_PORT, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(1000));
//     if (len > 0) {
//         rx_buf[len] = '\0'; // Null-terminate
//         ESP_LOGI(TAG, "<< Received ASCII: %s", rx_buf);
//     } else {
//         ESP_LOGE(TAG, "<< No response or timeout");
//     }

//     vTaskDelay(pdMS_TO_TICKS(1000));

//     ESP_LOGI(TAG, "Done.");
//     vTaskDelete(NULL);
// }

// static void modbus_ascii_task(void *arg) {
//     ESP_LOGI(TAG, "Sending holding register read:");
//     modbus_send_request(0x03, READ_HOLD_ADDR, READ_HOLD_COUNT);
//     vTaskDelay(pdMS_TO_TICKS(500)); // Let PLC respond

//     // Read ASCII response from UART
//     uint8_t rx_buf[256] = {0};
//     int len = uart_read_bytes(MB_UART_PORT, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(1000));
//     if (len <= 0) {
//         ESP_LOGE(TAG, "No response received");
//         vTaskDelete(NULL);
//         return;
//     }

//     // Convert ASCII -> binary
//     if (rx_buf[0] != ':') {
//         ESP_LOGE(TAG, "Invalid ASCII frame (missing ':')");
//         vTaskDelete(NULL);
//         return;
//     }

//     uint8_t binary_buf[128] = {0};
//     int bin_len = 0;

//     for (int i = 1; i < len - 2; i += 2) { // skip ':' and \r\n
//         char hi = rx_buf[i];
//         char lo = rx_buf[i + 1];
//         uint8_t byte = ((hi >= 'A') ? (hi - 'A' + 10) : (hi - '0')) << 4 |
//                        ((lo >= 'A') ? (lo - 'A' + 10) : (lo - '0'));
//         binary_buf[bin_len++] = byte;
//     }

//     // binary_buf[0] = address, [1] = function, [2] = byte count, [3+] = data
//     if (binary_buf[1] != 0x03) {
//         ESP_LOGE(TAG, "Unexpected function code: 0x%02X", binary_buf[1]);
//         vTaskDelete(NULL);
//         return;
//     }

//     int byte_count = binary_buf[2];
//     if (byte_count != READ_HOLD_COUNT * 2) {
//         ESP_LOGE(TAG, "Unexpected data length: %d", byte_count);
//         vTaskDelete(NULL);
//         return;
//     }

//     ESP_LOGI(TAG, "Parsed Holding Register Values:");
//     for (int i = 0; i < READ_HOLD_COUNT; i++) {
//         uint16_t reg = (binary_buf[3 + i * 2] << 8) | binary_buf[4 + i * 2];
//         ESP_LOGI(TAG, "HR[%d] = %d", READ_HOLD_ADDR + i, reg);
//     }

//     ESP_LOGI(TAG, "Done.");
//     vTaskDelete(NULL);
// }

static void modbus_send_ascii(uint8_t *pdu, size_t pdu_len)
{
    char ascii[128];
    size_t len = 0;
    ascii_calc_lrc_frame(pdu, pdu_len, ascii, &len);
    de_re_ctrl(true);
    uart_write_bytes(MB_UART_PORT, ascii, len);
    uart_wait_tx_done(MB_UART_PORT, pdMS_TO_TICKS(50));
    de_re_ctrl(false);
}

static void modbus_write_registers()
{
    ESP_LOGI(TAG, "Writing holding registers 4571–4575...");

    uint16_t start_addr = 4571;
    uint8_t reg_count = 5;
    uint8_t byte_count = reg_count * 2;

    uint8_t pdu[6 + 10]; // 6 = header, 10 = data for 5 registers
    pdu[0] = SLAVE_ADDR;
    pdu[1] = 0x10; // Function: Write Multiple Registers
    pdu[2] = (start_addr - 1) >> 8;
    pdu[3] = (start_addr - 1) & 0xFF;
    pdu[4] = 0x00;
    pdu[5] = reg_count;
    pdu[6] = byte_count;

    // Set example values to write: 1000, 1001, 1002, 1003, 1004
    for (int i = 0; i < reg_count; i++)
    {
        pdu[7 + i * 2] = (1000 + i) >> 8;
        pdu[8 + i * 2] = (1000 + i) & 0xFF;
    }

    modbus_send_ascii(pdu, 7 + byte_count);

    // (Optional) Read the ACK response — you can skip or add logging if needed
    uint8_t rx_buf[128] = {0};
    int len = uart_read_bytes(MB_UART_PORT, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(1000));
    if (len > 0 && rx_buf[0] == ':')
    {
        ESP_LOGI(TAG, "Write ACK received");
    }
    else
    {
        ESP_LOGW(TAG, "Write response timeout or malformed");
    }
}

static void modbus_read_registers()
{
    ESP_LOGI(TAG, "Reading holding registers 4571–4580...");
    uint16_t addr = 4571;
    uint16_t cnt = 10;

    uint8_t pdu[6] = {
        SLAVE_ADDR,
        0x03,
        (uint8_t)((addr - 1) >> 8),
        (uint8_t)((addr - 1) & 0xFF),
        (uint8_t)(cnt >> 8),
        (uint8_t)(cnt & 0xFF)};

    modbus_send_ascii(pdu, sizeof(pdu));

    uint8_t rx_buf[256] = {0};
    int len = uart_read_bytes(MB_UART_PORT, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(1000));
    if (len <= 0 || rx_buf[0] != ':')
    {
        ESP_LOGE(TAG, "Read response invalid or timeout");
        return;
    }

    uint8_t binary[128] = {0};
    int bin_len = 0;
    for (int i = 1; i < len - 2; i += 2)
    {
        char hi = rx_buf[i];
        char lo = rx_buf[i + 1];
        binary[bin_len++] = ((hi >= 'A' ? hi - 'A' + 10 : hi - '0') << 4) |
                            (lo >= 'A' ? lo - 'A' + 10 : lo - '0');
    }

    if (binary[1] != 0x03 || binary[2] != cnt * 2)
    {
        ESP_LOGE(TAG, "Unexpected function or byte count");
        return;
    }

    ESP_LOGI(TAG, "Read holding register values:");
    for (int i = 0; i < cnt; ++i)
    {
        uint16_t val = (binary[3 + i * 2] << 8) | binary[4 + i * 2];
        ESP_LOGI(TAG, "HR[%d] = %d", addr + i, val);
    }
}

extern "C" void app_main(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << MB_DE_RE_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE};
    gpio_config(&io);
    gpio_set_level(MB_DE_RE_PIN, 0);

    uart_config_t ucfg = {
        .baud_rate = MB_BAUDRATE,
        .data_bits = MB_DATA_BITS,
        .parity = MB_PARITY,
        .stop_bits = MB_STOP_BITS,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    uart_param_config(MB_UART_PORT, &ucfg);
    uart_set_pin(MB_UART_PORT, MB_TX_PIN, MB_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ESP_ERROR_CHECK(uart_driver_install(MB_UART_PORT, 512, 512, 0, NULL, 0));

    ESP_LOGI(TAG, "UART configured for 7E1 ASCII @ %d", MB_BAUDRATE);
    modbus_write_registers();
    vTaskDelay(pdMS_TO_TICKS(1000));

    modbus_read_registers();

    ESP_LOGI(TAG, "Modbus write-read cycle complete");
    // xTaskCreate(modbus_ascii_task, "modbus_ascii", 4096, NULL, 5, NULL);
}
