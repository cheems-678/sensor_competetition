#ifndef ESP_AT_UART_H
#define ESP_AT_UART_H
#include <stdint.h>

typedef struct {
    uint32_t rx_bytes, tx_bytes, overflow_count, error_count, rearm_failures;
} EspAtUartDiagnostics;
extern volatile EspAtUartDiagnostics EspAtUartDiag;

/* USART1 only. Start retains the caller's TX buffer until PollTx finishes. */
void EspAtUart_Init(void);
void EspAtUart_Service(void);
uint8_t EspAtUart_Read(uint8_t *byte);
void EspAtUart_ClearRx(void);
uint8_t EspAtUart_StartTx(const uint8_t *data, uint16_t length);
/* 0=pending, 1=complete, 2=failed. */
uint8_t EspAtUart_PollTx(void);
void EspAtUart_AbortTx(void);
void EspAtUart_RxCompleteFromIsr(void);
void EspAtUart_TxCompleteFromIsr(void);
void EspAtUart_ErrorFromIsr(void);
#endif
