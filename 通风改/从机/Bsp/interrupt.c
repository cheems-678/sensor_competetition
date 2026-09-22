#include "interrupt.h"

#include "slave_protocol_runtime.h"
#include "usart.h"

volatile uint16_t time_100ms;
volatile uint8_t Rx2Buffer[100];
volatile uint8_t rx2_pointer;
uint8_t rx2_data;

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *handle)
{
    if (handle->Instance == TIM4)
    {
        time_100ms++;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle->Instance == USART2)
    {
        if (SlaveRuntime_IsApplicationMode() != 0U)
        {
            SlaveRuntime_PushRxByteFromIsr(rx2_data);
        }
        else if (rx2_pointer < sizeof(Rx2Buffer))
        {
            Rx2Buffer[rx2_pointer++] = rx2_data;
        }
        (void)HAL_UART_Receive_IT(&huart2, &rx2_data, 1U);
    }
}
