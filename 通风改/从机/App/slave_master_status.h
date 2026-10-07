#ifndef SLAVE_MASTER_STATUS_H
#define SLAVE_MASTER_STATUS_H
#include <stdint.h>
#include "../../monitor_status_wire.h"

typedef struct {
    uint32_t rx_age_ms, bme_age_ms, uptime_ms, sample_seq;
    uint32_t pressure_pa;
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint8_t online, bme_valid, rain, dark, light_on, fan_pwm[4];
    uint8_t acoustic_valid;
    uint16_t acoustic_peak_to_peak[5];
    uint32_t acoustic_age_ms, acoustic_seq;
} SlaveMasterStatus;
typedef struct {
    uint32_t accepted_count, invalid_count, duplicate_count, received_tick;
    uint32_t last_flow_id;
} SlaveMasterStatusDiagnostics;
extern SlaveMasterStatusDiagnostics SlaveMasterStatusDiag;
void SlaveMasterStatus_Init(void);
uint8_t SlaveMasterStatus_Accept(uint16_t flow_id, const uint8_t *payload,
                               uint8_t size, uint32_t now_ms);
void SlaveMasterStatus_Get(uint32_t now_ms, SlaveMasterStatus *out);
#endif
