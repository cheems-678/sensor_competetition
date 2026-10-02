#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bme280.h"
#include "i2c.h"
#include "slave_bme280.h"
#include "slave_protocol_runtime.h"

I2C_TypeDef fake_i2c1;
static uint32_t tick;
static uint8_t bank[256], present, device_address, tx_fail;
static int fail_reg;
static unsigned sent_count, bus_edges;
static uint16_t gpio_levels, stuck_pins;
static uint8_t sent[141];
static uint16_t sent_len;

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
    gpio_levels = GPIO_PIN_6 | GPIO_PIN_7; stuck_pins = 0U;
    bank[0xD0] = 0x60U;
    for (i = 0U; i < 12U; i++) { put16(0x88U + i*2U, calibration[i]); }
    bank[0xA1] = 75U; put16(0xE1U, 362);
    bank[0xE4] = 20U; bank[0xE5] = 0x2EU; bank[0xE6] = 3U; bank[0xE7] = 30U;
    raw20(0xF7U, 415148U); raw20(0xFAU, 519888U);
    bank[0xFD] = (uint8_t)(35000U >> 8U); bank[0xFE] = (uint8_t)35000U;
    SlaveRuntime_Init(1U);
}
static void process(uint32_t now) { tick = now; SlaveRuntime_Process(tick); }
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
static void assert_reply(uint16_t flow, uint8_t valid)
{
    assert(sent_len == 31U && sent[2] == 4U && sent[3] == 2U);
    assert(sent[4] == 3U && sent[5] == 1U && sent[6] == 2U && sent[7] == 1U);
    assert((uint16_t)(sent[8] | ((uint16_t)sent[9] << 8U)) == flow);
    assert(sent[10] == 18U && sent[11] == 0U);
    assert(crc16(&sent[2], 27U) == (uint16_t)(sent[29] | ((uint16_t)sent[30] << 8U)));
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
    puts("8 slave BME/I2C/runtime test groups passed");
    return 0;
}
