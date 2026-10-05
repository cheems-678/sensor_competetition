/**
 ******************************************************************************
 * @file    keyboard16.c
 * @brief   4x4 矩阵键盘驱动实现（16按键）
 *
 * 实现标准 4 行 × 4 列矩阵键盘的初始化、扫描与键值处理。
 * 默认使用以下 STM32F1 引脚：
 * - 行线（输出）：PB5, PB6, PB7, PA8
 * - 列线（输入）：PB12, PB13, PB14, PB15（下拉输入）
 *
 * @note    - 按键按下时，对应行列导通，列线被拉高（需外部上拉或行输出高）
 *          - 扫描采用逐行置高、其余置低的方式检测列状态
 *          - 使用静态标志位实现简单软件消抖（防止重复触发）
 *          - 键值通过全局变量 \c KEY16 传递，由 \c key16_process() 处理
 ******************************************************************************
 */
#include "keyboard16.h"
#include <stdio.h>

/**
 * @brief  存储最近一次有效按键的键值（0 = 无按键）
 *
 * 取值范围：0x01 ~ 0x10，分别对应 K1 ~ K16。
 * 由 \ref KEYBOARD16_Scan() 更新，\ref key16_process() 清零。
 */
uint8_t KEY16 = 0;

/**
 * @brief  初始化 4x4 矩阵键盘的 GPIO 引脚
 *
 * 配置：
 * - 行线（PB5/PB6/PB7/PA8）为推挽输出（50MHz）
 * - 列线（PB12~PB15）为下拉输入
 *
 * @note   - 所有行线初始状态设为低电平（通过 \c GPIO_ResetBits）
 *         - 若硬件使用上拉电阻，可确保无按键时列线为低
 */
void KEYBOARD16_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

    // 使能 GPIOA 和 GPIOB 时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB,ENABLE);
	
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_Out_PP;                           //行，推挽输出
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_InitStructure);
	
    GPIO_ResetBits(GPIOA,GPIO_Pin_8);
    GPIO_InitStructure.GPIO_Mode=GPIO_Mode_Out_PP;                           //行，推挽输出
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_8;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
    GPIO_ResetBits(GPIOA,GPIO_Pin_8);
	
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IPD;                              // 列，下拉输入
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15;
	GPIO_Init(GPIOB,&GPIO_InitStructure);
}

/**
 * @brief  扫描 4x4 矩阵键盘，返回有效按键值
 * @retval 0x01~0x10: 对应 K1~K16 按下（一次有效触发）
 * @retval 0: 无按键或按键未释放
 *
 * 扫描逻辑：
 * 1. 依次将每一行置高，其余行置低
 * 2. 读取 4 列状态，若某列为高，则该行列交叉点按键被按下
 * 3. 使用静态标志位 \c flag1~flag4 实现“按下后需释放才能再次触发”的防抖
 *
 * @note   - 此为轮询式扫描，建议在主循环中定期调用
 *         - 不支持多键同时按下（仅返回第一个检测到的键）
 *         - 消抖依赖“释放检测”，非时间延时，响应较快
 *
 * @warning 若硬件列线上无上拉，可能无法检测到高电平。确保：
 *          - 行输出高时能驱动列线至高电平（或外接上拉电阻）
 */
uint8_t KEYBOARD16_Scan(void)
{	
    static uint8_t flag1=1,flag2=1,flag3=1,flag4=1;
    uint8_t KEY16 = 0;

    // 扫描第1行 (h1=PB5)
    h1 = 1;
    h2 = 0;
    h3 = 0;
    h4 = 0;
    if(flag1&&(l1==1||l2==1||l3==1||l4==1))
    {	
        flag1=0;
        if(l1==1)
        {
            KEY16=KEY1_PRES;
        }
        else if(l2==1)
            KEY16=KEY2_PRES;
        else if(l3==1)
            KEY16=KEY3_PRES;		
        else if(l4==1)
            KEY16=KEY4_PRES;
    }
    else if(l1==0&&l2==0&&l3==0&&l4==0)
        flag1=1;
    // else 
    //     return 0;
	
    // 扫描第2行 (h2=PB6)
    h1 = 0;
    h2 = 1;
    h3 = 0;
    h4 = 0;
    if(flag2&&(l1==1||l2==1||l3==1||l4==1))
    {	
        flag2=0;
        if(l1==1)
            KEY16=KEY5_PRES;
        else if(l2==1)
            KEY16=KEY6_PRES;
        else if(l3==1)
            KEY16=KEY7_PRES;		
        else if(l4==1)
            KEY16=KEY8_PRES;
    }
    else if(l1==0&&l2==0&&l3==0&&l4==0)
        flag2=1;
    // else 
    //     return 0;
		  
    // 扫描第3行 (h3=PB7)
    h1 = 0;
    h2 = 0;
    h3 = 1;
    h4 = 0;
    if(flag3&&(l1==1||l2==1||l3==1||l4==1))
    {	
        flag3=0;
        if(l1==1)
            KEY16=KEY9_PRES;
        else if(l2==1)
            KEY16=KEY10_PRES;
        else if(l3==1)
            KEY16=KEY11_PRES;		
        else if(l4==1)
            KEY16=KEY12_PRES;
    }
    else if(l1==0&&l2==0&&l3==0&&l4==0)
        flag3=1;
    // else 
    //     return 0;
	
    // 扫描第4行 (h4=PA8)
	h4 = 1;
    h1 = 0;
    h2 = 0;
    h3 = 0;
    if(flag4&&(l1==1||l2==1||l3==1||l4==1))
    {	
        flag4=0;
        if(l1==1)
            KEY16=KEY13_PRES;
        else if(l2==1)
            KEY16=KEY14_PRES;
        else if(l3==1)
            KEY16=KEY15_PRES;		
        else if(l4==1)
            KEY16=KEY16_PRES;
    }
    else if(l1==0&&l2==0&&l3==0&&l4==0)
        flag4=1;
    // else 
    //     return 0;
			
    return KEY16;			
}
		
/**
 * @brief  处理当前按键事件并输出调试信息
 *
 * 根据全局变量 \c KEY16 的值，通过 \c printf 输出对应按键名称。
 * 处理完成后清零 \c KEY16，避免重复处理。
 *
 * @note   - 依赖标准库 \c stdio.h 和串口重定向（如 \c fputc）
 *         - 实际部署时建议移除或条件编译 \c printf
 *         - 可扩展为执行具体功能（如菜单选择、参数调整等）
 */
void key16_process(void)
{
    //串口打印信息
    switch (KEY16)
    {
    case 0x01:
        printf("input 'K1'\n");
        break;
    case 0x02:
        printf("input 'K2'\n");
        break;
    case 0x03:
        printf("input 'K3'\n");
        break;
    case 0x04:
        printf("input 'K4'\n");
        break;
    case 0x05:
        printf("input 'K5'\n");
        break;
    case 0x06:
        printf("input 'K6'\n");
        break;
    case 0x07:
        printf("input 'K7'\n");
        break;
    case 0x08:
        printf("input 'K8'\n");
        break;
    case 0x09:
        printf("input 'K9'\n");
        break;
    case 0x0a:
        printf("input 'K10'\n");
        break;
    case 0x0b:
        printf("input 'K11'\n");
        break;
    case 0x0c:
        printf("input 'K12'\n");
        break;
    case 0x0d:
        printf("input 'K13'\n");
        break;
    case 0x0e:
        printf("input 'K14'\n");
        break;
    case 0x0f:
        printf("input 'K15'\n");
        break;
    case 0x10:
        printf("input 'K16'\n");
        break;
    default:
        break;
    }
    KEY16 = 0;  // 清除按键标志
}
