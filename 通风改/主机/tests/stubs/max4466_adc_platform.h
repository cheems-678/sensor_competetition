#ifndef MAX4466_ADC_PLATFORM_H
#define MAX4466_ADC_PLATFORM_H
#include <stdint.h>
#include <stddef.h>
void *Max4466TestCopy(void *dst, const void *src, size_t n);
#define memcpy Max4466TestCopy
typedef struct { uint32_t CNDTR; } FakeChannel;
typedef struct { uint32_t ISR; } FakeDma;
extern FakeChannel fake_channel;
extern FakeDma fake_dma;
#define DMA1_Channel1 (&fake_channel)
#define DMA1 (&fake_dma)
#define ADC1 ((void *)1)
#define GPIOA ((void *)2)
#define GPIOB ((void *)3)
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
typedef struct { uint32_t PeriphClockSelection, AdcClockSelection; } RCC_PeriphCLKInitTypeDef;
typedef struct { uint32_t Channel, Rank, SamplingTime; } ADC_ChannelConfTypeDef;
typedef struct { uint32_t Direction, PeriphInc, MemInc, PeriphDataAlignment, MemDataAlignment, Mode, Priority; } FakeDmaInit;
typedef struct { FakeChannel *Instance; FakeDmaInit Init; void *Parent; } DMA_HandleTypeDef;
typedef struct { uint32_t ScanConvMode, ContinuousConvMode, DiscontinuousConvMode, ExternalTrigConv, DataAlign, NbrOfConversion; } FakeAdcInit;
typedef struct { void *Instance; FakeAdcInit Init; DMA_HandleTypeDef *DMA_Handle; } ADC_HandleTypeDef;
#define HAL_OK 0
#define ENABLE 1
#define DISABLE 0
#define GPIO_PIN_0 1U
#define GPIO_PIN_4 16U
#define GPIO_PIN_5 32U
#define GPIO_PIN_6 64U
#define GPIO_MODE_ANALOG 3U
#define RCC_PERIPHCLK_ADC 1U
#define RCC_ADCPCLK2_DIV6 6U
#define DMA_PERIPH_TO_MEMORY 0U
#define DMA_PINC_DISABLE 0U
#define DMA_MINC_ENABLE 1U
#define DMA_PDATAALIGN_HALFWORD 2U
#define DMA_MDATAALIGN_HALFWORD 2U
#define DMA_CIRCULAR 1U
#define DMA_PRIORITY_HIGH 2U
#define ADC_SCAN_ENABLE 1U
#define ADC_SOFTWARE_START 0U
#define ADC_DATAALIGN_RIGHT 0U
#define ADC_SAMPLETIME_239CYCLES_5 239U
#define ADC_CHANNEL_0 0U
#define ADC_CHANNEL_4 4U
#define ADC_CHANNEL_5 5U
#define ADC_CHANNEL_6 6U
#define ADC_CHANNEL_8 8U
#define DMA_FLAG_GL1 15U
#define DMA_FLAG_TC1 2U
#define DMA_FLAG_HT1 4U
#define DMA_FLAG_TE1 8U
#define DMA1_Channel1_IRQn 11
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_ADC1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_DMA1_CLK_ENABLE() ((void)0)
#define __HAL_LINKDMA(a,f,d) do { (a)->f = &(d); (d).Parent = (a); } while (0)
#define __HAL_DMA_CLEAR_FLAG(d,f) do { (void)(d); DMA1->ISR &= ~(f); } while (0)
#define __get_PRIMASK() 0U
#define __disable_irq() ((void)0)
#define __set_PRIMASK(x) ((void)(x))
#define __DMB() ((void)0)
uint32_t HAL_GetTick(void);
int HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *p);
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *p);
int HAL_DMA_Init(DMA_HandleTypeDef *p);
int HAL_ADC_Init(ADC_HandleTypeDef *p);
int HAL_ADC_ConfigChannel(ADC_HandleTypeDef *p, ADC_ChannelConfTypeDef *c);
int HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *p);
int HAL_ADC_Start_DMA(ADC_HandleTypeDef *p, uint32_t *buf, uint32_t count);
int HAL_ADC_Stop_DMA(ADC_HandleTypeDef *p);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *p);
void HAL_NVIC_DisableIRQ(int irq);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_NVIC_ClearPendingIRQ(int irq);
void HAL_NVIC_SetPriority(int irq, unsigned preempt, unsigned sub);
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *p);
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *p);
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *p);
#endif
