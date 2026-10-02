#ifndef SLAVE_BME280_H
#define SLAVE_BME280_H
#include <stdint.h>

typedef struct {
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint32_t pressure_pa;
} SlaveBme280Sample;

typedef struct {
    uint32_t init_attempt_count, init_success_count;
    uint32_t sample_attempt_count, sample_success_count, failure_count, completed_count;
    uint32_t last_error, address_7bit, chip_id, calibration_valid;
    uint32_t sample_valid, sample_tick, state, i2c_error;
    int32_t temperature_x100;
    uint32_t humidity_x1024, pressure_pa;
} SlaveBme280Diagnostics;

extern volatile SlaveBme280Diagnostics SlaveBme280Diag;
void SlaveBme280_Init(uint32_t now_ms);
void SlaveBme280_Process(uint32_t now_ms);
void SlaveBme280_RequestSample(uint32_t now_ms);
uint8_t SlaveBme280_GetSample(uint32_t now_ms, SlaveBme280Sample *sample);
#endif
