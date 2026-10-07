#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "BME280.h"
#include "fan_pwm.h"
#include "master_bme280.h"
#include "master_config.h"
#include "master_ingress.h"
#include "master_light_control.h"
#include "master_rain.h"
#include "master_queues.h"
#include "master_runtime.h"
#include "master_acoustic.h"
static uint8_t audio_valid;
static MasterAcousticSample audio_sample;
uint8_t MasterAcoustic_GetSample(uint32_t now, MasterAcousticSample *sample)
{
    if (!audio_valid || (uint32_t)(now - audio_sample.tick) >= 300U) { return 0U; }
    *sample = audio_sample; return 1U;
}

static uint32_t tick;
static BME280_Status init_status, trigger_status, read_status;
static BME280_Data sensor_data;
static unsigned read_count, init_count, send_count, fan_count, light_count;
static uint8_t last_channel, last_duty, event_ready, fail_send;
static uint8_t preserve_received_tick;
static MasterEvent event;
static LoRaMessage sent;
static uint16_t slave_flow;
static uint8_t rain_state;
volatile MasterLightDiagnostics MasterLightDiag;
static uint8_t status_queue_busy;
uint8_t MasterQueues_EventWaiting(void) { return event_ready; }
uint8_t MasterQueues_LoRaWaiting(void) { return status_queue_busy; }
uint8_t FanPwm_GetDuty(uint8_t channel)
{ return channel == last_channel ? last_duty : 0U; }

uint32_t HAL_GetTick(void) { return tick; }

BME280_Status BME280_Init(BME280_HandleTypeDef *device)
{
    init_count++;
    device->i2c_address = 0x76U;
    device->chip_id = (init_status == BME280_OK) ? 0x60U : 0U;
    return init_status;
}

BME280_Status BME280_Config(BME280_HandleTypeDef *device,
    BME280_Oversampling t, BME280_Oversampling p, BME280_Oversampling h,
    BME280_Filter filter)
{
    (void)device;
    assert(t == BME280_OVERSAMPLING_X1 && p == BME280_OVERSAMPLING_X1);
    assert(h == BME280_OVERSAMPLING_X1 && filter == BME280_FILTER_OFF);
    return BME280_OK;
}

BME280_Status BME280_TriggerMeasurement(BME280_HandleTypeDef *device)
{
    (void)device;
    return trigger_status;
}

uint32_t BME280_GetMeasurementDelayMs(const BME280_HandleTypeDef *device)
{
    (void)device;
    return 10U;
}

BME280_Status BME280_ReadMeasurement(BME280_HandleTypeDef *device, BME280_Data *data)
{
    (void)device;
    read_count++;
    if (read_status == BME280_OK) { *data = sensor_data; }
    return read_status;
}

void MasterLight_Init(uint32_t now_ms) { (void)now_ms; }
void MasterLight_Process(uint32_t now_ms) { (void)now_ms; light_count++; }
void MasterRain_Init(uint32_t now_ms) { (void)now_ms; rain_state = 0xFFU; }
void MasterRain_Process(uint32_t now_ms) { (void)now_ms; }
uint8_t MasterRain_GetState(uint32_t now_ms) { (void)now_ms; return rain_state; }

uint8_t FanPwm_SetDuty(uint8_t channel, uint8_t duty)
{
    fan_count++;
    last_channel = channel;
    last_duty = duty;
    return (channel >= 1U && channel <= FAN_PWM_CHANNEL_COUNT && duty <= 100U) ? 1U : 0U;
}

uint8_t MasterQueues_ReceiveEvent(MasterEvent *out)
{
    if (event_ready == 0U) { return 0U; }
    *out = event;
    event_ready = 0U;
    return 1U;
}

uint8_t MasterQueues_SendLoRa(const LoRaMessage *message)
{
    if (fail_send != 0U) { return 0U; }
    assert(message->destination_role == LORA_ROLE_CONTROL_ROOM ||
           message->destination_role == LORA_ROLE_SLAVE);
    if (message->destination_role == LORA_ROLE_SLAVE)
    {
        assert(message->destination_group == 1U);
        assert(message->type == LORA_MSG_READ_TELEMETRY ||
               message->type == LORA_MSG_SET_WINDOW || message->type == WEB_RESULT);
        slave_flow = message->flow_id;
    }
    sent = *message;
    send_count++;
    return 1U;
}

static void reset(uint32_t start)
{
    audio_valid = 0U;
    tick = start;
    init_status = trigger_status = read_status = BME280_OK;
    sensor_data.temperature_x100 = -555;
    sensor_data.humidity_x1024 = 51200U;
    sensor_data.pressure_pa = 101325U;
    read_count = init_count = send_count = fan_count = light_count = 0U;
    event_ready = fail_send = 0U;
    preserve_received_tick = 0U;
    status_queue_busy = 0U; memset((void *)&MasterLightDiag, 0, sizeof(MasterLightDiag));
    memset(&sent, 0, sizeof(sent));
    MasterRuntime_Init();
}

static void request(uint8_t type, uint16_t flow, uint8_t value, uint8_t duty)
{
    memset(&event, 0, sizeof(event));
    event.type = MASTER_EVENT_LORA_MESSAGE;
    event.data.lora_message.type = type;
    event.data.lora_message.source_role = LORA_ROLE_CONTROL_ROOM;
    event.data.lora_message.source_group = 0U;
    event.data.lora_message.destination_role = LORA_ROLE_MASTER;
    event.data.lora_message.destination_group = 1U;
    event.data.lora_message.payload_length =
        (type == LORA_MSG_SET_FAN_SPEED || type == LORA_MSG_SET_WINDOW) ? 2U : 1U;
    event.data.lora_message.flow_id = flow;
    event.data.lora_message.payload[0] = value;
    event.data.lora_message.payload[1] = duty;
    event_ready = 1U;
}

static void process(uint32_t now)
{
    tick = now;
    if ((event_ready != 0U) && (preserve_received_tick == 0U))
    { event.received_tick = now; }
    MasterRuntime_ProcessOne(tick);
}

static void slave_reply(uint16_t flow, uint8_t valid)
{
    static const uint8_t data[] = {0xFA, 0x00, 0x58, 0x02, 0xA0, 0x86, 0x01, 0x00};
    request(LORA_MSG_TELEMETRY, flow, 0U, 0U);
    event.data.lora_message.source_role = LORA_ROLE_SLAVE;
    event.data.lora_message.source_group = 1U;
    event.data.lora_message.payload_length = 18U;
    memcpy(&event.data.lora_message.payload[1], data, 8U);
    if (!valid)
    {
        memset(&event.data.lora_message.payload[1], 0xFF, 8U);
        event.data.lora_message.payload[1] = 0U;
        event.data.lora_message.payload[2] = 0x80U;
    }
}

static void send_slave_query(uint32_t now)
{
    assert(sent.destination_role == LORA_ROLE_SLAVE);
    MasterRuntime_NotifySlaveRequestSent(slave_flow, now);
    assert(!MasterRuntime_CanTransmit());
}

static void assert_telemetry(uint16_t flow, uint8_t valid)
{
    static const uint8_t invalid[] = {
        0x73U, 0U, 0x80U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0U, 0x80U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU
    };
    assert(sent.type == LORA_MSG_TELEMETRY && sent.flow_id == flow);
    assert(sent.payload_length == 46U);
    assert(memcmp(&sent.payload[30], "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8U) == 0);
    assert(memcmp(&sent.payload[18], "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8U) == 0);
    if (valid == 0U) { assert(memcmp(sent.payload, invalid, sizeof(invalid)) == 0); }
    else
    {
        static const uint8_t expected[] = {
            0x73U, 0xC8U, 0xFFU, 0xF4U, 1U, 0xCDU, 0x8BU, 1U, 0U,
            0U, 0x80U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU
        };
        assert(memcmp(sent.payload, expected, sizeof(expected)) == 0);
    }
}

static void test_conversion_and_cache(void)
{
    MasterBme280Sample sample;
    reset(0U);
    process(0U);
    assert(MasterBme280Diag.chip_id == 0x60U);
    assert(MasterBme280Diag.calibration_valid == 1U);
    assert(!MasterBme280_GetSample(0U, &sample));
    process(9U);
    assert(read_count == 0U);
    process(10U);
    assert(MasterBme280_GetSample(10U, &sample));
    assert(sample.temperature_x10 == -56 && sample.humidity_x10 == 500U);
    assert(sample.pressure_pa == 101325U);
    assert(MasterBme280_GetSample(2009U, &sample));
    assert(!MasterBme280_GetSample(2010U, &sample));
    assert(!MasterBme280_GetSample(10U, NULL));
    process(1009U);
    assert(MasterBme280Diag.sample_attempt_count == 1U);
    process(1010U);
    assert(MasterBme280Diag.sample_attempt_count == 2U);
}

static void test_failure_retry_and_recovery(void)
{
    MasterBme280Sample sample;
    reset(0U);
    init_status = BME280_ERROR_NO_ACK;
    process(0U);
    assert(MasterBme280Diag.failure_count == 1U && init_count == 1U);
    process(999U);
    assert(init_count == 1U);
    init_status = BME280_OK;
    process(1000U);
    process(1010U);
    assert(MasterBme280_GetSample(1010U, &sample));
    read_status = BME280_ERROR_BUS;
    process(2010U);
    process(2020U);
    assert(!MasterBme280_GetSample(2020U, &sample));
    assert(MasterBme280Diag.last_error == BME280_ERROR_BUS);
    read_status = BME280_OK;
    process(3020U);
    process(3030U);
    assert(MasterBme280_GetSample(3030U, &sample));
}

static void test_timeout_trigger_error_and_range_check(void)
{
    uint32_t now;
    MasterBme280Sample sample;
    reset(0U);
    read_status = BME280_ERROR_NOT_READY;
    process(0U);
    for (now = 10U; now <= 50U; now++) { process(now); }
    assert(MasterBme280Diag.last_error == BME280_ERROR_TIMEOUT);
    assert(MasterBme280Diag.completed_count == 1U);
    reset(0U);
    trigger_status = BME280_ERROR_NO_ACK;
    process(0U);
    assert(MasterBme280Diag.last_error == BME280_ERROR_NO_ACK);
    assert(read_count == 0U);
    reset(0U);
    sensor_data.pressure_pa = 0U;
    process(0U);
    process(10U);
    assert(!MasterBme280_GetSample(10U, &sample));
    assert(MasterBme280Diag.failure_count == 1U);
}

static void test_tick_wrap(void)
{
    MasterBme280Sample sample;
    reset(0xFFFFFFFAU);
    process(0xFFFFFFFAU);
    process(3U);
    assert(read_count == 0U);
    process(4U);
    assert(MasterBme280_GetSample(4U, &sample));
    assert(!MasterBme280_GetSample(2004U, &sample));
}

static void test_cached_reply_and_placeholder(void)
{
    reset(0U);
    init_status = BME280_ERROR_NO_ACK;
    request(LORA_MSG_READ_TELEMETRY, 7U, 0U, 0U);
    process(0U);
    send_slave_query(50U);
    process(549U);
    assert(sent.type == LORA_MSG_READ_TELEMETRY);
    process(550U);
    assert_telemetry(7U, 0U);
    assert(MasterRuntime_CanTransmit());
    reset(0U);
    process(0U);
    process(10U);
    request(LORA_MSG_READ_TELEMETRY, 8U, 0U, 0U);
    process(11U);
    send_slave_query(60U);
    process(560U);
    assert_telemetry(8U, 1U);
    assert(MasterRuntimeDiag.slave_request_queued_count == 1U);
    assert(MasterRuntimeDiag.telemetry_timeout_count == 1U);
}

static void test_force_sample_and_fan_during_conversion(void)
{
    reset(0U);
    request(LORA_MSG_READ_TELEMETRY, 9U, 1U, 0U);
    process(0U);
    assert(send_count == 1U && sent.payload[0] == 1U);
    send_slave_query(0U);
    request(LORA_MSG_SET_FAN_SPEED, 10U, 2U, 75U);
    process(1U);
    assert(fan_count == 0U);
    assert(sent.type == LORA_MSG_ERROR && sent.flow_id == 10U && sent.payload[0] == 1U);
    process(10U);
    assert(MasterBme280Diag.completed_count == 1U);
    assert(!MasterRuntime_CanTransmit());
    slave_reply(slave_flow, 0U);
    process(11U);
    assert(sent.payload[0] == 0x77U);
    sent.payload[0] = 0x73U; /* Online sensor failure still has invalid slave slots. */
    assert_telemetry(9U, 1U);
    assert(light_count == 4U);
    request(LORA_MSG_READ_TELEMETRY, 11U, 1U, 0U);
    process(12U);
    send_slave_query(12U);
    slave_reply(slave_flow, 0U);
    process(13U);
    assert(sent.destination_role == LORA_ROLE_SLAVE); /* Await new local sample too. */
    process(22U);
    process(23U);
    assert(sent.payload[0] == 0x77U);
    sent.payload[0] = 0x73U;
    assert_telemetry(11U, 1U);
}

static void test_failed_force_and_queue_retry(void)
{
    reset(0U);
    init_status = BME280_ERROR_NO_ACK;
    request(LORA_MSG_READ_TELEMETRY, 12U, 1U, 0U);
    process(0U);
    send_slave_query(0U);
    process(1U);
    process(500U);
    assert_telemetry(12U, 0U);
    assert(MasterRuntimeDiag.telemetry_timeout_count == 1U);
    reset(0U);
    request(LORA_MSG_READ_TELEMETRY, 13U, 1U, 0U);
    process(0U);
    send_slave_query(0U);
    slave_reply(slave_flow, 0U);
    fail_send = 1U;
    process(10U);
    assert(send_count == 1U && MasterRuntimeDiag.lora_queue_failure_count == 1U);
    fail_send = 0U;
    process(11U);
    sent.payload[0] = 0x73U;
    assert_telemetry(13U, 1U);
    process(12U);
    assert(send_count == 2U);
}

static void test_busy_force_and_unsolicited_slave(void)
{
    reset(0U);
    request(LORA_MSG_READ_TELEMETRY, 14U, 1U, 0U);
    process(0U);
    send_slave_query(0U);
    request(LORA_MSG_READ_TELEMETRY, 15U, 1U, 0U);
    process(1U);
    assert(sent.type == LORA_MSG_ERROR && sent.payload[0] == 1U);
    slave_reply((uint16_t)(slave_flow + 1U), 1U);
    process(2U);
    assert(send_count == 2U);
    assert(MasterRuntimeDiag.slave_response_unmatched_count == 1U);
    slave_reply(slave_flow, 1U);
    process(10U);
    assert(sent.flow_id == 14U && sent.type == LORA_MSG_TELEMETRY);
    assert(sent.payload[0] == 0x77U && sent.payload[9] == 250U);
    assert(sent.payload[1] == 0xC8U); /* Master reading never overwritten. */
    assert(MasterRuntimeDiag.slave_response_match_count == 1U);
    slave_reply(slave_flow, 1U);
    process(11U);
    assert(MasterRuntimeDiag.slave_response_unmatched_count == 2U);
}

static void test_rounding_and_measurement_boundaries(void)
{
    static const int32_t temperatures[] = {-4000, -555, -554, -5, -4, 0, 4, 5, 2555, 8500};
    static const int16_t expected[] = {-400, -56, -55, -1, 0, 0, 0, 1, 256, 850};
    unsigned i;
    MasterBme280Sample sample;
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        reset(0U);
        sensor_data.temperature_x100 = temperatures[i];
        sensor_data.humidity_x1024 = 102400U;
        sensor_data.pressure_pa = (i % 2U) ? 30000U : 110000U;
        process(0U);
        process(10U);
        assert(MasterBme280_GetSample(10U, &sample));
        assert(sample.temperature_x10 == expected[i]);
        assert(sample.humidity_x10 == 1000U);
        assert(sample.pressure_pa == sensor_data.pressure_pa);
    }
    for (i = 0; i < 4U; i++)
    {
        reset(0U);
        if (i == 0U) { sensor_data.temperature_x100 = -4001; }
        if (i == 1U) { sensor_data.temperature_x100 = 8501; }
        if (i == 2U) { sensor_data.humidity_x1024 = 102401U; }
        if (i == 3U) { sensor_data.pressure_pa = 110001U; }
        process(0U);
        process(10U);
        assert(!MasterBme280_GetSample(10U, &sample));
    }
}

static void test_address_filter_late_reply_queue_timeout_and_wrap(void)
{
    reset(0U);
    process(0U); process(10U);
    request(LORA_MSG_READ_TELEMETRY, 20U, 0U, 0U); process(11U);
    assert(MasterRuntime_IsSlaveQueryCurrent(slave_flow));
    /* Request queued but not physically transmitted: discard after 1 second. */
    process(1011U);
    assert_telemetry(20U, 1U);
    assert(!MasterRuntime_IsSlaveQueryCurrent(slave_flow));
    MasterRuntime_NotifySlaveRequestSent(slave_flow, 1012U);
    assert(MasterRuntime_CanTransmit());

    request(LORA_MSG_READ_TELEMETRY, 21U, 0U, 0U); process(1013U);
    send_slave_query(1063U);
    slave_reply(slave_flow, 1U);
    event.data.lora_message.source_group = 2U; process(1064U);
    assert(MasterRuntimeDiag.address_drop_count == 1U);
    slave_reply(slave_flow, 1U);
    event.data.lora_message.destination_role = LORA_ROLE_CONTROL_ROOM; process(1065U);
    assert(MasterRuntimeDiag.address_drop_count == 2U);
    slave_reply(slave_flow, 1U);
    event.data.lora_message.payload_length = 17U; process(1066U);
    slave_reply(slave_flow, 1U);
    event.data.lora_message.payload[0] = 1U; process(1067U);
    assert(MasterRuntimeDiag.slave_response_unmatched_count == 2U);
    slave_reply(slave_flow, 1U); process(1563U); /* Arrived at timeout: too late. */
    assert_telemetry(21U, 1U);
    assert(MasterRuntimeDiag.slave_response_match_count == 0U);

    reset(0xFFFFFF00U);
    request(LORA_MSG_READ_TELEMETRY, 22U, 0U, 0U); process(tick);
    send_slave_query(0xFFFFFF32U);
    process(293U); assert(!MasterRuntime_CanTransmit());
    process(294U); assert_telemetry(22U, 1U);
    assert(MasterRuntime_CanTransmit());
}

static void test_five_minute_virtual_run(void)
{
    uint32_t now;
    unsigned valid_frames = 0U, invalid_frames = 0U;
    reset(0U);
    for (now = 0U; now < 300000U; now++)
    {
        init_status = read_status = (now >= 150000U && now <= 152000U) ?
            BME280_ERROR_NO_ACK : BME280_OK;
        if ((now % 1000U) == 0U)
        {
            request(LORA_MSG_READ_TELEMETRY, (uint16_t)(now / 1000U), 0U, 0U);
        }
        else if ((now % 1000U) == 50U)
        {
            send_slave_query(now);
        }
        else if ((now % 1000U) == 200U && !(now >= 200000U && now < 203000U))
        {
            slave_reply(slave_flow, 1U);
        }
        else if ((now % 1000U) == 700U)
        {
            request(LORA_MSG_SET_FAN_SPEED, (uint16_t)(1000U + now / 1000U), 1U, 0U);
        }
        process(now);
        if ((now % 1000U) == 600U)
        {
            assert(sent.type == LORA_MSG_TELEMETRY);
            if (sent.payload[1] == 0U && sent.payload[2] == 0x80U) { invalid_frames++; }
            else { valid_frames++; }
        }
    }
    assert(valid_frames > 290U && invalid_frames >= 2U);
    assert(valid_frames + invalid_frames == 300U);
    assert(MasterRuntimeDiag.telemetry_reply_count == 300U);
    assert(MasterRuntimeDiag.telemetry_timeout_count == 3U);
    assert(MasterRuntimeDiag.slave_response_match_count == 297U);
    assert(MasterRuntimeDiag.slave_request_queued_count == 300U);
    assert(fan_count == 300U && light_count == 300000U);
    assert(MasterBme280Diag.failure_count > 0U);
    assert(MasterBme280Diag.last_error == BME280_OK);
}

static void slave_reply_audio(uint16_t flow, uint32_t left, uint32_t right)
{
    unsigned i;
    slave_reply(flow, 1U);
    event.data.lora_message.payload_length = 26U;
    event.data.lora_message.payload[0] = 8U;
    for (i = 0U; i < 4U; i++)
    {
        event.data.lora_message.payload[18U + i] = (uint8_t)(left >> (8U * i));
        event.data.lora_message.payload[22U + i] = (uint8_t)(right >> (8U * i));
    }
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static void test_extended_audio_and_aggregate_expiry(void)
{
    reset(0U);
    request(LORA_MSG_READ_TELEMETRY, 40U, 0U, 0U); process(0U);
    send_slave_query(50U);
    slave_reply_audio((uint16_t)(slave_flow + 1U), 7U, 9U); process(51U);
    assert(MasterRuntimeDiag.slave_response_unmatched_count == 1U);
    slave_reply_audio(slave_flow, 0U, 131071U); process(52U);
    assert(sent.payload_length == 46U && sent.payload[0] == 0x77U);
    assert(read_u32(&sent.payload[18]) == 0xFFFFFFFFU);
    assert(read_u32(&sent.payload[22]) == 0xFFFFFFFFU);
    assert(sent.payload[9] == 250U && sent.payload[1] == 0xC8U);

    /* Queue failure must not make the same snapshot fresh again. */
    request(LORA_MSG_READ_TELEMETRY, 41U, 0U, 0U); process(60U);
    send_slave_query(110U);
    slave_reply_audio(slave_flow, 70000U, 17U);
    fail_send = 1U; process(111U);
    process(410U); /* 299 ms: still the received snapshot. */
    fail_send = 0U; process(411U); /* 300 ms: both invalid, BME retained. */
    assert(sent.type == LORA_MSG_TELEMETRY && sent.flow_id == 41U);
    assert(read_u32(&sent.payload[18]) == 0xFFFFFFFFU);
    assert(read_u32(&sent.payload[22]) == 0xFFFFFFFFU);
    assert(sent.payload[0] == 0x77U && sent.payload[9] == 250U);

    /* Local event queue delay counts toward audio age, not a new reception. */
    request(LORA_MSG_READ_TELEMETRY, 42U, 0U, 0U); process(420U);
    send_slave_query(470U);
    slave_reply_audio(slave_flow, 80000U, 65536U);
    event.received_tick = 471U; preserve_received_tick = 1U; process(771U);
    assert(sent.flow_id == 42U && sent.payload[0] == 0x77U);
    assert(read_u32(&sent.payload[18]) == 0xFFFFFFFFU);
    preserve_received_tick = 0U;

    reset(0xFFFFFF00U);
    request(LORA_MSG_READ_TELEMETRY, 43U, 0U, 0U); process(tick);
    send_slave_query(0xFFFFFF32U);
    slave_reply_audio(slave_flow, 131071U, 0U);
    fail_send = 1U; process(0xFFFFFF33U);
    fail_send = 0U; process(95U); /* 300 ms modulo subtraction. */
    assert(read_u32(&sent.payload[18]) == 0xFFFFFFFFU);
    assert(read_u32(&sent.payload[22]) == 0xFFFFFFFFU);

    reset(0U);
    request(LORA_MSG_READ_TELEMETRY, 44U, 0U, 0U); process(0U);
    send_slave_query(50U);
    slave_reply_audio(slave_flow, 0xFFFFFFFFU, 0xFFFFFFFFU); process(51U);
    assert(read_u32(&sent.payload[18]) == 0xFFFFFFFFU);
    assert(sent.payload[0] == 0x77U); /* Link online is not sensor validity. */
}

static void window_ack(uint16_t flow, uint8_t status)
{
    request(LORA_MSG_ACK, flow, LORA_MSG_SET_WINDOW, status);
    event.data.lora_message.source_role = LORA_ROLE_SLAVE;
    event.data.lora_message.source_group = 1U;
    event.data.lora_message.payload_length = 2U;
}

static void assert_window_ack(uint16_t flow, uint8_t status)
{
    assert(sent.type == LORA_MSG_ACK && sent.flow_id == flow);
    assert(sent.source_role == LORA_ROLE_MASTER && sent.source_group == 1U);
    assert(sent.destination_role == LORA_ROLE_CONTROL_ROOM && sent.destination_group == 0U);
    assert(sent.payload_length == 2U && sent.payload[0] == LORA_MSG_SET_WINDOW);
    assert(sent.payload[1] == status);
}

static void test_window_forward_and_real_ack(void)
{
    uint8_t status;
    for (status = 0U; status <= 3U; status++)
    {
        unsigned sample_attempts;
        reset(0U); process(0U); process(10U);
        sample_attempts = MasterBme280Diag.sample_attempt_count;
        request(LORA_MSG_SET_WINDOW, (uint16_t)(0x0300U + status), 1U, 1U);
        process(11U);
        assert(sent.type == LORA_MSG_SET_WINDOW && sent.destination_role == LORA_ROLE_SLAVE);
        assert(sent.payload_length == 2U && sent.payload[0] == 1U && sent.payload[1] == 1U);
        assert(sent.flow_id == 1U && sent.flow_id != (uint16_t)(0x0300U + status));
        assert(MasterRuntime_IsSlaveRequestCurrent(LORA_MSG_SET_WINDOW, slave_flow));
        assert(!MasterRuntime_IsSlaveRequestCurrent(LORA_MSG_READ_TELEMETRY, slave_flow));
        assert(!MasterRuntime_IsSlaveQueryCurrent(slave_flow));
        assert(MasterRuntime_CanTransmit() && send_count == 1U);
        window_ack(slave_flow, status); process(12U); /* Not yet sent: ignore. */
        assert(send_count == 1U && MasterRuntimeDiag.window_response_match_count == 0U);
        send_slave_query(60U);
        window_ack(slave_flow, status); process(61U);
        assert_window_ack((uint16_t)(0x0300U + status), status);
        assert(MasterRuntime_CanTransmit() && send_count == 2U);
        assert(MasterRuntimeDiag.window_command_count == 1U);
        assert(MasterRuntimeDiag.window_response_match_count == 1U);
        assert(MasterRuntimeDiag.window_request_queued_count == 1U);
        assert(MasterRuntimeDiag.window_reply_count == 1U);
        assert(MasterRuntimeDiag.telemetry_request_count == 0U);
        assert(MasterRuntimeDiag.telemetry_reply_count == 0U);
        assert(MasterRuntimeDiag.telemetry_timeout_count == 0U);
        assert(MasterRuntimeDiag.slave_response_match_count == 0U);
        assert(MasterBme280Diag.sample_attempt_count == sample_attempts);
        window_ack(slave_flow, status); process(62U);
        assert(send_count == 2U); /* A duplicate final ACK cannot reopen a transaction. */
    }
}

static void test_window_wrong_reply_rejection(void)
{
    uint16_t flow;
    reset(0U);
    request(LORA_MSG_SET_WINDOW, 100U, 1U, 0U); process(0U);
    flow = slave_flow;
    send_slave_query(50U);
    window_ack((uint16_t)(flow + 1U), 0U); process(51U);
    window_ack(flow, 0U); event.data.lora_message.payload[0] = LORA_MSG_SET_FAN_SPEED; process(52U);
    window_ack(flow, 4U); process(53U);
    window_ack(flow, 0U); event.data.lora_message.payload_length = 1U; process(54U);
    slave_reply_audio(flow, 1U, 2U); process(55U); /* TELEMETRY cannot confirm a window. */
    window_ack(flow, 0U); event.data.lora_message.source_group = 2U; process(56U);
    window_ack(flow, 0U); event.data.lora_message.destination_role = LORA_ROLE_CONTROL_ROOM; process(57U);
    assert(MasterRuntimeDiag.window_response_unmatched_count == 5U);
    assert(MasterRuntimeDiag.address_drop_count == 2U);
    assert(!MasterRuntime_CanTransmit() && send_count == 1U);
    window_ack(flow, 0U); process(58U);
    assert_window_ack(100U, 0U);
    assert(MasterRuntimeDiag.window_response_match_count == 1U);
    assert(MasterRuntimeDiag.slave_response_match_count == 0U);
}

static void test_window_busy_does_not_replace_telemetry_or_fan(void)
{
    uint16_t flow;
    reset(0U);
    request(LORA_MSG_READ_TELEMETRY, 200U, 1U, 0U); process(0U);
    flow = slave_flow; send_slave_query(0U);
    request(LORA_MSG_SET_WINDOW, 201U, 1U, 1U); process(1U);
    assert_window_ack(201U, 3U);
    assert(slave_flow == flow && !MasterRuntime_CanTransmit());
    window_ack(flow, 0U); process(2U); /* Even an identical flow has wrong reply type. */
    assert(MasterRuntimeDiag.slave_response_unmatched_count == 1U);
    request(LORA_MSG_SET_FAN_SPEED, 202U, 2U, 50U); process(3U);
    assert(fan_count == 0U);
    assert(sent.type == LORA_MSG_ERROR && sent.payload[0] == 1U);
    assert(!MasterRuntime_CanTransmit());
    slave_reply_audio(flow, 7U, 9U); process(10U);
    assert(sent.type == LORA_MSG_TELEMETRY && sent.flow_id == 200U && sent.payload[0] == 0x77U);
    assert(read_u32(&sent.payload[18]) == 0xFFFFFFFFU && read_u32(&sent.payload[22]) == 0xFFFFFFFFU);
    assert(MasterRuntimeDiag.window_busy_count == 1U && MasterRuntimeDiag.window_request_queued_count == 0U);

    request(LORA_MSG_SET_WINDOW, 203U, 1U, 0U); process(11U);
    flow = slave_flow; send_slave_query(61U);
    request(LORA_MSG_READ_TELEMETRY, 204U, 1U, 0U); process(62U);
    assert(sent.type == LORA_MSG_ERROR && sent.flow_id == 204U && sent.payload[0] == 1U);
    request(LORA_MSG_SET_WINDOW, 205U, 1U, 1U); process(63U);
    assert_window_ack(205U, 3U);
    assert(slave_flow == flow && !MasterRuntime_CanTransmit());
    window_ack(flow, 1U); process(64U);
    assert_window_ack(203U, 1U);
    assert(MasterRuntimeDiag.telemetry_reply_count == 1U && MasterRuntimeDiag.telemetry_timeout_count == 0U);
}

static void test_window_queue_rx_timeout_and_wrap(void)
{
    uint16_t flow;
    reset(0U); fail_send = 1U;
    request(LORA_MSG_SET_WINDOW, 300U, 1U, 1U); process(0U);
    assert(send_count == 0U && MasterRuntimeDiag.lora_queue_failure_count == 1U);
    fail_send = 0U; process(10U);
    flow = slave_flow;
    assert(MasterRuntime_IsSlaveRequestCurrent(LORA_MSG_SET_WINDOW, flow));
    process(999U); assert(sent.type == LORA_MSG_SET_WINDOW);
    process(1000U); assert_window_ack(300U, 2U);
    assert(!MasterRuntime_IsSlaveRequestCurrent(LORA_MSG_SET_WINDOW, flow));
    MasterRuntime_NotifySlaveRequestSent(flow, 1001U);
    assert(MasterRuntime_CanTransmit());
    assert(MasterRuntimeDiag.window_timeout_count == 1U && MasterRuntimeDiag.telemetry_timeout_count == 0U);

    reset(0U); fail_send = 1U;
    request(LORA_MSG_SET_WINDOW, 303U, 1U, 1U); process(0U);
    process(999U); process(1000U); /* Full queue prevents both request and result. */
    assert(send_count == 0U && MasterRuntimeDiag.window_request_queued_count == 0U);
    assert(MasterRuntimeDiag.window_timeout_count == 1U && MasterRuntimeDiag.window_reply_count == 0U);
    fail_send = 0U; process(1001U);
    assert_window_ack(303U, 2U);
    assert(send_count == 1U && MasterRuntimeDiag.window_request_queued_count == 0U);

    reset(0U); request(LORA_MSG_SET_WINDOW, 301U, 1U, 0U); process(0U);
    send_slave_query(900U); /* RX deadline is not the original queue deadline. */
    process(1399U); assert(sent.type == LORA_MSG_SET_WINDOW && !MasterRuntime_CanTransmit());
    window_ack(slave_flow, 0U); process(1400U); /* Exactly 500 ms is too late. */
    assert_window_ack(301U, 2U);
    assert(MasterRuntimeDiag.window_response_match_count == 0U);

    reset(0xFFFFFF00U); request(LORA_MSG_SET_WINDOW, 302U, 1U, 1U); process(tick);
    send_slave_query(0xFFFFFF32U);
    process(293U); assert(!MasterRuntime_CanTransmit());
    process(294U); assert_window_ack(302U, 2U);
    assert(MasterRuntimeDiag.window_timeout_count == 1U && MasterRuntimeDiag.telemetry_timeout_count == 0U);

    reset(0xFFFFFF00U); request(LORA_MSG_SET_WINDOW, 304U, 1U, 0U); process(tick);
    send_slave_query(0xFFFFFF32U);
    window_ack(slave_flow, 0U); process(0U);
    assert_window_ack(304U, 0U);
    assert(MasterRuntimeDiag.window_response_match_count == 1U && MasterRuntimeDiag.window_timeout_count == 0U);
}

static void test_window_final_ack_queue_failure_keeps_result(void)
{
    uint16_t flow;
    reset(0U); request(LORA_MSG_SET_WINDOW, 400U, 1U, 1U); process(0U);
    flow = slave_flow; send_slave_query(50U);
    fail_send = 1U;
    window_ack(flow, 0U); process(51U);
    assert(send_count == 1U && MasterRuntimeDiag.window_response_match_count == 1U);
    window_ack(flow, 1U); process(52U); /* Cannot replace the stored successful result. */
    request(LORA_MSG_SET_WINDOW, 401U, 1U, 0U); process(53U);
    assert(MasterRuntimeDiag.window_busy_count == 1U && slave_flow == flow);
    process(5000U); /* Reply queuing delay must not turn success into timeout. */
    fail_send = 0U; process(5001U);
    assert_window_ack(400U, 0U);
    assert(MasterRuntimeDiag.window_reply_count == 1U && MasterRuntimeDiag.window_timeout_count == 0U);
    process(5002U); assert(send_count == 2U);
}

static void test_window_then_forced_telemetry_is_independent(void)
{
    uint16_t old_flow;
    reset(0U); process(0U); process(10U);
    request(LORA_MSG_SET_WINDOW, 500U, 1U, 1U); process(11U);
    old_flow = slave_flow; send_slave_query(61U);
    window_ack(old_flow, 0U); process(62U);
    assert(MasterBme280Diag.sample_attempt_count == 1U);
    request(LORA_MSG_READ_TELEMETRY, 501U, 1U, 0U); process(63U);
    assert(slave_flow == (uint16_t)(old_flow + 1U));
    send_slave_query(113U);
    window_ack(old_flow, 0U); process(114U);
    assert(!MasterRuntime_CanTransmit());
    slave_reply_audio(slave_flow, 17U, 19U); process(115U);
    assert(sent.type == LORA_MSG_READ_TELEMETRY); /* Forced conversion began at 114 ms. */
    process(124U);
    assert(sent.type == LORA_MSG_TELEMETRY && sent.flow_id == 501U && sent.payload[0] == 0x77U);
    assert(read_u32(&sent.payload[18]) == 0xFFFFFFFFU && read_u32(&sent.payload[22]) == 0xFFFFFFFFU);
    assert(MasterBme280Diag.sample_attempt_count == 2U);
    assert(MasterRuntimeDiag.window_response_match_count == 1U);
    assert(MasterRuntimeDiag.slave_response_match_count == 1U);
}

static void test_window_slave_flow_wrap_and_malformed_request(void)
{
    uint32_t index;
    reset(0U);
    request(LORA_MSG_SET_WINDOW, 600U, 5U, 0U); process(0U);
    request(LORA_MSG_SET_WINDOW, 600U, 1U, 2U); process(0U);
    request(LORA_MSG_SET_WINDOW, 600U, 1U, 0U);
    event.data.lora_message.payload_length = 1U; process(0U);
    assert(send_count == 0U && MasterRuntimeDiag.window_command_count == 0U);
    for (index = 0U; index < 65537U; index++)
    {
        request(LORA_MSG_SET_WINDOW, (uint16_t)(index + 100U), 1U, (uint8_t)(index & 1U));
        process(index * 2U);
        assert(slave_flow == (uint16_t)(index + 1U));
        send_slave_query(index * 2U);
        window_ack(slave_flow, 0U); process(index * 2U + 1U);
        assert_window_ack((uint16_t)(index + 100U), 0U);
    }
    assert(MasterRuntimeDiag.window_response_match_count == 65537U);
    assert(MasterRuntimeDiag.telemetry_reply_count == 0U);
}

static void test_four_fan_commands_and_ack(void)
{
    uint8_t channel;
    reset(0U);
    for (channel = 1U; channel <= 4U; channel++)
    {
        request(LORA_MSG_SET_FAN_SPEED, (uint16_t)(900U + channel), channel, (uint8_t)(channel * 25U));
        process(channel);
        assert(last_channel == channel && last_duty == channel * 25U);
        assert(sent.type == LORA_MSG_ACK && sent.flow_id == 900U + channel);
        assert(sent.payload[0] == LORA_MSG_SET_FAN_SPEED && sent.payload[1] == 0U);
        assert(sent.destination_role == LORA_ROLE_CONTROL_ROOM);
    }
    assert(fan_count == 4U && send_count == 4U);
    assert(MasterRuntimeDiag.slave_request_queued_count == 0U);
}

static void test_rain_is_local_and_does_not_control_actuators(void)
{
    static const uint8_t states[] = {0U, 1U, 0xFFU};
    unsigned i;
    for (i = 0U; i < sizeof(states) / sizeof(states[0]); i++)
    {
        reset(0U);
        init_status = BME280_ERROR_NO_ACK;
        rain_state = states[i];
        request(LORA_MSG_READ_TELEMETRY, (uint16_t)(950U + i), 0U, 0U);
        process(0U);
        send_slave_query(50U);
        process(550U); /* Local rain survives remote and local BME failures. */
        assert(sent.payload_length == 46U && sent.payload[0] == 0x73U);
        assert(sent.payload[LORA_TELEMETRY_RAIN_OFFSET] == states[i]);
        assert(sent.payload[1] == 0U && sent.payload[2] == 0x80U);
        assert(fan_count == 0U && MasterRuntimeDiag.window_command_count == 0U);

        request(LORA_MSG_READ_TELEMETRY, (uint16_t)(960U + i), 0U, 0U);
        process(560U);
        send_slave_query(610U);
        slave_reply_audio(slave_flow, 17U, 19U);
        event.data.lora_message.payload[17] = (uint8_t)(1U - (states[i] & 1U));
        process(611U);
        assert(sent.payload[0] == 0x77U);
        assert(sent.payload[17] == states[i]); /* Never trust the slave's slot. */
        assert(read_u32(&sent.payload[18]) == 0xFFFFFFFFU);
        assert(read_u32(&sent.payload[22]) == 0xFFFFFFFFU);
        assert(fan_count == 0U && MasterRuntimeDiag.window_command_count == 0U);
    }
}

static void test_mq2_event_delay_and_aggregate_retry(void)
{
    uint16_t age;
    reset(0U);
    request(LORA_MSG_READ_TELEMETRY, 900U, 0U, 0U); process(0U); send_slave_query(50U);
    slave_reply_audio(slave_flow, 0U, 7U);
    event.data.lora_message.payload_length = 34U;
    event.data.lora_message.payload[0] = 0x18U;
    memset(&event.data.lora_message.payload[26], 0, 8U);
    event.data.lora_message.payload[32] = (uint8_t)1900U;
    event.data.lora_message.payload[33] = (uint8_t)(1900U >> 8U);
    event.received_tick = 80U; preserve_received_tick = 1U;
    fail_send = 1U; process(100U);
    fail_send = 0U; process(150U);
    age = (uint16_t)(sent.payload[36] | ((uint16_t)sent.payload[37] << 8U));
    assert(sent.payload[0] == 0x77U && sent.payload[30] == 0U && age == 1970U);

    request(LORA_MSG_READ_TELEMETRY, 901U, 0U, 0U); process(200U); send_slave_query(250U);
    slave_reply_audio(slave_flow, 0U, 7U);
    event.data.lora_message.payload_length = 34U; event.data.lora_message.payload[0] = 0x18U;
    memset(&event.data.lora_message.payload[26], 0, 8U);
    event.data.lora_message.payload[32] = (uint8_t)1990U;
    event.data.lora_message.payload[33] = (uint8_t)(1990U >> 8U);
    event.received_tick = 260U; process(270U);
    assert(memcmp(&sent.payload[30], "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8U) == 0);
    assert(sent.payload[0] == 0x77U && sent.payload[9] == 250U);
}

static void test_status_without_control_room(void)
{
    LoRaMessage status;
    reset(0U); process(0U); process(10U);
    assert(!MasterRuntime_PrepareStatus(&status, 999U));
    rain_state = 1U; MasterLightDiag.input_valid = MasterLightDiag.output_valid = 1U;
    MasterLightDiag.stable_dark = 1U; MasterLightDiag.light_on = 0U;
    last_channel = 4U; last_duty = 75U;
    assert(MasterRuntime_PrepareStatus(&status, 1000U));
    assert(status.type == LORA_MSG_MASTER_STATUS && status.destination_role == 3U);
    assert(LoRaProtocol_ValidateMessage(&status) == LORA_PROTOCOL_OK);
    assert((int16_t)MonitorStatus_U16(status.payload+1U) == -56);
    assert(status.payload[11] == 1U && status.payload[12] == 1U && status.payload[13] == 0U);
    assert(status.payload[14] == 0U && status.payload[17] == 75U);
    assert(MonitorStatus_U16(status.payload+9U) == 990U);
    assert(!MasterRuntime_PrepareStatus(&status, 1999U));
    status_queue_busy = 1U; assert(!MasterRuntime_PrepareStatus(&status, 2000U));
    status_queue_busy = 0U;
    request(LORA_MSG_READ_TELEMETRY, 99U, 0U, 0U);
    assert(!MasterRuntime_PrepareStatus(&status, 2000U));
    process(2000U); assert(!MasterRuntime_PrepareStatus(&status, 2000U));
    reset(0xFFFFFF00U);
    assert(!MasterRuntime_PrepareStatus(&status, 743U));
    assert(MasterRuntime_PrepareStatus(&status, 744U));
    assert(MonitorStatus_U16(status.payload+1U) == 0x8000U);
    assert(status.payload[12] == 255U && status.payload[13] == 255U);
    assert(LoRaProtocol_ValidateMessage(&status) == LORA_PROTOCOL_OK);
    status.destination_role = 1U; status.destination_group = 0U;
    assert(LoRaProtocol_ValidateMessage(&status) == LORA_PROTOCOL_INVALID_DIRECTION);
}

static void test_ultrasonic_event_delay_and_retry(void)
{
    uint16_t age;
    reset(0U); request(LORA_MSG_READ_TELEMETRY, 980U, 0U, 0U); process(0U); send_slave_query(50U);
    slave_reply_audio(slave_flow,0U,7U);
    event.data.lora_message.payload_length=42U; event.data.lora_message.payload[0]=0x38U;
    memset(event.data.lora_message.payload+26U,255U,8U);
    event.data.lora_message.payload[34]=250U; event.data.lora_message.payload[35]=0U;
    event.data.lora_message.payload[36]=252U; event.data.lora_message.payload[37]=0U;
    event.data.lora_message.payload[38]=(uint8_t)1469U; event.data.lora_message.payload[39]=(uint8_t)(1469U>>8U);
    event.data.lora_message.payload[40]=(uint8_t)1900U; event.data.lora_message.payload[41]=(uint8_t)(1900U>>8U);
    event.received_tick=80U; preserve_received_tick=1U; fail_send=1U; process(100U);
    fail_send=0U; process(150U);
    age=(uint16_t)(sent.payload[44]|((uint16_t)sent.payload[45]<<8U));
    assert(sent.payload_length == 46U && sent.payload[0]==0x77U && age==1970U && sent.payload[38]==250U);
    assert(memcmp(sent.payload+26U,"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF",8U)==0);
}

static void web_request(uint16_t flow,uint8_t op,uint8_t channel,uint8_t value,uint8_t id)
{
    request(WEB_REQUEST,flow,0U,0U);event.data.lora_message.version=4U;event.data.lora_message.source_role=LORA_ROLE_SLAVE;event.data.lora_message.source_group=1U;
    event.data.lora_message.payload_length=11U;event.data.lora_message.payload[0]=op;event.data.lora_message.payload[1]=channel;event.data.lora_message.payload[2]=value;
    memset(event.data.lora_message.payload+3,id,8U);
    assert(LoRaProtocol_ValidateMessage(&event.data.lora_message)==LORA_PROTOCOL_OK);
}
static void test_web_control_arbitration(void)
{
    uint16_t flow;uint8_t id;
    for(id=1U;id<=4U;id++)
    {reset(0U);web_request(1U,0x10U,id,75U,id);process(0U);assert(fan_count==1U&&last_channel==id&&last_duty==75U);assert(sent.type==WEB_RESULT&&sent.payload[8]==WEB_OK);web_request(1U,0x10U,id,75U,id);process(1U);assert(fan_count==1U);web_request(1U,0x10U,id,20U,id);process(2U);assert(sent.payload[8]==WEB_FAILED&&fan_count==1U);}
    reset(0U);web_request(1U,0x11U,4U,1U,9U);process(0U);flow=slave_flow;assert(sent.type==LORA_MSG_SET_WINDOW&&sent.payload[0]==4U);
    send_slave_query(50U);request(LORA_MSG_SET_WINDOW,1U,1U,0U);process(51U);assert_window_ack(1U,WEB_BUSY); /* Same flow, different owner. */
    window_ack(flow,WEB_OK);process(52U);assert(sent.type==WEB_RESULT&&sent.payload[8]==WEB_OK&&sent.payload[0]==9U);
    web_request(1U,0x11U,4U,1U,9U);process(53U);assert(sent.type==WEB_RESULT&&MasterRuntimeDiag.window_command_count==2U);
    reset(0U);request(LORA_MSG_READ_TELEMETRY,1U,0U,0U);process(0U);send_slave_query(50U);
    web_request(2U,0x10U,1U,50U,10U);process(51U);assert(fan_count==0U&&MasterRuntimeDiag.web_deferred==1U);
    process(550U);assert(sent.type==WEB_RESULT&&sent.payload[8]==WEB_OK&&sent.flow_id==2U&&fan_count==1U);
    reset(0U);web_request(1U,0x11U,1U,1U,11U);process(0U);send_slave_query(50U);process(550U);assert(sent.type==WEB_RESULT&&sent.payload[8]==WEB_UNKNOWN);
    reset(0U);web_request(1U,0x10U,1U,50U,12U);event.received_tick=0U;preserve_received_tick=1U;process(1000U);assert(fan_count==0U&&sent.payload[8]==WEB_UNKNOWN);
}
static void test_web_wait_limits(void)
{
    uint32_t start;uint8_t boundary;
    for(boundary=0U;boundary<2U;boundary++)
    {
        reset(0U);request(LORA_MSG_READ_TELEMETRY,1U,0U,0U);process(0U);
        web_request(2U,0x10U,1U,75U,30U);process(1U);fail_send=1U;process(1001U);
        assert(fan_count==0U);fail_send=0U;process(boundary?2001U:2000U);
        assert(fan_count==(boundary?0U:1U));
        assert(MasterRuntimeDiag.web_wait_timeout==(boundary?1U:0U));
        web_request(2U,0x10U,1U,75U,30U);process(2002U);
        assert(sent.type==WEB_RESULT&&sent.payload[8]==(boundary?WEB_BUSY:WEB_OK));
        assert(fan_count==(boundary?0U:1U));
    }
    reset(0U);request(LORA_MSG_READ_TELEMETRY,1U,0U,0U);process(0U);send_slave_query(50U);
    web_request(2U,0x11U,4U,1U,31U);process(51U);
    web_request(2U,0x11U,4U,1U,31U);process(52U);assert(MasterRuntimeDiag.web_deferred==1U);
    web_request(3U,0x11U,4U,0U,31U);process(53U);assert(sent.payload[8]==WEB_FAILED);
    web_request(4U,0x10U,1U,75U,32U);process(54U);assert(sent.payload[8]==WEB_BUSY);
    process(550U);request(LORA_MSG_READ_TELEMETRY,99U,0U,0U);process(551U);
    assert(MasterRuntimeDiag.telemetry_request_count==2U&&MasterRuntimeDiag.busy_reject_count==1U);
    assert(MasterRuntimeDiag.window_request_queued_count==1U&&fan_count==0U);
    start=0xFFFFFF00U;reset(start);request(LORA_MSG_READ_TELEMETRY,1U,0U,0U);process(start);
    web_request(2U,0x10U,4U,100U,33U);process(start+1U);process(start+1000U);
    assert(fan_count==1U&&last_channel==4U&&MasterRuntimeDiag.web_wait_timeout==0U);
}
int main(void)
{
    {
        unsigned i;
        reset(0U); request(LORA_MSG_READ_TELEMETRY, 1234U, 0U, 0U); process(0U); send_slave_query(50U);
        /* Slave absent: local microphone remains available, MQ/US do not. */
        audio_valid=1U; audio_sample.tick=500U;
        for(i=0U;i<5U;i++) audio_sample.peak_to_peak[i]=(uint16_t)(i*100U);
        process(550U);
        assert(sent.payload_length==46U && sent.payload[0]==0x73U);
        for(i=0U;i<5U;i++) assert(Max4466Wire_U16(sent.payload+18U+2U*i)==i*100U);
        assert(Max4466Wire_U16(sent.payload+28U)==50U && Max4466Wire_U16(sent.payload+30U)==65535U);
        assert(LoRaProtocol_ValidateMessage(&sent)==LORA_PROTOCOL_OK);
    }
    test_web_wait_limits();
    test_web_control_arbitration();
    test_ultrasonic_event_delay_and_retry();
    test_status_without_control_room();
    test_mq2_event_delay_and_aggregate_retry();
    test_conversion_and_cache();
    test_four_fan_commands_and_ack();
    test_rain_is_local_and_does_not_control_actuators();
    test_failure_retry_and_recovery();
    test_timeout_trigger_error_and_range_check();
    test_tick_wrap();
    test_cached_reply_and_placeholder();
    test_force_sample_and_fan_during_conversion();
    test_failed_force_and_queue_retry();
    test_busy_force_and_unsolicited_slave();
    test_rounding_and_measurement_boundaries();
    test_five_minute_virtual_run();
    test_address_filter_late_reply_queue_timeout_and_wrap();
    test_extended_audio_and_aggregate_expiry();
    test_window_forward_and_real_ack();
    test_window_wrong_reply_rejection();
    test_window_busy_does_not_replace_telemetry_or_fan();
    test_window_queue_rx_timeout_and_wrap();
    test_window_final_ack_queue_failure_keeps_result();
    test_window_then_forced_telemetry_is_independent();
    test_window_slave_flow_wrap_and_malformed_request();
    puts("26 master BME/runtime/MQ2/ultrasonic/window/rain/status test groups passed (including four fans, audio and 5-minute virtual run)");
    return 0;
}
