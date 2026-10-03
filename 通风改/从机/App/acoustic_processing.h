#ifndef ACOUSTIC_PROCESSING_H
#define ACOUSTIC_PROCESSING_H

#include <stdint.h>

#define ACOUSTIC_WINDOW_FRAMES 2048U

typedef struct {
    uint32_t rms[2];
    uint32_t peak[2];
    int32_t mean[2];
    int32_t last_sample[2];
    uint32_t nonzero[2];
    uint32_t padding_errors[2];
    uint32_t frame_count;
} AcousticResult;

typedef struct {
    int64_t sum[2];
    uint64_t sum_squared[2];
    uint32_t peak[2];
    uint32_t nonzero[2];
    uint32_t padding_errors[2];
    uint32_t frame_count;
} AcousticAccumulator;

/* SPH0645: MSB aligned 24-bit slot, 18 meaningful bits; ignore trailing 8 bits. */
int32_t Acoustic_DecodeSlot(uint16_t high, uint16_t low);
void Acoustic_Reset(AcousticAccumulator *accumulator);
/* Returns 1 only at a complete window; caller must reset on a capture gap. */
uint8_t Acoustic_PushFrame(AcousticAccumulator *accumulator,
                          const uint16_t words[4], AcousticResult *result);

#endif
