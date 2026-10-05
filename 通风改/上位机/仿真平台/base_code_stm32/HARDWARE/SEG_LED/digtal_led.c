/**
 * @file    digtal_led.c
 * @brief   数码管显示驱动实现
 * @author  
 * @version V1.0
 * @note
 *
 *硬件连接：------------------------MCU--74HC573---数码管显示控制
 *            说明: 2个74HC573，第一个用于控制段选，第二个用于控制位选（带时钟的，需要使用位选中的2个输出）
 *            ----------------------------------------------------------------
 *数码管段选：
 *			    MCU--------74HC573---------数码管
 *            PA0--------D0---O0---------A 
 *            PA1--------D1---O1---------B 
 *            PA2--------D2---O2---------C 
 *            PA3--------D3---O3---------D 
 *            PA4--------D4---O4---------E 
 *            PA5--------D5---O5---------F 
 *            PA6--------D6---O6---------G 
 *            PA7--------D7---O7---------DP
 *            PA8-----------OE(低电平有效)
 *			    PA9-----------LE(高电平有效)
 *            ---------------------------------------------------------------
 *数码管控制位选：
 *			   MCU--------74HC573---------数码管
 *           PB0--------D0---O0---------最右一位
 *           PB1--------D1---O1---------次右
 *           PB2--------D2---O2---------次左
 *           PB10-------D3---O3---------最左一位
 *           PB11-------D4---O4---------初始化数码管中间两点对应的引脚 p
 *           PB12-------D5---O5---------初始化数码管中间两点对应的引脚 n
 */

#include "digtal_led.h"

uint16_t table[]={0xC0,0xf9,0xa4,0xb0,0x99,0x92,0x82,0xf8,0x80,0x90,0x88,0x83,0xc6,0xa1,0x86,0x8e};

/**
 * @brief 7/8 段字形表（共 16 项，0-F）
 *
 * 每个表项对应段选的位模式，直接写入段选端口以显示对应数字/字符。
 */


void digtal_LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;  

    RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB , ENABLE);
    // 初始化数码管段选对应的引脚
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_7; 
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure); //初始化   
    GPIO_SetBits(GPIOA, GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_7 ); 

#if 1
    // 初始化数码管位选对应的引脚
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure); //初始化   
    GPIO_ResetBits(GPIOB, GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_10 ); 
    
    // 初始化数码管两点控制对应的引脚
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_11 | GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure); //初始化   
    GPIO_ResetBits(GPIOB, GPIO_Pin_11 | GPIO_Pin_12); 
#endif
}


/**
 * @brief 显示单个数码管（写入段选数据）
 * @param num 要显示的数值（0-15），超出范围时显示 0
 */
void show_digtal_one_segment(uint32_t num)
{
    if( num > 15)
        num = 0;
    GPIO_Write(GPIOA, table[num]);
}

void show_digtal_four_segment(uint32_t num)
{
    static uint32_t i = 0;

    if (num > 9999)
        num = 0;

    GPIO_ResetBits(GPIOB, GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_10 ); 
    switch ( i++ )
    {
    case 0: // 个位
    {
        show_digtal_one_segment( num % 10 );
        GPIO_SetBits( GPIOB, GPIO_Pin_0 );
    }
    break;
    case 1: // 十位
    {
        show_digtal_one_segment( (num/10) % 10 );
        GPIO_SetBits( GPIOB, GPIO_Pin_1 );
    }
    break;
    case 2: // 百位
    {
        show_digtal_one_segment( (num/100) % 10 );
        GPIO_SetBits( GPIOB, GPIO_Pin_2 );
    }
    break;
    case 3: // 千位
    {
        show_digtal_one_segment( num/1000 );
        GPIO_SetBits( GPIOB, GPIO_Pin_10 );
    }
    break;
    
    default:
        i = 0;
        break;
    }
    if( i>3 )
        i = 0;
}