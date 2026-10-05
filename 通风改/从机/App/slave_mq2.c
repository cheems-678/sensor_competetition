#include "slave_mq2.h"
#include "mq2_adc.h"
#include <string.h>

#define CONVERSION_TIMEOUT_MS 10U
#define RETRY_MS 1000U
#if SLAVE_MQ2_DIVIDER_DEN == 0 || SLAVE_MQ2_VDDA_MV > 3600U || SLAVE_MQ2_DIVIDER_NUM > 100U
#error Invalid MQ2 voltage configuration
#endif
static uint32_t g_started, g_retry;
static SlaveMq2Sample g_sample;
volatile SlaveMq2Diagnostics SlaveMq2Diag;

static void Fail(uint32_t now, uint32_t error)
{
    Mq2Adc_Stop();
    SlaveMq2Diag.initialized = SlaveMq2Diag.pending = SlaveMq2Diag.valid = 0U;
    SlaveMq2Diag.last_error = error;
    SlaveMq2Diag.error_count++;
    g_retry = now;
}

void SlaveMq2_Init(uint32_t now_ms)
{
    memset((void *)&SlaveMq2Diag, 0, sizeof(SlaveMq2Diag));
    memset(&g_sample, 0, sizeof(g_sample));
    g_retry = now_ms - RETRY_MS;
    g_started = now_ms - SLAVE_MQ2_PERIOD_MS;
    SlaveMq2_Process(now_ms);
}

void SlaveMq2_Process(uint32_t now_ms)
{
    uint16_t raw;
    uint8_t result;
    if (SlaveMq2Diag.valid != 0U &&
        (uint32_t)(now_ms - g_sample.tick) >= SLAVE_MQ2_MAX_AGE_MS)
    { SlaveMq2Diag.valid = 0U; }
    if (SlaveMq2Diag.initialized == 0U)
    {
        if ((uint32_t)(now_ms - g_retry) < RETRY_MS) { return; }
        if (Mq2Adc_Init() == 0U) { Fail(now_ms, SLAVE_MQ2_ERROR_INIT); return; }
        SlaveMq2Diag.initialized = 1U;
        g_started = now_ms - SLAVE_MQ2_PERIOD_MS;
    }
    if (SlaveMq2Diag.pending != 0U)
    {
        result = Mq2Adc_Poll(&raw);
        /* LoRa/I2C can delay servicing an already completed conversion.
           Preserve its START time rather than refreshing its age here. */
        if ((uint32_t)(now_ms - g_started) >= SLAVE_MQ2_MAX_AGE_MS ||
            (result == 0U && (uint32_t)(now_ms - g_started) >= CONVERSION_TIMEOUT_MS))
        { Fail(now_ms, SLAVE_MQ2_ERROR_TIMEOUT); return; }
        if (result == 0U) { return; }
        if (result != 1U || raw > 4095U)
        { Fail(now_ms, SLAVE_MQ2_ERROR_CONVERSION); return; }
        g_sample.raw = raw;
        g_sample.pa7_mv = ((uint32_t)raw * SLAVE_MQ2_VDDA_MV + 2047U) / 4095U;
        g_sample.ao_mv = ((uint32_t)raw * SLAVE_MQ2_VDDA_MV * SLAVE_MQ2_DIVIDER_NUM +
            2047U * SLAVE_MQ2_DIVIDER_DEN) / (4095U * SLAVE_MQ2_DIVIDER_DEN);
        g_sample.tick = g_started;
        g_sample.sequence = ++SlaveMq2Diag.sample_count;
        SlaveMq2Diag.sample_tick = g_sample.tick;
        SlaveMq2Diag.raw = raw;
        SlaveMq2Diag.pa7_mv = g_sample.pa7_mv;
        SlaveMq2Diag.ao_mv = g_sample.ao_mv;
        SlaveMq2Diag.pending = 0U;
        SlaveMq2Diag.valid = 1U;
        SlaveMq2Diag.last_error = SLAVE_MQ2_ERROR_NONE;
    }
    if ((uint32_t)(now_ms - g_started) >= SLAVE_MQ2_PERIOD_MS)
    {
        if (Mq2Adc_Start() == 0U) { Fail(now_ms, SLAVE_MQ2_ERROR_START); return; }
        g_started = now_ms;
        SlaveMq2Diag.pending = 1U;
    }
}

uint8_t SlaveMq2_GetSample(uint32_t now_ms, SlaveMq2Sample *sample)
{
    if (sample == 0 || SlaveMq2Diag.valid == 0U ||
        (uint32_t)(now_ms - g_sample.tick) >= SLAVE_MQ2_MAX_AGE_MS) { return 0U; }
    *sample = g_sample;
    return 1U;
}
