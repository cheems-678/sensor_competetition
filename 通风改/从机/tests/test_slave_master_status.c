#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "slave_master_status.h"
static uint8_t data[MONITOR_STATUS_BYTES];
static void u16(unsigned offset, uint16_t value)
{ data[offset] = (uint8_t)value; data[offset+1] = (uint8_t)(value >> 8); }
static void reset(void)
{
    memset(data, 0, sizeof(data)); SlaveMasterStatus_Init();
    data[0] = 1U; u16(1U, (uint16_t)-125); u16(3U, 1000U);
    data[5] = 0x4DU; data[6] = 0x8BU; data[7] = 1U; /* 101197 Pa */
    u16(9U, 100U); data[11] = 1U; data[12] = 0U; data[13] = 1U;
    data[14] = 0U; data[15] = 25U; data[16] = 70U; data[17] = 100U;
}
int main(void)
{
    SlaveMasterStatus out;
    uint8_t saved;
    unsigned i;
    reset(); SlaveMasterStatus_Get(0U, &out); assert(!out.online && out.rain == 255U);
    assert(SlaveMasterStatus_Accept(100U, data, sizeof(data), 1000U));
    SlaveMasterStatus_Get(2899U, &out);
    assert(out.online && out.bme_valid && out.bme_age_ms == 1999U);
    assert(out.temperature_x10 == -125 && out.humidity_x10 == 1000U && out.fan_pwm[3] == 100U);
    SlaveMasterStatus_Get(2900U, &out); assert(out.online && !out.bme_valid && out.rain == 1U);
    assert(!SlaveMasterStatus_Accept(100U, data, sizeof(data), 3999U));
    assert(!SlaveMasterStatus_Accept(99U, data, sizeof(data), 3999U));
    SlaveMasterStatus_Get(4000U, &out); assert(!out.online && !out.bme_valid && out.fan_pwm[0] == 255U);
    assert(SlaveMasterStatus_Accept(1U, data, sizeof(data), 4000U)); /* reboot */
    assert(SlaveMasterStatusDiag.duplicate_count == 2U);
    for (i = 11U; i < 18U; i++)
    {
        saved = data[i]; data[i] = (i < 14U) ? 2U : 101U;
        assert(!SlaveMasterStatus_Accept(2U, data, sizeof(data), 4001U));
        data[i] = saved;
    }
    assert(!SlaveMasterStatus_Accept(2U, data, 25U, 4001U));
    assert(!SlaveMasterStatus_Accept(2U, NULL, 26U, 4001U));
    u16(9U, 2000U); assert(!MonitorStatus_Validate(data, sizeof(data)));
    u16(9U, 0U); u16(1U, 0x8000U); assert(!MonitorStatus_Validate(data, sizeof(data)));
    u16(3U, 0xFFFFU); memset(data+5U, 0xFF, 6U);
    assert(SlaveMasterStatus_Accept(2U, data, sizeof(data), 4001U));
    SlaveMasterStatus_Get(4001U, &out); assert(out.online && !out.bme_valid && out.rain == 1U);
    reset(); assert(SlaveMasterStatus_Accept(0xFFFFU, data, 26U, 0xFFFFFFF0U));
    assert(SlaveMasterStatus_Accept(0U, data, 26U, 5U));
    SlaveMasterStatus_Get(1904U, &out); assert(out.bme_valid && out.bme_age_ms == 1999U);
    SlaveMasterStatus_Get(1905U, &out); assert(out.online && !out.bme_valid);
    SlaveMasterStatus_Get(3005U, &out); assert(!out.online);
    {
        uint8_t audio[MONITOR_STATUS_AUDIO_BYTES];
        reset(); memcpy(audio, data, sizeof(data)); memset(audio + 26U, 0xFF, 16U);
        audio[0] = MONITOR_STATUS_AUDIO_LAYOUT;
        assert(!MonitorStatus_Validate(audio, 26U));
        assert(!MonitorStatus_Validate(data, 42U)); /* wrong layout rejected before audio access */
        assert(SlaveMasterStatus_Accept(1U, audio, sizeof(audio), 100U));
        SlaveMasterStatus_Get(100U, &out); assert(out.bme_valid && !out.acoustic_valid);
        for (i = 0U; i < 5U; i++) { audio[26U+2U*i] = (uint8_t)i; audio[27U+2U*i] = 0U; }
        audio[36] = 100U; audio[37] = 0U; audio[38] = 9U; audio[39] = audio[40] = audio[41] = 0U;
        assert(SlaveMasterStatus_Accept(2U, audio, sizeof(audio), 200U));
        SlaveMasterStatus_Get(2099U, &out); assert(out.acoustic_valid && out.acoustic_age_ms == 1999U);
        assert(out.acoustic_peak_to_peak[0] == 0U && out.acoustic_peak_to_peak[4] == 4U && out.acoustic_seq == 9U);
        assert(!SlaveMasterStatus_Accept(2U, audio, sizeof(audio), 2099U));
        SlaveMasterStatus_Get(2100U, &out); assert(!out.acoustic_valid && out.online);
        audio[36] = 44U; audio[37] = 1U; assert(!MonitorStatus_Validate(audio, sizeof(audio))); /* 300 */
        audio[36] = 0U; audio[37] = 0U; audio[27] = 16U; assert(!MonitorStatus_Validate(audio, sizeof(audio))); /* 4096 */
        audio[27] = 0U; audio[36] = audio[37] = 255U; assert(!MonitorStatus_Validate(audio, sizeof(audio)));
        memset(audio+26U, 255, 12U); assert(SlaveMasterStatus_Accept(3U, audio, sizeof(audio), 2200U));
        assert(SlaveMasterStatus_Accept(4U, data, sizeof(data), 2300U));
        SlaveMasterStatus_Get(2300U, &out); assert(!out.acoustic_valid && out.bme_valid);
        reset(); audio[36] = audio[37] = 0U; memset(audio+26U, 0, 10U);
        assert(SlaveMasterStatus_Accept(65535U, audio, sizeof(audio), 0xFFFFFFF0U));
        SlaveMasterStatus_Get(1983U, &out); assert(out.acoustic_valid && out.acoustic_age_ms == 1999U);
        SlaveMasterStatus_Get(1984U, &out); assert(!out.acoustic_valid);
    }
    puts("PASS: master status wire bounds, independent expiry, duplicates, reboot and wraps");
    return 0;
}
