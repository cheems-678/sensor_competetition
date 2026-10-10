#ifndef MAX4466_ADC_H
#define MAX4466_ADC_H
#include <stdint.h>
#define MAX4466_CHANNELS 5U
#define MAX4466_CAPTURE_FRAMES 512U
#define MAX4466_CAPTURE_SAMPLES (MAX4466_CHANNELS * MAX4466_CAPTURE_FRAMES)
#define MAX4466_CAPTURE_PERIOD_MS 60U
#define MAX4466_CAPTURE_TIMEOUT_MS 100U
/* 512 * 5 * (239.5 + 12.5) / 12 MHz = 53.76 ms.
 * Use the earlier whole millisecond, never a late polling timestamp. */
#define MAX4466_CAPTURE_DURATION_MS 53U
typedef struct {
    uint16_t minimum[MAX4466_CHANNELS], maximum[MAX4466_CHANNELS];
    uint16_t mean[MAX4466_CHANNELS], samples; /* samples per channel */
    uint32_t tick, sequence;
} Max4466AdcWindow;
uint8_t Max4466Adc_Start(void);
void Max4466Adc_Stop(void);
/* 0=pending/idle, 1=complete stable five-channel window, 3=capture fault.
 * Array order: PA0, PA4, PA6, PB0, PA5 (ADC ranks IN0/4/6/8/5).
 * Normal DMA is stopped before statistics; no ADC/DMA IRQ is enabled. */
uint8_t Max4466Adc_Read(Max4466AdcWindow *output);
void Max4466Adc_IRQHandler(void);
#endif
