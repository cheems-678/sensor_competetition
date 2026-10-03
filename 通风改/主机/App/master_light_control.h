#ifndef MASTER_LIGHT_CONTROL_H
#define MASTER_LIGHT_CONTROL_H

#include <stdint.h>

typedef struct
{
    uint32_t toggle_count;
    uint32_t ws2812_failure_count;
    uint32_t last_toggle_ms;
    uint32_t light_on;     /* Last successfully applied state. */
    uint32_t raw_dark;     /* PA7 high means dark. */
    uint32_t stable_dark;
    uint32_t input_valid;  /* First stable input has been confirmed. */
    uint32_t output_valid; /* A WS2812 frame has been sent successfully. */
} MasterLightDiagnostics;

extern volatile MasterLightDiagnostics MasterLightDiag;

/* Owns PA7 digital input; PB13 remains owned by the WS2812 driver. */
void MasterLight_Init(uint32_t now_ms);
void MasterLight_Process(uint32_t now_ms);

#endif /* MASTER_LIGHT_CONTROL_H */
