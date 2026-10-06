#ifndef WEB_CONTROL_WIRE_H
#define WEB_CONTROL_WIRE_H
#include <stdint.h>
#include <string.h>
#define WEB_REQUEST 0x12U
#define WEB_RESULT 0x21U
#define WEB_REQUEST_SIZE 11U
#define WEB_RESULT_SIZE 9U
#define WEB_CACHE_MS 30000UL
#define WEB_QUEUE_MS 1000UL
#define WEB_TELEMETRY_WAIT_MS 2000UL
#define WEB_TIMEOUT_MS 8000UL
enum { WEB_OK, WEB_FAILED, WEB_UNKNOWN, WEB_BUSY, WEB_QUEUED, WEB_WAITING, WEB_MISSING };
typedef struct { uint8_t id[8], command, channel, value, state; uint32_t tick; } WebRecord;
static inline uint8_t Web_Valid(const uint8_t *p, uint8_t size)
{ return p && size == 11U && p[1] >= 1U && p[1] <= 4U &&
    ((p[0] == 0x10U && p[2] <= 100U) || (p[0] == 0x11U && p[2] <= 1U)); }
static inline uint8_t Web_Same(const WebRecord *r, const uint8_t *p)
{ return r->command == p[0] && r->channel == p[1] && r->value == p[2]; }
static inline WebRecord *Web_Find(WebRecord *cache, const uint8_t *id, uint32_t now)
{ uint8_t i; for (i=0U;i<4U;i++) if (cache[i].command && (uint32_t)(now-cache[i].tick)<WEB_CACHE_MS && !memcmp(cache[i].id,id,8U)) return cache+i; return 0; }
#endif
