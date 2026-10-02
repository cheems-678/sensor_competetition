#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "BME280.h"
#include "fan_pwm.h"
#include "master_bme280.h"
#include "master_config.h"
#include "master_ingress.h"
#include "master_light_control.h"
#include "master_queues.h"
#include "master_runtime.h"

static uint32_t tick;
static BME280_Status init_status, trigger_status, read_status;
static BME280_Data sensor_data;
static unsigned read_count, init_count, send_count, fan_count, light_count;
static uint8_t last_channel, last_duty, event_ready, fail_send;
static MasterEvent event;
static LoRaMessage sent;
static uint16_t slave_flow;

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

uint8_t FanPwm_SetDuty(uint8_t channel, uint8_t duty)
{
    fan_count++;
    last_channel = channel;
    last_duty = duty;
    return (channel >= 1U && channel <= 2U && duty <= 100U) ? 1U : 0U;
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
        assert(message->type == LORA_MSG_READ_TELEMETRY);
        slave_flow = message->flow_id;
    }
    sent = *message;
    send_count++;
    return 1U;
}

static void reset(uint32_t start)
{
    tick = start;
    init_status = trigger_status = read_status = BME280_OK;
    sensor_data.temperature_x100 = -555;
    sensor_data.humidity_x1024 = 51200U;
    sensor_data.pressure_pa = 101325U;
    read_count = init_count = send_count = fan_count = light_count = 0U;
    event_ready = fail_send = 0U;
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
    event.data.lora_message.payload_length = type == LORA_MSG_SET_FAN_SPEED ? 2U : 1U;
    event.data.lora_message.flow_id = flow;
    event.data.lora_message.payload[0] = value;
    event.data.lora_message.payload[1] = duty;
    event_ready = 1U;
}

static void process(uint32_t now)
{
    tick = now;
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
        3U, 0U, 0x80U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0U, 0x80U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU
    };
    assert(sent.type == LORA_MSG_TELEMETRY && sent.flow_id == flow);
    assert(sent.payload_length == 18U);
    if (valid == 0U) { assert(memcmp(sent.payload, invalid, sizeof(invalid)) == 0); }
    else
    {
        static const uint8_t expected[] = {
            3U, 0xC8U, 0xFFU, 0xF4U, 1U, 0xCDU, 0x8BU, 1U, 0U,
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
    assert(fan_count == 1U && last_channel == 2U && last_duty == 75U);
    assert(sent.type == LORA_MSG_ACK && sent.flow_id == 10U && sent.payload[1] == 0U);
    process(10U);
    assert(MasterBme280Diag.completed_count == 1U);
    assert(!MasterRuntime_CanTransmit());
    slave_reply(slave_flow, 0U);
    process(11U);
    assert(sent.payload[0] == 7U);
    sent.payload[0] = 3U; /* Online sensor failure still has invalid slave slots. */
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
    assert(sent.payload[0] == 7U);
    sent.payload[0] = 3U;
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
    sent.payload[0] = 3U;
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
    assert(sent.payload[0] == 7U && sent.payload[9] == 250U);
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

int main(void)
{
    test_conversion_and_cache();
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
    puts("11 master BME/runtime test groups passed (including 5-minute virtual run)");
    return 0;
}
