#include "slave_web_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "slave_esp_web.h"
#include "slave_bme280.h"
#include "slave_mq2.h"
#include "slave_hcsr04.h"
#include "slave_servo_test.h"
#include "slave_master_status.h"
#include "esp_at_uart.h"
#include "esp_ap_config.local.h"

volatile EspAtUartDiagnostics EspAtUartDiag;
volatile SlaveBme280Diagnostics SlaveBme280Diag;
volatile SlaveMq2Diagnostics SlaveMq2Diag;
static SlaveMq2Sample mq_sample;
static uint8_t mq_valid;
volatile SlaveHcsr04Diagnostics SlaveHcsr04Diag;
static SlaveHcsr04Sample ultrasonic;
static uint8_t ultrasonic_valid;
static uint32_t now;
static char rx[32768], responses[5][24576], last_tx[513];
static unsigned head, tail, response_length[5], raw_remaining, raw_id;
static unsigned tx_count, resets, closes, pads, max_tx, modern, ancient;
static unsigned ap_cur_count, ap_fallback_count;
static uint8_t suppress_prompt, suppress_send, suppress_at, early_close, tx_pending;
static uint8_t valid_sample, reject_gmr;
static uint16_t pulses[4];
static SlaveBme280Sample sample;

static void feed(const char *s)
{
    size_t n = strlen(s);
    assert(head + n < sizeof(rx));
    memcpy(&rx[head], s, n);
    head += (unsigned)n;
}
void EspAtUart_Init(void)
{ memset((void *)&EspAtUartDiag, 0, sizeof(EspAtUartDiag)); head = tail = 0U; }
void EspAtUart_Service(void) {}
void EspAtUart_ClearRx(void) { head = tail = 0U; }
uint8_t EspAtUart_Read(uint8_t *byte)
{
    if (tail == head) { head = tail = 0U; return 0U; }
    *byte = (uint8_t)rx[tail++]; return 1U;
}
void EspAtUart_AbortTx(void) { tx_pending = 0U; }
uint8_t EspAtUart_PollTx(void) { return tx_pending ? 0U : 1U; }
uint8_t EspAtUart_StartTx(const uint8_t *data, uint16_t size)
{
    unsigned i, id, n;
    assert(size <= 512U);
    tx_count++;
    if (size > max_tx) { max_tx = size; }
    memcpy(last_tx, data, size); last_tx[size] = '\0';
    for (i = 0U; i < size && data[i] == ' '; i++) {}
    if (i == size)
    {
        assert(size == 512U); pads++;
        raw_remaining = 0U;
        return 1U;
    }
    if (raw_remaining != 0U)
    {
        assert(size == raw_remaining);
        assert(response_length[raw_id] + size < sizeof(responses[0]));
        memcpy(&responses[raw_id][response_length[raw_id]], data, size);
        response_length[raw_id] += size;
        responses[raw_id][response_length[raw_id]] = '\0';
        raw_remaining = 0U;
        if (!suppress_send)
        {
            unsigned content_length = 0U;
            const char *header_end = strstr(responses[raw_id], "\r\n\r\n");
            const char *length_field = strstr(responses[raw_id], "Content-Length: ");
            if (early_close && header_end && length_field &&
                sscanf(length_field, "Content-Length: %u", &content_length) == 1 &&
                response_length[raw_id] == (unsigned)(header_end + 4 - responses[raw_id]) + content_length)
            {
                char closed[32];
                snprintf(closed, sizeof(closed), "%u,CLOSED\r\nSEND OK\r\n", raw_id);
                feed(closed);
            }
            else { feed("\r\nSEND OK\r\n"); }
        }
        return 1U;
    }
    if (strncmp(last_tx, "AT+CWSAP_CUR=", 13U) == 0)
    {
        assert(strcmp(last_tx, "AT+CWSAP_CUR=\"" SLAVE_ESP_SSID "\",\""
                      SLAVE_ESP_AP_PASSWORD "\",6,3\r\n") == 0);
        ap_cur_count++;
    }
    else if (strncmp(last_tx, "AT+CWSAP=", 9U) == 0)
    {
        assert(strcmp(last_tx, "AT+CWSAP=\"" SLAVE_ESP_SSID "\",\""
                      SLAVE_ESP_AP_PASSWORD "\",6,3\r\n") == 0);
        ap_fallback_count++;
    }
    if (sscanf(last_tx, "AT+CIPSEND=%u,%u", &id, &n) == 2)
    {
        assert(id < 5U && n > 0U && n <= 512U);
        raw_id = id; raw_remaining = n;
        if (!suppress_prompt) { feed("\r\nOK\r\n> "); }
    }
    else if (sscanf(last_tx, "AT+CIPCLOSE=%u", &id) == 1)
    {
        char closed[32]; closes++;
        snprintf(closed, sizeof(closed), "%u,CLOSED\r\nOK\r\n", id);
        feed(closed);
    }
    else if (strcmp(last_tx, "\r\nAT+RST\r\n") == 0)
    { assert(raw_remaining == 0U); resets++; feed("OK\r\nready\r\n"); }
    else if (strcmp(last_tx, "\r\nAT\r\n") == 0)
    { if (!suppress_at) { feed("\r\n\r\nERROR\r\nAT\r\n\r\nOK\r\n"); } }
    else if (strcmp(last_tx, "AT\r\n") == 0)
    { if (!suppress_at) { feed("AT\r\n\r\nOK\r\n"); } }
    else if (strcmp(last_tx, "AT+GMR\r\n") == 0)
    { feed(reject_gmr ? "ERROR\r\n" : "AT version:2.2.0.0\r\nSDK version:3.4\r\nOK\r\n"); }
    else if (modern && strstr(last_tx, "_CUR=") != 0) { feed("ERROR\r\n"); }
    else if (ancient && (strstr(last_tx, "_CUR=") != 0 || strcmp(last_tx, "AT+CWDHCP=1,2\r\n") == 0))
    { feed("ERROR\r\n"); }
    else { feed("OK\r\n"); }
    return 1U;
}
uint8_t SlaveBme280_GetSample(uint32_t tick, SlaveBme280Sample *out)
{
    if (!valid_sample || (uint32_t)(tick - SlaveBme280Diag.sample_tick) >= 2000U) { return 0U; }
    *out = sample; return 1U;
}
uint8_t SlaveMq2_GetSample(uint32_t tick, SlaveMq2Sample *out)
{
    if (!mq_valid || (uint32_t)(tick - mq_sample.tick) >= SLAVE_MQ2_MAX_AGE_MS) { return 0U; }
    *out = mq_sample; return 1U;
}
uint8_t SlaveHcsr04_GetSample(uint32_t tick, SlaveHcsr04Sample *out)
{
    if (!ultrasonic_valid || (uint32_t)(tick - ultrasonic.tick) >= SLAVE_HCSR04_MAX_AGE_MS) { return 0U; }
    *out = ultrasonic; return 1U;
}
uint8_t Sg90TestPwm_StartChannel(uint8_t id, uint16_t pulse)
{ assert(id >= 1U && id <= 4U); pulses[id - 1U] = pulse; return 1U; }
uint8_t Sg90TestPwm_SetChannelPulse(uint8_t id, uint16_t pulse)
{ assert(id >= 1U && id <= 4U); pulses[id - 1U] = pulse; return 1U; }
void Sg90TestPwm_StopChannel(uint8_t id) { pulses[id - 1U] = 0U; }
uint8_t Sg90TestPwm_IsChannelRunning(uint8_t id) { return pulses[id - 1U] != 0U; }
uint8_t Sg90TestPwm_Start(uint16_t pulse) { return Sg90TestPwm_StartChannel(1U, pulse); }
uint8_t Sg90TestPwm_SetPulse(uint16_t pulse) { return Sg90TestPwm_SetChannelPulse(1U, pulse); }
void Sg90TestPwm_Stop(void) { Sg90TestPwm_StopChannel(1U); }

static void step(unsigned count)
{
    while (count--)
    {
        now++;
        SlaveServoTest_Process(now);
        SlaveEspWeb_Process(now);
        SlaveServoTest_Process(now);
    }
}
static void reset(uint32_t start)
{
    SlaveMasterStatus_Init();
    SlaveWebControl_Init();
    now = start;
    memset(responses, 0, sizeof(responses));
    memset(response_length, 0, sizeof(response_length));
    memset((void *)&SlaveBme280Diag, 0, sizeof(SlaveBme280Diag));
    raw_remaining = tx_count = resets = closes = pads = max_tx = 0U;
    ap_cur_count = ap_fallback_count = 0U;
    suppress_prompt = suppress_send = suppress_at = early_close = tx_pending = reject_gmr = 0U;
    modern = ancient = 0U; valid_sample = 1U;
    sample.temperature_x10 = -125; sample.humidity_x10 = 0U; sample.pressure_pa = 100123U;
    SlaveBme280Diag.sample_tick = start + 1300U;
    SlaveBme280Diag.sample_success_count = 42U;
    ultrasonic_valid = 1U;
    memset((void *)&SlaveHcsr04Diag, 0, sizeof(SlaveHcsr04Diag));
    ultrasonic.raw_mm = 300U; ultrasonic.distance_mm = 299U;
    ultrasonic.pulse_us = 1749U; ultrasonic.filter_count = 5U;
    ultrasonic.tick = start + 1300U; ultrasonic.sequence = 7U;
    SlaveHcsr04Diag.sample_count = 7U;
    mq_valid = 1U;
    mq_sample.raw = 2048U; mq_sample.pa7_mv = 1650U; mq_sample.ao_mv = 3301U;
    mq_sample.tick = start + 1300U; mq_sample.sequence = 123U;
    SlaveMq2Diag.sample_count = 123U;
    SlaveServoTest_InitManual(now);
    SlaveEspWeb_Init(now);
}
static void ready(void)
{
    step(1400U);
    assert(SlaveEspWebDiag.ready == 1U);
    assert(SlaveEspWebDiag.at_detected == 1U);
}
static void ipd(unsigned id, const char *data)
{
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "+IPD,%u,%u:", id, (unsigned)strlen(data));
    feed(prefix); feed(data);
}
static void request(unsigned id, const char *path)
{
    char request_text[256];
    snprintf(request_text, sizeof(request_text), "GET %s HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n", path);
    ipd(id, request_text);
}
static void finish(unsigned count)
{
    unsigned budget = 1000U;
    while (SlaveEspWebDiag.response_count < count && budget--) { step(1U); }
    if (SlaveEspWebDiag.response_count != count)
    {
        fprintf(stderr, "finish: wanted=%u got=%lu state=%lu error=%lu req=%lu last=%s pads=%u resets=%u\n",
                count, (unsigned long)SlaveEspWebDiag.response_count, (unsigned long)SlaveEspWebDiag.state,
                (unsigned long)SlaveEspWebDiag.last_error, (unsigned long)SlaveEspWebDiag.request_count,
                last_tx, pads, resets);
    }
    assert(SlaveEspWebDiag.response_count == count);
    step(8U);
    assert(SlaveEspWebDiag.state == SLAVE_ESP_READY);
}
static void response_size(unsigned id)
{
    const char *length = strstr(responses[id], "Content-Length: ");
    const char *body = strstr(responses[id], "\r\n\r\n");
    unsigned size;
    assert(length && body && sscanf(length, "Content-Length: %u", &size) == 1);
    assert(size == response_length[id]-(unsigned)(body+4-responses[id]));
}
static void test_init_and_data(void)
{
    reset(0U); modern = 1U; ready();
    assert(ap_cur_count == 1U && ap_fallback_count == 1U);
    assert(strstr((const char *)SlaveEspWebDiag.at_version, "2.2.0.0"));
    request(0U, "/api/telemetry"); finish(1U); response_size(0U);
    assert(strstr(responses[0], "\"valid\":true"));
    assert(strstr(responses[0], "\"temperature_x10\":-125"));
    assert(strstr(responses[0], "\"humidity_x10\":0"));
    assert(strstr(responses[0], "\"pressure_pa\":100123"));
    assert(strstr(responses[0], "\"sample_seq\":42"));
    assert(SlaveBme280Diag.sample_tick == 1300U);
    assert(SlaveBme280Diag.sample_success_count == 42U);
    step(2000U); request(1U, "/api/telemetry"); finish(2U);
    assert(strstr(responses[1], "\"valid\":false"));
    assert(strstr(responses[1], "\"temperature_x10\":null"));
    assert(max_tx <= 512U);
    reset(0U); ancient = 1U; reject_gmr = 1U; ready();
    assert(ap_cur_count == 1U && ap_fallback_count == 1U);
    assert(SlaveEspWebDiag.at_version[0] == '\0');
    request(0U, "/"); finish(1U); response_size(0U);
    assert(strstr(responses[0], "Content-Encoding: gzip"));
    {const char *b=strstr(responses[0],"\r\n\r\n")+4;FILE *f=fopen("build/http_page.gz","wb");assert(f);assert((unsigned char)b[0]==31U&&(unsigned char)b[1]==139U);fwrite(b,1,response_length[0]-(unsigned)(b-responses[0]),f);fclose(f);}
    reset(0U); ready();
    assert(ap_cur_count == 1U && ap_fallback_count == 0U);
}
static void test_stream_and_connections(void)
{
    const char *prefix = "+IPD,0,8:GET /api";
    unsigned i;
    reset(0U); ready();
    for (i = 0U; prefix[i]; i++) { char one[2] = {prefix[i], 0}; feed(one); step(1U); }
    ipd(0U, "/telemetry HTTP/1.1\r\nX: ERROR\r\nSEND OK\r\nready\r\n\r\n");
    request(1U, "/");
    request(2U, "/favicon.ico");
    finish(3U);
    assert(strstr(responses[0], "application/json"));
    assert(strstr(responses[1], "text/html"));
    assert(strstr(responses[2], "204 No Content"));
    assert(resets == 0U && SlaveEspWebDiag.active_connections == 0U);
    response_size(0U); response_size(1U); response_size(2U);
    reset(0U); ready();
    ipd(0U, "GET / HTTP/1.1\r\n");
    feed("0,CLOSED\r\n0,CONNECT\r\n"); request(0U, "/api/telemetry"); finish(1U);
    assert(strstr(responses[0], "application/json"));
    assert(!strstr(responses[0], "<!doctype html>"));
    reset(0U); ready(); early_close = 1U;
    request(0U, "/api/telemetry"); finish(1U);
    assert(resets == 0U && closes == 0U && SlaveEspWebDiag.active_connections == 0U);
}
static void test_http_limits(void)
{
    char huge[2200];
    reset(0U); ready();
    request(0U, "/missing");
    ipd(1U, "POST /api/telemetry HTTP/1.1\r\n\r\n");
    memset(huge, 'A', sizeof(huge));
    memcpy(huge, "GET / HTTP/1.1\r\nX:", sizeof("GET / HTTP/1.1\r\nX:") - 1U);
    huge[sizeof(huge) - 1U] = '\0'; ipd(2U, huge);
    finish(3U);
    assert(strstr(responses[0], "404 Not Found"));
    assert(strstr(responses[1], "405 Method Not Allowed"));
    assert(strstr(responses[2], "400 Bad Request"));
    assert(SlaveEspWebDiag.rejected_count == 3U);
    reset(0U); ready();
    ipd(0U, "GET / HTTP/1.1\r\nX:"); step(2100U);
    assert(closes == 1U && SlaveEspWebDiag.request_count == 0U);
}
static void test_recovery_and_servo(void)
{
    reset(0U); suppress_at = 1U;
    assert(SlaveServoTest_SetWindowChannel(1U, 1U, now));
    assert(SlaveServoTest_SetWindowChannel(4U, 0U, now));
    step(299U); assert(pulses[0] == 1700U && pulses[3] == 1300U);
    step(1U); assert(pulses[0] == 1500U && pulses[3] == 1500U);
    step(3100U); assert(SlaveEspWebDiag.ready == 0U && SlaveEspWebDiag.timeout_count == 1U);
    assert(resets == 0U && pads == 0U);
    suppress_at = 0U; step(6500U); assert(SlaveEspWebDiag.ready == 1U);

    reset(0U); ready(); suppress_prompt = 1U;
    request(0U, "/api/telemetry"); step(50U);
    assert(raw_remaining > 0U);
    assert(SlaveServoTest_SetWindowChannel(2U, 1U, now));
    step(300U); assert(pulses[1] == 1500U);
    step(1950U); assert(resets == 0U);
    step(1600U); assert(pads == 1U && resets == 1U);
    suppress_prompt = 0U; step(1600U); assert(SlaveEspWebDiag.ready == 1U);
    assert(raw_remaining == 0U);
    request(1U, "/api/telemetry"); finish(1U);

    reset(0U); ready(); suppress_send = 1U;
    request(0U, "/api/telemetry"); step(4000U);
    assert(pads == 1U && resets == 1U);
    suppress_send = 0U; step(1500U); assert(SlaveEspWebDiag.ready == 1U);

    reset(0U); ready(); EspAtUartDiag.overflow_count++;
    step(1U); assert(SlaveEspWebDiag.last_error == SLAVE_ESP_ERROR_OVERFLOW);
    step(3000U); assert(SlaveEspWebDiag.ready == 1U);
    EspAtUartDiag.error_count++; step(1U);
    assert(SlaveEspWebDiag.last_error == SLAVE_ESP_ERROR_UART);
    step(3000U); assert(SlaveEspWebDiag.ready == 1U);
    feed("ready\r\n"); step(1U);
    assert(SlaveEspWebDiag.ready == 0U); step(1400U);
    assert(SlaveEspWebDiag.ready == 1U);

    reset(0U); ready(); tx_pending = 1U;
    request(0U, "/api/telemetry"); step(270U);
    assert(SlaveEspWebDiag.last_error == SLAVE_ESP_ERROR_TIMEOUT);
    assert(resets == 0U && pads == 0U);
    step(3000U); assert(SlaveEspWebDiag.ready == 1U);

    reset(0U); ready(); suppress_prompt = 1U;
    request(0U, "/api/telemetry"); step(20U);
    EspAtUartDiag.overflow_count++; step(1U);
    assert(SlaveEspWebDiag.last_error == SLAVE_ESP_ERROR_OVERFLOW);
    suppress_prompt = 0U; step(3000U);
    assert(pads == 1U && resets == 1U && raw_remaining == 0U);
    assert(SlaveEspWebDiag.ready == 1U);

    reset(0U); ready(); suppress_prompt = 1U;
    request(0U, "/"); step(20U);
    feed("0,CLOSED\r\n0,CONNECT\r\n"); request(0U, "/api/telemetry"); step(1U);
    assert(SlaveEspWebDiag.ready == 0U);
    suppress_prompt = 0U; step(3000U);
    assert(SlaveEspWebDiag.ready == 1U && pads == 1U);
    assert(response_length[0] == 0U); /* Old page cannot be served to the new ID. */
}
static void test_wrap_and_invalid_ipd(void)
{
    reset(0xFFFFFA00U); ready();
    request(0U, "/api/telemetry"); finish(1U);
    assert(strstr(responses[0], "\"valid\":true"));
    valid_sample = 0U; request(1U, "/api/telemetry"); finish(2U);
    assert(strstr(responses[1], "\"valid\":false"));
    reset(0U); ready(); feed("+IPD,0,20:GET"); step(2100U);
    assert(SlaveEspWebDiag.last_error == SLAVE_ESP_ERROR_IPD);
    step(3000U); assert(SlaveEspWebDiag.ready == 1U);
    feed("+IPD,0,999999:"); step(1U);
    assert(SlaveEspWebDiag.last_error == SLAVE_ESP_ERROR_IPD);
}
static void test_mq2_independence(void)
{
    reset(0U); ready(); valid_sample = 0U;
    request(0U, "/api/telemetry"); finish(1U);
    assert(strstr(responses[0], "\"temperature_x10\":null"));
    assert(strstr(responses[0], "\"mq2\":{\"valid\":true"));
    assert(strstr(responses[0], "\"raw\":2048,\"pa7_mv\":1650,\"ao_mv\":3301"));
    assert(mq_sample.tick == 1300U && mq_sample.sequence == 123U);
    mq_valid = 0U; valid_sample = 1U;
    request(1U, "/api/telemetry"); finish(2U);
    assert(strstr(responses[1], "\"temperature_x10\":-125"));
    assert(strstr(responses[1], "\"mq2\":{\"valid\":false"));
    assert(strstr(responses[1], "\"raw\":null,\"pa7_mv\":null,\"ao_mv\":null"));
    mq_valid = 1U; mq_sample.tick = now - 2000U;
    request(2U, "/api/telemetry"); finish(3U);
    assert(strstr(responses[2], "\"mq2\":{\"valid\":false"));
    /* Maximum decimal field widths must still fit the bounded JSON buffer. */
    now = 0xFFFFFFFFU - 100U; mq_sample.tick = now; mq_sample.sequence = 0xFFFFFFFFU;
    mq_sample.raw = 4095U; mq_sample.pa7_mv = 3300U; mq_sample.ao_mv = 6600U;
    SlaveBme280Diag.sample_tick = now; SlaveBme280Diag.sample_success_count = 0xFFFFFFFFU;
    sample.temperature_x10 = -32767; sample.humidity_x10 = 65534U; sample.pressure_pa = 0xFFFFFFFEU;
    request(3U, "/api/telemetry"); finish(4U);
    assert(strstr(responses[3], "\"ao_mv\":6600}"));
    assert(SlaveEspWebDiag.ready && SlaveEspWebDiag.last_error == 0U);
}
static void test_ultrasonic_snapshot(void)
{
    reset(0U); ready(); valid_sample = mq_valid = 0U;
    request(0U, "/api/telemetry"); finish(1U); response_size(0U);
    assert(strstr(responses[0], "\"ultrasonic\":{\"valid\":true,\"distance_mm\":299,\"raw_mm\":300"));
    assert(strstr(responses[0], "\"pulse_us\":1749,\"filter_count\":5"));
    assert(ultrasonic.tick == 1300U && ultrasonic.sequence == 7U);
    ultrasonic_valid = 0U; SlaveHcsr04Diag.last_error = SLAVE_HCSR04_ERROR_TIMEOUT;
    valid_sample = mq_valid = 1U;
    request(1U, "/api/telemetry"); finish(2U); response_size(1U);
    assert(strstr(responses[1], "\"ultrasonic\":{\"valid\":false,\"distance_mm\":null"));
    assert(strstr(responses[1], "\"error\":4}"));
    assert(strstr(responses[1], "\"mq2\":{\"valid\":true"));
    ultrasonic_valid = 1U; ultrasonic.tick = now - SLAVE_HCSR04_MAX_AGE_MS;
    request(2U, "/api/telemetry"); finish(3U);
    assert(strstr(responses[2], "\"ultrasonic\":{\"valid\":false"));
    now = 0xFFFFFF00U; ultrasonic.tick = now; ultrasonic.sequence = 0xFFFFFFFFU;
    SlaveHcsr04Diag.sample_count = 0xFFFFFFFFU;
    request(3U, "/api/telemetry"); finish(4U); response_size(3U);
    assert(strstr(responses[3], "\"ultrasonic\":{\"valid\":true"));
}
static void test_combined_monitor_snapshot(void)
{
    uint8_t payload[26];
    FILE *fixture;
    const char *body;
    reset(0U); ready();
    memset(payload, 0, sizeof(payload)); payload[0] = 1U;
    payload[1] = (uint8_t)(uint16_t)-234; payload[2] = (uint8_t)((uint16_t)-234 >> 8U);
    payload[3] = (uint8_t)550U; payload[4] = (uint8_t)(550U >> 8U);
    payload[5] = (uint8_t)100100UL; payload[6] = (uint8_t)(100100UL >> 8U); payload[7] = 1U;
    payload[9] = 100U; payload[11] = 0U; payload[12] = payload[13] = 1U;
    payload[14] = 0U; payload[15] = 25U; payload[16] = 50U; payload[17] = 100U;
    memset(payload+18U, 255U, 8U);
    assert(SlaveMasterStatus_Accept(1U, payload, 26U, now));
    assert(SlaveServoTest_SetWindowChannel(2U, 1U, now));
    request(0U, "/api/telemetry"); finish(1U); response_size(0U);
    assert(strstr(responses[0], "\"master\":{\"online\":true"));
    assert(strstr(responses[0], "\"temperature_x10\":-234"));
    assert(strstr(responses[0], "\"fan_pwm\":[0,25,50,100]"));
    assert(strstr(responses[0], "\"state\":6,\"pulse_us\":1700,\"error\":0"));
    body = strstr(responses[0], "\r\n\r\n") + 4;
    fixture = fopen("build/combined_telemetry_fixture.json", "wb"); assert(fixture);
    assert(fwrite(body, 1U, strlen(body), fixture) == strlen(body)); assert(fclose(fixture) == 0);
    step(1900U); SlaveBme280Diag.sample_tick = now; mq_sample.tick = now;
    request(1U, "/api/telemetry"); finish(2U);
    assert(strstr(responses[1], "\"online\":true") && strstr(responses[1], "\"temperature_x10\":null"));
    step(1200U); SlaveBme280Diag.sample_tick = now; mq_sample.tick = now;
    request(2U, "/api/telemetry"); finish(3U);
    assert(strstr(responses[2], "\"master\":{\"online\":false"));
    assert(strstr(responses[2], "\"fan_pwm\":[null,null,null,null]"));
    assert(strstr(responses[2], "\"temperature_x10\":-125"));
    now = 0xFFFFFF00U; SlaveBme280Diag.sample_tick = now;
    SlaveBme280Diag.sample_success_count = mq_sample.sequence = 0xFFFFFFFFU;
    mq_sample.tick = now; mq_sample.raw = 4095U; mq_sample.pa7_mv = 3300U; mq_sample.ao_mv = 6600U;
    assert(SlaveMasterStatus_Accept(2U, payload, 26U, now));
    SlaveServoTestDiag.state = SlaveServoTestDiag.pulse_us = SlaveServoTestDiag.last_error = 0xFFFFFFFFU;
    ultrasonic_valid = 1U; ultrasonic.tick = now; ultrasonic.sequence = 0xFFFFFFFFU;
    ultrasonic.raw_mm = ultrasonic.distance_mm = 500U; ultrasonic.pulse_us = 2916U;
    request(3U, "/api/telemetry"); finish(4U); response_size(3U);
    assert(SlaveEspWebDiag.ready && SlaveEspWebDiag.last_error == 0U);
}
static void control_post(unsigned id,const char *origin,const char *body,uint8_t split)
{
    char req[256];unsigned n;snprintf(req,sizeof(req),"POST /api/control HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\n\r\n",origin,(unsigned)strlen(body));ipd(id,req);
    if(split){char part[97];n=(unsigned)strlen(body)/2;memcpy(part,body,n);part[n]=0;ipd(id,part);step(5U);assert(SlaveEspWebDiag.response_count==0U);ipd(id,body+n);}else ipd(id,body);
}
static void test_control_http(void)
{
    uint8_t status[26],p[11],result[9];uint16_t flow;
    const char *body="{\"request_id\":\"0102030405060708\",\"type\":\"fan\",\"channel\":4,\"value\":75}";
    reset(0U);ready();memset(status,255,sizeof(status));status[0]=1;status[1]=0;status[2]=128;assert(SlaveMasterStatus_Accept(1,status,26,now));
    control_post(0,"http://192.168.4.1",body,1);finish(1);assert(strstr(responses[0],"202 Accepted"));assert(strstr(responses[0],"queued"));
    assert(strstr(responses[0],"\"phase\":\"radio_queued\"")&&strstr(responses[0],"\"reason\":\"none\""));
    assert(SlaveWebControl_Prepare(p,&flow,now)&&p[0]==0x10U&&p[1]==4U&&p[2]==75U);SlaveWebControl_Sent(1,now);memcpy(result,p+3,8);result[8]=0;SlaveWebControl_Accept(flow,result,9,now);
    request(1,"/api/control?id=0102030405060708");finish(2);assert(strstr(responses[1],"success"));response_size(1);
    control_post(2,"http://192.168.4.1",body,0);finish(3);assert(strstr(responses[2],"200 OK")&&strstr(responses[2],"success"));assert(!SlaveWebControl_Prepare(p,&flow,now));
    control_post(3,"http://attacker.invalid",body,0);finish(4);assert(strstr(responses[3],"400 Bad Request"));
    assert(strstr(responses[3],"application/json")&&strstr(responses[3],"\"phase\":\"rejected\"")&&strstr(responses[3],"invalid_request"));
    request(4,"/api/control?id=1111111111111111");finish(5);assert(strstr(responses[4],"404 Not Found"));
    reset(0U);ready();control_post(0,"http://192.168.4.1",body,0);finish(1);assert(strstr(responses[0],"503 Service Unavailable"));assert(!SlaveWebControl_Prepare(p,&flow,now));
    assert(strstr(responses[0],"master_offline"));
    reset(0U);ready();control_post(0,"http://192.168.4.1","{\"request_id\":\"0102030405060708\",\"type\":\"fan\",\"channel\":5,\"value\":75}",0);finish(1);assert(strstr(responses[0],"400 Bad Request"));
}
static void test_browser_headers(void)
{
    uint8_t status[26],p[11];uint16_t flow;unsigned i,j;
    char headers[2600],extra[2200],part[32];
    const char *body="{\"request_id\":\"0102030405060708\",\"type\":\"fan\",\"channel\":4,\"value\":75}";
    const char *names[]={"User-Agent: ","X-Browser-Test: ","Origin: ","Content-Type: ","Host: ","Content-Length: ","Transfer-Encoding: "};
    for(i=0U;i<9U;i++)
    {
        reset(0U);ready();memset(status,255,sizeof(status));status[0]=1U;status[1]=0U;status[2]=128U;
        assert(SlaveMasterStatus_Accept(1U,status,26U,now));
        if(i==7U)strcpy(extra,"Host: 192.168.4.1\r\n");
        else if(i==8U)strcpy(extra,"Origin: http://192.168.4.1\r\n");
        else {strcpy(extra,names[i]);j=(unsigned)strlen(extra);memset(extra+j,'A',400U);strcpy(extra+j+400U,"\r\n");}
        snprintf(headers,sizeof(headers),"POST /api/control HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Type: application/json\r\nContent-Length: %u\r\n%s\r\n%s",(unsigned)strlen(body),extra,body);
        for(j=0U;j<strlen(headers);j+=31U)
        {unsigned n=(unsigned)strlen(headers+j);if(n>31U)n=31U;memcpy(part,headers+j,n);part[n]=0;ipd(0U,part);step(1U);}
        finish(1U);
        if(i<2U)
        {assert(strstr(responses[0],"202 Accepted"));assert(SlaveWebControl_Prepare(p,&flow,now));assert(p[1]==4U&&p[2]==75U);}
        else
        {assert(strstr(responses[0],"400 Bad Request"));assert(strstr(responses[0],"invalid_request"));assert(!SlaveWebControl_Prepare(p,&flow,now));}
        response_size(0U);
    }
    /* An unrelated long field cannot bypass CRLF framing or the total request budget. */
    for(i=0U;i<2U;i++)
    {
        reset(0U);ready();strcpy(extra,"User-Agent: ");memset(extra+12,'A',i?1970U:200U);
        strcpy(extra+12+(i?1970U:200U),i?"\r\n":"\n");
        snprintf(headers,sizeof(headers),"POST /api/control HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Type: application/json\r\nContent-Length: %u\r\n%s\r\n%s",(unsigned)strlen(body),extra,body);
        ipd(0U,headers);finish(1U);assert(strstr(responses[0],"400 Bad Request"));assert(!SlaveWebControl_Prepare(p,&flow,now));
    }
}
int main(void)
{
    test_browser_headers();
    test_control_http();
    test_init_and_data(); test_stream_and_connections(); test_http_limits();
    test_recovery_and_servo(); test_wrap_and_invalid_ipd();
    test_mq2_independence();
    test_ultrasonic_snapshot();
    test_combined_monitor_snapshot();
    puts("PASS: ESP AT/HTTP state machine, freshness, recovery, connections and timed servos");
    return 0;
}
