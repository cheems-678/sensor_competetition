#include <assert.h>
#include <stdio.h>
#include "master_acoustic.h"
#include "max4466_adc.h"
static uint32_t seq, stamp, starts;
static uint16_t amplitude;
static uint16_t baseline = 1000U;
static uint8_t status, start_ok = 1U;
uint8_t Max4466Adc_Start(void) { starts++; return start_ok; }
void Max4466Adc_Stop(void) {}
uint8_t Max4466Adc_Read(uint16_t *p, uint32_t *s, uint32_t *t)
{
    unsigned i;
    for (i = 0U; i < MAX4466_BLOCK_WORDS; ++i) { p[i] = baseline + (i / 5U % 2U && amplitude ? amplitude + i % 5U : 0U); }
    *s = seq; *t = stamp; return status;
}
static void feed(uint32_t time, unsigned count, uint16_t amp)
{
    unsigned i; amplitude = amp; status = 1U;
    for (i = 0U; i < count; ++i) { stamp = time + 14U * i; seq++; MasterAcoustic_Process(stamp); }
}
int main(void)
{
    MasterAcousticSample s;
    MasterAcoustic_Init(0U); feed(14U, 6U, 200U);
    assert(!MasterAcoustic_GetSample(99U, &s));
    feed(100U, 3U, 500U); assert(!MasterAcoustic_GetSample(130U, &s));
    feed(142U, 1U, 500U); assert(MasterAcoustic_GetSample(142U, &s));
    assert(s.peak_to_peak[0] == 500U && s.peak_to_peak[4] == 504U && s.tick == 142U);
    feed(156U, 4U, 10U); assert(MasterAcoustic_GetSample(198U, &s) && s.peak_to_peak[0] == 10U);
    feed(212U, 4U, 0U); assert(MasterAcoustic_GetSample(254U, &s) && s.peak_to_peak[0] == 0U);
    assert(s.peak_to_peak[1]==0U && s.peak_to_peak[2]==0U && s.peak_to_peak[3]==0U && s.peak_to_peak[4]==0U);
    assert(MasterAcoustic_GetSample(553U, &s)); assert(!MasterAcoustic_GetSample(554U, &s));
    status = 2U; MasterAcoustic_Process(260U); assert(!MasterAcoustic_GetSample(260U, &s));
    feed(270U, 3U, 10U); seq++; feed(312U, 1U, 900U);
    assert(!MasterAcoustic_GetSample(312U, &s));
    feed(326U, 3U, 10U); assert(MasterAcoustic_GetSample(354U, &s) && s.peak_to_peak[0] == 900U);
    status = 3U; MasterAcoustic_Process(360U); assert(!MasterAcoustic_GetSample(360U, &s));
    MasterAcoustic_Process(1359U); assert(starts == 1U);
    start_ok = 0U; MasterAcoustic_Process(1360U); assert(starts == 2U && !MasterAcousticDiag.running);
    start_ok = 1U; MasterAcoustic_Process(2360U); assert(starts == 3U && MasterAcousticDiag.running);
    feed(2460U, 4U, 5U); assert(MasterAcoustic_GetSample(2502U, &s));
    status = 0U; MasterAcoustic_Process(2802U); assert(!MasterAcousticDiag.running);
    MasterAcoustic_Init(0xFFFFFF00U); seq = 0xFFFFFFFEU;
    feed(0xFFFFFF80U, 4U, 4U); assert(MasterAcoustic_GetSample(10U, &s));
    assert(!MasterAcoustic_GetSample(0x1D6U, &s));
    MasterAcoustic_Init(0U); baseline=0U; feed(100U,4U,4091U);
    assert(MasterAcoustic_GetSample(142U,&s) && s.peak_to_peak[4]==4095U && MasterAcousticDiag.clipped_mask==31U);
    baseline=4096U; feed(156U,1U,0U); assert(!MasterAcoustic_GetSample(156U,&s));
    puts("MAX4466 latest-window/zero/five-channel/gap/fault/retry/age/wrap passed"); return 0;
}
