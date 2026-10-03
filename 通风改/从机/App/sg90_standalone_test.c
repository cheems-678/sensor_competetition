#include "sg90_standalone_test.h"

#include "sg90_test_pwm.h"
#include "stm32f1xx_hal.h"

uint8_t Sg90StandaloneTest_RunCycle(void)
{
    static const uint16_t targets_us[] = {
        SG90_TEST_MIN_US, SG90_TEST_CENTER_US,
        SG90_TEST_MAX_US, SG90_TEST_CENTER_US
    };
    uint32_t index;

    for (index = 0U; index < (sizeof(targets_us) / sizeof(targets_us[0])); index++)
    {
        /* TIM4 keeps generating PWM while the CPU waits at each position. */
        HAL_Delay(SG90_STANDALONE_HOLD_MS);
        if (Sg90TestPwm_SetPulse(targets_us[index]) == 0U)
        {
            return 0U; /* The PWM driver has already stopped and pulled PB8 low. */
        }
    }
    return 1U;
}
