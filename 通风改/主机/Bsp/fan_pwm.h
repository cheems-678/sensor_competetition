#ifndef FAN_PWM_H
#define FAN_PWM_H

#include <stdint.h>

/* Channels 1..4: PB1/PB8/PA1/PB9. Independent active-high 25 kHz,
 * boot duty 0%. TIM2/3/4 are owned by this driver. */
#define FAN_PWM_CHANNEL_COUNT (4U)

void FanPwm_Init(void);
uint8_t FanPwm_SetDuty(uint8_t channel, uint8_t duty_percent);
uint8_t FanPwm_GetDuty(uint8_t channel);

#endif /* FAN_PWM_H */
