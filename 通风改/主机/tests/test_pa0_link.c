#define MASTER_TEST_REAL_ACOUSTIC
#define main MasterLegacyTestsMain
#include "test_master_bme280.c"
#undef main
#include "max4466_adc.h"
/* Historical filename retained; this test now exercises all five channels. */

static uint8_t adc_mode;
static uint32_t adc_started;
static unsigned adc_starts;
uint8_t Max4466Adc_Start(void)
{ adc_starts++; adc_started = tick; return adc_mode != 1U; }
void Max4466Adc_Stop(void) {}
uint8_t Max4466Adc_Read(Max4466AdcWindow *w)
{
    if (adc_mode == 2U) { return 3U; }
    uint8_t ch;
    if ((uint32_t)(tick - adc_started) < MAX4466_CAPTURE_PERIOD_MS) { return 0U; }
    for (ch = 0U; ch < 5U; ch++) {
        w->minimum[ch] = 2000U; w->maximum[ch] = 2100U + ch * 100U;
        w->mean[ch] = 2050U + ch * 50U;
    }
    w->samples = MAX4466_CAPTURE_FRAMES;
    w->tick = adc_started + MAX4466_CAPTURE_DURATION_MS; w->sequence = 1U;
    adc_started = tick;
    return 1U;
}

int main(void)
{
    uint8_t mode;
    uint8_t frame[128];
    uint16_t frame_length, i;
    for (mode = 0U; mode < 3U; mode++) {
        adc_mode = mode;
        test_five_minute_virtual_run();
        test_window_forward_and_real_ack();
        test_window_queue_rx_timeout_and_wrap();
        test_web_control_arbitration();
        test_web_wait_limits();
    }
    adc_mode = 1U; reset(0U); process(0U);
    request(LORA_MSG_READ_TELEMETRY, 900U, 0U, 0U); process(1U);
    send_slave_query(50U); slave_reply(slave_flow, 1U); process(200U);
    assert(sent.type == LORA_MSG_TELEMETRY && sent.payload[18] == 0xFFU);
    adc_mode = 0U; process(1000U); process(1060U); process(1120U);
    request(LORA_MSG_READ_TELEMETRY, 901U, 0U, 0U); process(1121U);
    send_slave_query(1150U); slave_reply(slave_flow, 1U); process(1200U);
    assert(sent.type == LORA_MSG_TELEMETRY && sent.flow_id == 901U);
    assert(sent.payload_length == 46U && sent.payload[18] == 100U && sent.payload[19] == 0U);
    for (mode = 0U; mode < 5U; mode++) {
        uint16_t value = (uint16_t)(sent.payload[18U + mode * 2U] |
                         ((uint16_t)sent.payload[19U + mode * 2U] << 8U));
        assert(value == (mode + 1U) * 100U);
    }
    assert(LoRaProtocol_Encode(&sent, frame, sizeof(frame), &frame_length) == LORA_PROTOCOL_OK);
    assert(frame_length == 59U);
    printf("MAX4466_TELEMETRY ");
    for (i = 0U; i < frame_length; i++) { printf("%02X", frame[i]); }
    puts("");
    adc_mode = 2U; process(1201U);
    request(LORA_MSG_SET_FAN_SPEED, 902U, 1U, 0U); process(1202U);
    assert(sent.type == LORA_MSG_ACK && sent.flow_id == 902U && sent.payload[1] == 0U);
    assert(adc_starts > 0U);
    puts("Five-channel real service + runtime: three 5-minute runs, ADC init/fault/recovery, telemetry/window/web/fan passed");
    return 0;
}
