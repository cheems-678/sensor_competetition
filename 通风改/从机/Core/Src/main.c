#ifndef SLAVE_SG90_STANDALONE_TEST
#define SLAVE_SG90_STANDALONE_TEST 0
#endif

#include "main.h"
#include "tim.h"

#if SLAVE_SG90_STANDALONE_TEST
#include "sg90_test_pwm.h"
#include "sg90_standalone_test.h"
#else
#include "usart.h"
#include "gpio.h"

#include "bsp_led.h"
#include "core_delay.h"
#include "interrupt.h"
#include "lora.h"
#include "slave_acoustic.h"
#include "slave_servo_test.h"
#endif

void SystemClock_Config(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

#if SLAVE_SG90_STANDALONE_TEST
    /* Standalone bench firmware: no radio, sensors, UART or host control. */
    MX_TIM4_Init();
    if (Sg90TestPwm_Start(SG90_TEST_CENTER_US) == 0U)
    {
        Error_Handler();
    }
    for (;;)
    {
        if (Sg90StandaloneTest_RunCycle() == 0U)
        {
            Error_Handler();
        }
    }
#else
    MX_GPIO_Init();
    MX_TIM4_Init();
    MX_TIM2_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();

    HAL_UART_Receive_IT(&huart2, &rx2_data, 1U);

    HAL_Delay(50U);
    LORA_Init();
    led2_on;
    CPU_TS_TmrInit();
    SlaveAcoustic_Init(HAL_GetTick());
    SlaveServoTest_InitManual(HAL_GetTick());

    for (;;)
    {
        /* DMA runs independently; drain before and after potentially slow I2C/UART. */
        SlaveAcoustic_Process(HAL_GetTick());
        SlaveServoTest_Process(HAL_GetTick());
        LoraP2PTrans();
        SlaveAcoustic_Process(HAL_GetTick());
    }
#endif
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clock = {0};

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        Error_Handler();
    }

    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV2;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
