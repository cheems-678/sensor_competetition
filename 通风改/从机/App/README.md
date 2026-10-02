# 从机 BME 与通信约定

- `slave_protocol_runtime.*`：v4 拆帧、请求匹配和有界延迟回复；`slave_bme280.*`：采样状态机及 RAM 诊断。硬件驱动在 `Bsp/bme280.*`，I2C1 初始化与恢复在现有 `Core/Src/i2c.c`。不恢复 RTOS，不使用通信串口打印调试文本。
- PB6=SCL、PB7=SDA，硬件 I2C1 100 kHz、开漏、上拉至 3.3 V；CSB 必须为高，SDO 决定 0x76/0x77。仅接受 BME280 ID=0x60，不将没有湿度的 BMP280 视为成功。
- 每秒强制模式 ×1 温湿压、滤波关闭；触发后主循环等待 10 ms 再读取，最多等待 50 ms。HAL I²C 每次操作超时 10 ms；总线恢复包含有界 9 个 SCL 脉冲和 ST ES096 模拟滤波器 BUSY 恢复序列，失败每秒重试。初始化失败不进入 Error_Handler 死循环。
- `SlaveBme280Diag` 保存地址、ID、错误、校准状态、采样计数和更新时间。失败立即失效，样本满 2 秒无效；重新接入自动恢复。
- 从机仅回答本组主机的 v4 0x01 请求，参数 0 读缓存，1 等待一次新测量（失败也结束并回复无效值）。回复 source=3/group=1、destination=2/group=1，流水号原样回显，18 字节数据区：flags=0，温湿压在 1–8，9–17 保留旧无效占位。主机聚合时将从机 1–8 放到上行 9–16，不覆盖主机 BME。
- 从机不主动广播；回复前至少静默 50 ms，转换等待不阻塞接收；重复请求缓存最多 2 秒且区分参数。声学与雨滴本轮不实现。
- 排队或重复发送时再次检查样本更新时间；发送前已失效/过期的快照改成占位值并重新计算 CRC，不能靠重复请求延长数据寿命。
- 验证：主从分别 Keil 构建；电脑侧测试放 `tests/`，不加入固件。交付 HEX 由用户烧录，再检查 ACK、ID、校准、连续采样、断开恢复和五分钟真实链路运行。
- 本目录只放业务源码、头文件及约定；不放 HEX、串口日志、临时下载或测试产物。

## 本轮交付与实板验收

- 主机、从机最新 HEX 分别由各自 `MDK-ARM/LoraSlaveV1.0.uvprojx` 构建，输出在各自 `MDK-ARM/LoraSlaveV1.0/LoraSlaveV1.hex`。由用户烧录两块板，控制室不重烧。不要误用另一个目录的同名 HEX。
- 上位机运行更新后的 `通风改/上位机/main.py`；旧打包 EXE 未在本轮更新，不用旧界面验收双 BME。
- 供电后检查 `SlaveBme280Diag`：`chip_id=0x60`、`address_7bit=0x76/0x77`、`calibration_valid=1`、`sample_success_count` 连续增长。异常查看 `last_error`（HAL 状态 0–3，4=量程）和 `i2c_error`。
- 检查 `SlaveRuntimeDiag.request_count/reply_count`、`MasterRuntimeDiag.slave_response_match_count`；上位机应显示六项数据和从机在线。flags=7 但从机三项 `--` 表示无线应答正常、BME 样本无效；flags=3 表示没有匹配到从机应答。
- 断开从机电源：主机三项继续显示，从机离线；恢复电源后自动恢复。断开从机 BME：链路仍在线，但从机三项无效；重新接入后自动重试恢复。
- 连续运行至少五分钟，人工操作风机1/PB1、风机2/PB8，检查 ACK、六项遥测和 PB13 闪烁。电脑侧虚拟五分钟测试不能替代此项。
- 本轮尚未完成实板烧录后的应答、采样和持续运行验收；声学与雨滴必须等 BME 链路验证成功后再接入。

恢复流程依据 [ST ES096 §2.8.7](https://www.st.com/resource/en/errata_sheet/es096-stm32f101x8b-stm32f102x8b-and-stm32f103x8b-mediumdensity-device-limitations-stmicroelectronics.pdf)，补偿公式沿用参考工程的 Bosch 整数算法并修正负数左移和中间量溢出风险。
