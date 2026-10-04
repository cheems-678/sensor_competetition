#ifndef SLAVE_ACOUSTIC_H
#define SLAVE_ACOUSTIC_H

#include <stdint.h>

enum {
    SLAVE_ACOUSTIC_WARMUP = 1,
    SLAVE_ACOUSTIC_CAPTURING = 2,
    SLAVE_ACOUSTIC_RETRY = 3
};

typedef struct {
    uint32_t state, restart_count, last_error;
    uint32_t processed_blocks, discontinuities, warmup_blocks;
    uint32_t window_count, window_valid, window_tick, processed_frames;
    uint32_t rms_left, rms_right, peak_left, peak_right;
    int32_t mean_left, mean_right, last_left, last_right;
    uint32_t nonzero_left, nonzero_right, padding_errors_left, padding_errors_right;
    uint32_t total_padding_errors_left, total_padding_errors_right;
    uint32_t validity_epoch;
    uint16_t raw_words[16];
} SlaveAcousticDiagnostics;

typedef struct {
    uint32_t rms_left, rms_right;
    uint32_t window_tick, validity_epoch;
} SlaveAcousticSnapshot;

extern volatile SlaveAcousticDiagnostics SlaveAcousticDiag;
void SlaveAcoustic_Init(uint32_t now_ms);
void SlaveAcoustic_Process(uint32_t now_ms);
/* Non-consuming stereo RMS from the latest complete, valid short window.
   Tick/epoch must travel with cached responses, never renewed by a query. */
uint8_t SlaveAcoustic_GetLatest(uint32_t now_ms, SlaveAcousticSnapshot *snapshot);
uint8_t SlaveAcoustic_IsSnapshotValid(uint32_t now_ms,
                                    const SlaveAcousticSnapshot *snapshot);

#endif
