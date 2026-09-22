#ifndef MASTER_LIGHT_CONTROL_H
#define MASTER_LIGHT_CONTROL_H

#include <stdint.h>

typedef struct
{
    uint32_t toggle_count;
    uint32_t ws2812_failure_count;
    uint32_t last_toggle_ms;
    uint32_t light_on;
    uint32_t sensor_init_count;
    uint32_t sensor_sample_count;
    uint32_t sensor_failure_count;
    uint32_t sensor_ready;
    uint32_t lux_x10;
} MasterLightDiagnostics;

extern volatile MasterLightDiagnostics MasterLightDiag;

void MasterLight_Init(uint32_t now_ms);
void MasterLight_Process(uint32_t now_ms);

#endif /* MASTER_LIGHT_CONTROL_H */
