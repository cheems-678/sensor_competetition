#include "slave_servo_test.h"

#include "sg90_test_pwm.h"

static const uint16_t targets_us[] = {
    SG90_TEST_CENTER_US, SG90_TEST_MIN_US, SG90_TEST_CENTER_US,
    SG90_TEST_MAX_US, SG90_TEST_CENTER_US
};

volatile SlaveServoTestDiagnostics SlaveServoTestDiag;
static uint32_t hold_started_at;
static uint32_t step_started_at;
static uint32_t window_started_at;
static uint8_t window_action;

static void SlaveServoTest_Fault(uint32_t error)
{
    Sg90TestPwm_Stop();
    SlaveServoTestDiag.last_error = error;
    SlaveServoTestDiag.pulse_us = 0U;
    SlaveServoTestDiag.state = SLAVE_SERVO_TEST_FAULT;
}

void SlaveServoTest_Init(uint32_t now_ms)
{
    SlaveServoTestDiag.state = SLAVE_SERVO_TEST_NOT_STARTED;
    SlaveServoTestDiag.stage_index = 0U;
    SlaveServoTestDiag.pulse_us = 0U;
    SlaveServoTestDiag.target_us = targets_us[0];
    SlaveServoTestDiag.update_count = 0U;
    SlaveServoTestDiag.last_update_tick = now_ms;
    SlaveServoTestDiag.last_error = SLAVE_SERVO_TEST_ERROR_NONE;
    hold_started_at = step_started_at = now_ms;
    if (Sg90TestPwm_Start(targets_us[0]) == 0U)
    {
        SlaveServoTest_Fault(SLAVE_SERVO_TEST_ERROR_START);
        return;
    }
    SlaveServoTestDiag.pulse_us = targets_us[0];
    SlaveServoTestDiag.state = SLAVE_SERVO_TEST_HOLDING;
}

void SlaveServoTest_InitManual(uint32_t now_ms)
{
    SlaveServoTestDiag.state = SLAVE_SERVO_TEST_NOT_STARTED;
    SlaveServoTestDiag.stage_index = 0U;
    SlaveServoTestDiag.pulse_us = 0U;
    SlaveServoTestDiag.target_us = SLAVE_SERVO_WINDOW_STOP_US;
    SlaveServoTestDiag.update_count = 0U;
    SlaveServoTestDiag.last_update_tick = now_ms;
    SlaveServoTestDiag.last_error = SLAVE_SERVO_TEST_ERROR_NONE;
    window_started_at = now_ms;
    window_action = 0U;
    if (Sg90TestPwm_Start(SLAVE_SERVO_WINDOW_STOP_US) == 0U)
    {
        SlaveServoTest_Fault(SLAVE_SERVO_TEST_ERROR_START);
        return;
    }
    SlaveServoTestDiag.pulse_us = SLAVE_SERVO_WINDOW_STOP_US;
    SlaveServoTestDiag.state = SLAVE_SERVO_TEST_MANUAL;
}

static uint8_t SlaveServoTest_StopWindowIfDue(uint32_t now_ms)
{
    if ((SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL_RUNNING) &&
        ((uint32_t)(now_ms - window_started_at) >= SLAVE_SERVO_WINDOW_RUN_MS))
    {
        SlaveServoTestDiag.target_us = SLAVE_SERVO_WINDOW_STOP_US;
        if (Sg90TestPwm_SetPulse(SLAVE_SERVO_WINDOW_STOP_US) == 0U)
        {
            SlaveServoTest_Fault(SLAVE_SERVO_TEST_ERROR_UPDATE);
            return 0U;
        }
        SlaveServoTestDiag.pulse_us = SLAVE_SERVO_WINDOW_STOP_US;
        SlaveServoTestDiag.last_update_tick = now_ms;
        SlaveServoTestDiag.update_count++;
        SlaveServoTestDiag.state = SLAVE_SERVO_TEST_MANUAL;
    }
    return 1U;
}

uint8_t SlaveServoTest_SetWindow(uint8_t open, uint32_t now_ms)
{
    uint16_t pulse_us;

    if ((open > 1U) ||
        ((SlaveServoTestDiag.state != SLAVE_SERVO_TEST_MANUAL) &&
         (SlaveServoTestDiag.state != SLAVE_SERVO_TEST_MANUAL_RUNNING)))
    {
        return 0U;
    }
    /* An expired action must stop before this command can start a new one. */
    if (SlaveServoTest_StopWindowIfDue(now_ms) == 0U)
    {
        return 0U;
    }
    if ((SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL_RUNNING) &&
        (window_action == open))
    {
        return 1U; /* Do not rewrite PWM or extend the original deadline. */
    }
    pulse_us = (open != 0U) ? SG90_TEST_MAX_US : SG90_TEST_MIN_US;
    SlaveServoTestDiag.target_us = pulse_us;
    if (Sg90TestPwm_SetPulse(pulse_us) == 0U)
    {
        SlaveServoTest_Fault(SLAVE_SERVO_TEST_ERROR_UPDATE);
        return 0U;
    }
    SlaveServoTestDiag.pulse_us = pulse_us;
    SlaveServoTestDiag.last_update_tick = now_ms;
    SlaveServoTestDiag.update_count++;
    window_action = open;
    window_started_at = now_ms;
    SlaveServoTestDiag.state = SLAVE_SERVO_TEST_MANUAL_RUNNING;
    return 1U;
}

void SlaveServoTest_Process(uint32_t now_ms)
{
    uint32_t pulse_us;
    uint32_t target_us;

    if (SlaveServoTestDiag.state == SLAVE_SERVO_TEST_MANUAL_RUNNING)
    {
        (void)SlaveServoTest_StopWindowIfDue(now_ms);
        return;
    }
    if (SlaveServoTestDiag.state == SLAVE_SERVO_TEST_HOLDING)
    {
        /* Include one PWM period for a pending preloaded pulse to take effect. */
        if ((uint32_t)(now_ms - hold_started_at) <
            (SLAVE_SERVO_TEST_HOLD_MS + SG90_TEST_PERIOD_MS))
        {
            return;
        }
        if ((SlaveServoTestDiag.stage_index + 1U) >=
            (sizeof(targets_us) / sizeof(targets_us[0])))
        {
            SlaveServoTestDiag.state = SLAVE_SERVO_TEST_COMPLETE;
            return; /* Keep sending the center pulse; do not restart the test. */
        }
        SlaveServoTestDiag.stage_index++;
        SlaveServoTestDiag.target_us = targets_us[SlaveServoTestDiag.stage_index];
        SlaveServoTestDiag.state = SLAVE_SERVO_TEST_MOVING;
        step_started_at = now_ms;
        return;
    }
    if ((SlaveServoTestDiag.state != SLAVE_SERVO_TEST_MOVING) ||
        ((uint32_t)(now_ms - step_started_at) < SG90_TEST_PERIOD_MS))
    {
        return;
    }

    pulse_us = SlaveServoTestDiag.pulse_us;
    target_us = SlaveServoTestDiag.target_us;
    if (pulse_us < target_us)
    {
        pulse_us += ((target_us - pulse_us) > SLAVE_SERVO_TEST_STEP_US) ?
                    SLAVE_SERVO_TEST_STEP_US : (target_us - pulse_us);
    }
    else if (pulse_us > target_us)
    {
        pulse_us -= ((pulse_us - target_us) > SLAVE_SERVO_TEST_STEP_US) ?
                    SLAVE_SERVO_TEST_STEP_US : (pulse_us - target_us);
    }
    if (Sg90TestPwm_SetPulse((uint16_t)pulse_us) == 0U)
    {
        SlaveServoTest_Fault(SLAVE_SERVO_TEST_ERROR_UPDATE);
        return;
    }
    step_started_at = now_ms; /* No catch-up loop after slow I2C/UART operations. */
    SlaveServoTestDiag.pulse_us = pulse_us;
    SlaveServoTestDiag.last_update_tick = now_ms;
    SlaveServoTestDiag.update_count++;
    if (pulse_us == target_us)
    {
        hold_started_at = now_ms;
        SlaveServoTestDiag.state = SLAVE_SERVO_TEST_HOLDING;
    }
}
