#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define UART_RX_BUFFER_SIZE (100U)

extern UART_HandleTypeDef huart2;
extern uint8_t Rx2Buffer[UART_RX_BUFFER_SIZE];
extern volatile uint8_t rx2_pointer;
extern volatile uint8_t rx2_frame_ready;
extern volatile uint8_t rx2_overflow;
extern uint8_t rx2_data;

void MX_USART2_UART_Init(void);
void Usart_SendString(UART_HandleTypeDef uart,
                      const unsigned char *data,
                      unsigned short length);

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */
