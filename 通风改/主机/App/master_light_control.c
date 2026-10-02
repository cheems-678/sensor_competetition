#include "master_light_control.h"

#include <string.h>

#include "ws2812.h"

#define MASTER_LIGHT_WHITE_LEVEL        (51U)

volatile MasterLightDiagnostics MasterLightDiag;
static uint8_t MasterLight_ApplyConstant(uint32_t now_ms)
{
    if (Ws2812_SetSolid(MASTER_LIGHT_WHITE_LEVEL,
                        MASTER_LIGHT_WHITE_LEVEL,
                        MASTER_LIGHT_WHITE_LEVEL) == 0U)
    {
        MasterLightDiag.ws2812_failure_count++;
        return 0U;
    }
    MasterLightDiag.light_on = 1U;
    MasterLightDiag.toggle_count++;
    MasterLightDiag.last_toggle_ms = now_ms;
    return 1U;
}

void MasterLight_Init(uint32_t now_ms)
{
    memset((void *)&MasterLightDiag, 0, sizeof(MasterLightDiag));
    Ws2812_Init();
    (void)MasterLight_ApplyConstant(now_ms);
}

void MasterLight_Process(uint32_t now_ms)
{
    (void)now_ms;
}
