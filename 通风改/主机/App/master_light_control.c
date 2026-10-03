#include "master_light_control.h"

#include <string.h>

#include "master_config.h"
#include "stm32f1xx_hal.h"
#include "ws2812.h"

#define MASTER_LIGHT_DO_PORT            GPIOA
#define MASTER_LIGHT_DO_PIN             GPIO_PIN_7

volatile MasterLightDiagnostics MasterLightDiag;
static uint32_t g_candidate_since_ms;
static uint32_t g_last_attempt_ms;
static uint8_t g_retry_pending;

static uint8_t MasterLight_ReadDark(void)
{
    return (HAL_GPIO_ReadPin(MASTER_LIGHT_DO_PORT, MASTER_LIGHT_DO_PIN) ==
            GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t MasterLight_Apply(uint8_t light_on, uint32_t now_ms)
{
    uint8_t level = (light_on != 0U) ? MASTER_LIGHT_WHITE_LEVEL : 0U;

    g_last_attempt_ms = now_ms;
    if (Ws2812_SetSolid(level, level, level) == 0U)
    {
        MasterLightDiag.ws2812_failure_count++;
        g_retry_pending = 1U;
        return 0U;
    }

    if ((MasterLightDiag.output_valid != 0U) &&
        (MasterLightDiag.light_on != light_on))
    {
        MasterLightDiag.toggle_count++;
        MasterLightDiag.last_toggle_ms = now_ms;
    }
    MasterLightDiag.light_on = light_on;
    MasterLightDiag.output_valid = 1U;
    g_retry_pending = 0U;
    return 1U;
}

void MasterLight_Init(uint32_t now_ms)
{
    GPIO_InitTypeDef gpio = {0};

    memset((void *)&MasterLightDiag, 0, sizeof(MasterLightDiag));
    g_candidate_since_ms = now_ms;
    g_last_attempt_ms = now_ms;
    g_retry_pending = 0U;

    /* The sensor circuit already pulls DO up to 3.3 V through R22. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = MASTER_LIGHT_DO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(MASTER_LIGHT_DO_PORT, &gpio);
    MasterLightDiag.raw_dark = MasterLight_ReadDark();

    Ws2812_Init();
    /* Clear any previously latched color before the first stable input. */
    (void)MasterLight_Apply(0U, now_ms);
}

void MasterLight_Process(uint32_t now_ms)
{
    uint8_t raw_dark = MasterLight_ReadDark();
    uint8_t target_on;

    if (raw_dark != MasterLightDiag.raw_dark)
    {
        MasterLightDiag.raw_dark = raw_dark;
        g_candidate_since_ms = now_ms;
    }
    if ((uint32_t)(now_ms - g_candidate_since_ms) >= MASTER_LIGHT_STABLE_MS)
    {
        MasterLightDiag.stable_dark = raw_dark;
        MasterLightDiag.input_valid = 1U;
    }

    target_on = (MasterLightDiag.input_valid != 0U) ?
                (uint8_t)MasterLightDiag.stable_dark : 0U;
    if ((MasterLightDiag.output_valid != 0U) &&
        (MasterLightDiag.light_on == target_on) && (g_retry_pending == 0U))
    {
        return;
    }
    if ((g_retry_pending != 0U) &&
        ((uint32_t)(now_ms - g_last_attempt_ms) < MASTER_LIGHT_RETRY_MS))
    {
        return;
    }

    (void)MasterLight_Apply(target_on, now_ms);
}
