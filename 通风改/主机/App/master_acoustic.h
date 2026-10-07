#ifndef MASTER_ACOUSTIC_H
#define MASTER_ACOUSTIC_H
#include <stdint.h>
#define MASTER_ACOUSTIC_MAX_AGE_MS 300U
typedef struct { uint16_t peak_to_peak[5]; uint32_t tick; } MasterAcousticSample;
typedef struct {
    uint32_t windows, gaps, failures, restarts, last_block_tick;
    uint16_t minimum[5], maximum[5];
    uint8_t running, valid, clipped_mask;
} MasterAcousticDiagnostics;
extern MasterAcousticDiagnostics MasterAcousticDiag;
void MasterAcoustic_Init(uint32_t now);
void MasterAcoustic_Process(uint32_t now);
uint8_t MasterAcoustic_GetSample(uint32_t now, MasterAcousticSample *output);
#endif
