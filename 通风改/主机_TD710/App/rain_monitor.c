#include "rain_monitor.h"

#include <stddef.h>

#include "master_config.h"

void RainMonitor_Init(RainMonitor *monitor, uint8_t raw_raining,
                      uint32_t now_ms)
{
    if (monitor == NULL)
    {
        return;
    }

    monitor->is_raining = 0U;
    monitor->candidate_raining = (raw_raining != 0U) ? 1U : 0U;
    monitor->candidate_since_ms = now_ms;
}

void RainMonitor_Update(RainMonitor *monitor, uint8_t raw_raining,
                        uint32_t now_ms)
{
    uint32_t required_duration_ms;

    if (monitor == NULL)
    {
        return;
    }

    raw_raining = (raw_raining != 0U) ? 1U : 0U;
    if (raw_raining != monitor->candidate_raining)
    {
        monitor->candidate_raining = raw_raining;
        monitor->candidate_since_ms = now_ms;
    }

    if (monitor->candidate_raining == monitor->is_raining)
    {
        return;
    }

    required_duration_ms = (monitor->candidate_raining != 0U) ?
                           MASTER_RAIN_ASSERT_MS : MASTER_RAIN_CLEAR_MS;
    if ((uint32_t)(now_ms - monitor->candidate_since_ms) >= required_duration_ms)
    {
        monitor->is_raining = monitor->candidate_raining;
    }
}

uint8_t RainMonitor_IsRaining(const RainMonitor *monitor)
{
    if (monitor == NULL)
    {
        return 0U;
    }

    return monitor->is_raining;
}
