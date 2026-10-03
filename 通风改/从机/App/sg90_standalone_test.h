#ifndef SG90_STANDALONE_TEST_H
#define SG90_STANDALONE_TEST_H

#include <stdint.h>

#define SG90_STANDALONE_HOLD_MS 2000U

/* Start center PWM once before calling. Returns zero on a PWM update failure. */
uint8_t Sg90StandaloneTest_RunCycle(void);

#endif
