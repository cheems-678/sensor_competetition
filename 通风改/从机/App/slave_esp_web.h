#ifndef SLAVE_ESP_WEB_H
#define SLAVE_ESP_WEB_H
#include <stdint.h>

#define SLAVE_ESP_SSID "Sensor_Node_01"
#define SLAVE_ESP_IP "192.168.4.1"
typedef enum {
    SLAVE_ESP_BOOT, SLAVE_ESP_INIT_START, SLAVE_ESP_INIT_TX, SLAVE_ESP_INIT_WAIT,
    SLAVE_ESP_READY, SLAVE_ESP_SEND_START, SLAVE_ESP_SEND_TX, SLAVE_ESP_PROMPT,
    SLAVE_ESP_RAW_START, SLAVE_ESP_RAW_TX, SLAVE_ESP_SEND_WAIT,
    SLAVE_ESP_CLOSE_START, SLAVE_ESP_CLOSE_TX, SLAVE_ESP_CLOSE_WAIT,
    SLAVE_ESP_RECOVER_START, SLAVE_ESP_RECOVER_TX, SLAVE_ESP_RECOVER_QUIET,
    SLAVE_ESP_RESET_START, SLAVE_ESP_RESET_TX, SLAVE_ESP_RESET_WAIT,
    SLAVE_ESP_BACKOFF
} SlaveEspWebState;
typedef enum {
    SLAVE_ESP_ERROR_NONE, SLAVE_ESP_ERROR_AT, SLAVE_ESP_ERROR_TIMEOUT,
    SLAVE_ESP_ERROR_UART, SLAVE_ESP_ERROR_OVERFLOW, SLAVE_ESP_ERROR_IPD
} SlaveEspWebError;
typedef struct {
    uint32_t state, ready, init_step, at_detected, init_count, recovery_count;
    uint32_t request_count, response_count, rejected_count, disconnect_count;
    uint32_t last_error, timeout_count, active_connections;
    char at_version[80];
} SlaveEspWebDiagnostics;
extern volatile SlaveEspWebDiagnostics SlaveEspWebDiag;
void SlaveEspWeb_Init(uint32_t now_ms);
void SlaveEspWeb_Process(uint32_t now_ms);
#endif
