#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "esp_log.h"
#include "mbcontroller.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#define MASTER_TAG "MODBUS_MASTER"

// Define Modbus connection parameters
#define MB_PORT_NUM UART_NUM_2     // UART port
#define MB_BAUD_RATE 38400         // Baud rate
#define MB_RX_PIN 17               // RX pin
#define MB_TX_PIN 16               // TX pin
#define MB_RTS_PIN 33              // RTS pin for RS485
#define MB_PARITY UART_PARITY_EVEN // Parity

// Function to read multiple holding registers
static void read_multiple_holding_registers(void)
{
    esp_err_t err = ESP_OK;
    uint8_t slave_addr = 1;
    uint16_t start_addr = 4570;
    uint16_t num_registers = 5;
    uint16_t *read_data = (uint16_t *)malloc(num_registers * sizeof(uint16_t));

    if (read_data == NULL)
    {
        ESP_LOGE(MASTER_TAG, "Failed to allocate memory for read data.");
        return;
    }
    ESP_LOGI(MASTER_TAG, "memory allocate to read_data");
    mb_param_request_t request = {
        .slave_addr = slave_addr,
        .command = 3, // Read Holding Registers
        .reg_start = start_addr,
        .reg_size = num_registers,
    };
    ESP_LOGI(MASTER_TAG, "Sending request: Slave=%d, Func=3, RegStart=%d, NumRegs=%d",
             request.slave_addr, request.reg_start, request.reg_size);

    // Send the request and wait for the response
    err = mbc_master_send_request(&request, read_data);

    if (err == ESP_OK)
    {
        ESP_LOGI(MASTER_TAG, "Read successful.");
        for (int i = 0; i < num_registers; i++)
        {
            ESP_LOGI(MASTER_TAG, "Register %d: %d (0x%04X)", start_addr + i, read_data[i], read_data[i]);
        }
    }
    else
    {
        ESP_LOGE(MASTER_TAG, "Failed to read registers: %s", esp_err_to_name(err));
    }

    free(read_data);
}

extern "C" void app_main()
{
    esp_log_level_set("MB_CONTROLLER_MASTER", ESP_LOG_DEBUG);

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_33);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_33, 1);

    // gpio_set_level(MODBUS_DERE_PIN, 0); // Default to receive mode
    ESP_LOGI(MASTER_TAG, "GPIO configured: DE/RE pin = GPIO%d", MB_RTS_PIN);

    uart_config_t uart_config = {
        .baud_rate = 38400,
        .data_bits = UART_DATA_7_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

    // Install UART driver
    // if (uart_driver_install(MB_PORT_NUM, 256 * 2, 0, 0, NULL, 0) != ESP_OK)
    // {
    //     ESP_LOGE(MASTER_TAG, "Failed to install UART driver.");
    //     return;
    // }

    if (uart_param_config(MB_PORT_NUM, &uart_config) != ESP_OK ||
        uart_set_pin(MB_PORT_NUM, MB_TX_PIN, MB_RX_PIN, MB_RTS_PIN, UART_PIN_NO_CHANGE) != ESP_OK)
    {
        ESP_LOGE(MASTER_TAG, "UART configuration failed.");
        uart_driver_delete(MB_PORT_NUM); // Clean up driver if config fails
        return;
    }
    uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);
    // ESP_LOGI(MASTER_TAG, "UART configured: Port=%d, Baud=%d",
    //          MB_PORT_NUM, MB_BAUD_RATE);

    ESP_LOGI(MASTER_TAG, "UART configured: Port=%d, Baud=%d", MB_PORT_NUM, MB_BAUD_RATE);

        ESP_ERROR_CHECK(uart_set_rx_timeout(MB_PORT_NUM, 3));

// uart_driver_delete(MB_PORT_NUM);

    void *master_handler = NULL;
    esp_err_t err = mbc_master_init(MB_PORT_SERIAL_MASTER, &master_handler);
    if (master_handler == NULL || err != ESP_OK)
    {
        ESP_LOGE(MASTER_TAG, "Modbus master initialization failed.");
        return; // Critical error, cannot continue
    }
    ESP_LOGI(MASTER_TAG, "Modbus master initialized.");

    mb_communication_info_t comm_info = {

        .mode = MB_MODE_ASCII, // Changed to RTU, which is more common. Change back if your slave is truly ASCII.
        .slave_addr = 1,
        .port = MB_PORT_NUM,
        .baudrate = MB_BAUD_RATE,
        .parity = MB_PARITY};
    mbc_master_setup((void *)&comm_info);

ESP_LOGI(MASTER_TAG, "Modbus master initialized. 1");

    // mbc_master_start();
    err = mbc_master_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(MASTER_TAG, "Modbus master start failed: %s", esp_err_to_name(err));
        return; // Prevent read loop from running
    }
        uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);

    ESP_LOGI(MASTER_TAG, "Modbus master started. Entering read loop...");

    // highlight-start
    // Loop to periodically read data instead of exiting immediately
    while (1)
    {
        // gpio_set_level(GPIO_NUM_33, 1);
        read_multiple_holding_registers();
        // Wait 5 seconds before the next read
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    // In a real application, you might have logic to break the loop and then call destroy.
    // For this test, the loop runs forever.
    // mbc_master_destroy();
    // highlight-end
}