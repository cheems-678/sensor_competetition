/**
 ******************************************************************************
 * @file    ds1302.c
 * @brief   DS1302 实时时钟模块 驱动实现
 *
 * 实现 DS1302 芯片的三线（SCLK、I/O、RST）通信协议，支持时间读写。
 * 默认使用 STM32F1 的 GPIOB 引脚：
 * - PB6  → RST（复位/片选）
 * - PB10 → SCLK（时钟）
 * - PB11 → I/O（数据）
 *
 * @note    - 所有寄存器以 BCD 码格式存储（如 0x23 表示 23）
 *          - 写入前需关闭写保护（CONTROL 寄存器 = 0x00），写完后启用（= 0x80）
 *          - 本驱动不使用硬件 SPI，采用 GPIO 模拟三线时序
 *          - 时间范围：年（00～99）、月（01～12）、日（01～31）、时（00～23）、分/秒（00～59）
 ******************************************************************************
 */

#include "ds1302.h"

/**
 * @brief  软件延时函数（用于满足 DS1302 时序要求）
 * @param  i: 延时循环次数（无单位，依赖系统主频）
 *
 * @warning 此延时精度依赖 CPU 主频，若主频变化需重新校准。
 *          在高优化等级下可能被编译器优化掉，建议使用 \c volatile 或内联汇编替代。
 */
static void delay(int i) {
	while (i) {
		i--;
	}
}

/**
 * @brief  初始化 DS1302 所需的 GPIO 引脚
 *
 * 配置 PB6（RST）、PB10（SCLK）、PB11（DAT）为推挽输出，并初始化为低电平。
 * 此函数应在系统启动时调用一次。
 */
void DS1302_Init() {
	GPIO_InitTypeDef GPIO_InitStructure;
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_10 | GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);	

	GPIO_ResetBits(GPIOB, GPIO_Pin_6 | GPIO_Pin_10 | GPIO_Pin_11);
}

/**
 * @brief  将 DS1302 数据引脚（PB11）配置为输出模式
 *
 * 用于主机向 DS1302 发送数据。
 */
void DS1302_Out() {
	GPIO_InitTypeDef GPIO_InitStructure;	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);	
}

/**
 * @brief  将 DS1302 数据引脚（PB11）配置为输入模式（上拉）
 *
 * 用于从 DS1302 读取数据。
 */
void DS1302_In() {
	GPIO_InitTypeDef GPIO_InitStructure;	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOB, &GPIO_InitStructure);	
}

/**
 * @brief  向 DS1302 写入一个字节（LSB 先传）
 * @param  value: 要写入的 8 位数据
 *
 * @note   - 时序：先拉低 CLK，设置 DAT，再拉高 CLK（上升沿锁存）
 *         - 使用软件延时确保满足最小脉冲宽度（>1μs）
 */
void DS1302_WriteByte(u8 value) {
	u8 i;
	DS1302_Out();
	for (i=0;i<8;i++){
		CLK_LOW;
		if (value&0x01) {
			DAT_HIGH;
		} else {
			DAT_LOW;
		}
		delay(1);
		CLK_HIGH;
		delay(1);		
		value >>= 1;
	}
}

/**
 * @brief  从 DS1302 读取一个字节（LSB 先传）
 * @param[out] value: 指向接收缓冲区的指针
 *
 * @note   - 时序：先拉高 CLK，读取 DAT，再拉低 CLK
 *         - 在 CLK 下降沿后采样数据位
 */
void DS1302_ReadByte(u8 *value) {
	u8 i;
	DS1302_In();
	for (i=0;i<8;i++) {
		*value >>= 1;
		CLK_HIGH;
		delay(1);
		CLK_LOW;
		delay(1);
		if (DAT == 1) {
			*value |= 0x80;
		} else {
			*value &= 0x7F;
		}
	}
}

/**
 * @brief  从 DS1302 指定地址读取一个寄存器值
 * @param  addr: 寄存器地址（如 SEC=0x80, MIN=0x82...）
 * @param[out] value: 读取到的原始 BCD 数据
 *
 * @note   地址自动转换为读命令（addr<<1 | 0x01）
 */
void DS1302_ReadReg(u8 addr, u8 *value) {
	CLK_LOW;
	RST_HIGH;
	DS1302_WriteByte((addr<<1)|0x01);
	DS1302_ReadByte(value);
	RST_LOW;
}

/**
 * @brief  向 DS1302 指定地址写入一个寄存器值
 * @param  addr: 寄存器地址
 * @param  value: 要写入的 BCD 数据
 *
 * @note   地址自动转换为写命令（(addr<<1) & 0xFE）
 */
void DS1302_WriteReg(u8 addr, u8 value) {
	CLK_LOW;
	RST_HIGH;
	DS1302_WriteByte((addr<<1)&0xfe);
	DS1302_WriteByte(value);
	CLK_LOW;
	RST_LOW;
}

/**
 * @brief  获取当前年份（00～99）
 * @param[out] year: 返回十进制年份（如 25 表示 2025 年）
 *
 * @note   从 YEAR 寄存器（0x8C）读取 BCD 值并转换为十进制。
 */
void DS1302_GetYear(u8 *year) {
	u8 value;
	DS1302_ReadReg(YEAR, &value);
	*year = ((value&0xf0)>>4)*10 + (value&0x0f);
}

/**
 * @brief  获取当前月份（1～12）
 * @param[out] month: 返回十进制月份
 */
void DS1302_GetMonth(u8 *month) {
	u8 value;
	DS1302_ReadReg(MONTH, &value);
	*month = ((value&0x10)>>4)*10 + (value&0x0f);
}

/**
 * @brief  获取当前日期（1～31）
 * @param[out] date: 返回十进制日
 */
void DS1302_GetDate(u8 *date) {
	u8 value;
	DS1302_ReadReg(DATE, &value);
	*date = ((value&0x30)>>4)*10 + (value&0x0f);
}

/**
 * @brief  获取当前小时（0～23，24 小时制）
 * @param[out] hour: 返回十进制小时
 */
void DS1302_GetHour(u8 *hour) {
	u8 value;
	DS1302_ReadReg(HR, &value);
	*hour = ((value&0x10)>>4)*10 + (value&0x0f);
}

/**
 * @brief  获取当前分钟（0～59）
 * @param[out] minute: 返回十进制分钟
 */
void DS1302_GetMinite(u8 *minute) {
	u8 value;
	DS1302_ReadReg(MIN, &value);
	*minute = ((value&0x70)>>4)*10 + (value&0x0f);
}

/**
 * @brief  获取当前秒数（0～59）
 * @param[out] second: 返回十进制秒
 */
void DS1302_GetSecond(u8 *second) {
	u8 value;
	DS1302_ReadReg(SEC, &value);
	*second = ((value&0x70)>>4)*10 + (value&0x0f);
}

/**
 * @brief  设置 DS1302 当前时间
 * @param  yr: 年（00～99，如 25 表示 2025）
 * @param  mon: 月（1～12）
 * @param  date: 日（1～31）
 * @param  hr: 小时（0～23）
 * @param  min: 分钟（0～59）
 * @param  sec: 秒（0～59）
 *
 * @note   - 自动处理 BCD 编码转换
 *         - 写入前关闭写保护（CONTROL=0x00），写完后启用（CONTROL=0x80）
 *         - 若需停止时钟，可将 SEC 寄存器最高位置 1（本函数默认清除此位）
 */
void DS1302_SetTime(u8 yr, u8 mon, u8 date, u8 hr, u8 min, u8 sec) {
	DS1302_WriteReg(CONTROL, 0x00);	// 关闭写保护
	DS1302_WriteReg(SEC, 0x80);		// 暂停时钟（可选，确保设置原子性）
	DS1302_WriteReg(YEAR, ((yr/10)<<4)|(yr%10));
	DS1302_WriteReg(MONTH, ((mon/10)<<4)|(mon%10));
	DS1302_WriteReg(DATE, ((date/10)<<4)|(date%10));
	DS1302_WriteReg(HR, ((hr/10)<<4)|(hr%10));
	DS1302_WriteReg(MIN, ((min/10)<<4)|(min%10));
	DS1302_WriteReg(SEC, ((sec/10)<<4)|(sec%10));	// 自动清除 CH 位（启动时钟）
	DS1302_WriteReg(CONTROL, 0x80);	// 启用写保护
}
