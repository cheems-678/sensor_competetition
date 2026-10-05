#include "master_rain.h"

#include <string.h>

#include "master_config.h"
#include "stm32f1xx_hal.h"

volatile MasterRainDiagnostics MasterRainDiag;

static uint8_t MasterRain_Read(void)
{
    return (HAL_GPIO_ReadPin(MASTER_RAIN_DO_PORT, MASTER_RAIN_DO_PIN) ==
            GPIO_PIN_RESET) ? 1U : 0U;
}

void MasterRain_Init(uint32_t now_ms)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t raw;

    memset((void *)&MasterRainDiag, 0, sizeof(MasterRainDiag));
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = MASTER_RAIN_DO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    /* Prevent floating DO. A disconnected module still looks dry, not healthy. */
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(MASTER_RAIN_DO_PORT, &gpio);

    raw = MasterRain_Read();
    MasterRainDiag.raw_raining = raw;
    MasterRainDiag.candidate_raining = raw;
    MasterRainDiag.candidate_since_ms = now_ms;
    MasterRainDiag.sample_tick = now_ms;
    MasterRainDiag.sample_count = 1U;
}

void MasterRain_Process(uint32_t now_ms)
{
    uint8_t raw = MasterRain_Read();
    uint32_t required_ms;

    /* After a long service gap, require a new uninterrupted confirmation. */
    if ((uint32_t)(now_ms - MasterRainDiag.sample_tick) >=
        MASTER_RAIN_SAMPLE_FRESH_MS)
    {
        MasterRainDiag.input_valid = 0U;
        MasterRainDiag.candidate_since_ms = now_ms;
    }
    MasterRainDiag.raw_raining = raw;
    MasterRainDiag.sample_tick = now_ms;
    MasterRainDiag.sample_count++;
    if (raw != MasterRainDiag.candidate_raining)
    {
        MasterRainDiag.candidate_raining = raw;
        MasterRainDiag.candidate_since_ms = now_ms;
    }
    if ((MasterRainDiag.input_valid != 0U) &&
        (raw == MasterRainDiag.stable_raining))
    {
        return;
    }

    required_ms = (raw != 0U) ? MASTER_RAIN_ASSERT_MS : MASTER_RAIN_CLEAR_MS;
    if ((uint32_t)(now_ms - MasterRainDiag.candidate_since_ms) >= required_ms)
    {
        MasterRainDiag.stable_raining = raw;
        MasterRainDiag.input_valid = 1U;
        MasterRainDiag.confirmation_count++;
    }
}

uint8_t MasterRain_GetState(uint32_t now_ms)
{
    if ((MasterRainDiag.input_valid == 0U) ||
        ((uint32_t)(now_ms - MasterRainDiag.sample_tick) >=
         MASTER_RAIN_SAMPLE_FRESH_MS))
    {
        return MASTER_RAIN_STATE_UNKNOWN;
    }
    return (uint8_t)MasterRainDiag.stable_raining;
}
