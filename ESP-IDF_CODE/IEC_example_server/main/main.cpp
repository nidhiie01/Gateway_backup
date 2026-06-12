/**
 * @file    main.cpp
 * @brief   Application Entry Point — IEC 60870-5-104 Client Test (Phase 1)
 *
 * This is the top-level application file for the ESP32 gateway firmware.
 * It demonstrates the very first step of IEC 60870-5-104 communication:
 *
 *   1. Connect ESP32 to WiFi (DHCP — IP assigned automatically)
 *   2. Open a TCP connection to the IEC 104 server simulator on the PC
 *   3. Send STARTDT_act and receive STARTDT_con
 *   4. Print "IEC104: Connected" on success
 *
 * This file is intentionally minimal — it only orchestrates the sequence.
 * All protocol logic lives in components/iec104/iec104_client.cpp.
 *
 * Platform : ESP32 with ESP-IDF v5.4.1 + FreeRTOS
 * Network  : WiFi (STA mode, same LAN as the simulator PC)
 */

/* =========================================================================
 * INCLUDES
 * ========================================================================= */

/* FreeRTOS — required for app_main task context */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" /* vTaskDelay(), pdMS_TO_TICKS() */

/* ESP-IDF logging — for status messages in app_main */
#include "esp_log.h"

/* Our IEC 104 client library */
#include "iec104_client.h"

/* =========================================================================
 * PRIVATE CONSTANTS
 * ========================================================================= */

/** @brief Log tag for the main application module */
static const char *TAG = "APP_MAIN";

/* =========================================================================
 * ENTRY POINT
 * ========================================================================= */

/**
 * @brief  Application entry point — called by ESP-IDF after system boot.
 *
 *         In ESP-IDF, app_main() runs inside a FreeRTOS task created
 *         by the framework. It has a default stack size defined in sdkconfig.
 *
 *         This function executes the IEC 104 connection sequence in order:
 *
 *           Phase 1: WiFi connection
 *           Phase 2: TCP connection to simulator
 *           Phase 3: STARTDT handshake
 *           Phase 4: Done — loop forever (or handle error)
 *
 *         Each phase returns an iec104_err_t status code.
 *         On any failure, an error is logged and the firmware halts.
 */
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, " ESP32 IEC 60870-5-104 Client — Phase 1   ");
    ESP_LOGI(TAG, " Goal: STARTDT handshake with simulator    ");
    ESP_LOGI(TAG, "============================================");

    iec104_err_t result; /* Holds the return code from each step */

    /* ------------------------------------------------------------------
     * Phase 1: Connect to WiFi
     *
     * iec104_wifi_init() will:
     *   - Initialise NVS, netif, event loop, WiFi driver
     *   - Connect to WIFI_SSID using WIFI_PASSWORD (defined in header)
     *   - Block until IP is obtained via DHCP or retry limit is reached
     * ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "[Phase 1] Connecting to WiFi...");

    result = iec104_wifi_init();
    if (result != IEC104_OK)
    {
        /* WiFi failed — cannot proceed without network */
        ESP_LOGE(TAG, "[Phase 1] FAILED — WiFi error code: %d", (int)result);
        ESP_LOGE(TAG, "Check WIFI_SSID and WIFI_PASSWORD macros in iec104_client.h");
        goto error_halt; /* Jump to cleanup/halt section */
    }
    ESP_LOGI(TAG, "[Phase 1] WiFi connected successfully");

    /* Small delay to let the IP stack fully settle after DHCP */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* ------------------------------------------------------------------
     * Phase 2: Open TCP connection to IEC 104 simulator
     *
     * iec104_tcp_connect() will:
     *   - Create a TCP socket
     *   - Set receive timeout (SO_RCVTIMEO)
     *   - Call connect() to IEC104_SERVER_IP : IEC104_SERVER_PORT
     * ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "[Phase 2] Opening TCP connection to simulator...");

    result = iec104_tcp_connect();
    if (result != IEC104_OK)
    {
        ESP_LOGE(TAG, "[Phase 2] FAILED — TCP error code: %d", (int)result);
        ESP_LOGE(TAG, "Check IEC104_SERVER_IP in iec104_client.h");
        ESP_LOGE(TAG, "Make sure the simulator is running and listening on port %d",
                 IEC104_SERVER_PORT);
        goto error_halt;
    }
    ESP_LOGI(TAG, "[Phase 2] TCP connection established");

    /* ------------------------------------------------------------------
     * Phase 3: Send STARTDT_act and wait for STARTDT_con
     *
     * iec104_startdt() will:
     *   - Build a 6-byte STARTDT_act U-frame (per IEC 60870-5-104 Table 4)
     *   - Send it to the server
     *   - Wait up to IEC104_STARTDT_TIMEOUT_MS for STARTDT_con reply
     *   - Validate the response
     *   - Print "IEC104: Connected" on success
     * ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "[Phase 3] Sending STARTDT_act...");

    result = iec104_startdt();
    if (result != IEC104_OK)
    {
        ESP_LOGE(TAG, "[Phase 3] FAILED — STARTDT error code: %d", (int)result);
        ESP_LOGE(TAG, "Possible reasons:");
        ESP_LOGE(TAG, "  - Simulator did not respond in time");
        ESP_LOGE(TAG, "  - Simulator sent an unexpected response");
        ESP_LOGE(TAG, "  - TCP connection dropped");
        goto error_halt;
    }
    ESP_LOGI(TAG, "[Phase 3] STARTDT handshake complete");

    /* ------------------------------------------------------------------
     * Phase 4: Test complete — close TCP and idle
     *
     * For this Phase 1 test, we simply close the connection after
     * confirming the STARTDT handshake worked. In future phases, you
     * would keep the connection open and exchange I-frames (data).
     * ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, " TEST COMPLETE — All phases passed!        ");
    ESP_LOGI(TAG, " IEC104 client handshake verified OK.      ");
    ESP_LOGI(TAG, "============================================");

    /* Disconnect gracefully */
    iec104_tcp_disconnect();

    /* Idle loop — keep the task alive (ESP-IDF requires app_main to not return) */
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(5000)); /* Sleep 5 seconds, repeat */
        ESP_LOGI(TAG, "Idle. Test was successful. Reset ESP32 to run again.");
    }

    /* ------------------------------------------------------------------
     * Error halt — reached on any failure above via goto
     * ------------------------------------------------------------------ */
error_halt:
    iec104_tcp_disconnect(); /* Clean up socket if open */

    ESP_LOGE(TAG, "============================================");
    ESP_LOGE(TAG, " FIRMWARE HALTED DUE TO ERROR              ");
    ESP_LOGE(TAG, " Fix the issue above and reset the device. ");
    ESP_LOGE(TAG, "============================================");

    /* Idle loop — don't return from app_main */
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}