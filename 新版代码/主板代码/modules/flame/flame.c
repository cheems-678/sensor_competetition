/**
 * @file    flame.c
 * @brief   火焰传感器模块驱动实现
 * @note    配置 PA8 为浮空输入, 读取 LM393 比较器数字输出。
 */
#include "flame.h"

void FLAME_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin  = FLAME_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(FLAME_PORT, &gpio);
}

uint8_t FLAME_Read(void)
{
    BitAction lvl = GPIO_ReadInputDataBit(FLAME_PORT, FLAME_PIN);

#if FLAME_ACTIVE_LOW
    return (lvl == Bit_RESET) ? FLAME_DETECTED : FLAME_NONE;
#else
    return (lvl == Bit_SET)   ? FLAME_DETECTED : FLAME_NONE;
#endif
}
