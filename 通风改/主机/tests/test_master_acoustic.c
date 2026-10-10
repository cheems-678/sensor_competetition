#include <assert.h>
#include <stdio.h>
#include "master_acoustic.h"
#include "max4466_adc.h"
static Max4466AdcWindow window;
static unsigned starts, stops;
static uint8_t status, start_ok = 1U;
uint8_t Max4466Adc_Start(void) { starts++; return start_ok; }
void Max4466Adc_Stop(void) { stops++; }
uint8_t Max4466Adc_Read(Max4466AdcWindow *p) { *p = window; return status; }
static void feed(uint32_t now, uint32_t source, uint16_t low, uint16_t high)
{
    unsigned ch;
    status = 1U;
    for (ch = 0U; ch < 5U; ch++) {
        window.minimum[ch] = low; window.maximum[ch] = high;
        window.mean[ch] = (uint16_t)(((uint32_t)low + high) / 2U);
    }
    window.samples = 512U; window.tick = source; window.sequence++;
    MasterAcoustic_Process(now);
}
int main(void)
{
    unsigned i;
    MasterAcousticSample sample;
    MasterAcoustic_Init(0U); feed(43U, 43U, 1000U, 1200U);
    assert(!MasterAcoustic_GetSample(99U, &sample));
    feed(143U, 143U, 1800U, 2300U);
    assert(MasterAcoustic_GetSample(143U, &sample) && sample.peak_to_peak[0] == 500U);
    for (i = 1U; i < 5U; i++) {
        assert(sample.peak_to_peak[i] == 500U && MasterAcousticDiag.minimum[i] == 1800U &&
               MasterAcousticDiag.maximum[i] == 2300U && MasterAcousticDiag.mean[i] == 2050U);
    }
    feed(193U, 193U, 2048U, 2058U);
    assert(MasterAcoustic_GetSample(193U, &sample) && sample.peak_to_peak[0] == 10U);
    feed(250U, 243U, 4095U, 4095U);
    assert(MasterAcoustic_GetSample(250U, &sample) && sample.peak_to_peak[0] == 0U);
    assert(MasterAcousticDiag.mean[0] == 4095U && MasterAcousticDiag.clipped_mask == 31U);
    assert(MasterAcousticDiag.samples == 512U);
    assert(MasterAcoustic_GetSample(542U, &sample) && !MasterAcoustic_GetSample(543U, &sample));
    feed(600U, 293U, 0U, 4095U); /* Completed DMA read late: do not publish as fresh. */
    assert(!MasterAcoustic_GetSample(600U, &sample));
    /* Independent extrema and mask bits, not a single value copied five times. */
    for (i = 0U; i < 5U; i++) {
        feed(610U + i, 610U + i, 1000U, 1000U);
        window.minimum[i] = 0U; window.maximum[i] = 100U + (uint16_t)i;
        window.mean[i] = 50U;
        MasterAcoustic_Process(610U + i);
        assert(MasterAcoustic_GetSample(610U + i, &sample));
        assert(MasterAcousticDiag.clipped_mask == (1U << i));
        {
            unsigned ch;
            for (ch = 0U; ch < 5U; ch++) {
                assert(sample.peak_to_peak[ch] == (ch == i ? 100U + i : 0U));
            }
        }
    }
    feed(643U, 643U, 0U, 4095U);
    assert(MasterAcoustic_GetSample(643U, &sample) && sample.peak_to_peak[0] == 4095U);
    status = 3U; MasterAcoustic_Process(650U);
    assert(!MasterAcousticDiag.running && !MasterAcoustic_GetSample(650U, &sample) && stops == 1U);
    MasterAcoustic_Process(1649U); assert(starts == 1U);
    start_ok = 0U; MasterAcoustic_Process(1650U); assert(starts == 2U && !MasterAcousticDiag.running);
    start_ok = 1U; MasterAcoustic_Process(2650U); assert(starts == 3U && MasterAcousticDiag.running);
    feed(2793U, 2793U, 2000U, 2010U);
    assert(MasterAcoustic_GetSample(2793U, &sample));
    status = 0U; MasterAcoustic_Process(3093U);
    assert(!MasterAcoustic_GetSample(3093U, &sample));
    MasterAcoustic_Init(0xFFFFFF00U);
    feed(0xFFFFFF80U, 0xFFFFFF80U, 2048U, 2048U);
    assert(MasterAcoustic_GetSample(10U, &sample) && sample.peak_to_peak[0] == 0U);
    assert(!MasterAcoustic_GetSample(172U, &sample));
    assert(!MasterAcoustic_GetSample(10U, 0));
    puts("Five-channel p2p/isolation/raw-diagnostics/warmup/age/fault/retry/wrap passed");
    return 0;
}
