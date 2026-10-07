#ifndef MAX4466_WIRE_H
#define MAX4466_WIRE_H
#include <stdint.h>
#define MAX4466_WIRE_SIZE 46U
#define MAX4466_WIRE_SOUND_OFFSET 18U
#define MAX4466_WIRE_AGE_OFFSET 28U
#define MAX4466_WIRE_MQ_OFFSET 30U
#define MAX4466_WIRE_US_OFFSET 38U
static __inline uint16_t Max4466Wire_U16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U)); }
static __inline uint8_t Max4466Wire_Validate(const uint8_t *p, uint8_t size)
{
    uint8_t i, empty = 1U, valid = 1U;
    uint16_t a, b, c, age;
    if (size != 46U || (p[0] != 0x73U && p[0] != 0x77U)) { return 0U; }
    for (i = 0U; i < 6U; ++i) {
        uint16_t v = Max4466Wire_U16(p + 18U + 2U * i);
        if (v != 0xFFFFU) { empty = 0U; }
        if (v > (i == 5U ? 299U : 4095U)) { valid = 0U; }
    }
    if (!empty && !valid) { return 0U; }
    for (i = 0U; i < 2U; ++i) {
        const uint8_t *q = p + (i == 0U ? 30U : 38U);
        a = Max4466Wire_U16(q); b = Max4466Wire_U16(q + 2U);
        c = Max4466Wire_U16(q + 4U); age = Max4466Wire_U16(q + 6U);
        if (a == 0xFFFFU && b == 0xFFFFU && c == 0xFFFFU && age == 0xFFFFU) { continue; }
        if (age >= 2000U) { return 0U; }
        if (i == 0U) {
            if (a > 4095U || b > 3600U || c > 7200U) { return 0U; }
        } else if (a < 50U || a > 500U || b < 50U || b > 500U || c == 0U || c >= 10000U) { return 0U; }
    }
    return 1U;
}
#endif
