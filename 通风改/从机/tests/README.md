# 从机电脑侧测试约定

- `test_*.c` 测试真实 BME 驱动、I2C 恢复流程、裸机采样与 LoRa 服务，以及真实声学 PCM/RMS 算法；`stubs/` 为电脑侧 HAL 替身，不能加入 Keil 工程。
- `test_slave_servo_test.c` 使用假 PWM 接口运行真实历史定位状态机和连续旋转型业务定时控制。业务测试覆盖停止脉宽启动、300 ms边界、到期只写一次停止脉宽、同方向在途重复不续时、反方向覆盖重新计时、停止后可重新动作、tick回绕/长停顿以及启动/运行/自动停止失败的停机保护；旧自动序列、20 ms限速、停留和独立模式仍回归。不证明真实PWM波形、精确转动时长、停止点或机械角度。
- `test_sg90_standalone_test.c` 使用假 `HAL_Delay` 与 PWM 接口运行真实独立单轮动作，覆盖重复往返、每步先等 2 秒的调用顺序、1300–1700 us 范围、四个位置的更新失败立即返回，以及替身时间计数跨越回绕。PWM 只由 `main` 启动一次，单轮动作不启动、停机或重试；测试不验证 `main` 硬件初始化、真实 HAL 延时、PWM 波形或实物运动。
- 测试结果只证明软件行为，不能代替实际 BME 应答、无线链路、上拉/供电、麦克风时钟/位对齐验收。
- 声学服务覆盖32条历史、最近1秒左右独立最大值、300 ms失效、故障/不连续/填充错误清空以及失效代次；BME/runtime测试用声学快照替身检查26字节回复、32位声音、发送前过期、重复缓存和动态CRC。通信测试不冒充真实DMA采集测试。
- BME/runtime 测试链接真实舵机业务控制与假 PWM，额外覆盖 `0x11 [1, action]` 的严格地址/载荷校验、ACK 独立槽及优先级、待发重复不执行、忙时不覆盖、20 ms 预加载与 50 ms 接收静默、有限 UART 重试、时间回绕，以及遥测/强制采样和重复缓存不被窗口命令破坏。ACK 仅表示软件 PWM 设定，不代表机械到位。
- 编译输出放 `build/`（Git 忽略），不提交临时程序、HEX 或日志。不删除已有文件。
- 使用现有 MinGW；只临时修改当前 PowerShell 进程 PATH，不安装依赖或修改系统配置。

```powershell
New-Item -ItemType Directory -Force build | Out-Null
$taskCompilerPath = $env:PATH
try {
    $env:PATH = 'D:/codeblocks/MinGW/bin;' + $taskCompilerPath
    & 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../App -I../Bsp test_slave_bme280.c ../Bsp/bme280.c ../App/slave_bme280.c ../App/slave_protocol_runtime.c ../App/slave_servo_test.c ../Core/Src/i2c.c -o build/test_slave_bme280.exe
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & './build/test_slave_bme280.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Test failed' }
} finally { $env:PATH = $taskCompilerPath }
```

声学算法测试沿用以上 PATH 与输出约定，在 `tests/` 执行：

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -I../App test_acoustic_processing.c ../App/acoustic_processing.c -o build/test_acoustic_processing.exe
& './build/test_acoustic_processing.exe'
```

声学服务测试使用假 DMA 块/捕获状态，运行真实主循环服务代码，不模拟实际电气时序：

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -I../App -I../Bsp test_slave_acoustic.c ../App/slave_acoustic.c ../App/acoustic_processing.c -o build/test_slave_acoustic.exe
& './build/test_slave_acoustic.exe'
```

SG90 定位状态机测试沿用以上 PATH 与输出约定，在 `tests/` 执行：

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -I../App -I../Bsp test_slave_servo_test.c ../App/slave_servo_test.c -o build/test_slave_servo_test.exe
& './build/test_slave_servo_test.exe'
```

SG90 独立测试模式的单轮动作测试沿用以上 PATH 与输出约定，在 `tests/` 执行；不需要运行通信或声学服务：

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -Istubs -I../App -I../Bsp test_sg90_standalone_test.c ../App/sg90_standalone_test.c -o build/test_sg90_standalone_test.exe
& './build/test_sg90_standalone_test.exe'
```

业务停止点/时长参数可覆写回归：同一套测试验证1490 us停止、250 ms动作，历史自动序列仍按1500 us中位运行。这只验证配置与状态机，不能证明1490 us适合当前实物；实际校准必须空载。

```powershell
& 'D:/codeblocks/MinGW/bin/gcc.exe' -std=c99 -Wall -Wextra -Werror -DSLAVE_SERVO_WINDOW_STOP_US=1490U -DSLAVE_SERVO_WINDOW_RUN_MS=250U -I../App -I../Bsp test_slave_servo_test.c ../App/slave_servo_test.c -o build/test_slave_servo_calibrated.exe
& './build/test_slave_servo_calibrated.exe'
```
