#ifndef HCSR04_TIMER_H
#define HCSR04_TIMER_H
#include <stdint.h>

#define HCSR04_ECHO_TIMEOUT_US 10000U
typedef enum {
    HCSR04_PENDING = 0, HCSR04_COMPLETE, HCSR04_ECHO_HIGH,
    HCSR04_TIMEOUT, HCSR04_OVERCAPTURE, HCSR04_BAD_EDGE
} Hcsr04Result;
/* Owns TIM3, PB0/TI3 and PB1/GPIO. No HAL timer callback is shared. */
uint8_t Hcsr04Timer_Init(void);
uint8_t Hcsr04Timer_Start(void);
Hcsr04Result Hcsr04Timer_Poll(uint16_t *pulse_us);
void Hcsr04Timer_Cancel(void);
void Hcsr04Timer_IRQHandler(void);
#endif
