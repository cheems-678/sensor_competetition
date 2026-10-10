#include "master_acoustic.h"
#include "max4466_adc.h"
#include <string.h>

MasterAcousticDiagnostics MasterAcousticDiag;
static MasterAcousticSample latest;
static uint32_t retry_tick, start_tick;

static void Start(uint32_t now)
{
    start_tick = retry_tick = now;
    MasterAcousticDiag.valid = 0U;
    MasterAcousticDiag.running = Max4466Adc_Start();
    if (!MasterAcousticDiag.running) { MasterAcousticDiag.failures++; }
    else { MasterAcousticDiag.restarts++; }
}

void MasterAcoustic_Init(uint32_t now)
{
    uint8_t ch;
    memset(&MasterAcousticDiag, 0, sizeof(MasterAcousticDiag));
    memset(&latest, 0, sizeof(latest));
    for (ch = 0U; ch < 5U; ++ch) {
        MasterAcousticDiag.minimum[ch] = 0xFFFFU;
        MasterAcousticDiag.maximum[ch] = 0xFFFFU;
        MasterAcousticDiag.mean[ch] = 0xFFFFU;
    }
    Start(now);
}

void MasterAcoustic_Process(uint32_t now)
{
    Max4466AdcWindow window;
    uint8_t result, ch;
    if (!MasterAcousticDiag.running) {
        if ((uint32_t)(now - retry_tick) >= 1000U) { Start(now); }
        return;
    }
    result = Max4466Adc_Read(&window);
    if (result == 3U) {
        Max4466Adc_Stop();
        MasterAcousticDiag.valid = MasterAcousticDiag.running = 0U;
        MasterAcousticDiag.failures++; retry_tick = now;
        return;
    }
    if (result != 1U) { return; }
    MasterAcousticDiag.last_block_tick = window.tick;
    MasterAcousticDiag.samples = window.samples;
    MasterAcousticDiag.clipped_mask = 0U;
    for (ch = 0U; ch < MAX4466_CHANNELS; ++ch) {
        MasterAcousticDiag.minimum[ch] = window.minimum[ch];
        MasterAcousticDiag.maximum[ch] = window.maximum[ch];
        MasterAcousticDiag.mean[ch] = window.mean[ch];
        if (window.minimum[ch] == 0U || window.maximum[ch] == 4095U) {
            MasterAcousticDiag.clipped_mask |= (uint8_t)(1U << ch);
        }
    }
    if ((uint32_t)(now - start_tick) < 100U ||
        (uint32_t)(now - window.tick) >= MASTER_ACOUSTIC_MAX_AGE_MS) {
        MasterAcousticDiag.valid = 0U;
        return;
    }
    for (ch = 0U; ch < MAX4466_CHANNELS; ++ch) {
        latest.peak_to_peak[ch] = window.maximum[ch] - window.minimum[ch];
    }
    latest.tick = window.tick;
    MasterAcousticDiag.windows++;
    MasterAcousticDiag.valid = 1U;
}

uint8_t MasterAcoustic_GetSample(uint32_t now, MasterAcousticSample *output)
{
    if (!output || !MasterAcousticDiag.valid ||
        (uint32_t)(now - latest.tick) >= MASTER_ACOUSTIC_MAX_AGE_MS) { return 0U; }
    *output = latest; return 1U;
}
