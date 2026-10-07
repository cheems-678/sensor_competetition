#ifndef SLAVE_HCSR04_H
#define SLAVE_HCSR04_H
#include <stdint.h>

#define SLAVE_HCSR04_PERIOD_MS 100U
#define SLAVE_HCSR04_MAX_AGE_MS 2000U
/* Fixed sound speed at about 20 C; calibrate against a ruler before field use. */
#ifndef SLAVE_HCSR04_SOUND_MM_S
#define SLAVE_HCSR04_SOUND_MM_S 343000UL
#endif
#ifndef SLAVE_HCSR04_OFFSET_MM
#define SLAVE_HCSR04_OFFSET_MM 0
#endif
#ifndef SLAVE_HCSR04_MIN_MM
#define SLAVE_HCSR04_MIN_MM 50U
#endif
#ifndef SLAVE_HCSR04_MAX_MM
#define SLAVE_HCSR04_MAX_MM 500U
#endif
typedef struct {
    uint32_t tick, sequence;
    uint16_t raw_mm, distance_mm, pulse_us;
    uint8_t filter_count;
} SlaveHcsr04Sample;
typedef enum {
    SLAVE_HCSR04_ERROR_NONE, SLAVE_HCSR04_ERROR_INIT, SLAVE_HCSR04_ERROR_START,
    SLAVE_HCSR04_ERROR_ECHO_HIGH, SLAVE_HCSR04_ERROR_TIMEOUT,
    SLAVE_HCSR04_ERROR_CAPTURE, SLAVE_HCSR04_ERROR_RANGE
} SlaveHcsr04Error;
typedef struct {
    uint32_t initialized, pending, valid, last_error, error_count;
    uint32_t trigger_count, sample_count, sample_tick, pulse_us, raw_mm, distance_mm, filter_count;
} SlaveHcsr04Diagnostics;
extern volatile SlaveHcsr04Diagnostics SlaveHcsr04Diag;
void SlaveHcsr04_Init(uint32_t now_ms);
void SlaveHcsr04_Process(uint32_t now_ms);
uint8_t SlaveHcsr04_GetSample(uint32_t now_ms, SlaveHcsr04Sample *sample);
#endif
