#ifndef MAX4466_ADC_H
#define MAX4466_ADC_H
#include <stdint.h>
#define MAX4466_CHANNELS 5U
#define MAX4466_BLOCK_FRAMES 128U
#define MAX4466_BLOCK_WORDS (MAX4466_CHANNELS * MAX4466_BLOCK_FRAMES)
uint8_t Max4466Adc_Start(void);
void Max4466Adc_Stop(void);
/* 0=no block, 1=stable complete block, 2=gap, 3=capture fault. */
uint8_t Max4466Adc_Read(uint16_t *output, uint32_t *sequence, uint32_t *tick);
void Max4466Adc_IRQHandler(void);
#endif
