#include <assert.h>
#include <stdio.h>
#include "slave_hcsr04.h"
#include "hcsr04_timer.h"

static Hcsr04Result result;
static uint16_t pulse;
static unsigned starts, inits, cancels;
static uint8_t init_ok = 1U, start_ok = 1U;
uint8_t Hcsr04Timer_Init(void) { inits++; return init_ok; }
uint8_t Hcsr04Timer_Start(void) { starts++; result = HCSR04_PENDING; return start_ok; }
Hcsr04Result Hcsr04Timer_Poll(uint16_t *out) { *out = pulse; return result; }
void Hcsr04Timer_Cancel(void) { cancels++; result = HCSR04_PENDING; }
static SlaveHcsr04Sample finish(uint32_t tick, uint16_t width)
{
    SlaveHcsr04Sample sample;
    pulse = width; result = HCSR04_COMPLETE;
    SlaveHcsr04_Process(tick + 5U);
    assert(SlaveHcsr04_GetSample(tick + 5U, &sample));
    assert(sample.tick == tick && sample.pulse_us == width);
    return sample;
}
int main(void)
{
    SlaveHcsr04Sample sample;
    unsigned previous;
    SlaveHcsr04_Init(0U); assert(inits == 1U && starts == 1U && SlaveHcsr04Diag.pending);
    assert(!SlaveHcsr04_GetSample(0U, &sample));
    sample = finish(0U, 584U); assert(sample.raw_mm == 100U && sample.distance_mm == 100U);
    assert(sample.sequence == 1U && sample.filter_count == 1U);
    SlaveHcsr04_Process(99U); assert(starts == 1U);
    SlaveHcsr04_Process(100U); sample = finish(100U, 1749U); assert(sample.raw_mm == 300U && sample.distance_mm == 200U);
    SlaveHcsr04_Process(200U); sample = finish(200U, 1166U); assert(sample.distance_mm == 200U);
    SlaveHcsr04_Process(300U); sample = finish(300U, 2916U); assert(sample.raw_mm == 500U);
    SlaveHcsr04_Process(400U); sample = finish(400U, 1750U);
    assert(sample.distance_mm == 300U && sample.filter_count == 5U);
    previous = starts; assert(SlaveHcsr04_GetSample(410U, &sample)); assert(starts == previous && sample.tick == 400U);
    SlaveHcsr04_Process(500U); result = HCSR04_TIMEOUT; SlaveHcsr04_Process(510U);
    assert(!SlaveHcsr04_GetSample(510U, &sample) && SlaveHcsr04Diag.filter_count == 0U);
    assert(SlaveHcsr04Diag.last_error == SLAVE_HCSR04_ERROR_TIMEOUT);
    SlaveHcsr04_Process(600U); sample = finish(600U, 2916U);
    assert(sample.distance_mm == 500U && sample.filter_count == 1U);
    SlaveHcsr04_Process(700U); pulse = 285U; result = HCSR04_COMPLETE; SlaveHcsr04_Process(705U);
    assert(!SlaveHcsr04_GetSample(705U, &sample) && SlaveHcsr04Diag.last_error == SLAVE_HCSR04_ERROR_RANGE);
    SlaveHcsr04_Process(800U); pulse = 65535U; result = HCSR04_COMPLETE; SlaveHcsr04_Process(805U);
    assert(!SlaveHcsr04Diag.valid && SlaveHcsr04Diag.raw_mm > 500U);
    SlaveHcsr04_Process(900U); result = HCSR04_ECHO_HIGH; SlaveHcsr04_Process(901U);
    assert(SlaveHcsr04Diag.last_error == SLAVE_HCSR04_ERROR_ECHO_HIGH);
    SlaveHcsr04_Process(1000U); result = HCSR04_OVERCAPTURE; SlaveHcsr04_Process(1001U);
    assert(SlaveHcsr04Diag.last_error == SLAVE_HCSR04_ERROR_CAPTURE);
    SlaveHcsr04_Process(1100U); SlaveHcsr04_Process(1120U);
    assert(cancels && !SlaveHcsr04Diag.valid);
    SlaveHcsr04_Process(1200U); sample = finish(1200U, 1749U);
    assert(SlaveHcsr04_GetSample(3199U, &sample)); assert(!SlaveHcsr04_GetSample(3200U, &sample));
    SlaveHcsr04_Process(3300U); assert(!SlaveHcsr04Diag.valid && SlaveHcsr04Diag.filter_count == 0U);
    sample = finish(3300U, 584U); assert(sample.distance_mm == 100U && sample.filter_count == 1U);
    SlaveHcsr04_Init(0xFFFFFFF0U); sample = finish(0xFFFFFFF0U, 1749U);
    assert(SlaveHcsr04_GetSample(0x7BFU, &sample)); assert(!SlaveHcsr04_GetSample(0x7C0U, &sample));
    SlaveHcsr04_Process(0x54U); sample = finish(0x54U, 1166U); assert(sample.tick == 0x54U);
    /* A capture read after the source deadline is invalid, not refreshed. */
    SlaveHcsr04_Process(0xB8U); pulse = 1749U; result = HCSR04_COMPLETE; SlaveHcsr04_Process(0x888U);
    assert(!SlaveHcsr04Diag.valid);
    init_ok = 0U; previous = inits; SlaveHcsr04_Init(3000U);
    assert(inits == previous + 1U && SlaveHcsr04Diag.last_error == SLAVE_HCSR04_ERROR_INIT);
    SlaveHcsr04_Process(3999U); assert(inits == previous + 1U);
    init_ok = 1U; SlaveHcsr04_Process(4000U); assert(SlaveHcsr04Diag.pending);
    (void)finish(4000U, 1749U);
    start_ok = 0U; SlaveHcsr04_Process(4100U);
    assert(!SlaveHcsr04Diag.valid && SlaveHcsr04Diag.last_error == SLAVE_HCSR04_ERROR_START);
    assert(!SlaveHcsr04_GetSample(4100U, 0));
    start_ok = 1U; SlaveHcsr04_Init(5000U);
    sample = finish(5000U, 292U); assert(sample.raw_mm == 50U && sample.distance_mm == 50U);
    SlaveHcsr04_Process(5100U); sample = finish(5100U, 297U); assert(sample.raw_mm == 51U);
    SlaveHcsr04_Process(5200U); pulse = 285U; result = HCSR04_COMPLETE; SlaveHcsr04_Process(5205U);
    assert(!SlaveHcsr04_GetSample(5205U, &sample) && SlaveHcsr04Diag.last_error == SLAVE_HCSR04_ERROR_RANGE);
    puts("PASS: HC-SR04 service, range, median, errors, stale capture, source age and tick rollover");
    return 0;
}
