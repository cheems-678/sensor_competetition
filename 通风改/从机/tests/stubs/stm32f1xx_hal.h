#ifndef TEST_HAL_H
#define TEST_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t CR1, SR2; } I2C_TypeDef;
typedef struct { uint32_t ClockSpeed, DutyCycle, OwnAddress1, AddressingMode,
    DualAddressMode, OwnAddress2, GeneralCallMode, NoStretchMode; } I2C_InitTypeDef;
typedef struct { I2C_TypeDef *Instance; I2C_InitTypeDef Init; uint32_t ErrorCode; } I2C_HandleTypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
extern I2C_TypeDef fake_i2c1;
#define I2C1 (&fake_i2c1)
#define GPIOB ((void *)1)
#define GPIO_PIN_6 64U
#define GPIO_PIN_7 128U
#define GPIO_MODE_AF_OD 1U
#define GPIO_MODE_OUTPUT_OD 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_HIGH 3U
#define I2C_DUTYCYCLE_2 0U
#define I2C_ADDRESSINGMODE_7BIT 0U
#define I2C_DUALADDRESS_DISABLE 0U
#define I2C_GENERALCALL_DISABLE 0U
#define I2C_NOSTRETCH_DISABLE 0U
#define I2C_MEMADD_SIZE_8BIT 1U
#define I2C_CR1_PE 1U
#define I2C_CR1_SWRST 0x8000U
#define I2C_SR2_BUSY 2U
#define CLEAR_BIT(reg, bits) ((reg) &= ~(bits))
#define SET_BIT(reg, bits) ((reg) |= (bits))
#define __DSB() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_I2C1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_I2C1_CLK_DISABLE() ((void)0)
/* ADC-specific HAL substitute, only used by test_mq2_adc.c. */
typedef struct { uint32_t SR, DR; } ADC_TypeDef;
typedef struct { uint32_t ScanConvMode, ContinuousConvMode, DiscontinuousConvMode,
    ExternalTrigConv, DataAlign, NbrOfConversion; } ADC_InitTypeDef;
typedef struct { ADC_TypeDef *Instance; ADC_InitTypeDef Init; uint32_t ErrorCode; } ADC_HandleTypeDef;
typedef struct { uint32_t Channel, Rank, SamplingTime; } ADC_ChannelConfTypeDef;
typedef struct { uint32_t PeriphClockSelection, AdcClockSelection; } RCC_PeriphCLKInitTypeDef;
extern ADC_TypeDef fake_adc1;
#define ADC1 (&fake_adc1)
#define GPIOA ((void *)2)
#define GPIO_MODE_ANALOG 5U
#define DISABLE 0U
#define ADC_SCAN_DISABLE 0U
#define ADC_SOFTWARE_START 1U
#define ADC_DATAALIGN_RIGHT 0U
#define ADC_CHANNEL_7 7U
#define ADC_REGULAR_RANK_1 1U
#define ADC_SAMPLETIME_239CYCLES_5 239U
#define RCC_PERIPHCLK_ADC 4U
#define RCC_ADCPCLK2_DIV6 6U
#define ADC_FLAG_EOC 2U
#define HAL_ADC_ERROR_NONE 0U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_ADC1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_ADC1_FORCE_RESET() ((void)0)
#define __HAL_RCC_ADC1_RELEASE_RESET() ((void)0)
#define __HAL_ADC_GET_FLAG(h, f) ((h)->Instance->SR & (f))
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *);
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *, ADC_ChannelConfTypeDef *);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *);
uint32_t HAL_ADC_GetError(ADC_HandleTypeDef *);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *, uint16_t, uint16_t,
    uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *, uint16_t, uint16_t,
    uint16_t, uint8_t *, uint16_t, uint32_t);
void HAL_GPIO_Init(void *, GPIO_InitTypeDef *);
void HAL_GPIO_DeInit(void *, uint16_t);
void HAL_GPIO_WritePin(void *, uint16_t, GPIO_PinState);
GPIO_PinState HAL_GPIO_ReadPin(void *, uint16_t);
#endif
