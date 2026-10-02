#include "slave_bme280.h"
#include "bme280.h"
#include "i2c.h"
#include <string.h>

#define STATE_RETRY 0U
#define STATE_IDLE 1U
#define STATE_MEASURING 2U
#define SAMPLE_INTERVAL_MS 1000U
#define CACHE_FRESH_MS 2000U
#define CONVERSION_TIMEOUT_MS 50U
#define ERROR_RANGE 4U

volatile SlaveBme280Diagnostics SlaveBme280Diag;
static BME280_HandleTypeDef g_sensor;
static SlaveBme280Sample g_sample;
static uint32_t g_next_tick, g_conversion_tick;

static void Fail(uint32_t error, uint32_t now_ms)
{
    SlaveBme280Diag.last_error = error;
    SlaveBme280Diag.i2c_error = hi2c1.ErrorCode;
    SlaveBme280Diag.failure_count++;
    SlaveBme280Diag.completed_count++;
    SlaveBme280Diag.sample_valid = 0U;
    SlaveBme280Diag.calibration_valid = 0U;
    SlaveBme280Diag.state = STATE_RETRY;
    g_next_tick = now_ms + SAMPLE_INTERVAL_MS;
}

void SlaveBme280_Init(uint32_t now_ms)
{
    memset((void *)&SlaveBme280Diag, 0, sizeof(SlaveBme280Diag));
    memset(&g_sensor, 0, sizeof(g_sensor));
    memset(&g_sample, 0, sizeof(g_sample));
    g_next_tick = g_conversion_tick = now_ms;
}

void SlaveBme280_RequestSample(uint32_t now_ms)
{
    if (SlaveBme280Diag.state != STATE_MEASURING) { g_next_tick = now_ms; }
}

uint8_t SlaveBme280_GetSample(uint32_t now_ms, SlaveBme280Sample *sample)
{
    if (sample == NULL || !SlaveBme280Diag.sample_valid ||
        (uint32_t)(now_ms - SlaveBme280Diag.sample_tick) >= CACHE_FRESH_MS) { return 0U; }
    *sample = g_sample;
    return 1U;
}

void SlaveBme280_Process(uint32_t now_ms)
{
    HAL_StatusTypeDef status;
    int32_t temperature;
    uint32_t pressure, humidity;
    uint8_t address;
    if ((int32_t)(now_ms - g_next_tick) < 0) { return; }
    if (SlaveBme280Diag.state == STATE_RETRY)
    {
        SlaveBme280Diag.init_attempt_count++;
        SlaveBme280Diag.chip_id = 0U;
        status = SlaveI2c_InitAndRecover();
        if (status != HAL_OK) { Fail(status, HAL_GetTick()); return; }
        for (address = BME280_I2C_ADDR_PRIM; address <= BME280_I2C_ADDR_SEC; address++)
        {
            status = BME280_Init(&g_sensor, &hi2c1, address);
            if (g_sensor.chip_id != 0U || SlaveBme280Diag.chip_id == 0U)
            {
                SlaveBme280Diag.address_7bit = address;
                SlaveBme280Diag.chip_id = g_sensor.chip_id;
            }
            if (status == HAL_OK) { break; }
        }
        if (status == HAL_OK)
        {
            status = BME280_Config(&g_sensor, BME280_OVERSAMPLING_X1,
                BME280_OVERSAMPLING_X1, BME280_OVERSAMPLING_X1, BME280_FILTER_OFF);
        }
        if (status != HAL_OK) { Fail(status, HAL_GetTick()); return; }
        SlaveBme280Diag.init_success_count++;
        SlaveBme280Diag.calibration_valid = 1U;
        SlaveBme280Diag.state = STATE_IDLE;
    }
    if (SlaveBme280Diag.state == STATE_IDLE)
    {
        SlaveBme280Diag.sample_attempt_count++;
        status = BME280_TriggerMeasurement(&g_sensor);
        now_ms = HAL_GetTick();
        if (status != HAL_OK) { Fail(status, now_ms); return; }
        g_conversion_tick = now_ms;
        g_next_tick = now_ms + 10U;
        SlaveBme280Diag.state = STATE_MEASURING;
        return;
    }
    status = BME280_ReadMeasurement(&g_sensor, &temperature, &pressure, &humidity);
    now_ms = HAL_GetTick();
    if (status == HAL_BUSY)
    {
        if ((uint32_t)(now_ms - g_conversion_tick) < CONVERSION_TIMEOUT_MS)
        { g_next_tick = now_ms + 1U; return; }
        status = HAL_TIMEOUT;
    }
    if (status != HAL_OK) { Fail(status, now_ms); return; }
    if (temperature < -4000 || temperature > 8500 || humidity > 102400U ||
        pressure < 30000U || pressure > 110000U)
    { Fail(ERROR_RANGE, now_ms); return; }
    g_sample.temperature_x10 = (int16_t)((temperature >= 0) ?
        (temperature + 5) / 10 : (temperature - 5) / 10);
    g_sample.humidity_x10 = (uint16_t)((humidity * 10U + 512U) / 1024U);
    g_sample.pressure_pa = pressure;
    SlaveBme280Diag.temperature_x100 = temperature;
    SlaveBme280Diag.humidity_x1024 = humidity;
    SlaveBme280Diag.pressure_pa = pressure;
    SlaveBme280Diag.sample_tick = now_ms;
    SlaveBme280Diag.sample_valid = 1U;
    SlaveBme280Diag.last_error = HAL_OK;
    SlaveBme280Diag.i2c_error = 0U;
    SlaveBme280Diag.sample_success_count++;
    SlaveBme280Diag.completed_count++;
    SlaveBme280Diag.state = STATE_IDLE;
    g_next_tick = now_ms + SAMPLE_INTERVAL_MS;
}
