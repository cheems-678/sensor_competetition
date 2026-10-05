#include <stdint.h>
#include <stdio.h>

#include "slave_servo_test.h"
#include "sg90_test_pwm.h"

static uint32_t checks;
static uint32_t failures;
static uint32_t start_calls;
static uint32_t update_calls;
static uint32_t stop_calls;
static uint32_t fake_now;
static uint32_t previous_update_tick;
static uint16_t previous_pulse;
static uint8_t start_success;
static uint8_t update_success;
static uint8_t manual_updates;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        failures++; \
        printf("FAIL line %d: %s\n", __LINE__, #condition); \
    } \
} while (0)

static uint16_t extra_pulses[3];
static unsigned extra_updates[3];
static uint8_t extra_success[3] = {1U, 1U, 1U};
uint8_t Sg90TestPwm_StartChannel(uint8_t id, uint16_t pulse)
{
    if (id < 2U || id > 4U) { return 0U; }
    extra_pulses[id - 2U] = pulse;
    extra_updates[id - 2U] = 0U;
    extra_success[id - 2U] = 1U;
    return 1U;
}
uint8_t Sg90TestPwm_SetChannelPulse(uint8_t id, uint16_t pulse)
{
    if (id < 2U || id > 4U || !extra_success[id - 2U]) { return 0U; }
    extra_pulses[id - 2U] = pulse;
    extra_updates[id - 2U]++;
    return 1U;
}
void Sg90TestPwm_StopChannel(uint8_t id)
{
    if (id >= 2U && id <= 4U) { extra_pulses[id - 2U] = 0U; }
}
uint8_t Sg90TestPwm_IsChannelRunning(uint8_t id)
{
    if (id == 1U) { return (uint8_t)(previous_pulse != 0U); }
    return (uint8_t)(id >= 2U && id <= 4U && extra_pulses[id - 2U] != 0U);
}

uint8_t Sg90TestPwm_Start(uint16_t pulse_us)
{
    start_calls++;
    CHECK(pulse_us == ((manual_updates != 0U) ?
          SLAVE_SERVO_WINDOW_STOP_US : SG90_TEST_CENTER_US));
    previous_pulse = pulse_us;
    previous_update_tick = fake_now;
    return start_success;
}

uint8_t Sg90TestPwm_SetPulse(uint16_t pulse_us)
{
    uint32_t delta = (pulse_us > previous_pulse) ?
                     pulse_us - previous_pulse : previous_pulse - pulse_us;
    update_calls++;
    CHECK(pulse_us >= SG90_TEST_MIN_US && pulse_us <= SG90_TEST_MAX_US);
    if (manual_updates == 0U)
    {
        CHECK(delta <= SLAVE_SERVO_TEST_STEP_US);
        CHECK((uint32_t)(fake_now - previous_update_tick) >= SG90_TEST_PERIOD_MS);
    }
    previous_pulse = pulse_us;
    previous_update_tick = fake_now;
    return update_success;
}

void Sg90TestPwm_Stop(void)
{
    stop_calls++;
}

static void reset_test(uint32_t now_ms)
{
    start_calls = update_calls = stop_calls = 0U;
    start_success = update_success = 1U;
    manual_updates = 0U;
    fake_now = now_ms;
    SlaveServoTest_Init(now_ms);
}

static void process_at(uint32_t now_ms)
{
    fake_now = now_ms;
    SlaveServoTest_Process(now_ms);
}

static void check_full_sequence(uint32_t started_at)
{
    static const uint16_t expected[] = {1500U, 1300U, 1500U, 1700U, 1500U};
    uint32_t elapsed;
    uint32_t previous_stage = 0U;
    uint32_t previous_state = SLAVE_SERVO_TEST_HOLDING;
    uint32_t hold_started_at = started_at;
    uint32_t holds_seen = 1U;

    reset_test(started_at);
    CHECK(start_calls == 1U && stop_calls == 0U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_HOLDING);
    CHECK(SlaveServoTestDiag.pulse_us == SG90_TEST_CENTER_US);
    for (elapsed = 0U; elapsed <= 13000U; elapsed++)
    {
        uint32_t now_ms = started_at + elapsed;
        uint32_t calls_before = update_calls;
        process_at(now_ms);
        CHECK(update_calls <= calls_before + 1U);
        CHECK(SlaveServoTestDiag.stage_index < 5U);
        CHECK(SlaveServoTestDiag.target_us == expected[SlaveServoTestDiag.stage_index]);
        if (SlaveServoTestDiag.stage_index != previous_stage)
        {
            CHECK(SlaveServoTestDiag.stage_index == previous_stage + 1U);
            CHECK((uint32_t)(now_ms - hold_started_at) >=
                  SLAVE_SERVO_TEST_HOLD_MS + SG90_TEST_PERIOD_MS);
            CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MOVING);
            previous_stage = SlaveServoTestDiag.stage_index;
        }
        if ((SlaveServoTestDiag.state == SLAVE_SERVO_TEST_HOLDING) &&
            (previous_state == SLAVE_SERVO_TEST_MOVING))
        {
            CHECK(SlaveServoTestDiag.pulse_us == expected[previous_stage]);
            hold_started_at = now_ms;
            holds_seen++;
        }
        previous_state = SlaveServoTestDiag.state;
    }
    CHECK(holds_seen == 5U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_COMPLETE);
    CHECK(SlaveServoTestDiag.stage_index == 4U);
    CHECK(SlaveServoTestDiag.pulse_us == SG90_TEST_CENTER_US);
    CHECK(SlaveServoTestDiag.last_error == SLAVE_SERVO_TEST_ERROR_NONE);
    CHECK(update_calls == 80U && SlaveServoTestDiag.update_count == 80U);
    CHECK(start_calls == 1U && stop_calls == 0U);
    process_at(started_at + 500000U);
    process_at(started_at + 500020U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_COMPLETE);
    CHECK(update_calls == 80U && start_calls == 1U);
}

static void test_hold_and_step_boundaries(void)
{
    reset_test(100U);
    process_at(2119U);
    CHECK(SlaveServoTestDiag.stage_index == 0U && update_calls == 0U);
    process_at(2120U);
    CHECK(SlaveServoTestDiag.stage_index == 1U && update_calls == 0U);
    process_at(2139U);
    CHECK(update_calls == 0U);
    process_at(2140U);
    CHECK(update_calls == 1U && SlaveServoTestDiag.pulse_us == 1490U);
    process_at(2140U);
    process_at(2159U);
    CHECK(update_calls == 1U);
    process_at(2160U);
    CHECK(update_calls == 2U && SlaveServoTestDiag.pulse_us == 1480U);
}

static void test_long_pause_does_not_catch_up(void)
{
    reset_test(0U);
    process_at(100000U);
    CHECK(SlaveServoTestDiag.stage_index == 1U && update_calls == 0U);
    process_at(200000U);
    CHECK(update_calls == 1U && SlaveServoTestDiag.pulse_us == 1490U);
    process_at(200000U);
    process_at(200019U);
    CHECK(update_calls == 1U);
    process_at(200020U);
    CHECK(update_calls == 2U && SlaveServoTestDiag.pulse_us == 1480U);
    CHECK(SlaveServoTestDiag.stage_index == 1U);
}

static void test_start_failure_stops_without_retry(void)
{
    reset_test(0U);
    start_success = 0U;
    fake_now = 42U;
    SlaveServoTest_Init(fake_now);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT);
    CHECK(SlaveServoTestDiag.last_error == SLAVE_SERVO_TEST_ERROR_START);
    CHECK(SlaveServoTestDiag.pulse_us == 0U && stop_calls == 1U);
    process_at(10000U);
    process_at(20000U);
    CHECK(start_calls == 2U && update_calls == 0U && stop_calls == 1U);
}

static void test_update_failure_stops_without_retry(void)
{
    reset_test(0U);
    process_at(2020U);
    update_success = 0U;
    process_at(2040U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT);
    CHECK(SlaveServoTestDiag.last_error == SLAVE_SERVO_TEST_ERROR_UPDATE);
    CHECK(SlaveServoTestDiag.pulse_us == 0U && stop_calls == 1U);
    CHECK(SlaveServoTestDiag.update_count == 0U);
    process_at(10000U);
    process_at(20000U);
    CHECK(start_calls == 1U && update_calls == 1U && stop_calls == 1U);
}

static void reset_manual(uint32_t now_ms)
{
    start_calls = update_calls = stop_calls = 0U;
    start_success = update_success = 1U;
    manual_updates = 1U;
    fake_now = now_ms;
    SlaveServoTest_InitManual(now_ms);
}

static uint8_t set_window_at(uint8_t open, uint32_t now_ms)
{
    fake_now = now_ms;
    return SlaveServoTest_SetWindow(open, now_ms);
}

static void test_manual_timed_actions_and_duplicates(void)
{
    const uint32_t started_at = 100000U;
    const uint32_t close_at = started_at + SLAVE_SERVO_WINDOW_RUN_MS + 1000U;
    reset_manual(0U);
    CHECK(start_calls == 1U && update_calls == 0U && stop_calls == 0U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL);
    CHECK(SlaveServoTestDiag.pulse_us == SLAVE_SERVO_WINDOW_STOP_US);
    CHECK(SlaveServoTestDiag.target_us == SLAVE_SERVO_WINDOW_STOP_US);
    process_at(started_at);
    CHECK(update_calls == 0U && SlaveServoTestDiag.stage_index == 0U);
    CHECK(set_window_at(1U, started_at) == 1U);
    CHECK(update_calls == 1U && previous_pulse == SG90_TEST_MAX_US);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL_RUNNING);
    CHECK(set_window_at(1U, started_at + SLAVE_SERVO_WINDOW_RUN_MS - 1U) == 1U);
    CHECK(update_calls == 1U && SlaveServoTestDiag.last_update_tick == started_at);
    process_at(started_at + SLAVE_SERVO_WINDOW_RUN_MS - 1U);
    CHECK(previous_pulse == SG90_TEST_MAX_US && update_calls == 1U);
    process_at(started_at + SLAVE_SERVO_WINDOW_RUN_MS);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL);
    CHECK(previous_pulse == SLAVE_SERVO_WINDOW_STOP_US && update_calls == 2U);
    CHECK(SlaveServoTestDiag.target_us == SLAVE_SERVO_WINDOW_STOP_US);
    process_at(close_at);
    CHECK(update_calls == 2U);
    CHECK(set_window_at(0U, close_at) == 1U);
    CHECK(previous_pulse == SG90_TEST_MIN_US && update_calls == 3U);
    CHECK(set_window_at(0U, close_at + SLAVE_SERVO_WINDOW_RUN_MS - 1U) == 1U);
    CHECK(update_calls == 3U);
    CHECK(set_window_at(2U, close_at + SLAVE_SERVO_WINDOW_RUN_MS - 1U) == 0U);
    process_at(close_at + SLAVE_SERVO_WINDOW_RUN_MS);
    CHECK(previous_pulse == SLAVE_SERVO_WINDOW_STOP_US && update_calls == 4U);
    CHECK(SlaveServoTestDiag.update_count == 4U);
    CHECK(start_calls == 1U && stop_calls == 0U);
}

static void test_manual_reversal_and_expired_restart(void)
{
    reset_manual(0U);
    CHECK(set_window_at(1U, 100U) == 1U);
    CHECK(set_window_at(0U, 200U) == 1U);
    process_at(100U + SLAVE_SERVO_WINDOW_RUN_MS);
    CHECK(previous_pulse == SG90_TEST_MIN_US && update_calls == 2U);
    process_at(200U + SLAVE_SERVO_WINDOW_RUN_MS - 1U);
    CHECK(previous_pulse == SG90_TEST_MIN_US && update_calls == 2U);
    process_at(200U + SLAVE_SERVO_WINDOW_RUN_MS);
    CHECK(previous_pulse == SLAVE_SERVO_WINDOW_STOP_US && update_calls == 3U);

    reset_manual(0U);
    CHECK(set_window_at(1U, 0U) == 1U);
    /* No Process call: a newly arriving command must first stop the expired one. */
    CHECK(set_window_at(1U, SLAVE_SERVO_WINDOW_RUN_MS) == 1U);
    CHECK(previous_pulse == SG90_TEST_MAX_US && update_calls == 3U);
    process_at(2U * SLAVE_SERVO_WINDOW_RUN_MS - 1U);
    CHECK(update_calls == 3U);
    process_at(2U * SLAVE_SERVO_WINDOW_RUN_MS);
    CHECK(previous_pulse == SLAVE_SERVO_WINDOW_STOP_US && update_calls == 4U);
    CHECK(start_calls == 1U && stop_calls == 0U);
}

static void test_manual_wrap_and_long_pause(void)
{
    const uint32_t started_at = UINT32_MAX - 100U;
    reset_manual(started_at);
    CHECK(set_window_at(1U, started_at) == 1U);
    process_at(started_at + SLAVE_SERVO_WINDOW_RUN_MS - 1U);
    CHECK(previous_pulse == SG90_TEST_MAX_US && update_calls == 1U);
    process_at(started_at + SLAVE_SERVO_WINDOW_RUN_MS);
    CHECK(previous_pulse == SLAVE_SERVO_WINDOW_STOP_US && update_calls == 2U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL);

    reset_manual(0U);
    CHECK(set_window_at(0U, 0U) == 1U);
    process_at(100000U);
    CHECK(previous_pulse == SLAVE_SERVO_WINDOW_STOP_US && update_calls == 2U);
    process_at(100000U);
    process_at(100020U);
    CHECK(update_calls == 2U && start_calls == 1U && stop_calls == 0U);
}

static void test_manual_stop_failure_and_no_retry(void)
{
    reset_manual(0U);
    CHECK(set_window_at(1U, 10U) == 1U);
    update_success = 0U;
    process_at(10U + SLAVE_SERVO_WINDOW_RUN_MS);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT);
    CHECK(SlaveServoTestDiag.last_error == SLAVE_SERVO_TEST_ERROR_UPDATE);
    CHECK(SlaveServoTestDiag.pulse_us == 0U && stop_calls == 1U);
    CHECK(update_calls == 2U && SlaveServoTestDiag.update_count == 1U);
    CHECK(set_window_at(0U, 10000U) == 0U);
    process_at(20000U);
    CHECK(update_calls == 2U && start_calls == 1U && stop_calls == 1U);

    reset_manual(0U);
    CHECK(set_window_at(0U, 0U) == 1U);
    update_success = 0U;
    CHECK(set_window_at(1U, SLAVE_SERVO_WINDOW_RUN_MS) == 0U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT);
    CHECK(update_calls == 2U && stop_calls == 1U);
}

static void test_manual_failure_and_automatic_ownership(void)
{
    reset_manual(0U);
    update_success = 0U;
    CHECK(SlaveServoTest_SetWindow(1U, 10U) == 0U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT);
    CHECK(SlaveServoTestDiag.last_error == SLAVE_SERVO_TEST_ERROR_UPDATE);
    CHECK(SlaveServoTestDiag.pulse_us == 0U && stop_calls == 1U);
    CHECK(SlaveServoTestDiag.update_count == 0U);
    CHECK(SlaveServoTest_SetWindow(0U, 20U) == 0U);
    process_at(100000U);
    CHECK(update_calls == 1U && start_calls == 1U && stop_calls == 1U);

    reset_manual(0U);
    start_success = 0U;
    SlaveServoTest_InitManual(10U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_FAULT);
    CHECK(SlaveServoTestDiag.last_error == SLAVE_SERVO_TEST_ERROR_START);
    CHECK(SlaveServoTest_SetWindow(1U, 20U) == 0U);
    CHECK(update_calls == 0U && stop_calls == 1U);

    reset_test(0U);
    CHECK(SlaveServoTest_SetWindow(1U, 10U) == 0U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_HOLDING);
    CHECK(update_calls == 0U && stop_calls == 0U);
}

static void test_four_independent_channels(void)
{
    uint32_t base = UINT32_MAX - 100U;
    const uint32_t run = SLAVE_SERVO_WINDOW_RUN_MS;
    uint8_t id;
    reset_manual(base);
    for (id = 1U; id <= 4U; id++)
    {
        CHECK(SlaveServoTest_GetDiagnostics(id)->state == SLAVE_SERVO_TEST_MANUAL);
        CHECK(SlaveServoTest_GetDiagnostics(id)->pulse_us == SLAVE_SERVO_WINDOW_STOP_US);
        CHECK(SlaveServoTest_SetWindowChannel(id, id & 1U, base + (id - 1U) * 20U) == 1U);
    }
    CHECK(previous_pulse == 1700U && extra_pulses[0] == 1300U);
    CHECK(extra_pulses[1] == 1700U && extra_pulses[2] == 1300U);
    CHECK(SlaveServoTest_SetWindowChannel(2U, 0U, base + 70U) == 1U);
    CHECK(extra_updates[0] == 1U); /* Duplicate must not extend ID 2's deadline. */
    process_at(base + run);
    CHECK(previous_pulse == SLAVE_SERVO_WINDOW_STOP_US);
    CHECK(extra_pulses[0] == 1300U && extra_pulses[1] == 1700U);
    CHECK(SlaveServoTest_SetWindowChannel(3U, 0U, base + run) == 1U);
    process_at(base + run + 20U);
    CHECK(extra_pulses[0] == SLAVE_SERVO_WINDOW_STOP_US);
    CHECK(extra_pulses[1] == 1300U && extra_pulses[2] == 1300U);
    process_at(base + run + 60U);
    CHECK(extra_pulses[2] == SLAVE_SERVO_WINDOW_STOP_US && extra_pulses[1] == 1300U);
    process_at(base + 2U * run);
    CHECK(extra_pulses[1] == SLAVE_SERVO_WINDOW_STOP_US);
    CHECK(SlaveServoTest_GetDiagnostics(0U) == 0);
    CHECK(SlaveServoTest_GetDiagnostics(5U) == 0);
    CHECK(SlaveServoTest_SetWindowChannel(0U, 1U, 0U) == 0U);
    CHECK(SlaveServoTest_SetWindowChannel(5U, 0U, 0U) == 0U);
    CHECK(SlaveServoTest_SetWindowChannel(2U, 2U, 0U) == 0U);

    reset_manual(0U);
    CHECK(SlaveServoTest_SetWindowChannel(2U, 1U, 10U) == 1U);
    CHECK(SlaveServoTest_SetWindowChannel(3U, 0U, 20U) == 1U);
    extra_success[0] = 0U;
    process_at(10U + run);
    CHECK(SlaveServoTest_GetDiagnostics(2U)->state == SLAVE_SERVO_TEST_FAULT);
    CHECK(extra_pulses[0] == 0U && extra_pulses[1] == 1300U);
    CHECK(SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL);
    process_at(20U + run);
    CHECK(extra_pulses[1] == SLAVE_SERVO_WINDOW_STOP_US);
    CHECK(SlaveServoTest_SetWindowChannel(4U, 1U, 10000U) == 1U);
    CHECK(extra_pulses[2] == 1700U);
}

int main(void)
{
    test_four_independent_channels();
    check_full_sequence(0U);
    check_full_sequence(UINT32_MAX - 100U);
    check_full_sequence(UINT32_MAX - 2100U);
    test_hold_and_step_boundaries();
    test_long_pause_does_not_catch_up();
    test_start_failure_stops_without_retry();
    test_update_failure_stops_without_retry();
    test_manual_timed_actions_and_duplicates();
    test_manual_reversal_and_expired_restart();
    test_manual_wrap_and_long_pause();
    test_manual_stop_failure_and_no_retry();
    test_manual_failure_and_automatic_ownership();
    printf("SG90 four-channel/legacy test: %lu checks, %lu failures\n",
           (unsigned long)checks, (unsigned long)failures);
    return (failures == 0U) ? 0 : 1;
}
