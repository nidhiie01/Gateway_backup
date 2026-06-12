/**
 * @file    iec104_client.h
 * @brief   IEC 60870-5-104 Client (Controlled Station) — Public API
 *
 * This library implements the IEC 60870-5-104 protocol from scratch,
 * based solely on the IEC 60870-5-104 standard specification.
 *
 * Scope of this file:
 *  - APCI (Application Protocol Control Information) constants
 *  - APDU frame type definitions
 *  - U-frame control field values (STARTDT, STOPDT, TESTFR)
 *  - Public function declarations for the client
 *
 * Terminology (per IEC 60870-5-104 standard):
 *  - Controlled Station : the CLIENT  (this device — ESP32 gateway)
 *  - Controlling Station: the SERVER  (RTU / simulator on PC)
 *  - APDU : Application Protocol Data Unit (complete message)
 *  - APCI : Application Protocol Control Information (6-byte header)
 *  - ASDU : Application Service Data Unit (payload, not used in this step)
 *
 * Author  : Custom implementation for ESP32 / ESP-IDF v5.4.1
 * Target  : ESP32 with FreeRTOS (ESP-IDF)
 */

#ifndef IEC104_CLIENT_H
#define IEC104_CLIENT_H

#ifdef __cplusplus
extern "C"
{
#endif

/* =========================================================================
 * INCLUDES
 * ========================================================================= */
#include <stdint.h>  /* uint8_t, uint16_t, etc.   */
#include <stdbool.h> /* bool, true, false          */

/* =========================================================================
 * USER CONFIGURATION MACROS
 * Edit these macros to match your network setup.
 * ========================================================================= */

/**
 * @brief IP address of the IEC 104 Server (RTU Simulator on your PC).
 *        Replace this with your Windows PC's local WiFi IP address.
 *        You can find it by running  ipconfig  in Windows CMD.
 *        Example: "192.168.1.105"
 */
#define IEC104_SERVER_IP "192.168.1.72"

/**
 * @brief TCP port used by IEC 60870-5-104.
 *        Default port as defined in the standard is 2404.
 *        Change only if your simulator uses a different port.
 */
#define IEC104_SERVER_PORT 2404

/**
 * @brief WiFi network SSID (network name) to connect to.
 *        Replace with your actual WiFi SSID.
 */
#define WIFI_SSID "Adiinfigtpl4G"

/**
 * @brief WiFi network password.
 *        Replace with your actual WiFi password.
 */
#define WIFI_PASSWORD "Adiinfi@1234"

/**
 * @brief Maximum time (in milliseconds) to wait for WiFi connection.
 *        Increase if your router is slow to respond.
 */
#define WIFI_CONNECT_TIMEOUT_MS 10000

/**
 * @brief Maximum time (in milliseconds) to wait for STARTDT_con reply
 *        from the server after sending STARTDT_act.
 *        Per standard, server should respond quickly (~t1 timer = 15s default).
 */
#define IEC104_STARTDT_TIMEOUT_MS 5000

/* =========================================================================
 * IEC 60870-5-104 PROTOCOL CONSTANTS
 * These values are defined by the IEC 60870-5-104 standard — do not change.
 * ========================================================================= */

/**
 * @brief Start byte of every APDU frame.
 *        Fixed value 0x68 as defined in IEC 60870-5-104, Section 5.
 */
#define IEC104_START_BYTE 0x68U

/**
 * @brief Length of the APCI header in bytes.
 *        APCI = Start(1) + Length(1) + Control fields(4) = 6 bytes total.
 *        But the "Length" field counts only the 4 control-field bytes,
 *        so minimum APDU length field value = 4 (for U-frames with no ASDU).
 */
#define IEC104_APCI_SIZE 6U

/**
 * @brief Minimum value of the APDU "Length" field.
 *        For U-frames (STARTDT, STOPDT, TESTFR): no ASDU payload,
 *        so the length field = 4 (only the 4 control bytes).
 */
#define IEC104_UFRAME_LENGTH 4U

/**
 * @brief Total byte size of a U-frame APDU on the wire.
 *        = 1 (start) + 1 (length field) + 4 (control fields) = 6 bytes.
 */
#define IEC104_UFRAME_TOTAL 6U

/* =========================================================================
 * U-FRAME CONTROL FIELD VALUES
 *
 * In IEC 60870-5-104, the 4 control bytes for a U-frame look like this:
 *
 *   Byte CF1: identifies frame type and U-frame function
 *   Byte CF2: mirrors CF1 pattern or 0x00
 *   Byte CF3: 0x00 (not used in U-frames)
 *   Byte CF4: 0x00 (not used in U-frames)
 *
 * U-frame is identified by bits 0 and 1 of CF1 both being 1 (i.e., CF1 & 0x03 == 0x03).
 *
 * STARTDT act  = 0x07, 0x00, 0x00, 0x00
 * STARTDT con  = 0x0B, 0x00, 0x00, 0x00
 * STOPDT  act  = 0x13, 0x00, 0x00, 0x00
 * STOPDT  con  = 0x23, 0x00, 0x00, 0x00
 * TESTFR  act  = 0x43, 0x00, 0x00, 0x00
 * TESTFR  con  = 0x83, 0x00, 0x00, 0x00
 *
 * Source: IEC 60870-5-104, Table 4 — U-format control field
 * ========================================================================= */

/** @brief CF1 byte for STARTDT activate   (client → server: "start data transfer") */
#define IEC104_CF1_STARTDT_ACT 0x07U

/** @brief CF1 byte for STARTDT confirm    (server → client: "confirmed, data transfer active") */
#define IEC104_CF1_STARTDT_CON 0x0BU

/** @brief CF1 byte for STOPDT activate    (client → server: "stop data transfer") */
#define IEC104_CF1_STOPDT_ACT 0x13U

/** @brief CF1 byte for STOPDT confirm     (server → client) */
#define IEC104_CF1_STOPDT_CON 0x23U

/** @brief CF1 byte for TESTFR activate    (keep-alive ping from client) */
#define IEC104_CF1_TESTFR_ACT 0x43U

/** @brief CF1 byte for TESTFR confirm     (keep-alive pong from server) */
#define IEC104_CF1_TESTFR_CON 0x83U

    /* =========================================================================
     * APDU FRAME STRUCTURE
     * Represents the 6-byte APCI portion of a U-frame on the wire.
     * ========================================================================= */

    /**
     * @brief Raw byte representation of a U-frame APDU (6 bytes total).
     *
     * Wire layout (IEC 60870-5-104, Section 5.1):
     *
     *   Offset 0 : Start byte      — always 0x68
     *   Offset 1 : Length field    — number of bytes following (= 4 for U-frames)
     *   Offset 2 : Control Field 1 (CF1) — encodes frame type + U-frame command
     *   Offset 3 : Control Field 2 (CF2) — 0x00 for U-frames
     *   Offset 4 : Control Field 3 (CF3) — 0x00 for U-frames
     *   Offset 5 : Control Field 4 (CF4) — 0x00 for U-frames
     */
    typedef struct
    {
        uint8_t start;  /**< Always IEC104_START_BYTE (0x68)                   */
        uint8_t length; /**< Byte count after this field (4 for U-frames)       */
        uint8_t cf1;    /**< Control Field 1: identifies U-frame type           */
        uint8_t cf2;    /**< Control Field 2: 0x00 for all U-frames             */
        uint8_t cf3;    /**< Control Field 3: 0x00 for all U-frames             */
        uint8_t cf4;    /**< Control Field 4: 0x00 for all U-frames             */
    } iec104_uframe_t;

    /* =========================================================================
     * RETURN / STATUS CODES
     * ========================================================================= */

    /**
     * @brief Return codes for IEC 104 client API functions.
     */
    typedef enum
    {
        IEC104_OK = 0,                /**< Operation succeeded                  */
        IEC104_ERR_WIFI = -1,         /**< WiFi connection failed               */
        IEC104_ERR_SOCKET = -2,       /**< TCP socket creation failed           */
        IEC104_ERR_CONNECT = -3,      /**< TCP connect() to server failed       */
        IEC104_ERR_SEND = -4,         /**< TCP send() failed                    */
        IEC104_ERR_RECV = -5,         /**< TCP recv() failed or timed out       */
        IEC104_ERR_BAD_RESPONSE = -6, /**< Server reply was not STARTDT_con     */
    } iec104_err_t;

    /* =========================================================================
     * PUBLIC API
     * ========================================================================= */

    /**
     * @brief  Initialise and connect ESP32 to the configured WiFi network.
     *
     *         This function:
     *           1. Initialises the TCP/IP stack (esp_netif)
     *           2. Creates a WiFi station (STA) interface
     *           3. Configures SSID + password from macros
     *           4. Starts WiFi and blocks until IP is obtained or timeout
     *
     * @return IEC104_OK       — WiFi connected, IP obtained
     * @return IEC104_ERR_WIFI — Connection failed or timed out
     */
    iec104_err_t iec104_wifi_init(void);

    /**
     * @brief  Open a TCP connection to the IEC 104 server (simulator).
     *
     *         Uses the IP and port defined in IEC104_SERVER_IP / IEC104_SERVER_PORT.
     *         Creates a blocking TCP socket and calls connect().
     *
     * @return IEC104_OK          — TCP connection established
     * @return IEC104_ERR_SOCKET  — Could not create socket
     * @return IEC104_ERR_CONNECT — connect() to server failed
     */
    iec104_err_t iec104_tcp_connect(void);

    /**
     * @brief  Send STARTDT_act and wait for STARTDT_con from the server.
     *
     *         Steps performed:
     *           1. Build a 6-byte STARTDT_act U-frame from scratch
     *           2. Send it over the TCP socket
     *           3. Wait up to IEC104_STARTDT_TIMEOUT_MS for a 6-byte reply
     *           4. Validate that reply is a STARTDT_con frame
     *           5. Log "IEC104: Connected" on success
     *
     * @return IEC104_OK               — STARTDT handshake complete
     * @return IEC104_ERR_SEND         — Failed to send STARTDT_act
     * @return IEC104_ERR_RECV         — No reply or recv error
     * @return IEC104_ERR_BAD_RESPONSE — Reply was not a valid STARTDT_con
     */
    iec104_err_t iec104_startdt(void);

    /**
     * @brief  Close the TCP socket and release resources.
     *         Call this when done or on error to clean up gracefully.
     */
    void iec104_tcp_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* IEC104_CLIENT_H */