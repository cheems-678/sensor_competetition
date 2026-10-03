#include "acoustic_processing.h"

#include <string.h>

int32_t Acoustic_DecodeSlot(uint16_t high, uint16_t low)
{
    uint32_t raw = ((uint32_t)high << 16) | low;
    int32_t sample = (int32_t)(raw >> 14);
    if ((sample & 0x20000L) != 0)
    {
        sample -= 0x40000L;
    }
    return sample;
}

static uint32_t Acoustic_IntegerSqrt(uint64_t value)
{
    uint64_t root = 0U;
    uint64_t bit = (uint64_t)1U << 62;
    while (bit > value)
    {
        bit >>= 2;
    }
    while (bit != 0U)
    {
        if (value >= root + bit)
        {
            value -= root + bit;
            root = (root >> 1) + bit;
        }
        else
        {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)root;
}

void Acoustic_Reset(AcousticAccumulator *accumulator)
{
    if (accumulator != 0)
    {
        memset(accumulator, 0, sizeof(*accumulator));
    }
}

uint8_t Acoustic_PushFrame(AcousticAccumulator *accumulator,
                          const uint16_t words[4], AcousticResult *result)
{
    uint32_t channel;
    int32_t sample[2];
    if ((accumulator == 0) || (words == 0) || (result == 0))
    {
        return 0U;
    }

    for (channel = 0U; channel < 2U; channel++)
    {
        uint32_t magnitude;
        sample[channel] = Acoustic_DecodeSlot(words[channel * 2U],
                                             words[channel * 2U + 1U]);
        magnitude = (uint32_t)((sample[channel] < 0) ?
                              -sample[channel] : sample[channel]);
        accumulator->sum[channel] += sample[channel];
        accumulator->sum_squared[channel] +=
            (uint64_t)((int64_t)sample[channel] * sample[channel]);
        if (magnitude > accumulator->peak[channel])
        {
            accumulator->peak[channel] = magnitude;
        }
        if (sample[channel] != 0)
        {
            accumulator->nonzero[channel]++;
        }
        /* Six unused bits of the 24-bit word must be zero, not the tri-state tail. */
        if ((words[channel * 2U + 1U] & 0x3F00U) != 0U)
        {
            accumulator->padding_errors[channel]++;
        }
    }
    accumulator->frame_count++;
    if (accumulator->frame_count < ACOUSTIC_WINDOW_FRAMES)
    {
        return 0U;
    }

    for (channel = 0U; channel < 2U; channel++)
    {
        /* Max numerator < 2^57 for 2048 signed 18-bit samples; no float/DC bias. */
        uint64_t sum_square = (uint64_t)(accumulator->sum[channel] *
                                       accumulator->sum[channel]);
        uint64_t variance =
            ((uint64_t)ACOUSTIC_WINDOW_FRAMES * accumulator->sum_squared[channel] -
             sum_square) / ((uint64_t)ACOUSTIC_WINDOW_FRAMES * ACOUSTIC_WINDOW_FRAMES);
        result->rms[channel] = Acoustic_IntegerSqrt(variance);
        result->peak[channel] = accumulator->peak[channel];
        result->mean[channel] =
            (int32_t)(accumulator->sum[channel] / ACOUSTIC_WINDOW_FRAMES);
        result->last_sample[channel] = sample[channel];
        result->nonzero[channel] = accumulator->nonzero[channel];
        result->padding_errors[channel] = accumulator->padding_errors[channel];
    }
    result->frame_count = accumulator->frame_count;
    Acoustic_Reset(accumulator);
    return 1U;
}
