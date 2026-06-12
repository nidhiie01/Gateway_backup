/**
 * @file    iec104_client.cpp
 * @brief   IEC 60870-5-104 Client (Controlled Station) — Implementation
 *
 * This file implements the IEC 60870-5-104 protocol logic from scratch.
 * All protocol constants, frame structures, and handshake sequences are
 * derived directly from the IEC 60870-5-104 standard specification.
 *
 * What this file covers (Phase 1 — initial connection test):
 *   1. WiFi initialisation using ESP-IDF station mode
 *   2. TCP socket creation and connection to the server (simulator)
 *   3. Building and sending a STARTDT_act U-frame
 *   4. Receiving and validating a STARTDT_con U-frame response
 *
 * IEC 60870-5-104 Connection Sequence (Section 9.1 of the standard):
 *
 *   CLIENT (Controlled Station)          SERVER (Controlling Station / RTU)
 *         |                                        |
 *         |-------- TCP connect() ---------------->|   (transport layer)
 *         |                                        |
 *         |-------- STARTDT_act (U-frame) -------->|   "I want to start data transfer"
 *         |                                        |
 *         |<------- STARTDT_con (U-frame) ---------|   "Confirmed, link is active"
 *         |                                        |
 *         |     [ Data exchange phase begins ]     |
 *
 * Author  : Custom implementation for ESP32 / ESP-IDF v5.4.1
 * Target  : ESP32 with FreeRTOS (ESP-IDF)
 */

/* =========================================================================
 * INCLUDES
 * ========================================================================= */

/* Our own IEC 104 header — contains frame structures and API declarations */
#include "iec104_client.h"

/* Standard C library */
#include <string.h> /* memset(), memcpy()                              */
#include <errno.h>  /* errno, strerror()                               */

/* ESP-IDF system */
#include "esp_log.h" /* ESP_LOGI, ESP_LOGE, ESP_LOGW                   */
#include "esp_err.h" /* esp_err_t, ESP_OK, ESP_ERROR_CHECK             */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"         /* vTaskDelay(), pdMS_TO_TICKS()         */
#include "freertos/event_groups.h" /* EventGroupHandle_t, xEventGroupWait.. */

/* ESP-IDF WiFi */
#include "esp_wifi.h"  /* esp_wifi_init(), esp_wifi_start(), etc.         */
#include "esp_event.h" /* esp_event_loop_create_default(), handlers       */
#include "esp_netif.h" /* esp_netif_init(), esp_netif_create_default_wifi */
#include "nvs_flash.h" /* nvs_flash_init() — required before WiFi init   */

/* POSIX socket API (provided by ESP-IDF lwIP) */
#include "lwip/sockets.h" /* socket(), connect(), send(), recv(), close()   */
#include "lwip/netdb.h"   /* struct sockaddr_in, inet_pton()                */

/* =========================================================================
 * PRIVATE CONSTANTS & TAG
 * ========================================================================= */

/**
 * @brief Log tag for this module — appears in all ESP_LOG messages.
 *        Example output:  I (1234) IEC104: Connected
 */
static const char *TAG = "IEC104";

/**
 * @brief FreeRTOS event bit that signals "WiFi connected + IP obtained".
 *        We use bit 0 (value 0x01) of the event group.
 */
#define WIFI_CONNECTED_BIT BIT0

/**
 * @brief FreeRTOS event bit that signals "WiFi connection failed".
 *        We use bit 1 (value 0x02) of the event group.
 */
#define WIFI_FAIL_BIT BIT1

/**
 * @brief Maximum number of WiFi reconnection attempts before giving up.
 */
#define WIFI_MAX_RETRY 5

/* =========================================================================
 * PRIVATE (MODULE-LEVEL) STATE VARIABLES
 * These are static so they are not visible outside this .cpp file.
 * ========================================================================= */

/**
 * @brief FreeRTOS event group handle used to synchronise WiFi events.
 *        Set by the WiFi event handler, waited on by iec104_wifi_init().
 */
static EventGroupHandle_t s_wifi_event_group = NULL;

/**
 * @brief Counter tracking how many times we have retried WiFi connection.
 */
static int s_wifi_retry_count = 0;

/**
 * @brief TCP socket file descriptor.
 *        -1 means no socket is open. Set by iec104_tcp_connect().
 */
static int s_tcp_socket = -1;

/* =========================================================================
 * PRIVATE HELPER: WiFi Event Handler
 * ========================================================================= */

/**
 * @brief  WiFi + IP event handler (called by ESP-IDF event loop).
 *
 *         This function is registered with esp_event_handler_instance_register()
 *         and is automatically called by the ESP-IDF event loop when:
 *           - WIFI_EVENT_STA_START        : WiFi station started → call connect
 *           - WIFI_EVENT_STA_DISCONNECTED : Connection lost → retry or fail
 *           - IP_EVENT_STA_GOT_IP         : IP address assigned → signal success
 *
 * @param  arg        : User-defined argument (not used here)
 * @param  event_base : Event category (WIFI_EVENT or IP_EVENT)
 * @param  event_id   : Specific event within the category
 * @param  event_data : Event-specific data (e.g. IP info struct)
 */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    /* ------------------------------------------------------------------ */
    /* Case 1: WiFi station has started — attempt the first connection     */
    /* ------------------------------------------------------------------ */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WiFi STA started — attempting connection to SSID: %s", WIFI_SSID);
        esp_wifi_connect(); /* Kick off the connection attempt */
    }

    /* ------------------------------------------------------------------ */
    /* Case 2: WiFi got disconnected (wrong password, out of range, etc.)  */
    /* ------------------------------------------------------------------ */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_wifi_retry_count < WIFI_MAX_RETRY)
        {
            /* Still within retry limit — try again */
            s_wifi_retry_count++;
            ESP_LOGW(TAG, "WiFi disconnected — retry %d / %d",
                     s_wifi_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        }
        else
        {
            /* Exceeded retry limit — signal failure to the waiting task */
            ESP_LOGE(TAG, "WiFi connection failed after %d retries", WIFI_MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }

    /* ------------------------------------------------------------------ */
    /* Case 3: IP address successfully obtained from DHCP                  */
    /* ------------------------------------------------------------------ */
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        /* Cast event_data to ip_event_got_ip_t to read the assigned IP */
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        /* Print the assigned IP in human-readable form */
        ESP_LOGI(TAG, "WiFi connected! IP address: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        /* Reset retry counter for future use */
        s_wifi_retry_count = 0;

        /* Signal the waiting task that WiFi is ready */
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* =========================================================================
 * PUBLIC API IMPLEMENTATION
 * ========================================================================= */

/**
 * @brief  Initialise WiFi in Station (STA) mode and wait for connection.
 *
 *         Detailed steps:
 *          Step 1 — Initialise NVS flash (required by WiFi driver)
 *          Step 2 — Initialise the TCP/IP network interface layer
 *          Step 3 — Create the default event loop (handles WiFi/IP events)
 *          Step 4 — Create a default WiFi STA network interface
 *          Step 5 — Initialise WiFi driver with default config
 *          Step 6 — Register our event handler for WIFI_EVENT + IP_EVENT
 *          Step 7 — Configure SSID and password
 *          Step 8 — Set WiFi mode to Station (STA)
 *          Step 9 — Start WiFi — this triggers WIFI_EVENT_STA_START
 *          Step 10 — Block and wait for either CONNECTED or FAIL event bit
 */
iec104_err_t iec104_wifi_init(void)
{
    ESP_LOGI(TAG, "--- Initialising WiFi ---");

    /* ------------------------------------------------------------------
     * Step 1: Initialise Non-Volatile Storage (NVS).
     *         The WiFi driver stores calibration data in NVS.
     *         If NVS has no free pages or a new version, erase and re-init.
     * ------------------------------------------------------------------ */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS needs erase — erasing and reinitialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    /* ------------------------------------------------------------------
     * Step 2: Create the FreeRTOS event group for WiFi synchronisation.
     *         The event handler will set bits on this group.
     * ------------------------------------------------------------------ */
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return IEC104_ERR_WIFI;
    }

    /* ------------------------------------------------------------------
     * Step 3: Initialise the underlying TCP/IP stack (lwIP).
     *         Must be called once before any network interface is created.
     * ------------------------------------------------------------------ */
    ESP_ERROR_CHECK(esp_netif_init());

    /* ------------------------------------------------------------------
     * Step 4: Create the default system event loop.
     *         WiFi and IP events are dispatched through this loop.
     * ------------------------------------------------------------------ */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* ------------------------------------------------------------------
     * Step 5: Create the default WiFi STA (station) netif object.
     *         This represents the WiFi client interface on the TCP/IP stack.
     * ------------------------------------------------------------------ */
    esp_netif_create_default_wifi_sta();

    /* ------------------------------------------------------------------
     * Step 6: Initialise the WiFi driver with default configuration.
     *         WIFI_INIT_CONFIG_DEFAULT() fills in all internal defaults.
     * ------------------------------------------------------------------ */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* ------------------------------------------------------------------
     * Step 7: Register our event handler for both WIFI_EVENT and IP_EVENT.
     *         ESP_EVENT_ANY_ID means: call the handler for all IDs in that base.
     * ------------------------------------------------------------------ */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,          /* Event base: all WiFi events          */
        ESP_EVENT_ANY_ID,    /* Event ID:   any WiFi event           */
        &wifi_event_handler, /* Our handler function                 */
        NULL,                /* No extra argument needed             */
        NULL                 /* We don't need the handler instance   */
        ));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,            /* Event base: IP events                */
        IP_EVENT_STA_GOT_IP, /* Event ID:   only "got IP" event      */
        &wifi_event_handler, /* Same handler handles IP events too   */
        NULL,
        NULL));

    /* ------------------------------------------------------------------
     * Step 8: Configure WiFi credentials (SSID and password).
     *         wifi_config_t is a union; we fill the 'sta' (station) member.
     *         strncpy is used to safely copy SSID/password into fixed buffers.
     * ------------------------------------------------------------------ */
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t)); /* Zero out the whole struct */

    /* Copy SSID — max 32 bytes as per 802.11 standard */
    strncpy((char *)wifi_config.sta.ssid,
            WIFI_SSID,
            sizeof(wifi_config.sta.ssid) - 1);

    /* Copy password — max 64 bytes */
    strncpy((char *)wifi_config.sta.password,
            WIFI_PASSWORD,
            sizeof(wifi_config.sta.password) - 1);

    /* ------------------------------------------------------------------
     * Step 9: Set WiFi to STA (client) mode and apply the config.
     * ------------------------------------------------------------------ */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    /* ------------------------------------------------------------------
     * Step 10: Start the WiFi driver.
     *          This triggers WIFI_EVENT_STA_START → our handler calls connect.
     * ------------------------------------------------------------------ */
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi started — waiting for connection...");

    /* ------------------------------------------------------------------
     * Step 11: Block here until either CONNECTED or FAIL bit is set.
     *          xEventGroupWaitBits() returns when ANY of the listed bits is set.
     *          portMAX_DELAY = wait forever (our timeout is handled by retry count).
     * ------------------------------------------------------------------ */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,                 /* Event group to watch          */
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, /* Wait for either of these    */
        pdFALSE,                            /* Don't clear bits on exit      */
        pdFALSE,                            /* Wait for ANY bit (not all)    */
        portMAX_DELAY                       /* Block indefinitely            */
    );

    /* ------------------------------------------------------------------
     * Step 12: Check which bit was set and return accordingly.
     * ------------------------------------------------------------------ */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "WiFi ready — IP assigned via DHCP");
        return IEC104_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGE(TAG, "WiFi failed — could not connect to SSID: %s", WIFI_SSID);
        return IEC104_ERR_WIFI;
    }

    /* Should never reach here */
    ESP_LOGE(TAG, "WiFi: unexpected event group state");
    return IEC104_ERR_WIFI;
}

/* ------------------------------------------------------------------------- */

/**
 * @brief  Open a blocking TCP socket and connect to the IEC 104 server.
 *
 *         Steps:
 *          1. Create a TCP socket (AF_INET = IPv4, SOCK_STREAM = TCP)
 *          2. Build server address struct with IP and port
 *          3. Call connect() — blocks until connected or error
 *          4. Store socket fd in s_tcp_socket for use by other functions
 */
iec104_err_t iec104_tcp_connect(void)
{
    ESP_LOGI(TAG, "--- Opening TCP connection to %s:%d ---",
             IEC104_SERVER_IP, IEC104_SERVER_PORT);

    /* ------------------------------------------------------------------
     * Step 1: Create a TCP/IP socket.
     *   AF_INET      = IPv4 address family
     *   SOCK_STREAM  = TCP (reliable, stream-oriented)
     *   0            = protocol auto-select (TCP for SOCK_STREAM)
     * ------------------------------------------------------------------ */
    s_tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (s_tcp_socket < 0)
    {
        ESP_LOGE(TAG, "socket() failed: errno=%d (%s)", errno, strerror(errno));
        return IEC104_ERR_SOCKET;
    }
    ESP_LOGI(TAG, "TCP socket created (fd=%d)", s_tcp_socket);

    /* ------------------------------------------------------------------
     * Step 2: Set a receive timeout on the socket.
     *         This prevents recv() from blocking forever if the server
     *         does not reply. We set it to IEC104_STARTDT_TIMEOUT_MS.
     * ------------------------------------------------------------------ */
    struct timeval recv_timeout;
    recv_timeout.tv_sec = IEC104_STARTDT_TIMEOUT_MS / 1000;
    recv_timeout.tv_usec = (IEC104_STARTDT_TIMEOUT_MS % 1000) * 1000;

    if (setsockopt(s_tcp_socket, SOL_SOCKET, SO_RCVTIMEO,
                   &recv_timeout, sizeof(recv_timeout)) < 0)
    {
        ESP_LOGW(TAG, "setsockopt(SO_RCVTIMEO) failed — proceeding without timeout");
        /* Non-fatal — continue anyway */
    }

    /* ------------------------------------------------------------------
     * Step 3: Fill in the server address structure.
     *   sin_family      = AF_INET (IPv4)
     *   sin_port        = server port in network byte order (big-endian)
     *   sin_addr.s_addr = server IP, converted from string to 32-bit int
     * ------------------------------------------------------------------ */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(IEC104_SERVER_PORT); /* host-to-network byte order */

    /* inet_pton converts "192.168.x.x" string → binary network address */
    int ip_result = inet_pton(AF_INET, IEC104_SERVER_IP, &server_addr.sin_addr);
    if (ip_result <= 0)
    {
        ESP_LOGE(TAG, "inet_pton() failed for IP '%s' — check IEC104_SERVER_IP macro",
                 IEC104_SERVER_IP);
        close(s_tcp_socket);
        s_tcp_socket = -1;
        return IEC104_ERR_CONNECT;
    }

    /* ------------------------------------------------------------------
     * Step 4: Attempt the TCP connection.
     *         connect() blocks until the server accepts or an error occurs.
     * ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "Connecting to IEC104 server at %s:%d ...",
             IEC104_SERVER_IP, IEC104_SERVER_PORT);

    int conn_result = connect(s_tcp_socket,
                              (struct sockaddr *)&server_addr,
                              sizeof(server_addr));
    if (conn_result != 0)
    {
        ESP_LOGE(TAG, "connect() failed: errno=%d (%s)", errno, strerror(errno));
        close(s_tcp_socket);
        s_tcp_socket = -1;
        return IEC104_ERR_CONNECT;
    }

    ESP_LOGI(TAG, "TCP connection established to %s:%d",
             IEC104_SERVER_IP, IEC104_SERVER_PORT);
    return IEC104_OK;
}

/* ------------------------------------------------------------------------- */

/**
 * @brief  Perform the IEC 60870-5-104 STARTDT handshake.
 *
 *         IEC 104 requires that after TCP is established, the client
 *         (controlled station) sends a STARTDT_act U-frame to activate
 *         the data transfer phase. The server must reply with STARTDT_con.
 *
 *         STARTDT_act frame (6 bytes):
 *           Byte 0: 0x68  (start byte — always fixed)
 *           Byte 1: 0x04  (length = 4, counts only the 4 control bytes)
 *           Byte 2: 0x07  (CF1 — identifies this as STARTDT_act)
 *           Byte 3: 0x00  (CF2 — unused in U-frames)
 *           Byte 4: 0x00  (CF3 — unused in U-frames)
 *           Byte 5: 0x00  (CF4 — unused in U-frames)
 *
 *         STARTDT_con frame (6 bytes — same layout):
 *           Byte 0: 0x68
 *           Byte 1: 0x04
 *           Byte 2: 0x0B  (CF1 — identifies this as STARTDT_con)
 *           Byte 3: 0x00
 *           Byte 4: 0x00
 *           Byte 5: 0x00
 */
iec104_err_t iec104_startdt(void)
{
    ESP_LOGI(TAG, "--- Performing STARTDT handshake ---");

    /* Sanity check: make sure TCP socket is open */
    if (s_tcp_socket < 0)
    {
        ESP_LOGE(TAG, "STARTDT called but TCP socket is not open");
        return IEC104_ERR_SEND;
    }

    /* ------------------------------------------------------------------
     * Step 1: Build the STARTDT_act frame.
     *
     *         We manually fill each byte according to IEC 60870-5-104
     *         Table 4 (U-format control field encoding):
     *           CF1 = 0x07 means: bits[7:2]=000001, bit1=1, bit0=1
     *                 → bit1=1, bit0=1 → this is a U-frame
     *                 → bits[7:2] = 000001 → STARTDT act
     * ------------------------------------------------------------------ */
    iec104_uframe_t startdt_act;

    startdt_act.start = IEC104_START_BYTE;     /* 0x68 — fixed start byte      */
    startdt_act.length = IEC104_UFRAME_LENGTH; /* 0x04 — 4 bytes follow        */
    startdt_act.cf1 = IEC104_CF1_STARTDT_ACT;  /* 0x07 — STARTDT activate      */
    startdt_act.cf2 = 0x00U;                   /* Unused in U-frames           */
    startdt_act.cf3 = 0x00U;                   /* Unused in U-frames           */
    startdt_act.cf4 = 0x00U;                   /* Unused in U-frames           */

    /* Log the bytes we are about to send for debugging */
    ESP_LOGI(TAG, "Sending STARTDT_act: [%02X %02X %02X %02X %02X %02X]",
             startdt_act.start, startdt_act.length,
             startdt_act.cf1, startdt_act.cf2,
             startdt_act.cf3, startdt_act.cf4);

    /* ------------------------------------------------------------------
     * Step 2: Send the STARTDT_act frame over the TCP socket.
     *         send() returns the number of bytes actually sent.
     *         We must ensure all 6 bytes are sent.
     * ------------------------------------------------------------------ */
    int bytes_sent = send(s_tcp_socket,
                          &startdt_act,
                          IEC104_UFRAME_TOTAL, /* Send exactly 6 bytes */
                          0);                  /* No special flags     */

    if (bytes_sent != IEC104_UFRAME_TOTAL)
    {
        ESP_LOGE(TAG, "send() failed: sent=%d expected=%d errno=%d (%s)",
                 bytes_sent, IEC104_UFRAME_TOTAL, errno, strerror(errno));
        return IEC104_ERR_SEND;
    }
    ESP_LOGI(TAG, "STARTDT_act sent successfully (%d bytes)", bytes_sent);

    /* ------------------------------------------------------------------
     * Step 3: Wait for STARTDT_con reply from the server.
     *         The server must respond with a 6-byte STARTDT_con frame.
     *         recv() will block until data arrives or the SO_RCVTIMEO fires.
     * ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "Waiting for STARTDT_con from server (timeout: %d ms)...",
             IEC104_STARTDT_TIMEOUT_MS);

    iec104_uframe_t response;
    memset(&response, 0, sizeof(response)); /* Zero buffer before read */

    int bytes_received = recv(s_tcp_socket,
                              &response,
                              IEC104_UFRAME_TOTAL, /* Expect exactly 6 bytes */
                              0);                  /* No special flags       */

    if (bytes_received <= 0)
    {
        if (bytes_received == 0)
        {
            /* Server closed the connection */
            ESP_LOGE(TAG, "recv(): server closed the connection (0 bytes)");
        }
        else
        {
            /* Error or timeout */
            ESP_LOGE(TAG, "recv() failed or timed out: errno=%d (%s)",
                     errno, strerror(errno));
        }
        return IEC104_ERR_RECV;
    }

    /* Log the raw bytes received for debugging */
    ESP_LOGI(TAG, "Received %d bytes: [%02X %02X %02X %02X %02X %02X]",
             bytes_received,
             response.start, response.length,
             response.cf1, response.cf2,
             response.cf3, response.cf4);

    /* ------------------------------------------------------------------
     * Step 4: Validate that the received frame is a valid STARTDT_con.
     *
     *         A valid STARTDT_con must have:
     *           start  == 0x68  (correct IEC 104 start byte)
     *           length == 0x04  (U-frame has no ASDU payload)
     *           cf1    == 0x0B  (STARTDT_con identifier per standard Table 4)
     *           cf2    == 0x00
     *           cf3    == 0x00
     *           cf4    == 0x00
     * ------------------------------------------------------------------ */

    /* Check start byte */
    if (response.start != IEC104_START_BYTE)
    {
        ESP_LOGE(TAG, "Invalid start byte: got 0x%02X, expected 0x%02X",
                 response.start, IEC104_START_BYTE);
        return IEC104_ERR_BAD_RESPONSE;
    }

    /* Check CF1 byte for STARTDT_con identifier */
    if (response.cf1 != IEC104_CF1_STARTDT_CON)
    {
        ESP_LOGE(TAG, "Unexpected CF1 byte: got 0x%02X, expected 0x%02X (STARTDT_con)",
                 response.cf1, IEC104_CF1_STARTDT_CON);
        return IEC104_ERR_BAD_RESPONSE;
    }

    /* ------------------------------------------------------------------
     * Step 5: Handshake successful — log the success message.
     * ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " IEC104: Connected");
    ESP_LOGI(TAG, " STARTDT handshake complete.");
    ESP_LOGI(TAG, " Data transfer phase is now ACTIVE.");
    ESP_LOGI(TAG, "==============================================");

    return IEC104_OK;
}

/* ------------------------------------------------------------------------- */

/**
 * @brief  Close the TCP socket and reset the socket file descriptor.
 *
 *         Should be called:
 *          - After the test is complete
 *          - On any error to release the socket resource
 *          - Before rebooting or going to sleep
 */
void iec104_tcp_disconnect(void)
{
    if (s_tcp_socket >= 0)
    {
        ESP_LOGI(TAG, "Closing TCP socket (fd=%d)", s_tcp_socket);
        close(s_tcp_socket); /* Close the TCP connection */
        s_tcp_socket = -1;   /* Mark socket as closed    */
        ESP_LOGI(TAG, "TCP socket closed");
    }
    else
    {
        ESP_LOGW(TAG, "iec104_tcp_disconnect() called but no socket was open");
    }
}