#include "slave_hcsr04.h"
#include "hcsr04_timer.h"
#include <string.h>

volatile SlaveHcsr04Diagnostics SlaveHcsr04Diag;
static SlaveHcsr04Sample g_sample;
static uint16_t g_history[5];
static uint8_t g_count, g_next;
static uint32_t g_started, g_retry;

static void Invalidate(uint32_t error)
{
    SlaveHcsr04Diag.valid = 0U;
    SlaveHcsr04Diag.pending = 0U;
    SlaveHcsr04Diag.last_error = error;
    SlaveHcsr04Diag.error_count++;
    SlaveHcsr04Diag.filter_count = 0U;
    g_count = g_next = 0U;
}

static uint16_t Median(uint16_t value)
{
    uint16_t sorted[5], temp;
    uint8_t i, j;
    g_history[g_next] = value;
    g_next = (uint8_t)((g_next + 1U) % 5U);
    if (g_count < 5U) { g_count++; }
    for (i = 0U; i < g_count; i++) { sorted[i] = g_history[i]; }
    for (i = 1U; i < g_count; i++)
    {
        temp = sorted[i]; j = i;
        while (j != 0U && sorted[j - 1U] > temp)
        { sorted[j] = sorted[j - 1U]; j--; }
        sorted[j] = temp;
    }
    if ((g_count & 1U) == 0U)
    { return (uint16_t)(((uint32_t)sorted[g_count / 2U - 1U] + sorted[g_count / 2U]) / 2U); }
    return sorted[g_count / 2U];
}

void SlaveHcsr04_Init(uint32_t now_ms)
{
    memset((void *)&SlaveHcsr04Diag, 0, sizeof(SlaveHcsr04Diag));
    memset(&g_sample, 0, sizeof(g_sample));
    g_count = g_next = 0U;
    g_started = now_ms - SLAVE_HCSR04_PERIOD_MS;
    g_retry = now_ms - 1000U;
    SlaveHcsr04_Process(now_ms);
}

void SlaveHcsr04_Process(uint32_t now_ms)
{
    uint16_t pulse;
    Hcsr04Result result;
    int32_t mm;
    if (SlaveHcsr04Diag.valid != 0U &&
        (uint32_t)(now_ms - g_sample.tick) >= SLAVE_HCSR04_MAX_AGE_MS)
    {
        SlaveHcsr04Diag.valid = 0U;
        SlaveHcsr04Diag.filter_count = 0U;
        g_count = g_next = 0U;
    }
    if (SlaveHcsr04Diag.initialized == 0U)
    {
        if ((uint32_t)(now_ms - g_retry) < 1000U) { return; }
        g_retry = now_ms;
        if (Hcsr04Timer_Init() == 0U)
        { Invalidate(SLAVE_HCSR04_ERROR_INIT); return; }
        SlaveHcsr04Diag.initialized = 1U;
    }
    if (SlaveHcsr04Diag.pending != 0U)
    {
        result = Hcsr04Timer_Poll(&pulse);
        /* Capture timeout is hardware timed; this also bounds a lost interrupt.
           A late main-loop read cannot give an old capture a new source time. */
        if ((uint32_t)(now_ms - g_started) >= SLAVE_HCSR04_MAX_AGE_MS ||
            (result == HCSR04_PENDING && (uint32_t)(now_ms - g_started) >= 20U))
        { Hcsr04Timer_Cancel(); Invalidate(SLAVE_HCSR04_ERROR_TIMEOUT); }
        else if (result == HCSR04_PENDING) { return; }
        else if (result != HCSR04_COMPLETE)
        {
            Invalidate(result == HCSR04_ECHO_HIGH ? SLAVE_HCSR04_ERROR_ECHO_HIGH :
                result == HCSR04_TIMEOUT ? SLAVE_HCSR04_ERROR_TIMEOUT : SLAVE_HCSR04_ERROR_CAPTURE);
        }
        else
        {
            SlaveHcsr04Diag.pulse_us = pulse;
            /* Widen the product so a malformed pulse cannot overflow it. */
            mm = (int32_t)(((uint64_t)pulse * SLAVE_HCSR04_SOUND_MM_S + 1000000UL) / 2000000UL)
                + SLAVE_HCSR04_OFFSET_MM;
            SlaveHcsr04Diag.raw_mm = mm >= 0 ? (uint32_t)mm : 0U;
            if (mm < (int32_t)SLAVE_HCSR04_MIN_MM || mm > (int32_t)SLAVE_HCSR04_MAX_MM)
            { Invalidate(SLAVE_HCSR04_ERROR_RANGE); }
            else
            {
                g_sample.raw_mm = (uint16_t)mm;
                g_sample.distance_mm = Median((uint16_t)mm);
                g_sample.pulse_us = pulse;
                g_sample.filter_count = g_count;
                g_sample.tick = g_started;
                g_sample.sequence = ++SlaveHcsr04Diag.sample_count;
                SlaveHcsr04Diag.sample_tick = g_sample.tick;
                SlaveHcsr04Diag.distance_mm = g_sample.distance_mm;
                SlaveHcsr04Diag.filter_count = g_count;
                SlaveHcsr04Diag.pending = 0U;
                SlaveHcsr04Diag.last_error = SLAVE_HCSR04_ERROR_NONE;
                SlaveHcsr04Diag.valid = 1U;
            }
        }
    }
    if ((uint32_t)(now_ms - g_started) >= SLAVE_HCSR04_PERIOD_MS)
    {
        g_started = now_ms;
        if (Hcsr04Timer_Start() == 0U)
        { Hcsr04Timer_Cancel(); Invalidate(SLAVE_HCSR04_ERROR_START); return; }
        SlaveHcsr04Diag.trigger_count++;
        SlaveHcsr04Diag.pending = 1U;
    }
}

uint8_t SlaveHcsr04_GetSample(uint32_t now_ms, SlaveHcsr04Sample *sample)
{
    if (sample == 0 || SlaveHcsr04Diag.valid == 0U ||
        (uint32_t)(now_ms - g_sample.tick) >= SLAVE_HCSR04_MAX_AGE_MS) { return 0U; }
    *sample = g_sample;
    return 1U;
}
