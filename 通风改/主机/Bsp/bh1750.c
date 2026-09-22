#include "bh1750.h"

#include <string.h>

#include "stm32f1xx_hal.h"

#define BH1750_CMD_POWER_ON             (0x01U)
#define BH1750_CMD_RESET                (0x07U)
#define BH1750_CMD_CONTINUOUS_HRES_MODE (0x10U)
#define BH1750_I2C_TIMEOUT_MS           (10U)

volatile Bh1750Diagnostics Bh1750Diag;
static I2C_HandleTypeDef g_bh1750_i2c;
static uint16_t g_bh1750_address_8bit;

static uint8_t Bh1750_SendCommand(uint8_t command)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_Master_Transmit(&g_bh1750_i2c,
                                     g_bh1750_address_8bit,
                                     &command,
                                     1U,
                                     BH1750_I2C_TIMEOUT_MS);
    Bh1750Diag.last_hal_status = (uint32_t)status;
    Bh1750Diag.last_hal_error = HAL_I2C_GetError(&g_bh1750_i2c);
    if (status != HAL_OK)
    {
        Bh1750Diag.failure_count++;
        return 0U;
    }
    return 1U;
}

uint8_t Bh1750_Init(void)
{
    static const uint8_t addresses[] = {0x23U, 0x5CU};
    GPIO_InitTypeDef gpio = {0};
    HAL_StatusTypeDef status;
    uint32_t address_index;

    Bh1750Diag.init_attempt_count++;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    __HAL_RCC_I2C2_FORCE_RESET();
    __HAL_RCC_I2C2_RELEASE_RESET();
    memset(&g_bh1750_i2c, 0, sizeof(g_bh1750_i2c));
    g_bh1750_i2c.Instance = I2C2;
    g_bh1750_i2c.Init.ClockSpeed = 100000U;
    g_bh1750_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    g_bh1750_i2c.Init.OwnAddress1 = 0U;
    g_bh1750_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    g_bh1750_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    g_bh1750_i2c.Init.OwnAddress2 = 0U;
    g_bh1750_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    g_bh1750_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    status = HAL_I2C_Init(&g_bh1750_i2c);
    Bh1750Diag.last_hal_status = (uint32_t)status;
    Bh1750Diag.last_hal_error = HAL_I2C_GetError(&g_bh1750_i2c);
    if (status != HAL_OK)
    {
        Bh1750Diag.failure_count++;
        return 0U;
    }
    for (address_index = 0U;
         address_index < (sizeof(addresses) / sizeof(addresses[0]));
         address_index++)
    {
        g_bh1750_address_8bit = (uint16_t)(addresses[address_index] << 1U);
        status = HAL_I2C_IsDeviceReady(&g_bh1750_i2c,
                                       g_bh1750_address_8bit,
                                       2U,
                                       BH1750_I2C_TIMEOUT_MS);
        Bh1750Diag.last_hal_status = (uint32_t)status;
        Bh1750Diag.last_hal_error = HAL_I2C_GetError(&g_bh1750_i2c);
        if (status != HAL_OK)
        {
            continue;
        }
        if ((Bh1750_SendCommand(BH1750_CMD_POWER_ON) != 0U) &&
            (Bh1750_SendCommand(BH1750_CMD_RESET) != 0U) &&
            (Bh1750_SendCommand(BH1750_CMD_CONTINUOUS_HRES_MODE) != 0U))
        {
            Bh1750Diag.address_7bit = addresses[address_index];
            Bh1750Diag.init_success_count++;
            return 1U;
        }
    }

    Bh1750Diag.failure_count++;
    return 0U;
}

uint8_t Bh1750_ReadLuxX10(uint32_t *lux_x10)
{
    uint8_t data[2];
    uint16_t raw;
    HAL_StatusTypeDef status;

    if (lux_x10 == NULL)
    {
        return 0U;
    }

    Bh1750Diag.read_attempt_count++;
    status = HAL_I2C_Master_Receive(&g_bh1750_i2c,
                                    g_bh1750_address_8bit,
                                    data,
                                    sizeof(data),
                                    BH1750_I2C_TIMEOUT_MS);
    Bh1750Diag.last_hal_status = (uint32_t)status;
    Bh1750Diag.last_hal_error = HAL_I2C_GetError(&g_bh1750_i2c);
    if (status != HAL_OK)
    {
        Bh1750Diag.failure_count++;
        return 0U;
    }

    raw = (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
    /* Default MTreg=69 and high-resolution mode: lux = raw / 1.2. */
    *lux_x10 = (((uint32_t)raw * 100UL) + 6UL) / 12UL;
    Bh1750Diag.raw = raw;
    Bh1750Diag.lux_x10 = *lux_x10;
    Bh1750Diag.read_success_count++;
    Bh1750Diag.last_success_ms = HAL_GetTick();
    return 1U;
}
