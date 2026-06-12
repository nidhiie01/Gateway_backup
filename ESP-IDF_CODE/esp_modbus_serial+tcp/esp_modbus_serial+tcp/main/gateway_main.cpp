#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "mbcontroller.h" // ESP-Modbus master API
#include "esp_netif.h"
#include "ethernet_init.h"
#include "esp_eth.h"
#include "esp_event.h"

#define TAG "MODBUS_MASTER"

#define MB_PORT_NUM UART_NUM_2
#define MB_BAUDRATE 38400
#define MB_DATABITS UART_DATA_7_BITS
#define MB_PARITY UART_PARITY_EVEN
#define MB_STOPBITS UART_STOP_BITS_1

#define TX_PIN 17
#define RX_PIN 16
#define RTS_PIN 33 // Driver enable pin for RS-485

#define SLAVE_ADDR 1
#define START_ADDR 4572      // Starting holding register
#define TOTAL_REGS 4572      // Number of registers to read
#define MAX_REGS_PER_REQ 125 // Modbus spec limit

#define MB_TCP_TIMEOUT_MS 200
#define MB_TCP_TEST_TIMEOUT_US 100

#define MB_SERIAL_TIMEOUT_MS 200

#define MB_TCP_FUNC_READ_HOLDING 0x03
#define MB_TCP_FUNC_WRITE_HOLDING 0x10
#define MB_TCP_FUNC_READ_COILS 0x01
#define MB_TCP_FUNC_WRITE_COILS 0x0F

#define TEST_HOLDING_START_ADDR 4198
#define TEST_HOLDING_QTY 10
#define TEST_COILS_START_ADDR 2050
#define TEST_COILS_QTY 16

// ============= NETWORK CONFIGURATION =============
// IMPORTANT: Adjust these to match your network!
#define GATEWAY_IP "192.168.1.10"       // ESP32 Gateway IP (this device)
#define GATEWAY_NETMASK "255.255.255.0" // Subnet mask
#define GATEWAY_GW "192.168.1.1"        // Router/Gateway IP

// static mb_communication_info_t comm_info;
static void *modbus_serial_handle = NULL;
/////////////////////////////////////////////////// TCP/IP //////////////////////////////////////////

static bool eth_link_up = false;     // Ethernet link flag
static bool tcp_initialized = false; // TCP master init flag
// Shared Modbus handle for all PLCs
static void *modbus_tcp_handle = NULL;

// Add these global variables:
static esp_netif_t *eth_netif = NULL; // Global netif handle
static esp_eth_handle_t *eth_handles_global = NULL;
static uint8_t eth_port_cnt_global = 0;

// PLC Configuration
typedef struct
{
    const char *ip;   // PLC IP address
    uint16_t port;    // Modbus TCP port (usually 502)
    uint8_t uid;      // Modbus Unit ID (usually 0 or 1)
    const char *name; // Friendly name for logging
} plc_connection_t;

// // PLC connection info
// typedef struct
// {
//     const char *ip;
//     uint16_t port;
//     uint8_t uid;
//     // void *handle;
// } plc_connection_t;

// static const char *ip_addr_table[1 + 1];

// List of PLCs
static plc_connection_t plcs[] = {
    {"192.168.1.105", 502, 0, "PLC_A"},

    // { "192.168.1.102", 502, 1, NULL },
    // { "192.168.1.103", 502, 1, NULL }
};

#define NUM_PLCS (sizeof(plcs) / sizeof(plc_connection_t))

static const char *ip_addr_table[NUM_PLCS + 1];

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "╔════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Ethernet Got IP Address       ║");
    ESP_LOGI(TAG, "╠════════════════════════════════╣");
    ESP_LOGI(TAG, "║ IP:      " IPSTR "    ║", IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "║ Netmask: " IPSTR "  ║", IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "║ Gateway: " IPSTR "      ║", IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "╚════════════════════════════════╝");

    eth_link_up = true;
}

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == ETH_EVENT)
    {
        switch (event_id)
        {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "[ETH] Ethernet Link UP");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            eth_link_up = false;
            tcp_initialized = false;
            ESP_LOGW(TAG, "[ETH] Ethernet Link DOWN");
            break;
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP)
    {
        eth_link_up = true;
    }
}

// ============= ETHERNET INITIALIZATION =============
void ethernet_init()
{
    ESP_LOGI(TAG, "Initializing Ethernet with STATIC IP...");

    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles;

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(ethernet_init_all(&eth_handles, &eth_port_cnt));

    esp_netif_t *eth_netifs[eth_port_cnt];
    esp_eth_netif_glue_handle_t eth_netif_glues[eth_port_cnt];

    if (eth_port_cnt == 1)
    {
        // Single Ethernet interface
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        eth_netifs[0] = esp_netif_new(&cfg);
        eth_netif = eth_netifs[0];
        eth_netif_glues[0] = esp_eth_new_netif_glue(eth_handles[0]);
        ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[0], eth_netif_glues[0]));

        // ===== CONFIGURE STATIC IP =====
        ESP_LOGI(TAG, "Configuring Static IP...");

        // Stop DHCP client
        esp_netif_dhcpc_stop(eth_netifs[0]);

        // Parse and set static IP configuration
        esp_netif_ip_info_t ip_info;
        esp_netif_str_to_ip4(GATEWAY_IP, &ip_info.ip);
        esp_netif_str_to_ip4(GATEWAY_NETMASK, &ip_info.netmask);
        esp_netif_str_to_ip4(GATEWAY_GW, &ip_info.gw);

        ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netifs[0], &ip_info));

        ESP_LOGI(TAG, "╔════════════════════════════════╗");
        ESP_LOGI(TAG, "║  Static IP Configured          ║");
        ESP_LOGI(TAG, "╠════════════════════════════════╣");
        ESP_LOGI(TAG, "║ IP:      %s    ║", GATEWAY_IP);
        ESP_LOGI(TAG, "║ Netmask: %s  ║", GATEWAY_NETMASK);
        ESP_LOGI(TAG, "║ Gateway: %s      ║", GATEWAY_GW);
        ESP_LOGI(TAG, "╚════════════════════════════════╝");
    }
    else
    {
        // Multiple interfaces (not typical for this use case)
        ESP_LOGW(TAG, "Multiple Ethernet interfaces detected!");
        // ... handle multiple interfaces if needed
    }

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               &got_ip_event_handler, NULL));

    // Start Ethernet driver
    for (int i = 0; i < eth_port_cnt; i++)
    {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }

    ESP_LOGI(TAG, "Ethernet initialization complete");
}

static void build_ip_table(void)
{
    for (int i = 0; i < NUM_PLCS; i++)
    {
        ip_addr_table[i] = plcs[i].ip;
    }
    ip_addr_table[NUM_PLCS] = NULL; // NULL terminator is CRITICAL!
}

// Shared IP table
// static const char *ip_addr_table[NUM_PLCS + 1]; // +1 for NULL terminator

// Modbus for TCP/IP
// Init one PLC TCP master handle
static esp_err_t mb_tcp_master_init(void)
{
    ESP_LOGI(TAG, "Initializing Modbus TCP Master...");

    build_ip_table();

    mb_communication_info_t comm;
    memset(&comm, 0, sizeof(comm));
    comm.tcp_opts.mode = MB_TCP;
    comm.tcp_opts.port = 502;
    comm.tcp_opts.uid = 0;
    comm.tcp_opts.response_tout_ms = 500; // 500ms timeout
    comm.tcp_opts.test_tout_us = 1000;
    comm.tcp_opts.addr_type = MB_IPV4;
    comm.tcp_opts.ip_addr_table = (void *)ip_addr_table;

    esp_err_t err = mbc_master_create_tcp(&comm, &modbus_tcp_handle);
    if (err != ESP_OK || modbus_tcp_handle == NULL)
    {
        ESP_LOGE(TAG, "Modbus TCP create failed: 0x%x", err);
        return ESP_FAIL;
    }

    err = mbc_master_start(modbus_tcp_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Modbus TCP start failed: 0x%x", err);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Modbus TCP Master initialized successfully");
    ESP_LOGI(TAG, "Configured for %d PLCs:", NUM_PLCS);
    for (int i = 0; i < NUM_PLCS; i++)
    {
        ESP_LOGI(TAG, "  [%d] %s - %s:502 (UID=%d)",
                 i + 1, plcs[i].name, plcs[i].ip, plcs[i].uid);
    }
    return ESP_OK;
}

// Generic register read/write
static esp_err_t tcp_communication(uint8_t uid,
                                   uint8_t func_code,
                                   uint16_t start_reg,
                                   uint16_t nRegs,
                                   void *dataBuf)
{
    mb_param_request_t req = {
        uid,
        func_code,
        start_reg,
        nRegs};
    return mbc_master_send_request(modbus_tcp_handle, &req, dataBuf);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////// RS485 //////////////////////////////////////////

// Modbus master initialization
static esp_err_t mb_serial_master_init(mb_comm_mode_t mode, uint32_t baudrate, uart_parity_t parity, uart_word_length_t data_bits, uart_stop_bits_t stop_bits)
{
    mb_communication_info_t comm;

    comm.ser_opts.port = MB_PORT_NUM;
    comm.ser_opts.mode = mode;
    comm.ser_opts.baudrate = baudrate;
    comm.ser_opts.parity = parity;
    comm.ser_opts.uid = 0;
    comm.ser_opts.response_tout_ms = 200;
    comm.ser_opts.data_bits = data_bits;
    comm.ser_opts.stop_bits = stop_bits;

    esp_err_t err = mbc_master_create_serial(&comm, &modbus_serial_handle);
    MB_RETURN_ON_FALSE((modbus_serial_handle != NULL), ESP_ERR_INVALID_STATE, TAG,
                       "mb controller initialization fail.");
    MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,
                       "mb controller initialization fail, returns(0x%x).", (int)err);

    // Set UART pin numbers
    err = uart_set_pin(MB_PORT_NUM, TX_PIN, RX_PIN, RTS_PIN, UART_PIN_NO_CHANGE);
    MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,
                       "mb serial set pin failure, uart_set_pin() returned (0x%x).", (int)err);

    err = mbc_master_start(modbus_serial_handle);
    MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,
                       "mb controller start fail, returned (0x%x).", (int)err);

    // Set driver mode to Half Duplex
    err = uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);
    MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,
                       "mb serial set mode failure, uart_set_mode() returned (0x%x).", (int)err);

    vTaskDelay(5);

    return err;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------
// Read Holding Register via Modbus stack
//----------------------------------------------------

esp_err_t serial_communication(uint8_t slave_addr, uint8_t command, uint16_t start_reg, uint16_t nRegs, void *dstBuf)
{
    mb_param_request_t req = {
        slave_addr,
        command,
        start_reg,
        nRegs};

    return mbc_master_send_request(modbus_serial_handle, &req, dstBuf);
}

// Get coil bit value from a Modbus coil data buffer
static inline bool get_coil_bit(const void *data, uint16_t bit_pos)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t byte_index = bit_pos / 8;
    uint8_t bit_index = bit_pos % 8;

    return (bytes[byte_index] >> bit_index) & 0x01;
}

//----------------------------------------------------
// Main Application
//----------------------------------------------------
extern "C" void app_main()
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  ESP32 Modbus TCP Multi-PLC Gateway   ║");
    ESP_LOGI(TAG, "║  Version: 1.0                         ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    /////////////////////////////////////////////////// RS485 RTU //////////////////////////////////////////
    // ESP_ERROR_CHECK(master_init(MB_RTU,115200,UART_PARITY_DISABLE,UART_DATA_8_BITS,UART_STOP_BITS_1));

    /////////////////////////////////////////////////// RS485 ASCII //////////////////////////////////////////
    // ESP_LOGI(TAG, "[SERIAL] Init RS485 ASCII...");
    // ESP_ERROR_CHECK(mb_serial_master_init(MB_ASCII, 38400, UART_PARITY_EVEN, UART_DATA_7_BITS, UART_STOP_BITS_1));

    /////////////////////////////////////////////////// TCP/IP //////////////////////////////////////////
    // Initialize Ethernet with static IP
    ethernet_init();
    // ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
    //                                            &eth_event_handler, NULL));
    // ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    // ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler, NULL));

    // Wait for Ethernet link to stabilize
    ESP_LOGI(TAG, "Waiting for Ethernet link...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    // build_ip_table();

    // Verify IP configuration
    if (eth_netif != NULL)
    {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(eth_netif, &ip_info) == ESP_OK)
        {
            if (ip_info.ip.addr != 0)
            {
                ESP_LOGI(TAG, "✓ IP Address verified: " IPSTR, IP2STR(&ip_info.ip));
                eth_link_up = true; // Set flag manually for static IP
            }
            else
            {
                ESP_LOGE(TAG, "✗ IP Address not configured!");
            }
        }
    }

    if (!eth_link_up)
    {
        ESP_LOGE(TAG, "Ethernet not ready! Check connections and configuration.");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    // // Init all Ethernet PLC handles
    // for (int i = 0; i < NUM_PLCS; i++)
    // {
    //     // ESP_LOGI(TAG, "[TCP] Init TCP");

    //     // ESP_ERROR_CHECK(mb_tcp_master_init(&plcs[i]));
    //     // ESP_LOGI(TAG, "Init End");
    // }

    // ESP_LOGI(TAG, "Init RS485...");
    // ESP_ERROR_CHECK(mb_serial_master_init(MB_ASCII, 38400, MB_PARITY, MB_DATABITS, MB_STOPBITS));

    while (1)
    {

        for (int i = 0; i < NUM_PLCS; i++)
        {
            // plc_connection_t *plc = &plcs[i];

            if (eth_link_up)
            {
                if (!tcp_initialized)
                {
                    // if (modbus_tcp_handle != NULL)
                    // {
                    //     // mbc_master_destroy(modbus_tcp_handle);
                    //     ESP_LOGI(TAG, "[TCP] Stopping old TCP master handle");
                    //     mbc_master_stop(modbus_tcp_handle); // stop the master before re-init
                    //     modbus_tcp_handle = NULL;
                    //     ESP_LOGI(TAG, "[TCP] Previous TCP master handle destroyed");
                    // }
                    if (mb_tcp_master_init() == ESP_OK)
                    {
                        tcp_initialized = true;
                        ESP_LOGI(TAG, "[TCP] TCP Master initialized for PLC %s", plcs[i].ip);
                    }
                    else
                    {
                        ESP_LOGW(TAG, "[TCP] Init failed for PLC %s", plcs[i].ip);
                        continue; // skip comms for this PLC this cycle
                    }
                }

                // TEST Holding REgisters
                uint16_t vals[10];
                if (tcp_communication(plcs[i].uid, 0x03, 4198, 10, vals) == ESP_OK)
                {
                    ESP_LOGI(TAG, "[TCP] PLC %s (UID=%d):", plcs[i].ip, plcs[i].uid);
                    for (int i = 0; i < 10; i++)
                    {
                        ESP_LOGI(TAG, "[TCP] Reg[%d] = %u", 4198 + i, vals[i]);
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "[TCP] Failed to read from PLC %s", plcs[i].ip);
                }

                uint16_t newVal = 1234;
                if (tcp_communication(plcs[i].uid, 0x10, 4198, 1, &newVal) == ESP_OK)
                {
                    ESP_LOGI(TAG, "[TCP] Write single reg OK");
                }
            }
            else
            {
                ESP_LOGW(TAG, "[TCP] Link down — skipping PLC %s", plcs[i].ip);
            }
            // // TEST Holding REgisters
            // uint16_t vals[10];
            // if (serial_communication(1, 0x03, 4570, 10, vals) == ESP_OK)
            // {
            //     for (int i = 0; i < 10; i++)
            //     {
            //         ESP_LOGI(TAG, "[SERIAL] Reg[%d] = %u", 4570 + i, vals[i]);
            //     }
            // }

            // uint16_t newVal = 456;
            // if (serial_communication(1, 0x10, 4575, 1, &newVal) == ESP_OK)
            // {
            //     ESP_LOGI(TAG, "[SERIAL] Write single reg OK");
            // }

            // // TEST Coils
            // uint8_t coilStates[2] = {0}; // 16 bits → 2 bytes
            // if (modbus_access_register_or_coils(1, 0x01, 2050, 16, coilStates) == ESP_OK)
            // {
            //     for (int i = 0; i < 16; i++)
            //     {

            //         ESP_LOGI(TAG, "Coil[%d] = %s", i, get_coil_bit(coilStates, i) ? "ON" : "OFF");
            //     }
            // }

            // uint8_t coilValues[1] = {0xFF}; // 10101010 pattern
            // if (modbus_access_register_or_coils(1, 0x0F, 2055, 8, coilValues) == ESP_OK)
            // {
            //     ESP_LOGI(TAG, "Wrote 8 coils");
            // }

            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}