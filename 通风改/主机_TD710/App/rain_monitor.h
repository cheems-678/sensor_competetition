#ifndef RAIN_MONITOR_H
#define RAIN_MONITOR_H

#include <stdint.h>

typedef struct
{
    uint8_t is_raining;
    uint8_t candidate_raining;
    uint32_t candidate_since_ms;
} RainMonitor;

void RainMonitor_Init(RainMonitor *monitor, uint8_t raw_raining,
                      uint32_t now_ms);
void RainMonitor_Update(RainMonitor *monitor, uint8_t raw_raining,
                        uint32_t now_ms);
uint8_t RainMonitor_IsRaining(const RainMonitor *monitor);

#endif /* RAIN_MONITOR_H */
