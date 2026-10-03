#include "sph0645.h"

#include "stm32f1xx_hal.h"

#define SPH0645_BOUNDARY_FLAGS (DMA_ISR_HTIF4 | DMA_ISR_TCIF4)
#define SPH0645_ALL_FLAGS      (DMA_ISR_GIF4 | SPH0645_BOUNDARY_FLAGS | DMA_ISR_TEIF4)

volatile Sph0645Diagnostics Sph0645Diag;
static volatile uint16_t capture_words[SPH0645_DMA_WORDS];
static volatile uint32_t completed_half;
static uint32_t consumed_sequence;

static void Sph0645_Fault(uint32_t error)
{
    /* Stop the clock first; never continue accepting data after a lost SPI word. */
    TIM1->CR1 &= ~TIM_CR1_CEN;
    if (Sph0645Diag.active_error == SPH0645_ERROR_NONE)
    {
        Sph0645Diag.failure_count++;
        Sph0645Diag.active_error = error;
        Sph0645Diag.last_error = error;
    }
    Sph0645Diag.running = 0U;
}

void Sph0645_Stop(void)
{
    TIM1->CR1 &= ~TIM_CR1_CEN;
    TIM3->CR1 &= ~TIM_CR1_CEN;
    SPI2->CR1 &= ~SPI_CR1_SPE;
    DMA1_Channel4->CCR &= ~DMA_CCR_EN;
    HAL_NVIC_DisableIRQ(DMA1_Channel4_IRQn);
    DMA1->IFCR = DMA_IFCR_CGIF4;
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel4_IRQn);
    Sph0645Diag.running = 0U;
}

uint8_t Sph0645_Start(uint32_t now_ms)
{
    GPIO_InitTypeDef pin = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    Sph0645_Stop();
    Sph0645Diag.start_count++;
    Sph0645Diag.active_error = SPH0645_ERROR_NONE;

    /* The divider/timing contract is for the existing 72/36/72 MHz clock tree. */
    if ((SystemCoreClock != 72000000U) ||
        (HAL_RCC_GetPCLK1Freq() != 36000000U) ||
        (HAL_RCC_GetPCLK2Freq() != 72000000U))
    {
        Sph0645_Fault(SPH0645_ERROR_CLOCK_CONFIG);
        return 0U;
    }
    if ((AFIO->MAPR & (AFIO_MAPR_TIM1_REMAP | AFIO_MAPR_TIM3_REMAP)) != 0U)
    {
        Sph0645_Fault(SPH0645_ERROR_PIN_REMAP);
        return 0U;
    }

    /* RCC reset clears the SPI bit counter, not merely its DMA destination. */
    __HAL_RCC_TIM1_FORCE_RESET();
    __HAL_RCC_TIM3_FORCE_RESET();
    __HAL_RCC_SPI2_FORCE_RESET();
    __HAL_RCC_TIM1_RELEASE_RESET();
    __HAL_RCC_TIM3_RELEASE_RESET();
    __HAL_RCC_SPI2_RELEASE_RESET();

    /* PB12 observes WS only. SSM/SSI keep SPI selected during BOTH channels. */
    pin.Pin = GPIO_PIN_12 | GPIO_PIN_13;
    pin.Mode = GPIO_MODE_INPUT;
    pin.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &pin);
    pin.Pin = GPIO_PIN_15;
    pin.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB, &pin);

    TIM1->PSC = 0U;
    TIM1->ARR = 34U;
    TIM1->CCR1 = 18U;
    TIM1->CCMR1 = TIM_CCMR1_OC1M | TIM_CCMR1_OC1PE; /* PWM2, low at CNT=0. */
    TIM1->CR1 = TIM_CR1_ARPE;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0U;
    TIM1->CNT = 0U;
    /* Set UPDATE TRGO only AFTER UG, while TIM3 remains disconnected/stopped. */
    TIM1->CR2 = TIM_CR2_MMS_1;
    TIM1->CCER = TIM_CCER_CC1E;
    TIM1->BDTR = TIM_BDTR_MOE;

    TIM3->PSC = 0U;
    TIM3->ARR = 63U;
    TIM3->CCR3 = 32U;
    TIM3->CCMR2 = TIM_CCMR2_OC3M | TIM_CCMR2_OC3PE; /* PWM2: 32 low + 32 high. */
    TIM3->CR1 = TIM_CR1_ARPE;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;
    TIM3->CNT = 0U;
    TIM3->CCER = TIM_CCER_CC3E;
    /* RM0008 Table 86: TIM3 ITR0=TIM1; SMS=111 external clock mode 1. */
    TIM3->SMCR = TIM_SMCR_SMS;

    pin.Pin = GPIO_PIN_8;
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &pin);
    pin.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOB, &pin);

    SPI2->CR1 = SPI_CR1_CPHA | SPI_CR1_DFF | SPI_CR1_SSM | SPI_CR1_RXONLY;
    SPI2->CR2 = SPI_CR2_RXDMAEN;
    DMA1_Channel4->CCR = 0U;
    DMA1_Channel4->CPAR = (uint32_t)&SPI2->DR;
    DMA1_Channel4->CMAR = (uint32_t)capture_words;
    DMA1_Channel4->CNDTR = SPH0645_DMA_WORDS;
    DMA1_Channel4->CCR = DMA_CCR_MINC | DMA_CCR_CIRC |
                         DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 | DMA_CCR_PL_1 |
                         DMA_CCR_HTIE | DMA_CCR_TCIE | DMA_CCR_TEIE;
    DMA1->IFCR = DMA_IFCR_CGIF4;
    consumed_sequence = Sph0645Diag.completed_blocks;
    completed_half = 0U;
    Sph0645Diag.last_block_tick = now_ms;
    Sph0645Diag.bclk_hz = 72000000U / 35U;
    Sph0645Diag.sample_rate_millihz = (uint32_t)((uint64_t)72000000U * 1000U / (35U * 64U));
    __HAL_DBGMCU_FREEZE_TIM1();
    __HAL_DBGMCU_FREEZE_TIM3();
    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 3U, 0U);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    DMA1_Channel4->CCR |= DMA_CCR_EN;
    SPI2->CR1 |= SPI_CR1_SPE;
    TIM3->CR1 |= TIM_CR1_CEN;
    Sph0645Diag.running = 1U;
    __DMB();
    /* First rising edge launches data, first falling edge samples it/increments WS. */
    TIM1->CR1 |= TIM_CR1_CEN;
    return 1U;
}

void Sph0645_DmaIRQ(void)
{
    /* Clear only observed events; CGIF4 would also erase a newly arrived event. */
    uint32_t flags = DMA1->ISR & (SPH0645_BOUNDARY_FLAGS | DMA_ISR_TEIF4);
    DMA1->IFCR = flags;
    if (Sph0645Diag.running == 0U)
    {
        return;
    }
    if ((flags & DMA_ISR_TEIF4) != 0U)
    {
        Sph0645_Fault(SPH0645_ERROR_DMA);
        return;
    }
    if ((flags & SPH0645_BOUNDARY_FLAGS) == SPH0645_BOUNDARY_FLAGS)
    {
        /* Cannot count how many complete laps elapsed while the ISR was blocked. */
        Sph0645_Fault(SPH0645_ERROR_IRQ_LATE);
        return;
    }
    if ((flags & SPH0645_BOUNDARY_FLAGS) != 0U)
    {
        completed_half = ((flags & DMA_ISR_TCIF4) != 0U) ? 1U : 0U;
        Sph0645Diag.last_block_tick = HAL_GetTick();
        __DMB();
        Sph0645Diag.completed_blocks++;
    }
}

uint32_t Sph0645_Check(uint32_t now_ms)
{
    /* DMA can read DR before this SR read and complete the OVR clear sequence. */
    uint32_t spi_status = SPI2->SR;
    Sph0645Diag.dma_remaining = DMA1_Channel4->CNDTR;
    Sph0645Diag.spi_status = spi_status;
    Sph0645Diag.dma_status = DMA1->ISR & SPH0645_ALL_FLAGS;
    Sph0645Diag.pin_inputs = GPIOB->IDR & (GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15);
    if (Sph0645Diag.running != 0U)
    {
        if ((spi_status & SPI_SR_OVR) != 0U)
        {
            Sph0645_Fault(SPH0645_ERROR_SPI_OVERRUN);
        }
        /* ISR timestamp may advance one tick AFTER caller took its now_ms snapshot. */
        else if ((int32_t)(now_ms - Sph0645Diag.last_block_tick) >= (int32_t)SPH0645_CLOCK_TIMEOUT_MS)
        {
            Sph0645_Fault(SPH0645_ERROR_NO_CLOCK);
        }
    }
    return Sph0645Diag.active_error;
}

static uint8_t Sph0645_HalfIsInactive(uint32_t half)
{
    uint32_t remaining = DMA1_Channel4->CNDTR;
    if ((remaining == 0U) || (remaining > SPH0645_DMA_WORDS))
    {
        return 0U;
    }
    return ((remaining > SPH0645_BLOCK_WORDS) ? (half == 1U) : (half == 0U)) ? 1U : 0U;
}

uint8_t Sph0645_ReadBlock(uint16_t words[SPH0645_BLOCK_WORDS],
                         uint32_t *sequence, uint32_t *block_tick)
{
    uint32_t current, half, tick, index, primask;
    if ((words == 0) || (sequence == 0) || (block_tick == 0) ||
        (Sph0645Diag.running == 0U))
    {
        return 0U;
    }
    /* Snapshot metadata only; never mask UART/DMA interrupts for the bulk copy. */
    primask = __get_PRIMASK();
    __disable_irq();
    current = Sph0645Diag.completed_blocks;
    half = completed_half;
    tick = Sph0645Diag.last_block_tick;
    if (primask == 0U)
    {
        __enable_irq();
    }
    if (current == consumed_sequence)
    {
        return 0U;
    }
    if (((DMA1->ISR & SPH0645_BOUNDARY_FLAGS) != 0U) ||
        (Sph0645_HalfIsInactive(half) == 0U))
    {
        return 0U;
    }
    Sph0645Diag.dropped_blocks += (uint32_t)(current - consumed_sequence - 1U);
    consumed_sequence = current;
    __DMB();
    for (index = 0U; index < SPH0645_BLOCK_WORDS; index++)
    {
        words[index] = capture_words[half * SPH0645_BLOCK_WORDS + index];
    }
    __DMB();
    if ((Sph0645Diag.running == 0U) ||
        (current != Sph0645Diag.completed_blocks) ||
        ((DMA1->ISR & SPH0645_BOUNDARY_FLAGS) != 0U) ||
        (Sph0645_HalfIsInactive(half) == 0U))
    {
        Sph0645Diag.copy_races++;
        Sph0645Diag.dropped_blocks++;
        return 0U;
    }
    *sequence = current;
    *block_tick = tick;
    Sph0645Diag.copied_blocks++;
    return 1U;
}
