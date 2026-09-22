/**
 * @file    buzzer.c
 * @brief   蜂鸣器驱动实现 (有源蜂鸣器, PB12 GPIO控制)
 */
#include "buzzer.h"

static uint8_t buzzer_on_level(void)  { return BUZZER_ACTIVE_HIGH ? Bit_SET : Bit_RESET; }
static uint8_t buzzer_off_level(void) { return BUZZER_ACTIVE_HIGH ? Bit_RESET : Bit_SET; }

BuzzerStatus Buzzer_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = BUZZER_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(BUZZER_PORT, &gpio);

    GPIO_WriteBit(BUZZER_PORT, BUZZER_PIN, buzzer_off_level());
    return BUZZER_OK;
}

void Buzzer_On(void)
{
    GPIO_WriteBit(BUZZER_PORT, BUZZER_PIN, buzzer_on_level());
}

void Buzzer_Off(void)
{
    GPIO_WriteBit(BUZZER_PORT, BUZZER_PIN, buzzer_off_level());
}

void Buzzer_Toggle(void)
{
    GPIO_WriteBit(BUZZER_PORT, BUZZER_PIN,
        (BitAction)!GPIO_ReadOutputDataBit(BUZZER_PORT, BUZZER_PIN));
}

static void buzzer_delay_ms(uint32_t ms)
{
    uint32_t i;
    for (i = 0; i < ms * 12000; i++)
        __NOP();
}

void Buzzer_Beep(uint32_t ms)
{
    Buzzer_On();
    buzzer_delay_ms(ms);
    Buzzer_Off();
}
