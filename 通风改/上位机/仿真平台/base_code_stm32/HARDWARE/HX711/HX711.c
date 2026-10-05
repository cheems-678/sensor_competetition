/**
 ******************************************************************************
 * @file    HX711.c
 * @brief   HX711 24位高精度ADC模块驱动实现
 *
 * 实现 HX711 电子秤专用 ADC 芯片的基本驱动。
 * 默认使用 STM32F1 的 GPIOB 引脚：
 * - PB8 → SCK（时钟输出）
 * - PB9 → DOUT（数据输入，上拉）
 *
 * @note    - 本实现固定使用通道 A、增益 128（最常用配置）
 *          - 读取函数为阻塞式，等待 DOUT 变低后开始采样
 *          - 返回值为 24 位无符号整数，已进行符号位修正（补码转偏移二进制）
 *          - 所有延时依赖 delay.h 提供的微秒级精度函数
 ******************************************************************************
 */
#include "HX711.h"
#include "delay.h"

/**
 * @brief  初始化 HX711 所需的 GPIO 引脚
 *
 * 配置：
 * - PB8 (SCK) 为推挽输出（50MHz）
 * - PB9 (DOUT) 为上拉输入
 *
 * @note   必须在调用 HX711_Read() 前执行一次。
 */
void Init_HX711pin(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);	 //使能PB端口时钟

	 //HX711_SCK
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;				 // 端口配置
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; 		 //推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;		 //IO口速度为50MHz
	GPIO_Init(GPIOB, &GPIO_InitStructure);					 //根据设定参数初始化GPIOB
	
	//HX711_DOUT
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;//输入上拉
    GPIO_Init(GPIOB, &GPIO_InitStructure);  			
}



/**
 * @brief  从 HX711 读取 24 位 ADC 数据（通道 A，增益 128）
 * @return 24 位 ADC 原始值（范围：0 ~ 0xFFFFFF）
 *
 * 通信流程：
 * 1. 等待 DOUT 变为低电平（表示转换完成）
 * 2. 发送 24 个 SCK 脉冲，在下降沿采样 DOUT
 * 3. 再发送 1 个额外脉冲以设置下次增益为 128（通道 A）
 * 4. 对结果异或 0x800000，将偏移二进制码转换为标准二进制（便于后续处理）
 *
 * @note   - 函数会阻塞直到 HX711 就绪（DOUT=0）
 *         - 若传感器未连接或损坏，可能无限等待（建议增加超时机制）
 *         - 返回值为无符号整数，负重时高位为 1（因已做符号修正）
 *
 * @warning 此函数仅支持通道 A、增益 128。如需其他配置，需修改末尾 SCK 脉冲数量。
 */
u32 HX711_Read(void)	//增益128
{
	unsigned long count; 
	unsigned char i; 
  //	HX711_DOUT=1; 
	delay_us(1);
	HX711_SCK=0; 
	count=0; 

    // 等待 HX711 完成转换（DOUT 拉低）
	while(HX711_DOUT); 
	// 读取 24 位数据（MSB 先传）
	for(i=0;i<24;i++)
	{ 
		HX711_SCK=1; 
		count=count<<1; 	// 左移准备接收新位
		delay_us(1);
		HX711_SCK=0; 
		if(HX711_DOUT)
			count++; 		// 在 SCK 下降沿采样
		delay_us(1);
	} 

    // 第 25 个脉冲：选择下次增益为 128（通道 A）
 	HX711_SCK=1; 
	delay_us(1);
	HX711_SCK=0;  // 通道A 增益128
//	delay_us(1);
//	 
//	HX711_SCK=1; 
//	delay_us(1);
//	HX711_SCK=0;  // 通道B 增益32
//	delay_us(1);
//	 
//	HX711_SCK=1; 
//	delay_us(1);
//	HX711_SCK=0;  // 通道A 增益64
	
    // HX711 输出为偏移二进制（offset binary），最高位为符号位
    // 异或 0x800000 可将其转换为标准二进制补码形式（便于有符号处理）
	count=count^0x800000;//第25个脉冲下降沿来时，转换数据
	return(count);
}

