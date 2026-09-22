#include "master_dht11.h"

#include <string.h>
#include "stm32f1xx_hal.h"

#define MASTER_DHT11_GPIO_PORT GPIOA
#define MASTER_DHT11_GPIO_PIN  GPIO_PIN_5
#define DHT11_WARMUP_MS        (2000UL)
#define DHT11_MIN_INTERVAL_MS  (2000UL)
#define DHT11_EDGE_TIMEOUT_US  (150U)
#define DHT11_ONE_MIN_US       (50U)
#define DHT11_POLL_LIMIT       (100000UL)

volatile MasterDht11Diagnostics MasterDht11Diag;
static uint32_t g_init_tick;
static uint8_t g_timer_ready;

static uint8_t MasterDht11_ConfigureTimer(void)
{
    RCC_ClkInitTypeDef clock;
    uint32_t latency;
    uint32_t timer_clock;

    __HAL_RCC_TIM1_CLK_ENABLE();
    HAL_RCC_GetClockConfig(&clock, &latency);
    timer_clock = HAL_RCC_GetPCLK2Freq();
    if (clock.APB2CLKDivider != RCC_HCLK_DIV1)
    {
        timer_clock *= 2U;
    }
    MasterDht11Diag.timer_clock_hz = timer_clock;
    if ((timer_clock < 1000000UL) || ((timer_clock % 1000000UL) != 0U))
    {
        g_timer_ready = 0U;
        MasterDht11Diag.last_error = MASTER_DHT11_TIMER_ERROR;
        return 0U;
    }

    __HAL_RCC_TIM1_FORCE_RESET();
    __HAL_RCC_TIM1_RELEASE_RESET();
    TIM1->PSC = (timer_clock / 1000000UL) - 1UL;
    TIM1->ARR = 0xFFFFU;
    TIM1->DIER = 0U;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0U;
    TIM1->CNT = 0U;
    TIM1->CR1 = TIM_CR1_CEN;
    MasterDht11Diag.timer_psc = TIM1->PSC;
    MasterDht11Diag.timer_arr = TIM1->ARR;
    g_timer_ready = 1U;
    return 1U;
}

static uint16_t MasterDht11_NowUs(void)
{
    return (uint16_t)TIM1->CNT;
}

static uint8_t MasterDht11_Level(void)
{
    return ((MASTER_DHT11_GPIO_PORT->IDR & MASTER_DHT11_GPIO_PIN) != 0U) ? 1U : 0U;
}

static void MasterDht11_Release(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = MASTER_DHT11_GPIO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(MASTER_DHT11_GPIO_PORT, &gpio);
}

static void MasterDht11_DriveLow(void)
{
    GPIO_InitTypeDef gpio = {0};

    HAL_GPIO_WritePin(MASTER_DHT11_GPIO_PORT, MASTER_DHT11_GPIO_PIN, GPIO_PIN_RESET);
    gpio.Pin = MASTER_DHT11_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MASTER_DHT11_GPIO_PORT, &gpio);
}

/* All waits are measured by TIM1, not by counting C-loop iterations.
 * The second bound also returns if a damaged/reconfigured timer stops.
 * USART2 and SysTick interrupts remain enabled throughout acquisition.
 */
static uint8_t MasterDht11_WaitLevel(uint8_t level, uint16_t timeout_us,
                                    uint16_t *edge, MasterDht11Status error)
{
    uint16_t start = MasterDht11_NowUs();
    uint32_t polls = 0U;

    while (MasterDht11_Level() != level)
    {
        if ((uint16_t)(MasterDht11_NowUs() - start) >= timeout_us)
        {
            MasterDht11Diag.last_error = (uint32_t)error;
            return 0U;
        }
        if (++polls >= DHT11_POLL_LIMIT)
        {
            MasterDht11Diag.last_error = MASTER_DHT11_TIMER_ERROR;
            return 0U;
        }
    }
    *edge = MasterDht11_NowUs();
    return 1U;
}

static uint8_t MasterDht11_ReadFrame(uint8_t frame[5])
{
    uint16_t falling;
    uint16_t rising;
    uint16_t next_falling;
    uint16_t width;
    uint8_t bit;

    MasterDht11_Release();
    MasterDht11Diag.idle_level = MasterDht11_Level();
    if (MasterDht11_WaitLevel(1U, DHT11_EDGE_TIMEOUT_US, &rising,
                              MASTER_DHT11_IDLE_LOW) == 0U)
    {
        return 0U;
    }
    MasterDht11Diag.idle_level = 1U;

    MasterDht11_DriveLow();
    HAL_Delay(20U);
    MasterDht11Diag.start_level = MasterDht11_Level();
    if (MasterDht11Diag.start_level != 0U)
    {
        MasterDht11Diag.last_error = MASTER_DHT11_DRIVE_LOW_FAILED;
        return 0U;
    }

    /* Release directly to input: never drive against the sensor's reply. */
    MasterDht11_Release();
    MasterDht11Diag.released_level = MasterDht11_Level();
    if (MasterDht11_WaitLevel(1U, 40U, &rising,
                              MASTER_DHT11_RELEASE_FAILED) == 0U)
    {
        return 0U;
    }
    MasterDht11Diag.released_level = 1U;
    if (MasterDht11_WaitLevel(0U, 200U, &falling,
                              MASTER_DHT11_NO_RESPONSE) == 0U)
    {
        return 0U;
    }
    if (MasterDht11_WaitLevel(1U, DHT11_EDGE_TIMEOUT_US, &rising,
                              MASTER_DHT11_RESPONSE_LOW_TIMEOUT) == 0U)
    {
        return 0U;
    }
    MasterDht11Diag.response_low_us = (uint16_t)(rising - falling);
    if (MasterDht11_WaitLevel(0U, DHT11_EDGE_TIMEOUT_US, &falling,
                              MASTER_DHT11_RESPONSE_HIGH_TIMEOUT) == 0U)
    {
        return 0U;
    }
    MasterDht11Diag.response_high_us = (uint16_t)(falling - rising);
    if ((MasterDht11Diag.response_low_us < 40U) ||
        (MasterDht11Diag.response_high_us < 40U))
    {
        MasterDht11Diag.last_error = MASTER_DHT11_PULSE_INVALID;
        return 0U;
    }

    for (bit = 0U; bit < 40U; bit++)
    {
        MasterDht11Diag.failed_bit = bit;
        if (MasterDht11_WaitLevel(1U, DHT11_EDGE_TIMEOUT_US, &rising,
                                  MASTER_DHT11_BIT_LOW_TIMEOUT) == 0U)
        {
            return 0U;
        }
        MasterDht11Diag.low_us[bit] = (uint16_t)(rising - falling);
        if (MasterDht11_WaitLevel(0U, DHT11_EDGE_TIMEOUT_US, &next_falling,
                                  MASTER_DHT11_BIT_HIGH_TIMEOUT) == 0U)
        {
            return 0U;
        }
        width = (uint16_t)(next_falling - rising);
        MasterDht11Diag.high_us[bit] = width;
        if ((MasterDht11Diag.low_us[bit] < 10U) || (width < 10U) || (width > 110U))
        {
            MasterDht11Diag.last_error = MASTER_DHT11_PULSE_INVALID;
            return 0U;
        }
        frame[bit / 8U] = (uint8_t)((frame[bit / 8U] << 1U) |
                                  ((width >= DHT11_ONE_MIN_US) ? 1U : 0U));
        falling = next_falling;
    }
    MasterDht11Diag.failed_bit = UINT32_MAX;
    return 1U;
}

void MasterDht11_Init(void)
{
    uint16_t start;

    memset((void *)&MasterDht11Diag, 0, sizeof(MasterDht11Diag));
    MasterDht11Diag.last_error = MASTER_DHT11_NOT_READY;
    MasterDht11Diag.failed_bit = UINT32_MAX;
    g_timer_ready = 0U;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    MasterDht11_Release();
    if (MasterDht11_ConfigureTimer() == 0U)
    {
        return;
    }
    start = MasterDht11_NowUs();
    HAL_Delay(2U);
    MasterDht11Diag.timer_test_ticks = (uint16_t)(MasterDht11_NowUs() - start);
    if ((MasterDht11Diag.timer_test_ticks >= 1000U) &&
        (MasterDht11Diag.timer_test_ticks <= 5000U))
    {
        g_timer_ready = 1U;
    }
    else
    {
        MasterDht11Diag.last_error = MASTER_DHT11_TIMER_ERROR;
    }
    g_init_tick = HAL_GetTick();
}

uint8_t MasterDht11_ResumeTiming(void)
{
    return MasterDht11_ConfigureTimer();
}

uint8_t MasterDht11_Read(int16_t *temperature_x10, uint16_t *humidity_x10)
{
    uint8_t frame[5] = {0};
    uint8_t ok;
    uint8_t bit;
    uint16_t start;
    uint16_t humidity;
    int16_t temperature;
    uint32_t now = HAL_GetTick();

    if ((temperature_x10 == NULL) || (humidity_x10 == NULL))
    {
        MasterDht11Diag.last_error = MASTER_DHT11_ARGUMENT_ERROR;
        return 0U;
    }
    if (g_timer_ready == 0U)
    {
        MasterDht11Diag.last_error = MASTER_DHT11_TIMER_ERROR;
        return 0U;
    }
    if (((uint32_t)(now - g_init_tick) < DHT11_WARMUP_MS) ||
        ((MasterDht11Diag.attempt_count != 0U) &&
         ((uint32_t)(now - MasterDht11Diag.last_attempt_ms) < DHT11_MIN_INTERVAL_MS)))
    {
        MasterDht11Diag.last_error = MASTER_DHT11_NOT_READY;
        return 0U;
    }

    MasterDht11Diag.attempt_count++;
    MasterDht11Diag.last_attempt_ms = now;
    MasterDht11Diag.last_error = MASTER_DHT11_OK;
    MasterDht11Diag.failed_bit = UINT32_MAX;
    MasterDht11Diag.start_level = 0U;
    MasterDht11Diag.released_level = 0U;
    MasterDht11Diag.response_low_us = 0U;
    MasterDht11Diag.response_high_us = 0U;
    for (bit = 0U; bit < 40U; bit++)
    {
        MasterDht11Diag.low_us[bit] = 0U;
        MasterDht11Diag.high_us[bit] = 0U;
    }
    start = MasterDht11_NowUs();
    ok = MasterDht11_ReadFrame(frame);
    MasterDht11_Release();
    MasterDht11Diag.elapsed_us = (uint16_t)(MasterDht11_NowUs() - start);
    for (bit = 0U; bit < 5U; bit++)
    {
        MasterDht11Diag.raw[bit] = frame[bit];
    }
    if (ok == 0U)
    {
        MasterDht11Diag.failure_count++;
        return 0U;
    }
    if ((uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]) != frame[4])
    {
        MasterDht11Diag.last_error = MASTER_DHT11_CHECKSUM_ERROR;
        MasterDht11Diag.failure_count++;
        return 0U;
    }

    humidity = (uint16_t)((uint16_t)frame[0] * 10U + frame[1]);
    temperature = (int16_t)((int16_t)(frame[2] & 0x7FU) * 10 + frame[3]);
    if ((frame[2] & 0x80U) != 0U)
    {
        temperature = (int16_t)-temperature;
    }
    if ((humidity > 1000U) || (temperature < -400) || (temperature > 800))
    {
        MasterDht11Diag.last_error = MASTER_DHT11_RANGE_ERROR;
        MasterDht11Diag.failure_count++;
        return 0U;
    }
    *temperature_x10 = temperature;
    *humidity_x10 = humidity;
    MasterDht11Diag.success_count++;
    MasterDht11Diag.last_success_ms = HAL_GetTick();
    return 1U;
}
