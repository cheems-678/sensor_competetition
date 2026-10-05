#ifndef SLAVE_SERVO_TEST_H
#define SLAVE_SERVO_TEST_H

#include <stdint.h>

#define SLAVE_SERVO_TEST_STEP_US     10U
#define SLAVE_SERVO_TEST_HOLD_MS     2000U
#define SLAVE_SERVO_COUNT            4U

/* Continuous-rotation business action; calibrate the unloaded stop point. */
#ifndef SLAVE_SERVO_WINDOW_RUN_MS
#define SLAVE_SERVO_WINDOW_RUN_MS    300U
#endif
#ifndef SLAVE_SERVO_WINDOW_STOP_US
#define SLAVE_SERVO_WINDOW_STOP_US   1500U
#endif

typedef enum
{
    SLAVE_SERVO_TEST_NOT_STARTED = 0,
    SLAVE_SERVO_TEST_MOVING,
    SLAVE_SERVO_TEST_HOLDING,
    SLAVE_SERVO_TEST_COMPLETE,
    SLAVE_SERVO_TEST_FAULT,
    SLAVE_SERVO_TEST_MANUAL,
    SLAVE_SERVO_TEST_MANUAL_RUNNING
} SlaveServoTestState;

typedef enum
{
    SLAVE_SERVO_TEST_ERROR_NONE = 0,
    SLAVE_SERVO_TEST_ERROR_START,
    SLAVE_SERVO_TEST_ERROR_UPDATE,
    SLAVE_SERVO_TEST_ERROR_CONFIG
} SlaveServoTestError;

typedef struct
{
    uint32_t state;
    uint32_t stage_index;
    uint32_t pulse_us;
    uint32_t target_us;
    uint32_t update_count;
    uint32_t last_update_tick;
    uint32_t last_error;
} SlaveServoTestDiagnostics;

/* Command diagnostics only: no state confirms physical position. */
extern volatile SlaveServoTestDiagnostics SlaveServoTestDiag;
/* IDs 2..4; ID 1 keeps the historical diagnostic symbol above. */
extern volatile SlaveServoTestDiagnostics SlaveServoExtraDiag[SLAVE_SERVO_COUNT - 1U];

typedef struct
{
    uint32_t run_ms;
    uint16_t stop_us;
    uint16_t close_us;
    uint16_t open_us;
} SlaveServoConfig;

extern const SlaveServoConfig SlaveServoConfigs[SLAVE_SERVO_COUNT];
const volatile SlaveServoTestDiagnostics *SlaveServoTest_GetDiagnostics(uint8_t servo_id);
uint8_t SlaveServoTest_SetWindowChannel(uint8_t servo_id, uint8_t open, uint32_t now_ms);

void SlaveServoTest_Init(uint32_t now_ms);
/* Business mode initializes all four outputs at their calibrated stop points. */
void SlaveServoTest_InitManual(uint32_t now_ms);
/* Starts a timed action. An in-flight same-direction command does not renew it.
 * Success confirms only the PWM setting, not action completion or position. */
uint8_t SlaveServoTest_SetWindow(uint8_t open, uint32_t now_ms);
void SlaveServoTest_Process(uint32_t now_ms);

#endif
