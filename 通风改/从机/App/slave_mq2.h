#ifndef SLAVE_MQ2_H
#define SLAVE_MQ2_H
#include <stdint.h>

#ifndef SLAVE_MQ2_VDDA_MV
#define SLAVE_MQ2_VDDA_MV 3300U
#endif
/* AO--10k--PA7--10k--GND. Both AO and PA7 values are estimates. */
#ifndef SLAVE_MQ2_DIVIDER_NUM
#define SLAVE_MQ2_DIVIDER_NUM 2U
#endif
#ifndef SLAVE_MQ2_DIVIDER_DEN
#define SLAVE_MQ2_DIVIDER_DEN 1U
#endif
#define SLAVE_MQ2_PERIOD_MS 100U
#define SLAVE_MQ2_MAX_AGE_MS 2000U

typedef struct { uint16_t raw; uint32_t pa7_mv, ao_mv, tick, sequence; } SlaveMq2Sample;
typedef enum {
    SLAVE_MQ2_ERROR_NONE, SLAVE_MQ2_ERROR_INIT, SLAVE_MQ2_ERROR_START,
    SLAVE_MQ2_ERROR_CONVERSION, SLAVE_MQ2_ERROR_TIMEOUT
} SlaveMq2Error;
typedef struct {
    uint32_t initialized, pending, valid, last_error, error_count;
    uint32_t sample_count, sample_tick, raw, pa7_mv, ao_mv;
} SlaveMq2Diagnostics;
extern volatile SlaveMq2Diagnostics SlaveMq2Diag;
void SlaveMq2_Init(uint32_t now_ms);
void SlaveMq2_Process(uint32_t now_ms);
/* Success describes an ADC sample, not sensor presence or an alarm. */
uint8_t SlaveMq2_GetSample(uint32_t now_ms, SlaveMq2Sample *sample);
#endif
