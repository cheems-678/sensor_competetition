#include "master_bme280.h"

#include <string.h>

#include "BME280.h"
#include "master_config.h"

#define BME_STATE_RETRY      (0U)
#define BME_STATE_IDLE       (1U)
#define BME_STATE_MEASURING  (2U)

volatile MasterBme280Diagnostics MasterBme280Diag;
static BME280_HandleTypeDef g_sensor;
static MasterBme280Sample g_sample;
static uint32_t g_next_action_ms;
static uint32_t g_conversion_start_ms;

static void MasterBme280_Fail(BME280_Status status, uint32_t now_ms)
{
    MasterBme280Diag.last_error = (uint32_t)status;
    MasterBme280Diag.failure_count++;
    MasterBme280Diag.completed_count++;
    MasterBme280Diag.sample_valid = 0U;
    MasterBme280Diag.calibration_valid = 0U;
    MasterBme280Diag.state = BME_STATE_RETRY;
    g_next_action_ms = now_ms + MASTER_BME_RETRY_MS;
}

void MasterBme280_Init(uint32_t now_ms)
{
    memset((void *)&MasterBme280Diag, 0, sizeof(MasterBme280Diag));
    memset(&g_sensor, 0, sizeof(g_sensor));
    memset(&g_sample, 0, sizeof(g_sample));
    MasterBme280Diag.state = BME_STATE_RETRY;
    g_next_action_ms = now_ms;
    g_conversion_start_ms = now_ms;
}

void MasterBme280_RequestSample(uint32_t now_ms)
{
    if (MasterBme280Diag.state != BME_STATE_MEASURING)
    {
        g_next_action_ms = now_ms;
    }
}

uint8_t MasterBme280_GetSample(uint32_t now_ms, MasterBme280Sample *sample)
{
    if ((sample == NULL) || (MasterBme280Diag.sample_valid == 0U) ||
        ((uint32_t)(now_ms - MasterBme280Diag.sample_tick) >=
         MASTER_BME_CACHE_FRESH_MS))
    {
        return 0U;
    }
    *sample = g_sample;
    return 1U;
}

void MasterBme280_Process(uint32_t now_ms)
{
    BME280_Status status;
    BME280_Data data;

    if ((int32_t)(now_ms - g_next_action_ms) < 0)
    {
        return;
    }

    if (MasterBme280Diag.state == BME_STATE_RETRY)
    {
        MasterBme280Diag.init_attempt_count++;
        status = BME280_Init(&g_sensor);
        MasterBme280Diag.address_7bit = g_sensor.i2c_address;
        MasterBme280Diag.chip_id = g_sensor.chip_id;
        if (status == BME280_OK)
        {
            status = BME280_Config(&g_sensor, BME280_OVERSAMPLING_X1,
                                  BME280_OVERSAMPLING_X1,
                                  BME280_OVERSAMPLING_X1, BME280_FILTER_OFF);
        }
        now_ms = HAL_GetTick();
        if (status != BME280_OK)
        {
            MasterBme280_Fail(status, now_ms);
            return;
        }
        MasterBme280Diag.init_success_count++;
        MasterBme280Diag.calibration_valid = 1U;
        MasterBme280Diag.state = BME_STATE_IDLE;
    }

    if (MasterBme280Diag.state == BME_STATE_IDLE)
    {
        MasterBme280Diag.sample_attempt_count++;
        status = BME280_TriggerMeasurement(&g_sensor);
        now_ms = HAL_GetTick();
        if (status != BME280_OK)
        {
            MasterBme280_Fail(status, now_ms);
            return;
        }
        g_conversion_start_ms = now_ms;
        g_next_action_ms = now_ms + BME280_GetMeasurementDelayMs(&g_sensor);
        MasterBme280Diag.state = BME_STATE_MEASURING;
        return;
    }

    status = BME280_ReadMeasurement(&g_sensor, &data);
    now_ms = HAL_GetTick();
    if (status == BME280_ERROR_NOT_READY)
    {
        if ((uint32_t)(now_ms - g_conversion_start_ms) <
            MASTER_BME_CONVERSION_TIMEOUT_MS)
        {
            g_next_action_ms = now_ms + 1U;
            return;
        }
        status = BME280_ERROR_TIMEOUT;
    }
    if ((status == BME280_OK) &&
        ((data.temperature_x100 < -4000) || (data.temperature_x100 > 8500) ||
         (data.humidity_x1024 > 102400U) ||
         (data.pressure_pa < 30000U) || (data.pressure_pa > 110000U)))
    {
        status = BME280_ERROR_NOT_READY;
    }
    if (status != BME280_OK)
    {
        MasterBme280_Fail(status, now_ms);
        return;
    }

    g_sample.temperature_x10 = (int16_t)((data.temperature_x100 >= 0) ?
        (data.temperature_x100 + 5) / 10 : (data.temperature_x100 - 5) / 10);
    g_sample.humidity_x10 = (uint16_t)(
        (data.humidity_x1024 * 10U + 512U) / 1024U);
    g_sample.pressure_pa = data.pressure_pa;
    MasterBme280Diag.temperature_x100 = data.temperature_x100;
    MasterBme280Diag.humidity_x1024 = data.humidity_x1024;
    MasterBme280Diag.pressure_pa = data.pressure_pa;
    MasterBme280Diag.sample_tick = now_ms;
    MasterBme280Diag.sample_valid = 1U;
    MasterBme280Diag.last_error = BME280_OK;
    MasterBme280Diag.sample_success_count++;
    MasterBme280Diag.completed_count++;
    MasterBme280Diag.state = BME_STATE_IDLE;
    g_next_action_ms = now_ms + MASTER_BME_SAMPLE_INTERVAL_MS;
}
