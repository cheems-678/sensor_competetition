#ifndef MQ2_ADC_H
#define MQ2_ADC_H
#include <stdint.h>

uint8_t Mq2Adc_Init(void);
uint8_t Mq2Adc_Start(void);
/* 0=conversion pending, 1=complete, 2=hardware error. Never waits for EOC. */
uint8_t Mq2Adc_Poll(uint16_t *raw);
void Mq2Adc_Stop(void);
#endif
