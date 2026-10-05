#include "esp_at_uart.h"
#include "usart.h"
#include <string.h>

#define RX_SIZE 1024U
static uint8_t g_rx[RX_SIZE], g_rx_byte;
static volatile uint16_t g_head, g_tail;
static volatile uint8_t g_tx_result = 1U, g_rearm;
static uint16_t g_tx_length;
volatile EspAtUartDiagnostics EspAtUartDiag;

void EspAtUart_ClearRx(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    g_tail = g_head;
    if (mask == 0U) { __enable_irq(); }
}

void EspAtUart_Init(void)
{
    memset((void *)&EspAtUartDiag, 0, sizeof(EspAtUartDiag));
    g_head = g_tail = 0U;
    g_tx_result = 1U;
    g_rearm = 1U;
    EspAtUart_Service();
}

void EspAtUart_Service(void)
{
    if (g_rearm != 0U)
    {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        (void)HAL_UART_AbortReceive(&huart1);
        g_rearm = 0U;
        if (HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1U) != HAL_OK)
        {
            g_rearm = 1U;
            EspAtUartDiag.rearm_failures++;
        }
        if (mask == 0U) { __enable_irq(); }
    }
}

uint8_t EspAtUart_Read(uint8_t *byte)
{
    uint16_t tail = g_tail;
    if (byte == 0 || tail == g_head) { return 0U; }
    *byte = g_rx[tail];
    __DMB();
    g_tail = (uint16_t)((tail + 1U) & (RX_SIZE - 1U));
    return 1U;
}

uint8_t EspAtUart_StartTx(const uint8_t *data, uint16_t length)
{
    if (data == 0 || length == 0U || g_tx_result == 0U) { return 0U; }
    g_tx_length = length;
    g_tx_result = 0U;
    if (HAL_UART_Transmit_IT(&huart1, (uint8_t *)data, length) != HAL_OK)
    {
        g_tx_result = 2U;
        EspAtUartDiag.error_count++;
        return 0U;
    }
    return 1U;
}
uint8_t EspAtUart_PollTx(void) { return g_tx_result; }
void EspAtUart_AbortTx(void)
{
    (void)HAL_UART_AbortTransmit(&huart1);
    g_tx_result = 2U;
}

void EspAtUart_RxCompleteFromIsr(void)
{
    uint16_t next = (uint16_t)((g_head + 1U) & (RX_SIZE - 1U));
    EspAtUartDiag.rx_bytes++;
    if (next == g_tail) { EspAtUartDiag.overflow_count++; }
    else
    {
        g_rx[g_head] = g_rx_byte;
        __DMB();
        g_head = next;
    }
    if (HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1U) != HAL_OK)
    {
        g_rearm = 1U;
        EspAtUartDiag.rearm_failures++;
    }
}
void EspAtUart_TxCompleteFromIsr(void)
{
    EspAtUartDiag.tx_bytes += g_tx_length;
    g_tx_result = 1U;
}
void EspAtUart_ErrorFromIsr(void)
{
    EspAtUartDiag.error_count++;
    /* RX ORE and TX can overlap. The owner aborts/re-synchronizes both. */
    g_tx_result = 2U;
    g_rearm = 1U;
}
