#ifndef SG90_TEST_PWM_H
#define SG90_TEST_PWM_H

#include <stdint.h>

/* Conservative unloaded bench range, not calibrated absolute angles. */
#define SG90_TEST_MIN_US       1300U
#define SG90_TEST_CENTER_US    1500U
#define SG90_TEST_MAX_US       1700U
#define SG90_TEST_PERIOD_MS    20U
#define SG90_SERVO_COUNT       4U

/* Public IDs: 1=PB8, 2=PB9, 3=PB10, 4=PB11. Shared timers keep running. */
uint8_t Sg90TestPwm_StartChannel(uint8_t servo_id, uint16_t pulse_us);
uint8_t Sg90TestPwm_SetChannelPulse(uint8_t servo_id, uint16_t pulse_us);
void Sg90TestPwm_StopChannel(uint8_t servo_id);
uint8_t Sg90TestPwm_IsChannelRunning(uint8_t servo_id);

uint8_t Sg90TestPwm_Start(uint16_t pulse_us);
uint8_t Sg90TestPwm_SetPulse(uint16_t pulse_us);
void Sg90TestPwm_Stop(void);

#endif
