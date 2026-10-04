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
#include "slave_protocol_runtime.h"
#include "slave_bme280.h"
#include "slave_acoustic.h"
#include "slave_servo_test.h"
#include "sg90_test_pwm.h"

#define CHECK(condition) do { checks++; assert(condition); } while (0)
static unsigned checks, pc_count, gateway_downlinks, master_downlinks, stale_drops;
static unsigned pwm_starts, pwm_updates, pwm_stops;
static uint32_t tick;
static uint16_t pwm_pulse, last_slave_flow;
static uint8_t slave_online, lose_ack, pwm_ok, hold_master_tx;
static LoRaMessage last_pc;
volatile MasterBme280Diagnostics MasterBme280Diag;
volatile SlaveBme280Diagnostics SlaveBme280Diag;

uint32_t HAL_GetTick(void) { return tick; }
void MasterLight_Init(uint32_t now) { (void)now; }
void MasterLight_Process(uint32_t now) { (void)now; }
uint8_t FanPwm_SetDuty(uint8_t channel, uint8_t duty)
{ return ((channel == 1U || channel == 2U) && duty <= 100U) ? 1U : 0U; }
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
    CHECK(message.flow_id == last_slave_flow);
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
        if (message.destination_role == LORA_ROLE_SLAVE &&
            !MasterRuntime_IsSlaveRequestCurrent(message.type, message.flow_id))
        { stale_drops++; continue; }
        CHECK(LoRaProtocol_Encode(&message, frame, sizeof(frame), &length) == LORA_PROTOCOL_OK);
        if (message.destination_role == LORA_ROLE_SLAVE)
        {
            CHECK(message.source_role == LORA_ROLE_MASTER);
            CHECK(message.source_group == 1U && message.destination_group == 1U);
            master_downlinks++; last_slave_flow = message.flow_id;
            if (slave_online)
            { for (i = 0U; i < length; i++) { SlaveRuntime_PushRxByteFromIsr(frame[i]); } }
            MasterRuntime_NotifySlaveRequestSent(message.flow_id, tick);
        }
        else
        { for (i = 0U; i < length; i++) { GatewayRuntime_PushLoRaByteFromIsr(frame[i]); } }
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
    pc_count = gateway_downlinks = master_downlinks = stale_drops = 0U;
    pwm_starts = pwm_updates = pwm_stops = 0U;
    slave_online = pwm_ok = 1U; lose_ack = hold_master_tx = 0U;
    memset(&last_pc, 0, sizeof(last_pc));
    CHECK(MasterQueues_Init() != 0U);
    MasterRuntime_Init(); SlaveRuntime_Init(1U); SlaveServoTest_InitManual(tick);
    GatewayRuntime_Init(gateway_send, NULL);
    CHECK(pwm_pulse == 1500U && SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL);
}
static void command(uint8_t type, uint16_t flow, uint8_t action)
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
    message.payload[0] = (type == LORA_MSG_SET_WINDOW) ? 1U : action;
    message.payload[1] = action;
    CHECK(LoRaProtocol_Encode(&message, frame, sizeof(frame), &length) == LORA_PROTOCOL_OK);
    for (i = 0U; i < length; i++) { GatewayRuntime_PushPcByteFromIsr(frame[i]); }
    pump();
}
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
    CHECK(last_pc.payload_length == 26U && last_pc.payload[0] == 0x0FU);
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
    puts("PASS failed stop faults PWM while 0F telemetry and prior action ACK remain valid");
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
int main(void)
{
    normal_chain(); same_direction_repeat(); reverse_restarts_timer();
    telemetry_isolation(); unknown_results(); driver_failure();
    stop_driver_failure(); stale_queue(); tick_wrap();
    printf("window chain: 9 groups, %u checks, 0 failures\n", checks);
    return 0;
}
