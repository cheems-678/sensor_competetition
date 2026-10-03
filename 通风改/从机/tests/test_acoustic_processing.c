#include "acoustic_processing.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t checks;
static uint32_t failures;

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        checks++;                                                            \
        if (!(condition))                                                    \
        {                                                                    \
            failures++;                                                      \
            if (failures <= 20U)                                             \
            {                                                                \
                printf("FAIL line %u: %s\n", (unsigned int)__LINE__,           \
                       #condition);                                          \
            }                                                                \
        }                                                                    \
    } while (0)

static void encode_sample(int32_t sample, uint8_t tail, uint16_t words[2])
{
    uint32_t raw = (((uint32_t)sample & 0x3FFFFU) << 14) | tail;
    words[0] = (uint16_t)(raw >> 16);
    words[1] = (uint16_t)raw;
}

static void encode_frame(int32_t left, int32_t right, uint8_t left_tail,
                         uint8_t right_tail, uint16_t words[4])
{
    encode_sample(left, left_tail, &words[0]);
    encode_sample(right, right_tail, &words[2]);
}

static void check_zero_accumulator(const AcousticAccumulator *accumulator)
{
    const unsigned char *bytes = (const unsigned char *)accumulator;
    size_t index;
    for (index = 0U; index < sizeof(*accumulator); index++)
    {
        CHECK(bytes[index] == 0U);
    }
}

/* Exercise every signed 18-bit value, including unused bits set to all ones. */
static void test_exhaustive_decode(void)
{
    int32_t expected;
    for (expected = -131072L; expected <= 131071L; expected++)
    {
        uint16_t words[2];
        encode_sample(expected, 0U, words);
        CHECK(Acoustic_DecodeSlot(words[0], words[1]) == expected);
        CHECK(Acoustic_DecodeSlot(words[0], (uint16_t)(words[1] | 0x3FFFU)) ==
              expected);
    }
    CHECK(Acoustic_DecodeSlot(0x8000U, 0x0000U) == -131072L);
    CHECK(Acoustic_DecodeSlot(0x7FFFU, 0xC000U) == 131071L);
    CHECK(Acoustic_DecodeSlot(0xFFFFU, 0xC000U) == -1L);
    CHECK(Acoustic_DecodeSlot(0x0000U, 0x4000U) == 1L);
}

static void test_dc_and_result_timing(void)
{
    AcousticAccumulator accumulator;
    AcousticResult result;
    AcousticResult before;
    uint16_t words[4];
    uint32_t frame;

    memset(&accumulator, 0xA5, sizeof(accumulator));
    Acoustic_Reset(&accumulator);
    check_zero_accumulator(&accumulator);
    memset(&result, 0x5A, sizeof(result));
    before = result;
    encode_frame(12345L, -777L, 0U, 0U, words);
    for (frame = 0U; frame < ACOUSTIC_WINDOW_FRAMES; frame++)
    {
        uint8_t ready = Acoustic_PushFrame(&accumulator, words, &result);
        CHECK(ready == (uint8_t)(frame == ACOUSTIC_WINDOW_FRAMES - 1U));
        if (ready == 0U)
        {
            CHECK(memcmp(&result, &before, sizeof(result)) == 0);
            CHECK(accumulator.frame_count == frame + 1U);
        }
    }
    CHECK(result.frame_count == ACOUSTIC_WINDOW_FRAMES);
    CHECK(result.rms[0] == 0U && result.rms[1] == 0U);
    CHECK(result.mean[0] == 12345L && result.mean[1] == -777L);
    CHECK(result.peak[0] == 12345U && result.peak[1] == 777U);
    CHECK(result.last_sample[0] == 12345L && result.last_sample[1] == -777L);
    CHECK(result.nonzero[0] == ACOUSTIC_WINDOW_FRAMES);
    CHECK(result.nonzero[1] == ACOUSTIC_WINDOW_FRAMES);
    CHECK(result.padding_errors[0] == 0U && result.padding_errors[1] == 0U);
    check_zero_accumulator(&accumulator);
}

static void test_independent_stereo_and_consecutive_windows(void)
{
    AcousticAccumulator accumulator;
    AcousticResult result;
    uint16_t words[4];
    uint32_t frame;

    Acoustic_Reset(&accumulator);
    for (frame = 0U; frame < ACOUSTIC_WINDOW_FRAMES; frame++)
    {
        int32_t sign = ((frame & 1U) == 0U) ? 1L : -1L;
        encode_frame(sign * 1000L, -sign * 2000L, (uint8_t)frame,
                     (uint8_t)(255U - (frame & 255U)), words);
        CHECK(Acoustic_PushFrame(&accumulator, words, &result) ==
              (uint8_t)(frame == ACOUSTIC_WINDOW_FRAMES - 1U));
    }
    CHECK(result.rms[0] == 1000U && result.rms[1] == 2000U);
    CHECK(result.mean[0] == 0L && result.mean[1] == 0L);
    CHECK(result.peak[0] == 1000U && result.peak[1] == 2000U);
    CHECK(result.last_sample[0] == -1000L && result.last_sample[1] == 2000L);
    CHECK(result.nonzero[0] == ACOUSTIC_WINDOW_FRAMES);
    CHECK(result.nonzero[1] == ACOUSTIC_WINDOW_FRAMES);
    CHECK(result.padding_errors[0] == 0U && result.padding_errors[1] == 0U);

    /* No explicit reset: a fresh silent window must not inherit either channel. */
    encode_frame(0L, 0L, 0xFFU, 0xAAU, words);
    for (frame = 0U; frame < ACOUSTIC_WINDOW_FRAMES; frame++)
    {
        CHECK(Acoustic_PushFrame(&accumulator, words, &result) ==
              (uint8_t)(frame == ACOUSTIC_WINDOW_FRAMES - 1U));
    }
    CHECK(result.rms[0] == 0U && result.rms[1] == 0U);
    CHECK(result.peak[0] == 0U && result.peak[1] == 0U);
    CHECK(result.mean[0] == 0L && result.mean[1] == 0L);
    CHECK(result.last_sample[0] == 0L && result.last_sample[1] == 0L);
    CHECK(result.nonzero[0] == 0U && result.nonzero[1] == 0U);
    CHECK(result.padding_errors[0] == 0U && result.padding_errors[1] == 0U);
    check_zero_accumulator(&accumulator);

    /* A left-channel-only stimulus proves that right RMS is not shared. */
    for (frame = 0U; frame < ACOUSTIC_WINDOW_FRAMES; frame++)
    {
        encode_frame(((frame & 1U) == 0U) ? 313L : -313L, 0L, 0U, 0U, words);
        CHECK(Acoustic_PushFrame(&accumulator, words, &result) ==
              (uint8_t)(frame == ACOUSTIC_WINDOW_FRAMES - 1U));
    }
    CHECK(result.rms[0] == 313U && result.rms[1] == 0U);
    CHECK(result.peak[0] == 313U && result.peak[1] == 0U);
    CHECK(result.nonzero[0] == ACOUSTIC_WINDOW_FRAMES && result.nonzero[1] == 0U);
}

static void test_fractional_dc_mean(void)
{
    AcousticAccumulator accumulator;
    AcousticResult result;
    uint16_t words[4];
    uint32_t frame;

    Acoustic_Reset(&accumulator);
    /* Means are +/-1.5; true variance .75. Centering at a truncated mean
       would incorrectly produce RMS 1 instead of integer RMS 0. */
    for (frame = 0U; frame < ACOUSTIC_WINDOW_FRAMES; frame++)
    {
        int32_t value = ((frame & 3U) == 0U) ? 0L : 2L;
        encode_frame(value, -value, 0U, 0U, words);
        CHECK(Acoustic_PushFrame(&accumulator, words, &result) ==
              (uint8_t)(frame == ACOUSTIC_WINDOW_FRAMES - 1U));
    }
    CHECK(result.rms[0] == 0U && result.rms[1] == 0U);
    CHECK(result.mean[0] == 1L && result.mean[1] == -1L);
    CHECK(result.peak[0] == 2U && result.peak[1] == 2U);
    CHECK(result.nonzero[0] == (ACOUSTIC_WINDOW_FRAMES * 3U) / 4U);
    CHECK(result.nonzero[1] == (ACOUSTIC_WINDOW_FRAMES * 3U) / 4U);

    /* Variance 2.25 tests flooring a nonintegral RMS (1.5). */
    for (frame = 0U; frame < ACOUSTIC_WINDOW_FRAMES; frame++)
    {
        encode_frame(((frame & 1U) == 0U) ? 0L : 3L,
                     ((frame & 1U) == 0U) ? -5L : -2L, 0U, 0U, words);
        CHECK(Acoustic_PushFrame(&accumulator, words, &result) ==
              (uint8_t)(frame == ACOUSTIC_WINDOW_FRAMES - 1U));
    }
    CHECK(result.rms[0] == 1U && result.rms[1] == 1U);
    CHECK(result.mean[0] == 1L && result.mean[1] == -3L);
}

static void test_full_scale(void)
{
    AcousticAccumulator accumulator;
    AcousticResult result;
    uint16_t words[4];
    uint32_t frame;

    Acoustic_Reset(&accumulator);
    for (frame = 0U; frame < ACOUSTIC_WINDOW_FRAMES; frame++)
    {
        encode_frame(((frame & 1U) == 0U) ? 131071L : -131072L,
                     ((frame & 1U) == 0U) ? -131072L : 131071L, 0U, 0U, words);
        CHECK(Acoustic_PushFrame(&accumulator, words, &result) ==
              (uint8_t)(frame == ACOUSTIC_WINDOW_FRAMES - 1U));
    }
    CHECK(result.rms[0] == 131071U && result.rms[1] == 131071U);
    CHECK(result.peak[0] == 131072U && result.peak[1] == 131072U);
    CHECK(result.mean[0] == 0L && result.mean[1] == 0L);
    CHECK(result.last_sample[0] == -131072L && result.last_sample[1] == 131071L);
    CHECK(result.nonzero[0] == ACOUSTIC_WINDOW_FRAMES);
    CHECK(result.nonzero[1] == ACOUSTIC_WINDOW_FRAMES);

    /* Max constant magnitudes stress sum^2 without allowing DC through RMS. */
    encode_frame(-131072L, 131071L, 0U, 0U, words);
    for (frame = 0U; frame < ACOUSTIC_WINDOW_FRAMES; frame++)
    {
        CHECK(Acoustic_PushFrame(&accumulator, words, &result) ==
              (uint8_t)(frame == ACOUSTIC_WINDOW_FRAMES - 1U));
    }
    CHECK(result.rms[0] == 0U && result.rms[1] == 0U);
    CHECK(result.peak[0] == 131072U && result.peak[1] == 131071U);
    CHECK(result.mean[0] == -131072L && result.mean[1] == 131071L);
}

static void test_padding_and_tristate_tail(void)
{
    AcousticAccumulator accumulator;
    AcousticResult result;
    uint16_t words[4];
    uint32_t frame;

    Acoustic_Reset(&accumulator);
    for (frame = 0U; frame < ACOUSTIC_WINDOW_FRAMES; frame++)
    {
        /* Cycle all 256 possible tri-state tail patterns independently. */
        encode_frame(17L, -23L, (uint8_t)frame,
                     (uint8_t)((frame * 73U) + 29U), words);
        if (frame < 6U)
        {
            words[1] |= (uint16_t)(1U << (8U + frame));
        }
        if ((frame & 1U) == 0U)
        {
            words[3] |= 0x3F00U;
        }
        CHECK(Acoustic_PushFrame(&accumulator, words, &result) ==
              (uint8_t)(frame == ACOUSTIC_WINDOW_FRAMES - 1U));
    }
    CHECK(result.padding_errors[0] == 6U);
    CHECK(result.padding_errors[1] == ACOUSTIC_WINDOW_FRAMES / 2U);
    CHECK(result.rms[0] == 0U && result.rms[1] == 0U);
    CHECK(result.mean[0] == 17L && result.mean[1] == -23L);
    CHECK(result.last_sample[0] == 17L && result.last_sample[1] == -23L);
    CHECK(result.peak[0] == 17U && result.peak[1] == 23U);
}

static void test_explicit_reset_and_null_arguments(void)
{
    AcousticAccumulator accumulator;
    AcousticAccumulator before_accumulator;
    AcousticResult result;
    AcousticResult before_result;
    uint16_t words[4];
    uint32_t frame;

    Acoustic_Reset(&accumulator);
    encode_frame(1000L, -2000L, 0U, 0U, words);
    memset(&result, 0xA5, sizeof(result));
    for (frame = 0U; frame < 100U; frame++)
    {
        CHECK(Acoustic_PushFrame(&accumulator, words, &result) == 0U);
    }
    before_accumulator = accumulator;
    before_result = result;
    Acoustic_Reset(0);
    CHECK(Acoustic_PushFrame(0, words, &result) == 0U);
    CHECK(Acoustic_PushFrame(&accumulator, 0, &result) == 0U);
    CHECK(Acoustic_PushFrame(&accumulator, words, 0) == 0U);
    CHECK(Acoustic_PushFrame(0, 0, 0) == 0U);
    CHECK(memcmp(&accumulator, &before_accumulator, sizeof(accumulator)) == 0);
    CHECK(memcmp(&result, &before_result, sizeof(result)) == 0);

    /* A capture gap resets even a partially accumulated nonzero window. */
    Acoustic_Reset(&accumulator);
    check_zero_accumulator(&accumulator);
    encode_frame(0L, 0L, 0U, 0U, words);
    for (frame = 0U; frame < ACOUSTIC_WINDOW_FRAMES; frame++)
    {
        CHECK(Acoustic_PushFrame(&accumulator, words, &result) ==
              (uint8_t)(frame == ACOUSTIC_WINDOW_FRAMES - 1U));
    }
    CHECK(result.frame_count == ACOUSTIC_WINDOW_FRAMES);
    CHECK(result.rms[0] == 0U && result.rms[1] == 0U);
    CHECK(result.mean[0] == 0L && result.mean[1] == 0L);
    CHECK(result.peak[0] == 0U && result.peak[1] == 0U);
    CHECK(result.nonzero[0] == 0U && result.nonzero[1] == 0U);
}

int main(void)
{
    test_exhaustive_decode();
    test_dc_and_result_timing();
    test_independent_stereo_and_consecutive_windows();
    test_fractional_dc_mean();
    test_full_scale();
    test_padding_and_tristate_tail();
    test_explicit_reset_and_null_arguments();
    printf("Acoustic processing: %lu checks, %lu failures\n",
           (unsigned long)checks, (unsigned long)failures);
    return (failures == 0U) ? 0 : 1;
}
