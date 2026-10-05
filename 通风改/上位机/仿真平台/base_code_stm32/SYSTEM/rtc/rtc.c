/**
 * @file    rtc.c
 * @brief   STM32F10x 实时时钟（RTC）驱动实现
 *
 * 本文件实现基于 LSE（32.768 kHz 外部晶振）的 RTC 驱动，提供：
 * - 时间设置（年/月/日/时/分/秒）
 * - 自动从编译时间初始化（用于开发阶段）
 * - 秒中断更新全局时间结构体 `timer`
 * - 闰年判断与星期计算
 *
 * @note
 * - 时间基准：Unix 时间戳（1970-01-01 00:00:00 UTC）
 * - 支持年份范围：1970 ～ 2099
 * - 使用 BKP_DR1 判断是否首次配置（当前未启用，可扩展）
 * - 默认启用秒中断，自动更新 `timer` 全局变量
 *
 * @warning
 * - 必须外接 32.768 kHz 晶振（LSE），否则无法正常工作；
 * - 仿真时使用 PSC=327（≈100 Hz）加速，**真机必须改为 32767**；
 * - RTC 一旦配置，后备寄存器断电仍保持数据（需 VBAT 供电）。
 */

#include "sys.h"
#include "rtc.h" 
#include "delay.h"

/*------------------ 全局变量 ------------------*/

/**
 * @brief 全局时间结构体，由 RTC 中断自动更新
 * @details 包含年、月、日、时、分、秒、星期
 */
tm timer;

/*------------------ 静态常量 ------------------*/

/** 平年每月天数 */										 
u8 const table_week[12]={0,3,3,6,1,4,6,2,5,0,3,5}; //月修正数据表	  
/** 月修正表（用于星期计算） */
const u8 mon_table[12]={31,28,31,30,31,30,31,31,30,31,30,31};

/*------------------ 中断配置 ------------------*/

/**
 * @brief 配置 RTC 全局中断优先级
 * @details
 * - 中断通道：RTC_IRQn
 * - 抢占优先级：1，子优先级：0
 * - 使能后，秒中断和闹钟中断均可触发
 */
void RTC_NVIC_Config(void)
{	
    NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = RTC_IRQn;		//RTC全局中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;	//先占优先级1位,从优先级3位
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;	//先占优先级0位,从优先级4位
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;		//使能该通道中断
	NVIC_Init(&NVIC_InitStructure);		//根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器
}

/*------------------ RTC 初始化 ------------------*/

/**
 * @brief 初始化 RTC（使用 LSE 作为时钟源）
 * @return
 *   - 0: 成功
 *   - 1: LSE 晶振未就绪（超时）
 * @details
 * 1. 使能 PWR/BKP 时钟，允许访问后备区；
 * 2. 启用 LSE 并等待就绪（最多 2.5 秒）；
 * 3. 配置 RTC 时钟源为 LSE；
 * 4. 设置预分频器（仿真用 327，真机应为 32767）；
 * 5. 设置默认时间（2022-03-23 16:12:55）；
 * 6. 使能秒中断，自动更新 `timer`。
 *
 * @note
 * - **真机部署时必须将 PSC 改为 32767**（32768 分频 → 1 Hz）；
 * - 当前未使用 BKP_DR1 判断首次配置，每次上电都会重设时间；
 * - 若需“仅首次配置”，可检查 BKP->DR1 标志位。
 */
u8 RTC_Init(void)
{
    u8 temp=0;
    RTC_NVIC_Config();
			
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);	//使能PWR和BKP外设时钟   
    PWR_BackupAccessCmd(ENABLE);	//使能RTC和后备寄存器访问 
    BKP_DeInit();	//将外设BKP的全部寄存器重设为缺省值 	
    RCC_LSEConfig(RCC_LSE_ON);	//设置外部低速晶振(LSE),使用外设低速晶振
    while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET)	//检查指定的RCC标志位设置与否,等待低速晶振就绪
    {
    temp++;
    delay_ms(10);
    }
    if(temp>=250)return 1;//初始化时钟失败,晶振有问题	    
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);		//设置RTC时钟(RTCCLK),选择LSE作为RTC时钟    
    RCC_RTCCLKCmd(ENABLE);	//使能RTC时钟  
    RTC_WaitForSynchro();	// 设置 RCC 后，必须进行此操作
    RTC_WaitForLastTask();	//等待最近一次对RTC寄存器的写操作完成
    RTC_ITConfig(RTC_IT_SEC, ENABLE);		//使能RTC秒中断
    RTC_WaitForLastTask();	//等待最近一次对RTC寄存器的写操作完成
    
    
	// RTC_SetPrescaler(32767); // 在真机验证时，应设置RTC预分频的值为 32767
    RTC_SetPrescaler(327);      // 仿真时为了加快速度，设置RTC预分频的值为 327
    RTC_WaitForLastTask();	    // 等待最近一次对RTC寄存器的写操作完成
    RTC_Set(2022,3,23,16,12,55);  // 设置时间为 2022年3月23日 16时12分55秒	  
	    				     
	RTC_Get();//更新时间	
	return 0; //ok
}


/*------------------ RTC 中断服务函数 ------------------*/

/**
 * @brief RTC 全局中断服务函数
 * @details
 * - 处理秒中断：更新全局时间 `timer`
 * - 处理闹钟中断：仅清除标志（用户可扩展）
 * @note 必须清除对应中断挂起位，否则会重复进入中断。
 */
void RTC_IRQHandler(void)
{
	if( RTC_GetITStatus(RTC_IT_SEC) )
	{
		RTC_ClearITPendingBit(RTC_IT_SEC);
		RTC_Get();
	}
	if( RTC_GetITStatus(RTC_IT_ALR) )
	{
		RTC_ClearITPendingBit(RTC_IT_ALR);
	}  							 	   	 
}
 

/*------------------ 日期时间工具函数 ------------------*/

/**
 * @brief 判断指定年份是否为闰年
 * @param year 年份（1970～2099）
 * @return 1: 闰年；0: 平年
 */
u8 Is_Leap_Year(u16 year)
{			  
	if(year%4==0) //必须能被4整除
		{ 
		if(year%100==0) 
			{ 
			if(year%400==0)return 1;//如果以00结尾,还要能被400整除 	   
			else return 0;   
			}else return 1;   
		}else return 0;	
}	
 			   
/**
 * @brief 将公历日期转换为 Unix 时间戳并写入 RTC 计数器
 * @param syear 年（1970～2099）
 * @param smon  月（1～12）
 * @param sday  日（1～31）
 * @param hour  时（0～23）
 * @param min   分（0～59）
 * @param sec   秒（0～59）
 * @return 0: 成功；1: 年份非法
 */
u8 RTC_Set(u16 syear,u8 smon,u8 sday,u8 hour,u8 min,u8 sec)
{
	u16 t;
	u32 seccount=0;
	if(syear<1970||syear>2099)return 1;	   
	for(t=1970;t<syear;t++)	//把所有年份的秒钟相加
		{
		if(Is_Leap_Year(t))seccount+=31622400;//闰年的秒钟数
		else seccount+=31536000;			  //平年的秒钟数
		}
	smon-=1;
	for(t=0;t<smon;t++)	   //把前面月份的秒钟数相加
		{
		seccount+=(u32)mon_table[t]*86400;//月份秒钟数相加
		if(Is_Leap_Year(syear)&&t==1)seccount+=86400;//闰年2月份增加一天的秒钟数	   
		}
	seccount+=(u32)(sday-1)*86400;//把前面日期的秒钟数相加 
	seccount+=(u32)hour*3600;//小时秒钟数
	seccount+=(u32)min*60;	 //分钟秒钟数
	seccount+=sec;//最后的秒钟加上去

	RTC_WaitForLastTask();	//等待最近一次对RTC寄存器的写操作完成

	RTC_SetCounter(seccount);	//设置RTC计数器的值

	RTC_WaitForLastTask();	//等待最近一次对RTC寄存器的写操作完成  	
	return 0;	    
}

/**
 * @brief 从 RTC 计数器读取时间并更新全局 `timer`
 * @return 0: 成功
 * @note 该函数被秒中断自动调用，也可手动调用刷新时间。
 */
u8 RTC_Get(void)
{
	static u16 daycnt=0;
	u32 timecount=0; 
	u32 temp=0;
	u16 temp1=0;	  
	   
	timecount=RTC->CNTH;//得到计数器中的值(秒钟数)
	timecount<<=16;
	timecount+=RTC->CNTL;			 

	temp=timecount/86400;   //得到天数(秒钟数对应的)
	if(daycnt!=temp)//超过一天了
	{	  
		daycnt=temp;
		temp1=1970;	//从1970年开始
		while(temp>=365)
		{				 
			if(Is_Leap_Year(temp1))//是闰年
			{
				if(temp>=366)temp-=366;//闰年的秒钟数
				else {temp1++;break;}  
			}
			else temp-=365;	  //平年 
			temp1++;  
		}   
		timer.w_year=temp1;//得到年份
		temp1=0;
		while(temp>=28)//超过了一个月
		{
			if(Is_Leap_Year(timer.w_year)&&temp1==1)//当年是不是闰年/2月份
			{
				if(temp>=29)temp-=29;//闰年的秒钟数
				else break; 
			}
			else 
			{
				if(temp>=mon_table[temp1])temp-=mon_table[temp1];//平年
				else break;
			}
			temp1++;  
		}
		timer.w_month=temp1+1;//得到月份
		timer.w_date=temp+1;  //得到日期 
	}
	temp=timecount%86400;     //得到秒钟数   	   
	timer.hour=temp/3600;     //小时
	timer.min=(temp%3600)/60; //分钟	
	timer.sec=(temp%3600)%60; //秒钟
	timer.week=RTC_Get_Week(timer.w_year,timer.w_month,timer.w_date);//获取星期   
	return 0;
}	 

/**
 * @brief 根据公历日期计算星期（Zeller 公式简化版）
 * @param year  年（1901～2099）
 * @param month 月（1～12）
 * @param day   日（1～31）
 * @return 星期（0=Sunday, 1=Monday, ..., 6=Saturday）
 */																						 
u8 RTC_Get_Week(u16 year,u8 month,u8 day)
{	
	u16 temp2;
	u8 yearH,yearL;
	
	yearH=year/100;	yearL=year%100; 
	// 如果为21世纪,年份数加100  
	if (yearH>19)yearL+=100;
	// 所过闰年数只算1900年之后的  
	temp2=yearL+yearL/4;
	temp2=temp2%7; 
	temp2=temp2+day+table_week[month-1];
	if (yearL%4==0&&month<3)temp2--;
	return(temp2%7);
} 

/*------------------ 编译时间自动设置（调试用） ------------------*/

/**
 * @brief 字符串前 len 字节比较
 * @param s1/s2 待比较字符串
 * @param len   比较长度
 * @return 1: 相等；0: 不等
 * @note 仅用于 Auto_Time_Set，非通用函数
 */
u8 str_cmpx(u8*s1,u8*s2,u8 len)
{
	u8 i;
	for(i=0;i<len;i++)if((*s1++)!=*s2++)return 0;
	return 1;	   
}

// 由编译器自动生成
extern const u8 *COMPILED_DATE;//获得编译日期
extern const u8 *COMPILED_TIME;//获得编译时间

/** 月份字符串映射表 */
const u8 Month_Tab[12][3]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}; 

/**
 * @brief 自动将 RTC 时间设置为编译时间（仅用于开发调试）
 * @details
 * - 解析 __DATE__ 和 __TIME__ 宏生成的字符串；
 * - 转换为年月日时分秒并写入 RTC；
 * @note 发布版本应禁用此功能，改用用户设置或网络时间。
 */
void Auto_Time_Set(void)
{
	u8 temp[3];
	u8 i;
	u8 mon,date;
	u16 year;
	u8 sec,min,hour;

    // 解析月份
	for(i=0;i<3;i++)temp[i]=COMPILED_DATE[i];   
	for(i=0;i<12;i++)if(str_cmpx((u8*)Month_Tab[i],temp,3))break;	
	mon=i+1;//得到月份

    // 解析日
	if(COMPILED_DATE[4]==' ')date=COMPILED_DATE[5]-'0'; 
	else date=10*(COMPILED_DATE[4]-'0')+COMPILED_DATE[5]-'0';  

    // 解析年
	year=1000*(COMPILED_DATE[7]-'0')+100*(COMPILED_DATE[8]-'0')+10*(COMPILED_DATE[9]-'0')+COMPILED_DATE[10]-'0';	   

    // 解析时间
	hour=10*(COMPILED_TIME[0]-'0')+COMPILED_TIME[1]-'0';  
	min=10*(COMPILED_TIME[3]-'0')+COMPILED_TIME[4]-'0';  
	sec=10*(COMPILED_TIME[6]-'0')+COMPILED_TIME[7]-'0';  
	RTC_Set(year,mon,date,hour,min,sec)	;
} 

