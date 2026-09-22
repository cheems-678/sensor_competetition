/**
 * @file    hc_sr501.c
 * @brief   HC-SR501 人体红外 (PIR) 传感器驱动实现
 * @note    HC-SR501 是基于热释电红外 (Passive Infrared) 原理的
 *          人体移动检测模块。当人体进入检测区域时, 模块 OUT 引脚
 *          输出高电平; 人离开后延时一段时间自动恢复低电平。
 *          
 *          工作原理:
 *          热释电传感器检测红外辐射变化 → 信号经 BISS0001 芯片
 *          处理 → 输出 TTL 电平信号给 STM32 GPIO
 *          
 *          本驱动支持两种使用方式:
 *          1. 轮询模式: 在主循环中调用 PIR_Read() 读取
 *          2. 中断模式: 调用 PIR_Init_IT() 注册回调,
 *             有人/无人时自动触发 EXTI 中断调用回调
 */
#include "hc_sr501.h"

/* 回调函数指针, 在中断模式下使用 */
static void (*pir_callback)(uint8_t state);

/* EXTI 中断处理函数声明 (在文件尾部实现) */
void EXTI9_5_IRQHandler(void);

/*==================================================================*
 *                        模块初始化                                 *
 *==================================================================*/

/**
 * @brief   初始化 HC-SR501 GPIO (轮询模式)
 * @note    配置 PA7 为浮空输入:
 *          - GPIO_Mode_IN_FLOATING: 引脚电平完全由外部决定
 *          - HC-SR501 OUT 引脚为推挽输出, 直接驱动 STM32 输入
 *          - 不使用内部上拉/下拉, 避免影响外部信号
 *          
 *          上电后 HC-SR501 需要约 30~60 秒稳定,
 *          此期间 PIR_Read() 可能返回 PIR_MOTION, 属于正常现象。
 * @return  PIR_OK  初始化成功
 */
PIRStatus PIR_Init(void)
{
    GPIO_InitTypeDef gpio;

    /* 清空回调指针, 不使用中断模式 */
    pir_callback = 0;

    /* 开启 GPIOA 时钟 (APB2 总线) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* 配置 PA7 为浮空输入: 引脚电平由 HC-SR501 OUT 决定 */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = PIR_PIN;           /* PA7 */
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;  /* 浮空输入 */
    GPIO_Init(PIR_PORT, &gpio);

    return PIR_OK;
}

/**
 * @brief   读取传感器当前状态
 * @return  PIR_MOTION (1) 检测到人体移动 OUT=高电平
 *          PIR_NO_MOTION (0) 无人移动 OUT=低电平
 * @note    通过读取 GPIO 输入数据寄存器 (IDR) 获取引脚电平:
 *          GPIO_ReadInputDataBit() 返回 Bit_SET(1) 或 Bit_RESET(0)
 *          
 *          如果在中断模式下使用, 也可用此函数获取当前状态,
 *          与中断回调互补。
 */
uint8_t PIR_Read(void)
{
    /* 读取 PA7 输入电平, 转换为 PIR_MOTION / PIR_NO_MOTION */
    if (GPIO_ReadInputDataBit(PIR_PORT, PIR_PIN) == Bit_SET) {
        return PIR_MOTION;   /* 高电平 = 检测到人 */
    } else {
        return PIR_NO_MOTION; /* 低电平 = 无人 */
    }
}

/*==================================================================*
 *                       EXTI 中断初始化                             *
 *==================================================================*/

/**
 * @brief   初始化 HC-SR501 并开启 EXTI 中断 (中断模式)
 * @param   cb  回调函数指针, 在中断中调用
 * @return  PIR_OK  初始化成功
 * @note    中断配置步骤:
 *          
 *          1. GPIO 配置 (同 PIR_Init): PA7 浮空输入
 *          2. AFIO 配置: 将 PA7 连接到 EXTI7 信号线
 *          3. EXTI 配置: 使能 Line7, 双边沿触发
 *          4. NVIC 配置: 使能 EXTI9_5_IRQn 中断
 *          
 *          中断触发时机:
 *          - 上升沿 (GPIO LOW→HIGH): 有人进入检测区域
 *            回调参数: PIR_MOTION (1)
 *          - 下降沿 (GPIO HIGH→LOW): 人离开检测区域
 *            回调参数: PIR_NO_MOTION (0)
 *          
 *          注意:
 *          - EXTI Line5~9 共享 EXTI9_5_IRQHandler
 *          - 如果其他模块也使用此中断, 需合并中断处理
 *          - 上电后前 60 秒内可能连续触发, 建议在回调中
 *            做软件去抖 (连续采样多次确认)
 */
PIRStatus PIR_Init_IT(void (*cb)(uint8_t state))
{
    GPIO_InitTypeDef  gpio;
    EXTI_InitTypeDef  exti;
    NVIC_InitTypeDef  nvic;

    /* 保存回调函数指针 */
    pir_callback = cb;

    /* 第一步: GPIO 时钟和配置 (同轮询模式) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = PIR_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(PIR_PORT, &gpio);

    /* 第二步: 开启 AFIO 时钟, 配置 EXTI 中断源 */
    /* AFIO 控制 GPIO 到 EXTI 的映射关系          */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /*
     * GPIO_PortSourceGPIOA + GPIO_PinSource7:
     * 将 PA7 连接到 EXTI7
     * EXTI 模块根据配置的信号线检测上升/下降沿
     */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource7);

    /* 第三步: 配置 EXTI Line7 */
    EXTI_StructInit(&exti);
    exti.EXTI_Line    = EXTI_Line7;           /* EXTI 信号线 7 */
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;  /* 中断模式 (非事件) */
    exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling; /* 双边沿触发 */
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    /* 第四步: 配置 NVIC 中断 */
    /* EXTI9_5_IRQn: EXTI Line5~9 共用此中断入口 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    nvic.NVIC_IRQChannel = EXTI9_5_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 3;
    nvic.NVIC_IRQChannelSubPriority = 2;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    return PIR_OK;
}

/*==================================================================*
 *                      EXTI 中断处理函数                            *
 *==================================================================*/

/**
 * @brief   EXTI Line5~9 中断处理函数
 * @note    中断源: EXTI Line7 (PA7 连接 HC-SR501 OUT)
 *          
 *          触发条件:
 *          - 上升沿: 有人进入, 调用回调(PIR_MOTION)
 *          - 下降沿: 人离开, 调用回调(PIR_NO_MOTION)
 *          
 *          处理流程:
 *          1. 检查 EXTI Line7 是否有中断挂起
 *          2. 读取当前引脚电平判断上升/下降
 *          3. 调用回调函数通知应用层
 *          4. 清除中断挂起标志
 *          
 *          注意:
 *          - 本函数覆盖 startup 启动文件中的 [WEAK] EXTI9_5_IRQHandler
 *          - 如果其他模块也使用此中断, 需在原函数中追加处理
 *          - 中断中应尽快完成处理, 回调函数不宜过长
 */
void EXTI9_5_IRQHandler(void)
{
    /*
     * 判断是否是 EXTI Line7 触发了中断:
     * PR (Pending Register) 位为 1 表示该线有中断请求
     * 虽然本函数仅处理 PA7 中断, 但此判断可防止其他
     * EXTI9_5 中断源 (如 PB5, PC5 等) 误入此处理
     */
    if (EXTI_GetITStatus(EXTI_Line7) != RESET) {

        /*
         * 读取 PA7 当前电平判断触发边沿:
         * - GPIO 为 HIGH = 上升沿触发 = 有人进入
         * - GPIO 为 LOW  = 下降沿触发 = 人离开
         * 
         * 注意: 在中断中 GPIO 电平一定与触发边沿匹配,
         * 因为刚触发的边沿已将引脚驱动到相应电平,
         * 且在中断返回前不会再次变化 (HC-SR501 输出保持)
         */
        uint8_t state = PIR_NO_MOTION;
        if (GPIO_ReadInputDataBit(PIR_PORT, PIR_PIN) == Bit_SET) {
            state = PIR_MOTION;  /* 上升沿: 检测到人 */
        } else {
            state = PIR_NO_MOTION; /* 下降沿: 人离开 */
        }

        /* 如果注册了回调函数, 通知应用层状态变化 */
        if (pir_callback) {
            pir_callback(state);
        }

        /*
         * 清除 EXTI Line7 中断挂起标志:
         * 必须调用, 否则中断会反复触发,
         * CPU 将卡在中断处理函数中无法返回主循环
         */
        EXTI_ClearITPendingBit(EXTI_Line7);
    }
}

