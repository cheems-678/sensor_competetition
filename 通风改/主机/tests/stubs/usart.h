#ifndef TEST_USART_H
#define TEST_USART_H
#include "main.h"
extern UART_HandleTypeDef huart2;
extern uint8_t Rx2Buffer[256];
extern uint16_t rx2_pointer;
extern uint8_t rx2_frame_ready, rx2_overflow;
void Usart_SendString(UART_HandleTypeDef uart, unsigned char *data, unsigned short length);
#endif
