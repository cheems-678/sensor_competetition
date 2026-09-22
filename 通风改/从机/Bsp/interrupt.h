#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "main.h"

extern volatile uint16_t time_100ms;
extern volatile uint8_t Rx2Buffer[100];
extern volatile uint8_t rx2_pointer;
extern uint8_t rx2_data;

#endif /* INTERRUPT_H */
