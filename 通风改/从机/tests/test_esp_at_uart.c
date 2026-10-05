#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "esp_at_uart.h"
UART_HandleTypeDef huart1;
static uint8_t *rx_byte;
static uint32_t mask, rearm_count, abort_count;
static uint8_t rx_fail, tx_fail;
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask = 1U; }
void __enable_irq(void) { mask = 0U; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *handle)
{ assert(handle == &huart1); return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *handle)
{ assert(handle == &huart1); abort_count++; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *handle, uint8_t *data, uint16_t size)
{ assert(handle == &huart1 && size == 1U); rx_byte = data; rearm_count++; return rx_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *handle, uint8_t *data, uint16_t size)
{ assert(handle == &huart1 && data != 0 && size != 0U); return tx_fail ? HAL_ERROR : HAL_OK; }
static void receive(uint8_t byte) { *rx_byte = byte; EspAtUart_RxCompleteFromIsr(); }
int main(void)
{
    uint8_t byte, data[512] = {0}; unsigned i;
    EspAtUart_Init(); assert(rearm_count == 1U);
    for (i = 0U; i < 1024U; i++) { receive((uint8_t)i); }
    assert(EspAtUartDiag.overflow_count == 1U && EspAtUartDiag.rx_bytes == 1024U);
    for (i = 0U; i < 1023U; i++) { assert(EspAtUart_Read(&byte)); assert(byte == (uint8_t)i); }
    assert(!EspAtUart_Read(&byte));
    for (i = 0U; i < 100U; i++) { receive((uint8_t)(i + 1U)); assert(EspAtUart_Read(&byte) && byte == i + 1U); }
    receive(1U); mask = 1U; EspAtUart_ClearRx(); assert(mask == 1U && !EspAtUart_Read(&byte)); mask = 0U;
    assert(EspAtUart_StartTx(data, sizeof(data)) && EspAtUart_PollTx() == 0U);
    assert(!EspAtUart_StartTx(data, sizeof(data)));
    EspAtUart_TxCompleteFromIsr(); assert(EspAtUart_PollTx() == 1U && EspAtUartDiag.tx_bytes == 512U);
    tx_fail = 1U; assert(!EspAtUart_StartTx(data, 1U)); assert(EspAtUart_PollTx() == 2U);
    assert(EspAtUartDiag.error_count == 1U); tx_fail = 0U;
    assert(EspAtUart_StartTx(data, 1U)); EspAtUart_ErrorFromIsr();
    assert(EspAtUart_PollTx() == 2U && EspAtUartDiag.error_count == 2U);
    EspAtUart_AbortTx(); assert(abort_count == 1U);
    rx_fail = 1U; receive(2U); assert(EspAtUartDiag.rearm_failures == 1U);
    EspAtUart_Service(); assert(EspAtUartDiag.rearm_failures == 2U);
    rx_fail = 0U; EspAtUart_Service(); receive(3U);
    assert(EspAtUart_Read(&byte) && byte == 2U); assert(EspAtUart_Read(&byte) && byte == 3U);
    puts("PASS: real ESP UART ring, overflow, async TX, ISR errors and RX rearming");
    return 0;
}
