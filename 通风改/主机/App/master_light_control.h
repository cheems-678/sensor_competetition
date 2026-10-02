#ifndef MASTER_LIGHT_CONTROL_H
#define MASTER_LIGHT_CONTROL_H

#include <stdint.h>

typedef struct
{
    uint32_t toggle_count;
    uint32_t ws2812_failure_count;
    uint32_t last_toggle_ms;
    uint32_t light_on;
} MasterLightDiagnostics;

extern volatile MasterLightDiagnostics MasterLightDiag;

void MasterLight_Init(uint32_t now_ms);
void MasterLight_Process(uint32_t now_ms);

#endif /* MASTER_LIGHT_CONTROL_H */
