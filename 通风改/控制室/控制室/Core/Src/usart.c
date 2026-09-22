#include "usart.h"

#include "lora.h"

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
uint8_t Rx2Buffer[UART_RX_BUFFER_SIZE];
volatile uint8_t rx2_pointer, rx2_frame_ready, rx2_overflow;
uint8_t rx2_data;
uint8_t Rx1Buffer[UART_RX_BUFFER_SIZE];
volatile uint8_t rx1_pointer, rx1_frame_ready, rx1_overflow;
uint8_t rx1_data;

static void UART_Init(UART_HandleTypeDef *handle, USART_TypeDef *instance)
{
    handle->Instance = instance;
    handle->Init.BaudRate = 115200;
    handle->Init.WordLength = UART_WORDLENGTH_8B;
    handle->Init.StopBits = UART_STOPBITS_1;
    handle->Init.Parity = UART_PARITY_NONE;
    handle->Init.Mode = UART_MODE_TX_RX;
    handle->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    handle->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(handle) != HAL_OK)
    {
        Error_Handler();
    }
}

void MX_USART1_UART_Init(void)
{
    UART_Init(&huart1, USART1);
}

void MX_USART2_UART_Init(void)
{
    UART_Init(&huart2, USART2);
}

void HAL_UART_MspInit(UART_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    if (handle->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_9;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio);
        gpio.Pin = GPIO_PIN_10;
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &gpio);
        HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
    else if (handle->Instance == USART2)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
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
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *handle)
{
    if (handle->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);
        HAL_NVIC_DisableIRQ(USART1_IRQn);
    }
    else if (handle->Instance == USART2)
    {
        __HAL_RCC_USART2_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_3);
        HAL_NVIC_DisableIRQ(USART2_IRQn);
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

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle->Instance == USART2)
    {
        LoraControl_OnLoraUartByteFromIsr(rx2_data);
        (void)HAL_UART_Receive_IT(&huart2, &rx2_data, 1U);
    }
    else if (handle->Instance == USART1)
    {
        LoraControl_OnPcUartByteFromIsr(rx1_data);
        (void)HAL_UART_Receive_IT(&huart1, &rx1_data, 1U);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    if (handle->Instance == USART1)
    {
        LoraControl_OnPcUartErrorFromIsr();
        (void)HAL_UART_Receive_IT(&huart1, &rx1_data, 1U);
    }
    else if (handle->Instance == USART2)
    {
        LoraControl_OnLoraUartErrorFromIsr();
        (void)HAL_UART_Receive_IT(&huart2, &rx2_data, 1U);
    }
}
