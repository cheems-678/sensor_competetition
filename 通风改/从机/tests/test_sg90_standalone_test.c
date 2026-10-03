#include <stdint.h>
#include <stdio.h>

#include "sg90_standalone_test.h"
#include "sg90_test_pwm.h"
#include "stm32f1xx_hal.h"

#define EVENT_CAPACITY 1024U
#define NO_FAILURE UINT32_MAX

typedef enum
{
    EVENT_DELAY,
    EVENT_PULSE
} TestEventKind;

typedef struct
{
    TestEventKind kind;
    uint32_t value;
    uint32_t tick;
} TestEvent;

static TestEvent events[EVENT_CAPACITY];
static uint32_t event_count;
static uint32_t delay_calls;
static uint32_t update_calls;
static uint32_t start_calls;
static uint32_t stop_calls;
static uint32_t fake_now;
static uint32_t fail_update_index;
static uint32_t checks;
static uint32_t failures;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        failures++; \
        printf("FAIL line %d: %s\n", __LINE__, #condition); \
    } \
} while (0)

static void record_event(TestEventKind kind, uint32_t value)
{
    CHECK(event_count < EVENT_CAPACITY);
    if (event_count < EVENT_CAPACITY)
    {
        events[event_count].kind = kind;
        events[event_count].value = value;
        events[event_count].tick = fake_now;
        event_count++;
    }
}

void HAL_Delay(uint32_t delay)
{
    delay_calls++;
    record_event(EVENT_DELAY, delay);
    fake_now += delay;
}

uint8_t Sg90TestPwm_SetPulse(uint16_t pulse_us)
{
    uint32_t update_index = update_calls++;

    CHECK(pulse_us >= SG90_TEST_MIN_US && pulse_us <= SG90_TEST_MAX_US);
    record_event(EVENT_PULSE, pulse_us);
    return (update_index == fail_update_index) ? 0U : 1U;
}

uint8_t Sg90TestPwm_Start(uint16_t pulse_us)
{
    (void)pulse_us;
    start_calls++;
    return 1U;
}

void Sg90TestPwm_Stop(void)
{
    stop_calls++;
}

static void reset_test(uint32_t started_at)
{
    event_count = 0U;
    delay_calls = 0U;
    update_calls = 0U;
    start_calls = 0U;
    stop_calls = 0U;
    fake_now = started_at;
    fail_update_index = NO_FAILURE;
}

static void check_event_pairs(uint32_t started_at, uint32_t pair_count)
{
    static const uint16_t expected[] = {1300U, 1500U, 1700U, 1500U};
    uint32_t index;

    CHECK(event_count == pair_count * 2U);
    for (index = 0U; index < pair_count; index++)
    {
        const TestEvent *delay = &events[index * 2U];
        const TestEvent *pulse = &events[index * 2U + 1U];

        CHECK(delay->kind == EVENT_DELAY);
        CHECK(delay->value == 2000U);
        CHECK(delay->tick == started_at + index * 2000U);
        CHECK(pulse->kind == EVENT_PULSE);
        CHECK(pulse->value == expected[index % 4U]);
        CHECK(pulse->tick == started_at + (index + 1U) * 2000U);
        CHECK((uint32_t)(pulse->tick - delay->tick) == 2000U);
    }
}

static void test_repeated_cycles(uint32_t started_at, uint32_t cycle_count)
{
    uint32_t cycle;

    reset_test(started_at);
    for (cycle = 0U; cycle < cycle_count; cycle++)
    {
        CHECK(Sg90StandaloneTest_RunCycle() == 1U);
        CHECK(delay_calls == (cycle + 1U) * 4U);
        CHECK(update_calls == (cycle + 1U) * 4U);
        CHECK(fake_now == started_at + (cycle + 1U) * 8000U);
        CHECK(start_calls == 0U && stop_calls == 0U);
    }
    check_event_pairs(started_at, cycle_count * 4U);
}

static void test_each_update_failure(void)
{
    uint32_t failed_position;

    for (failed_position = 0U; failed_position < 4U; failed_position++)
    {
        reset_test(42U);
        fail_update_index = failed_position;
        CHECK(Sg90StandaloneTest_RunCycle() == 0U);
        CHECK(update_calls == failed_position + 1U);
        CHECK(delay_calls == failed_position + 1U);
        CHECK(fake_now == 42U + (failed_position + 1U) * 2000U);
        CHECK(start_calls == 0U && stop_calls == 0U);
        check_event_pairs(42U, failed_position + 1U);
    }
}

static void test_later_cycle_failure(void)
{
    reset_test(0U);
    fail_update_index = 6U;
    CHECK(Sg90StandaloneTest_RunCycle() == 1U);
    CHECK(Sg90StandaloneTest_RunCycle() == 0U);
    CHECK(update_calls == 7U && delay_calls == 7U);
    CHECK(fake_now == 14000U);
    CHECK(start_calls == 0U && stop_calls == 0U);
    check_event_pairs(0U, 7U);
}

int main(void)
{
    CHECK(SG90_STANDALONE_HOLD_MS == 2000U);
    test_repeated_cycles(0U, 1U);
    test_repeated_cycles(123U, 100U);
    test_repeated_cycles(UINT32_MAX - 1000U, 3U);
    test_each_update_failure();
    test_later_cycle_failure();
    printf("SG90 standalone cycle test: %lu checks, %lu failures\n",
           (unsigned long)checks, (unsigned long)failures);
    return (failures == 0U) ? 0 : 1;
}
