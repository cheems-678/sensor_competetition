#ifndef MASTER_RAIN_H
#define MASTER_RAIN_H

#include <stdint.h>

#define MASTER_RAIN_STATE_UNKNOWN (0xFFU)

typedef struct
{
    uint32_t raw_raining;
    uint32_t candidate_raining;
    uint32_t stable_raining;
    uint32_t input_valid;
    uint32_t candidate_since_ms;
    uint32_t sample_tick;
    uint32_t sample_count;
    uint32_t confirmation_count;
} MasterRainDiagnostics;

extern volatile MasterRainDiagnostics MasterRainDiag;

/* PA11 DO only; no actuator writes, interrupts or timer ownership. */
void MasterRain_Init(uint32_t now_ms);
void MasterRain_Process(uint32_t now_ms);
/* 0=dry, 1=wet, FF=not yet confirmed or sample expired. */
uint8_t MasterRain_GetState(uint32_t now_ms);

#endif /* MASTER_RAIN_H */
