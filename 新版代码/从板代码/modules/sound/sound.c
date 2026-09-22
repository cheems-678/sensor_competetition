/**
 * @file    sound.c
 * @brief   声音传感器模块驱动实现
 * @note    配置 PA6 为浮空输入, 读取 LM393 比较器数字输出。
 */
#include "sound.h"

void SOUND_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin  = SOUND_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(SOUND_PORT, &gpio);
}

uint8_t SOUND_Read(void)
{
    BitAction lvl = GPIO_ReadInputDataBit(SOUND_PORT, SOUND_PIN);

#if SOUND_ACTIVE_HIGH
    return (lvl == Bit_SET)   ? SOUND_DETECTED : SOUND_QUIET;
#else
    return (lvl == Bit_RESET) ? SOUND_DETECTED : SOUND_QUIET;
#endif
}
