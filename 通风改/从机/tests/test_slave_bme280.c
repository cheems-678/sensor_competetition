#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bme280.h"
#include "i2c.h"
#include "slave_bme280.h"
#include "slave_protocol_runtime.h"
#include "slave_acoustic.h"
#include "slave_servo_test.h"
#include "sg90_test_pwm.h"

I2C_TypeDef fake_i2c1;
static uint32_t tick;
static uint8_t bank[256], present, device_address, tx_fail;
static int fail_reg;
static unsigned sent_count, bus_edges;
static unsigned tx_attempts, pwm_start_calls, pwm_update_calls, pwm_stop_calls;
static uint8_t pwm_start_success, pwm_update_success;
static uint16_t pwm_pulse;
static uint16_t gpio_levels, stuck_pins;
static uint8_t sent[141];
static uint16_t sent_len;
static uint8_t audio_valid;
static uint32_t audio_tick, audio_epoch, audio_left, audio_right;

uint8_t Sg90TestPwm_Start(uint16_t pulse_us)
{
    assert(pulse_us == SG90_TEST_CENTER_US);
    pwm_start_calls++;
    pwm_pulse = pulse_us;
    return pwm_start_success;
}
uint8_t Sg90TestPwm_SetPulse(uint16_t pulse_us)
{
    assert(pulse_us >= SG90_TEST_MIN_US && pulse_us <= SG90_TEST_MAX_US);
    pwm_update_calls++;
    if (!pwm_update_success) { return 0U; }
    pwm_pulse = pulse_us;
    return 1U;
}
void Sg90TestPwm_Stop(void) { pwm_stop_calls++; pwm_pulse = 0U; }

/* Capture itself is exercised by test_slave_acoustic; protocol owns snapshots. */
uint8_t SlaveAcoustic_GetRecentMax(uint32_t now, SlaveAcousticSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    if (!audio_valid || (int32_t)(now - audio_tick) >= 300) { return 0U; }
    snapshot->rms_left = audio_left;
    snapshot->rms_right = audio_right;
    snapshot->window_tick = audio_tick;
    snapshot->validity_epoch = audio_epoch;
    return 1U;
}
uint8_t SlaveAcoustic_IsSnapshotValid(uint32_t now,
                                    const SlaveAcousticSnapshot *snapshot)
{
    return (audio_valid && snapshot->validity_epoch == audio_epoch &&
            (int32_t)(now - audio_tick) < 300 &&
            (int32_t)(now - snapshot->window_tick) < 300) ? 1U : 0U;
}

uint32_t HAL_GetTick(void) { return tick; }
void HAL_Delay(uint32_t delay) { tick += delay; }
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *pins) { (void)port; (void)pins; }
void HAL_GPIO_DeInit(void *port, uint16_t pins) { (void)port; (void)pins; }
void HAL_GPIO_WritePin(void *port, uint16_t pins, GPIO_PinState state)
{
    uint16_t before = gpio_levels;
    (void)port;
    if (state == GPIO_PIN_SET) { gpio_levels |= pins; }
    else { gpio_levels &= (uint16_t)~pins; }
    if (before != gpio_levels) { bus_edges++; }
}
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin)
{
    (void)port;
    if (stuck_pins & pin) { tick++; return GPIO_PIN_RESET; }
    return (gpio_levels & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *handle)
{
    assert(handle->Init.ClockSpeed == 100000U);
    handle->Instance->CR1 |= I2C_CR1_PE;
    if (bus_edges >= 4U) { handle->Instance->SR2 &= ~I2C_SR2_BUSY; }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *handle)
{ handle->ErrorCode = 0U; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *handle, uint16_t address,
    uint16_t reg, uint16_t reg_size, uint8_t *data, uint16_t len, uint32_t timeout)
{
    assert(timeout == 10U && reg_size == I2C_MEMADD_SIZE_8BIT);
    if (!present || address != (uint16_t)(device_address << 1U) || fail_reg == (int)reg)
    { handle->ErrorCode = 4U; return HAL_ERROR; }
    assert(reg + len <= 256U);
    memcpy(data, &bank[reg], len);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *handle, uint16_t address,
    uint16_t reg, uint16_t reg_size, uint8_t *data, uint16_t len, uint32_t timeout)
{
    assert(timeout == 10U && reg_size == I2C_MEMADD_SIZE_8BIT && len == 1U);
    if (!present || address != (uint16_t)(device_address << 1U) || fail_reg == (int)reg)
    { handle->ErrorCode = 4U; return HAL_ERROR; }
    bank[reg] = *data;
    return HAL_OK;
}
uint8_t LORA_SendData(const uint8_t *data, uint16_t len)
{
    tx_attempts++;
    if (tx_fail) { return 0U; }
    assert(len <= sizeof(sent));
    memcpy(sent, data, len);
    sent_len = len;
    sent_count++;
    return 1U;
}
static void put16(unsigned reg, int value)
{ bank[reg] = (uint8_t)value; bank[reg + 1U] = (uint8_t)((unsigned)value >> 8U); }
static void raw20(unsigned reg, unsigned value)
{ bank[reg] = (uint8_t)(value >> 12U); bank[reg+1U] = (uint8_t)(value >> 4U); bank[reg+2U] = (uint8_t)(value << 4U); }
static void reset(uint32_t start)
{
    static const int calibration[] = {27504,26435,-1000,36477,-10685,3024,2855,140,-7,15500,-14600,6000};
    unsigned i;
    tick = start;
    memset(bank, 0, sizeof(bank));
    memset(&fake_i2c1, 0, sizeof(fake_i2c1));
    memset(&hi2c1, 0, sizeof(hi2c1));
    present = 1U; device_address = 0x76U; fail_reg = -1;
    tx_fail = 0U; sent_count = bus_edges = 0U;
    tx_attempts = pwm_start_calls = pwm_update_calls = pwm_stop_calls = 0U;
    pwm_start_success = pwm_update_success = 1U;
    pwm_pulse = 0U;
    audio_valid = 0U;
    audio_tick = audio_epoch = audio_left = audio_right = 0U;
    gpio_levels = GPIO_PIN_6 | GPIO_PIN_7; stuck_pins = 0U;
    bank[0xD0] = 0x60U;
    for (i = 0U; i < 12U; i++) { put16(0x88U + i*2U, calibration[i]); }
    bank[0xA1] = 75U; put16(0xE1U, 362);
    bank[0xE4] = 20U; bank[0xE5] = 0x2EU; bank[0xE6] = 3U; bank[0xE7] = 30U;
    raw20(0xF7U, 415148U); raw20(0xFAU, 519888U);
    bank[0xFD] = (uint8_t)(35000U >> 8U); bank[0xFE] = (uint8_t)35000U;
    SlaveRuntime_Init(1U);
    SlaveServoTest_InitManual(HAL_GetTick());
}
static void process(uint32_t now)
{
    tick = now;
    /* Match the business loop: service the local stop timer before radio work. */
    SlaveServoTest_Process(tick);
    SlaveRuntime_Process(tick);
}
static uint16_t crc16(const uint8_t *data, unsigned len)
{
    uint16_t crc = 0xFFFFU;
    unsigned i, bit;
    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) { crc = (crc & 1U) ? (uint16_t)((crc >> 1U)^0xA001U) : (uint16_t)(crc >> 1U); }
    }
    return crc;
}
static void request(uint16_t flow, uint8_t force, uint8_t wrong_group, uint8_t corrupt)
{
    uint8_t frame[] = {0xAA,0x55,4,1,2,1,3,1,0,0,1,0,0,0};
    unsigned i; uint16_t crc;
    frame[8] = (uint8_t)flow; frame[9] = (uint8_t)(flow >> 8U); frame[11] = force;
    if (wrong_group) { frame[7] = 2U; }
    crc = crc16(&frame[2], 10U);
    frame[12] = (uint8_t)crc; frame[13] = (uint8_t)(crc >> 8U);
    if (corrupt) { frame[13] ^= 1U; }
    for (i = 0U; i < sizeof(frame); i++) { SlaveRuntime_PushRxByteFromIsr(frame[i]); }
}
static void window_frame(uint16_t flow, uint8_t source_role, uint8_t source_group,
                         uint8_t destination_role, uint8_t destination_group,
                         uint8_t length, uint8_t id, uint8_t action, uint8_t corrupt)
{
    uint8_t frame[16] = {0xAA,0x55,4,0x11,0,0,0,0,0,0,0,0,0,0,0,0};
    unsigned i;
    uint16_t crc;
    assert(length <= 3U);
    frame[4] = source_role; frame[5] = source_group;
    frame[6] = destination_role; frame[7] = destination_group;
    frame[8] = (uint8_t)flow; frame[9] = (uint8_t)(flow >> 8U);
    frame[10] = length; frame[11] = id; frame[12] = action;
    crc = crc16(&frame[2], 9U + length);
    frame[11U + length] = (uint8_t)crc;
    frame[12U + length] = (uint8_t)(crc >> 8U);
    if (corrupt) { frame[12U + length] ^= 1U; }
    for (i = 0U; i < 13U + length; i++)
    { SlaveRuntime_PushRxByteFromIsr(frame[i]); }
}
static void window_request(uint16_t flow, uint8_t action)
{ window_frame(flow, 2U, 1U, 3U, 1U, 2U, 1U, action, 0U); }
static void assert_window_ack(uint16_t flow, uint8_t status)
{
    assert(sent_len == 15U && sent[0] == 0xAAU && sent[1] == 0x55U);
    assert(sent[2] == 4U && sent[3] == 0x20U);
    assert(sent[4] == 3U && sent[5] == 1U && sent[6] == 2U && sent[7] == 1U);
    assert((uint16_t)(sent[8] | ((uint16_t)sent[9] << 8U)) == flow);
    assert(sent[10] == 2U && sent[11] == 0x11U && sent[12] == status);
    assert(crc16(&sent[2], 11U) == (uint16_t)(sent[13] | ((uint16_t)sent[14] << 8U)));
}
static void assert_reply(uint16_t flow, uint8_t valid)
{
    unsigned index;
    assert(sent_len == 39U && sent[2] == 4U && sent[3] == 2U);
    assert(sent[4] == 3U && sent[5] == 1U && sent[6] == 2U && sent[7] == 1U);
    assert((uint16_t)(sent[8] | ((uint16_t)sent[9] << 8U)) == flow);
    assert(sent[10] == 26U && sent[11] == 8U);
    assert(crc16(&sent[2], 35U) == (uint16_t)(sent[37] | ((uint16_t)sent[38] << 8U)));
    for (index = 20U; index < 29U; index++) { assert(sent[index] == 0xFFU); }
    if (valid)
    {
        assert(sent[12] == 251U && sent[13] == 0U); /* 25.08C -> 25.1C */
        assert(sent[16] == 0x2DU && sent[17] == 0x89U && sent[18] == 1U && sent[19] == 0U);
    }
    else
    {
        static const uint8_t invalid[] = {0,0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        assert(memcmp(&sent[12], invalid, 8U) == 0);
    }
}
static uint32_t read32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
            ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U));
}
static void assert_audio(uint32_t left, uint32_t right)
{
    assert(read32(&sent[29]) == left && read32(&sent[33]) == right);
}
static void test_sensor_vector_cache_and_address(void)
{
    SlaveBme280Sample sample;
    reset(0U); device_address = 0x77U;
    process(0U);
    assert(SlaveBme280Diag.address_7bit == 0x77U && SlaveBme280Diag.chip_id == 0x60U);
    assert(SlaveBme280Diag.calibration_valid && SlaveBme280Diag.state == 2U);
    assert(bank[0xF2] == 1U && bank[0xF5] == 0U && bank[0xF4] == 0x25U);
    process(19U); assert(!SlaveBme280_GetSample(tick, &sample));
    process(20U); assert(SlaveBme280_GetSample(tick, &sample));
    assert(SlaveBme280Diag.temperature_x100 == 2508 && sample.temperature_x10 == 251);
    assert(sample.pressure_pa == 100653U && sample.humidity_x10 <= 1000U);
    assert(SlaveBme280Diag.humidity_x1024 >= 77060U &&
           SlaveBme280Diag.humidity_x1024 <= 77100U); /* Independent float formula: 77079.1 */
    assert(sample.humidity_x10 == 753U);
    assert(SlaveBme280_GetSample(2019U, &sample));
    assert(!SlaveBme280_GetSample(2020U, &sample));
    assert(!SlaveBme280_GetSample(20U, NULL));
}
static void test_init_failures_and_reconnect(void)
{
    static const int regs[] = {0xE0,0x88,0xE1,0xF2,0xF5,0xF4};
    unsigned i;
    SlaveBme280Sample sample;
    for (i = 0U; i < sizeof(regs)/sizeof(regs[0]); i++)
    {
        reset(0U); fail_reg = regs[i]; process(0U);
        assert(!SlaveBme280Diag.sample_valid && SlaveBme280Diag.failure_count == 1U);
    }
    reset(0U); bank[0xD0] = 0x58U; process(0U);
    assert(SlaveBme280Diag.chip_id == 0x58U && !SlaveBme280Diag.calibration_valid);
    reset(0U); put16(0x8EU, 0); process(0U);
    assert(!SlaveBme280Diag.calibration_valid);
    reset(0U); present = 0U; process(0U); process(999U);
    assert(SlaveBme280Diag.init_attempt_count == 1U);
    present = 1U; process(1000U); process(1020U);
    assert(SlaveBme280_GetSample(tick, &sample));
    present = 0U; process(2020U); process(2030U);
    assert(!SlaveBme280_GetSample(tick, &sample));
    present = 1U; process(3030U); process(3050U);
    assert(SlaveBme280_GetSample(tick, &sample));
}
static void test_conversion_timeout_and_tick_wrap(void)
{
    reset(0U); process(0U); bank[0xF3] = 8U;
    process(20U); assert(!SlaveBme280Diag.completed_count);
    process(60U); assert(SlaveBme280Diag.last_error == HAL_TIMEOUT);
    assert(SlaveBme280Diag.completed_count == 1U);
    reset(0xFFFFFFF0U); process(tick); process(4U);
    assert(SlaveBme280Diag.sample_success_count == 1U);
    reset(0U); bank[0xF3] = 1U; process(0U);
    assert(SlaveBme280Diag.last_error == HAL_ERROR || SlaveBme280Diag.last_error == HAL_TIMEOUT);
    assert(tick < 100U); /* NVM update stuck is bounded. */
}
static void test_i2c_busy_and_line_fault(void)
{
    reset(0U); fake_i2c1.SR2 = I2C_SR2_BUSY;
    assert(SlaveI2c_InitAndRecover() == HAL_OK);
    assert(bus_edges >= 4U && !(fake_i2c1.SR2 & I2C_SR2_BUSY));
    reset(0U); stuck_pins = GPIO_PIN_6;
    assert(SlaveI2c_InitAndRecover() == HAL_TIMEOUT && tick < 100U);
    reset(0U); stuck_pins = GPIO_PIN_7;
    assert(SlaveI2c_InitAndRecover() == HAL_TIMEOUT && tick < 100U);
}
static void test_protocol_cached_force_duplicates_and_filters(void)
{
    unsigned before;
    reset(0U); process(0U); process(20U);
    request(0x1234U,0U,0U,0U); process(21U); process(70U);
    assert(sent_count == 0U); process(71U); assert_reply(0x1234U, 1U);
    request(0x1234U,0U,0U,0U); process(72U); process(122U);
    assert(SlaveRuntimeDiag.duplicate_request_count == 1U && sent_count == 2U);
    before = SlaveBme280Diag.completed_count;
    request(0x1234U,1U,0U,0U); process(123U); process(124U);
    assert(SlaveBme280Diag.completed_count == before);
    process(134U); process(184U); assert_reply(0x1234U,1U);
    assert(SlaveBme280Diag.completed_count == before + 1U && sent_count == 3U);
    request(5U,0U,1U,0U); process(185U);
    request(5U,0U,0U,1U); process(186U);
    assert(SlaveRuntimeDiag.ignored_message_count == 1U && SlaveRuntimeDiag.invalid_frame_count == 1U);
    process(2200U); request(0x1234U,1U,0U,0U); process(2201U);
    assert(SlaveRuntimeDiag.request_count == 3U); /* Cache expired. */
}
static void test_protocol_fault_and_tx_retry(void)
{
    reset(0U); present = 0U;
    request(6U,1U,0U,0U); process(0U); process(1U); process(51U);
    assert_reply(6U,0U);
    reset(0U); process(0U); process(20U); tx_fail = 1U;
    request(7U,0U,0U,0U); process(21U); process(71U); process(81U); process(91U);
    assert(SlaveRuntimeDiag.tx_failure_count == 1U && sent_count == 0U);
    tx_fail = 0U; request(7U,0U,0U,0U); process(92U); process(142U);
    assert_reply(7U,1U);
}

static void test_driver_signed_calibration_and_invalid_raw(void)
{
    BME280_HandleTypeDef sensor;
    int32_t temperature;
    uint32_t pressure, humidity;
    reset(0U);
    assert(BME280_Init(NULL, &hi2c1, 0x76U) == HAL_ERROR);
    assert(BME280_ReadRegs(NULL, 0U, bank, 1U) == HAL_ERROR);
    assert(BME280_TriggerMeasurement(NULL) == HAL_ERROR);
    assert(BME280_ReadMeasurement(NULL, &temperature, &pressure, &humidity) == HAL_ERROR);
    assert(SlaveI2c_InitAndRecover() == HAL_OK);
    /* H4=-2048, H5=-1 exercise 12-bit sign extension and wide compensation. */
    bank[0xE4] = 0x80U; bank[0xE5] = 0xF0U; bank[0xE6] = 0xFFU;
    assert(BME280_Init(&sensor, &hi2c1, 0x76U) == HAL_OK);
    assert(BME280_Config(&sensor, BME280_OVERSAMPLING_X1, BME280_OVERSAMPLING_X1,
        BME280_OVERSAMPLING_X1, BME280_FILTER_OFF) == HAL_OK);
    assert(sensor.dig_H4_msb == -128 && sensor.dig_H5_msb == -1);
    assert(BME280_ReadMeasurement(&sensor, &temperature, &pressure, &humidity) == HAL_OK);
    assert(temperature == 2508 && humidity == 102400U);
    raw20(0xFAU, 0x80000U);
    assert(BME280_ReadMeasurement(&sensor, &temperature, &pressure, &humidity) == HAL_ERROR);
    raw20(0xFAU, 519888U);
    bank[0xF3] = 8U;
    assert(BME280_ReadAll(&sensor, &temperature, &pressure, &humidity) == HAL_TIMEOUT);
    assert(tick < 100U);
}

static void test_duplicate_snapshot_expires_despite_new_sample(void)
{
    reset(0U); process(0U); process(20U);
    /* A snapshot is composed while the sensor is converting its next sample. */
    request(8U, 0U, 0U, 0U); process(1020U); process(1030U); process(1070U);
    assert_reply(8U, 1U);
    assert(SlaveBme280Diag.sample_tick == 1030U);
    /* The duplicate refers to the old sample at 20ms, not the current one. */
    request(8U, 0U, 0U, 0U); process(2021U); process(2071U);
    assert(SlaveBme280Diag.sample_valid == 1U);
    assert_reply(8U, 0U);
}

static void test_audio_wire_values_and_duplicate_snapshot_age(void)
{
    reset(0U); process(0U); process(20U);
    audio_valid = 1U; audio_tick = 20U; audio_epoch = 1U;
    audio_left = 0U; audio_right = 131071U;
    request(0x4567U, 0U, 0U, 0U); process(21U); process(71U);
    assert_reply(0x4567U, 1U); assert_audio(0U, 131071U);
    /* A newer window does not replace or renew the duplicate's old snapshot. */
    audio_tick = 100U; audio_left = 70000U; audio_right = 80000U;
    request(0x4567U, 0U, 0U, 0U); process(101U); process(151U);
    assert_reply(0x4567U, 1U); assert_audio(0U, 131071U);
    audio_tick = 270U;
    request(0x4567U, 0U, 0U, 0U); process(270U); process(320U);
    assert_reply(0x4567U, 1U);
    assert_audio(0xFFFFFFFFUL, 0xFFFFFFFFUL);
    /* Once invalidated, this duplicate cannot revive even with fresh capture. */
    audio_tick = 321U;
    request(0x4567U, 0U, 0U, 0U); process(321U); process(371U);
    assert_reply(0x4567U, 1U);
    assert_audio(0xFFFFFFFFUL, 0xFFFFFFFFUL);
    /* A new flow takes a genuinely new stereo snapshot, keeping wide values. */
    request(0x4568U, 0U, 0U, 0U); process(372U); process(422U);
    assert_reply(0x4568U, 1U); assert_audio(70000U, 80000U);
}

static void test_audio_fault_epoch_pending_expiry_and_tx_retry(void)
{
    reset(0U); process(0U); process(20U);
    audio_valid = 1U; audio_tick = 20U; audio_epoch = 1U;
    audio_left = 123U; audio_right = 456U;
    request(1U, 0U, 0U, 0U); process(21U);
    /* Capture fault/recovery between build and TX must invalidate old data. */
    audio_epoch++; audio_tick = 60U;
    process(71U); assert_reply(1U, 1U);
    assert_audio(0xFFFFFFFFUL, 0xFFFFFFFFUL);
    request(1U, 0U, 0U, 0U); process(72U); process(122U);
    assert_reply(1U, 1U); assert_audio(0xFFFFFFFFUL, 0xFFFFFFFFUL);

    /* The first attempt is fresh but fails; retry reaches the stale boundary. */
    reset(0U); process(0U); process(20U);
    audio_valid = 1U; audio_tick = 20U; audio_epoch = 2U;
    audio_left = 65536U; audio_right = 0U;
    request(2U, 0U, 0U, 0U); process(269U);
    tx_fail = 1U; process(319U);
    assert(sent_count == 0U);
    audio_tick = 320U; /* New capture cannot refresh the original snapshot. */
    tx_fail = 0U; process(329U);
    assert_reply(2U, 1U); assert_audio(0xFFFFFFFFUL, 0xFFFFFFFFUL);

    reset(UINT32_MAX - 100U); process(tick); process(UINT32_MAX - 80U);
    audio_valid = 1U; audio_tick = tick; audio_epoch = 3U;
    audio_left = 999U; audio_right = 100000U;
    request(3U, 0U, 0U, 0U); process(UINT32_MAX - 79U); process(UINT32_MAX - 29U);
    assert_reply(3U, 1U); assert_audio(999U, 100000U);
    audio_tick = 170U;
    request(3U, 0U, 0U, 0U); process(170U); process(220U);
    assert_reply(3U, 1U); assert_audio(0xFFFFFFFFUL, 0xFFFFFFFFUL);
}

static void test_force_waits_only_for_bme_audio_failure_is_independent(void)
{
    unsigned completed;
    reset(0U); process(0U); process(20U);
    completed = SlaveBme280Diag.completed_count;
    audio_valid = 1U; audio_tick = 20U; audio_epoch = 1U;
    audio_left = 0U; audio_right = 65536U;
    request(4U, 1U, 0U, 0U); process(21U); process(22U);
    assert(sent_count == 0U && SlaveBme280Diag.completed_count == completed);
    process(32U); process(82U);
    assert(SlaveBme280Diag.completed_count == completed + 1U);
    assert_reply(4U, 1U); assert_audio(0U, 65536U);

    reset(0U); present = 0U;
    audio_valid = 1U; audio_tick = 0U; audio_epoch = 1U;
    audio_left = 123U; audio_right = 456U;
    request(5U, 1U, 0U, 0U); process(0U); process(1U); process(51U);
    assert_reply(5U, 0U); assert_audio(123U, 456U);
}

static void test_window_strict_address_payload_and_crc(void)
{
    static const uint8_t invalid[][6] = {
        {1U,1U,3U,1U,2U,1U}, {2U,2U,3U,1U,2U,1U},
        {2U,1U,2U,1U,2U,1U}, {2U,1U,3U,2U,2U,1U},
        {2U,1U,3U,1U,0U,1U}, {2U,1U,3U,1U,1U,1U},
        {2U,1U,3U,1U,3U,1U}, {2U,1U,3U,1U,2U,0U},
        {2U,1U,3U,1U,2U,2U}
    };
    unsigned i;
    reset(0U); process(0U); process(20U);
    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); i++)
    {
        window_frame(1U, invalid[i][0], invalid[i][1], invalid[i][2],
                     invalid[i][3], invalid[i][4], invalid[i][5], 1U, 0U);
        process(21U + i);
    }
    window_request(2U, 2U); process(31U);
    window_frame(3U, 2U, 1U, 3U, 1U, 2U, 1U, 1U, 1U); process(32U);
    process(200U);
    assert(pwm_start_calls == 1U && pwm_update_calls == 0U && pwm_stop_calls == 0U);
    assert(pwm_pulse == 1500U && sent_count == 0U && tx_attempts == 0U);
    assert(SlaveRuntimeDiag.ignored_message_count == 10U);
    assert(SlaveRuntimeDiag.invalid_frame_count == 1U);
    assert(SlaveRuntimeDiag.request_count == 0U);
}

static void test_window_targets_preload_and_receive_silence(void)
{
    reset(0U); process(0U); process(20U);
    tick = 21U; window_request(0x1234U, 1U); process(21U);
    assert(pwm_pulse == 1700U && pwm_update_calls == 1U && sent_count == 0U);
    process(40U); process(41U); process(70U);
    assert(sent_count == 0U); process(71U);
    assert_window_ack(0x1234U, 0U);
    assert(sent_count == 1U && SlaveRuntimeDiag.reply_count == 1U);
    tick = 72U; window_request(0x1235U, 0U); process(72U);
    assert(pwm_pulse == 1300U && pwm_update_calls == 2U);
    process(121U); assert(sent_count == 1U); process(122U);
    assert_window_ack(0x1235U, 0U);
    SlaveServoTest_Process(200000U);
    assert(pwm_start_calls == 1U && pwm_update_calls == 3U &&
           pwm_pulse == SLAVE_SERVO_WINDOW_STOP_US);
    assert(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL);

    /* RX was already quiet: wait a full preload period after delayed parsing. */
    reset(0U); process(0U); process(20U);
    tick = 21U; window_request(4U, 1U); process(71U);
    assert(pwm_update_calls == 1U && tx_attempts == 0U);
    process(90U); assert(tx_attempts == 0U); process(91U);
    assert_window_ack(4U, 0U);
}

static void test_window_pending_duplicate_and_busy_never_execute(void)
{
    reset(0U); process(0U); process(20U);
    tick = 21U; window_request(8U, 1U); process(21U);
    tick = 22U; window_request(8U, 1U); process(22U);
    tick = 23U; window_request(9U, 0U); process(23U);
    tick = 24U; window_request(8U, 0U); process(24U);
    assert(pwm_pulse == 1700U && pwm_update_calls == 1U);
    assert(SlaveRuntimeDiag.request_count == 1U);
    assert(SlaveRuntimeDiag.duplicate_request_count == 1U);
    assert(SlaveRuntimeDiag.ignored_message_count == 2U);
    process(73U); assert(sent_count == 0U); process(74U);
    assert_window_ack(8U, 0U);
    tick = 75U; window_request(10U, 0U); process(75U); process(125U);
    assert_window_ack(10U, 0U);
    assert(pwm_update_calls == 2U && pwm_start_calls == 1U);
}

static void test_window_ack_priority_preserves_telemetry_and_duplicate_cache(void)
{
    reset(0U); process(0U); process(20U);
    audio_valid = 1U; audio_tick = 20U; audio_epoch = 1U;
    audio_left = 70000U; audio_right = 80000U;
    tick = 21U; request(40U, 0U, 0U, 0U); process(21U);
    tick = 22U; window_request(41U, 1U); process(22U); process(71U);
    assert(sent_count == 0U); process(72U);
    assert(sent_count == 1U); assert_window_ack(41U, 0U);
    process(72U); process(121U); assert(sent_count == 1U);
    process(122U); assert(sent_count == 2U);
    assert_reply(40U, 1U); assert_audio(70000U, 80000U);
    audio_tick = 123U; audio_left = 123U; audio_right = 456U;
    tick = 123U; request(40U, 0U, 0U, 0U); process(123U); process(173U);
    assert(sent_count == 3U); assert_reply(40U, 1U);
    assert_audio(70000U, 80000U);
    assert(SlaveRuntimeDiag.duplicate_request_count == 1U);
    assert(pwm_update_calls == 1U);
}

static void test_window_does_not_cancel_force_sample_or_bme_failures(void)
{
    unsigned completed;
    reset(0U); process(0U); process(20U);
    completed = SlaveBme280Diag.completed_count;
    tick = 21U; request(50U, 1U, 0U, 0U); process(21U);
    tick = 22U; window_request(51U, 0U); process(22U);
    tick = 23U; request(50U, 1U, 0U, 0U); process(23U);
    assert(SlaveRuntimeDiag.duplicate_request_count == 1U);
    process(32U);
    assert(SlaveBme280Diag.completed_count == completed + 1U);
    process(73U); assert(sent_count == 1U); assert_window_ack(51U, 0U);
    process(122U); assert(sent_count == 1U); process(123U);
    assert_reply(50U, 1U); assert(sent_count == 2U);

    reset(0U); present = 0U;
    audio_valid = 1U; audio_tick = 0U; audio_epoch = 1U;
    audio_left = 123U; audio_right = 456U;
    window_request(52U, 1U); request(53U, 1U, 0U, 0U); process(0U);
    process(1U); process(50U);
    assert_window_ack(52U, 0U);
    process(100U); assert_reply(53U, 0U); assert_audio(123U, 456U);
    assert(pwm_pulse == 1700U && SlaveBme280Diag.failure_count != 0U);
}

static void test_window_driver_failure_and_bounded_uart_retry(void)
{
    reset(0U); process(0U); process(20U); pwm_update_success = 0U;
    tick = 21U; window_request(60U, 1U); process(21U); process(71U);
    assert_window_ack(60U, 1U);
    assert(pwm_stop_calls == 1U && pwm_update_calls == 1U);
    assert(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT);
    tick = 72U; window_request(61U, 0U); process(72U); process(122U);
    assert_window_ack(61U, 1U);
    assert(pwm_update_calls == 1U && pwm_start_calls == 1U);

    reset(0U); pwm_start_success = 0U; SlaveServoTest_InitManual(0U);
    window_request(62U, 1U); process(0U); process(50U);
    assert_window_ack(62U, 1U); assert(pwm_update_calls == 0U);

    reset(0U); process(0U); process(20U); tx_fail = 1U;
    tick = 21U; request(63U, 0U, 0U, 0U); process(21U);
    tick = 22U; window_request(64U, 1U); process(22U);
    process(72U); assert(tx_attempts == 1U);
    process(81U); assert(tx_attempts == 1U); process(82U);
    assert(tx_attempts == 2U); process(91U); assert(tx_attempts == 2U);
    process(92U); assert(tx_attempts == 3U && SlaveRuntimeDiag.tx_failure_count == 1U);
    assert(pwm_update_calls == 1U && sent_count == 0U);
    tx_fail = 0U; process(93U); process(141U); assert(tx_attempts == 3U);
    process(142U); assert_reply(63U, 1U);
    tick = 143U; window_request(65U, 0U); process(143U); process(193U);
    assert_window_ack(65U, 0U);
    assert(pwm_update_calls == 2U && pwm_start_calls == 1U);
}

static void test_window_ack_tick_wrap_and_late_audio_expiry(void)
{
    reset(UINT32_MAX - 10U);
    window_request(70U, 1U); process(tick);
    process(8U); process(9U); process(38U); assert(tx_attempts == 0U);
    process(39U); assert_window_ack(70U, 0U);
    assert(pwm_update_calls == 1U && pwm_pulse == 1700U);

    /* Keep a pending telemetry snapshot across multiple accepted window ACKs. */
    reset(0U); process(0U); process(20U);
    audio_valid = 1U; audio_tick = 20U; audio_epoch = 1U;
    audio_left = 123U; audio_right = 456U;
    tick = 21U; request(71U, 0U, 0U, 0U); process(21U);
    tick = 22U; window_request(72U, 1U); process(22U);
    process(72U); assert_window_ack(72U, 0U);
    tick = 73U; window_request(73U, 0U); process(73U); process(123U);
    assert_window_ack(73U, 0U);
    tick = 124U; window_request(74U, 1U); process(124U); process(174U);
    assert_window_ack(74U, 0U);
    tick = 175U; window_request(75U, 0U); process(175U); process(225U);
    tick = 226U; window_request(76U, 1U); process(226U); process(276U);
    assert_window_ack(76U, 0U); assert(sent_count == 5U);
    audio_tick = 300U; audio_left = 999U; audio_right = 1000U;
    process(326U); assert(sent_count == 6U); assert_reply(71U, 1U);
    assert_audio(0xFFFFFFFFUL, 0xFFFFFFFFUL);
    tick = 327U; request(71U, 0U, 0U, 0U); process(327U); process(377U);
    assert_reply(71U, 1U); assert_audio(0xFFFFFFFFUL, 0xFFFFFFFFUL);
}

static void test_window_timed_stop_and_stop_failure_preserve_telemetry(void)
{
    reset(0U); process(0U); process(20U);
    tick = 21U; window_request(80U, 1U); process(21U); process(71U);
    assert_window_ack(80U, 0U);
    /* A fresh flow carrying the same in-flight action must not renew motion. */
    tick = 75U; window_request(81U, 1U); process(75U); process(125U);
    assert_window_ack(81U, 0U);
    assert(pwm_update_calls == 1U);
    process(21U + SLAVE_SERVO_WINDOW_RUN_MS - 1U);
    assert(pwm_pulse == 1700U && pwm_update_calls == 1U);
    process(21U + SLAVE_SERVO_WINDOW_RUN_MS);
    assert(pwm_pulse == SLAVE_SERVO_WINDOW_STOP_US && pwm_update_calls == 2U);
    assert(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL);

    reset(0U); process(0U); process(20U);
    tick = 21U; window_request(82U, 1U); process(21U); process(71U);
    assert_window_ack(82U, 0U); /* Only confirms the earlier start PWM setting. */
    audio_valid = 1U; audio_tick = 300U; audio_epoch = 1U;
    audio_left = 70000U; audio_right = 80000U;
    tick = 300U; request(83U, 0U, 0U, 0U); process(300U);
    pwm_update_success = 0U;
    process(320U); assert(pwm_pulse == 1700U);
    process(321U);
    assert(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT);
    assert(SlaveServoTestDiag.last_error == SLAVE_SERVO_TEST_ERROR_UPDATE);
    assert(pwm_stop_calls == 1U && pwm_update_calls == 2U && pwm_pulse == 0U);
    process(350U); assert_reply(83U, 1U); assert_audio(70000U, 80000U);
    assert(sent_count == 2U);
    tick = 351U; window_request(84U, 0U); process(351U); process(401U);
    assert_window_ack(84U, 1U);
    assert(pwm_start_calls == 1U && pwm_update_calls == 2U && pwm_stop_calls == 1U);
}

int main(void)
{
    test_sensor_vector_cache_and_address();
    test_init_failures_and_reconnect();
    test_conversion_timeout_and_tick_wrap();
    test_i2c_busy_and_line_fault();
    test_protocol_cached_force_duplicates_and_filters();
    test_protocol_fault_and_tx_retry();
    test_driver_signed_calibration_and_invalid_raw();
    test_duplicate_snapshot_expires_despite_new_sample();
    test_audio_wire_values_and_duplicate_snapshot_age();
    test_audio_fault_epoch_pending_expiry_and_tx_retry();
    test_force_waits_only_for_bme_audio_failure_is_independent();
    test_window_strict_address_payload_and_crc();
    test_window_targets_preload_and_receive_silence();
    test_window_pending_duplicate_and_busy_never_execute();
    test_window_ack_priority_preserves_telemetry_and_duplicate_cache();
    test_window_does_not_cancel_force_sample_or_bme_failures();
    test_window_driver_failure_and_bounded_uart_retry();
    test_window_ack_tick_wrap_and_late_audio_expiry();
    test_window_timed_stop_and_stop_failure_preserve_telemetry();
    puts("19 slave BME/I2C/runtime/audio-upload/timed-window test groups passed");
    return 0;
}
