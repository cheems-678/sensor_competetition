#ifndef MONITOR_STATUS_WIRE_H
#define MONITOR_STATUS_WIRE_H
#include <stdint.h>

#define MONITOR_STATUS_TYPE 0x03U
#define MONITOR_STATUS_BYTES 26U
#define MONITOR_STATUS_LAYOUT 1U
#define MONITOR_STATUS_PERIOD_MS 1000UL
#define MONITOR_STATUS_ONLINE_MS 3000UL
#define MONITOR_BME_MAX_AGE_MS 2000UL
#define MONITOR_BME_TEMP 1U
#define MONITOR_BME_HUM 3U
#define MONITOR_BME_PRESSURE 5U
#define MONITOR_BME_AGE 9U
#define MONITOR_RAIN 11U
#define MONITOR_DARK 12U
#define MONITOR_LIGHT 13U
#define MONITOR_FANS 14U
#define MONITOR_UPTIME 18U
#define MONITOR_BME_SEQUENCE 22U

static inline uint16_t MonitorStatus_U16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U)); }
static inline uint32_t MonitorStatus_U32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
         ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U); }
static inline uint8_t MonitorStatus_Validate(const uint8_t *p, uint8_t size)
{
    uint8_t i;
    int16_t temperature;
    uint16_t humidity, age;
    uint32_t pressure;
    if (p == 0 || size != MONITOR_STATUS_BYTES || p[0] != MONITOR_STATUS_LAYOUT)
    { return 0U; }
    temperature = (int16_t)MonitorStatus_U16(p + MONITOR_BME_TEMP);
    humidity = MonitorStatus_U16(p + MONITOR_BME_HUM);
    pressure = MonitorStatus_U32(p + MONITOR_BME_PRESSURE);
    age = MonitorStatus_U16(p + MONITOR_BME_AGE);
    if (temperature == (-32767 - 1))
    {
        if (humidity != 0xFFFFU || pressure != 0xFFFFFFFFUL || age != 0xFFFFU)
        { return 0U; }
    }
    else if (temperature < -400 || temperature > 850 || humidity > 1000U ||
             pressure < 30000UL || pressure > 110000UL || age >= MONITOR_BME_MAX_AGE_MS)
    { return 0U; }
    for (i = MONITOR_RAIN; i <= MONITOR_LIGHT; i++)
    { if (p[i] > 1U && p[i] != 0xFFU) { return 0U; } }
    for (i = MONITOR_FANS; i < MONITOR_FANS + 4U; i++)
    { if (p[i] > 100U && p[i] != 0xFFU) { return 0U; } }
    return 1U;
}
#endif
