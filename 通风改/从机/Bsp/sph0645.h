#ifndef SPH0645_H
#define SPH0645_H

#include <stdint.h>

#define SPH0645_DMA_WORDS       4096U
#define SPH0645_BLOCK_WORDS     (SPH0645_DMA_WORDS / 2U)
#define SPH0645_BLOCK_FRAMES    (SPH0645_BLOCK_WORDS / 4U)
#define SPH0645_CLOCK_TIMEOUT_MS 100U

enum {
    SPH0645_ERROR_NONE = 0,
    SPH0645_ERROR_CLOCK_CONFIG = 1,
    SPH0645_ERROR_DMA = 2,
    SPH0645_ERROR_SPI_OVERRUN = 3,
    SPH0645_ERROR_IRQ_LATE = 4,
    SPH0645_ERROR_NO_CLOCK = 5,
    SPH0645_ERROR_PIN_REMAP = 6
};

typedef struct {
    uint32_t running, start_count, failure_count;
    uint32_t active_error, last_error;
    uint32_t completed_blocks, copied_blocks, dropped_blocks, copy_races;
    uint32_t last_block_tick, bclk_hz, sample_rate_millihz;
    uint32_t dma_remaining, spi_status, dma_status, pin_inputs;
} Sph0645Diagnostics;

extern volatile Sph0645Diagnostics Sph0645Diag;

/* Owns TIM1/TIM3/SPI2/DMA1 Ch4; does not initialize or use HAL SPI/I2S handles. */
uint8_t Sph0645_Start(uint32_t now_ms);
void Sph0645_Stop(void);
uint32_t Sph0645_Check(uint32_t now_ms);
/* Only returns a copy of an inactive, unmodified half; sequence detects gaps. */
uint8_t Sph0645_ReadBlock(uint16_t words[SPH0645_BLOCK_WORDS],
                         uint32_t *sequence, uint32_t *block_tick);
void Sph0645_DmaIRQ(void);

#endif
