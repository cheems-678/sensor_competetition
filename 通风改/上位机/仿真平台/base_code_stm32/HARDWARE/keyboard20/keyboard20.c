/**
 ******************************************************************************
 * @file    keyboard20.c
 * @brief   4x5 矩阵键盘驱动实现（20按键）
 *
 * 实现标准 5 行 × 4 列矩阵键盘的初始化、扫描与键值处理。
 * 默认使用以下 STM32F1 引脚：
 * - 行线（输出）：
 *     - PA5, PA6, PA7 （GPIOA）
 *     - PB0, PB1       （GPIOB）
 * - 列线（输入）：PA1, PA2, PA3, PA4（下拉输入）
 *
 * @note    - 按键按下时，对应行列导通，列线被拉高（需行输出高电平驱动）
 *          - 扫描采用逐行置高、其余置低的方式检测列状态
 *          - 使用静态标志位  flag1~flag5 实现“按下后需释放才能再次触发”的软件消抖
 *          - 键值通过全局变量  KEY20 传递，由  key20_process() 处理
 *          - 按键功能包括数字键、方向键、功能键（如 F1/F2、Esc、Enter 等）
 ******************************************************************************
 */
#include "keyboard20.h"
#include <stdio.h>

/**
 * @brief  存储最近一次有效按键的键值（0 = 无按键）
 *
 * 取值范围：0x01 ~ 0x14，分别对应 F1 ~ Ent（共20个键）。
 * 由 \ref KEYBOARD20_Scan() 更新，\ref key20_process() 清零。
 */
uint8_t KEY20 = 0;

/**
 * @brief  初始化 5x4 矩阵键盘的 GPIO 引脚
 *
 * 配置：
 * - 行线（PA5/6/7, PB0/1）为推挽输出（50MHz）
 * - 列线（PA1~PA4）为下拉输入
 *
 * @note   - 所有行线初始状态为低电平（由复位状态保证）
 *         - 若硬件列线上无上拉电阻，需确保行输出高时能可靠驱动列线至高电平
 */
void KEYBOARD20_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; //行，推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; //行，推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD; // 列，下拉输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/**
 * @brief  扫描 5x4 矩阵键盘，返回有效按键值
 * @retval 0x01~0x14: 对应 F1/Ent 等20个按键（一次有效触发）
 * @retval 0: 无按键或按键未释放
 *
 * 扫描逻辑：
 * 1. 依次将每一行（H1~H5）置高，其余行置低
 * 2. 读取 4 列（L1~L4）状态，若某列为高，则该行列交叉点按键被按下
 * 3. 使用静态标志位 \c flag1~flag5 实现“按下锁定 + 释放解锁”防抖
 *
 * @note   - 此为轮询式扫描，建议在主循环中定期调用
 *         - 不支持多键同时按下（仅返回第一个检测到的键）
 *         - 消抖基于释放检测，响应较快，无需延时
 *
 * @warning 若列线无法被拉高（如驱动能力不足或无上拉），将无法检测按键。
 *          建议在列线上接 10kΩ 上拉电阻至 VCC。
 */
uint8_t KEYBOARD20_Scan(void)
{
    static uint8_t flag1 = 1, flag2 = 1, flag3 = 1, flag4 = 1, flag5 = 1;

    H1 = 1;
    H2 = 0;
    H3 = 0;
    H4 = 0;
    H5 = 0;
    if (flag1 && (L1 == 1 || L2 == 1 || L3 == 1 || L4 == 1))
    {
        flag1 = 0;
        if (L1 == 1)
            KEY20 = KEY1_PRES;
        else if (L2 == 1)
            KEY20 = KEY2_PRES;
        else if (L3 == 1)
            KEY20 = KEY3_PRES;
        else if (L4 == 1)
            KEY20 = KEY4_PRES;
    }
    else if (L1 == 0 && L2 == 0 && L3 == 0 && L4 == 0)
        flag1 = 1;
    // else
    //     return 0;

    H1 = 0;
    H2 = 1;
    H3 = 0;
    H4 = 0;
    H5 = 0;
    if (flag2 && (L1 == 1 || L2 == 1 || L3 == 1 || L4 == 1))
    {
        flag2 = 0;
        if (L1 == 1)
            KEY20 = KEY5_PRES;
        else if (L2 == 1)
            KEY20 = KEY6_PRES;
        else if (L3 == 1)
            KEY20 = KEY7_PRES;
        else if (L4 == 1)
            KEY20 = KEY8_PRES;
    }
    else if (L1 == 0 && L2 == 0 && L3 == 0 && L4 == 0)
        flag2 = 1;
    // else
    //     return 0;

    H1 = 0;
    H2 = 0;
    H3 = 1;
    H4 = 0;
    H5 = 0;
    if (flag3 && (L1 == 1 || L2 == 1 || L3 == 1 || L4 == 1))
    {
        flag3 = 0;
        if (L1 == 1)
            KEY20 = KEY9_PRES;
        else if (L2 == 1)
            KEY20 = KEY10_PRES;
        else if (L3 == 1)
            KEY20 = KEY11_PRES;
        else if (L4 == 1)
            KEY20 = KEY12_PRES;
    }
    else if (L1 == 0 && L2 == 0 && L3 == 0 && L4 == 0)
        flag3 = 1;
    // else
    //     return 0;

    H1 = 0;
    H2 = 0;
    H3 = 0;
    H4 = 1;
    H5 = 0;
    if (flag4 && (L1 == 1 || L2 == 1 || L3 == 1 || L4 == 1))
    {
        flag4 = 0;
        if (L1 == 1)
            KEY20 = KEY13_PRES;
        else if (L2 == 1)
            KEY20 = KEY14_PRES;
        else if (L3 == 1)
            KEY20 = KEY15_PRES;
        else if (L4 == 1)
            KEY20 = KEY16_PRES;
    }
    else if (L1 == 0 && L2 == 0 && L3 == 0 && L4 == 0)
        flag4 = 1;
    // else
    //     return 0;

    H5 = 1;
    H1 = 0;
    H2 = 0;
    H3 = 0;
    H4 = 0;
    if (flag5 && (L1 == 1 || L2 == 1 || L3 == 1 || L4 == 1))
    {
        flag5 = 0;
        if (L1 == 1)
            KEY20 = KEY17_PRES;
        else if (L2 == 1)
            KEY20 = KEY18_PRES;
        else if (L3 == 1)
            KEY20 = KEY19_PRES;
        else if (L4 == 1)
            KEY20 = KEY20_PRES;
    }

    else if (L1 == 0 && L2 == 0 && L3 == 0 && L4 == 0)
        flag5 = 1;
    // else
    //     return 0;
    return KEY20;
}

/**
 * @brief  处理当前按键事件并输出调试信息
 *
 * 根据全局变量 \c KEY20 的值，通过 \c printf 输出对应按键功能名称。
 * 处理完成后清零 \c KEY20，避免重复处理。
 *
 * @note   - 依赖标准库 \c stdio.h 和串口重定向（如 \c fputc）
 *         - 实际部署时建议移除或条件编译 \c printf
 *         - 可扩展为执行具体功能（如菜单导航、参数输入等）
 */
void key20_process(void)
{
    //串口打印信息
    switch (KEY20)
    {
    case 0x01:
        printf("input 'F1'\n");
        break;
    case 0x02:
        printf("input 'F2'\n");
        break;
    case 0x03:
        printf("input '#'\n");
        break;
    case 0x04:
        printf("input '*'\n");
        break;
    case 0x05:
        printf("input '1'\n");
        break;
    case 0x06:
        printf("input '2'\n");
        break;
    case 0x07:
        printf("input '3'\n");
        break;
    case 0x08:
        printf("input 'UP'\n");
        break;
    case 0x09:
        printf("input '4'\n");
        break;
    case 0x0a:
        printf("input '5'\n");
        break;
    case 0x0b:
        printf("input '6'\n");
        break;
    case 0x0c:
        printf("input 'DOWN'\n");
        break;
    case 0x0d:
        printf("input '7'\n");
        break;
    case 0x0e:
        printf("input '8'\n");
        break;
    case 0x0f:
        printf("input '9'\n");
        break;
    case 0x10:
        printf("input 'Esc'\n");
        break;
    case 0x11:
        printf("input 'LEFT' \n");
        break;
    case 0x12:
        printf("input '0'\n");
        break;
    case 0x13:
        printf("input 'RIGHT'\n");
        break;
    case 0x14:
        printf("input 'Ent'\n");
        break;
    default:
        break;
    }
    KEY20 = 0;  // 清除按键标志
}