#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"
#include "master_config.h"
#include "master_rain.h"

GPIO_TypeDef TestGpioA;
static GPIO_PinState sensor_level;
static GPIO_InitTypeDef gpio_config;
static unsigned clock_count, init_count, read_count;

void TestGpioAClockEnable(void) { clock_count++; }

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config)
{
    assert(port == GPIOA && clock_count != 0U);
    gpio_config = *config;
    init_count++;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    assert(port == GPIOA && pin == GPIO_PIN_11 && init_count != 0U);
    read_count++;
    return sensor_level;
}

void HAL_Delay(uint32_t delay)
{
    (void)delay;
    assert(!"Rain confirmation must not call HAL_Delay");
}

static void reset(uint32_t now_ms, GPIO_PinState level)
{
    sensor_level = level;
    clock_count = init_count = read_count = 0U;
    memset(&gpio_config, 0, sizeof(gpio_config));
    MasterRain_Init(now_ms);
}

static void process(uint32_t now_ms, GPIO_PinState level)
{
    unsigned previous_reads = read_count;
    sensor_level = level;
    MasterRain_Process(now_ms);
    assert(read_count == previous_reads + 1U);
    assert(MasterRainDiag.sample_tick == now_ms);
}

static void test_pin_and_dry_start(void)
{
    assert(MasterRain_GetState(0U) == 0xFFU);
    assert(MASTER_RAIN_ASSERT_MS == 200U && MASTER_RAIN_CLEAR_MS == 3000U);
    reset(100U, GPIO_PIN_SET);
    assert(clock_count == 1U && init_count == 1U && read_count == 1U);
    assert(gpio_config.Pin == GPIO_PIN_11 && gpio_config.Mode == GPIO_MODE_INPUT);
    assert(gpio_config.Pull == GPIO_PULLUP && gpio_config.Speed == GPIO_SPEED_FREQ_LOW);
    assert(MasterRainDiag.raw_raining == 0U && !MasterRainDiag.input_valid);
    assert(MasterRain_GetState(100U) == 0xFFU);
    process(1100U, GPIO_PIN_SET);
    process(2100U, GPIO_PIN_SET);
    process(3099U, GPIO_PIN_SET);
    assert(MasterRain_GetState(3099U) == 0xFFU);
    process(3100U, GPIO_PIN_SET);
    assert(MasterRain_GetState(3100U) == 0U);
    assert(MasterRainDiag.confirmation_count == 1U);
    process(3200U, GPIO_PIN_SET);
    assert(MasterRainDiag.confirmation_count == 1U && init_count == 1U);
}

static void test_wet_start_and_clear_boundary(void)
{
    reset(0U, GPIO_PIN_RESET);
    assert(MasterRainDiag.raw_raining == 1U);
    process(199U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(199U) == 0xFFU);
    process(200U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(200U) == 1U);
    process(300U, GPIO_PIN_SET);
    process(1300U, GPIO_PIN_SET);
    process(2300U, GPIO_PIN_SET);
    process(3299U, GPIO_PIN_SET);
    assert(MasterRain_GetState(3299U) == 1U);
    process(3300U, GPIO_PIN_SET);
    assert(MasterRain_GetState(3300U) == 0U);
    assert(MasterRainDiag.confirmation_count == 2U);
}

static void test_bounce_restarts_confirmation(void)
{
    reset(0U, GPIO_PIN_RESET);
    process(100U, GPIO_PIN_SET);
    process(150U, GPIO_PIN_RESET);
    process(349U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(349U) == 0xFFU);
    process(350U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(350U) == 1U);
    process(400U, GPIO_PIN_SET);
    process(1400U, GPIO_PIN_SET);
    process(2400U, GPIO_PIN_SET);
    process(3399U, GPIO_PIN_RESET); /* Even a short wet pulse cancels clearing. */
    assert(MasterRain_GetState(3399U) == 1U);
    process(3400U, GPIO_PIN_SET);
    process(4400U, GPIO_PIN_SET);
    process(5400U, GPIO_PIN_SET);
    process(6399U, GPIO_PIN_SET);
    assert(MasterRain_GetState(6399U) == 1U);
    process(6400U, GPIO_PIN_SET);
    assert(MasterRain_GetState(6400U) == 0U);
    process(6500U, GPIO_PIN_RESET);
    process(6699U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(6699U) == 0U);
    process(6700U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(6700U) == 1U);
    assert(MasterRainDiag.confirmation_count == 3U);
}

static void test_stale_gap_and_reinitialize(void)
{
    reset(0U, GPIO_PIN_RESET);
    process(200U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(2199U) == 1U);
    assert(MasterRain_GetState(2200U) == 0xFFU);
    process(2200U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(2200U) == 0xFFU);
    process(2399U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(2399U) == 0xFFU);
    process(2400U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(2400U) == 1U);
    reset(2500U, GPIO_PIN_SET);
    assert(MasterRain_GetState(2500U) == 0xFFU);
    assert(MasterRainDiag.confirmation_count == 0U);
    process(4500U, GPIO_PIN_SET); /* Unobserved gap must not confirm dry. */
    assert(MasterRain_GetState(4500U) == 0xFFU);
    process(5500U, GPIO_PIN_SET);
    process(6500U, GPIO_PIN_SET);
    process(7499U, GPIO_PIN_SET);
    assert(MasterRain_GetState(7499U) == 0xFFU);
    process(7500U, GPIO_PIN_SET);
    assert(MasterRain_GetState(7500U) == 0U);
    process(9500U, GPIO_PIN_SET); /* A known dry input must also reconfirm. */
    assert(MasterRain_GetState(9500U) == 0xFFU);
    process(10500U, GPIO_PIN_SET);
    process(11500U, GPIO_PIN_SET);
    process(12499U, GPIO_PIN_SET);
    assert(MasterRain_GetState(12499U) == 0xFFU);
    process(12500U, GPIO_PIN_SET);
    assert(MasterRain_GetState(12500U) == 0U);
    process(14500U, GPIO_PIN_RESET); /* Gap and edge occur on the same read. */
    assert(MasterRain_GetState(14500U) == 0xFFU);
    process(14699U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(14699U) == 0xFFU);
    process(14700U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(14700U) == 1U);
}

static void test_tick_wrap(void)
{
    uint32_t start = 0xFFFFFF9CU;
    reset(start, GPIO_PIN_RESET);
    process(start + 199U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(start + 199U) == 0xFFU);
    process(start + 200U, GPIO_PIN_RESET);
    assert(MasterRain_GetState(start + 200U) == 1U);
    start = 0xFFFFFF00U;
    reset(start, GPIO_PIN_SET);
    process(start + 1000U, GPIO_PIN_SET);
    process(start + 2000U, GPIO_PIN_SET);
    process(start + 2999U, GPIO_PIN_SET);
    assert(MasterRain_GetState(start + 2999U) == 0xFFU);
    process(start + 3000U, GPIO_PIN_SET);
    assert(MasterRain_GetState(start + 3000U) == 0U);
    assert(MasterRain_GetState(start + 5000U) == 0xFFU);
}

int main(void)
{
    test_pin_and_dry_start();
    test_wet_start_and_clear_boundary();
    test_bounce_restarts_confirmation();
    test_stale_gap_and_reinitialize();
    test_tick_wrap();
    puts("5 PA11 rain test groups passed (GPIO, debounce, stale gap and wrap)");
    return 0;
}
