#include "slave_servo_test.h"

#include "sg90_test_pwm.h"

static const uint16_t targets_us[] = {
    SG90_TEST_CENTER_US, SG90_TEST_MIN_US, SG90_TEST_CENTER_US,
    SG90_TEST_MAX_US, SG90_TEST_CENTER_US
};

volatile SlaveServoTestDiagnostics SlaveServoTestDiag;
volatile SlaveServoTestDiagnostics SlaveServoExtraDiag[SLAVE_SERVO_COUNT - 1U];
/* Calibrate each row separately: run_ms, stop_us, close_us, open_us. */
const SlaveServoConfig SlaveServoConfigs[SLAVE_SERVO_COUNT] = {
    {SLAVE_SERVO_WINDOW_RUN_MS, SLAVE_SERVO_WINDOW_STOP_US, 1300U, 1700U}, /* PB8 */
    {SLAVE_SERVO_WINDOW_RUN_MS, SLAVE_SERVO_WINDOW_STOP_US, 1300U, 1700U}, /* PB9 */
    {SLAVE_SERVO_WINDOW_RUN_MS, SLAVE_SERVO_WINDOW_STOP_US, 1300U, 1700U}, /* PB10 */
    {SLAVE_SERVO_WINDOW_RUN_MS, SLAVE_SERVO_WINDOW_STOP_US, 1300U, 1700U}  /* PB11 */
};
static uint32_t hold_started_at;
static uint32_t step_started_at;
static uint32_t window_started_at[SLAVE_SERVO_COUNT];
static uint8_t window_action[SLAVE_SERVO_COUNT];

static volatile SlaveServoTestDiagnostics *ChannelDiag(uint8_t servo_id)
{
    return (servo_id == 1U) ? &SlaveServoTestDiag : &SlaveServoExtraDiag[servo_id - 2U];
}

const volatile SlaveServoTestDiagnostics *SlaveServoTest_GetDiagnostics(uint8_t servo_id)
{
    if ((servo_id < 1U) || (servo_id > SLAVE_SERVO_COUNT))
    {
        return 0;
    }
    return ChannelDiag(servo_id);
}

static void ChannelFault(uint8_t servo_id, uint32_t error)
{
    volatile SlaveServoTestDiagnostics *diag = ChannelDiag(servo_id);
    if (servo_id == 1U) { Sg90TestPwm_Stop(); }
    else { Sg90TestPwm_StopChannel(servo_id); }
    diag->last_error = error;
    diag->pulse_us = 0U;
    diag->state = SLAVE_SERVO_TEST_FAULT;
}

static uint8_t SetPulse(uint8_t servo_id, uint16_t pulse_us)
{
    return (servo_id == 1U) ? Sg90TestPwm_SetPulse(pulse_us) :
                            Sg90TestPwm_SetChannelPulse(servo_id, pulse_us);
}

static void SlaveServoTest_Fault(uint32_t error)
{
    Sg90TestPwm_Stop();
    SlaveServoTestDiag.last_error = error;
    SlaveServoTestDiag.pulse_us = 0U;
    SlaveServoTestDiag.state = SLAVE_SERVO_TEST_FAULT;
}

void SlaveServoTest_Init(uint32_t now_ms)
{
    uint8_t id;
    for (id = 2U; id <= SLAVE_SERVO_COUNT; id++)
    {
        ChannelDiag(id)->state = SLAVE_SERVO_TEST_NOT_STARTED;
    }
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
    uint8_t id;
    for (id = 1U; id <= SLAVE_SERVO_COUNT; id++)
    {
        volatile SlaveServoTestDiagnostics *diag = ChannelDiag(id);
        const SlaveServoConfig *config = &SlaveServoConfigs[id - 1U];
        diag->state = SLAVE_SERVO_TEST_NOT_STARTED;
        diag->stage_index = 0U;
        diag->pulse_us = 0U;
        diag->target_us = config->stop_us;
        diag->update_count = 0U;
        diag->last_update_tick = now_ms;
        diag->last_error = SLAVE_SERVO_TEST_ERROR_NONE;
        window_started_at[id - 1U] = now_ms;
        window_action[id - 1U] = 0U;
        if ((config->run_ms == 0U) || (config->run_ms > 0x7FFFFFFFU) ||
            (config->stop_us < SG90_TEST_MIN_US) || (config->stop_us > SG90_TEST_MAX_US) ||
            (config->close_us < SG90_TEST_MIN_US) || (config->close_us > SG90_TEST_MAX_US) ||
            (config->open_us < SG90_TEST_MIN_US) || (config->open_us > SG90_TEST_MAX_US))
        {
            ChannelFault(id, SLAVE_SERVO_TEST_ERROR_CONFIG);
            continue;
        }
        if (((id == 1U) ? Sg90TestPwm_Start(config->stop_us) :
                         Sg90TestPwm_StartChannel(id, config->stop_us)) == 0U)
        {
            ChannelFault(id, SLAVE_SERVO_TEST_ERROR_START);
            continue;
        }
        diag->pulse_us = config->stop_us;
        diag->state = SLAVE_SERVO_TEST_MANUAL;
    }
}

static uint8_t StopWindowIfDue(uint8_t servo_id, uint32_t now_ms)
{
    volatile SlaveServoTestDiagnostics *diag = ChannelDiag(servo_id);
    const SlaveServoConfig *config = &SlaveServoConfigs[servo_id - 1U];
    if (Sg90TestPwm_IsChannelRunning(servo_id) == 0U)
    {
        ChannelFault(servo_id, SLAVE_SERVO_TEST_ERROR_UPDATE);
        return 0U;
    }
    if ((diag->state == SLAVE_SERVO_TEST_MANUAL_RUNNING) &&
        ((uint32_t)(now_ms - window_started_at[servo_id - 1U]) >= config->run_ms))
    {
        diag->target_us = config->stop_us;
        if (SetPulse(servo_id, config->stop_us) == 0U)
        {
            ChannelFault(servo_id, SLAVE_SERVO_TEST_ERROR_UPDATE);
            return 0U;
        }
        diag->pulse_us = config->stop_us;
        diag->last_update_tick = now_ms;
        diag->update_count++;
        diag->state = SLAVE_SERVO_TEST_MANUAL;
    }
    return 1U;
}

uint8_t SlaveServoTest_SetWindowChannel(uint8_t servo_id, uint8_t open, uint32_t now_ms)
{
    uint16_t pulse_us;
    volatile SlaveServoTestDiagnostics *diag;
    if ((servo_id < 1U) || (servo_id > SLAVE_SERVO_COUNT) || (open > 1U))
    {
        return 0U;
    }
    diag = ChannelDiag(servo_id);
    if ((diag->state != SLAVE_SERVO_TEST_MANUAL) &&
        (diag->state != SLAVE_SERVO_TEST_MANUAL_RUNNING))
    {
        return 0U;
    }
    if (StopWindowIfDue(servo_id, now_ms) == 0U)
    {
        return 0U;
    }
    if ((diag->state == SLAVE_SERVO_TEST_MANUAL_RUNNING) &&
        (window_action[servo_id - 1U] == open))
    {
        return 1U;
    }
    pulse_us = (open != 0U) ? SlaveServoConfigs[servo_id - 1U].open_us :
                             SlaveServoConfigs[servo_id - 1U].close_us;
    diag->target_us = pulse_us;
    if (SetPulse(servo_id, pulse_us) == 0U)
    {
        ChannelFault(servo_id, SLAVE_SERVO_TEST_ERROR_UPDATE);
        return 0U;
    }
    diag->pulse_us = pulse_us;
    diag->last_update_tick = now_ms;
    diag->update_count++;
    window_action[servo_id - 1U] = open;
    window_started_at[servo_id - 1U] = now_ms;
    diag->state = SLAVE_SERVO_TEST_MANUAL_RUNNING;
    return 1U;
}

uint8_t SlaveServoTest_SetWindow(uint8_t open, uint32_t now_ms)
{
    return SlaveServoTest_SetWindowChannel(1U, open, now_ms);
}

void SlaveServoTest_Process(uint32_t now_ms)
{
    uint32_t pulse_us;
    uint32_t target_us;

    uint8_t id;
    for (id = 1U; id <= SLAVE_SERVO_COUNT; id++)
    {
        volatile SlaveServoTestDiagnostics *diag = ChannelDiag(id);
        if ((diag->state == SLAVE_SERVO_TEST_MANUAL) ||
            (diag->state == SLAVE_SERVO_TEST_MANUAL_RUNNING))
        {
            (void)StopWindowIfDue(id, now_ms);
        }
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
