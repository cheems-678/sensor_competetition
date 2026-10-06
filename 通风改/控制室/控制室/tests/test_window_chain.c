#include "slave_web_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Bsp/gateway_runtime.h"
/* Distinct ASCII header names keep MinGW independent of source-path encoding.
   slave_protocol_runtime.c is separately compiled with its own HAL/lora stubs. */
#include "lora_protocol.h"
#include "master_runtime.h"
#include "master_queues.h"
#include "master_bme280.h"
#include "master_rain.h"
#include "master_light_control.h"
#include "slave_protocol_runtime.h"
#include "slave_bme280.h"
#include "slave_acoustic.h"
#include "slave_mq2.h"
#include "slave_hcsr04.h"
#include "slave_servo_test.h"
#include "slave_master_status.h"
#include "sg90_test_pwm.h"

#define CHECK(condition) do { checks++; assert(condition); } while (0)
static unsigned checks, pc_count, gateway_downlinks, master_downlinks, stale_drops;
static unsigned pwm_starts, pwm_updates, pwm_stops;
static uint32_t tick;
static uint16_t pwm_pulse, last_slave_flow;
static uint8_t slave_online, lose_ack, pwm_ok, hold_master_tx;
static uint8_t master_rain_state;
static LoRaMessage last_pc;
volatile MasterBme280Diagnostics MasterBme280Diag;
volatile MasterLightDiagnostics MasterLightDiag;
uint8_t FanPwm_GetDuty(uint8_t channel) { return (uint8_t)((channel-1U)*25U); }
static uint8_t send_status,lose_web_result;
static unsigned web_fan_count;
volatile SlaveBme280Diagnostics SlaveBme280Diag;
volatile SlaveMq2Diagnostics SlaveMq2Diag;
volatile SlaveHcsr04Diagnostics SlaveHcsr04Diag;
static SlaveHcsr04Sample ultrasonic_sample;
uint8_t SlaveHcsr04_GetSample(uint32_t now, SlaveHcsr04Sample *sample)
{
    if (!SlaveHcsr04Diag.valid || (uint32_t)(now - ultrasonic_sample.tick) >= 2000U) { return 0U; }
    *sample = ultrasonic_sample; return 1U;
}
static uint8_t mq2_valid;
static uint32_t mq2_tick;
uint8_t SlaveMq2_GetSample(uint32_t now, SlaveMq2Sample *sample)
{
    if (!mq2_valid || (uint32_t)(now - mq2_tick) >= 2000U) { return 0U; }
    sample->raw = 1241U; sample->pa7_mv = 1000U; sample->ao_mv = 2000U;
    sample->tick = mq2_tick; sample->sequence = 1U;
    return 1U;
}

uint32_t HAL_GetTick(void) { return tick; }
void MasterLight_Init(uint32_t now) { (void)now; }
void MasterLight_Process(uint32_t now) { (void)now; }
void MasterRain_Init(uint32_t now)
{ (void)now; master_rain_state = MASTER_RAIN_STATE_UNKNOWN; }
void MasterRain_Process(uint32_t now) { (void)now; }
uint8_t MasterRain_GetState(uint32_t now) { (void)now; return master_rain_state; }
uint8_t FanPwm_SetDuty(uint8_t channel, uint8_t duty)
{web_fan_count++; return (channel >=1U && channel<=4U && duty<=100U)?1U:0U;}
void MasterBme280_Init(uint32_t now)
{ memset((void *)&MasterBme280Diag, 0, sizeof(MasterBme280Diag)); MasterBme280Diag.sample_tick = now; }
void MasterBme280_Process(uint32_t now) { MasterBme280Diag.sample_tick = now; }
void MasterBme280_RequestSample(uint32_t now)
{ MasterBme280Diag.sample_tick = now; MasterBme280Diag.completed_count++; }
uint8_t MasterBme280_GetSample(uint32_t now, MasterBme280Sample *sample)
{ (void)now; sample->temperature_x10 = 201; sample->humidity_x10 = 402U; sample->pressure_pa = 101101U; return 1U; }
void SlaveBme280_Init(uint32_t now)
{ memset((void *)&SlaveBme280Diag, 0, sizeof(SlaveBme280Diag)); SlaveBme280Diag.sample_valid = 1U; SlaveBme280Diag.sample_tick = now; }
void SlaveBme280_Process(uint32_t now) { SlaveBme280Diag.sample_tick = now; }
void SlaveBme280_RequestSample(uint32_t now)
{ SlaveBme280Diag.sample_tick = now; SlaveBme280Diag.completed_count++; }
uint8_t SlaveBme280_GetSample(uint32_t now, SlaveBme280Sample *sample)
{ (void)now; sample->temperature_x10 = 123; sample->humidity_x10 = 456U; sample->pressure_pa = 100123U; return 1U; }
uint8_t SlaveAcoustic_GetLatest(uint32_t now, SlaveAcousticSnapshot *snapshot)
{ snapshot->rms_left = 321U; snapshot->rms_right = 654U; snapshot->window_tick = now; snapshot->validity_epoch = 1U; return 1U; }
uint8_t SlaveAcoustic_IsSnapshotValid(uint32_t now, const SlaveAcousticSnapshot *snapshot)
{ return ((uint32_t)(now - snapshot->window_tick) < 300U) ? 1U : 0U; }
static uint16_t extra_pulses[3];
static unsigned extra_updates[3];
static uint8_t extra_success[3] = {1U, 1U, 1U};
uint8_t Sg90TestPwm_StartChannel(uint8_t id, uint16_t pulse)
{
    if (id < 2U || id > 4U) { return 0U; }
    extra_pulses[id - 2U] = pulse;
    extra_updates[id - 2U] = 0U;
    extra_success[id - 2U] = 1U;
    return 1U;
}
uint8_t Sg90TestPwm_SetChannelPulse(uint8_t id, uint16_t pulse)
{
    if (id < 2U || id > 4U || !extra_success[id - 2U]) { return 0U; }
    extra_pulses[id - 2U] = pulse;
    extra_updates[id - 2U]++;
    return 1U;
}
void Sg90TestPwm_StopChannel(uint8_t id)
{
    if (id >= 2U && id <= 4U) { extra_pulses[id - 2U] = 0U; }
}
uint8_t Sg90TestPwm_IsChannelRunning(uint8_t id)
{
    if (id == 1U) { return (uint8_t)(pwm_pulse != 0U); }
    return (uint8_t)(id >= 2U && id <= 4U && extra_pulses[id - 2U] != 0U);
}

uint8_t Sg90TestPwm_Start(uint16_t pulse)
{ CHECK(pulse == 1500U); pwm_starts++; pwm_pulse = pulse; return 1U; }
uint8_t Sg90TestPwm_SetPulse(uint16_t pulse)
{ CHECK(pulse == 1300U || pulse == 1500U || pulse == 1700U); pwm_updates++; if (!pwm_ok) { return 0U; } pwm_pulse = pulse; return 1U; }
void Sg90TestPwm_Stop(void) { pwm_stops++; pwm_pulse = 0U; }

static void receive_master(const uint8_t *frame, uint16_t length)
{
    MasterEvent event;
    memset(&event, 0, sizeof(event));
    CHECK(LoRaProtocol_Decode(frame, length, &event.data.lora_message) == LORA_PROTOCOL_OK);
    event.type = MASTER_EVENT_LORA_MESSAGE;
    event.received_tick = tick;
    CHECK(MasterQueues_SendEvent(&event) != 0U);
}
uint8_t LORA_SendData(const uint8_t *frame, uint16_t length)
{
    LoRaMessage message;
    CHECK(LoRaProtocol_Decode(frame, length, &message) == LORA_PROTOCOL_OK);
    CHECK(message.source_role == LORA_ROLE_SLAVE && message.destination_role == LORA_ROLE_MASTER);
    CHECK(message.source_group == 1U && message.destination_group == 1U);
    if(message.type!=WEB_REQUEST){CHECK(message.flow_id == last_slave_flow);}
    if (message.type == LORA_MSG_TELEMETRY)
    { CHECK(message.payload[17] == 0xFFU); } /* Rain belongs to the master. */
    if (lose_ack && message.type == LORA_MSG_ACK) { return 1U; }
    receive_master(frame, length);
    return 1U;
}
static uint8_t gateway_send(GatewayOutputPort port, const uint8_t *frame,
                             uint16_t length, void *context)
{
    (void)context;
    if (port == GATEWAY_OUTPUT_LORA)
    { gateway_downlinks++; receive_master(frame, length); }
    else
    { CHECK(LoRaProtocol_Decode(frame, length, &last_pc) == LORA_PROTOCOL_OK); pc_count++; }
    return 1U;
}
static void master_transmit(void)
{
    LoRaMessage message;
    uint8_t frame[LORA_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t length, i;
    if (hold_master_tx) { return; }
    while (MasterRuntime_CanTransmit() && MasterQueues_ReceiveLoRa(&message))
    {
        if (message.destination_role == LORA_ROLE_SLAVE && (message.type==LORA_MSG_SET_WINDOW||message.type==LORA_MSG_READ_TELEMETRY) &&
            !MasterRuntime_IsSlaveRequestCurrent(message.type, message.flow_id))
        { stale_drops++; continue; }
        CHECK(LoRaProtocol_Encode(&message, frame, sizeof(frame), &length) == LORA_PROTOCOL_OK);
        if (message.destination_role == LORA_ROLE_SLAVE)
        {
            CHECK(message.source_role == LORA_ROLE_MASTER);
            CHECK(message.source_group == 1U && message.destination_group == 1U);
            master_downlinks++; last_slave_flow = message.flow_id;
            if (slave_online && !(lose_web_result&&message.type==WEB_RESULT))
            { for (i = 0U; i < length; i++) { SlaveRuntime_PushRxByteFromIsr(frame[i]); } }
            if(message.type==WEB_RESULT){for(i=0U;i<length;i++)GatewayRuntime_PushLoRaByteFromIsr(frame[i]);}
            else MasterRuntime_NotifySlaveRequestSent(message.flow_id, tick);
        }
        else
        { for (i = 0U; i < length; i++) { GatewayRuntime_PushLoRaByteFromIsr(frame[i]); } }
    }
    if (send_status && MasterRuntime_PrepareStatus(&message, tick))
    {
        CHECK(LoRaProtocol_Encode(&message, frame, sizeof(frame), &length) == LORA_PROTOCOL_OK);
        for (i = 0U; i < length; i++)
        {
            if (slave_online) { SlaveRuntime_PushRxByteFromIsr(frame[i]); }
            GatewayRuntime_PushLoRaByteFromIsr(frame[i]); /* shared radio, ignored by gateway */
        }
    }
}
static void pump(void)
{
    GatewayRuntime_Process(tick);
    MasterRuntime_ProcessOne(tick); master_transmit();
    if (slave_online) { SlaveRuntime_Process(tick); SlaveServoTest_Process(tick); }
    MasterRuntime_ProcessOne(tick); master_transmit();
    GatewayRuntime_Process(tick);
}
static void advance(unsigned milliseconds)
{ unsigned i; for (i = 0U; i < milliseconds; i++) { tick++; pump(); } }
static void reset(uint32_t start)
{
    tick = start;
    lose_web_result=0U;web_fan_count=0U;
    send_status = 0U; memset((void *)&MasterLightDiag, 0, sizeof(MasterLightDiag));
    mq2_valid = 0U; mq2_tick = start;
    memset((void *)&SlaveHcsr04Diag, 0, sizeof(SlaveHcsr04Diag));
    memset((void *)&SlaveMq2Diag, 0, sizeof(SlaveMq2Diag));
    pc_count = gateway_downlinks = master_downlinks = stale_drops = 0U;
    pwm_starts = pwm_updates = pwm_stops = 0U;
    slave_online = pwm_ok = 1U; lose_ack = hold_master_tx = 0U;
    memset(&last_pc, 0, sizeof(last_pc));
    CHECK(MasterQueues_Init() != 0U);
    MasterRuntime_Init(); SlaveRuntime_Init(1U); SlaveServoTest_InitManual(tick);
    GatewayRuntime_Init(gateway_send, NULL);
    CHECK(pwm_pulse == 1500U && SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL);
}
static void command_channel(uint8_t type, uint16_t flow, uint8_t action, uint8_t servo_id)
{
    LoRaMessage message;
    uint8_t frame[LORA_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t length, i;
    memset(&message, 0, sizeof(message));
    message.version = LORA_PROTOCOL_VERSION; message.type = type;
    message.source_role = LORA_ROLE_CONTROL_ROOM; message.source_group = 0U;
    message.destination_role = LORA_ROLE_MASTER; message.destination_group = 1U;
    message.flow_id = flow;
    message.payload_length = (type == LORA_MSG_SET_WINDOW) ? 2U : 1U;
    message.payload[0] = (type == LORA_MSG_SET_WINDOW) ? servo_id : action;
    message.payload[1] = action;
    CHECK(LoRaProtocol_Encode(&message, frame, sizeof(frame), &length) == LORA_PROTOCOL_OK);
    for (i = 0U; i < length; i++) { GatewayRuntime_PushPcByteFromIsr(frame[i]); }
    pump();
}
static void command(uint8_t type, uint16_t flow, uint8_t action)
{ command_channel(type, flow, action, 1U); }
static void check_ack(uint16_t flow, uint8_t status)
{
    CHECK(last_pc.type == LORA_MSG_ACK && last_pc.flow_id == flow);
    CHECK(last_pc.source_role == LORA_ROLE_MASTER && last_pc.source_group == 1U);
    CHECK(last_pc.destination_role == LORA_ROLE_CONTROL_ROOM && last_pc.destination_group == 0U);
    CHECK(last_pc.payload_length == 2U && last_pc.payload[0] == LORA_MSG_SET_WINDOW);
    CHECK(last_pc.payload[1] == status);
}
static uint32_t read_u32(const uint8_t *data)
{ return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U); }
static void check_telemetry(uint16_t flow)
{
    CHECK(last_pc.type == LORA_MSG_TELEMETRY && last_pc.flow_id == flow);
    CHECK(last_pc.payload_length == 42U && last_pc.payload[0] == 0x3FU);
    CHECK(last_pc.payload[1] == 201U && last_pc.payload[2] == 0U);
    CHECK(read_u32(&last_pc.payload[5]) == 101101U);
    CHECK(last_pc.payload[9] == 123U && last_pc.payload[10] == 0U);
    CHECK(last_pc.payload[11] == 0xC8U && last_pc.payload[12] == 1U);
    CHECK(read_u32(&last_pc.payload[13]) == 100123U);
    CHECK(last_pc.payload[17] == 0xFFU);
    CHECK(read_u32(&last_pc.payload[18]) == 321U && read_u32(&last_pc.payload[22]) == 654U);
}
static void normal_chain(void)
{
    reset(100U); advance(10000U);
    CHECK(pwm_starts == 1U && pwm_updates == 0U && pwm_pulse == 1500U);
    command(LORA_MSG_SET_WINDOW, 0x100U, 1U);
    CHECK(pwm_pulse == 1700U && pc_count == 0U && last_slave_flow != 0x100U);
    advance(49U); CHECK(pc_count == 0U); advance(1U); check_ack(0x100U, 0U);
    CHECK(pwm_pulse == 1700U && SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL_RUNNING);
    advance(249U);
    CHECK(pwm_pulse == 1700U && pwm_updates == 1U && pc_count == 1U);
    advance(1U);
    CHECK(pwm_pulse == 1500U && pwm_updates == 2U && pc_count == 1U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL && pwm_stops == 0U);
    command(LORA_MSG_SET_WINDOW, 0x101U, 0U); advance(50U); check_ack(0x101U, 0U);
    CHECK(pwm_pulse == 1300U);
    advance(249U); CHECK(pwm_pulse == 1300U && pwm_updates == 3U);
    advance(1U); CHECK(pwm_pulse == 1500U && pwm_updates == 4U);
    /* A new command after stopping starts even when its direction is unchanged. */
    command(LORA_MSG_SET_WINDOW, 0x102U, 0U); advance(50U); check_ack(0x102U, 0U);
    CHECK(pwm_pulse == 1300U && pwm_starts == 1U && pwm_updates == 5U);
    advance(250U); CHECK(pwm_pulse == 1500U && pwm_updates == 6U);
    CHECK(gateway_downlinks == 3U && master_downlinks == 3U && pc_count == 3U);
    puts("PASS startup/open/close/restart stop at 300 ms and multihop flow");
}
static void same_direction_repeat(void)
{
    uint16_t first_slave_flow;
    reset(0U);
    command(LORA_MSG_SET_WINDOW, 0x110U, 1U);
    first_slave_flow = last_slave_flow;
    advance(50U); check_ack(0x110U, 0U); advance(149U);
    command(LORA_MSG_SET_WINDOW, 0x111U, 1U);
    CHECK(last_slave_flow != first_slave_flow);
    CHECK(pwm_pulse == 1700U && pwm_updates == 1U);
    CHECK(SlaveServoTestDiag.last_update_tick == 0U && SlaveServoTestDiag.update_count == 1U);
    advance(50U); check_ack(0x111U, 0U); advance(50U);
    CHECK(tick == 299U && pwm_pulse == 1700U && pwm_updates == 1U);
    advance(1U);
    CHECK(pwm_pulse == 1500U && pwm_updates == 2U && pc_count == 2U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL && SlaveServoTestDiag.last_update_tick == 300U);
    puts("PASS different-flow same-direction repeat preserves the original deadline");
}
static void reverse_restarts_timer(void)
{
    reset(0U);
    command(LORA_MSG_SET_WINDOW, 0x120U, 1U); advance(50U); check_ack(0x120U, 0U);
    advance(100U); command(LORA_MSG_SET_WINDOW, 0x121U, 0U);
    CHECK(pwm_pulse == 1300U && pwm_updates == 2U);
    CHECK(SlaveServoTestDiag.last_update_tick == 150U && SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL_RUNNING);
    advance(50U); check_ack(0x121U, 0U); advance(100U);
    CHECK(tick == 300U && pwm_pulse == 1300U && pwm_updates == 2U);
    advance(149U); CHECK(tick == 449U && pwm_pulse == 1300U && pwm_updates == 2U);
    advance(1U);
    CHECK(pwm_pulse == 1500U && pwm_updates == 3U && pc_count == 2U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL && SlaveServoTestDiag.last_update_tick == 450U);
    puts("PASS reverse command changes PWM and starts a new 300 ms deadline");
}
static void telemetry_isolation(void)
{
    reset(0U);
    command(LORA_MSG_READ_TELEMETRY, 0x200U, 1U);
    command(LORA_MSG_SET_WINDOW, 0x201U, 1U); /* Gateway queues behind telemetry. */
    advance(50U); check_telemetry(0x200U); CHECK(pwm_pulse == 1500U && pc_count == 1U);
    advance(51U); check_ack(0x201U, 0U);
    command(LORA_MSG_READ_TELEMETRY, 0x202U, 0U); advance(50U); check_telemetry(0x202U);
    CHECK(pwm_pulse == 1700U && pc_count == 3U && MasterRuntimeDiag.slave_response_match_count == 2U);
    puts("PASS telemetry/window/telemetry keep CRC and sensor fields separate");
}
static void rain_telemetry_chain(void)
{
    unsigned online, rain;
    for (online = 0U; online <= 1U; online++)
    {
        for (rain = 0U; rain <= 1U; rain++)
        {
            uint16_t flow = (uint16_t)(0x700U + online * 2U + rain);
            reset(0U);
            slave_online = (uint8_t)online;
            master_rain_state = (uint8_t)rain;
            command(LORA_MSG_READ_TELEMETRY, flow, 0U);
            advance(online ? 50U : 500U);
            CHECK(pc_count == 1U && last_pc.type == LORA_MSG_TELEMETRY);
            CHECK(last_pc.flow_id == flow && last_pc.payload_length == 42U);
            CHECK(last_pc.payload[0] == (online ? 0x3FU : 0x3BU));
            CHECK(last_pc.payload[17] == rain);
            CHECK(last_pc.payload[1] == 201U && last_pc.payload[2] == 0U);
            CHECK(read_u32(&last_pc.payload[5]) == 101101U);
            if (online)
            {
                CHECK(last_pc.payload[9] == 123U && last_pc.payload[10] == 0U);
                CHECK(read_u32(&last_pc.payload[13]) == 100123U);
                CHECK(read_u32(&last_pc.payload[18]) == 321U);
                CHECK(read_u32(&last_pc.payload[22]) == 654U);
            }
            else
            {
                CHECK(last_pc.payload[9] == 0U && last_pc.payload[10] == 0x80U);
                CHECK(last_pc.payload[11] == 0xFFU && last_pc.payload[12] == 0xFFU);
                CHECK(read_u32(&last_pc.payload[13]) == 0xFFFFFFFFU);
                CHECK(read_u32(&last_pc.payload[18]) == 0xFFFFFFFFU);
                CHECK(read_u32(&last_pc.payload[22]) == 0xFFFFFFFFU);
            }
            CHECK(pwm_pulse == 1500U && pwm_updates == 0U);
        }
    }
    puts("PASS local dry/wet telemetry survives master/gateway and slave disconnection");
}
static void unknown_results(void)
{
    reset(0U); slave_online = 0U;
    command(LORA_MSG_SET_WINDOW, 0x300U, 1U); advance(499U); CHECK(pc_count == 0U);
    advance(1U); check_ack(0x300U, 2U); CHECK(pwm_pulse == 1500U && pwm_updates == 0U);
    reset(0U); lose_ack = 1U;
    command(LORA_MSG_SET_WINDOW, 0x301U, 1U); advance(299U);
    CHECK(pwm_pulse == 1700U && pwm_updates == 1U && pc_count == 0U);
    advance(1U);
    CHECK(pwm_pulse == 1500U && pwm_updates == 2U && pc_count == 0U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL && SlaveRuntimeDiag.reply_count == 1U);
    advance(199U); CHECK(pc_count == 0U); advance(1U); check_ack(0x301U, 2U);
    CHECK(pwm_pulse == 1500U && pwm_updates == 2U && pc_count == 1U);
    puts("PASS lost ACK stops locally at 300 ms but reports unknown at 500 ms");
}
static void driver_failure(void)
{
    reset(0U); pwm_ok = 0U;
    command(LORA_MSG_SET_WINDOW, 0x400U, 0U); advance(50U); check_ack(0x400U, 1U);
    CHECK(pwm_pulse == 0U && pwm_stops == 1U && SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT);
    command(LORA_MSG_READ_TELEMETRY, 0x401U, 0U); advance(50U); check_telemetry(0x401U);
    puts("PASS PWM fault returns driver error without stopping telemetry");
}
static void stop_driver_failure(void)
{
    reset(0U);
    command(LORA_MSG_SET_WINDOW, 0x410U, 1U); advance(50U); check_ack(0x410U, 0U);
    pwm_ok = 0U;
    advance(249U); CHECK(pwm_pulse == 1700U && pwm_updates == 1U && pwm_stops == 0U);
    advance(1U);
    CHECK(pwm_pulse == 0U && pwm_updates == 2U && pwm_stops == 1U && pc_count == 1U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT && SlaveServoTestDiag.last_error == SLAVE_SERVO_TEST_ERROR_UPDATE);
    check_ack(0x410U, 0U); /* The prior ACK confirmed the action, not its eventual stop. */
    command(LORA_MSG_READ_TELEMETRY, 0x411U, 0U); advance(50U); check_telemetry(0x411U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT && pc_count == 2U);
    command(LORA_MSG_SET_WINDOW, 0x412U, 0U); advance(50U); check_ack(0x412U, 1U);
    CHECK(pwm_updates == 2U && pwm_stops == 1U);
    puts("PASS failed stop faults PWM while 1F telemetry and prior action ACK remain valid");
}
static void stale_queue(void)
{
    reset(0U); hold_master_tx = 1U;
    command(LORA_MSG_SET_WINDOW, 0x500U, 1U); advance(1000U);
    CHECK(pwm_updates == 0U && pc_count == 0U && MasterRuntimeDiag.window_timeout_count == 1U);
    hold_master_tx = 0U; pump(); check_ack(0x500U, 2U); advance(100U);
    CHECK(stale_drops == 1U && master_downlinks == 0U && pwm_pulse == 1500U);
    puts("PASS expired queued command is cancelled before execution");
}
static void tick_wrap(void)
{
    reset(0xFFFFFFE0UL);
    command(LORA_MSG_SET_WINDOW, 0x600U, 1U); advance(50U); check_ack(0x600U, 0U);
    CHECK(pwm_pulse == 1700U && tick == 18U);
    advance(249U); CHECK(tick == 267U && pwm_pulse == 1700U && pwm_updates == 1U);
    advance(1U);
    CHECK(tick == 268U && pwm_pulse == 1500U && pwm_updates == 2U && pc_count == 1U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL && SlaveServoTestDiag.last_update_tick == 268U);
    reset(0xFFFFFFE0UL); slave_online = 0U;
    command(LORA_MSG_SET_WINDOW, 0x601U, 0U); advance(500U); check_ack(0x601U, 2U);
    CHECK(pwm_pulse == 1500U);
    puts("PASS reply, 300 ms stop and timeout across HAL tick wrap");
}
static void four_channel_chain(void)
{
    uint8_t id;
    reset(0U);
    for (id = 1U; id <= 4U; id++)
    {
        command_channel(LORA_MSG_SET_WINDOW, (uint16_t)(200U + id), id & 1U, id);
        advance(50U); check_ack((uint16_t)(200U + id), 0U);
        CHECK(SlaveServoTest_GetDiagnostics(id)->pulse_us == ((id & 1U) ? 1700U : 1300U));
    }
    CHECK(pwm_pulse == 1700U && extra_pulses[0] == 1300U);
    CHECK(extra_pulses[1] == 1700U && extra_pulses[2] == 1300U);
    advance(99U); CHECK(pwm_pulse == 1700U);
    advance(1U); CHECK(pwm_pulse == 1500U && extra_pulses[0] == 1300U);
    advance(50U); CHECK(extra_pulses[0] == 1500U && extra_pulses[1] == 1700U);
    advance(50U); CHECK(extra_pulses[1] == 1500U && extra_pulses[2] == 1300U);
    advance(50U); CHECK(extra_pulses[2] == 1500U);
    CHECK(pc_count == 4U && gateway_downlinks == 4U && master_downlinks == 4U);
    puts("PASS four IDs routed intact through gateway/master/slave with independent deadlines");
}

static void mq2_chain(void)
{
    uint16_t age;
    reset(100U); mq2_valid = 1U; SlaveMq2Diag.valid = 1U;
    command(LORA_MSG_READ_TELEMETRY, 900U, 0U); advance(50U);
    check_telemetry(900U);
    CHECK(last_pc.payload[26] == (uint8_t)1241U && last_pc.payload[27] == (uint8_t)(1241U >> 8U));
    CHECK(last_pc.payload[28] == (uint8_t)1000U && last_pc.payload[30] == (uint8_t)2000U);
    age = (uint16_t)(last_pc.payload[32] | ((uint16_t)last_pc.payload[33] << 8U));
    CHECK(age == 50U);
    advance(1950U); command(LORA_MSG_READ_TELEMETRY, 901U, 0U); advance(50U);
    CHECK(last_pc.flow_id == 901U && last_pc.payload[0] == 0x3FU);
    CHECK(memcmp(&last_pc.payload[26], "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8U) == 0);
    CHECK(last_pc.payload[9] == 123U); /* MQ expiry leaves BME available. */
    reset(0xFFFFFFF0U); mq2_valid = 1U; SlaveMq2Diag.valid = 1U;
    command(LORA_MSG_READ_TELEMETRY, 902U, 0U); advance(50U);
    CHECK(last_pc.flow_id == 902U && last_pc.payload[32] == 50U && last_pc.payload[33] == 0U);
    puts("PASS MQ2 real three-board encoding/forwarding, independent expiry and tick wrap");
}

static void master_status_chain(void)
{
    SlaveMasterStatus status;
    unsigned initial_pc;
    reset(0U); send_status = 1U; master_rain_state = 1U;
    MasterLightDiag.input_valid = MasterLightDiag.output_valid = 1U;
    MasterLightDiag.stable_dark = MasterLightDiag.light_on = 1U;
    advance(1001U); SlaveMasterStatus_Get(tick, &status);
    CHECK(status.online && status.bme_valid && status.temperature_x10 == 201);
    CHECK(status.rain == 1U && status.dark == 1U && status.fan_pwm[3] == 75U);
    CHECK(pc_count == 0U && gateway_downlinks == 0U && SlaveRuntimeDiag.request_count == 0U);
    command(LORA_MSG_SET_WINDOW, 400U, 1U); advance(50U);
    CHECK(last_pc.type == LORA_MSG_ACK && last_pc.flow_id == 400U);
    advance(300U); CHECK(pwm_pulse == 1500U);
    master_rain_state = 0xFFU;
    command(LORA_MSG_READ_TELEMETRY, 401U, 0U); advance(50U); check_telemetry(401U);
    initial_pc = pc_count;
    advance(300000U); CHECK(pc_count == initial_pc);
    SlaveMasterStatus_Get(tick, &status); CHECK(status.online && status.bme_valid);
    send_status = 0U; advance(3000U); SlaveMasterStatus_Get(tick, &status); CHECK(!status.online);
    command(LORA_MSG_READ_TELEMETRY, 402U, 0U); advance(50U); check_telemetry(402U);
    reset(0xFFFFFFF0U); send_status = 1U; advance(1001U);
    SlaveMasterStatus_Get(tick, &status); CHECK(status.online && status.bme_valid);
    puts("PASS autonomous master status three-board chain, gateway isolation, control and expiry");
}

static void ultrasonic_chain(void)
{
    reset(100U); SlaveHcsr04Diag.valid=1U;
    ultrasonic_sample.tick=tick; ultrasonic_sample.distance_mm=250U;
    ultrasonic_sample.raw_mm=252U; ultrasonic_sample.pulse_us=1469U;
    command(LORA_MSG_READ_TELEMETRY,980U,0U); advance(50U);
    check_telemetry(980U);
    CHECK(last_pc.payload[34]==250U && last_pc.payload[36]==252U);
    CHECK((last_pc.payload[38]|((uint16_t)last_pc.payload[39]<<8U))==1469U);
    CHECK(last_pc.payload[40]==50U && last_pc.payload[41]==0U);
    advance(1950U); command(LORA_MSG_READ_TELEMETRY,981U,0U); advance(50U);
    CHECK(memcmp(last_pc.payload+34U,"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF",8U)==0);
    CHECK(last_pc.payload[9]==123U);
    SlaveHcsr04Diag.valid=1U; ultrasonic_sample.tick=tick;
    command(LORA_MSG_READ_TELEMETRY,982U,0U); advance(50U);
    CHECK(last_pc.payload[34]==250U);
    SlaveHcsr04Diag.valid=0U; command(LORA_MSG_READ_TELEMETRY,983U,0U); advance(50U);
    CHECK(last_pc.payload[34]==255U && last_pc.payload[35]==255U);
    reset(0xFFFFFFF0U); SlaveHcsr04Diag.valid=1U; ultrasonic_sample.tick=tick;
    command(LORA_MSG_READ_TELEMETRY,984U,0U); advance(50U);
    CHECK(last_pc.payload[40]==50U && last_pc.payload[34]==250U);
    puts("PASS ultrasonic real three-board measurements/fault/expiry/recovery/wrap");
}

static void web_chain(void)
{
    uint8_t p[11]={0x10U,1U,75U,1,2,3,4,5,6,7,8},state;unsigned i,count;
    reset(0U);send_status=1U;advance(1001U);
    for(i=1U;i<=4U;i++)
    {
        p[1]=(uint8_t)i;p[3]=(uint8_t)i;CHECK(SlaveWebControl_Submit(p,tick,&state)==202U);advance(100U);
        CHECK(SlaveWebControl_Status(p+3,tick)==WEB_OK);CHECK(web_fan_count==i);CHECK(pc_count==0U);
        CHECK(SlaveWebControl_Submit(p,tick,&state)==200U);advance(100U);CHECK(web_fan_count==i);
    }
    for(i=1U;i<=4U;i++)
    {
        p[0]=0x11U;p[1]=(uint8_t)i;p[2]=1U;p[3]=(uint8_t)(i+10U);
        CHECK(SlaveWebControl_Submit(p,tick,&state)==202U);advance(160U);
        CHECK(SlaveWebControl_Status(p+3,tick)==WEB_OK);CHECK(pc_count==0U);advance(300U);
        CHECK(i==1U?pwm_pulse==1500U:extra_pulses[i-2U]==1500U);
    }
    count=pc_count;p[3]=25U;lose_web_result=1U;CHECK(SlaveWebControl_Submit(p,tick,&state)==202U);advance(8000U);
    CHECK(SlaveWebControl_Status(p+3,tick)==WEB_UNKNOWN);CHECK(pc_count==count);lose_web_result=0U;
    p[3]=26U;lose_ack=1U;CHECK(SlaveWebControl_Submit(p,tick,&state)==202U);advance(700U);CHECK(SlaveWebControl_Status(p+3,tick)==WEB_UNKNOWN);lose_ack=0U;
    p[3]=27U;lose_ack=1U;command(LORA_MSG_SET_WINDOW,99U,0U);CHECK(SlaveWebControl_Submit(p,tick,&state)==202U);advance(650U);lose_ack=0U;
    CHECK(SlaveWebControl_Status(p+3,tick)==WEB_BUSY);check_ack(99U,WEB_UNKNOWN);
    p[3]=28U;CHECK(SlaveWebControl_Submit(p,tick,&state)==202U);for(i=0U;i<100U&&MasterRuntime_CanTransmit();i++)advance(1U);CHECK(!MasterRuntime_CanTransmit());command(LORA_MSG_SET_WINDOW,1U,1U);advance(160U);check_ack(1U,WEB_BUSY);
    advance(200U);CHECK(SlaveWebControl_Status(p+3,tick)==WEB_OK);
    advance(300000U);CHECK(web_fan_count==4U);
    puts("PASS web three-board: four fans/windows, duplicates, gateway isolation, busy, lost ACK/result and 5-minute monitoring");
}
int main(void)
{
    web_chain();
    ultrasonic_chain();
    master_status_chain();
    mq2_chain();
    four_channel_chain();
    normal_chain(); same_direction_repeat(); reverse_restarts_timer();
    telemetry_isolation(); rain_telemetry_chain(); unknown_results(); driver_failure();
    stop_driver_failure(); stale_queue(); tick_wrap();
    printf("window/MQ2/ultrasonic/status chain: 15 groups, %u checks, 0 failures\n", checks);
    return 0;
}
