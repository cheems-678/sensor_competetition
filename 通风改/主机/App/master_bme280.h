#ifndef MASTER_BME280_H
#define MASTER_BME280_H

#include <stdint.h>

typedef struct
{
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint32_t pressure_pa;
} MasterBme280Sample;

typedef struct
{
    uint32_t init_attempt_count;
    uint32_t init_success_count;
    uint32_t sample_attempt_count;
    uint32_t sample_success_count;
    uint32_t failure_count;
    uint32_t completed_count;
    uint32_t last_error;
    uint32_t address_7bit;
    uint32_t chip_id;
    uint32_t calibration_valid;
    uint32_t sample_valid;
    uint32_t sample_tick;
    uint32_t state;
    int32_t temperature_x100;
    uint32_t humidity_x1024;
    uint32_t pressure_pa;
} MasterBme280Diagnostics;

extern volatile MasterBme280Diagnostics MasterBme280Diag;

void MasterBme280_Init(uint32_t now_ms);
void MasterBme280_Process(uint32_t now_ms);
/* An in-flight conversion may satisfy a fresh request when it completes. */
void MasterBme280_RequestSample(uint32_t now_ms);
uint8_t MasterBme280_GetSample(uint32_t now_ms, MasterBme280Sample *sample);

#endif /* MASTER_BME280_H */
