#include "slave_master_status.h"
#include <string.h>

SlaveMasterStatusDiagnostics SlaveMasterStatusDiag;
static uint8_t g_payload[MONITOR_STATUS_BYTES], g_received;
static uint16_t g_flow;
static uint32_t g_tick;

void SlaveMasterStatus_Init(void)
{
    memset(&SlaveMasterStatusDiag, 0, sizeof(SlaveMasterStatusDiag));
    memset(g_payload, 0, sizeof(g_payload));
    g_received = 0U; g_flow = 0U; g_tick = 0U;
}
uint8_t SlaveMasterStatus_Accept(uint16_t flow_id, const uint8_t *payload,
                               uint8_t size, uint32_t now_ms)
{
    if (MonitorStatus_Validate(payload, size) == 0U)
    { SlaveMasterStatusDiag.invalid_count++; return 0U; }
    /* A reboot can restart its sequence; re-synchronize after the old cache expires. */
    if (g_received != 0U && (uint32_t)(now_ms - g_tick) < MONITOR_STATUS_ONLINE_MS &&
        (int16_t)(flow_id - g_flow) <= 0)
    { SlaveMasterStatusDiag.duplicate_count++; return 0U; }
    memcpy(g_payload, payload, sizeof(g_payload));
    g_flow = flow_id; g_tick = now_ms; g_received = 1U;
    SlaveMasterStatusDiag.accepted_count++;
    SlaveMasterStatusDiag.last_flow_id = flow_id;
    SlaveMasterStatusDiag.received_tick = now_ms;
    return 1U;
}
void SlaveMasterStatus_Get(uint32_t now_ms, SlaveMasterStatus *out)
{
    uint32_t elapsed = now_ms - g_tick;
    uint16_t age;
    if (out == 0) { return; }
    memset(out, 0, sizeof(*out));
    out->rain = out->dark = out->light_on = 0xFFU;
    memset(out->fan_pwm, 0xFF, sizeof(out->fan_pwm));
    if (g_received == 0U || elapsed >= MONITOR_STATUS_ONLINE_MS) { return; }
    out->online = 1U; out->rx_age_ms = elapsed;
    out->uptime_ms = MonitorStatus_U32(g_payload + MONITOR_UPTIME);
    out->sample_seq = MonitorStatus_U32(g_payload + MONITOR_BME_SEQUENCE);
    out->rain = g_payload[MONITOR_RAIN]; out->dark = g_payload[MONITOR_DARK];
    out->light_on = g_payload[MONITOR_LIGHT];
    memcpy(out->fan_pwm, g_payload + MONITOR_FANS, sizeof(out->fan_pwm));
    age = MonitorStatus_U16(g_payload + MONITOR_BME_AGE);
    if (age >= MONITOR_BME_MAX_AGE_MS || elapsed >= MONITOR_BME_MAX_AGE_MS - age)
    { return; }
    out->bme_valid = 1U; out->bme_age_ms = age + elapsed;
    out->temperature_x10 = (int16_t)MonitorStatus_U16(g_payload + MONITOR_BME_TEMP);
    out->humidity_x10 = MonitorStatus_U16(g_payload + MONITOR_BME_HUM);
    out->pressure_pa = MonitorStatus_U32(g_payload + MONITOR_BME_PRESSURE);
}
