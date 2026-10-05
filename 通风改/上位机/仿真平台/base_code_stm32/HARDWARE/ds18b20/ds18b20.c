/**
 ******************************************************************************
 * @file    ds18b20.c
 * @brief   DS18B20 数字温度传感器驱动实现
 *
 * 实现 DS18B20 单总线（1-Wire）通信协议，支持温度读取与配置。
 * 默认使用 STM32F1 的 PA0 作为 DQ 数据引脚。
 *
 * @note    - 温度返回值单位：0.0001°C（如 251250 表示 25.1250°C）
 *          - 支持 -55°C ～ +125°C 测量范围
 *          - 初始化时自动配置：
 *            • 报警上限 = 100°C，下限 = 0°C
 *            • 分辨率 = 12 位（0.0625°C，寄存器值 0x7F）
 *          - 所有延时依赖 \c delay.h 提供的微秒级精度函数
 ******************************************************************************
 */
#include "ds18b20.h"
#include "delay.h"

/**
 * @brief  发送复位脉冲给 DS18B20
 *
 * 主机拉低总线 480～960μs（本实现为 750μs），然后释放（上拉），
 * 触发 DS18B20 发送存在脉冲（Presence Pulse）。
 */
void DS18B20_Rst(void)
{
  DS18B20_IO_OUT();   //SET PA0 OUTPUT
  DS18B20_DQ_OUT = 0; //拉低DQ
  delay_us(750);      //拉低750us
  DS18B20_DQ_OUT = 1; //DQ=1
  delay_us(15);       //15US
  //DS18B20_DQ_OUT = 0; //DQ=1
}

/**
 * @brief  检测 DS18B20 是否存在
 * @retval 0: 存在（收到正确存在脉冲）
 * @retval 1: 不存在（超时）
 *
 * DS18B20 在复位后会拉低总线 60～240μs 作为响应。
 * 本函数检测该低电平窗口。
 */
u8 DS18B20_Check(void)
{
  u8 retry = 0;
  DS18B20_IO_IN(); // 设置 PA0 为输入模式（开漏+上拉）

  // 等待 DS18B20 拉低（存在脉冲开始）
  while (DS18B20_DQ_IN && retry < 200)
  {
    retry++;
    delay_us(1);
  };
  if (retry >= 200) // 最多等待 200μs
    return 1; // 超时，无响应
  else
    retry = 0;
    // 等待 DS18B20 释放（存在脉冲结束）
  while (!DS18B20_DQ_IN && retry < 240) // 最多等待 240μs
  {
    retry++;
    delay_us(1);
  };
  if (retry >= 240)
    return 1; // 脉冲过长，异常

  return 0; // 存在脉冲正常
}

/**
 * @brief  从 DS18B20 读取 1 位数据
 * @retval 0 或 1: 读取到的数据位
 *
 * 读时序：
 * - 主机拉低 >1μs 后释放
 * - 在 15μs 内采样：高 → 1，低 → 0
 * - 总周期 ≥60μs
 */
u8 DS18B20_Read_Bit(void) // read one bit
{
  u8 data;
  DS18B20_IO_OUT(); //SET PA0 OUTPUT
  DS18B20_DQ_OUT = 0; // 拉低启动读时序
  delay_us(2);        // >1μs
  DS18B20_DQ_OUT = 1; // 释放总线
  DS18B20_IO_IN();    // 切换为输入

  delay_us(10);       // 在 15μs 内采样
  if (DS18B20_DQ_IN)
    data = 1;
  else
    data = 0;
  delay_us(50);       // 完成剩余时序（总 ≥60μs）
  return data;
}

/**
 * @brief  从 DS18B20 读取 1 字节数据（LSB 先传）
 * @retval 读取到的 8 位数据
 *
 * 连续读取 8 位，并按 LSB→MSB 顺序组装为字节。
 * 注意：DS18B20 低位先传，因此需右移累积。
 */
u8 DS18B20_Read_Byte(void) // read one byte
{
  u8 i, j, dat;
  dat = 0;
  for (i = 1; i <= 8; i++)
  {
    j = DS18B20_Read_Bit(); // 读取 LSB first
    dat = (j << 7) | (dat >> 1);  // 将新位放入最高位，整体右移
  }
  return dat;
}


/**
 * @brief  向 DS18B20 写入 1 字节数据（LSB 先传）
 * @param  dat: 要写入的 8 位数据
 *
 * 写 1：拉低 1～15μs 后释放  
 * 写 0：拉低 60～120μs 后释放  
 * 本实现：
 * - 写 1：2μs 低 + 61μs 高（总 63μs）
 * - 写 0：61μs 低 + 2μs 高（总 63μs）
 */
void DS18B20_Write_Byte(u8 dat)
{
  u8 j;
  u8 testb;
  DS18B20_IO_OUT(); //SET PA0 OUTPUT;
  for (j = 1; j <= 8; j++)
  {
    testb = dat & 0x01;
    dat = dat >> 1;
    if (testb)
    {
      DS18B20_DQ_OUT = 0; // Write 1
      delay_us(2);
      DS18B20_DQ_OUT = 1;
      delay_us(61);
    }
    else
    {
      DS18B20_DQ_OUT = 0; // Write 0
      delay_us(61);
      DS18B20_DQ_OUT = 1;
      delay_us(2);
    }
  }
}

/**
 * @brief  启动一次温度转换（跳过 ROM）
 *
 * 发送命令序列：
 * - 复位 + 存在检测
 * - Skip ROM (0xCC)
 * - Convert T (0x44)
 *
 * @note 转换完成后需等待 ≥750ms（12 位模式），但本函数不阻塞。
 */
void DS18B20_Start(void) // ds1820 start convert
{
  DS18B20_Rst();
  DS18B20_Check();
  DS18B20_Write_Byte(0xcc); // Skip ROM（单设备）
  DS18B20_Write_Byte(0x44); // Start temperature conversion
}

/**
 * @brief  初始化 DS18B20 并配置默认参数
 * @retval 0: 初始化成功
 * @retval 1: 未检测到 DS18B20
 *
 * 执行以下操作：
 * 1. 配置 PA0 为推挽输出
 * 2. 发送复位并检测设备
 * 3. 配置报警阈值（上限 100°C，下限 0°C）
 * 4. 设置分辨率为 12 位（0.0625°C）
 * 5. 将配置保存到 EEPROM（发送 Copy Scratchpad 命令 0x48）
 */
u8 DS18B20_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); // 使能PORTA口时钟

  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;        // PA0
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; // 复用推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  GPIO_SetBits(GPIOA, GPIO_Pin_0);  // 默认高电平
  DS18B20_Rst();
//	return DS18B20_Check();
  if(DS18B20_Check())
	{	
    //设备不存在
    return 1;
	}
	else
	{
    // 配置 TH/TL/Config 寄存器
		DS18B20_Write_Byte(0xCC);
		DS18B20_Write_Byte(0x4E);	//开启写入DS18B20模块的功能
		DS18B20_Write_Byte(100);	//写入报警阈值上限	100℃
		DS18B20_Write_Byte(0x00);	//写入报警阈值下限	0℃
		DS18B20_Write_Byte(0x7F); //设置分辨率	0x1F 0.5℃		0x3F 0.25℃  	0x5F 0.125℃		0x7F 0.0625℃
    // 保存配置到 EEPROM
		DS18B20_Rst();
		DS18B20_Check();
		DS18B20_Write_Byte(0xCC);
		DS18B20_Write_Byte(0x48);

		DS18B20_DQ_OUT = 1; // 释放总线
		return 0;
	}
}

/**
 * @brief  获取当前温度值
 * @return 温度值 × 10000（单位：0.0001°C）
 *         范围：-550000 ～ +1250000（即 -55.0000°C ～ +125.0000°C）
 *
 * 读取流程：
 * 1. 启动温度转换
 * 2. 复位并读取 9 字节暂存器（仅用前 2 字节：TL, TH）
 * 3. 处理符号位（bit11）
 * 4. 转换为整数：raw × 625 = °C × 10000（因 1 LSB = 0.0625°C = 625/10000）
 *
 * @note   - 若需更高效率，可分离“启动”和“读取”为两个函数以支持非阻塞
 *         - 当前实现为阻塞式（Start 后立即读，假设转换已完成）
 */
int DS18B20_Get_Temp(void)
{
  u8 temp;
  u8 TL, TH;
  int tem;
  DS18B20_Start(); // 启动转换（注意：此处未等待转换完成！）
  DS18B20_Rst();
  DS18B20_Check();
  DS18B20_Write_Byte(0xcc); // 发送skip ROM命令
  DS18B20_Write_Byte(0xbe); // convert
  TL = DS18B20_Read_Byte(); // LSB
  TH = DS18B20_Read_Byte(); // MSB

  if (TH > 7)
  {
    TH = ~TH;
    TL = ~TL;
    temp = 0; //温度为负
  }
  else
    temp = 1; //温度为正
  tem = TH;   //获得高八位
  tem <<= 8;
  tem += TL;                //获得底八位
  tem = tem * 625; //转换
  if (temp)
    return tem; //返回温度值
  else
    return -tem;
}
