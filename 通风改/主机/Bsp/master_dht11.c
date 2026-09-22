#include "master_dht11.h"

#include "stm32f1xx_hal.h"

#define MASTER_DHT11_GPIO_PORT           GPIOA
#define MASTER_DHT11_GPIO_PIN            GPIO_PIN_5
#define MASTER_DHT11_RESPONSE_TIMEOUT_US (120U)

/*
 * TIM1 is the HAL 1 ms timebase and TIM2/TIM3/TIM4 drive the fans.  Do not
 * reconfigure any of them for DHT timing.  SysTick runs from HCLK and is only
 * sampled for sub-millisecond delays, so it can be used without changing the
 * RTOS or peripheral timer configuration.
 */
static void MasterDht11_DelayUs(uint16_t delay_us)
{
    uint32_t start_count = SysTick->VAL;
    uint32_t current_count;
    uint32_t reload_count = SysTick->LOAD + 1UL;
    uint32_t required_cycles = (SystemCoreClock / 1000000UL) * delay_us;
    uint32_t elapsed_cycles;

    do
    {
        current_count = SysTick->VAL;
        if (start_count >= current_count)
        {
            elapsed_cycles = start_count - current_count;
        }
        else
        {
            elapsed_cycles = start_count + reload_count - current_count;
        }
    } while (elapsed_cycles < required_cycles);
}

static void MasterDht11_SetOutput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = MASTER_DHT11_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MASTER_DHT11_GPIO_PORT, &gpio);
}

static void MasterDht11_SetInput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = MASTER_DHT11_GPIO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(MASTER_DHT11_GPIO_PORT, &gpio);
}

static void MasterDht11_SetHigh(void)
{
    HAL_GPIO_WritePin(MASTER_DHT11_GPIO_PORT,
                      MASTER_DHT11_GPIO_PIN,
                      GPIO_PIN_SET);
}

static uint8_t MasterDht11_WaitForLevel(GPIO_PinState level,
                                        uint32_t timeout_us)
{
    uint32_t elapsed_us = 0U;

    while (HAL_GPIO_ReadPin(MASTER_DHT11_GPIO_PORT,
                            MASTER_DHT11_GPIO_PIN) != level)
    {
        MasterDht11_DelayUs(1U);
        elapsed_us++;
        if (elapsed_us >= timeout_us)
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t MasterDht11_ReadByte(uint8_t *value)
{
    uint8_t bit_index;
    uint8_t byte = 0U;

    if (value == NULL)
    {
        return 0U;
    }

    for (bit_index = 0U; bit_index < 8U; bit_index++)
    {
        if (MasterDht11_WaitForLevel(GPIO_PIN_SET,
                                     MASTER_DHT11_RESPONSE_TIMEOUT_US) == 0U)
        {
            return 0U;
        }

        /* Reference project's DHT11 decision point: 40 us into the high pulse. */
        MasterDht11_DelayUs(40U);
        byte <<= 1U;
        if (HAL_GPIO_ReadPin(MASTER_DHT11_GPIO_PORT,
                             MASTER_DHT11_GPIO_PIN) == GPIO_PIN_SET)
        {
            byte |= 1U;
        }

        if (MasterDht11_WaitForLevel(GPIO_PIN_RESET,
                                     MASTER_DHT11_RESPONSE_TIMEOUT_US) == 0U)
        {
            return 0U;
        }
    }

    *value = byte;
    return 1U;
}

static uint8_t MasterDht11_ReadFrame(uint8_t frame[5])
{
    uint8_t byte_index;

    MasterDht11_SetOutput();
    HAL_GPIO_WritePin(MASTER_DHT11_GPIO_PORT,
                      MASTER_DHT11_GPIO_PIN,
                      GPIO_PIN_RESET);
    HAL_Delay(18U);
    MasterDht11_SetHigh();
    MasterDht11_DelayUs(20U);
    MasterDht11_SetInput();
    MasterDht11_DelayUs(20U);

    if ((MasterDht11_WaitForLevel(GPIO_PIN_RESET,
                                  MASTER_DHT11_RESPONSE_TIMEOUT_US) == 0U) ||
        (MasterDht11_WaitForLevel(GPIO_PIN_SET,
                                  MASTER_DHT11_RESPONSE_TIMEOUT_US) == 0U) ||
        (MasterDht11_WaitForLevel(GPIO_PIN_RESET,
                                  MASTER_DHT11_RESPONSE_TIMEOUT_US) == 0U))
    {
        MasterDht11_SetOutput();
        MasterDht11_SetHigh();
        return 0U;
    }

    for (byte_index = 0U; byte_index < 5U; byte_index++)
    {
        if (MasterDht11_ReadByte(&frame[byte_index]) == 0U)
        {
            MasterDht11_SetOutput();
            MasterDht11_SetHigh();
            return 0U;
        }
    }

    MasterDht11_SetOutput();
    MasterDht11_SetHigh();
    return 1U;
}

void MasterDht11_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    MasterDht11_SetOutput();
    MasterDht11_SetHigh();
}

uint8_t MasterDht11_Read(int16_t *temperature_x10,
                         uint16_t *humidity_x10)
{
    uint8_t frame[5];
    uint16_t humidity;
    int16_t temperature;

    if ((temperature_x10 == NULL) || (humidity_x10 == NULL))
    {
        return 0U;
    }

    if (MasterDht11_ReadFrame(frame) == 0U)
    {
        return 0U;
    }

    if ((uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]) != frame[4])
    {
        return 0U;
    }

    humidity = (uint16_t)((uint16_t)frame[0] * 10U + (uint16_t)frame[1]);
    temperature = (int16_t)((int16_t)(frame[2] & 0x7FU) * 10 +
                            (int16_t)frame[3]);
    if ((frame[2] & 0x80U) != 0U)
    {
        temperature = (int16_t)-temperature;
    }

    if ((humidity > 1000U) || (temperature < -400) || (temperature > 800))
    {
        return 0U;
    }

    *temperature_x10 = temperature;
    *humidity_x10 = humidity;
    return 1U;
}
