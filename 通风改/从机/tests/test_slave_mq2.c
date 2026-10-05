#include <assert.h>
#include <stdio.h>
#include "slave_mq2.h"
#include "mq2_adc.h"

static unsigned inits, starts, stops;
static uint8_t init_ok, start_ok, poll_result;
static uint16_t value;
uint8_t Mq2Adc_Init(void) { inits++; return init_ok; }
uint8_t Mq2Adc_Start(void) { starts++; return start_ok; }
uint8_t Mq2Adc_Poll(uint16_t *out) { *out = value; return poll_result; }
void Mq2Adc_Stop(void) { stops++; }

static void reset(uint32_t tick)
{
    inits = starts = stops = 0U;
    init_ok = start_ok = poll_result = 1U;
    value = 2048U;
    SlaveMq2_Init(tick);
}
int main(void)
{
    SlaveMq2Sample s;
    reset(0U);
    assert(inits == 1U && starts == 1U && SlaveMq2Diag.pending);
    assert(!SlaveMq2_GetSample(0U, &s));
    SlaveMq2_Process(1U);
    assert(SlaveMq2_GetSample(1U, &s));
    assert(s.raw == 2048U && s.pa7_mv == 1650U && s.ao_mv == 3301U);
    assert(s.tick == 0U && s.sequence == 1U);
    assert(SlaveMq2_GetSample(1999U, &s));
    assert(!SlaveMq2_GetSample(2000U, &s));
    assert(!SlaveMq2_GetSample(1U, 0));
    assert(SlaveMq2Diag.sample_count == 1U && starts == 1U);
    SlaveMq2_Process(99U); assert(starts == 1U);
    value = 0U; SlaveMq2_Process(100U); SlaveMq2_Process(101U);
    assert(SlaveMq2_GetSample(101U, &s) && s.raw == 0U && s.ao_mv == 0U);
    value = 4095U; SlaveMq2_Process(200U); SlaveMq2_Process(201U);
    assert(SlaveMq2_GetSample(201U, &s) && s.pa7_mv == 3300U && s.ao_mv == 6600U);
    /* LoRa TX may delay a COMPLETE result; source age must survive. */
    SlaveMq2_Process(300U); SlaveMq2_Process(800U);
    assert(SlaveMq2_GetSample(800U, &s) && s.tick == 300U);
    assert(starts == 5U); /* next conversion starts once, no catch-up loop */
    SlaveMq2_Process(2800U);
    assert(!SlaveMq2_GetSample(2800U, &s) && SlaveMq2Diag.last_error == SLAVE_MQ2_ERROR_TIMEOUT);

    reset(0U); poll_result = 0U;
    SlaveMq2_Process(9U); assert(SlaveMq2Diag.pending);
    SlaveMq2_Process(10U);
    assert(SlaveMq2Diag.last_error == SLAVE_MQ2_ERROR_TIMEOUT && stops == 1U);
    SlaveMq2_Process(1009U); assert(inits == 1U);
    poll_result = 1U; SlaveMq2_Process(1010U); SlaveMq2_Process(1011U);
    assert(inits == 2U && SlaveMq2_GetSample(1011U, &s));
    assert(SlaveMq2Diag.last_error == 0U && SlaveMq2Diag.error_count == 1U);

    reset(0U); SlaveMq2_Process(1U);
    start_ok = 0U; SlaveMq2_Process(100U);
    assert(SlaveMq2Diag.last_error == SLAVE_MQ2_ERROR_START && !SlaveMq2_GetSample(101U, &s));
    init_ok = 0U; SlaveMq2_Process(1100U);
    assert(SlaveMq2Diag.last_error == SLAVE_MQ2_ERROR_INIT);
    init_ok = start_ok = 1U; SlaveMq2_Process(2100U); SlaveMq2_Process(2101U);
    assert(SlaveMq2_GetSample(2101U, &s));
    SlaveMq2_Process(2200U); poll_result = 2U; SlaveMq2_Process(2201U);
    assert(SlaveMq2Diag.last_error == SLAVE_MQ2_ERROR_CONVERSION);
    assert(!SlaveMq2_GetSample(2201U, &s));
    poll_result = 1U; value = 4096U; SlaveMq2_Process(3201U); SlaveMq2_Process(3202U);
    assert(SlaveMq2Diag.last_error == SLAVE_MQ2_ERROR_CONVERSION);

    reset(0xFFFFFFF0U); SlaveMq2_Process(0xFFFFFFF1U);
    assert(SlaveMq2_GetSample(0U, &s) && s.tick == 0xFFFFFFF0U);
    assert(SlaveMq2_GetSample(1983U, &s) && !SlaveMq2_GetSample(1984U, &s));
    poll_result = 0U; SlaveMq2_Process(84U); SlaveMq2_Process(94U);
    assert(SlaveMq2Diag.last_error == SLAVE_MQ2_ERROR_TIMEOUT);
    puts("MQ2 service: sampling, source age, failures/recovery, voltage and wrap passed");
    return 0;
}
