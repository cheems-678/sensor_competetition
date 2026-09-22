# 主机 v4 架构

主机只保留两条业务链路：LoRa 单帧转发和四路 PWM 风机控制。

```text
USART2 ISR -> 环形缓冲 -> LoRaTask -> q_master_event -> MasterRuntime
                                                        |          |
                                                   q_lora_tx    FanPwm
                                                        |          |
                                                     LoRaTask   PA1/PB1/PB8/PB9
```

- 固定组号：`1`。
- `defaultTask`：处理读遥测、从机返回、超时和风机命令。
- `LoRaTask`：负责 USART2 的 LoRa 流式拆帧和完整帧发送。
- `FanPwm`：四路独立高有效 PWM，25 kHz，0–100%。
- DGUS、USART1 屏幕、USART3 Modbus、TD710 自动控制和主机 BME 任务已移除。
- 当前 18 字节遥测区只允许无效占位值，详见 `通风改/PROTOCOL_V4.md`。
