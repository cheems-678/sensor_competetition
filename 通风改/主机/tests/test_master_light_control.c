#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"
#include "master_config.h"
#include "master_light_control.h"
#include "ws2812.h"

typedef struct
{
    uint32_t tick;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t success;
} LightAttempt;

GPIO_TypeDef TestGpioA;
static uint32_t tick;
static GPIO_PinState sensor_level;
static GPIO_InitTypeDef gpio_config;
static unsigned gpio_clock_count, gpio_init_count, gpio_read_count;
static unsigned ws2812_init_count, attempt_count, failures_remaining;
static LightAttempt attempts[32];

uint32_t HAL_GetTick(void) { return tick; }

void HAL_Delay(uint32_t delay)
{
    (void)delay;
    assert(!"Light confirmation and retries must not block on HAL_Delay");
}

void TestGpioAClockEnable(void) { gpio_clock_count++; }

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config)
{
    assert(port == GPIOA);
    assert(gpio_clock_count != 0U);
    gpio_config = *config;
    gpio_init_count++;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    assert(port == GPIOA && pin == GPIO_PIN_7);
    assert(gpio_init_count != 0U);
    gpio_read_count++;
    return sensor_level;
}

void Ws2812_Init(void) { ws2812_init_count++; }

uint8_t Ws2812_SetSolid(uint8_t red, uint8_t green, uint8_t blue)
{
    LightAttempt *attempt;
    assert(ws2812_init_count != 0U);
    assert(attempt_count < sizeof(attempts) / sizeof(attempts[0]));
    attempt = &attempts[attempt_count++];
    attempt->tick = tick;
    attempt->red = red;
    attempt->green = green;
    attempt->blue = blue;
    attempt->success = failures_remaining == 0U;
    if (failures_remaining != 0U) { failures_remaining--; }
    return attempt->success;
}

static void reset(uint32_t start, GPIO_PinState level, unsigned failures)
{
    tick = start;
    sensor_level = level;
    gpio_clock_count = gpio_init_count = gpio_read_count = 0U;
    ws2812_init_count = attempt_count = 0U;
    failures_remaining = failures;
    memset(&gpio_config, 0, sizeof(gpio_config));
    memset(attempts, 0, sizeof(attempts));
    MasterLight_Init(start);
}

static void process(uint32_t now, GPIO_PinState level)
{
    unsigned reads_before = gpio_read_count;
    tick = now;
    sensor_level = level;
    MasterLight_Process(now);
    assert(gpio_read_count > reads_before);
}

static void assert_attempt(unsigned index, uint32_t time, uint8_t level,
                           uint8_t success)
{
    assert(index < attempt_count);
    assert(attempts[index].tick == time);
    assert(attempts[index].red == level && attempts[index].green == level);
    assert(attempts[index].blue == level && attempts[index].success == success);
}

static void test_gpio_parameters_and_bright_start(void)
{
    assert(MASTER_LIGHT_STABLE_MS == 1000UL);
    assert(MASTER_LIGHT_RETRY_MS == 1000UL);
    assert(MASTER_LIGHT_WHITE_LEVEL == 51U);
    reset(500U, GPIO_PIN_RESET, 0U);
    assert(gpio_clock_count == 1U && gpio_init_count == 1U);
    assert(gpio_config.Pin == GPIO_PIN_7 && gpio_config.Mode == GPIO_MODE_INPUT);
    assert(gpio_config.Pull == GPIO_NOPULL);
    assert(gpio_config.Speed == GPIO_SPEED_FREQ_LOW);
    assert(ws2812_init_count == 1U && gpio_read_count != 0U);
    assert(attempt_count == 1U);
    assert_attempt(0U, 500U, 0U, 1U);
    assert(MasterLightDiag.light_on == 0U && MasterLightDiag.output_valid == 1U);
    assert(MasterLightDiag.raw_dark == 0U && MasterLightDiag.input_valid == 0U);
    assert(MasterLightDiag.toggle_count == 0U);
    assert(MasterLightDiag.last_toggle_ms == 0U);
    process(1499U, GPIO_PIN_RESET);
    assert(MasterLightDiag.input_valid == 0U && attempt_count == 1U);
    process(1500U, GPIO_PIN_RESET);
    assert(MasterLightDiag.input_valid == 1U && MasterLightDiag.stable_dark == 0U);
    process(10000U, GPIO_PIN_RESET);
    assert(attempt_count == 1U && MasterLightDiag.toggle_count == 0U);
}

static void test_dark_start_and_no_repeated_white(void)
{
    reset(100U, GPIO_PIN_SET, 0U);
    assert_attempt(0U, 100U, 0U, 1U);
    assert(MasterLightDiag.raw_dark == 1U && MasterLightDiag.light_on == 0U);
    process(1099U, GPIO_PIN_SET);
    assert(attempt_count == 1U && MasterLightDiag.input_valid == 0U);
    process(1100U, GPIO_PIN_SET);
    assert(attempt_count == 2U);
    assert_attempt(1U, 1100U, 51U, 1U);
    assert(MasterLightDiag.input_valid == 1U && MasterLightDiag.stable_dark == 1U);
    assert(MasterLightDiag.light_on == 1U && MasterLightDiag.output_valid == 1U);
    assert(MasterLightDiag.toggle_count == 1U && MasterLightDiag.last_toggle_ms == 1100U);
    process(1101U, GPIO_PIN_SET);
    process(20000U, GPIO_PIN_SET);
    assert(attempt_count == 2U && MasterLightDiag.toggle_count == 1U);
}

static void test_bidirectional_confirmation_and_short_pulses(void)
{
    reset(0U, GPIO_PIN_RESET, 0U);
    process(1000U, GPIO_PIN_RESET);
    process(1100U, GPIO_PIN_SET);
    process(2099U, GPIO_PIN_SET);
    assert(MasterLightDiag.raw_dark == 1U && MasterLightDiag.stable_dark == 0U);
    assert(attempt_count == 1U);
    process(2099U, GPIO_PIN_RESET);
    process(2100U, GPIO_PIN_SET);
    process(3099U, GPIO_PIN_SET);
    assert(attempt_count == 1U);
    process(3100U, GPIO_PIN_SET);
    assert_attempt(1U, 3100U, 51U, 1U);
    process(4000U, GPIO_PIN_RESET);
    process(4999U, GPIO_PIN_RESET);
    assert(MasterLightDiag.raw_dark == 0U && MasterLightDiag.stable_dark == 1U);
    assert(MasterLightDiag.light_on == 1U && attempt_count == 2U);
    process(4999U, GPIO_PIN_SET);
    process(5000U, GPIO_PIN_RESET);
    process(5999U, GPIO_PIN_RESET);
    assert(attempt_count == 2U);
    process(6000U, GPIO_PIN_RESET);
    assert_attempt(2U, 6000U, 0U, 1U);
    assert(MasterLightDiag.stable_dark == 0U && MasterLightDiag.light_on == 0U);
    assert(MasterLightDiag.toggle_count == 2U && MasterLightDiag.last_toggle_ms == 6000U);
    process(10000U, GPIO_PIN_RESET);
    assert(attempt_count == 3U);
}

static void test_initial_black_failure_retries_and_reinitialization(void)
{
    reset(100U, GPIO_PIN_RESET, 2U);
    assert_attempt(0U, 100U, 0U, 0U);
    assert(MasterLightDiag.output_valid == 0U);
    assert(MasterLightDiag.ws2812_failure_count == 1U);
    process(1099U, GPIO_PIN_RESET);
    assert(attempt_count == 1U && MasterLightDiag.output_valid == 0U);
    process(1100U, GPIO_PIN_RESET);
    assert_attempt(1U, 1100U, 0U, 0U);
    assert(MasterLightDiag.output_valid == 0U && MasterLightDiag.light_on == 0U);
    assert(MasterLightDiag.ws2812_failure_count == 2U);
    process(2099U, GPIO_PIN_RESET);
    assert(attempt_count == 2U);
    process(2100U, GPIO_PIN_RESET);
    assert_attempt(2U, 2100U, 0U, 1U);
    assert(MasterLightDiag.output_valid == 1U && MasterLightDiag.light_on == 0U);
    assert(MasterLightDiag.toggle_count == 0U && MasterLightDiag.last_toggle_ms == 0U);
    process(9000U, GPIO_PIN_RESET);
    assert(attempt_count == 3U);
    reset(10000U, GPIO_PIN_SET, 0U);
    assert(attempt_count == 1U && MasterLightDiag.input_valid == 0U);
    assert(MasterLightDiag.ws2812_failure_count == 0U && MasterLightDiag.toggle_count == 0U);
    process(10999U, GPIO_PIN_SET);
    assert(attempt_count == 1U);
    process(11000U, GPIO_PIN_SET);
    assert_attempt(1U, 11000U, 51U, 1U);

    /* A failed startup frame retries the current confirmed target. */
    reset(12000U, GPIO_PIN_SET, 1U);
    process(12999U, GPIO_PIN_SET);
    assert(attempt_count == 1U && MasterLightDiag.output_valid == 0U);
    process(13000U, GPIO_PIN_SET);
    assert_attempt(1U, 13000U, 51U, 1U);
    assert(MasterLightDiag.light_on == 1U && MasterLightDiag.output_valid == 1U);
    assert(MasterLightDiag.toggle_count == 0U);
    assert(MasterLightDiag.ws2812_failure_count == 1U);
}

static void test_open_and_close_failures_preserve_actual_state(void)
{
    reset(0U, GPIO_PIN_RESET, 0U);
    process(1000U, GPIO_PIN_RESET);
    process(1010U, GPIO_PIN_SET);
    failures_remaining = 2U;
    process(2010U, GPIO_PIN_SET);
    assert_attempt(1U, 2010U, 51U, 0U);
    assert(MasterLightDiag.light_on == 0U && MasterLightDiag.output_valid == 1U);
    assert(MasterLightDiag.toggle_count == 0U && MasterLightDiag.last_toggle_ms == 0U);
    assert(MasterLightDiag.ws2812_failure_count == 1U);
    process(3009U, GPIO_PIN_SET);
    assert(attempt_count == 2U);
    process(3010U, GPIO_PIN_SET);
    assert_attempt(2U, 3010U, 51U, 0U);
    assert(MasterLightDiag.light_on == 0U && MasterLightDiag.toggle_count == 0U);
    process(4009U, GPIO_PIN_SET);
    assert(attempt_count == 3U);
    process(4010U, GPIO_PIN_SET);
    assert_attempt(3U, 4010U, 51U, 1U);
    assert(MasterLightDiag.light_on == 1U && MasterLightDiag.toggle_count == 1U);
    assert(MasterLightDiag.last_toggle_ms == 4010U);
    process(4100U, GPIO_PIN_RESET);
    failures_remaining = 1U;
    process(5100U, GPIO_PIN_RESET);
    assert_attempt(4U, 5100U, 0U, 0U);
    assert(MasterLightDiag.stable_dark == 0U && MasterLightDiag.light_on == 1U);
    assert(MasterLightDiag.toggle_count == 1U && MasterLightDiag.last_toggle_ms == 4010U);
    assert(MasterLightDiag.ws2812_failure_count == 3U);
    process(6099U, GPIO_PIN_RESET);
    assert(attempt_count == 5U);
    process(6100U, GPIO_PIN_RESET);
    assert_attempt(5U, 6100U, 0U, 1U);
    assert(MasterLightDiag.light_on == 0U && MasterLightDiag.toggle_count == 2U);
    assert(MasterLightDiag.last_toggle_ms == 6100U);
    process(20000U, GPIO_PIN_RESET);
    assert(attempt_count == 6U);
}

static void test_confirmed_target_cancels_old_failed_command(void)
{
    reset(0U, GPIO_PIN_RESET, 0U);
    process(1000U, GPIO_PIN_RESET);
    process(1010U, GPIO_PIN_SET);
    failures_remaining = 1U;
    process(2010U, GPIO_PIN_SET);
    assert_attempt(1U, 2010U, 51U, 0U);
    process(2020U, GPIO_PIN_RESET);
    process(3009U, GPIO_PIN_RESET);
    assert(attempt_count == 2U && MasterLightDiag.stable_dark == 1U);
    process(3020U, GPIO_PIN_RESET);
    assert(MasterLightDiag.stable_dark == 0U && MasterLightDiag.light_on == 0U);
    /* A failed white frame may have changed some pixels; restore all black. */
    assert_attempt(2U, 3020U, 0U, 1U);
    assert(attempt_count == 3U && MasterLightDiag.toggle_count == 0U);
    assert(MasterLightDiag.last_toggle_ms == 0U);
    process(3500U, GPIO_PIN_RESET);
    assert(attempt_count == 3U);
    process(3600U, GPIO_PIN_SET);
    process(4600U, GPIO_PIN_SET);
    assert_attempt(3U, 4600U, 51U, 1U);
    assert(MasterLightDiag.toggle_count == 1U);

    /* Likewise, a failed black frame is replaced by the newly stable white. */
    process(4700U, GPIO_PIN_RESET);
    failures_remaining = 1U;
    process(5700U, GPIO_PIN_RESET);
    assert_attempt(4U, 5700U, 0U, 0U);
    process(5800U, GPIO_PIN_SET);
    process(6699U, GPIO_PIN_SET);
    assert(attempt_count == 5U);
    process(6800U, GPIO_PIN_SET);
    assert_attempt(5U, 6800U, 51U, 1U);
    assert(MasterLightDiag.light_on == 1U && MasterLightDiag.toggle_count == 1U);
    assert(MasterLightDiag.last_toggle_ms == 4600U);
    process(10000U, GPIO_PIN_SET);
    assert(attempt_count == 6U);
}

static void test_input_confirmation_across_tick_wrap(void)
{
    uint32_t start = UINT32_MAX - 499U;
    reset(start, GPIO_PIN_SET, 0U);
    process((uint32_t)(start + 999U), GPIO_PIN_SET);
    assert(attempt_count == 1U && MasterLightDiag.input_valid == 0U);
    process((uint32_t)(start + 1000U), GPIO_PIN_SET);
    assert_attempt(1U, (uint32_t)(start + 1000U), 51U, 1U);
    assert(MasterLightDiag.light_on == 1U && MasterLightDiag.toggle_count == 1U);
    reset(0U, GPIO_PIN_SET, 0U);
    process(1000U, GPIO_PIN_SET);
    process(start, GPIO_PIN_RESET);
    process((uint32_t)(start + 999U), GPIO_PIN_RESET);
    assert(attempt_count == 2U && MasterLightDiag.light_on == 1U);
    process((uint32_t)(start + 1000U), GPIO_PIN_RESET);
    assert_attempt(2U, (uint32_t)(start + 1000U), 0U, 1U);
    assert(MasterLightDiag.light_on == 0U && MasterLightDiag.toggle_count == 2U);
}

static void test_retry_across_tick_wrap(void)
{
    uint32_t start = UINT32_MAX - 2000U;
    uint32_t failure_time = (uint32_t)(start + 1001U);
    reset(start, GPIO_PIN_RESET, 0U);
    process((uint32_t)(start + 1U), GPIO_PIN_SET);
    failures_remaining = 1U;
    process(failure_time, GPIO_PIN_SET);
    assert_attempt(1U, failure_time, 51U, 0U);
    process((uint32_t)(failure_time + 999U), GPIO_PIN_SET);
    assert(attempt_count == 2U && MasterLightDiag.light_on == 0U);
    process((uint32_t)(failure_time + 1000U), GPIO_PIN_SET);
    assert_attempt(2U, (uint32_t)(failure_time + 1000U), 51U, 1U);
    assert(MasterLightDiag.light_on == 1U && MasterLightDiag.toggle_count == 1U);
    assert(MasterLightDiag.last_toggle_ms == (uint32_t)(failure_time + 1000U));

    start = UINT32_MAX - 499U;
    reset(start, GPIO_PIN_RESET, 1U);
    process((uint32_t)(start + 999U), GPIO_PIN_RESET);
    assert(attempt_count == 1U && MasterLightDiag.output_valid == 0U);
    process((uint32_t)(start + 1000U), GPIO_PIN_RESET);
    assert_attempt(1U, (uint32_t)(start + 1000U), 0U, 1U);
    assert(MasterLightDiag.output_valid == 1U && MasterLightDiag.toggle_count == 0U);
}

int main(void)
{
    test_gpio_parameters_and_bright_start();
    test_dark_start_and_no_repeated_white();
    test_bidirectional_confirmation_and_short_pulses();
    test_initial_black_failure_retries_and_reinitialization();
    test_open_and_close_failures_preserve_actual_state();
    test_confirmed_target_cancels_old_failed_command();
    test_input_confirmation_across_tick_wrap();
    test_retry_across_tick_wrap();
    puts("8 master light-control groups passed (GPIO/polarity/debounce/retry/wrap)");
    return 0;
}
