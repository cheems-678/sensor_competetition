#ifndef FAN_PWM_H
#define FAN_PWM_H

#include <stdint.h>

void FanPwm_Init(void);
uint8_t FanPwm_SetDuty(uint8_t channel, uint8_t duty_percent);
uint8_t FanPwm_GetDuty(uint8_t channel);

#endif /* FAN_PWM_H */
