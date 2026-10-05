/**
 * @file    touch.c
 * @brief   电阻式触摸屏驱动（基于 ADS7843/ADS7846 芯片）
 *
 * 本文件实现了通过 GPIO 模拟 SPI 接口驱动 ADS7843/ADS7846 触摸控制器，
 * 支持坐标读取、中断检测、屏幕校准等功能。
 *
 * @note
 * - 使用三线制 SPI（TDIN, TCLK, DOUT），片选由 TCS 控制
 * - PEN 中断：PA11 下降沿触发（表示触摸按下）
 * - 默认命令：
 *     - CMD_RDX = 0xD0 → 读 X 坐标（12 位）
 *     - CMD_RDY = 0x90 → 读 Y 坐标（12 位）
 * - 校准方式：四点法（(20,20), (220,20), (20,300), (220,300)）
 * - 坐标转换公式：
 *     - X0 = xfac * X + xoff
 *     - Y0 = yfac * Y + yoff
 * - 引脚分配（STM32F10x）：
 *     - TDIN  → PA7
 *     - TCLK  → PA5
 *     - DOUT  → PA6（输入）
 *     - TCS   → PA12（片选）
 *     - PEN   → PA11（中断输入，上拉）
 *
 * @warning 校准失败时会自动翻转 X/Y 方向（处理屏幕倒置情况）
 */
#include "touch.h" 
#include "lcd.h"
#include "delay.h"
#include "stdlib.h"
#include "math.h"	 

/** @brief 全局笔状态结构体 */
Pen_Holder Pen_Point;

/** @brief 默认触摸方向：X/Y 与 LCD 一致 */
u8 CMD_RDX=0XD0;
u8 CMD_RDY=0X90;

/*------------------ SPI 模拟通信 ------------------*/

/**
 * @brief 通过 GPIO 模拟 SPI 向 ADS7843 写入 1 字节数据
 * @param num 要发送的 8 位数据（MSB 先发）
 * @details 时钟上升沿锁存数据
 */
void ADS_Write_Byte(u8 num)    
{  
	u8 count=0;   
	for(count=0;count<8;count++)  
	{ 	  
		if(num&0x80)TDIN=1;  
		else TDIN=0;   
		num<<=1;    
		TCLK=0;
		TCLK=1;//上升沿有效
	} 			    
} 		 

/**
 * @brief 从 ADS7843 读取 ADC 转换结果
 * @param CMD 读取命令（CMD_RDX 或 CMD_RDY）
 * @return 12 位有效 ADC 值（范围 0～4095）
 * @note 转换时间最长 6μs，需延时等待
 */ 
u16 ADS_Read_AD(u8 CMD)	  
{ 	 
	u8 count=0; 	  
	u16 Num=0; 
	TCLK=0;//先拉低时钟 	 
	TCS=0; //选中触摸屏IC
	ADS_Write_Byte(CMD);//发送命令字
	delay_us(6);//ADS7846的转换时间最长为6us
	TCLK=1;//给1个时钟，清除BUSY    
	TCLK=0; 	 
	for(count=0;count<15;count++)  
	{ 				  
		Num<<=1; 	 
		TCLK=1;			    
		TCLK=0; //下降沿有效  	    	
		if(DOUT)Num++; 		 
	}  	
	Num>>=3;   //只有高12位有效.
	TCS=1;	   //释放片选	 
	return(Num); 
}

/*------------------ 坐标读取与滤波 ------------------*/

/**
 * @brief 多次采样并滤波后返回单轴坐标值
 * @param xy 读取指令（CMD_RDX / CMD_RDY）
 * @return 滤波后的 ADC 值
 *
 * @details
 * - 连续采样 READ_TIMES 次
 * - 升序排序后丢弃 LOST_VAL 个最小值和最大值
 * - 返回剩余数据的平均值
 *
 * @note 当前配置：READ_TIMES=1, LOST_VAL=0（无滤波），可根据需求调整
 */
#define READ_TIMES 1 //读取次数
#define LOST_VAL 0	 //丢弃值
u16 ADS_Read_XY(u8 xy)
{
	u16 i, j;
	u16 buf[READ_TIMES];
	u16 sum=0;
	u16 temp;
	for(i=0;i<READ_TIMES;i++)
	{				 
		buf[i]=ADS_Read_AD(xy);	    
	}				    
	for(i=0;i<READ_TIMES-1; i++)//排序
	{
		for(j=i+1;j<READ_TIMES;j++)
		{
			if(buf[i]>buf[j])//升序排列
			{
				temp=buf[i];
				buf[i]=buf[j];
				buf[j]=temp;
			}
		}
	}	  
	sum=0;
	for(i=LOST_VAL;i<READ_TIMES-LOST_VAL;i++)sum+=buf[i];
	temp=sum/(READ_TIMES-2*LOST_VAL);
	return temp;   
} 

/**
 * @brief 读取一次原始坐标（X, Y）
 * @param x 指向 X 坐标存储地址
 * @param y 指向 Y 坐标存储地址
 * @return 0: 失败（坐标 < 100）；1: 成功
 * @note 坐标小于 100 视为无效（防抖）
 */
u8 Read_ADS(u16 *x,u16 *y)
{
	u16 xtemp,ytemp;			 	 		  
	xtemp=ADS_Read_XY(CMD_RDX);
	ytemp=ADS_Read_XY(CMD_RDY);	  												   
	if(xtemp<100||ytemp<100)return 0;//读数失败
	*x=xtemp;
	*y=ytemp;
	return 1;//读数成功
}	

/**
 * @brief 双次采样校验读取坐标（提高准确性）
 * @param x 指向 X 坐标存储地址
 * @param y 指向 Y 坐标存储地址
 * @return 0: 失败；1: 成功
 *
 * @note 当前实现仅单次采样（原双采样逻辑被注释），
 *       若需启用，请取消注释并设置 ERR_RANGE（如 50）
 */
#define ERR_RANGE 50 ///< 允许的最大两次采样偏差
u8 Read_ADS2(u16 *x,u16 *y) 
{
	u16 x1,y1;
 	// u16 x2,y2;
 	u8 flag;    
    flag=Read_ADS(&x1,&y1);
    *x=x1;
    *y=y1;
    if(flag==0)return(0);
    return 1;
    /*
    flag=Read_ADS(&x2,&y2);	   
    if(flag==0)return(0);   
    if(((x2<=x1&&x1<x2+ERR_RANGE)||(x1<=x2&&x2<x1+ERR_RANGE))//前后两次采样在+-50内
    &&((y2<=y1&&y1<y2+ERR_RANGE)||(y1<=y2&&y2<y1+ERR_RANGE)))
    {
        *x=(x1+x2)/2;
        *y=(y1+y2)/2;
        return 1;
    }else return 0;	  
    */
} 

/**
 * @brief 读取一次触摸坐标（阻塞直到松开）
 * @return 0: 超时（>2.5s）；1: 有效触摸
 * @details 关闭 PEN 中断，防止重复触发
 */			   
u8 Read_TP_Once(void)
{
	u8 t=0;	    
	Pen_Int_Set(0);//关闭中断
	Pen_Point.Key_Sta=Key_Up;
	Read_ADS2(&Pen_Point.X,&Pen_Point.Y);
	while(PEN==0&&t<=250)
	{
		t++;
		//delay_ms(10);
		delay_ms(1);
	};
	Pen_Int_Set(1);//开启中断		 	 
	if(t>=250)return 0;//按下2.5s 认为无效
	else return 1;	
}

/*------------------ LCD 辅助绘图函数 ------------------*/

/**
 * @brief 在 LCD 上绘制校准十字光标
 * @param x 横坐标
 * @param y 纵坐标
 * @note 用于四点校准时显示目标位置
 */
void Drow_Touch_Point(u8 x,u16 y)
{
	LCD_DrawLine(x-12,y,x+13,y);//横线
	LCD_DrawLine(x,y-12,x,y+13);//竖线
	LCD_DrawPoint(x+1,y+1);
	LCD_DrawPoint(x-1,y+1);
	LCD_DrawPoint(x+1,y-1);
	LCD_DrawPoint(x-1,y-1);
	Draw_Circle(x,y,6);//画中心圈
}	  

/**
 * @brief 绘制 2×2 像素的大点（用于触摸反馈）
 * @param x 横坐标
 * @param y 纵坐标
 */   
void Draw_Big_Point(u8 x,u16 y)
{	    
	LCD_DrawPoint(x,y);//中心点 
	LCD_DrawPoint(x+1,y);
	LCD_DrawPoint(x,y+1);
	LCD_DrawPoint(x+1,y+1);	 	  	
}

/*------------------ 坐标转换与中断处理 ------------------*/

/**
 * @brief 将原始 ADC 值转换为 LCD 像素坐标
 * @details 使用校准参数 xfac/xoff/yfac/yoff 进行线性映射
 */
void Convert_Pos(void)
{		 	  
	if(Read_ADS2(&Pen_Point.X,&Pen_Point.Y))
	{
		Pen_Point.X0=Pen_Point.xfac*Pen_Point.X+Pen_Point.xoff;
		Pen_Point.Y0=Pen_Point.yfac*Pen_Point.Y+Pen_Point.yoff;  
	}
}	   

/**
 * @brief PEN 中断服务函数（PA11 下降沿触发）
 * @details 标记按键状态为按下，并清除中断标志
 */
void EXTI15_10_IRQHandler(void)
{ 		   			 
	Pen_Point.Key_Sta=Key_Down;//按键按下  		 			 
	EXTI->PR=1<<11;  //清除LINE11上的中断标志位 
} 

/**
 * @brief 使能/禁用 PEN 中断
 * @param en 1: 使能；0: 禁用
 */ 
void Pen_Int_Set(u8 en)
{
	if(en)EXTI->IMR|=1<<11;   //开启line11上的中断	  		  	
	else EXTI->IMR&=~(1<<11); //关闭line11上的中断	   
}	  

/*------------------ 校准与调试辅助 ------------------*/

/**
 * @brief 在 LCD 上显示校准调试信息
 * @param str 提示字符串（如 "ver fac is:"）
 */
void ADJ_INFO_SHOW(char *str)
{
	LCD_ShowString(40,40,"x1:       y1:       ");
	LCD_ShowString(40,60,"x2:       y2:       ");
	LCD_ShowString(40,80,"x3:       y3:       ");
	LCD_ShowString(40,100,"x4:       y4:       ");
 	LCD_ShowString(40,100,"x4:       y4:       ");
 	LCD_ShowString(40,120,str);					   
}
	 
/**
 * @brief 执行四点触摸屏校准
 * @details
 * 1. 用户依次点击四个角点：(20,20), (220,20), (20,300), (220,300)
 * 2. 验证四边形几何一致性（对边相等、对角线相等）
 * 3. 计算校准系数 xfac, xoff, yfac, yoff
 * 4. 若校准失败或方向错误，自动翻转 X/Y 并重试
 */
void Touch_Adjust(void)
{								 
	signed short pos_temp[4][2];//坐标缓存值
	u8  cnt=0;	
	u16 d1,d2;
	u32 tem1,tem2;
	float fac; 	   
	cnt=0;				
	POINT_COLOR=BLUE;
	BACK_COLOR =WHITE;
	//LCD_Clear(WHITE);//清屏   
	POINT_COLOR=RED;//红色 
	LCD_Clear(WHITE);//清屏 
	Drow_Touch_Point(20,20);//画点1 
	Pen_Point.Key_Sta=Key_Up;//消除触发信号 
	Pen_Point.xfac=0;//xfac用来标记是否校准过,所以校准之前必须清掉!以免错误	 	 
	while(1)
	{
		if(Pen_Point.Key_Sta==Key_Down)//按键按下了
		{
			if(Read_TP_Once())//得到单次按键值
			{  								   
				pos_temp[cnt][0]=Pen_Point.X;
				pos_temp[cnt][1]=Pen_Point.Y;
				cnt++;
			}			 
			switch(cnt)
			{			   
				case 1:
					LCD_Clear(WHITE);//清屏 
					Drow_Touch_Point(220,20);//画点2
					break;
				case 2:
					LCD_Clear(WHITE);//清屏 
					Drow_Touch_Point(20,300);//画点3
					break;
				case 3:
					LCD_Clear(WHITE);//清屏 
					Drow_Touch_Point(220,300);//画点4
					break;
				case 4:	 //全部四个点已经得到
	    		    //对边相等
					tem1=abs(pos_temp[0][0]-pos_temp[1][0]);//x1-x2
					tem2=abs(pos_temp[0][1]-pos_temp[1][1]);//y1-y2
					tem1*=tem1;
					tem2*=tem2;
					d1=sqrt(tem1+tem2);//得到1,2的距离
					
					tem1=abs(pos_temp[2][0]-pos_temp[3][0]);//x3-x4
					tem2=abs(pos_temp[2][1]-pos_temp[3][1]);//y3-y4
					tem1*=tem1;
					tem2*=tem2;
					d2=sqrt(tem1+tem2);//得到3,4的距离
					fac=(float)d1/d2;
					if(fac<0.92||fac>1.07||d1==0||d2==0)//不合格
					{
						cnt=0;
						LCD_Clear(WHITE);//清屏 
						Drow_Touch_Point(20,20);
						ADJ_INFO_SHOW("ver fac is:");   
						LCD_ShowNum(40+24,40,pos_temp[0][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,40,pos_temp[0][1],4,16);	//显示数值
						LCD_ShowNum(40+24,60,pos_temp[1][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,60,pos_temp[1][1],4,16);	//显示数值
						LCD_ShowNum(40+24,80,pos_temp[2][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,80,pos_temp[2][1],4,16);	//显示数值
						LCD_ShowNum(40+24,100,pos_temp[3][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,100,pos_temp[3][1],4,16);	//显示数值
						//扩大100倍显示
						LCD_ShowNum(40,140,fac*100,3,16);//显示数值,该数值必须在95~105范围之内.
						continue;
					}
					tem1=abs(pos_temp[0][0]-pos_temp[2][0]);//x1-x3
					tem2=abs(pos_temp[0][1]-pos_temp[2][1]);//y1-y3
					tem1*=tem1;
					tem2*=tem2;
					d1=sqrt(tem1+tem2);//得到1,3的距离
					
					tem1=abs(pos_temp[1][0]-pos_temp[3][0]);//x2-x4
					tem2=abs(pos_temp[1][1]-pos_temp[3][1]);//y2-y4
					tem1*=tem1;
					tem2*=tem2;
					d2=sqrt(tem1+tem2);//得到2,4的距离
					fac=(float)d1/d2;
					if(fac<0.92||fac>1.07)//不合格
					{
						cnt=0;
						LCD_Clear(WHITE);//清屏 
						Drow_Touch_Point(20,20);
						ADJ_INFO_SHOW("hor fac is:");   
						LCD_ShowNum(40+24,40,pos_temp[0][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,40,pos_temp[0][1],4,16);	//显示数值
						LCD_ShowNum(40+24,60,pos_temp[1][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,60,pos_temp[1][1],4,16);	//显示数值
						LCD_ShowNum(40+24,80,pos_temp[2][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,80,pos_temp[2][1],4,16);	//显示数值
						LCD_ShowNum(40+24,100,pos_temp[3][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,100,pos_temp[3][1],4,16);	//显示数值
						//扩大100倍显示
						LCD_ShowNum(40,140,fac*100,3,16);//显示数值,该数值必须在95~105范围之内.
 						continue;
					}//正确了
								   
					//对角线相等
					tem1=abs(pos_temp[1][0]-pos_temp[2][0]);//x1-x3
					tem2=abs(pos_temp[1][1]-pos_temp[2][1]);//y1-y3
					tem1*=tem1;
					tem2*=tem2;
					d1=sqrt(tem1+tem2);//得到1,4的距离
	
					tem1=abs(pos_temp[0][0]-pos_temp[3][0]);//x2-x4
					tem2=abs(pos_temp[0][1]-pos_temp[3][1]);//y2-y4
					tem1*=tem1;
					tem2*=tem2;
					d2=sqrt(tem1+tem2);//得到2,3的距离
					fac=(float)d1/d2;
					if(fac<0.92||fac>1.07)//不合格
					{
						cnt=0;
						LCD_Clear(WHITE);//清屏 
						Drow_Touch_Point(20,20);
						ADJ_INFO_SHOW("dia fac is:");   
						LCD_ShowNum(40+24,40,pos_temp[0][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,40,pos_temp[0][1],4,16);	//显示数值
						LCD_ShowNum(40+24,60,pos_temp[1][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,60,pos_temp[1][1],4,16);	//显示数值
						LCD_ShowNum(40+24,80,pos_temp[2][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,80,pos_temp[2][1],4,16);	//显示数值
						LCD_ShowNum(40+24,100,pos_temp[3][0],4,16);		//显示数值
						LCD_ShowNum(40+24+80,100,pos_temp[3][1],4,16);	//显示数值
						//扩大100倍显示
						LCD_ShowNum(40,140,fac*100,3,16);//显示数值,该数值必须在95~105范围之内.
						continue;
					}//正确了
					//计算结果
					Pen_Point.xfac=(float)200/(pos_temp[1][0]-pos_temp[0][0]);//得到xfac		 
					Pen_Point.xoff=(240-Pen_Point.xfac*(pos_temp[1][0]+pos_temp[0][0]))/2;//得到xoff
						  
					Pen_Point.yfac=(float)280/(pos_temp[2][1]-pos_temp[0][1]);//得到yfac
					Pen_Point.yoff=(320-Pen_Point.yfac*(pos_temp[2][1]+pos_temp[0][1]))/2;//得到yoff  

					if(abs(Pen_Point.xfac)>2||abs(Pen_Point.yfac)>2)//触屏和预设的相反了.
					{
						cnt=0;
						LCD_Clear(WHITE);//清屏 
						Drow_Touch_Point(20,20);
						LCD_ShowString(35,110,"TP Need readjust!");
						Pen_Point.touchtype=!Pen_Point.touchtype;//修改触屏类型.
						if(Pen_Point.touchtype)//X,Y方向与屏幕相反
						{
							CMD_RDX=0X90;
							CMD_RDY=0XD0;	 
						}else				   //X,Y方向与屏幕相同
						{
							CMD_RDX=0XD0;
							CMD_RDY=0X90;	 
						}
						//delay_ms(500);
						delay_ms(5);
						continue;
					}
					POINT_COLOR=BLUE;
					LCD_Clear(WHITE);//清屏
					LCD_ShowString(35,110,"Touch Screen Adjust OK!");//校正完成
					//delay_ms(500);
					delay_ms(5);
					LCD_Clear(WHITE);//清屏   
					return;//校正完成				 
			}
		}
	} 
}		    

/*------------------ 触摸屏初始化 ------------------*/

/**
 * @brief 初始化触摸屏硬件与中断
 * @details
 * - 配置 SPI 模拟引脚（PA5/6/7/12）
 * - 配置 PEN 中断（PA11，下降沿触发）
 * - 设置默认校准参数（可被 Touch_Adjust 覆盖）
 */
void Touch_Init(void)
{	
	NVIC_InitTypeDef NVIC_InitStructure;  //中断
	GPIO_InitTypeDef GPIO_InitStructure;	//GPIO
	EXTI_InitTypeDef EXTI_InitStructure;	//外部中断线		    		   
	//注意,时钟使能之后,对GPIO的操作才有效
	//所以上拉之前,必须使能时钟.才能实现真正的上拉输出
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA  | RCC_APB2Periph_AFIO, ENABLE);
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7|GPIO_Pin_5|GPIO_Pin_12;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;  //推挽输出 
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);	

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11|GPIO_Pin_6;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU ;  //上拉输入
	GPIO_Init(GPIOA, &GPIO_InitStructure);
 	   
 	Pen_Point.X = 3736;
 	Pen_Point.Y = 3851;
 	Read_ADS(&Pen_Point.X,&Pen_Point.Y);//第一次读取初始化	
			 
	NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn; //使能按键所在的外部中断通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; //先占优先级2级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; //从优先级0级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; //使能外部中断通道
	NVIC_Init(&NVIC_InitStructure); //根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器 

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);	  
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource11); 
	  
	EXTI_InitStructure.EXTI_Line = EXTI_Line11;	//外部线路EXIT1
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;			//设外外部中断模式:EXTI线路为中断请求
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;  //外部中断触发沿选择:设置输入线路下降沿为中断请求
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;		//使能外部中断新状态
	EXTI_Init(&EXTI_InitStructure);		//根据EXTI_InitStruct中指定的参数初始化外设EXTI寄存器
	
	Pen_Point.xfac = 0.058617;
    Pen_Point.yfac = 0.077864;
    Pen_Point.xoff = 0;
    Pen_Point.yoff = 0; 
 
}

