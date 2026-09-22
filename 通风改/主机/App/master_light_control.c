#include "master_light_control.h"

#include <string.h>

#include "bh1750.h"
#include "ws2812.h"

#define MASTER_LIGHT_TOGGLE_INTERVAL_MS (1000UL)
#define MASTER_LIGHT_WHITE_LEVEL        (51U)
#define MASTER_BH1750_FIRST_SAMPLE_MS   (180UL)
#define MASTER_BH1750_SAMPLE_MS         (500UL)
#define MASTER_BH1750_RETRY_MS          (2000UL)

volatile MasterLightDiagnostics MasterLightDiag;
static uint32_t g_next_toggle_ms;
static uint32_t g_next_sensor_ms;

static uint8_t MasterLight_Apply(uint8_t turn_on, uint32_t now_ms)
{
    uint8_t level = (turn_on != 0U) ? MASTER_LIGHT_WHITE_LEVEL : 0U;

    if (Ws2812_SetSolid(level, level, level) == 0U)
    {
        MasterLightDiag.ws2812_failure_count++;
        return 0U;
    }
    MasterLightDiag.light_on = (turn_on != 0U) ? 1U : 0U;
    MasterLightDiag.toggle_count++;
    MasterLightDiag.last_toggle_ms = now_ms;
    return 1U;
}

void MasterLight_Init(uint32_t now_ms)
{
    memset((void *)&MasterLightDiag, 0, sizeof(MasterLightDiag));
    Ws2812_Init();
    (void)MasterLight_Apply(1U, now_ms);
    g_next_toggle_ms = now_ms + MASTER_LIGHT_TOGGLE_INTERVAL_MS;
    MasterLightDiag.sensor_init_count++;
    if (Bh1750_Init() != 0U)
    {
        MasterLightDiag.sensor_ready = 1U;
        g_next_sensor_ms = now_ms + MASTER_BH1750_FIRST_SAMPLE_MS;
    }
    else
    {
        g_next_sensor_ms = now_ms + MASTER_BH1750_RETRY_MS;
    }
}

void MasterLight_Process(uint32_t now_ms)
{
    uint8_t next_state;
    uint32_t lux_x10;

    if ((int32_t)(now_ms - g_next_toggle_ms) >= 0)
    {
        next_state = (MasterLightDiag.light_on == 0U) ? 1U : 0U;
        (void)MasterLight_Apply(next_state, now_ms);
        g_next_toggle_ms = now_ms + MASTER_LIGHT_TOGGLE_INTERVAL_MS;
    }

    if ((int32_t)(now_ms - g_next_sensor_ms) < 0)
    {
        return;
    }
    if (MasterLightDiag.sensor_ready == 0U)
    {
        MasterLightDiag.sensor_init_count++;
        if (Bh1750_Init() != 0U)
        {
            MasterLightDiag.sensor_ready = 1U;
            g_next_sensor_ms = now_ms + MASTER_BH1750_FIRST_SAMPLE_MS;
        }
        else
        {
            MasterLightDiag.sensor_failure_count++;
            g_next_sensor_ms = now_ms + MASTER_BH1750_RETRY_MS;
        }
        return;
    }
    if (Bh1750_ReadLuxX10(&lux_x10) != 0U)
    {
        MasterLightDiag.lux_x10 = lux_x10;
        MasterLightDiag.sensor_sample_count++;
        g_next_sensor_ms = now_ms + MASTER_BH1750_SAMPLE_MS;
    }
    else
    {
        MasterLightDiag.sensor_failure_count++;
        MasterLightDiag.sensor_ready = 0U;
        g_next_sensor_ms = now_ms + MASTER_BH1750_RETRY_MS;
    }
}
