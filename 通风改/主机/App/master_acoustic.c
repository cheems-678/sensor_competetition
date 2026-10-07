#include "master_acoustic.h"
#include "max4466_adc.h"
#include <string.h>
MasterAcousticDiagnostics MasterAcousticDiag;
static uint16_t block[MAX4466_BLOCK_WORDS], lo[5], hi[5];
static MasterAcousticSample latest;
static uint32_t retry_tick, start_tick, last_sequence;
static uint8_t blocks, have_sequence;
static void Discard(void)
{
    uint8_t i;
    blocks = 0U; have_sequence = 0U; MasterAcousticDiag.valid = 0U;
    for (i = 0U; i < 5U; ++i) { lo[i] = 4095U; hi[i] = 0U; }
}
static void Start(uint32_t now)
{
    Discard(); start_tick = retry_tick = now;
    MasterAcousticDiag.running = Max4466Adc_Start();
    MasterAcousticDiag.last_block_tick = now;
    if (!MasterAcousticDiag.running) { MasterAcousticDiag.failures++; }
    else { MasterAcousticDiag.restarts++; }
}
void MasterAcoustic_Init(uint32_t now)
{
    memset(&MasterAcousticDiag, 0, sizeof(MasterAcousticDiag)); Start(now);
}
void MasterAcoustic_Process(uint32_t now)
{
    uint32_t seq, tick;
    uint16_t frame;
    uint8_t result, ch;
    if (!MasterAcousticDiag.running) {
        if ((uint32_t)(now - retry_tick) >= 1000U) { Start(now); } return;
    }
    result = Max4466Adc_Read(block, &seq, &tick);
    if (result == 3U || (uint32_t)(now - MasterAcousticDiag.last_block_tick) >= 300U) {
        Max4466Adc_Stop(); Discard(); MasterAcousticDiag.running = 0U;
        MasterAcousticDiag.failures++; retry_tick = now; return;
    }
    if (result == 2U) { Discard(); MasterAcousticDiag.gaps++; return; }
    if (result == 0U) { return; }
    MasterAcousticDiag.last_block_tick = tick;
    if ((uint32_t)(now - start_tick) < 100U || (uint32_t)(now - tick) >= 300U) { Discard(); return; }
    if (have_sequence && (uint32_t)(seq - last_sequence) != 1U) { Discard(); MasterAcousticDiag.gaps++; }
    have_sequence = 1U; last_sequence = seq;
    for (frame = 0U; frame < MAX4466_BLOCK_FRAMES; ++frame) {
        for (ch = 0U; ch < 5U; ++ch) {
            uint16_t value = block[frame * 5U + ch];
            if (value > 4095U) { Discard(); MasterAcousticDiag.gaps++; return; }
            if (value < lo[ch]) { lo[ch] = value; }
            if (value > hi[ch]) { hi[ch] = value; }
        }
    }
    if (++blocks == 4U) {
        MasterAcousticDiag.clipped_mask = 0U;
        for (ch = 0U; ch < 5U; ++ch) {
            latest.peak_to_peak[ch] = hi[ch] - lo[ch];
            MasterAcousticDiag.minimum[ch] = lo[ch]; MasterAcousticDiag.maximum[ch] = hi[ch];
            if (lo[ch] == 0U || hi[ch] == 4095U) { MasterAcousticDiag.clipped_mask |= (uint8_t)(1U << ch); }
            lo[ch] = 4095U; hi[ch] = 0U;
        }
        latest.tick = tick; blocks = 0U; MasterAcousticDiag.valid = 1U; MasterAcousticDiag.windows++;
    }
}
uint8_t MasterAcoustic_GetSample(uint32_t now, MasterAcousticSample *output)
{
    if (!output || !MasterAcousticDiag.valid || (uint32_t)(now - latest.tick) >= 300U) { return 0U; }
    *output = latest; return 1U;
}
