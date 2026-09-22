#ifndef MASTER_DHT11_H
#define MASTER_DHT11_H

#include <stdint.h>

typedef enum
{
    MASTER_DHT11_OK = 0,
    MASTER_DHT11_NOT_READY = 1,
    MASTER_DHT11_TIMER_ERROR = 2,
    MASTER_DHT11_IDLE_LOW = 3,
    MASTER_DHT11_DRIVE_LOW_FAILED = 4,
    MASTER_DHT11_RELEASE_FAILED = 5,
    MASTER_DHT11_NO_RESPONSE = 6,
    MASTER_DHT11_RESPONSE_LOW_TIMEOUT = 7,
    MASTER_DHT11_RESPONSE_HIGH_TIMEOUT = 8,
    MASTER_DHT11_BIT_LOW_TIMEOUT = 9,
    MASTER_DHT11_BIT_HIGH_TIMEOUT = 10,
    MASTER_DHT11_PULSE_INVALID = 11,
    MASTER_DHT11_CHECKSUM_ERROR = 12,
    MASTER_DHT11_RANGE_ERROR = 13,
    MASTER_DHT11_ARGUMENT_ERROR = 14
} MasterDht11Status;

/* RAM diagnostics only; low_us/high_us are in chronological bit order. */
typedef struct
{
    uint32_t attempt_count;
    uint32_t success_count;
    uint32_t failure_count;
    uint32_t last_error;
    uint32_t failed_bit;       /* 0..39; UINT32_MAX outside data bits */
    uint32_t last_attempt_ms;
    uint32_t last_success_ms;
    uint32_t elapsed_us;
    uint32_t idle_level;
    uint32_t start_level;
    uint32_t released_level;
    uint32_t response_low_us;
    uint32_t response_high_us;
    uint32_t timer_clock_hz;
    uint32_t timer_psc;
    uint32_t timer_arr;
    uint32_t timer_test_ticks; /* TIM1 ticks during HAL_Delay(2) at init */
    uint8_t raw[5];
    uint8_t reserved[3];
    uint16_t low_us[40];
    uint16_t high_us[40];
} MasterDht11Diagnostics;

extern volatile MasterDht11Diagnostics MasterDht11Diag;

/*
 * PA5 单总线 DHT11 驱动。
 * 温度和湿度均按 0.1 单位返回，例如 25.3℃ 返回 253。
 */
void MasterDht11_Init(void);
/* Restore TIM1 to the 1 MHz free-running mode after WS2812 transmission. */
uint8_t MasterDht11_ResumeTiming(void);
uint8_t MasterDht11_Read(int16_t *temperature_x10,
                         uint16_t *humidity_x10);

#endif /* MASTER_DHT11_H */
