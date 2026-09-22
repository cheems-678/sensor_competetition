#include "usart.h"

#include <stdio.h>

#include "lora_transport.h"

UART_HandleTypeDef huart2;
uint8_t Rx2Buffer[UART_RX_BUFFER_SIZE];
volatile uint8_t rx2_pointer;
volatile uint8_t rx2_frame_ready;
volatile uint8_t rx2_overflow;
uint8_t rx2_data;

void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
}

void Usart_SendString(UART_HandleTypeDef uart,
                      const unsigned char *data,
                      unsigned short length)
{
    if ((data != NULL) && (length != 0U))
    {
        (void)HAL_UART_Transmit(&uart, (uint8_t *)data, length, HAL_MAX_DELAY);
    }
}

void HAL_UART_MspInit(UART_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio = {0};
    if (handle->Instance != USART2)
    {
        return;
    }

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *handle)
{
    if (handle->Instance != USART2)
    {
        return;
    }
    __HAL_RCC_USART2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_3);
    HAL_NVIC_DisableIRQ(USART2_IRQn);
}

int fputc(int ch, FILE *stream)
{
    (void)stream;
    if (LoraTransport_IsApplicationMode() == 0U)
    {
        HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1U, 1000U);
    }
    return ch;
}

static void UART_StoreRxByte(uint8_t data)
{
    if (rx2_frame_ready != 0U)
    {
        return;
    }
    if (rx2_pointer < (UART_RX_BUFFER_SIZE - 1U))
    {
        Rx2Buffer[rx2_pointer++] = data;
        Rx2Buffer[rx2_pointer] = '\0';
    }
    else
    {
        rx2_overflow = 1U;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle->Instance == USART2)
    {
        if (LoraTransport_IsApplicationMode() != 0U)
        {
            (void)LoraTransport_PushRxFromIsr(rx2_data);
        }
        else
        {
            UART_StoreRxByte(rx2_data);
        }
        (void)HAL_UART_Receive_IT(&huart2, &rx2_data, 1U);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    if (handle->Instance == USART2)
    {
        if (LoraTransport_IsApplicationMode() != 0U)
        {
            LoraTransport_ReportRxErrorFromIsr();
        }
        else
        {
            rx2_overflow = 1U;
        }
        (void)HAL_UART_Receive_IT(&huart2, &rx2_data, 1U);
    }
}
